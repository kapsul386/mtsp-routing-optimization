#pragma once

// DualOpt/FILO2-inspired improvement operators for the v21 large-instance
// path.
//
// 1. Granular inter-route pass: candidate-list relocate + swap moves across
//    routes. It mirrors FILO2's granular local search idea: inspect only
//    spatially close neighbours from the global candidate set, accept only
//    strict scalar improvements, and respect the optional route_cap.
//
// 2. Region reopt: pick an expensive spatial centre, remove its K nearest
//    customers, rebuild the local subproblem with candidate-restricted repair,
//    then polish touched routes. This is the DualOpt-style region step:
//    optimize a geographic window rather than a whole route pair.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include "04_route_index.hpp"
#include "05_route_list.hpp"
#include "08_route_local_search.hpp"
#include "14_repair_ops.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace mtsp::v21 {

struct GranularMoveStats {
    int relocate_accepts = 0;
    int swap_accepts = 0;
    int two_optstar_accepts = 0;
    int oropt_accepts = 0;
    int region_calls = 0;
    int region_accepts = 0;
};

namespace detail {

inline std::vector<int> BuildSampledCustomers(const RouteList& rl, int scan_limit,
                                              std::mt19937& rng) {
    std::vector<int> out;
    if (scan_limit <= 0 || rl.NodeCount() <= 1) return out;
    out.reserve(static_cast<size_t>(std::min(scan_limit, std::max(0, rl.NodeCount() - 1))));

    const int m = rl.RouteCount();
    if (m <= 0) return out;
    const int route_offset = static_cast<int>(rng() % static_cast<unsigned>(m));
    for (int rr = 0; rr < m && static_cast<int>(out.size()) < scan_limit; ++rr) {
        const int r = (route_offset + rr) % m;
        const auto& route = rl.Route(r);
        const int customers = static_cast<int>(route.size()) - 2;
        if (customers <= 0) continue;
        const int per_route_quota = std::max(1, scan_limit / std::max(1, m));
        const int stride = std::max(1, customers / per_route_quota);
        const int start = 1 + static_cast<int>(rng() % static_cast<unsigned>(std::max(1, stride)));
        for (int pos = start; pos <= customers && static_cast<int>(out.size()) < scan_limit; pos += stride) {
            out.push_back(route[static_cast<size_t>(pos)]);
        }
    }

    if (static_cast<int>(out.size()) < scan_limit) {
        const int offset = static_cast<int>(rng() % static_cast<unsigned>(std::max(1, rl.NodeCount() - 1)));
        for (int probe = 0; probe < rl.NodeCount() - 1 && static_cast<int>(out.size()) < scan_limit; ++probe) {
            const int c = 1 + ((offset + probe) % (rl.NodeCount() - 1));
            if (rl.RouteOf(c) >= 0) out.push_back(c);
        }
    }
    return out;
}

inline int FindPos(const std::vector<int>& route, int city) {
    for (size_t i = 1; i + 1 < route.size(); ++i) {
        if (route[i] == city) return static_cast<int>(i);
    }
    return -1;
}

inline std::vector<int> BuildPositionIndex(const RouteList& rl) {
    std::vector<int> pos(static_cast<size_t>(rl.NodeCount()), -1);
    for (int r = 0; r < rl.RouteCount(); ++r) {
        const auto& route = rl.Route(r);
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            const int c = route[i];
            if (c >= 0 && c < rl.NodeCount()) pos[static_cast<size_t>(c)] = static_cast<int>(i);
        }
    }
    return pos;
}

inline void AddUniquePosition(std::vector<int>& positions, int p, int route_size) {
    if (p >= 0 && p + 1 < route_size) positions.push_back(p);
}

