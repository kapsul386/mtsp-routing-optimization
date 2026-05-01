#pragma once

// Destroy operators for the ALNS pipeline. Each operator removes K customers
// from the current solution and returns them in DestroyResult.removed; the
// repair stage then re-inserts them. The mix is: Random (uniform sampling),
// ClusterBfs (spatial cluster around a seed), Expensive (worst-edge-driven),
// Zone (KDTree radius around a seed), and CriticalRoute (min-max biased,
// removes from the longest route). Different operators give the search
// access to different escape moves; ALNS adaptive weights pick which to use.

#include "00_types.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include "05_route_list.hpp"
#include "12_alns_framework.hpp"
#include <algorithm>
#include <queue>
#include <random>
#include <vector>

namespace mtsp::v21 {

// Per-replica context shared by every destroy operator (captured by reference
// in the type-erased std::function passed to AlnsFramework).
struct DestroyContext {
    DistanceOracle& d;
    const KDTree2D& kdtree;
    int n_total;  // node count
};

// 1. Random destroy: uniformly pick K customers across all routes.
inline DestroyResult DestroyRandom(RouteList& rl, std::mt19937& rng, int K, DestroyContext& ctx) {
    DestroyResult res;
    res.dirty_routes.assign(static_cast<size_t>(rl.RouteCount()), 0);
    if (K <= 0) return res;
    const int n = rl.NodeCount();
    std::vector<int> all;
    all.reserve(static_cast<size_t>(n - 1));
    for (int c = 1; c < n; ++c) if (rl.RouteOf(c) >= 0) all.push_back(c);
    std::shuffle(all.begin(), all.end(), rng);
    const int take = std::min<int>(K, static_cast<int>(all.size()));
    for (int i = 0; i < take; ++i) {
        const int c = all[static_cast<size_t>(i)];
        const int r = rl.RouteOf(c);
        if (r >= 0) res.dirty_routes[static_cast<size_t>(r)] = 1;
        rl.Remove(c, ctx.d);
        res.removed.push_back(c);
    }
    return res;
}

// 2. Cluster-BFS destroy: pick a seed customer, then destroy K cities closest
//    to it (via KDTree). Equivalent to the Shaw/related-removal operator.
inline DestroyResult DestroyClusterBfs(RouteList& rl, std::mt19937& rng, int K, DestroyContext& ctx) {
    DestroyResult res;
    res.dirty_routes.assign(static_cast<size_t>(rl.RouteCount()), 0);
    if (K <= 0) return res;
    // Pick a placed customer as seed
    const int n = rl.NodeCount();
    std::vector<int> placed;
    placed.reserve(static_cast<size_t>(n - 1));
    for (int c = 1; c < n; ++c) if (rl.RouteOf(c) >= 0) placed.push_back(c);
    if (placed.empty()) return res;
    const int seed = placed[rng() % placed.size()];
    std::vector<int> nearest;
    ctx.kdtree.Knn(seed, std::max(K * 3, K + 10), nearest);
    int taken = 0;
    {
        const int r = rl.RouteOf(seed);
        if (r >= 0) res.dirty_routes[static_cast<size_t>(r)] = 1;
        rl.Remove(seed, ctx.d);
        res.removed.push_back(seed);
        ++taken;
    }
    for (int c : nearest) {
        if (taken >= K) break;
        if (c == 0) continue;
        const int r = rl.RouteOf(c);
        if (r < 0) continue;
        res.dirty_routes[static_cast<size_t>(r)] = 1;
        rl.Remove(c, ctx.d);
        res.removed.push_back(c);
        ++taken;
    }
    return res;
}

// 3. Expensive-edges destroy: sort customers by their "removal gain" (sum of
//    incident edge lengths minus the bypass), pick K with largest gain.
inline DestroyResult DestroyExpensiveEdges(RouteList& rl, std::mt19937& rng, int K, DestroyContext& ctx) {
    DestroyResult res;
    res.dirty_routes.assign(static_cast<size_t>(rl.RouteCount()), 0);
    if (K <= 0) return res;
    struct Cand { double gain; int city; int route; };
    std::vector<Cand> cands;
    cands.reserve(1024);
    for (int r = 0; r < rl.RouteCount(); ++r) {
        const auto& route = rl.Route(r);
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            const int prev = route[i - 1], cur = route[i], next = route[i + 1];
            const double gain = ctx.d(prev, cur) + ctx.d(cur, next) - ctx.d(prev, next);
            cands.push_back({gain, cur, r});
        }
    }
    if (cands.empty()) return res;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.gain > b.gain; });
    // Take top K with a bit of randomization (top 2K, sample K).
    const int pool = std::min<int>(static_cast<int>(cands.size()), std::max(K * 2, K + 4));
    std::vector<int> idx(static_cast<size_t>(pool));
    for (int i = 0; i < pool; ++i) idx[static_cast<size_t>(i)] = i;
    std::shuffle(idx.begin(), idx.end(), rng);
    const int take = std::min<int>(K, pool);
    std::vector<char> picked(cands.size(), 0);
    for (int i = 0; i < take; ++i) picked[static_cast<size_t>(idx[static_cast<size_t>(i)])] = 1;
    for (size_t i = 0; i < cands.size(); ++i) {
        if (!picked[i]) continue;
        const int c = cands[i].city;
        const int r = rl.RouteOf(c);
        if (r < 0) continue;
        res.dirty_routes[static_cast<size_t>(r)] = 1;
        rl.Remove(c, ctx.d);
        res.removed.push_back(c);
    }
    return res;
}

