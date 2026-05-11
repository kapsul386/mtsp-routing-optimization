#pragma once

// Repair operators for the ALNS pipeline. Take a list of removed customers
// produced by a destroy operator and reinsert them into the partial solution.
// Two strategies: cheapest-insertion (greedy by per-customer best position)
// and regret-2 (orders insertions by largest opportunity cost; harder
// customers go first). Both operators are candidate-restricted via the
// `candidates` vector to keep per-step cost manageable on large n.
// route_cap (FILO2-inspired) is an optional hard upper bound on per-route
// customer count; only set by `lkh_v21_minsum_cap`.
//
// Both operators maintain a global pos_of[city] index. The previous version
// scanned each route linearly to locate every kNN neighbour — O(L) per
// candidate, which dominated wall-time on large routes (n=25k/m=5 has
// route_size ~5000). The indexed path is functionally identical but does
// O(1) lookups; positions are bumped incrementally after each insert so the
// cache stays exact without periodic full rebuilds.

#include "00_types.hpp"
#include "02_distance.hpp"
#include "05_route_list.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace mtsp::v21 {

// Parameters shared by all repair operators. Captured by reference in the
// type-erased repair lambdas registered with AlnsFramework.
struct RepairContext {
    DistanceOracle& d;
    const CandidateSets& candidates;
    bool balance_aware = false;  // for min-max: penalise inserting onto longest routes
    // FILO2-inspired capacity-aware repair (high-m MINSUM stabilization).
    // 0 = disabled (legacy MINSUM/MINMAX behaviour). >0 = max number of
    // customers (excluding depot endpoints) that any route may hold; repair
    // skips at-cap routes entirely. Set by `lkh_v21_minsum_cap` solver from
    // ceil((n-1)/m) * (1 + slack_frac).
    int route_cap = 0;
};

namespace repair_detail {

// Build a global city -> position cache for the current RouteList state.
// Position is the index inside its route vector (depot endpoints get -1 here
// because we never want to insert against a depot anchor). Cities not placed
// stay at -1.
inline std::vector<int> BuildPosOfIndex(const RouteList& rl) {
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

// After inserting `city` into `route_after_insert` at gap `after_pos` (so the
// new city occupies index after_pos+1), every city at index >= after_pos+2
// shifted right by one. Walk just that tail and bump pos_of accordingly. This
// keeps the global index exact at O(L_tail) per insert instead of O(L) full
// rebuilds.
inline void BumpPositionsAfterInsert(std::vector<int>& pos_of,
                                     const std::vector<int>& route_after_insert,
                                     int after_pos, int inserted_city) {
    if (inserted_city >= 0 && inserted_city < static_cast<int>(pos_of.size())) {
        pos_of[static_cast<size_t>(inserted_city)] = after_pos + 1;
    }
    const int sz = static_cast<int>(route_after_insert.size());
    for (int i = after_pos + 2; i + 1 < sz; ++i) {
        const int c = route_after_insert[static_cast<size_t>(i)];
        if (c >= 0 && c < static_cast<int>(pos_of.size())) {
            pos_of[static_cast<size_t>(c)] = i;
        }
    }
}

// Find the best insertion position (after_pos) for `city` across all routes
// using a candidate-list-restricted scan. `pos_of` is read-only here and is
// expected to be exact for all currently-placed cities. Returns -1 in best_r
// if no candidate-driven slot was found; callers fall back accordingly.
// Best and second-best insertion slots for a single customer across all routes.
// Used by both cheapest-insertion and regret-2 to share the scan logic.
struct InsertionPick {
    int route = -1;
    int after_pos = -1;
    double delta = std::numeric_limits<double>::max();
    double second = std::numeric_limits<double>::max();  // regret-2
};

inline InsertionPick FindBestInsertion(const RouteList& rl, int city,
                                       const std::vector<int>& pos_of,
                                       const RepairContext& ctx) {
    InsertionPick out;
    if (city < 0 || city >= static_cast<int>(ctx.candidates.size())) return out;
    const auto& cand_list = ctx.candidates[static_cast<size_t>(city)];
    // Per-route position scratch — small fixed budget per route is enough
    // because endpoint slots and at most one window around each kNN neighbour
    // collapse to O(k_NN) entries.
    std::vector<int> positions;
    positions.reserve(cand_list.size() * 2 + 4);
    for (int r = 0; r < rl.RouteCount(); ++r) {
        const auto& route = rl.Route(r);
        const int sz = static_cast<int>(route.size());
        if (sz < 2) continue;
        if (ctx.route_cap > 0 && rl.RouteSize(r) >= ctx.route_cap) continue;

        positions.clear();
        positions.push_back(0);
        positions.push_back(sz - 2);
        for (int nb : cand_list) {
            if (nb == 0) continue;
            if (rl.RouteOf(nb) != r) continue;
            const int p = (nb >= 0 && nb < static_cast<int>(pos_of.size()))
                        ? pos_of[static_cast<size_t>(nb)] : -1;
            if (p <= 0 || p + 1 >= sz) continue;
            // pos_of is kept exact, so route[p] == nb is invariant.
            if (p - 1 >= 0) positions.push_back(p - 1);
            if (p < sz - 1) positions.push_back(p);
        }
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        for (int p : positions) {
            if (p < 0 || p + 1 >= sz) continue;
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
            if (cost < out.delta) {
                out.second = out.delta;
                out.delta = cost;
                out.route = r;
                out.after_pos = p;
            } else if (cost < out.second) {
                out.second = cost;
            }
        }
    }
    return out;
}

inline void FallbackInsert(RouteList& rl, int city, std::vector<int>& pos_of,
                           const RepairContext& ctx) {
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
        for (int r = 0; r < rl.RouteCount(); ++r)
            if (rl.RouteLength(r) < sl) { sl = rl.RouteLength(r); sr = r; }
    }
    const auto& route = rl.Route(sr);
    const int after = static_cast<int>(route.size()) - 2;
    rl.InsertAt(sr, after, city, ctx.d);
    BumpPositionsAfterInsert(pos_of, rl.Route(sr), after, city);
}

}  // namespace repair_detail