inline int PickExpensiveSeed(const RouteList& rl, DistanceOracle& d, std::mt19937& rng,
                             int samples, const std::vector<int>* pos_index = nullptr) {
    if (rl.NodeCount() <= 1) return -1;
    int best_city = -1;
    double best_gain = -std::numeric_limits<double>::max();

    const int offset = static_cast<int>(rng() % static_cast<unsigned>(std::max(1, rl.NodeCount() - 1)));
    for (int probe = 0; probe < std::max(1, samples); ++probe) {
        const int c = 1 + ((offset + probe * 7919) % (rl.NodeCount() - 1));
        const int r = rl.RouteOf(c);
        if (r < 0) continue;
        const auto& route = rl.Route(r);
        const int pos = (pos_index && c >= 0 && c < static_cast<int>(pos_index->size()))
                      ? (*pos_index)[static_cast<size_t>(c)]
                      : FindPos(route, c);
        if (pos <= 0 || pos + 1 >= static_cast<int>(route.size())) continue;
        const int prev = route[static_cast<size_t>(pos - 1)];
        const int next = route[static_cast<size_t>(pos + 1)];
        const double removal_gain = d(prev, c) + d(c, next) - d(prev, next);
        if (removal_gain > best_gain) {
            best_gain = removal_gain;
            best_city = c;
        }
    }

    if (best_city >= 0) return best_city;
    for (int probe = 0; probe < rl.NodeCount() - 1; ++probe) {
        const int c = 1 + ((offset + probe) % (rl.NodeCount() - 1));
        if (rl.RouteOf(c) >= 0) return c;
    }
    return -1;
}

}  // namespace detail

template <typename AcceptPolicy>
inline bool TryGranularRelocateOnce(RouteList& rl,
                                    AcceptPolicy& accept,
                                    DistanceOracle& d,
                                    const CandidateSets& candidates,
                                    int scan_limit,
                                    int route_cap,
                                    std::mt19937& rng) {
    struct Move {
        int city = -1;
        int from = -1;
        int to = -1;
        int from_pos = -1;
        int after_pos = -1;
        double delta = 0.0;
    };

    Move best;
    double best_delta = 0.0;
    const auto sampled = detail::BuildSampledCustomers(rl, scan_limit, rng);
    const auto pos_of = detail::BuildPositionIndex(rl);
    for (int city : sampled) {
        const int from = rl.RouteOf(city);
        if (from < 0) continue;
        const auto& rf = rl.Route(from);
        const int pos = (city >= 0 && city < static_cast<int>(pos_of.size()))
                      ? pos_of[static_cast<size_t>(city)] : -1;
        if (pos <= 0 || pos + 1 >= static_cast<int>(rf.size())) continue;

        const int prev = rf[static_cast<size_t>(pos - 1)];
        const int next = rf[static_cast<size_t>(pos + 1)];
        const double dL_from = d(prev, next) - d(prev, city) - d(city, next);
        if (city < 0 || city >= static_cast<int>(candidates.size())) continue;

        for (int nb : candidates[static_cast<size_t>(city)]) {
            if (nb == 0 || nb == city) continue;
            const int to = rl.RouteOf(nb);
            if (to < 0 || to == from) continue;
            if (route_cap > 0 && rl.RouteSize(to) >= route_cap) continue;
            const auto& rt = rl.Route(to);
            const int nb_pos = (nb >= 0 && nb < static_cast<int>(pos_of.size()))
                             ? pos_of[static_cast<size_t>(nb)] : -1;
            if (nb_pos <= 0) continue;

            std::vector<int> positions;
            positions.reserve(4);
            detail::AddUniquePosition(positions, nb_pos - 1, static_cast<int>(rt.size()));
            detail::AddUniquePosition(positions, nb_pos, static_cast<int>(rt.size()));
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
            for (int after : positions) {
                const int a = rt[static_cast<size_t>(after)];
                const int b = rt[static_cast<size_t>(after + 1)];
                const double dL_to = d(a, city) + d(city, b) - d(a, b);
                const double delta = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to);
                if (delta < best_delta - kEps) {
                    best_delta = delta;
                    best = Move{city, from, to, pos, after, delta};
                }
            }
        }
    }

    if (best.city < 0 || !accept.StrictAccept(best.delta)) return false;
    rl.Remove(best.city, d);
    rl.InsertAt(best.to, best.after_pos, best.city, d);
    return true;
}