// 4. Zone destroy: pick a random customer, then take all customers within
//    radius proportional to sqrt(K * avg_density).
inline DestroyResult DestroyZone(RouteList& rl, std::mt19937& rng, int K, DestroyContext& ctx) {
    DestroyResult res;
    res.dirty_routes.assign(static_cast<size_t>(rl.RouteCount()), 0);
    if (K <= 0) return res;
    const int n = rl.NodeCount();
    std::vector<int> placed;
    placed.reserve(static_cast<size_t>(n - 1));
    for (int c = 1; c < n; ++c) if (rl.RouteOf(c) >= 0) placed.push_back(c);
    if (placed.empty()) return res;
    const int seed = placed[rng() % placed.size()];
    // Use kNN to estimate radius: take K-th neighbour distance
    std::vector<int> nearest;
    ctx.kdtree.Knn(seed, K + 4, nearest);
    if (static_cast<int>(nearest.size()) <= 1) return res;
    const int target = nearest.back();
    const double radius = std::sqrt(ctx.d.SquaredDistance(seed, target));
    std::vector<int> in_radius;
    ctx.kdtree.RangeRadius(seed, radius * 1.05, in_radius);
    in_radius.push_back(seed);
    std::shuffle(in_radius.begin(), in_radius.end(), rng);
    int taken = 0;
    for (int c : in_radius) {
        if (taken >= K) break;
        if (c == 0) continue;
        const int r = rl.RouteOf(c);
        if (r < 0) continue;
        res.dirty_routes[static_cast<size_t>(r)] = 1;
        rl.Remove(c, ctx.d);
        res.removed.push_back(c);
        ++taken;
    }
    return res;
}

// 5. CriticalRoute destroy: wipe the currently-longest route entirely. Only
//    enabled for min-max solver. K is ignored; we remove the whole route.
inline DestroyResult DestroyCriticalRoute(RouteList& rl, std::mt19937& /*rng*/, int /*K*/, DestroyContext& ctx) {
    DestroyResult res;
    res.dirty_routes.assign(static_cast<size_t>(rl.RouteCount()), 0);
    const int r = rl.LongestRoute();
    res.dirty_routes[static_cast<size_t>(r)] = 1;
    res.removed = rl.WipeRouteCustomers(r, ctx.d);
    return res;
}

}  // namespace mtsp::v21
