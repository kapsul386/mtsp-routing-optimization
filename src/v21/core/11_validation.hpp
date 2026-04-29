#pragma once

#include "00_types.hpp"
#include "02_distance.hpp"
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <vector>

namespace mtsp::v21 {

// Lightweight sanitizer: ensures every route has depot endpoints, every
// non-depot city in 1..n-1 appears exactly once across routes, and the route
// count equals m. If duplicates/missing cities are detected we reassign them
// to the shortest route (last resort — should not happen with correct ALNS).
inline void SanitizeRoutes(RouteSet& routes, const mtsp::Instance& inst) {
    const int n = inst.GetNodeCount();
    const int m = std::max(1, inst.GetSalesmanCount());
    if (static_cast<int>(routes.size()) < m) routes.resize(static_cast<size_t>(m), std::vector<int>{0, 0});
    if (static_cast<int>(routes.size()) > m) routes.resize(static_cast<size_t>(m));
    for (auto& r : routes) {
        if (r.empty()) { r = {0, 0}; continue; }
        if (r.front() != 0) r.insert(r.begin(), 0);
        if (r.size() < 2 || r.back() != 0) r.push_back(0);
    }
    std::vector<int> seen(static_cast<size_t>(n), 0);
    seen[0] = 1;
    for (auto& r : routes) {
        std::vector<int> cleaned;
        cleaned.reserve(r.size());
        cleaned.push_back(0);
        for (size_t i = 1; i + 1 < r.size(); ++i) {
            const int v = r[i];
            if (v <= 0 || v >= n) continue;
            if (seen[static_cast<size_t>(v)] == 1) continue;
            seen[static_cast<size_t>(v)] = 1;
            cleaned.push_back(v);
        }
        cleaned.push_back(0);
        r = std::move(cleaned);
    }
    // Append any missing cities to the shortest route.
    auto append_to_shortest = [&](int v) {
        size_t best = 0; size_t blen = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < routes.size(); ++i) if (routes[i].size() < blen) { blen = routes[i].size(); best = i; }
        routes[best].insert(routes[best].end() - 1, v);
    };
    for (int v = 1; v < n; ++v) if (!seen[static_cast<size_t>(v)]) {
        append_to_shortest(v);
        seen[static_cast<size_t>(v)] = 1;
    }
}

inline bool ValidateRoutes(const RouteSet& routes, int n) {
    std::vector<int> seen(static_cast<size_t>(n), 0);
    seen[0] = 1;
    for (const auto& r : routes) {
        if (r.size() < 2 || r.front() != 0 || r.back() != 0) return false;
        for (size_t i = 1; i + 1 < r.size(); ++i) {
            const int v = r[i];
            if (v <= 0 || v >= n) return false;
            if (seen[static_cast<size_t>(v)] == 1) return false;
            seen[static_cast<size_t>(v)] = 1;
        }
    }
    for (int v = 1; v < n; ++v) if (!seen[static_cast<size_t>(v)]) return false;
    return true;
}

inline double RouteSumLength(const RouteSet& routes, DistanceOracle& d) {
    double total = 0.0;
    for (const auto& r : routes) for (size_t i = 1; i < r.size(); ++i) total += d(r[i - 1], r[i]);
    return total;
}

inline double MaxRouteLength(const RouteSet& routes, DistanceOracle& d) {
    double best = 0.0;
    for (const auto& r : routes) {
        double s = 0.0;
        for (size_t i = 1; i < r.size(); ++i) s += d(r[i - 1], r[i]);
        if (s > best) best = s;
    }
    return best;
}

// Lexicographic cost (max, λ·sum).
struct LexCost {
    double max = 0.0;
    double sum = 0.0;
    double lambda = 1e-3;

    double Scalarized() const { return max + lambda * sum; }
    bool StrictlyBetterThan(const LexCost& other, double tol = kEps) const {
        if (max + tol < other.max) return true;
        if (other.max + tol < max) return false;
        return sum + tol < other.sum;
    }
};

inline LexCost ComputeLexCost(const RouteSet& routes, DistanceOracle& d, double lambda) {
    LexCost c; c.lambda = lambda;
    for (const auto& r : routes) {
        double s = 0.0;
        for (size_t i = 1; i < r.size(); ++i) s += d(r[i - 1], r[i]);
        c.sum += s;
        if (s > c.max) c.max = s;
    }
    return c;
}

}  // namespace mtsp::v21