template <typename AcceptPolicy>
inline bool TryGranularSwapOnce(RouteList& rl,
                                AcceptPolicy& accept,
                                DistanceOracle& d,
                                const CandidateSets& candidates,
                                int scan_limit,
                                std::mt19937& rng) {
    struct Move {
        int a_city = -1;
        int b_city = -1;
        int ra = -1;
        int rb = -1;
        int pa = -1;
        int pb = -1;
        double delta = 0.0;
    };

    Move best;
    double best_delta = 0.0;
    const auto sampled = detail::BuildSampledCustomers(rl, scan_limit, rng);
    const auto pos_of = detail::BuildPositionIndex(rl);
    for (int a_city : sampled) {
        const int ra = rl.RouteOf(a_city);
        if (ra < 0) continue;
        const auto& Ra = rl.Route(ra);
        const int pa = (a_city >= 0 && a_city < static_cast<int>(pos_of.size()))
                     ? pos_of[static_cast<size_t>(a_city)] : -1;
        if (pa <= 0 || pa + 1 >= static_cast<int>(Ra.size())) continue;
        const int a_prev = Ra[static_cast<size_t>(pa - 1)];
        const int a_next = Ra[static_cast<size_t>(pa + 1)];
        if (a_city < 0 || a_city >= static_cast<int>(candidates.size())) continue;

        for (int b_city : candidates[static_cast<size_t>(a_city)]) {
            if (b_city == 0 || b_city == a_city) continue;
            const int rb = rl.RouteOf(b_city);
            if (rb < 0 || rb == ra) continue;
            const auto& Rb = rl.Route(rb);
            const int pb = (b_city >= 0 && b_city < static_cast<int>(pos_of.size()))
                         ? pos_of[static_cast<size_t>(b_city)] : -1;
            if (pb <= 0 || pb + 1 >= static_cast<int>(Rb.size())) continue;

            const int b_prev = Rb[static_cast<size_t>(pb - 1)];
            const int b_next = Rb[static_cast<size_t>(pb + 1)];
            const double dL_a = d(a_prev, b_city) + d(b_city, a_next)
                              - d(a_prev, a_city) - d(a_city, a_next);
            const double dL_b = d(b_prev, a_city) + d(a_city, b_next)
                              - d(b_prev, b_city) - d(b_city, b_next);
            const double delta = accept.DeltaForCrossRouteMove(rl, ra, rb, dL_a, dL_b);
            if (delta < best_delta - kEps) {
                best_delta = delta;
                best = Move{a_city, b_city, ra, rb, pa, pb, delta};
            }
        }
    }

    if (best.a_city < 0 || !accept.StrictAccept(best.delta)) return false;
    auto route_a = rl.Route(best.ra);
    auto route_b = rl.Route(best.rb);
    route_a[static_cast<size_t>(best.pa)] = best.b_city;
    route_b[static_cast<size_t>(best.pb)] = best.a_city;
    rl.ReplaceRoute(best.ra, std::move(route_a), d);
    rl.ReplaceRoute(best.rb, std::move(route_b), d);
    return true;
}

