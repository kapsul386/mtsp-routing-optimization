#pragma once

// Repair operators for the ALNS pipeline. Take a list of removed customers
// produced by a destroy operator and reinsert them into the partial solution.
// Two strategies: cheapest-insertion (greedy by per-customer best position)
// and regret-2 (orders insertions by largest opportunity cost; harder
// customers go first). Both operators are candidate-restricted via the
// `candidates` vector to keep per-step cost manageable on large n.
// route_cap (FILO2-inspired) is an optional hard upper bound on per-route
// customer count; only set by `lkh_v21_minsum_cap`.

#include "00_types.hpp"
#include "02_distance.hpp"
#include "05_route_list.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace mtsp::v21 {

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

// Cheapest-insertion repair: for each removed customer (in random order),
// scan candidate-restricted positions across all routes and pick the cheapest.
// When ctx.route_cap > 0, routes already at capacity are skipped entirely.
inline void RepairCheapestInsertion(RouteList& rl, std::vector<int>& removed,
                                     std::mt19937& rng, RepairContext& ctx) {
    std::shuffle(removed.begin(), removed.end(), rng);
    for (int city : removed) {
        if (rl.RouteOf(city) >= 0) continue;
        double best_delta = std::numeric_limits<double>::max();
        int best_r = -1;
        int best_after = -1;
        for (int r = 0; r < rl.RouteCount(); ++r) {
            const auto& route = rl.Route(r);
            if (route.size() < 2) continue;
            // FILO2-style hard cap: skip routes that already have capacity
            // worth of customers. cap == 0 disables the check (legacy mode).
            if (ctx.route_cap > 0 && rl.RouteSize(r) >= ctx.route_cap) continue;
            // Try candidate-driven positions + endpoints
            std::vector<int> positions;
            positions.push_back(0);
            positions.push_back(static_cast<int>(route.size()) - 2);
            for (int nb : ctx.candidates[static_cast<size_t>(city)]) {
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
            for (int p : positions) {
                if (p < 0 || p + 1 >= static_cast<int>(route.size())) continue;
                const int a = route[static_cast<size_t>(p)];
                const int b = route[static_cast<size_t>(p + 1)];
                const double dlt = ctx.d(a, city) + ctx.d(city, b) - ctx.d(a, b);
                double penalty = 0.0;
                if (ctx.balance_aware) {
                    // Penalty for adding to longer route
                    const double new_len = rl.RouteLength(r) + dlt;
                    const double current_max = rl.MaxLength();
                    if (new_len > current_max) penalty = (new_len - current_max) * 0.5;
                }
                const double cost = dlt + penalty;
                if (cost < best_delta) { best_delta = cost; best_r = r; best_after = p; }
            }
        }
        if (best_r < 0) {
            // Fallback: in cap mode, prefer the route with the fewest customers
            // (under cap) so we keep distribution tight; in legacy mode, fall
            // back to shortest-length route as before.
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
                for (int r = 0; r < rl.RouteCount(); ++r) if (rl.RouteLength(r) < sl) { sl = rl.RouteLength(r); sr = r; }
            }
            const auto& route = rl.Route(sr);
            const int after = static_cast<int>(route.size()) - 2;
            rl.InsertAt(sr, after, city, ctx.d);
        } else {
            rl.InsertAt(best_r, best_after, city, ctx.d);
        }
    }
}

// Regret-2 insertion: for each removed customer, compute (best - second_best)
// insertion cost. Insert the customer with largest regret first at its best
// position. Repeat. Same FILO2-style cap logic as RepairCheapestInsertion.
inline void RepairRegret2Insertion(RouteList& rl, std::vector<int>& removed,
                                    std::mt19937& /*rng*/, RepairContext& ctx) {
    while (!removed.empty()) {
        int chosen_idx = -1;
        int chosen_route = -1;
        int chosen_after = -1;
        double chosen_delta = 0.0;
        double largest_regret = -std::numeric_limits<double>::max();
        for (size_t k = 0; k < removed.size(); ++k) {
            const int city = removed[k];
            if (rl.RouteOf(city) >= 0) continue;
            double best1 = std::numeric_limits<double>::max();
            double best2 = std::numeric_limits<double>::max();
            int b_r = -1, b_a = -1;
            for (int r = 0; r < rl.RouteCount(); ++r) {
                const auto& route = rl.Route(r);
                if (route.size() < 2) continue;
                if (ctx.route_cap > 0 && rl.RouteSize(r) >= ctx.route_cap) continue;
                std::vector<int> positions;
                positions.push_back(0);
                positions.push_back(static_cast<int>(route.size()) - 2);
                for (int nb : ctx.candidates[static_cast<size_t>(city)]) {
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
                for (int p : positions) {
                    if (p < 0 || p + 1 >= static_cast<int>(route.size())) continue;
                    const int a = route[static_cast<size_t>(p)];
                    const int b = route[static_cast<size_t>(p + 1)];
                    const double dlt = ctx.d(a, city) + ctx.d(city, b) - ctx.d(a, b);
                    double penalty = 0.0;
                    if (ctx.balance_aware) {
                        const double new_len = rl.RouteLength(r) + dlt;
                        const double cm = rl.MaxLength();
                        if (new_len > cm) penalty = (new_len - cm) * 0.5;
                    }
                    const double cost = dlt + penalty;
                    if (cost < best1) { best2 = best1; best1 = cost; b_r = r; b_a = p; }
                    else if (cost < best2) { best2 = cost; }
                }
            }
            if (b_r < 0) continue;
            const double regret = (best2 == std::numeric_limits<double>::max() ? 1e9 : best2) - best1;
            if (regret > largest_regret) {
                largest_regret = regret;
                chosen_idx = static_cast<int>(k);
                chosen_route = b_r;
                chosen_after = b_a;
                chosen_delta = best1;
            }
        }
        if (chosen_idx < 0) {
            // No insertion found — fall back. In cap mode prefer smallest-size
            // under-cap route to keep balance tight; in legacy mode use the
            // shortest-length route as before.
            for (int city : removed) {
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
                    for (int r = 0; r < rl.RouteCount(); ++r) if (rl.RouteLength(r) < sl) { sl = rl.RouteLength(r); sr = r; }
                }
                const auto& route = rl.Route(sr);
                rl.InsertAt(sr, static_cast<int>(route.size()) - 2, city, ctx.d);
            }
            removed.clear();
            return;
        }
        const int city = removed[static_cast<size_t>(chosen_idx)];
        rl.InsertAt(chosen_route, chosen_after, city, ctx.d);
        (void)chosen_delta;
        removed.erase(removed.begin() + chosen_idx);
    }
}

}  // namespace mtsp::v21
