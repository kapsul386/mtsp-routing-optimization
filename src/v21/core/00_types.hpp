#pragma once

// v21 core: shared typedefs and small utilities. Header-only.
//
// All v21 code lives inside namespace mtsp::v21 to keep symbols isolated from
// the legacy v8/v9 modules. The minsum and minmax solver TUs include this
// file (and the rest of core/) directly via #include "../core/...".

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mtsp::v21 {

using Coord = std::pair<double, double>;
using CandidateSets = std::vector<std::vector<int>>;

inline constexpr double kEps = 1e-9;
inline constexpr double kCoordEps = 1e-12;
inline constexpr long long kLargeInstanceDistancePairs = 4'000'000LL;

inline uint64_t PackEdgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32U) |
           static_cast<uint32_t>(b);
}

}  // namespace mtsp::v21
