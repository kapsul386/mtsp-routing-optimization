// mtsp_lkh_wrapper_v20.cpp
//
// v20: SAFETY-NET wrapper around v19's hybrid MINSUM pipeline.
//
// PRIMARY GOAL (per user request):
//   *** v20 must never be worse than `2opt+greed` on MINSUM. ***
//
// Why v19 lost on large instances (overnight benchmark):
//   - n=50_000  m=5: v19 was 5.7% WORSE than 2opt+greed.
//   - n=100_000 m=5: v19 was 0.7% WORSE than 2opt+greed.
//   The cause: cross-route MINSUM moves disrupt the carefully balanced
//   round-robin NN seed, and the final 2-opt has too little budget to
//   re-converge each disrupted route on its own.
//
// v20 = v19 + Phase 0:
//   Compute the pure `2opt+greed` baseline (round-robin NN + exhaustive
//   O(L^2) 2-opt to convergence on every route, in parallel). Save it as
//   `baseline_routes` AND seed `best_minsum_routes` from it. The snapshot
//   rule already in v19 (update only when validate_ok && new_sum < best)
//   makes the entire pipeline strictly monotone w.r.t. that baseline.
//
//   => v20 MINSUM <= 2opt+greed MINSUM, by construction.
//
// On smaller n (<=25_000) the baseline is essentially free (<5s), so v20
// keeps all of v19's wins (5-22% better than 2opt+greed). On n>=50_000
// the baseline takes a meaningful share of the budget, and the remaining
// time is spent trying to improve over it via the v19 cross-route loop +
// final exhaustive 2-opt.
//
// ----- Original v19 design notes (unchanged) -----
//
// Key design choices (vs v17/v18):
//   1. MULTI-SEED PORTFOLIO selected by MINSUM only (no balanced surrogate
//      bootstrapping). Seeds: round-robin nearest neighbour (matches the
//      strong "2opt+greed" baseline), fast lookahead seed (BuildFastSeedRoutesV7),
//      polar sweep, and savings (only when n is small enough that savings is cheap).
//   2. Strict MINSUM-only inter-route move acceptance. The optional cluster-
//      aware rebalance is run inside a sandbox routeset and only kept if MINSUM
//      strictly improves (no MIN-MAX leak).
//   3. CANDIDATE-DRIVEN intra-route ILS (ApplyFirstImproving2OptV5 + double-bridge),
//      reused from lkh_wrapper_v9 — same machinery v17/v18 already validated.
//   4. EXHAUSTIVE FINAL 2-OPT POLISH ("greedy 2-opt") on every route, identical
//      to mtsp::ImproveRoute2Opt — runs to a true 2-opt local optimum. This is
//      the single biggest reason 2opt+greed beats v17/v18 at n=10k/25k.
//   5. Best-MINSUM solution is snapshotted after every phase; the function
//      out.swap(best_minsum_routes) at the end is the only return path.
//   6. Anytime safety: if any phase regresses MINSUM (e.g. anneal), we revert
//      to the snapshot. ValidateRoutes is the contract — never broken.
//   7. Time budget shape (default):
//        - seed phase           ≤ 8% (cheap)
//        - candidate set build  ≤ 12%
//        - intra-route ILS      ~ 35%
//        - inter-route MINSUM   ~ 25%
//        - cross-route oropt    ~ 10%
//        - final exhaustive 2-opt: 10% (or ALL remaining time on small n).
//
// Reuses lkh_wrapper_v9 modules via direct .cpp inclusion (same pattern as v17/v18).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

#include "lkh_wrapper_v9/00_common.cpp"
#include "lkh_wrapper_v8/10_candidate_sets.cpp"
#include "lkh_wrapper_v9/20_route_local_search.cpp"
#include "lkh_wrapper_v9/30_cluster_model.cpp"
#include "lkh_wrapper_v9/40_seed_routes.cpp"
#include "lkh_wrapper_v9/50_rebalance.cpp"

namespace {

// ===== Helpers ===============================================================

double RouteSumLengthV19(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    double total = 0.0;
    for (const auto& route : routes) {
        total += RouteLengthGenericV5(route, distance);
    }
    return total;
}

double MaxRouteLengthV19(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    double best = 0.0;
    for (const auto& route : routes) {
        best = std::max(best, RouteLengthGenericV5(route, distance));
    }
    return best;
}

void EnsureClosedDepotRoutesV19(mtsp::RouteSet& routes) {
    for (auto& route : routes) {
        if (route.empty()) {
            route = {0, 0};
            continue;
        }
        if (route.front() != 0) {
            route.insert(route.begin(), 0);
        }
        // Always push a closing 0 if the route still has only one element or
        // its tail is not 0. The first branch handles the [0] -> [0, 0] case
        // where SanitizeAndCompleteRoutesV7 left a route with just the depot.
        if (route.size() < 2 || route.back() != 0) {
            route.push_back(0);
        }
    }
}

// Build the trivial round-robin nearest-neighbour seed used by the
// `2opt+greed` baseline. For uniform instances this is surprisingly strong.
mtsp::RouteSet BuildRoundRobinNNSeedV19(const mtsp::Instance& inst) {
    const int m = std::max(1, inst.GetSalesmanCount());
    mtsp::RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    std::vector<int> current(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
    visited[0] = 1;
    int remaining = inst.GetNodeCount() - 1;

    while (remaining > 0) {
        for (int s = 0; s < m && remaining > 0; ++s) {
            double best_dist = std::numeric_limits<double>::max();
            int best_city = -1;
            for (int city = 1; city < inst.GetNodeCount(); ++city) {
                if (!visited[static_cast<size_t>(city)]) {
                    const double d = inst.Distance(current[static_cast<size_t>(s)], city);
                    if (d < best_dist) {
                        best_dist = d;
                        best_city = city;
                    }
                }
            }
            if (best_city == -1) {
                continue;
            }
            routes[static_cast<size_t>(s)].push_back(best_city);
            current[static_cast<size_t>(s)] = best_city;
            visited[static_cast<size_t>(best_city)] = 1;
            --remaining;
        }
    }
    for (auto& route : routes) {
        route.push_back(0);
    }
    return routes;
}

// Polar sweep seed (same idea as BuildPolarSweepSeedV18 but standalone here so
// v19 does not depend on v18 internals).
mtsp::RouteSet BuildPolarSweepSeedV19(const mtsp::Instance& inst) {
    const int n = inst.GetNodeCount();
    const int m = std::max(1, inst.GetSalesmanCount());
    const auto& coords = inst.GetCoords();

    mtsp::RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
    if (n <= 1) {
        for (auto& r : routes) r.push_back(0);
        return routes;
    }

    const double dx0 = coords[0].first;
    const double dy0 = coords[0].second;

    std::vector<int> order;
    order.reserve(static_cast<size_t>(n - 1));
    std::vector<double> angle(static_cast<size_t>(n), 0.0);
    std::vector<double> drad(static_cast<size_t>(n), 0.0);
    for (int i = 1; i < n; ++i) {
        const double dx = coords[static_cast<size_t>(i)].first - dx0;
        const double dy = coords[static_cast<size_t>(i)].second - dy0;
        angle[static_cast<size_t>(i)] = std::atan2(dy, dx);
        drad[static_cast<size_t>(i)] = std::sqrt(dx * dx + dy * dy);
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double aa = angle[static_cast<size_t>(a)];
        const double bb = angle[static_cast<size_t>(b)];
        if (aa != bb) return aa < bb;
        return drad[static_cast<size_t>(a)] < drad[static_cast<size_t>(b)];
    });

    const int per = std::max(1, (n - 1 + m - 1) / m);
    int pos = 0;
    for (int r = 0; r < m && pos < static_cast<int>(order.size()); ++r) {
        const int take = std::min(per, static_cast<int>(order.size()) - pos);
        for (int k = 0; k < take; ++k) {
            routes[static_cast<size_t>(r)].push_back(order[static_cast<size_t>(pos + k)]);
        }
        pos += take;
    }
    int rr = 0;
    while (pos < static_cast<int>(order.size())) {
        routes[static_cast<size_t>(rr % m)].push_back(order[static_cast<size_t>(pos)]);
        ++pos;
        ++rr;
    }
    for (auto& r : routes) r.push_back(0);
    return routes;
}

enum class ValidationFailure : int {
    Ok = 0,
    BadEndpoints = 1,
    BadInteriorNode = 2,
    DuplicateNode = 3,
    MissingNode = 4
};

ValidationFailure ValidateLocalRoutesDetail(const mtsp::RouteSet& routes, int node_count) {
    std::vector<int> seen(static_cast<size_t>(node_count), 0);
    seen[0] = 1;
    for (const auto& route : routes) {
        if (route.size() < 2 || route.front() != 0 || route.back() != 0) {
            return ValidationFailure::BadEndpoints;
        }
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            const int node = route[i];
            if (node <= 0 || node >= node_count) {
                return ValidationFailure::BadInteriorNode;
            }
            if (seen[static_cast<size_t>(node)] == 1) {
                return ValidationFailure::DuplicateNode;
            }
            seen[static_cast<size_t>(node)] = 1;
        }
    }
    for (int node = 1; node < node_count; ++node) {
        if (seen[static_cast<size_t>(node)] == 0) return ValidationFailure::MissingNode;
    }
    return ValidationFailure::Ok;
}

