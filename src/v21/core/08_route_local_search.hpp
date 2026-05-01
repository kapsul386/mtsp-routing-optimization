#pragma once

// Single-route local search: Exhaustive2Opt (O(L^2) to a true local optimum)
// and NeighborList2Opt (O(L*k) candidate-restricted with don't-look bits).
// Used by the seed polish phase, by selective LS inside the ALNS loop, and
// by IteratedLocalSearchSingleRoute (perturbation + 2-opt restart).

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "04_route_index.hpp"
#include <algorithm>
#include <random>
#include <vector>

namespace mtsp::v21 {

// Exhaustive O(L^2) 2-opt to a true local optimum, raw arithmetic (matches the
// 2opt+greed baseline). Time-budgeted; polls every 4096 inner iters.
template <typename DistanceFn>
inline void Exhaustive2Opt(std::vector<int>& route, DistanceFn& dist, SearchBudget& budget,
                           int* passes_out = nullptr) {
    if (route.size() <= 4) { if (passes_out) *passes_out = 0; return; }
    bool improved = true;
    int passes = 0;
    while (improved) {
        improved = false;
        ++passes;
        for (size_t i = 1; i + 2 < route.size(); ++i) {
            if (budget.ShouldStop()) { if (passes_out) *passes_out = passes; return; }
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                if ((j & 4095U) == 0 && budget.ShouldStop()) { if (passes_out) *passes_out = passes; return; }
                const double before = dist(route[i - 1], route[i]) + dist(route[j], route[j + 1]);
                const double after = dist(route[i - 1], route[j]) + dist(route[i], route[j + 1]);
                if (after + kEps < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                }
            }
        }
    }
    if (passes_out) *passes_out = passes;
}

// Candidate-list-driven 2-opt with don't-look bits. O(L * k) per pass — used
// for huge routes where O(L^2) is impractical. Returns true if improved at
// least once.
template <typename DistanceFn>
inline bool NeighborList2Opt(std::vector<int>& route, const CandidateSets& candidates,
                              DistanceFn& dist, SearchBudget& budget, RouteIndex& idx) {
    if (route.size() <= 4) return false;
    bool any = false;
    std::vector<char> dontlook(route.size(), 0);
    bool improved = true;
    int safety_passes = 0;
    while (improved && safety_passes < 64) {
        improved = false;
        ++safety_passes;
        idx.Build(route);
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            if (budget.ShouldStop()) return any;
            if (dontlook[i]) continue;
            const int a = route[i];
            const int prev_a = route[i - 1];
            const int next_a = route[i + 1];
            bool local_improved = false;
            for (int b : candidates[static_cast<size_t>(a)]) {
                if (b == 0 || b == prev_a || b == next_a || b == a) continue;
                const int j = idx.Get(b);
                if (j <= 0 || j + 1 >= static_cast<int>(route.size())) continue;
                if (j == static_cast<int>(i)) continue;
                size_t lo = i, hi = static_cast<size_t>(j);
                if (lo > hi) std::swap(lo, hi);
                if (lo + 1 >= hi) continue;
                const double before = dist(route[lo - 1], route[lo]) + dist(route[hi], route[hi + 1]);
                const double after = dist(route[lo - 1], route[hi]) + dist(route[lo], route[hi + 1]);
                if (after + kEps < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(lo),
                                 route.begin() + static_cast<std::ptrdiff_t>(hi + 1));
                    idx.Build(route);
                    dontlook[lo] = 0;
                    dontlook[hi] = 0;
                    if (lo > 0) dontlook[lo - 1] = 0;
                    if (hi + 1 < route.size()) dontlook[hi + 1] = 0;
                    improved = true;
                    any = true;
                    local_improved = true;
                    break;
                }
            }
            if (!local_improved) dontlook[i] = 1;
        }
    }
    return any;
}

// Double-bridge perturbation: quartet random cut + reattach as A C B D.
inline void DoubleBridgeKick(std::vector<int>& route, std::mt19937& rng) {
    if (route.size() < 9) return;  // need 4 cuts away from depot
    const int n = static_cast<int>(route.size());
    // Pick 3 cut points strictly inside (between positions 1 and n-2)
    const int p1 = 1 + static_cast<int>(rng() % static_cast<unsigned>(n / 4));
    const int p2 = p1 + 1 + static_cast<int>(rng() % static_cast<unsigned>(n / 4));
    const int p3 = p2 + 1 + static_cast<int>(rng() % static_cast<unsigned>(n / 4));
    if (p3 >= n - 1) return;
    std::vector<int> result;
    result.reserve(route.size());
    result.insert(result.end(), route.begin(), route.begin() + p1);
    result.insert(result.end(), route.begin() + p3, route.end() - 1);
    result.insert(result.end(), route.begin() + p2, route.begin() + p3);
    result.insert(result.end(), route.begin() + p1, route.begin() + p2);
    result.push_back(route.back());
    route = std::move(result);
}

// Iterated local search on a single route: 2-opt → DoubleBridge → 2-opt loop.
template <typename DistanceFn>
inline void IteratedLocalSearchSingleRoute(std::vector<int>& route,
                                           const CandidateSets& candidates,
                                           DistanceFn& dist,
                                           SearchBudget& budget,
                                           RouteIndex& idx,
                                           std::mt19937& rng,
                                           int rounds) {
    if (route.size() <= 6) return;
    auto length = [&]() { return RouteLengthGeneric(route, dist); };
    NeighborList2Opt(route, candidates, dist, budget, idx);
    if (route.size() < 4000) Exhaustive2Opt(route, dist, budget);
    double best_len = length();
    std::vector<int> best_route = route;
    for (int r = 0; r < rounds && !budget.ShouldStop(); ++r) {
        DoubleBridgeKick(route, rng);
        NeighborList2Opt(route, candidates, dist, budget, idx);
        if (route.size() < 4000 && !budget.ShouldStop()) Exhaustive2Opt(route, dist, budget);
        const double cur_len = length();
        if (cur_len + kEps < best_len) { best_len = cur_len; best_route = route; }
        else { route = best_route; }
    }
    route = best_route;
}

}  // namespace mtsp::v21
