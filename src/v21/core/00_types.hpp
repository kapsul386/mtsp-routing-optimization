#pragma once

// Shared typedefs and small utilities for the v21 core. Header-only.
// All v21 code lives in namespace mtsp::v21 to keep symbols isolated from
// the legacy v8/v9 modules.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mtsp::v21 {

using Coord = std::pair<double, double>;
// candidates[v] holds the k nearest neighbors of vertex v (built once per instance).
using CandidateSets = std::vector<std::vector<int>>;

inline constexpr double kEps = 1e-9;
inline constexpr double kCoordEps = 1e-12;
// Threshold above which DistanceOracle switches to a cached on-demand mode
// instead of the full O(n^2) precomputed matrix (memory-driven).
inline constexpr long long kLargeInstanceDistancePairs = 4'000'000LL;

// Order-independent uint64 key for an edge {a, b}. Used by edge-level caches.
inline uint64_t PackEdgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32U) |
           static_cast<uint32_t>(b);
}

}  // namespace mtsp::v21