bool ValidateLocalRoutes(const mtsp::RouteSet& routes, int node_count) {
    return ValidateLocalRoutesDetail(routes, node_count) == ValidationFailure::Ok;
}

// MINSUM relocate: move a single node to another route iff total length
// strictly decreases. Returns true on success and the routes are modified.
template <typename DistanceFn>
bool TryMinsumRelocateV19(mtsp::RouteSet& routes,
                          const CandidateSets& candidates,
                          DistanceFn& distance,
                          SearchBudgetV5& budget) {
    if (routes.size() < 2) return false;
    std::vector<RouteIndexV5> indices;
    indices.reserve(routes.size());
    for (const auto& route : routes) {
        indices.emplace_back(static_cast<int>(candidates.size()));
        indices.back().Build(route);
    }

    size_t best_from = routes.size();
    size_t best_to = routes.size();
    size_t best_i = 0;
    size_t best_after = 0;
    double best_delta = -kEps;

    for (size_t from = 0; from < routes.size() && !budget.ShouldStop(); ++from) {
        for (size_t i = 1; i + 1 < routes[from].size() && !budget.ShouldStop(); ++i) {
            const int city = routes[from][i];
            const int prev = routes[from][i - 1];
            const int next = routes[from][i + 1];
            const double removal = distance(prev, city) + distance(city, next) - distance(prev, next);

            for (size_t to = 0; to < routes.size(); ++to) {
                if (to == from || routes[to].size() < 2) continue;

                std::vector<size_t> positions;
                positions.reserve(candidates[static_cast<size_t>(city)].size() + 4ULL);
                positions.push_back(0);
                positions.push_back(routes[to].size() - 2);
                for (int neighbor : candidates[static_cast<size_t>(city)]) {
                    const int pos = indices[to].Get(neighbor);
                    if (pos >= 0 && pos + 1 < static_cast<int>(routes[to].size())) {
                        positions.push_back(static_cast<size_t>(pos));
                    }
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

                for (size_t after : positions) {
                    const int a = routes[to][after];
                    const int b = routes[to][after + 1];
                    const double insert = distance(a, city) + distance(city, b) - distance(a, b);
                    const double delta = insert - removal;
                    if (delta + kEps < best_delta) {
                        best_delta = delta;
                        best_from = from;
                        best_to = to;
                        best_i = i;
                        best_after = after;
                    }
                }
            }
        }
    }

    if (best_from == routes.size()) return false;

    const int city = routes[best_from][best_i];
    routes[best_from].erase(routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i));
    routes[best_to].insert(routes[best_to].begin() + static_cast<std::ptrdiff_t>(best_after + 1), city);
    return true;
}

// MINSUM 1-1 swap: swap one city from route a with one city from route b iff
// the total length strictly decreases.
template <typename DistanceFn>
bool TryMinsumSwapV19(mtsp::RouteSet& routes,
                      const CandidateSets& candidates,
                      DistanceFn& distance,
                      SearchBudgetV5& budget) {
    if (routes.size() < 2) return false;

    std::vector<RouteIndexV5> indices;
    indices.reserve(routes.size());
    for (const auto& route : routes) {
        indices.emplace_back(static_cast<int>(candidates.size()));
        indices.back().Build(route);
    }

    size_t best_a = routes.size();
    size_t best_b = routes.size();
    size_t best_i = 0;
    size_t best_j = 0;
    double best_delta = -kEps;

    for (size_t a = 0; a < routes.size() && !budget.ShouldStop(); ++a) {
        for (size_t i = 1; i + 1 < routes[a].size() && !budget.ShouldStop(); ++i) {
            const int city_a = routes[a][i];
            const int prev_a = routes[a][i - 1];
            const int next_a = routes[a][i + 1];

            for (int neighbor : candidates[static_cast<size_t>(city_a)]) {
                if (neighbor <= 0) continue;
                for (size_t b = 0; b < routes.size(); ++b) {
                    if (b == a) continue;
                    const int j = indices[b].Get(neighbor);
                    if (j <= 0 || j + 1 >= static_cast<int>(routes[b].size())) continue;
                    const int city_b = routes[b][j];
                    const int prev_b = routes[b][j - 1];
                    const int next_b = routes[b][j + 1];

                    const double removed = distance(prev_a, city_a) + distance(city_a, next_a)
                                         + distance(prev_b, city_b) + distance(city_b, next_b);
                    const double added   = distance(prev_a, city_b) + distance(city_b, next_a)
                                         + distance(prev_b, city_a) + distance(city_a, next_b);
                    const double delta = added - removed;
                    if (delta + kEps < best_delta) {
                        best_delta = delta;
                        best_a = a;
                        best_b = b;
                        best_i = static_cast<size_t>(i);
                        best_j = static_cast<size_t>(j);
                    }
                }
            }
        }
    }

    if (best_a == routes.size()) return false;
    std::swap(routes[best_a][best_i], routes[best_b][best_j]);
    return true;
}

