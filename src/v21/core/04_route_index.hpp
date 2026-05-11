#pragma once

// O(L) (city -> position) lookup for a single route. Build() bumps a version
// stamp instead of zeroing the position array, so repeated rebuilds during
// inner loops stay O(L) instead of O(n). Used by 2-opt and inter-route move
// generators that need to translate "nearest neighbor of city c is city b"
// into a position in the current route.

#include <cstdint>
#include <vector>
#include <algorithm>

namespace mtsp::v21 {

// Allocate position and seen arrays for a problem of `node_count` nodes.
// All positions start as -1 (not in index) and the version stamp starts at 1.
struct RouteIndex {
    explicit RouteIndex(int node_count)
        : position(static_cast<size_t>(node_count), -1),
          seen(static_cast<size_t>(node_count), 0) {}

    // Repopulate the index for `route`. O(|route|).
    void Build(const std::vector<int>& route) {
        ++stamp;
        if (stamp == 0) {
            std::fill(seen.begin(), seen.end(), 0u);
            stamp = 1;
        }
        for (size_t idx = 0; idx < route.size(); ++idx) {
            const int node = route[idx];
            position[static_cast<size_t>(node)] = static_cast<int>(idx);
            seen[static_cast<size_t>(node)] = stamp;
        }
    }

    // Position of `node` in the most recently built route, or -1 if absent.
    int Get(int node) const {
        return seen[static_cast<size_t>(node)] == stamp
                   ? position[static_cast<size_t>(node)]
                   : -1;
    }

    std::vector<int> position;
    std::vector<uint32_t> seen;
    uint32_t stamp = 1;
};

}  // namespace mtsp::v21
