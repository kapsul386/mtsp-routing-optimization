#pragma once

// Light intra-route 3-opt-like moves (or-opt with segment lengths 1..3).
// Cheaper than full 3-opt but addresses the move classes 2-opt cannot reach
// — relocating a short segment to a non-adjacent position. Used as a
// complementary pass after 2-opt during selective LS.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "04_route_index.hpp"
#include <algorithm>
#include <vector>

namespace mtsp::v21 {

// Or-opt: try moving a segment of length L (1, 2, or 3) to a new position
// inside the same route, candidate-restricted. Single-pass first-improving.
// Returns true on improvement.
template <typename DistanceFn>
inline bool TryOrOptIntraRoute(std::vector<int>& route, const CandidateSets& candidates,
                                DistanceFn& dist, int seg_len, SearchBudget& budget,
                                RouteIndex& idx) {
    if (seg_len < 1 || seg_len > 3) return false;
    if (static_cast<int>(route.size()) < seg_len + 4) return false;
    idx.Build(route);
    bool any = false;
    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t i = 1; i + static_cast<size_t>(seg_len) < route.size() - 1; ++i) {
            if (budget.ShouldStop()) return any;
            const int prev = route[i - 1];
            const int seg_first = route[i];
            const int seg_last = route[i + static_cast<size_t>(seg_len) - 1];
            const int next = route[i + static_cast<size_t>(seg_len)];
            const double removal = dist(prev, seg_first) + dist(seg_last, next) - dist(prev, next);
            if (removal <= kEps) continue;
            double best_delta = -kEps;
            size_t best_after = 0;
            bool best_reverse = false;
            for (int nb : candidates[static_cast<size_t>(seg_first)]) {
                const int p = idx.Get(nb);
                if (p < 0 || p + 1 >= static_cast<int>(route.size())) continue;
                const size_t after = static_cast<size_t>(p);
                if (after >= i - 1 && after <= i + static_cast<size_t>(seg_len) - 1) continue;
                const int a = route[after];
                const int b = route[after + 1];
                const double ins = dist(a, seg_first) + dist(seg_last, b) - dist(a, b);
                const double delta = ins - removal;
                if (delta + kEps < best_delta) { best_delta = delta; best_after = after; best_reverse = false; }
                if (seg_len >= 2) {
                    const double ins_rev = dist(a, seg_last) + dist(seg_first, b) - dist(a, b);
                    const double delta_rev = ins_rev - removal;
                    if (delta_rev + kEps < best_delta) { best_delta = delta_rev; best_after = after; best_reverse = true; }
                }
            }
            if (best_delta < -kEps) {
                std::vector<int> block(route.begin() + static_cast<std::ptrdiff_t>(i),
                                       route.begin() + static_cast<std::ptrdiff_t>(i + static_cast<size_t>(seg_len)));
                if (best_reverse) std::reverse(block.begin(), block.end());
                route.erase(route.begin() + static_cast<std::ptrdiff_t>(i),
                            route.begin() + static_cast<std::ptrdiff_t>(i + static_cast<size_t>(seg_len)));
                size_t insert_pos = best_after;
                if (insert_pos >= i) insert_pos -= static_cast<size_t>(seg_len);
                route.insert(route.begin() + static_cast<std::ptrdiff_t>(insert_pos + 1), block.begin(), block.end());
                idx.Build(route);
                improved = true;
                any = true;
                break;
            }
        }
    }
    return any;
}

// Run Or-opt for segment lengths 1, 2, 3 in sequence.
template <typename DistanceFn>
inline bool TryOrOptAllLengths(std::vector<int>& route, const CandidateSets& candidates,
                                DistanceFn& dist, SearchBudget& budget, RouteIndex& idx) {
    bool any = false;
    for (int seg = 1; seg <= 3; ++seg) {
        if (budget.ShouldStop()) break;
        if (TryOrOptIntraRoute(route, candidates, dist, seg, budget, idx)) any = true;
    }
    return any;
}

}  // namespace mtsp::v21
