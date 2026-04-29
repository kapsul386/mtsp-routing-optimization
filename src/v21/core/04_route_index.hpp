#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

namespace mtsp::v21 {

// Stamped lookup of (city -> position-in-route). Calling Build() bumps a
// version stamp instead of clearing the array, so repeated builds stay O(L).
struct RouteIndex {
    explicit RouteIndex(int node_count)
        : position(static_cast<size_t>(node_count), -1),
          seen(static_cast<size_t>(node_count), 0) {}

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