template <typename AcceptPolicy>
inline bool TryGranularTwoOptStarOnce(RouteList& rl,
                                      AcceptPolicy& accept,
                                      DistanceOracle& d,
                                      const CandidateSets& candidates,
                                      int scan_limit,
                                      std::mt19937& rng) {
    if (accept.IsMinMax()) return false;

    struct Move {
        int ra = -1;
        int rb = -1;
        int cut_a = -1;
        int cut_b = -1;
        double delta = 0.0;
    };

    Move best;
    double best_delta = 0.0;
    const auto sampled = detail::BuildSampledCustomers(rl, scan_limit, rng);
    const auto pos_of = detail::BuildPositionIndex(rl);

    for (int city : sampled) {
        const int ra = rl.RouteOf(city);
        if (ra < 0 || city <= 0 || city >= static_cast<int>(candidates.size())) continue;
        const auto& route_a = rl.Route(ra);
        const int pos_a = (city < static_cast<int>(pos_of.size()))
                        ? pos_of[static_cast<size_t>(city)] : -1;
        if (pos_a <= 0 || pos_a >= static_cast<int>(route_a.size())) continue;

        std::vector<int> cuts_a;
        cuts_a.reserve(2);
        detail::AddUniquePosition(cuts_a, pos_a - 1, static_cast<int>(route_a.size()));
        detail::AddUniquePosition(cuts_a, pos_a, static_cast<int>(route_a.size()));
        std::sort(cuts_a.begin(), cuts_a.end());
        cuts_a.erase(std::unique(cuts_a.begin(), cuts_a.end()), cuts_a.end());

        for (int nb : candidates[static_cast<size_t>(city)]) {
            if (nb == 0 || nb == city) continue;
            const int rb = rl.RouteOf(nb);
            if (rb < 0 || rb == ra) continue;
            const auto& route_b = rl.Route(rb);
            const int pos_b = (nb >= 0 && nb < static_cast<int>(pos_of.size()))
                            ? pos_of[static_cast<size_t>(nb)] : -1;
            if (pos_b <= 0 || pos_b >= static_cast<int>(route_b.size())) continue;

            std::vector<int> cuts;
            cuts.reserve(2);
            detail::AddUniquePosition(cuts, pos_b - 1, static_cast<int>(route_b.size()));
            detail::AddUniquePosition(cuts, pos_b, static_cast<int>(route_b.size()));
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

            for (int cut_a : cuts_a) {
                const int a = route_a[static_cast<size_t>(cut_a)];
                const int b = route_a[static_cast<size_t>(cut_a + 1)];
                for (int cut_b : cuts) {
                    const int c = route_b[static_cast<size_t>(cut_b)];
                    const int e = route_b[static_cast<size_t>(cut_b + 1)];
                    const double delta = d(a, e) + d(c, b) - d(a, b) - d(c, e);
                    if (delta < best_delta - kEps) {
                        best_delta = delta;
                        best = Move{ra, rb, cut_a, cut_b, delta};
                    }
                }
            }
        }
    }

    if (best.ra < 0 || !accept.StrictAccept(best.delta)) return false;

    const auto route_a = rl.Route(best.ra);
    const auto route_b = rl.Route(best.rb);
    std::vector<int> new_a;
    std::vector<int> new_b;
    new_a.reserve(route_a.size() + route_b.size());
    new_b.reserve(route_a.size() + route_b.size());

    new_a.insert(new_a.end(), route_a.begin(), route_a.begin() + best.cut_a + 1);
    new_a.insert(new_a.end(), route_b.begin() + best.cut_b + 1, route_b.end());
    new_b.insert(new_b.end(), route_b.begin(), route_b.begin() + best.cut_b + 1);
    new_b.insert(new_b.end(), route_a.begin() + best.cut_a + 1, route_a.end());

    if (new_a.empty() || new_a.front() != 0) new_a.insert(new_a.begin(), 0);
    if (new_a.size() < 2 || new_a.back() != 0) new_a.push_back(0);
    if (new_b.empty() || new_b.front() != 0) new_b.insert(new_b.begin(), 0);
    if (new_b.size() < 2 || new_b.back() != 0) new_b.push_back(0);

    rl.ReplaceRoute(best.ra, std::move(new_a), d);
    rl.ReplaceRoute(best.rb, std::move(new_b), d);
    return true;
}

template <typename AcceptPolicy>
inline int TryGranularTwoOptStarPass(RouteList& rl,
                                     AcceptPolicy& accept,
                                     DistanceOracle& d,
                                     const CandidateSets& candidates,
                                     SearchBudget& budget,
                                     std::mt19937& rng,
                                     int max_moves,
                                     int scan_limit,
                                     GranularMoveStats* stats = nullptr) {
    int accepted = 0;
    for (int step = 0; step < max_moves && !budget.ForceCheck(); ++step) {
        if (!TryGranularTwoOptStarOnce(rl, accept, d, candidates, scan_limit, rng)) break;
        ++accepted;
        if (stats) ++stats->two_optstar_accepts;
    }
    return accepted;
}

