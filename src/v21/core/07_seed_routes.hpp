#pragma once

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

inline void EnsureClosedDepot(RouteSet& routes) {
    for (auto& r : routes) {
        if (r.empty()) { r = {0, 0}; continue; }
        if (r.front() != 0) r.insert(r.begin(), 0);
        if (r.size() < 2 || r.back() != 0) r.push_back(0);
    }
}

// Round-robin nearest-neighbour seed (matches the legacy 2opt+greed baseline).
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
