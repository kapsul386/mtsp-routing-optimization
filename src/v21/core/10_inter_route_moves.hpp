#pragma once

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "04_route_index.hpp"
#include "05_route_list.hpp"
#include <algorithm>
#include <vector>

namespace mtsp::v21 {

// AcceptPolicy contract:
//   double Cost(const RouteList&)                // current scalar cost
//   double DeltaIfRelocate(rl, from_r, to_r, dL_from, dL_to)
//   bool   StrictAccept(double delta)            // for greedy improving moves
//
// We provide concrete policies in minsum/minmax/*_accept.hpp. Move operators
// here pick the best improving move under StrictAccept (greedy descent).

// Best-improving cross-route relocate: move one customer from any route to any
// other if AcceptPolicy::StrictAccept(delta) holds.
template <typename AcceptPolicy>
inline bool TryRelocateInterRoute(RouteList& rl, const CandidateSets& candidates,
                                   DistanceOracle& d, AcceptPolicy& accept,
                                   SearchBudget& budget) {
    const int m = rl.RouteCount();
    if (m < 2) return false;
    std::vector<RouteIndex> idx;
    idx.reserve(static_cast<size_t>(m));
    for (int r = 0; r < m; ++r) {
        idx.emplace_back(static_cast<int>(candidates.size()));
        idx.back().Build(rl.Route(r));
    }

    double best_delta = 0.0;
    int best_from = -1, best_to = -1;
    size_t best_i = 0, best_after = 0;
    for (int from = 0; from < m && !budget.ShouldStop(); ++from) {
        const auto& Rf = rl.Route(from);
        for (size_t i = 1; i + 1 < Rf.size() && !budget.ShouldStop(); ++i) {
            const int city = Rf[i];
            const int prev = Rf[i - 1];
            const int next = Rf[i + 1];
            const double dL_from = d(prev, next) - d(prev, city) - d(city, next);
            for (int to = 0; to < m; ++to) {
                if (to == from || rl.Route(to).size() < 2) continue;
                const auto& Rt = rl.Route(to);
                std::vector<size_t> positions;
                positions.reserve(candidates[static_cast<size_t>(city)].size() + 4ULL);
                positions.push_back(0);
                positions.push_back(Rt.size() - 2);
                for (int nb : candidates[static_cast<size_t>(city)]) {
                    const int p = idx[static_cast<size_t>(to)].Get(nb);
                    if (p >= 0 && p + 1 < static_cast<int>(Rt.size()))
                        positions.push_back(static_cast<size_t>(p));
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                for (size_t after : positions) {
                    const int a = Rt[after];
                    const int b = Rt[after + 1];
                    const double dL_to = d(a, city) + d(city, b) - d(a, b);
                    const double delta = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to);
                    if (delta + kEps < best_delta) {
                        best_delta = delta;
                        best_from = from; best_to = to; best_i = i; best_after = after;
                    }
                }
            }
        }
    }
    if (best_from < 0) return false;
    if (!accept.StrictAccept(best_delta)) return false;
    const int city = rl.Route(best_from)[best_i];
    rl.Remove(city, d);
    rl.InsertAt(best_to, static_cast<int>(best_after), city, d);
    return true;
}

// Best-improving cross-route 1-1 swap.
template <typename AcceptPolicy>
inline bool TrySwapInterRoute(RouteList& rl, const CandidateSets& candidates,
                               DistanceOracle& d, AcceptPolicy& accept,
                               SearchBudget& budget) {
    const int m = rl.RouteCount();
    if (m < 2) return false;
    std::vector<RouteIndex> idx;
    idx.reserve(static_cast<size_t>(m));
    for (int r = 0; r < m; ++r) {
        idx.emplace_back(static_cast<int>(candidates.size()));
        idx.back().Build(rl.Route(r));
    }
    double best_delta = 0.0;
    int best_a = -1, best_b = -1;
    size_t best_i = 0, best_j = 0;
    for (int a = 0; a < m && !budget.ShouldStop(); ++a) {
        const auto& Ra = rl.Route(a);
        for (size_t i = 1; i + 1 < Ra.size() && !budget.ShouldStop(); ++i) {
            const int city_a = Ra[i];
            const int prev_a = Ra[i - 1];
            const int next_a = Ra[i + 1];
            for (int nb : candidates[static_cast<size_t>(city_a)]) {
                if (nb == 0) continue;
                for (int b = 0; b < m; ++b) {
                    if (b == a) continue;
                    const int j = idx[static_cast<size_t>(b)].Get(nb);
                    if (j <= 0 || j + 1 >= static_cast<int>(rl.Route(b).size())) continue;
                    const auto& Rb = rl.Route(b);
                    const int city_b = Rb[static_cast<size_t>(j)];
                    const int prev_b = Rb[static_cast<size_t>(j - 1)];
                    const int next_b = Rb[static_cast<size_t>(j + 1)];
                    const double removed = d(prev_a, city_a) + d(city_a, next_a) +
                                           d(prev_b, city_b) + d(city_b, next_b);
                    const double added = d(prev_a, city_b) + d(city_b, next_a) +
                                         d(prev_b, city_a) + d(city_a, next_b);
                    const double dL_a = (d(prev_a, city_b) + d(city_b, next_a)) - (d(prev_a, city_a) + d(city_a, next_a));
                    const double dL_b = (d(prev_b, city_a) + d(city_a, next_b)) - (d(prev_b, city_b) + d(city_b, next_b));
                    (void)removed; (void)added;
                    const double delta = accept.DeltaForCrossRouteMove(rl, a, b, dL_a, dL_b);
                    if (delta + kEps < best_delta) {
                        best_delta = delta;
                        best_a = a; best_b = b; best_i = i; best_j = static_cast<size_t>(j);
                    }
                }
            }
        }
    }
    if (best_a < 0) return false;
    if (!accept.StrictAccept(best_delta)) return false;
    auto& Ra = rl.MutableRoute(best_a);
    auto& Rb = rl.MutableRoute(best_b);
    const int ca = Ra[best_i], cb = Rb[best_j];
    Ra[best_i] = cb;
    Rb[best_j] = ca;
    rl.RecomputeLength(best_a, d);
    rl.RecomputeLength(best_b, d);
    // Update route_of via ReplaceRoute trick: simplest is a direct route_of_ swap.
    // Since RouteList doesn't expose route_of_ writes, we re-validate via ReplaceRoute.
    {
        auto na = Ra; auto nb = Rb;
        rl.ReplaceRoute(best_a, std::move(na), d);
        rl.ReplaceRoute(best_b, std::move(nb), d);
    }
    return true;
}

// Or-opt cross-route: move a segment of length 1..3 from route A to route B.
template <typename AcceptPolicy>
inline bool TryOrOptCrossRoute(RouteList& rl, const CandidateSets& candidates,
                                DistanceOracle& d, AcceptPolicy& accept,
                                int seg_len, SearchBudget& budget) {
    if (seg_len < 1 || seg_len > 3) return false;
    const int m = rl.RouteCount();
    if (m < 2) return false;
    std::vector<RouteIndex> idx;
    idx.reserve(static_cast<size_t>(m));
    for (int r = 0; r < m; ++r) {
        idx.emplace_back(static_cast<int>(candidates.size()));
        idx.back().Build(rl.Route(r));
    }
    double best_delta = 0.0;
    int best_from = -1, best_to = -1;
    size_t best_i = 0, best_after = 0;
    bool best_reverse = false;
    for (int from = 0; from < m && !budget.ShouldStop(); ++from) {
        const auto& Rf = rl.Route(from);
        if (static_cast<int>(Rf.size()) < seg_len + 3) continue;
        for (size_t i = 1; i + static_cast<size_t>(seg_len) < Rf.size(); ++i) {
            if (budget.ShouldStop()) break;
            const int prev = Rf[i - 1];
            const int seg_first = Rf[i];
            const int seg_last = Rf[i + static_cast<size_t>(seg_len) - 1];
            const int next = Rf[i + static_cast<size_t>(seg_len)];
            const double dL_from = d(prev, next) - d(prev, seg_first) - d(seg_last, next);
            for (int to = 0; to < m; ++to) {
                if (to == from || rl.Route(to).size() < 2) continue;
                const auto& Rt = rl.Route(to);
                std::vector<size_t> positions;
                positions.reserve(candidates[static_cast<size_t>(seg_first)].size() +
                                  candidates[static_cast<size_t>(seg_last)].size() + 4ULL);
                positions.push_back(0);
                positions.push_back(Rt.size() - 2);
                for (int nb : candidates[static_cast<size_t>(seg_first)]) {
                    const int p = idx[static_cast<size_t>(to)].Get(nb);
                    if (p >= 0 && p + 1 < static_cast<int>(Rt.size())) positions.push_back(static_cast<size_t>(p));
                }
                for (int nb : candidates[static_cast<size_t>(seg_last)]) {
                    const int p = idx[static_cast<size_t>(to)].Get(nb);
                    if (p >= 0 && p + 1 < static_cast<int>(Rt.size())) positions.push_back(static_cast<size_t>(p));
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                for (size_t after : positions) {
                    const int a = Rt[after];
                    const int b = Rt[after + 1];
                    const double dL_to_fwd = d(a, seg_first) + d(seg_last, b) - d(a, b);
                    const double delta_fwd = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to_fwd);
                    if (delta_fwd + kEps < best_delta) {
                        best_delta = delta_fwd;
                        best_from = from; best_to = to; best_i = i; best_after = after; best_reverse = false;
                    }
                    if (seg_len >= 2) {
                        const double dL_to_rev = d(a, seg_last) + d(seg_first, b) - d(a, b);
                        const double delta_rev = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to_rev);
                        if (delta_rev + kEps < best_delta) {
                            best_delta = delta_rev;
                            best_from = from; best_to = to; best_i = i; best_after = after; best_reverse = true;
                        }
                    }
                }
            }
        }
    }
    if (best_from < 0) return false;
    if (!accept.StrictAccept(best_delta)) return false;

    auto& Rf = rl.MutableRoute(best_from);
    std::vector<int> block(Rf.begin() + static_cast<std::ptrdiff_t>(best_i),
                            Rf.begin() + static_cast<std::ptrdiff_t>(best_i + static_cast<size_t>(seg_len)));
    if (best_reverse) std::reverse(block.begin(), block.end());
    Rf.erase(Rf.begin() + static_cast<std::ptrdiff_t>(best_i),
             Rf.begin() + static_cast<std::ptrdiff_t>(best_i + static_cast<size_t>(seg_len)));
    rl.RecomputeLength(best_from, d);
    auto& Rt = rl.MutableRoute(best_to);
    Rt.insert(Rt.begin() + static_cast<std::ptrdiff_t>(best_after + 1), block.begin(), block.end());
    rl.RecomputeLength(best_to, d);
    // Refresh route_of_ via ReplaceRoute (full re-mapping)
    auto Rf_copy = Rf; auto Rt_copy = Rt;
    rl.ReplaceRoute(best_from, std::move(Rf_copy), d);
    rl.ReplaceRoute(best_to, std::move(Rt_copy), d);
    return true;
}

}  // namespace mtsp::v21
