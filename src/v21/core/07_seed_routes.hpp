#pragma once

// Initial-solution constructors. The pipeline runs several of these in the
// seed phase and picks the best by ScalarCost; constructive cost dominates
// only the first ~5% of the search budget, so quality is what matters. Every
// constructor here returns m closed routes (each starts and ends at depot 0)
// and covers all customers exactly once.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace mtsp::v21 {

// Closes any open route by adding/removing depot endpoints as needed.
inline void EnsureClosedDepot(RouteSet& routes) {
    for (auto& r : routes) {
        if (r.empty()) { r = {0, 0}; continue; }
        if (r.front() != 0) r.insert(r.begin(), 0);
        if (r.size() < 2 || r.back() != 0) r.push_back(0);
    }
}

// Round-robin nearest-neighbor over all m agents. O(n^2) — strongest seed for
// small/medium n; matches the legacy 2opt+greed baseline shape.
inline RouteSet BuildRoundRobinNN(const mtsp::Instance& inst) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    std::vector<int> cur(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(n), 0);
    visited[0] = 1;
    int remaining = n - 1;
    while (remaining > 0) {
        for (int s = 0; s < m && remaining > 0; ++s) {
            double best_d = std::numeric_limits<double>::max();
            int best_c = -1;
            for (int c = 1; c < n; ++c) {
                if (!visited[static_cast<size_t>(c)]) {
                    const double dd = inst.Distance(cur[static_cast<size_t>(s)], c);
                    if (dd < best_d) { best_d = dd; best_c = c; }
                }
            }
            if (best_c == -1) continue;
            routes[static_cast<size_t>(s)].push_back(best_c);
            cur[static_cast<size_t>(s)] = best_c;
            visited[static_cast<size_t>(best_c)] = 1;
            --remaining;
        }
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

// kNN-graph-accelerated round-robin nearest-neighbour. O(n*k) instead of O(n^2 / m).
inline RouteSet BuildRoundRobinNNFast(const mtsp::Instance& inst,
                                       const CandidateSets& candidates) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    std::vector<int> cur(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(n), 0);
    visited[0] = 1;
    int remaining = n - 1;
    while (remaining > 0) {
        bool any = false;
        for (int s = 0; s < m && remaining > 0; ++s) {
            int from = cur[static_cast<size_t>(s)];
            int best = -1;
            double bd = std::numeric_limits<double>::max();
            for (int j : candidates[static_cast<size_t>(from)]) {
                if (j == 0) continue;
                if (!visited[static_cast<size_t>(j)]) {
                    const double dd = inst.Distance(from, j);
                    if (dd < bd) { bd = dd; best = j; }
                }
            }
            if (best == -1) {
                // Fall back to scan
                for (int c = 1; c < n; ++c) {
                    if (!visited[static_cast<size_t>(c)]) {
                        const double dd = inst.Distance(from, c);
                        if (dd < bd) { bd = dd; best = c; }
                    }
                }
            }
            if (best == -1) continue;
            routes[static_cast<size_t>(s)].push_back(best);
            cur[static_cast<size_t>(s)] = best;
            visited[static_cast<size_t>(best)] = 1;
            --remaining;
            any = true;
        }
        if (!any) break;
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

// Depot-candidate seeded NN. First, take K nearest customers to the depot
// (K = m or 2m). Each agent makes its first step into this depot neighbourhood:
// with multiplier=1, agent s gets the s-th nearest available depot candidate;
// with multiplier=2, it may jump to the second ring (s+m) with probability
// `spread_probability`. After that, continue the same kNN-accelerated
// round-robin NN fill. This keeps empty routes allowed when n-1 < m.
inline RouteSet BuildDepotCandidateNNSeed(const mtsp::Instance& inst,
                                           const CandidateSets& candidates,
                                           int candidate_multiplier,
                                           double spread_probability,
                                           std::mt19937& rng,
                                           int forced_ring = -1) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    std::vector<int> cur(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(n), 0);
    visited[0] = 1;
    int remaining = n - 1;
    if (remaining <= 0) {
        for (auto& r : routes) r.push_back(0);
        return routes;
    }

    const int mult = std::max(1, candidate_multiplier);
    const int pool_size = std::min(n - 1, std::max(m, mult * m));
    KDTree2D tree(inst.GetCoords());
    std::vector<int> depot_pool;
    tree.Knn(0, pool_size + 4, depot_pool);
    depot_pool.erase(std::remove(depot_pool.begin(), depot_pool.end(), 0), depot_pool.end());
    if (static_cast<int>(depot_pool.size()) > pool_size) depot_pool.resize(static_cast<size_t>(pool_size));

    std::bernoulli_distribution spread(std::clamp(spread_probability, 0.0, 1.0));
    std::vector<char> picked(static_cast<size_t>(n), 0);
    for (int s = 0; s < m && remaining > 0; ++s) {
        int desired = s;
        if (forced_ring >= 0) {
            const int ring = std::min(mult - 1, forced_ring);
            if (s + ring * m < static_cast<int>(depot_pool.size())) {
                desired = s + ring * m;
            }
        } else if (mult >= 2 && s + m < static_cast<int>(depot_pool.size()) && spread(rng)) {
            desired = s + m;
        }
        int chosen = -1;
        if (desired < static_cast<int>(depot_pool.size())) {
            const int c = depot_pool[static_cast<size_t>(desired)];
            if (c > 0 && !visited[static_cast<size_t>(c)] && !picked[static_cast<size_t>(c)]) chosen = c;
        }
        if (chosen < 0) {
            for (int c : depot_pool) {
                if (c > 0 && !visited[static_cast<size_t>(c)] && !picked[static_cast<size_t>(c)]) {
                    chosen = c;
                    break;
                }
            }
        }
        if (chosen < 0) break;
        routes[static_cast<size_t>(s)].push_back(chosen);
        cur[static_cast<size_t>(s)] = chosen;
        visited[static_cast<size_t>(chosen)] = 1;
        picked[static_cast<size_t>(chosen)] = 1;
        --remaining;
    }

    while (remaining > 0) {
        bool any = false;
        for (int s = 0; s < m && remaining > 0; ++s) {
            int from = cur[static_cast<size_t>(s)];
            int best = -1;
            double bd = std::numeric_limits<double>::max();
            for (int j : candidates[static_cast<size_t>(from)]) {
                if (j == 0) continue;
                if (!visited[static_cast<size_t>(j)]) {
                    const double dd = inst.Distance(from, j);
                    if (dd < bd) { bd = dd; best = j; }
                }
            }
            if (best == -1) {
                for (int c = 1; c < n; ++c) {
                    if (!visited[static_cast<size_t>(c)]) {
                        const double dd = inst.Distance(from, c);
                        if (dd < bd) { bd = dd; best = c; }
                    }
                }
            }
            if (best == -1) continue;
            routes[static_cast<size_t>(s)].push_back(best);
            cur[static_cast<size_t>(s)] = best;
            visited[static_cast<size_t>(best)] = 1;
            --remaining;
            any = true;
        }
        if (!any) break;
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

// Angular-sector depot seed. Unlike the depot-2m seed, first steps are spread
// by direction from the depot, so routes start with a genuinely different
// geometric decomposition before the same kNN round-robin fill takes over.
inline RouteSet BuildAngularSectorNNSeed(const mtsp::Instance& inst,
                                         const CandidateSets& candidates,
                                         int pool_multiplier,
                                         double rotation_fraction,
                                         double radial_quantile,
                                         std::mt19937& rng) {
    constexpr double kPi = 3.141592653589793238462643383279502884;
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    const auto& coords = inst.GetCoords();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    std::vector<int> cur(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(n), 0);
    visited[0] = 1;
    int remaining = n - 1;
    if (remaining <= 0) {
        for (auto& r : routes) r.push_back(0);
        return routes;
    }

    struct SectorCity {
        int city = -1;
        double r2 = 0.0;
    };
    std::vector<std::vector<SectorCity>> sectors(static_cast<size_t>(m));
    const double x0 = coords[0].first;
    const double y0 = coords[0].second;
    const double full = 2.0 * kPi;
    const double sector_width = full / static_cast<double>(m);
    const double shift = std::clamp(rotation_fraction, 0.0, 1.0) * sector_width;
    const int mult = std::max(1, pool_multiplier);
    const int pool_size = std::min(n - 1, std::max(m, mult * m));
    KDTree2D tree(inst.GetCoords());
    std::vector<int> depot_pool;
    tree.Knn(0, pool_size + 8, depot_pool);
    depot_pool.erase(std::remove(depot_pool.begin(), depot_pool.end(), 0), depot_pool.end());
    if (static_cast<int>(depot_pool.size()) > pool_size) depot_pool.resize(static_cast<size_t>(pool_size));

    for (int city : depot_pool) {
        const double dx = coords[static_cast<size_t>(city)].first - x0;
        const double dy = coords[static_cast<size_t>(city)].second - y0;
        double angle = std::atan2(dy, dx) - shift;
        while (angle < 0.0) angle += full;
        while (angle >= full) angle -= full;
        int sector = static_cast<int>(angle / sector_width);
        if (sector >= m) sector = m - 1;
        sectors[static_cast<size_t>(sector)].push_back({city, dx * dx + dy * dy});
    }

    std::vector<int> fallback = depot_pool;
    std::sort(fallback.begin(), fallback.end(), [&](int a, int b) {
        const double ax = coords[static_cast<size_t>(a)].first - x0;
        const double ay = coords[static_cast<size_t>(a)].second - y0;
        const double bx = coords[static_cast<size_t>(b)].first - x0;
        const double by = coords[static_cast<size_t>(b)].second - y0;
        return ax * ax + ay * ay < bx * bx + by * by;
    });

    (void)rng;
    const double q = std::clamp(radial_quantile, 0.0, 1.0);
    for (int s = 0; s < m && remaining > 0; ++s) {
        auto& sector = sectors[static_cast<size_t>(s)];
        std::sort(sector.begin(), sector.end(), [](const SectorCity& a, const SectorCity& b) {
            if (std::abs(a.r2 - b.r2) > 1e-12) return a.r2 < b.r2;
            return a.city < b.city;
        });

        int chosen = -1;
        if (!sector.empty()) {
            const int pos = std::clamp(static_cast<int>(q * static_cast<double>(sector.size() - 1)),
                                       0, static_cast<int>(sector.size()) - 1);
            for (int delta = 0; delta < static_cast<int>(sector.size()); ++delta) {
                const int left = pos - delta;
                const int right = pos + delta;
                if (left >= 0) {
                    const int c = sector[static_cast<size_t>(left)].city;
                    if (!visited[static_cast<size_t>(c)]) { chosen = c; break; }
                }
                if (right < static_cast<int>(sector.size())) {
                    const int c = sector[static_cast<size_t>(right)].city;
                    if (!visited[static_cast<size_t>(c)]) { chosen = c; break; }
                }
            }
        }
        if (chosen < 0) {
            for (int c : fallback) {
                if (!visited[static_cast<size_t>(c)]) { chosen = c; break; }
            }
        }
        if (chosen < 0) break;
        routes[static_cast<size_t>(s)].push_back(chosen);
        cur[static_cast<size_t>(s)] = chosen;
        visited[static_cast<size_t>(chosen)] = 1;
        --remaining;
    }

    while (remaining > 0) {
        bool any = false;
        for (int s = 0; s < m && remaining > 0; ++s) {
            const int from = cur[static_cast<size_t>(s)];
            int best = -1;
            double bd = std::numeric_limits<double>::max();
            for (int j : candidates[static_cast<size_t>(from)]) {
                if (j == 0) continue;
                if (!visited[static_cast<size_t>(j)]) {
                    const double dd = inst.Distance(from, j);
                    if (dd < bd) { bd = dd; best = j; }
                }
            }
            if (best == -1) {
                for (int c = 1; c < n; ++c) {
                    if (!visited[static_cast<size_t>(c)]) {
                        const double dd = inst.Distance(from, c);
                        if (dd < bd) { bd = dd; best = c; }
                    }
                }
            }
            if (best == -1) continue;
            routes[static_cast<size_t>(s)].push_back(best);
            cur[static_cast<size_t>(s)] = best;
            visited[static_cast<size_t>(best)] = 1;
            --remaining;
            any = true;
        }
        if (!any) break;
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

// Pick a small deterministic set of non-depot starts for alternate NN chains.
// These are intentionally radial/sector anchors: the raw route may be a little
// longer than the depot-start chain, but after circular split/rebalance the
// route order can cut into cleaner non-empty salesman tours.
inline std::vector<int> PickSingleRouteNNStartVariants(const mtsp::Instance& inst,
                                                       int max_starts) {
    std::vector<int> starts;
    if (max_starts <= 0) return starts;
    const int n = inst.GetNodeCount();
    if (n <= 2) return starts;

    const auto& coords = inst.GetCoords();
    const double x0 = coords[0].first;
    const double y0 = coords[0].second;
    const int sectors = std::max(1, max_starts);
    std::vector<std::pair<double, int>> sector_best(static_cast<size_t>(sectors), {-1.0, -1});
    std::vector<std::pair<double, int>> radial;
    radial.reserve(static_cast<size_t>(n - 1));

    constexpr double two_pi = 6.28318530717958647692;
    for (int i = 1; i < n; ++i) {
        const double dx = coords[static_cast<size_t>(i)].first - x0;
        const double dy = coords[static_cast<size_t>(i)].second - y0;
        const double r2 = dx * dx + dy * dy;
        double angle = std::atan2(dy, dx) + 3.14159265358979323846;
        if (angle < 0.0) angle += two_pi;
        if (angle >= two_pi) angle -= two_pi;
        int sector = static_cast<int>((angle / two_pi) * static_cast<double>(sectors));
        if (sector >= sectors) sector = sectors - 1;
        auto& best = sector_best[static_cast<size_t>(sector)];
        if (r2 > best.first) best = {r2, i};
        radial.emplace_back(r2, i);
    }

    auto add_unique = [&](int node) {
        if (node <= 0) return;
        if (std::find(starts.begin(), starts.end(), node) != starts.end()) return;
        if (static_cast<int>(starts.size()) >= max_starts) return;
        starts.push_back(node);
    };

    std::sort(radial.begin(), radial.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (!radial.empty()) add_unique(radial.front().second);
    for (const auto& [dist2, node] : sector_best) {
        (void)dist2;
        add_unique(node);
    }
    for (const auto& [dist2, node] : radial) {
        (void)dist2;
        if (static_cast<int>(starts.size()) >= max_starts) break;
        add_unique(node);
    }
    return starts;
}

// Fast single-route seed for metric MINSUM with empty routes allowed. It builds
// one TSP-like NN chain and leaves the remaining salesmen empty. If start_city
// is a customer, the chain starts 0 -> start_city and then continues NN from
// that anchor; start_city <= 0 preserves the original depot-start behaviour.
inline RouteSet BuildSingleRouteNNSeedFromStart(const mtsp::Instance& inst,
                                                const CandidateSets& candidates,
                                                int start_city) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0, 0});
    if (n <= 1) return routes;

    std::vector<int> main_route;
    main_route.reserve(static_cast<size_t>(n + 1));
    main_route.push_back(0);
    std::vector<char> visited(static_cast<size_t>(n), 0);
    visited[0] = 1;
    KDTree2D tree(inst.GetCoords());
    int cur = 0;
    int remaining = n - 1;
    std::vector<int> near;
    if (start_city > 0 && start_city < n) {
        main_route.push_back(start_city);
        visited[static_cast<size_t>(start_city)] = 1;
        cur = start_city;
        --remaining;
    }

    while (remaining > 0) {
        int best = -1;
        double bd = std::numeric_limits<double>::max();
        if (cur >= 0 && cur < static_cast<int>(candidates.size())) {
            for (int nb : candidates[static_cast<size_t>(cur)]) {
                if (nb == 0 || visited[static_cast<size_t>(nb)]) continue;
                const double dd = inst.Distance(cur, nb);
                if (dd < bd) { bd = dd; best = nb; }
            }
        }

        for (int k = 64; best < 0 && k <= 4096 && k < n; k *= 2) {
            tree.Knn(cur, std::min(k, n - 1), near);
            for (int nb : near) {
                if (nb == 0 || visited[static_cast<size_t>(nb)]) continue;
                best = nb;
                break;
            }
        }

        if (best < 0) {
            for (int c = 1; c < n; ++c) {
                if (visited[static_cast<size_t>(c)]) continue;
                const double dd = inst.Distance(cur, c);
                if (dd < bd) { bd = dd; best = c; }
            }
        }
        if (best < 0) break;
        main_route.push_back(best);
        visited[static_cast<size_t>(best)] = 1;
        cur = best;
        --remaining;
    }
    main_route.push_back(0);
    routes[0] = std::move(main_route);
    EnsureClosedDepot(routes);
    return routes;
}

// Convenience wrapper: single-route NN seed starting from the depot.
inline RouteSet BuildSingleRouteNNSeed(const mtsp::Instance& inst,
                                       const CandidateSets& candidates) {
    return BuildSingleRouteNNSeedFromStart(inst, candidates, 0);
}

// Polar sweep: sort cities by angle from depot, then assign in equal arcs.
inline RouteSet BuildPolarSweep(const mtsp::Instance& inst) {
    const int n = inst.GetNodeCount();
    const int m = std::max(1, inst.GetSalesmanCount());
    const auto& coords = inst.GetCoords();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    if (n <= 1) { for (auto& r : routes) r.push_back(0); return routes; }
    const double dx0 = coords[0].first, dy0 = coords[0].second;
    std::vector<int> order;
    std::vector<double> angle(static_cast<size_t>(n), 0.0);
    std::vector<double> drad(static_cast<size_t>(n), 0.0);
    order.reserve(static_cast<size_t>(n - 1));
    for (int i = 1; i < n; ++i) {
        const double dx = coords[static_cast<size_t>(i)].first - dx0;
        const double dy = coords[static_cast<size_t>(i)].second - dy0;
        angle[static_cast<size_t>(i)] = std::atan2(dy, dx);
        drad[static_cast<size_t>(i)] = std::sqrt(dx * dx + dy * dy);
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (angle[static_cast<size_t>(a)] != angle[static_cast<size_t>(b)])
            return angle[static_cast<size_t>(a)] < angle[static_cast<size_t>(b)];
        return drad[static_cast<size_t>(a)] < drad[static_cast<size_t>(b)];
    });
    const int per = std::max(1, (n - 1 + m - 1) / m);
    int pos = 0;
    for (int r = 0; r < m && pos < (int)order.size(); ++r) {
        const int take = std::min(per, (int)order.size() - pos);
        for (int k = 0; k < take; ++k) routes[static_cast<size_t>(r)].push_back(order[static_cast<size_t>(pos + k)]);
        pos += take;
    }
    int rr = 0;
    while (pos < (int)order.size()) {
        routes[static_cast<size_t>(rr % m)].push_back(order[static_cast<size_t>(pos)]);
        ++pos; ++rr;
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

// k-means on Euclidean coordinates with balanced post-step (push/pull cities
// between clusters until each has ~n/m). Used by min-max as a balanced seed.
inline RouteSet BuildKMeansBalanced(const mtsp::Instance& inst, std::mt19937& rng,
                                    int k_iters = 20) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    const auto& coords = inst.GetCoords();
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    if (n <= 1) { for (auto& r : routes) r.push_back(0); return routes; }

    // Init centers: pick m random non-depot points
    std::vector<int> centers(static_cast<size_t>(m));
    {
        std::vector<int> pool;
        pool.reserve(static_cast<size_t>(n - 1));
        for (int i = 1; i < n; ++i) pool.push_back(i);
        std::shuffle(pool.begin(), pool.end(), rng);
        for (int s = 0; s < m; ++s) centers[static_cast<size_t>(s)] = pool[static_cast<size_t>(s % (n - 1))];
    }

    std::vector<Coord> centroids(static_cast<size_t>(m));
    for (int s = 0; s < m; ++s) centroids[static_cast<size_t>(s)] = coords[static_cast<size_t>(centers[static_cast<size_t>(s)])];

    std::vector<int> assign(static_cast<size_t>(n), 0);
    for (int it = 0; it < k_iters; ++it) {
        // Assign each non-depot city to nearest centroid
        for (int c = 1; c < n; ++c) {
            double bd = std::numeric_limits<double>::max();
            int bs = 0;
            for (int s = 0; s < m; ++s) {
                const double dx = coords[static_cast<size_t>(c)].first - centroids[static_cast<size_t>(s)].first;
                const double dy = coords[static_cast<size_t>(c)].second - centroids[static_cast<size_t>(s)].second;
                const double dd = dx * dx + dy * dy;
                if (dd < bd) { bd = dd; bs = s; }
            }
            assign[static_cast<size_t>(c)] = bs;
        }
        // Recompute centroids
        std::vector<double> sx(static_cast<size_t>(m), 0.0), sy(static_cast<size_t>(m), 0.0);
        std::vector<int> cnt(static_cast<size_t>(m), 0);
        for (int c = 1; c < n; ++c) {
            const int s = assign[static_cast<size_t>(c)];
            sx[static_cast<size_t>(s)] += coords[static_cast<size_t>(c)].first;
            sy[static_cast<size_t>(s)] += coords[static_cast<size_t>(c)].second;
            ++cnt[static_cast<size_t>(s)];
        }
        for (int s = 0; s < m; ++s) {
            if (cnt[static_cast<size_t>(s)] > 0) {
                centroids[static_cast<size_t>(s)].first = sx[static_cast<size_t>(s)] / cnt[static_cast<size_t>(s)];
                centroids[static_cast<size_t>(s)].second = sy[static_cast<size_t>(s)] / cnt[static_cast<size_t>(s)];
            }
        }
    }

    // Balance: move overpopulated clusters to underpopulated.
    std::vector<int> cnt(static_cast<size_t>(m), 0);
    for (int c = 1; c < n; ++c) ++cnt[static_cast<size_t>(assign[static_cast<size_t>(c)])];
    const int target = (n - 1) / m;
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (int c = 1; c < n; ++c) {
            const int s = assign[static_cast<size_t>(c)];
            if (cnt[static_cast<size_t>(s)] <= target + 1) continue;
            // Find under-filled cluster with closest centroid
            int best_t = -1; double bd = std::numeric_limits<double>::max();
            for (int t = 0; t < m; ++t) {
                if (cnt[static_cast<size_t>(t)] >= target + 1) continue;
                const double dx = coords[static_cast<size_t>(c)].first - centroids[static_cast<size_t>(t)].first;
                const double dy = coords[static_cast<size_t>(c)].second - centroids[static_cast<size_t>(t)].second;
                const double dd = dx * dx + dy * dy;
                if (dd < bd) { bd = dd; best_t = t; }
            }
            if (best_t >= 0) {
                assign[static_cast<size_t>(c)] = best_t;
                --cnt[static_cast<size_t>(s)]; ++cnt[static_cast<size_t>(best_t)];
                changed = true;
            }
        }
        if (!changed) break;
    }

    // Build routes: per cluster, NN-order from depot
    for (int s = 0; s < m; ++s) {
        std::vector<int> members;
        for (int c = 1; c < n; ++c) if (assign[static_cast<size_t>(c)] == s) members.push_back(c);
        std::vector<char> visited(members.size(), 0);
        int cur = 0;
        for (size_t k = 0; k < members.size(); ++k) {
            int best = -1; double bd = std::numeric_limits<double>::max();
            for (size_t j = 0; j < members.size(); ++j) {
                if (visited[j]) continue;
                const double dd = inst.Distance(cur, members[j]);
                if (dd < bd) { bd = dd; best = static_cast<int>(j); }
            }
            if (best < 0) break;
            routes[static_cast<size_t>(s)].push_back(members[static_cast<size_t>(best)]);
            cur = members[static_cast<size_t>(best)];
            visited[static_cast<size_t>(best)] = 1;
        }
        routes[static_cast<size_t>(s)].push_back(0);
    }
    EnsureClosedDepot(routes);
    return routes;
}

// Lightweight Clarke-Wright savings seed (for n <= ~20k).
inline RouteSet BuildSavingsSeed(const mtsp::Instance& inst,
                                 const CandidateSets& candidates,
                                 SearchBudget& budget) {
    const int m = std::max(1, inst.GetSalesmanCount());
    const int n = inst.GetNodeCount();
    if (n <= 1) {
        RouteSet trivial(static_cast<size_t>(m), std::vector<int>{0, 0});
        return trivial;
    }
    struct Edge { int a; int b; double saving; };
    std::vector<Edge> edges;
    edges.reserve(static_cast<size_t>(n) * 8ULL);
    for (int i = 1; i < n; ++i) {
        if ((i & 1023) == 0 && budget.ForceCheck()) break;
        for (int j : candidates[static_cast<size_t>(i)]) {
            if (j > i && j != 0) {
                const double sv = inst.Distance(0, i) + inst.Distance(0, j) - inst.Distance(i, j);
                if (sv > 0.0) edges.push_back({i, j, sv});
            }
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.saving > b.saving; });

    std::vector<int> route_id(static_cast<size_t>(n), -1);
    std::vector<std::vector<int>> opens;
    opens.reserve(static_cast<size_t>(n));
    for (int c = 1; c < n; ++c) {
        route_id[static_cast<size_t>(c)] = static_cast<int>(opens.size());
        opens.push_back({c});
    }
    auto first_or_last = [](const std::vector<int>& r, int c) {
        return !r.empty() && (r.front() == c || r.back() == c);
    };
    for (const auto& e : edges) {
        if (budget.ShouldStop()) break;
        const int ri = route_id[static_cast<size_t>(e.a)];
        const int rj = route_id[static_cast<size_t>(e.b)];
        if (ri < 0 || rj < 0 || ri == rj) continue;
        auto& Ri = opens[static_cast<size_t>(ri)];
        auto& Rj = opens[static_cast<size_t>(rj)];
        if (!first_or_last(Ri, e.a) || !first_or_last(Rj, e.b)) continue;
        if (Ri.front() == e.a) std::reverse(Ri.begin(), Ri.end());
        if (Rj.back() == e.b) std::reverse(Rj.begin(), Rj.end());
        if (static_cast<int>(opens.size()) <= m) break;
        for (int c : Rj) { Ri.push_back(c); route_id[static_cast<size_t>(c)] = ri; }
        Rj.clear();
    }
    RouteSet collected;
    for (auto& r : opens) if (!r.empty()) collected.push_back(std::move(r));
    while (static_cast<int>(collected.size()) > m) {
        size_t bi = 0, bj = 1; double bc = std::numeric_limits<double>::max();
        for (size_t i = 0; i < collected.size(); ++i)
            for (size_t j = i + 1; j < collected.size(); ++j) {
                if (collected[i].empty() || collected[j].empty()) continue;
                const double cc = inst.Distance(collected[i].back(), collected[j].front());
                if (cc < bc) { bc = cc; bi = i; bj = j; }
            }
        collected[bi].insert(collected[bi].end(), collected[bj].begin(), collected[bj].end());
        collected.erase(collected.begin() + static_cast<std::ptrdiff_t>(bj));
    }
    while (static_cast<int>(collected.size()) < m) {
        size_t lg = 0, lgs = 0;
        for (size_t i = 0; i < collected.size(); ++i)
            if (collected[i].size() > lgs) { lgs = collected[i].size(); lg = i; }
        if (lgs < 2) { collected.push_back({}); continue; }
        std::vector<int> tail(collected[lg].begin() + static_cast<std::ptrdiff_t>(lgs / 2), collected[lg].end());
        collected[lg].erase(collected[lg].begin() + static_cast<std::ptrdiff_t>(lgs / 2), collected[lg].end());
        collected.push_back(std::move(tail));
    }
    RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    for (size_t i = 0; i < collected.size() && i < routes.size(); ++i) {
        for (int c : collected[i]) routes[i].push_back(c);
        routes[i].push_back(0);
    }
    for (size_t i = collected.size(); i < routes.size(); ++i) routes[i].push_back(0);
    EnsureClosedDepot(routes);
    return routes;
}

}  // namespace mtsp::v21
