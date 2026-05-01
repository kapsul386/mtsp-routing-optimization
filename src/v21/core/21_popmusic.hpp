#pragma once

// POPMUSIC-style spatial decomposition step. Picks a center customer, gathers
// the K nearest customers (across routes) into a sub-problem, removes them
// from their current positions, and runs cheapest-insertion + 2-opt over the
// concatenated remainders. Strict accept (only commits on improvement).
// Empirically off by default for n>60k (post-polish solutions are already
// at a local optimum that this step cannot beat); kept as an experimental
// hook controllable via AutoTuneParams.popmusic_every.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include "04_route_index.hpp"
#include "05_route_list.hpp"
#include "08_route_local_search.hpp"
#include "14_repair_ops.hpp"
#include <mtsp_solver.h>
#include <algorithm>
#include <random>
#include <vector>

namespace mtsp::v21 {

// POPMUSIC-style spatial decomposition step.
//
// Idea: pick a spatial centre, take its K nearest customers, remove them all
// (efficiently, in a single O(N) filter pass per dirty route — NOT K times
// linear-scan), then rebuild that subproblem with cheapest-insertion +
// bounded exhaustive 2-opt on the dirty routes only.
//
// This is "decomposition by geographic locality" rather than by route — the
// removed customers may have come from any of the m routes, but they sit in
// the same geographic neighbourhood, so re-stitching them often discovers
// inter-route improvements that don't appear from purely random destroy
// operators.
//
// Effective on n > 60k where:
//   - PT is off (per-step cost too high to amortise across replicas)
//   - pair-reopt is off (routes are too long for full wipe-rebuild to gain)
//   - global ALNS does few iterations per second, so each iteration must do
//     more work to be worth its time
//
// Returns true if `accept.ScalarCost(rl)` strictly improved.
template <typename AcceptPolicy>
inline bool TryPopmusicStep(RouteList& rl,
                             AcceptPolicy& accept,
                             const KDTree2D& kdtree,
                             DistanceOracle& d,
                             const CandidateSets& candidates,
                             SearchBudget& budget,
                             std::mt19937& rng,
                             int K_pop) {
    if (K_pop <= 0 || rl.RouteCount() < 2) return false;
    const int n = rl.NodeCount();
    if (n <= 1) return false;

    // ---- Snapshot ----
    RouteSet snap; rl.StoreTo(snap);
    const double pre_cost = accept.ScalarCost(rl);

    // ---- Pick a placed seed customer ----
    // Sampling without rejection: scan from a random offset.
    int seed = -1;
    {
        const int offset = static_cast<int>(rng() % static_cast<unsigned>(std::max(1, n - 1)));
        for (int probe = 0; probe < n - 1; ++probe) {
            const int c = 1 + ((offset + probe) % (n - 1));
            if (rl.RouteOf(c) >= 0) { seed = c; break; }
        }
    }
    if (seed < 0) return false;

    // ---- Get K_pop nearest via KDTree ----
    std::vector<int> nearest;
    nearest.reserve(static_cast<size_t>(K_pop) + 1);
    kdtree.Knn(seed, K_pop, nearest);
    nearest.push_back(seed);

    // ---- Mark selected, identify dirty routes ----
    std::vector<char> selected(static_cast<size_t>(n), 0);
    std::vector<char> dirty(static_cast<size_t>(rl.RouteCount()), 0);
    std::vector<int> removed;
    removed.reserve(nearest.size());
    for (int c : nearest) {
        if (c == 0) continue;
        if (selected[static_cast<size_t>(c)]) continue;
        selected[static_cast<size_t>(c)] = 1;
        const int r = rl.RouteOf(c);
        if (r >= 0) {
            dirty[static_cast<size_t>(r)] = 1;
            removed.push_back(c);
        }
    }
    if (removed.empty()) return false;

    // ---- Filter selected customers out of every dirty route — O(N) total ----
    // (One linear scan per dirty route, not K linear scans.)
    for (int r = 0; r < rl.RouteCount(); ++r) {
        if (!dirty[static_cast<size_t>(r)]) continue;
        const auto& route = rl.Route(r);
        std::vector<int> new_route;
        new_route.reserve(route.size());
        for (int c : route) {
            if (c != 0 && selected[static_cast<size_t>(c)]) continue;
            new_route.push_back(c);
        }
        // Ensure depot endpoints
        if (new_route.empty() || new_route.front() != 0) new_route.insert(new_route.begin(), 0);
        if (new_route.size() < 2 || new_route.back() != 0) new_route.push_back(0);
        rl.ReplaceRoute(r, std::move(new_route), d);
    }

    // ---- Re-insert removed customers via cheapest-insertion ----
    // Regret-2 is too expensive at K=1k+ (O(K^2 * m * cand)) and starves the
    // outer ALNS loop on n=100k. Cheapest is fast (O(K * cand)) and competitive.
    RepairContext rctx{d, candidates, accept.IsMinMax()};
    RepairCheapestInsertion(rl, removed, rng, rctx);

    // ---- 2-opt cleanup on dirty routes (time-budgeted) ----
    if (!budget.ForceCheck()) {
        const int polish_ms = std::max(300, budget.RemainingMs() / 30);
        SearchBudget pop_budget = budget.SubBudget(polish_ms);
        RouteIndex local_idx(n);
        for (int r = 0; r < rl.RouteCount(); ++r) {
            if (!dirty[static_cast<size_t>(r)] || pop_budget.ForceCheck()) continue;
            auto route_copy = rl.Route(r);
            NeighborList2Opt(route_copy, candidates, d, pop_budget, local_idx);
            if (route_copy.size() < 5000 && !pop_budget.ForceCheck()) {
                Exhaustive2Opt(route_copy, d, pop_budget);
            }
            rl.ReplaceRoute(r, std::move(route_copy), d);
        }
    }

    // ---- Accept-or-revert ----
    const double post_cost = accept.ScalarCost(rl);
    if (post_cost + kEps < pre_cost) {
        return true;
    }
    rl.LoadFrom(snap, d);
    return false;
}

}  // namespace mtsp::v21