// Cheapest-insertion repair: for each removed customer (in random order),
// scan candidate-restricted positions across all routes and pick the cheapest.
// When ctx.route_cap > 0, routes already at capacity are skipped entirely.
inline void RepairCheapestInsertion(RouteList& rl, std::vector<int>& removed,
                                     std::mt19937& rng, RepairContext& ctx) {
    std::shuffle(removed.begin(), removed.end(), rng);
    auto pos_of = repair_detail::BuildPosOfIndex(rl);
    for (int city : removed) {
        if (rl.RouteOf(city) >= 0) continue;
        const auto pick = repair_detail::FindBestInsertion(rl, city, pos_of, ctx);
        if (pick.route < 0) {
            repair_detail::FallbackInsert(rl, city, pos_of, ctx);
            continue;
        }
        rl.InsertAt(pick.route, pick.after_pos, city, ctx.d);
        repair_detail::BumpPositionsAfterInsert(pos_of, rl.Route(pick.route),
                                                pick.after_pos, city);
    }
}

// Regret-2 insertion: for each removed customer, compute (best - second_best)
// insertion cost. Insert the customer with largest regret first at its best
// position. Repeat. Same FILO2-style cap logic as RepairCheapestInsertion.
inline void RepairRegret2Insertion(RouteList& rl, std::vector<int>& removed,
                                    std::mt19937& /*rng*/, RepairContext& ctx) {
    auto pos_of = repair_detail::BuildPosOfIndex(rl);
    while (!removed.empty()) {
        int chosen_idx = -1;
        repair_detail::InsertionPick chosen;
        double largest_regret = -std::numeric_limits<double>::max();
        for (size_t k = 0; k < removed.size(); ++k) {
            const int city = removed[k];
            if (rl.RouteOf(city) >= 0) continue;
            const auto pick = repair_detail::FindBestInsertion(rl, city, pos_of, ctx);
            if (pick.route < 0) continue;
            const double second = (pick.second == std::numeric_limits<double>::max()
                                   ? 1e9 : pick.second);
            const double regret = second - pick.delta;
            if (regret > largest_regret) {
                largest_regret = regret;
                chosen_idx = static_cast<int>(k);
                chosen = pick;
            }
        }
        if (chosen_idx < 0) {
            // No insertion found via candidate-list — fall back per remaining
            // city. Each fallback updates pos_of so subsequent ones still work.
            for (int city : removed) {
                if (rl.RouteOf(city) >= 0) continue;
                repair_detail::FallbackInsert(rl, city, pos_of, ctx);
            }
            removed.clear();
            return;
        }
        const int city = removed[static_cast<size_t>(chosen_idx)];
        rl.InsertAt(chosen.route, chosen.after_pos, city, ctx.d);
        repair_detail::BumpPositionsAfterInsert(pos_of, rl.Route(chosen.route),
                                                chosen.after_pos, city);
        removed.erase(removed.begin() + chosen_idx);
    }
}

}  // namespace mtsp::v21