// MINSUM Or-opt of segment length L (1, 2, or 3): move a small block from one
// route to another iff total length strictly decreases. Single best move per call.
template <typename DistanceFn>
bool TryOrOptCrossRouteV19(mtsp::RouteSet& routes,
                           const CandidateSets& candidates,
                           DistanceFn& distance,
                           int segment_len,
                           SearchBudgetV5& budget) {
    if (routes.size() < 2 || segment_len < 1 || segment_len > 3) return false;

    std::vector<RouteIndexV5> indices;
    indices.reserve(routes.size());
    for (const auto& route : routes) {
        indices.emplace_back(static_cast<int>(candidates.size()));
        indices.back().Build(route);
    }

    size_t best_from = routes.size();
    size_t best_to = routes.size();
    size_t best_i = 0;
    size_t best_after = 0;
    bool best_reverse = false;
    double best_delta = -kEps;

    for (size_t from = 0; from < routes.size() && !budget.ShouldStop(); ++from) {
        if (static_cast<int>(routes[from].size()) < segment_len + 3) continue;
        for (size_t i = 1; i + segment_len < routes[from].size() && !budget.ShouldStop(); ++i) {
            const int prev = routes[from][i - 1];
            const int seg_first = routes[from][i];
            const int seg_last = routes[from][i + segment_len - 1];
            const int next = routes[from][i + segment_len];
            const double removal = distance(prev, seg_first)
                                 + distance(seg_last, next)
                                 - distance(prev, next);

            for (size_t to = 0; to < routes.size(); ++to) {
                if (to == from || routes[to].size() < 2) continue;

                std::vector<size_t> positions;
                positions.reserve(candidates[static_cast<size_t>(seg_first)].size() +
                                  candidates[static_cast<size_t>(seg_last)].size() + 4ULL);
                positions.push_back(0);
                positions.push_back(routes[to].size() - 2);
                for (int neighbor : candidates[static_cast<size_t>(seg_first)]) {
                    const int pos = indices[to].Get(neighbor);
                    if (pos >= 0 && pos + 1 < static_cast<int>(routes[to].size())) {
                        positions.push_back(static_cast<size_t>(pos));
                    }
                }
                for (int neighbor : candidates[static_cast<size_t>(seg_last)]) {
                    const int pos = indices[to].Get(neighbor);
                    if (pos >= 0 && pos + 1 < static_cast<int>(routes[to].size())) {
                        positions.push_back(static_cast<size_t>(pos));
                    }
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

                for (size_t after : positions) {
                    const int a = routes[to][after];
                    const int b = routes[to][after + 1];
                    // forward orientation
                    {
                        const double insert = distance(a, seg_first)
                                            + distance(seg_last, b)
                                            - distance(a, b);
                        const double delta = insert - removal;
                        if (delta + kEps < best_delta) {
                            best_delta = delta;
                            best_from = from;
                            best_to = to;
                            best_i = i;
                            best_after = after;
                            best_reverse = false;
                        }
                    }
                    if (segment_len >= 2) {
                        const double insert_rev = distance(a, seg_last)
                                                + distance(seg_first, b)
                                                - distance(a, b);
                        const double delta_rev = insert_rev - removal;
                        if (delta_rev + kEps < best_delta) {
                            best_delta = delta_rev;
                            best_from = from;
                            best_to = to;
                            best_i = i;
                            best_after = after;
                            best_reverse = true;
                        }
                    }
                }
            }
        }
    }

    if (best_from == routes.size()) return false;

    std::vector<int> block(routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i),
                           routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i + segment_len));
    if (best_reverse) std::reverse(block.begin(), block.end());
    routes[best_from].erase(routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i),
                            routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i + segment_len));
    routes[best_to].insert(routes[best_to].begin() + static_cast<std::ptrdiff_t>(best_after + 1),
                           block.begin(), block.end());
    return true;
}

// Exhaustive intra-route 2-opt that uses the raw Instance::Distance (no
// per-pair cache). Matches the exact arithmetic of `mtsp::ImproveRoute2Opt`
// (the algorithm 2opt+greed uses) but with a time budget. Cache-free is
// FASTER than the cached oracle on the first pass at large n because we
// avoid the unordered_map rehash storm caused by ~100M unique inserts.
inline void Exhaustive2OptOnRouteRawV20(std::vector<int>& route,
                                         const mtsp::Instance& inst,
                                         SearchBudgetV5& budget,
                                         int* passes_out) {
    if (route.size() <= 4) {
        if (passes_out) *passes_out = 0;
        return;
    }
    bool improved = true;
    int passes = 0;
    while (improved) {
        improved = false;
        ++passes;
        for (size_t i = 1; i + 2 < route.size(); ++i) {
            if (budget.ShouldStop()) {
                if (passes_out) *passes_out = passes;
                return;
            }
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                if ((j & 4095U) == 0 && budget.ShouldStop()) {
                    if (passes_out) *passes_out = passes;
                    return;
                }
                const double before = inst.Distance(route[i - 1], route[i])
                                    + inst.Distance(route[j], route[j + 1]);
                const double after  = inst.Distance(route[i - 1], route[j])
                                    + inst.Distance(route[i], route[j + 1]);
                if (after + kEps < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                }
            }
        }
        if (budget.ShouldStop()) {
            if (passes_out) *passes_out = passes;
            return;
        }
    }
    if (passes_out) *passes_out = passes;
}

// Exhaustive intra-route 2-opt to a true local optimum. Equivalent to
// mtsp::ImproveRoute2Opt but uses the cached oracle. Time-budgeted with
// inner-loop budget polling so the deadline is respected even on huge routes.
template <typename DistanceFn>
void Exhaustive2OptOnRouteV19(std::vector<int>& route,
                              DistanceFn& distance,
                              SearchBudgetV5& budget,
                              int* passes_out = nullptr) {
    if (route.size() <= 4) return;
    bool improved = true;
    int passes = 0;
    while (improved) {
        improved = false;
        ++passes;
        for (size_t i = 1; i + 2 < route.size(); ++i) {
            if (budget.ShouldStop()) {
                if (passes_out) *passes_out = passes;
                return;
            }
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                if ((j & 4095U) == 0 && budget.ShouldStop()) {
                    if (passes_out) *passes_out = passes;
                    return;
                }
                const double before = distance(route[i - 1], route[i])
                                    + distance(route[j], route[j + 1]);
                const double after  = distance(route[i - 1], route[j])
                                    + distance(route[i], route[j + 1]);
                if (after + kEps < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                }
            }
        }
        if (budget.ShouldStop()) {
            if (passes_out) *passes_out = passes;
            return;
        }
    }
    if (passes_out) *passes_out = passes;
}

// Like Exhaustive2OptOnRouteV19 but uses don't-look bits + a candidate set
// to keep the cost per pass close to O(L * K). For large routes this is the
// only practical way to reach a 2-opt local optimum in bounded time. Also
// time-budgeted with frequent polling.
template <typename DistanceFn>
void NeighborList2OptOnRouteV19(std::vector<int>& route,
                                const CandidateSets& candidates,
                                DistanceFn& distance,
                                int node_count,
                                SearchBudgetV5& budget) {
    if (route.size() <= 4) return;
    RouteIndexV5 index(node_count);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);
    int passes = 0;
    while (!budget.ShouldStop()) {
        if (!ApplyFirstImproving2OptV5(route, candidates, distance, index, dont_look, budget)) {
            // 2-opt converged with don't-look bits: escape with a single
            // dont-look reset and try once more, then break.
            std::fill(dont_look.begin(), dont_look.end(), 0);
            if (!ApplyFirstImproving2OptV5(route, candidates, distance, index, dont_look, budget)) {
                break;
            }
        }
        if (++passes > 200000) break;  // safety
    }
}

