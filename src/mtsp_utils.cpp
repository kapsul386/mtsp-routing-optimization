#include <algorithm>
#include <limits>
#include <vector>

#include <mtsp_instance.h>
#include <mtsp_utils.h>

namespace mtsp {

// Returns the total route length as the sum of distances over consecutive vertex pairs.
// The route must start and end at the depot (vertex 0).
double RouteLength(const std::vector<int>& route) {
    const Instance& inst = Instance::GetInstance();
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += inst.Distance(route[i - 1], route[i]);
    }
    return total;
}

// MINSUM objective function — total length across all routes.
// Used by all solvers as the primary quality metric.
double ObjectiveMinsum(const RouteSet& routes) {
    double total = 0.0;
    for (const auto& route : routes) {
        total += RouteLength(route);
    }
    return total;
}

// Greedy nearest-neighbor over a list of assigned clients.
// Returns a closed route [0, c_1, c_2, ..., c_k, 0] where at each step
// the nearest unvisited client from the given list is selected.
std::vector<int> BuildNearestOrder(const std::vector<int>& assigned) {
    const Instance& inst = Instance::GetInstance();
    std::vector<int> route;
    route.reserve(assigned.size() + 2);
    route.push_back(0);

    std::vector<int> pool = assigned;
    int current = 0;
    // Linear scan for the nearest at each step — O(k^2) for k clients.
    // Sufficient for small routes; larger ones use a KD-tree.
    while (!pool.empty()) {
        auto best_it = pool.begin();
        double best_dist = std::numeric_limits<double>::max();
        for (auto it = pool.begin(); it != pool.end(); ++it) {
            const double candidate = inst.Distance(current, *it);
            if (candidate < best_dist) {
                best_dist = candidate;
                best_it = it;
            }
        }
        current = *best_it;
        route.push_back(current);
        pool.erase(best_it);
    }

    route.push_back(0);
    return route;
}

// 2-opt optimization of a route to convergence.
// Each pass looks for the first improving segment reversal; if one is found,
// std::reverse is applied and the loop continues. Complexity: O(L^2) per pass,
// number of passes to convergence is typically O(1)..O(L).
void ImproveRoute2Opt(std::vector<int>& route) {
    if (route.size() <= 4) {
        return;
    }

    const Instance& inst = Instance::GetInstance();
    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t i = 1; i + 2 < route.size(); ++i) {
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                // Compare the length of two edges before and after a possible reversal [i..j].
                const double before =
                    inst.Distance(route[i - 1], route[i]) + inst.Distance(route[j], route[j + 1]);
                const double after =
                    inst.Distance(route[i - 1], route[j]) + inst.Distance(route[i], route[j + 1]);
                if (after + 1e-9 < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                }
            }
        }
    }
}

// Validate a route set: each client is visited exactly once,
// each route starts and ends at the depot, no duplicates.
// Used as a post-condition contract after each solver in the main harness.
bool ValidateRoutes(const RouteSet& routes) {
    const Instance& inst = Instance::GetInstance();
    std::vector<int> seen(inst.GetNodeCount(), 0);
    seen[0] = 1;  // depot is considered visited by construction.

    // Check each route: starts/ends at depot, no duplicates within.
    for (const auto& route : routes) {
        if (route.size() < 2 || route.front() != 0 || route.back() != 0) {
            return false;
        }
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            const int node = route[i];
            if (node <= 0 || node >= inst.GetNodeCount() || seen[node] == 1) {
                return false;
            }
            seen[node] = 1;
        }
    }

    // Final check: all clients must have been visited.
    for (int node = 1; node < inst.GetNodeCount(); ++node) {
        if (seen[node] == 0) {
            return false;
        }
    }
    return true;
}

} // namespace mtsp
