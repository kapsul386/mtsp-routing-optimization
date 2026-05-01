#pragma once

// Periodic re-optimization of two routes treated as a single TSP. When the
// ALNS loop has not improved for a while, picking the longest route and its
// nearest neighbor (PickClosestRoute) and running Exhaustive2Opt over the
// concatenated city sequence often finds a refactoring that destroy+repair
// would not stumble onto. Disabled on n>60k where individual routes are too
// long for O(L^2) 2-opt to fit in the per-call sub-budget.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "04_route_index.hpp"
#include "05_route_list.hpp"
#include "08_route_local_search.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace mtsp::v21 {

// Route-pair re-optimization. Picks two routes (typically: longest + nearest by
// centroid), wipes them, and rebuilds them as a focused two-route subproblem.
// Effective for inter-route improvements that the global ALNS misses because
// it spreads attention across all m routes.
//
// Strategy:
//   1. Snapshot rl.
//   2. Wipe r1 and r2 (collect customers).
//   3. Cheapest-insertion of customers into {r1, r2} only (constrained).
//   4. NeighborList2Opt + bounded Exhaustive2Opt on each.
//   5. If new (L_r1 + L_r2) < old, accept; else revert.
//
// Cheapness: for ~2k+2k=4k customers, the wipe+insert+2opt costs ~10ms. Even
// called every 100 iterations the overhead is well under budget.

inline std::pair<double, double> ComputeRouteCentroid(const std::vector<int>& route,
                                                      const std::vector<Coord>& coords) {
    double sx = 0.0, sy = 0.0;
    int count = 0;
    for (size_t i = 1; i + 1 < route.size(); ++i) {
        const int c = route[i];
        sx += coords[static_cast<size_t>(c)].first;
        sy += coords[static_cast<size_t>(c)].second;
        ++count;
    }
    if (count == 0) return {0.0, 0.0};
    return {sx / count, sy / count};
}

inline int PickClosestRoute(const RouteList& rl, int r_target, const std::vector<Coord>& coords) {
    if (rl.RouteCount() < 2) return -1;
    const auto target_centroid = ComputeRouteCentroid(rl.Route(r_target), coords);
    int best = -1;
    double best_dist = std::numeric_limits<double>::max();
    for (int r = 0; r < rl.RouteCount(); ++r) {
        if (r == r_target) continue;
        if (rl.Route(r).size() < 4) continue;  // skip empty/tiny
        const auto cen = ComputeRouteCentroid(rl.Route(r), coords);
        const double dx = cen.first - target_centroid.first;
        const double dy = cen.second - target_centroid.second;
        const double dd = dx * dx + dy * dy;
        if (dd < best_dist) { best_dist = dd; best = r; }
    }
    return best;
}

// Constrained cheapest-insertion: insert each removed city at the cheapest
// position restricted to {r1, r2}. After all inserted, run 2-opt on each.
//
// Returns true if AcceptPolicy::ScalarCost(rl) strictly decreases (works
// uniformly for MINSUM scalar and MIN-MAX lex objectives).
template <typename AcceptPolicy>
inline bool TryReoptimizeRoutePair(RouteList& rl, int r1, int r2,
                                    DistanceOracle& d,
                                    const CandidateSets& candidates,
                                    SearchBudget& budget,
                                    std::mt19937& rng,
                                    AcceptPolicy& accept) {
    if (r1 == r2) return false;
    if (rl.Route(r1).size() < 4 || rl.Route(r2).size() < 4) return false;

    const double old_scalar = accept.ScalarCost(rl);

    // Snapshot the entire RouteList (cheap — m vectors)
    RouteSet snap; rl.StoreTo(snap);

    // Wipe both routes
    auto removed1 = rl.WipeRouteCustomers(r1, d);
    auto removed2 = rl.WipeRouteCustomers(r2, d);
    std::vector<int> removed = std::move(removed1);
    removed.insert(removed.end(), removed2.begin(), removed2.end());
    if (removed.empty()) return false;

    // Constrained cheapest-insertion into {r1, r2}
    std::shuffle(removed.begin(), removed.end(), rng);
    for (int city : removed) {
        if (rl.RouteOf(city) >= 0) continue;
        double best_delta = std::numeric_limits<double>::max();
        int best_r = -1, best_after = -1;
        for (int r : {r1, r2}) {
            const auto& route = rl.Route(r);
            // Try every insertion gap (small subset via candidates + endpoints)
            std::vector<int> positions;
            positions.push_back(0);
            if (route.size() >= 2) positions.push_back(static_cast<int>(route.size()) - 2);
            // Candidate-driven positions (cheap)
            for (int nb : candidates[static_cast<size_t>(city)]) {
                if (nb == 0) continue;
                if (rl.RouteOf(nb) != r) continue;
                for (size_t i = 1; i + 1 < route.size(); ++i) {
                    if (route[i] == nb) {
                        if (static_cast<int>(i) - 1 >= 0) positions.push_back(static_cast<int>(i) - 1);
                        if (i < route.size() - 2) positions.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
            // Also scan all gaps if route is small (cheap, finds true cheapest)
            if (route.size() < 200) {
                positions.clear();
                positions.reserve(route.size());
                for (size_t i = 0; i + 1 < route.size(); ++i) positions.push_back(static_cast<int>(i));
            }
            for (int p : positions) {
                if (p < 0 || p + 1 >= static_cast<int>(route.size())) continue;
                const int a = route[static_cast<size_t>(p)];
                const int b = route[static_cast<size_t>(p + 1)];
                const double dlt = d(a, city) + d(city, b) - d(a, b);
                if (dlt < best_delta) { best_delta = dlt; best_r = r; best_after = p; }
            }
        }
        if (best_r < 0) {
            // Fallback: append to r1
            const auto& route = rl.Route(r1);
            rl.InsertAt(r1, static_cast<int>(route.size()) - 2, city, d);
        } else {
            rl.InsertAt(best_r, best_after, city, d);
        }
    }

    // 2-opt cleanup on both rebuilt routes (time-budgeted small)
    {
        const int polish_ms = std::max(50, budget.RemainingMs() / 50);
        SearchBudget pair_budget = budget.SubBudget(polish_ms);
        RouteIndex idx(static_cast<int>(candidates.size()));
        auto polish_route = [&](int r) {
            auto route_copy = rl.Route(r);
            NeighborList2Opt(route_copy, candidates, d, pair_budget, idx);
            if (route_copy.size() < 5000 && !pair_budget.ForceCheck()) {
                Exhaustive2Opt(route_copy, d, pair_budget);
            }
            rl.ReplaceRoute(r, std::move(route_copy), d);
        };
        polish_route(r1);
        polish_route(r2);
    }

    const double new_scalar = accept.ScalarCost(rl);
    if (new_scalar + kEps < old_scalar) {
        return true;
    }
    // Revert
    rl.LoadFrom(snap, d);
    return false;
}

}  // namespace mtsp::v21