// Parallel intra-route polish using IteratedLocalSearchV5 (candidate-driven
// 2-opt + double-bridge perturbation). Bounded by a shared budget deadline.
void PolishRoutesParallelV19(mtsp::RouteSet& routes,
                             const mtsp::Instance& inst,
                             int rounds_per_route,
                             const CandidateSets& local_candidates,
                             int node_count,
                             SearchBudgetV5& shared_budget,
                             unsigned int seed_base,
                             bool omp_enabled) {
    const auto deadline_ms = shared_budget.RemainingMs();
    const int safe_deadline = std::max(1, deadline_ms);
#ifdef _OPENMP
    if (omp_enabled) {
        const int route_count = static_cast<int>(routes.size());
#pragma omp parallel for schedule(dynamic) if(route_count > 1)
        for (int r = 0; r < route_count; ++r) {
            std::mt19937 local_rng(seed_base ^ static_cast<unsigned int>(r) * 0x9E3779B9U);
            DistanceOracleV5 local_distance(inst);
            SearchBudgetV5 local_budget(safe_deadline, 0, 64);
            IteratedLocalSearchV5(routes[static_cast<size_t>(r)],
                                  local_rng,
                                  rounds_per_route,
                                  local_candidates,
                                  node_count,
                                  local_distance,
                                  local_budget);
        }
        return;
    }
#endif
    (void)omp_enabled;
    DistanceOracleV5 distance(inst);
    for (size_t r = 0; r < routes.size(); ++r) {
        if (shared_budget.ShouldStop()) break;
        std::mt19937 local_rng(seed_base ^ static_cast<unsigned int>(r) * 0x9E3779B9U);
        SearchBudgetV5 local_budget(shared_budget.RemainingMs(), 0, 64);
        IteratedLocalSearchV5(routes[r],
                              local_rng,
                              rounds_per_route,
                              local_candidates,
                              node_count,
                              distance,
                              local_budget);
    }
}

// ===== Solver class ==========================================================