template <typename AcceptPolicy>
inline bool TryGranularOrOptOnce(RouteList& rl,
                                 AcceptPolicy& accept,
                                 DistanceOracle& d,
                                 const CandidateSets& candidates,
                                 int scan_limit,
                                 int route_cap,
                                 int max_seg_len,
                                 std::mt19937& rng) {
    struct Move {
        int from = -1;
        int to = -1;
        int pos = -1;
        int len = 0;
        int after_pos = -1;
        bool reverse = false;
        double delta = 0.0;
    };

    Move best;
    double best_delta = 0.0;
    const int max_len = std::clamp(max_seg_len, 2, 4);
    const auto sampled = detail::BuildSampledCustomers(rl, scan_limit, rng);
    const auto pos_of = detail::BuildPositionIndex(rl);

    for (int city : sampled) {
        const int from = rl.RouteOf(city);
        if (from < 0 || city <= 0 || city >= static_cast<int>(candidates.size())) continue;
        const auto& rf = rl.Route(from);
        const int pos = (city < static_cast<int>(pos_of.size()))
                      ? pos_of[static_cast<size_t>(city)] : -1;
        if (pos <= 0 || pos + 1 >= static_cast<int>(rf.size())) continue;

        for (int len = 2; len <= max_len; ++len) {
            if (pos + len >= static_cast<int>(rf.size())) break;
            const int first = rf[static_cast<size_t>(pos)];
            const int last = rf[static_cast<size_t>(pos + len - 1)];
            const int prev = rf[static_cast<size_t>(pos - 1)];
            const int next = rf[static_cast<size_t>(pos + len)];
            const double dL_from = d(prev, next) - d(prev, first) - d(last, next);

            std::vector<int> near;
            near.reserve(candidates[static_cast<size_t>(first)].size() +
                         candidates[static_cast<size_t>(last)].size());
            for (int nb : candidates[static_cast<size_t>(first)]) near.push_back(nb);
            if (last != first && last >= 0 && last < static_cast<int>(candidates.size())) {
                for (int nb : candidates[static_cast<size_t>(last)]) near.push_back(nb);
            }
            std::sort(near.begin(), near.end());
            near.erase(std::unique(near.begin(), near.end()), near.end());

            for (int nb : near) {
                if (nb == 0 || nb == first || nb == last) continue;
                const int to = rl.RouteOf(nb);
                if (to < 0 || to == from) continue;
                if (route_cap > 0 && rl.RouteSize(to) + len > route_cap) continue;
                const auto& rt = rl.Route(to);
                const int nb_pos = (nb >= 0 && nb < static_cast<int>(pos_of.size()))
                                 ? pos_of[static_cast<size_t>(nb)] : -1;
                if (nb_pos <= 0) continue;

                std::vector<int> positions;
                positions.reserve(2);
                detail::AddUniquePosition(positions, nb_pos - 1, static_cast<int>(rt.size()));
                detail::AddUniquePosition(positions, nb_pos, static_cast<int>(rt.size()));
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

                for (int after : positions) {
                    const int a = rt[static_cast<size_t>(after)];
                    const int b = rt[static_cast<size_t>(after + 1)];
                    const double dL_to_fwd = d(a, first) + d(last, b) - d(a, b);
                    const double delta_fwd = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to_fwd);
                    if (delta_fwd < best_delta - kEps) {
                        best_delta = delta_fwd;
                        best = Move{from, to, pos, len, after, false, delta_fwd};
                    }
                    if (len >= 2) {
                        const double dL_to_rev = d(a, last) + d(first, b) - d(a, b);
                        const double delta_rev = accept.DeltaForCrossRouteMove(rl, from, to, dL_from, dL_to_rev);
                        if (delta_rev < best_delta - kEps) {
                            best_delta = delta_rev;
                            best = Move{from, to, pos, len, after, true, delta_rev};
                        }
                    }
                }
            }
        }
    }

    if (best.from < 0 || !accept.StrictAccept(best.delta)) return false;

    auto route_from = rl.Route(best.from);
    auto route_to = rl.Route(best.to);
    std::vector<int> block(route_from.begin() + best.pos,
                           route_from.begin() + best.pos + best.len);
    if (best.reverse) std::reverse(block.begin(), block.end());
    route_from.erase(route_from.begin() + best.pos,
                     route_from.begin() + best.pos + best.len);
    route_to.insert(route_to.begin() + best.after_pos + 1, block.begin(), block.end());
    rl.ReplaceRoute(best.from, std::move(route_from), d);
    rl.ReplaceRoute(best.to, std::move(route_to), d);
    return true;
}

