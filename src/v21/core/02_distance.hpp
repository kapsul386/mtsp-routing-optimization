#pragma once

// Lazy distance oracle. For small instances (n*n <= kLargeInstanceDistancePairs)
// we delegate every call to the underlying Instance which already holds a full
// O(n^2) matrix. For large instances we keep an on-demand unordered_map cache
// keyed by PackEdgeKey, paying memory only for actually-queried pairs.
// Depot distances are precomputed once because seed strategies query them
// densely.
//
// Not thread-safe: the cache map is mutated by operator(). Each replica/thread
// must own its own DistanceOracle (see PtReplicaCtx) or use Raw() / a separate
// stateless functor.

#include "00_types.hpp"
#include <mtsp_instance.h>
#include <cmath>
#include <unordered_map>
#include <cstdint>

namespace mtsp::v21 {

class DistanceOracle {
public:
    explicit DistanceOracle(const mtsp::Instance& inst)
        : inst_(inst),
          coords_(inst.GetCoords()),
          n_(inst.GetNodeCount()),
          cache_enabled_(static_cast<long long>(inst.GetNodeCount()) * static_cast<long long>(inst.GetNodeCount()) >
                         kLargeInstanceDistancePairs),
          depot_dist_(coords_.size(), 0.0) {
        if (cache_enabled_) {
            cache_.reserve(std::max<size_t>(4096, coords_.size() * 24ULL));
        }
        for (size_t node = 0; node < coords_.size(); ++node) {
            depot_dist_[node] = std::sqrt(SquaredDistance(static_cast<int>(node), 0));
        }
    }

    // Euclidean distance between two nodes; cached on large instances. Mutating.
    double operator()(int a, int b) {
        if (a == b) return 0.0;
        if (!cache_enabled_) return inst_.Distance(a, b);
        const uint64_t key = PackEdgeKey(a, b);
        const auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        const double v = std::sqrt(SquaredDistance(a, b));
        cache_.emplace(key, v);
        return v;
    }

    // Cache-free distance: bit-identical to Instance::Distance, safe across threads.
    double Raw(int a, int b) const { return inst_.Distance(a, b); }

    double SquaredDistance(int a, int b) const {
        const double dx = coords_[static_cast<size_t>(a)].first - coords_[static_cast<size_t>(b)].first;
        const double dy = coords_[static_cast<size_t>(a)].second - coords_[static_cast<size_t>(b)].second;
        return dx * dx + dy * dy;
    }

    double DepotDistance(int node) const { return depot_dist_[static_cast<size_t>(node)]; }
    const std::vector<Coord>& GetCoords() const { return coords_; }
    int NodeCount() const { return n_; }
    const mtsp::Instance& GetInstance() const { return inst_; }

private:
    const mtsp::Instance& inst_;
    const std::vector<Coord>& coords_;
    int n_;
    bool cache_enabled_;
    std::unordered_map<uint64_t, double> cache_;
    std::vector<double> depot_dist_;
};

// Sum of edge lengths along a route (caller-supplied distance functor).
template <typename DistanceFn>
double RouteLengthGeneric(const std::vector<int>& route, DistanceFn& d) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) total += d(route[i - 1], route[i]);
    return total;
}

}  // namespace mtsp::v21