class LkhWrapperSolverV20 : public mtsp::Solver {
public:
    std::unordered_map<std::string, std::string> GetLastMetadata() const override {
        return last_metadata_;
    }

    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
        if (opts.count("time-budget-ms")) {
            time_budget_ms_ = std::max(0, std::stoi(opts.at("time-budget-ms")));
        }
        if (opts.count("reserve-budget-ms")) {
            reserve_budget_ms_ = std::max(0, std::stoi(opts.at("reserve-budget-ms")));
        }
        if (opts.count("local-candidate-count")) {
            local_candidate_count_ = std::max(4, std::stoi(opts.at("local-candidate-count")));
        }
        if (opts.count("global-candidate-count")) {
            global_candidate_count_ = std::max(8, std::stoi(opts.at("global-candidate-count")));
        }
        if (opts.count("rounds")) {
            ils_rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("threads")) {
            thread_count_ = std::max(0, std::stoi(opts.at("threads")));
        }
        if (opts.count("omp-polish")) {
            const std::string val = opts.at("omp-polish");
            omp_polish_enabled_ = !(val == "0" || val == "false" || val == "no" || val == "off");
        }
        if (opts.count("final-2opt")) {
            const std::string val = opts.at("final-2opt");
            final_2opt_enabled_ = !(val == "0" || val == "false" || val == "no" || val == "off");
        }
        if (opts.count("final-2opt-cap-ms")) {
            final_2opt_cap_ms_override_ = std::max(0, std::stoi(opts.at("final-2opt-cap-ms")));
        }
        if (opts.count("polar-seed")) {
            const std::string val = opts.at("polar-seed");
            polar_seed_enabled_ = !(val == "0" || val == "false" || val == "no" || val == "off");
        }
        if (opts.count("rr-seed")) {
            const std::string val = opts.at("rr-seed");
            roundrobin_seed_enabled_ = !(val == "0" || val == "false" || val == "no" || val == "off");
        }
        if (opts.count("savings-seed")) {
            const std::string val = opts.at("savings-seed");
            savings_seed_enabled_ = !(val == "0" || val == "false" || val == "no" || val == "off");
        }
    }

    void Solve(mtsp::RouteSet& out) override {
        last_metadata_.clear();
        const mtsp::Instance& inst = mtsp::Instance::GetInstance();
        const int node_count = inst.GetNodeCount();
        const int salesman_count = inst.GetSalesmanCount();

#ifdef _OPENMP
        if (thread_count_ > 0) {
            omp_set_num_threads(thread_count_);
        }
        last_metadata_["omp_polish"] = omp_polish_enabled_ ? "true" : "false";
        last_metadata_["omp_max_threads"] = std::to_string(omp_get_max_threads());
#else
        last_metadata_["omp_polish"] = "unavailable";
#endif

        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const bool unlimited = time_budget_ms_ <= 0;
        const int effective_total_ms = unlimited
            ? 0
            : std::max(1, time_budget_ms_ - std::max(0, reserve_budget_ms_));
        last_metadata_["effective_total_ms"] = std::to_string(effective_total_ms);
        last_metadata_["node_count"] = std::to_string(node_count);
        last_metadata_["salesman_count"] = std::to_string(salesman_count);
        last_metadata_["seed"] = std::to_string(seed_);

        const auto solve_start = std::chrono::steady_clock::now();
        SearchBudgetV5 total_budget(effective_total_ms, 0, 32);

        // ====== Phase 0.5 (v20 SAFETY NET) — runs FIRST ======
        // Build the deterministic round-robin NN seed and run an exhaustive
        // O(L^2)-per-pass 2-opt to a true local optimum on each route in
        // parallel. This reproduces the `2opt+greed` solver MINSUM exactly,
        // and we adopt it as the lower bound for everything that follows.
        mtsp::RouteSet baseline_routes = BuildRoundRobinNNSeedV19(inst);
        double baseline_sum = std::numeric_limits<double>::infinity();
        {
            const int reserved_for_phases_ms = unlimited
                ? 0
                : std::max(2000, effective_total_ms / 10);
            const int baseline_budget_ms = unlimited
                ? 0
                : std::max(2000, effective_total_ms - reserved_for_phases_ms);
            last_metadata_["baseline_2optgreed_budget_ms"] = std::to_string(baseline_budget_ms);

            SanitizeAndCompleteRoutesV7(baseline_routes, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutesV19(baseline_routes);

            const auto baseline_started = std::chrono::steady_clock::now();
            const auto baseline_deadline = baseline_started +
                std::chrono::milliseconds(baseline_budget_ms > 0
                                          ? baseline_budget_ms
                                          : 24 * 3600 * 1000);
            auto baseline_remaining_ms = [&]() {
                const auto now = std::chrono::steady_clock::now();
                const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                    baseline_deadline - now).count();
                return delta > 50 ? static_cast<int>(delta) : 50;
            };

            std::vector<int> baseline_passes(baseline_routes.size(), 0);
#ifdef _OPENMP
            if (omp_polish_enabled_ && static_cast<int>(baseline_routes.size()) > 1) {
                const int route_count = static_cast<int>(baseline_routes.size());
#pragma omp parallel for schedule(dynamic)
                for (int r = 0; r < route_count; ++r) {
                    SearchBudgetV5 local_budget(baseline_remaining_ms(), 0, 64);
                    int passes = 0;
                    Exhaustive2OptOnRouteRawV20(baseline_routes[static_cast<size_t>(r)],
                                                 inst, local_budget, &passes);
                    baseline_passes[static_cast<size_t>(r)] = passes;
                }
            } else
#endif
            {
                for (size_t r = 0; r < baseline_routes.size(); ++r) {
                    SearchBudgetV5 local_budget(baseline_remaining_ms(), 0, 64);
                    if (local_budget.ShouldStop()) break;
                    int passes = 0;
                    Exhaustive2OptOnRouteRawV20(baseline_routes[r], inst, local_budget, &passes);
                    baseline_passes[r] = passes;
                }
            }
            {
                std::string passes_str;
                for (size_t r = 0; r < baseline_passes.size(); ++r) {
                    if (r) passes_str += ",";
                    passes_str += std::to_string(baseline_passes[r]);
                }
                last_metadata_["baseline_2optgreed_passes_per_route"] = passes_str;
            }

            SanitizeAndCompleteRoutesV7(baseline_routes, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutesV19(baseline_routes);
            const auto baseline_done = std::chrono::steady_clock::now();
            baseline_sum = RouteSumLengthV19(baseline_routes, distance);
            last_metadata_["baseline_2optgreed_sum"] = std::to_string(baseline_sum);
            last_metadata_["baseline_2optgreed_ms"] = std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    baseline_done - baseline_started).count());

            if (!ValidateLocalRoutes(baseline_routes, node_count)) {
                last_metadata_["baseline_2optgreed_invalid"] = "true";
                baseline_sum = std::numeric_limits<double>::infinity();
            }
        }

        // ---- Phase 0: cheap geometric candidate set for initial construction ----
        const int K_local = LocalCandidateCount(node_count);
        const int K_global = GlobalCandidateCount(node_count, K_local);
        last_metadata_["candidate_count_local"] = std::to_string(K_local);
        last_metadata_["candidate_count_global"] = std::to_string(K_global);

        const int seed_phase_ms = unlimited ? 0 : std::max(500, effective_total_ms / 12);
        SearchBudgetV5 seed_budget(seed_phase_ms, 0, 32);

        CandidateSets cheap_candidates = BuildGeometricCandidatesV5(inst,
                                                                     std::max(K_local + 2, 12),
                                                                     exact_candidate_threshold_);

        // ---- Phase 1: multi-seed portfolio (pick best by MINSUM) ----
        mtsp::RouteSet best = BuildFastSeedSafe(inst, cheap_candidates, distance, seed_budget);
        double best_sum = RouteSumLengthV19(best, distance);
        last_metadata_["seed_fast_sum"] = std::to_string(best_sum);
        last_metadata_["seed_chosen"] = "fast";

        if (roundrobin_seed_enabled_) {
            mtsp::RouteSet rr = BuildRoundRobinNNSeedV19(inst);
            const double rr_sum = RouteSumLengthV19(rr, distance);
            last_metadata_["seed_rr_sum"] = std::to_string(rr_sum);
            if (rr_sum + kEps < best_sum && ValidateLocalRoutes(rr, node_count)) {
                best.swap(rr);
                best_sum = rr_sum;
                last_metadata_["seed_chosen"] = "rr";
            }
        }

        if (polar_seed_enabled_) {
            mtsp::RouteSet polar = BuildPolarSweepSeedV19(inst);
            SanitizeAndCompleteRoutesV7(polar, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutesV19(polar);
            const double polar_sum = RouteSumLengthV19(polar, distance);
            last_metadata_["seed_polar_sum"] = std::to_string(polar_sum);
            if (polar_sum + kEps < best_sum && ValidateLocalRoutes(polar, node_count)) {
                best.swap(polar);
                best_sum = polar_sum;
                last_metadata_["seed_chosen"] = "polar";
            }
        }

        // Savings is expensive; only attempt for n <= 20k where it can win.
        if (savings_seed_enabled_ && node_count <= 20000 && !seed_budget.ShouldStop()) {
            const int savings_K = std::min(48, std::max(K_global, 16));
            CandidateSets sav_cands = BuildGeometricCandidatesV5(inst, savings_K, exact_candidate_threshold_);
            NormalizeCandidateSymmetryV5(sav_cands, static_cast<size_t>(savings_K));
            mtsp::RouteSet savings = BuildSavingsLikeSeed(inst, sav_cands, distance, seed_budget);
            if (!savings.empty() && ValidateLocalRoutes(savings, node_count)) {
                const double sav_sum = RouteSumLengthV19(savings, distance);
                last_metadata_["seed_savings_sum"] = std::to_string(sav_sum);
                if (sav_sum + kEps < best_sum) {
                    best.swap(savings);
                    best_sum = sav_sum;
                    last_metadata_["seed_chosen"] = "savings";
                }
            }
        }

        last_metadata_["seed_minsum"] = std::to_string(best_sum);

        // Snapshot best-known MINSUM solution; never lose this even if
        // subsequent phases time out or regress.
        mtsp::RouteSet best_minsum_routes = best;
        double best_minsum_sum = best_sum;

        // ---- Phase 1.5: integrate baseline as the safety floor ----
        // Phase 0.5 (above) already produced a 2opt+greed baseline. Compare
        // it against the multi-seed best and adopt whichever is stronger.
        // The strict snapshot rule then makes downstream monotone w.r.t.
        // baseline_sum (= 2opt+greed result).
        if (std::isfinite(baseline_sum) && baseline_sum + kEps < best_minsum_sum) {
            best = baseline_routes;
            best_sum = baseline_sum;
            best_minsum_routes = baseline_routes;
            best_minsum_sum = baseline_sum;
            last_metadata_["baseline_2optgreed_used_as_best"] = "true";
        } else {
            last_metadata_["baseline_2optgreed_used_as_best"] =
                std::isfinite(baseline_sum) ? "false" : "invalid";
        }

        // ---- Phase 2: build hybrid candidate set for the improvement phase ----
        const int candidate_phase_ms = unlimited ? 0 : std::max(500, effective_total_ms / 8);
        SearchBudgetV5 cand_budget(candidate_phase_ms, 0, 32);
        const int popmusic_solutions = PopmusicSolutions(node_count);
        const int popmusic_sample = PopmusicSample(node_count);
        const int popmusic_window = PopmusicWindow(node_count);
        last_metadata_["popmusic_solutions"] = std::to_string(popmusic_solutions);

        CandidateSets global_candidates;
        if (popmusic_solutions > 0 && node_count >= 1024) {
            global_candidates = BuildHybridCandidateSetsV8(inst,
                                                            K_global,
                                                            std::max(K_global, 12),
                                                            exact_candidate_threshold_,
                                                            popmusic_solutions,
                                                            popmusic_sample,
                                                            popmusic_window,
                                                            rng,
                                                            distance,
                                                            cand_budget);
        } else {
            global_candidates = BuildGeometricCandidatesV5(inst, K_global, exact_candidate_threshold_);
            NormalizeCandidateSymmetryV5(global_candidates, static_cast<size_t>(K_global));
        }
        CandidateSets local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, K_local);

        // ---- Phase 3: cluster-aware MINSUM-only rebalance (sandboxed) ----
        if (node_count >= 2000 && !total_budget.ShouldStop()) {
            const int desired = DesiredClusterCountV6(node_count, salesman_count);
            const int cluster_phase_ms = unlimited ? 0 : std::max(500, effective_total_ms / 20);
            SearchBudgetV5 cluster_budget(cluster_phase_ms, 0, 16);
            ClusterModelV6 model = BuildLightweightClustersV6(inst, desired, rng, cluster_budget);
            last_metadata_["cluster_count"] = std::to_string(model.clusters.size());
            if (!model.clusters.empty()) {
                AugmentCandidatesWithClusterBridgesV6(global_candidates,
                                                       inst,
                                                       model,
                                                       std::max(K_global + 4, K_global));
                local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, K_local);

                // Try cluster-aware open-route rebalance on a SANDBOX copy.
                // Accept only if MINSUM strictly improves.
                mtsp::RouteSet sandbox = best;
                for (auto& r : sandbox) if (!r.empty() && r.back() == 0) r.pop_back();
                const int rebalance_phase_ms = unlimited ? 0 : std::max(500, effective_total_ms / 25);
                SearchBudgetV5 rebalance_budget(rebalance_phase_ms, 0, 16);
                RebalanceOpenRoutesClusterAwareV6(sandbox, model, distance, rebalance_budget, 8);
                SanitizeAndCompleteRoutesV7(sandbox, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutesV19(sandbox);
                if (ValidateLocalRoutes(sandbox, node_count)) {
                    const double sandbox_sum = RouteSumLengthV19(sandbox, distance);
                    last_metadata_["cluster_rebalance_sum"] = std::to_string(sandbox_sum);
                    if (sandbox_sum + kEps < best_sum) {
                        best.swap(sandbox);
                        best_sum = sandbox_sum;
                        if (best_sum + kEps < best_minsum_sum) {
                            best_minsum_routes = best;
                            best_minsum_sum = best_sum;
                        }
                    }
                }
            }
        }

        // ---- Phase 4: intra-route ILS (parallel by route) ----
        const int polish_phase_ms = unlimited ? 0 : std::max(500, (effective_total_ms * 35) / 100);
        SearchBudgetV5 polish_budget(polish_phase_ms, 0, 32);
        last_metadata_["polish_phase_budget_ms"] = std::to_string(polish_phase_ms);
        PolishRoutesParallelV19(best, inst, ils_rounds_, local_candidates, node_count,
                                polish_budget, seed_ ^ 0xBB67AE85U, omp_polish_enabled_);
        SanitizeAndCompleteRoutesV7(best, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(best);
        const double after_polish_sum = RouteSumLengthV19(best, distance);
        last_metadata_["after_polish_sum"] = std::to_string(after_polish_sum);
        if (ValidateLocalRoutes(best, node_count) && after_polish_sum + kEps < best_minsum_sum) {
            best_minsum_routes = best;
            best_minsum_sum = after_polish_sum;
        }
        if (after_polish_sum < best_sum) best_sum = after_polish_sum;

        // ---- Phase 5: alternating MINSUM relocate / swap / oropt loop ----
        const int interroute_phase_ms = unlimited ? 0 : std::max(500, (effective_total_ms * 25) / 100);
        SearchBudgetV5 inter_budget(interroute_phase_ms, 0, 32);
        last_metadata_["inter_budget_ms"] = std::to_string(interroute_phase_ms);
        int inter_iters = 0;
        int inter_relocate_accepts = 0;
        int inter_swap_accepts = 0;
        int inter_oropt_accepts = 0;
        std::string broken_by;
        while (!inter_budget.ShouldStop()) {
            bool any = false;
            std::string move_kind;
            if (TryMinsumRelocateV19(best, global_candidates, distance, inter_budget)) {
                any = true;
                ++inter_relocate_accepts;
                move_kind = "relocate";
            } else if (TryMinsumSwapV19(best, global_candidates, distance, inter_budget)) {
                any = true;
                ++inter_swap_accepts;
                move_kind = "swap";
            } else if (TryOrOptCrossRouteV19(best, global_candidates, distance, 1, inter_budget)) {
                any = true;
                ++inter_oropt_accepts;
                move_kind = "oropt1";
            } else if (TryOrOptCrossRouteV19(best, global_candidates, distance, 2, inter_budget)) {
                any = true;
                ++inter_oropt_accepts;
                move_kind = "oropt2";
            } else if (TryOrOptCrossRouteV19(best, global_candidates, distance, 3, inter_budget)) {
                any = true;
                ++inter_oropt_accepts;
                move_kind = "oropt3";
            }
            if (!any) break;
            ++inter_iters;
            if (broken_by.empty() && !ValidateLocalRoutes(best, node_count)) {
                broken_by = move_kind + "@" + std::to_string(inter_iters);
            }
            if (inter_iters >= 50000) break;  // safety

            // Periodically re-polish dirty routes so accepted moves get cleaned.
            if ((inter_iters & 31) == 0) {
                const int subbudget = std::min(2000, std::max(200, inter_budget.RemainingMs() / 8));
                SearchBudgetV5 sub(subbudget, 0, 32);
                PolishRoutesParallelV19(best, inst, std::max(1, ils_rounds_ / 2),
                                         local_candidates, node_count, sub,
                                         seed_ ^ 0xC2B2AE3DU ^ static_cast<unsigned int>(inter_iters),
                                         omp_polish_enabled_);
            }
        }
        SanitizeAndCompleteRoutesV7(best, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(best);
        const double after_inter_sum = RouteSumLengthV19(best, distance);
        last_metadata_["inter_iters"] = std::to_string(inter_iters);
        last_metadata_["inter_relocate_accepts"] = std::to_string(inter_relocate_accepts);
        last_metadata_["inter_swap_accepts"] = std::to_string(inter_swap_accepts);
        last_metadata_["inter_oropt_accepts"] = std::to_string(inter_oropt_accepts);
        last_metadata_["after_inter_sum"] = std::to_string(after_inter_sum);
        const auto inter_validation = ValidateLocalRoutesDetail(best, node_count);
        const bool inter_valid = inter_validation == ValidationFailure::Ok;
        last_metadata_["after_inter_valid"] = inter_valid ? "true" : "false";
        last_metadata_["after_inter_validation_code"] = std::to_string(static_cast<int>(inter_validation));
        last_metadata_["inter_broken_by"] = broken_by.empty() ? "none" : broken_by;
        if (inter_valid && after_inter_sum + kEps < best_minsum_sum) {
            best_minsum_routes = best;
            best_minsum_sum = after_inter_sum;
            last_metadata_["after_inter_snapshot"] = "true";
        } else {
            last_metadata_["after_inter_snapshot"] = "false";
        }
        if (after_inter_sum < best_sum) best_sum = after_inter_sum;

        // ---- Phase 6: second polish pass on improved routes ----
        const int second_polish_ms = unlimited ? 0 : std::max(500, (effective_total_ms * 12) / 100);
        SearchBudgetV5 second_polish_budget(second_polish_ms, 0, 32);
        PolishRoutesParallelV19(best, inst, std::max(1, ils_rounds_),
                                 local_candidates, node_count,
                                 second_polish_budget,
                                 seed_ ^ 0x6A09E667U,
                                 omp_polish_enabled_);
        SanitizeAndCompleteRoutesV7(best, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(best);
        const double after_polish2_sum = RouteSumLengthV19(best, distance);
        last_metadata_["after_polish2_sum"] = std::to_string(after_polish2_sum);
        if (ValidateLocalRoutes(best, node_count) && after_polish2_sum + kEps < best_minsum_sum) {
            best_minsum_routes = best;
            best_minsum_sum = after_polish2_sum;
        }
        if (after_polish2_sum < best_sum) best_sum = after_polish2_sum;

        // ---- Phase 7: final exhaustive 2-opt (matches `2opt+greed` baseline) ----
        // This is where small/medium n catches up: ApplyFirstImproving2OptV5
        // uses don't-look bits and may stop short of a true local optimum.
        // ImproveRoute2Opt-style sweep finishes the job — and on uniform
        // instances this single phase explains the ~5-9% gap with `2opt+greed`.
        if (final_2opt_enabled_) {
            int final_2opt_cap_ms;
            if (final_2opt_cap_ms_override_ > 0) {
                final_2opt_cap_ms = final_2opt_cap_ms_override_;
            } else if (unlimited) {
                final_2opt_cap_ms = 0;
            } else {
                const int remaining = total_budget.RemainingMs();
                if (node_count <= 25000) {
                    // Small/medium n: spend nearly all remaining time on
                    // exhaustive 2-opt to catch the `2opt+greed` baseline.
                    final_2opt_cap_ms = std::max(500, remaining - 200);
                } else if (node_count <= 50000) {
                    final_2opt_cap_ms = std::max(500, remaining / 2);
                } else {
                    // Large n: cap to ~12% of total budget; sweeps are O(L^2)
                    // and would otherwise burn all remaining time on a single route.
                    final_2opt_cap_ms = std::max(500, std::min(remaining,
                                                                (effective_total_ms * 12) / 100));
                }
            }
            last_metadata_["final_2opt_cap_ms"] = std::to_string(final_2opt_cap_ms);
            const auto final_started = std::chrono::steady_clock::now();
            // Shared wall-clock deadline. Each thread reads "remaining until
            // deadline" rather than starting its own budget, which would have
            // O(num_routes * cap_ms) total wall time.
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(final_2opt_cap_ms);
            // Routes shorter than this run an exhaustive O(L^2)-per-pass
            // 2-opt sweep (full local optimum). Larger routes first run a
            // candidate-restricted pass (cheap) then a single bounded
            // exhaustive sweep. The exhaustive sweep is what closes the gap
            // with the `2opt+greed` baseline at n=10k/25k.
            const int huge_route_threshold = 6000;
#ifdef _OPENMP
            if (omp_polish_enabled_ && static_cast<int>(best.size()) > 1) {
                const int route_count = static_cast<int>(best.size());
#pragma omp parallel for schedule(dynamic)
                for (int r = 0; r < route_count; ++r) {
                    DistanceOracleV5 local_distance(inst);
                    auto compute_remaining = [deadline]() {
                        const auto now = std::chrono::steady_clock::now();
                        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - now).count();
                        return delta > 50 ? static_cast<int>(delta) : 50;
                    };
                    if (static_cast<int>(best[static_cast<size_t>(r)].size()) > huge_route_threshold) {
                        // Cheap pre-pass with candidates first.
                        SearchBudgetV5 nl_budget(compute_remaining(), 0, 64);
                        NeighborList2OptOnRouteV19(best[static_cast<size_t>(r)],
                                                    local_candidates,
                                                    local_distance,
                                                    node_count,
                                                    nl_budget);
                    }
                    SearchBudgetV5 ex_budget(compute_remaining(), 0, 64);
                    Exhaustive2OptOnRouteV19(best[static_cast<size_t>(r)],
                                              local_distance,
                                              ex_budget);
                }
            } else
#endif
            {
                auto compute_remaining = [deadline]() {
                    const auto now = std::chrono::steady_clock::now();
                    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - now).count();
                    return delta > 50 ? static_cast<int>(delta) : 50;
                };
                for (auto& route : best) {
                    if (compute_remaining() <= 50) break;
                    if (static_cast<int>(route.size()) > huge_route_threshold) {
                        SearchBudgetV5 nl_budget(compute_remaining(), 0, 64);
                        NeighborList2OptOnRouteV19(route, local_candidates, distance, node_count, nl_budget);
                    }
                    SearchBudgetV5 ex_budget(compute_remaining(), 0, 64);
                    Exhaustive2OptOnRouteV19(route, distance, ex_budget);
                }
            }
            const auto final_done = std::chrono::steady_clock::now();
            last_metadata_["final_2opt_elapsed_ms"] = std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(final_done - final_started).count());
            SanitizeAndCompleteRoutesV7(best, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutesV19(best);
            const double after_final = RouteSumLengthV19(best, distance);
            last_metadata_["after_final_2opt_sum"] = std::to_string(after_final);
            if (ValidateLocalRoutes(best, node_count) && after_final + kEps < best_minsum_sum) {
                best_minsum_routes = best;
                best_minsum_sum = after_final;
            }
        }

        // ---- Phase 8: anytime improvement loop with leftover budget ----
        // Alternates one MINSUM cross-route move with one bounded exhaustive
        // 2-opt sweep on the dirtied routes. Runs only while total_budget has
        // headroom. Each iteration that improves MINSUM updates the snapshot.
        int phase8_iters = 0;
        int phase8_accepts = 0;
        if (!total_budget.ShouldStop()) {
            best = best_minsum_routes;
            while (!total_budget.ShouldStop()) {
                const int remaining = total_budget.RemainingMs();
                if (remaining <= 200) break;
                bool any = false;
                if (TryMinsumRelocateV19(best, global_candidates, distance, total_budget)) any = true;
                else if (TryMinsumSwapV19(best, global_candidates, distance, total_budget)) any = true;
                else if (TryOrOptCrossRouteV19(best, global_candidates, distance, 2, total_budget)) any = true;
                else if (TryOrOptCrossRouteV19(best, global_candidates, distance, 3, total_budget)) any = true;
                if (!any) break;
                ++phase8_iters;
                // Bounded 2-opt cleanup on all routes; share the remaining
                // budget so wall time stays bounded.
                const int cleanup_cap = std::min(remaining - 50, std::max(200, remaining / 8));
                SearchBudgetV5 cleanup_budget(cleanup_cap, 0, 64);
                for (auto& route : best) {
                    if (cleanup_budget.ShouldStop()) break;
                    const int route_size = static_cast<int>(route.size());
                    if (route_size > 6000) {
                        NeighborList2OptOnRouteV19(route, local_candidates, distance, node_count, cleanup_budget);
                    } else {
                        Exhaustive2OptOnRouteV19(route, distance, cleanup_budget);
                    }
                }
                SanitizeAndCompleteRoutesV7(best, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutesV19(best);
                const double phase8_sum = RouteSumLengthV19(best, distance);
                if (ValidateLocalRoutes(best, node_count) && phase8_sum + kEps < best_minsum_sum) {
                    best_minsum_routes = best;
                    best_minsum_sum = phase8_sum;
                    ++phase8_accepts;
                }
            }
        }
        last_metadata_["phase8_iters"] = std::to_string(phase8_iters);
        last_metadata_["phase8_accepts"] = std::to_string(phase8_accepts);

        // ---- Final return ----
        const auto solve_end = std::chrono::steady_clock::now();
        last_metadata_["total_elapsed_ms"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(solve_end - solve_start).count());
        last_metadata_["best_minsum_sum"] = std::to_string(best_minsum_sum);
        last_metadata_["best_minsum_max"] = std::to_string(MaxRouteLengthV19(best_minsum_routes, distance));

        // Hard contract: only ever return the snapshotted MINSUM-best routes.
        out.swap(best_minsum_routes);
        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(out);
        // If sanitation regressed (extremely unlikely), restore the snapshot
        // copy that was already validated. We keep a fallback in best.
        if (!ValidateLocalRoutes(out, node_count)) {
            out = best;
            SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutesV19(out);
        }
    }

private:
    // ---- helpers ----
    int LocalCandidateCount(int node_count) const {
        if (local_candidate_count_ > 0) return local_candidate_count_;
        if (node_count >= 100000) return 8;
        if (node_count >= 50000) return 10;
        if (node_count >= 10000) return 12;
        return 14;
    }
    int GlobalCandidateCount(int node_count, int local_count) const {
        if (global_candidate_count_ > 0) return std::max(global_candidate_count_, local_count + 4);
        if (node_count >= 100000) return std::max(local_count + 4, 14);
        if (node_count >= 50000) return std::max(local_count + 6, 18);
        return std::max(local_count + 8, 22);
    }
    int PopmusicSolutions(int node_count) const {
        if (node_count >= 100000) return 0;  // too expensive at 100k
        if (node_count >= 50000) return 2;
        if (node_count >= 10000) return 3;
        if (node_count >= 2000) return 4;
        return 0;
    }
    int PopmusicSample(int node_count) const {
        if (node_count >= 50000) return 1500;
        if (node_count >= 10000) return 800;
        return 400;
    }
    int PopmusicWindow(int node_count) const {
        if (node_count >= 50000) return 80;
        if (node_count >= 10000) return 60;
        return 40;
    }

    mtsp::RouteSet BuildFastSeedSafe(const mtsp::Instance& inst,
                                     const CandidateSets& cheap_candidates,
                                     DistanceOracleV5& distance,
                                     SearchBudgetV5& budget) const {
        mtsp::RouteSet routes;
        BuildFastSeedRoutesV7(routes,
                              inst,
                              cheap_candidates,
                              distance,
                              budget,
                              route_size_slack_,
                              lookahead_weight_,
                              depot_weight_);
        SanitizeAndCompleteRoutesV7(routes, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(routes);
        return routes;
    }

    // Lightweight Clarke-Wright savings seed (only invoked for n<=20k).
    mtsp::RouteSet BuildSavingsLikeSeed(const mtsp::Instance& inst,
                                        const CandidateSets& candidates,
                                        DistanceOracleV5& distance,
                                        SearchBudgetV5& budget) const {
        const int m = std::max(1, inst.GetSalesmanCount());
        const int n = inst.GetNodeCount();
        if (n <= 1) {
            mtsp::RouteSet trivial(static_cast<size_t>(m), std::vector<int>{0, 0});
            return trivial;
        }
        struct Edge { int a; int b; double saving; };
        std::vector<Edge> edges;
        edges.reserve(static_cast<size_t>(n) * 8ULL);
        const double depot_dist0 = 0.0;
        (void)depot_dist0;
        for (int i = 1; i < n; ++i) {
            if ((i & 1023) == 0 && budget.ForceCheck()) break;
            for (int j : candidates[static_cast<size_t>(i)]) {
                if (j > i && j != 0) {
                    const double saving = distance(0, i) + distance(0, j) - distance(i, j);
                    if (saving > 0.0) {
                        edges.push_back({i, j, saving});
                    }
                }
            }
        }
        std::sort(edges.begin(), edges.end(),
                  [](const Edge& a, const Edge& b) { return a.saving > b.saving; });

        std::vector<int> route_id(static_cast<size_t>(n), -1);
        std::vector<std::vector<int>> open_routes;
        open_routes.reserve(static_cast<size_t>(n));
        // Each city starts as its own open route.
        for (int city = 1; city < n; ++city) {
            route_id[static_cast<size_t>(city)] = static_cast<int>(open_routes.size());
            open_routes.push_back({city});
        }

        auto first_or_last = [](const std::vector<int>& route, int city) {
            return !route.empty() && (route.front() == city || route.back() == city);
        };

        for (const auto& edge : edges) {
            if (budget.ShouldStop()) break;
            const int ri = route_id[static_cast<size_t>(edge.a)];
            const int rj = route_id[static_cast<size_t>(edge.b)];
            if (ri < 0 || rj < 0 || ri == rj) continue;
            auto& routeI = open_routes[static_cast<size_t>(ri)];
            auto& routeJ = open_routes[static_cast<size_t>(rj)];
            if (!first_or_last(routeI, edge.a) || !first_or_last(routeJ, edge.b)) continue;

            // Orient routeI so a is at the back, routeJ so b is at the front.
            if (routeI.front() == edge.a) std::reverse(routeI.begin(), routeI.end());
            if (routeJ.back() == edge.b) std::reverse(routeJ.begin(), routeJ.end());

            // Stop merging once we are at exactly m routes; otherwise we'd be
            // forced to do balance-blind merges later.
            if (static_cast<int>(open_routes.size()) <= m) break;

            // Merge routeJ into routeI.
            for (int city : routeJ) {
                routeI.push_back(city);
                route_id[static_cast<size_t>(city)] = ri;
            }
            routeJ.clear();
        }

        // Collect non-empty open routes.
        mtsp::RouteSet collected;
        for (auto& route : open_routes) {
            if (!route.empty()) collected.push_back(std::move(route));
        }
        // Ensure exactly m routes by greedy split/merge to balance MINSUM.
        while (static_cast<int>(collected.size()) > m) {
            // Merge two shortest routes (lowest extra cost via depot).
            size_t best_i = 0;
            size_t best_j = 1;
            double best_cost = std::numeric_limits<double>::max();
            for (size_t i = 0; i < collected.size(); ++i) {
                for (size_t j = i + 1; j < collected.size(); ++j) {
                    if (collected[i].empty() || collected[j].empty()) continue;
                    const double cost = distance(collected[i].back(), collected[j].front());
                    if (cost < best_cost) {
                        best_cost = cost;
                        best_i = i;
                        best_j = j;
                    }
                }
            }
            collected[best_i].insert(collected[best_i].end(),
                                     collected[best_j].begin(),
                                     collected[best_j].end());
            collected.erase(collected.begin() + static_cast<std::ptrdiff_t>(best_j));
        }
        while (static_cast<int>(collected.size()) < m) {
            // Split the longest route in half.
            size_t longest = 0;
            size_t longest_size = 0;
            for (size_t i = 0; i < collected.size(); ++i) {
                if (collected[i].size() > longest_size) {
                    longest_size = collected[i].size();
                    longest = i;
                }
            }
            if (longest_size < 2) {
                collected.push_back({});
                continue;
            }
            std::vector<int> tail(collected[longest].begin() + static_cast<std::ptrdiff_t>(longest_size / 2),
                                  collected[longest].end());
            collected[longest].erase(collected[longest].begin() + static_cast<std::ptrdiff_t>(longest_size / 2),
                                     collected[longest].end());
            collected.push_back(std::move(tail));
        }

        // Wrap with depot, sanitize.
        mtsp::RouteSet routes(static_cast<size_t>(m), std::vector<int>{0});
        for (size_t i = 0; i < collected.size() && i < routes.size(); ++i) {
            for (int city : collected[i]) routes[i].push_back(city);
            routes[i].push_back(0);
        }
        for (size_t i = collected.size(); i < routes.size(); ++i) {
            routes[i].push_back(0);
        }
        SanitizeAndCompleteRoutesV7(routes, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutesV19(routes);
        return routes;
    }

    unsigned int seed_ = 1U;
    int time_budget_ms_ = 0;          // 0 = unlimited (run to convergence)
    int reserve_budget_ms_ = 200;
    int local_candidate_count_ = 0;   // 0 = auto by node_count
    int global_candidate_count_ = 0;  // 0 = auto by node_count
    int ils_rounds_ = 6;
    int thread_count_ = 0;
    int exact_candidate_threshold_ = 2048;
    int final_2opt_cap_ms_override_ = 0;
    bool omp_polish_enabled_ = true;
    bool final_2opt_enabled_ = true;
    bool polar_seed_enabled_ = true;
    bool roundrobin_seed_enabled_ = true;
    bool savings_seed_enabled_ = true;
    double route_size_slack_ = 0.40;
    double lookahead_weight_ = 0.6;
    double depot_weight_ = 0.05;
    mutable std::unordered_map<std::string, std::string> last_metadata_;
};

} // namespace

namespace mtsp {

static bool reg_lkh_mtsp_v20 = (SolverFactory::RegisterSolver("lkh-wrapper-v20", []() {
    return std::make_unique<LkhWrapperSolverV20>();
}),
                                true);

} // namespace mtsp