template <typename AcceptPolicy>
inline int TryGranularOrOptPass(RouteList& rl,
                                AcceptPolicy& accept,
                                DistanceOracle& d,
                                const CandidateSets& candidates,
                                SearchBudget& budget,
                                std::mt19937& rng,
                                int max_moves,
                                int scan_limit,
                                int route_cap,
                                int max_seg_len,
                                GranularMoveStats* stats = nullptr) {
    int accepted = 0;
    for (int step = 0; step < max_moves && !budget.ForceCheck(); ++step) {
        if (!TryGranularOrOptOnce(rl, accept, d, candidates, scan_limit,
                                  route_cap, max_seg_len, rng)) break;
        ++accepted;
        if (stats) ++stats->oropt_accepts;
    }
    return accepted;
}

template <typename AcceptPolicy>
inline int TryGranularInterRoutePass(RouteList& rl,
                                     AcceptPolicy& accept,
                                     DistanceOracle& d,
                                     const CandidateSets& candidates,
                                     SearchBudget& budget,
                                     std::mt19937& rng,
                                     int max_moves,
                                     int scan_limit,
                                     int route_cap,
                                     GranularMoveStats* stats = nullptr) {
    int accepted = 0;
    for (int step = 0; step < max_moves && !budget.ForceCheck(); ++step) {
        if (TryGranularRelocateOnce(rl, accept, d, candidates, scan_limit, route_cap, rng)) {
            ++accepted;
            if (stats) ++stats->relocate_accepts;
            continue;
        }
        if (TryGranularSwapOnce(rl, accept, d, candidates, scan_limit, rng)) {
            ++accepted;
            if (stats) ++stats->swap_accepts;
            continue;
        }
        break;
    }
    return accepted;
}

inline void RepairCheapestInsertionIndexed(RouteList& rl,
                                           std::vector<int>& removed,
                                           std::mt19937& rng,
                                           RepairContext& ctx,
                                           int rebuild_period = 128) {
    std::shuffle(removed.begin(), removed.end(), rng);
    std::vector<int> pos_of = detail::BuildPositionIndex(rl);
    int since_rebuild = 0;

    for (int city : removed) {
        if (rl.RouteOf(city) >= 0) continue;
        if (since_rebuild >= rebuild_period) {
            pos_of = detail::BuildPositionIndex(rl);
            since_rebuild = 0;
        }

        double best_delta = std::numeric_limits<double>::max();
        int best_r = -1;
        int best_after = -1;
        if (city < 0 || city >= static_cast<int>(ctx.candidates.size())) continue;

        for (int r = 0; r < rl.RouteCount(); ++r) {
            const auto& route = rl.Route(r);
            if (route.size() < 2) continue;
            if (ctx.route_cap > 0 && rl.RouteSize(r) >= ctx.route_cap) continue;

            std::vector<int> positions;
            positions.reserve(ctx.candidates[static_cast<size_t>(city)].size() * 2 + 2);
            positions.push_back(0);
            positions.push_back(static_cast<int>(route.size()) - 2);
            for (int nb : ctx.candidates[static_cast<size_t>(city)]) {
                if (nb == 0) continue;
                if (rl.RouteOf(nb) != r) continue;
                const int pos = (nb >= 0 && nb < static_cast<int>(pos_of.size()))
                              ? pos_of[static_cast<size_t>(nb)] : -1;
                if (pos <= 0 || pos + 1 >= static_cast<int>(route.size())) continue;
                if (route[static_cast<size_t>(pos)] != nb) continue;
                detail::AddUniquePosition(positions, pos - 1, static_cast<int>(route.size()));
                detail::AddUniquePosition(positions, pos, static_cast<int>(route.size()));
            }
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
            for (int p : positions) {
                const int a = route[static_cast<size_t>(p)];
                const int b = route[static_cast<size_t>(p + 1)];
                const double dlt = ctx.d(a, city) + ctx.d(city, b) - ctx.d(a, b);
                double penalty = 0.0;
                if (ctx.balance_aware) {
                    const double new_len = rl.RouteLength(r) + dlt;
                    const double current_max = rl.MaxLength();
                    if (new_len > current_max) penalty = (new_len - current_max) * 0.5;
                }
                const double cost = dlt + penalty;
                if (cost < best_delta) {
                    best_delta = cost;
                    best_r = r;
                    best_after = p;
                }
            }
        }

        if (best_r < 0) {
            int sr = -1;
            if (ctx.route_cap > 0) {
                int min_size = std::numeric_limits<int>::max();
                for (int r = 0; r < rl.RouteCount(); ++r) {
                    const int sz = rl.RouteSize(r);
                    if (sz < ctx.route_cap && sz < min_size) {
                        min_size = sz;
                        sr = r;
                    }
                }
            }
            if (sr < 0) {
                sr = 0;
                double sl = std::numeric_limits<double>::max();
                for (int r = 0; r < rl.RouteCount(); ++r) {
                    if (rl.RouteLength(r) < sl) {
                        sl = rl.RouteLength(r);
                        sr = r;
                    }
                }
            }
            const auto& route = rl.Route(sr);
            best_r = sr;
            best_after = static_cast<int>(route.size()) - 2;
        }

        rl.InsertAt(best_r, best_after, city, ctx.d);
        if (city >= 0 && city < static_cast<int>(pos_of.size())) {
            pos_of[static_cast<size_t>(city)] = best_after + 1;
        }
        ++since_rebuild;
    }
}

template <typename AcceptPolicy>
inline bool TryDualOptRegionReopt(RouteList& rl,
                                  AcceptPolicy& accept,
                                  const KDTree2D& kdtree,
                                  DistanceOracle& d,
                                  const CandidateSets& candidates,
                                  SearchBudget& budget,
                                  std::mt19937& rng,
                                  int K_region,
                                  int route_cap,
                                  GranularMoveStats* stats = nullptr) {
    if (stats) ++stats->region_calls;
    if (K_region <= 0 || rl.NodeCount() <= 1 || rl.RouteCount() < 2) return false;

    const double base_cost = accept.ScalarCost(rl);
    RouteSet base_snap;
    rl.StoreTo(base_snap);
    const auto base_pos = detail::BuildPositionIndex(rl);

    const int attempts = std::clamp(K_region / 1200 + 1, 2, 3);
    for (int attempt = 0; attempt < attempts && !budget.ForceCheck(); ++attempt) {
        if (attempt > 0) rl.LoadFrom(base_snap, d);
        const int K_try = std::min(std::max(50, rl.NodeCount() - 1),
            attempt == 0 ? K_region :
            attempt == 1 ? K_region + K_region / 3 :
            attempt == 2 ? std::max(200, K_region / 2) :
                           std::min(std::max(50, rl.NodeCount() - 1), K_region * 2));

        const int seed_samples = std::min(2048, std::max(128, rl.NodeCount() / 80));
        const int seed = detail::PickExpensiveSeed(rl, d, rng, seed_samples, &base_pos);
        if (seed < 0) continue;

        std::vector<int> nearest;
        nearest.reserve(static_cast<size_t>(K_try) + 1);
        kdtree.Knn(seed, K_try, nearest);
        nearest.push_back(seed);

        std::vector<char> selected(static_cast<size_t>(rl.NodeCount()), 0);
        std::vector<char> dirty(static_cast<size_t>(rl.RouteCount()), 0);
        std::vector<int> removed;
        removed.reserve(nearest.size() + static_cast<size_t>(K_try / 4 + 8));
        for (int c : nearest) {
            if (c <= 0 || c >= rl.NodeCount()) continue;
            if (selected[static_cast<size_t>(c)]) continue;
            const int r = rl.RouteOf(c);
            if (r < 0) continue;
            selected[static_cast<size_t>(c)] = 1;
            dirty[static_cast<size_t>(r)] = 1;
            removed.push_back(c);
        }
        if (removed.empty()) continue;

        // Aggressive DualOpt-style boundary loosening: include immediate
        // route neighbours around selected customers so the spatial window can
        // change how it connects back to route skeletons, not only permute the
        // exact K nearest customers.
        const int extra_limit = std::max(16, K_try / 8);
        int extra_added = 0;
        for (int r = 0; r < rl.RouteCount() && extra_added < extra_limit; ++r) {
            if (!dirty[static_cast<size_t>(r)]) continue;
            const auto& route = rl.Route(r);
            for (size_t i = 1; i + 1 < route.size() && extra_added < extra_limit; ++i) {
                const int c = route[i];
                if (!selected[static_cast<size_t>(c)]) continue;
                const int left = route[i - 1];
                const int right = route[i + 1];
                for (int nb : {left, right}) {
                    if (nb <= 0 || nb >= rl.NodeCount()) continue;
                    if (selected[static_cast<size_t>(nb)]) continue;
                    selected[static_cast<size_t>(nb)] = 1;
                    removed.push_back(nb);
                    ++extra_added;
                    if (extra_added >= extra_limit) break;
                }
            }
        }

        for (int r = 0; r < rl.RouteCount(); ++r) {
            if (!dirty[static_cast<size_t>(r)]) continue;
            const auto& route = rl.Route(r);
            std::vector<int> kept;
            kept.reserve(route.size());
            for (int c : route) {
                if (c != 0 && selected[static_cast<size_t>(c)]) continue;
                kept.push_back(c);
            }
            if (kept.empty() || kept.front() != 0) kept.insert(kept.begin(), 0);
            if (kept.size() < 2 || kept.back() != 0) kept.push_back(0);
            rl.ReplaceRoute(r, std::move(kept), d);
        }

        RepairContext rctx{d, candidates, accept.IsMinMax()};
        rctx.route_cap = route_cap;
        if (static_cast<int>(removed.size()) <= 300) {
            RepairRegret2Insertion(rl, removed, rng, rctx);
        } else {
            RepairCheapestInsertionIndexed(rl, removed, rng, rctx);
        }

        if (!budget.ForceCheck()) {
            const int polish_ms = std::max(100, budget.RemainingMs() / 25);
            SearchBudget polish_budget = budget.SubBudget(polish_ms);
            RouteIndex idx(rl.NodeCount());
            for (int r = 0; r < rl.RouteCount(); ++r) {
                if (!dirty[static_cast<size_t>(r)] || polish_budget.ForceCheck()) continue;
                auto route_copy = rl.Route(r);
                NeighborList2Opt(route_copy, candidates, d, polish_budget, idx);
                if (route_copy.size() < 4500 && !polish_budget.ForceCheck()) {
                    Exhaustive2Opt(route_copy, d, polish_budget);
                }
                rl.ReplaceRoute(r, std::move(route_copy), d);
            }
        }

        const double post_cost = accept.ScalarCost(rl);
        if (post_cost + kEps < base_cost) {
            if (stats) ++stats->region_accepts;
            return true;
        }
    }
    rl.LoadFrom(base_snap, d);
    return false;
}

}  // namespace mtsp::v21
