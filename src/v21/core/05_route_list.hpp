#pragma once

#include "00_types.hpp"
#include "02_distance.hpp"
#include <mtsp_solver.h>
#include <vector>
#include <cassert>
#include <algorithm>

namespace mtsp::v21 {

// RouteList: thin wrapper around mtsp::RouteSet that maintains:
//   - route_of_[city]   -> O(1) lookup of which route a city is on
//   - route_length_[r]  -> cached length, incrementally updated on moves
//   - dirty_routes_[r]  -> bitmap for selective LS
//
// We deliberately keep the underlying storage as vector<vector<int>> (with
// depot at both ends per route) — this matches all existing v8/v9 ports and
// avoids re-implementing FILO's phantom-depot doubly-linked list.
//
// Move complexity: O(L) for relocate/swap (L = source-route length). At 100k
// cities / 100 agents this is ~1k ops per move — fine for tens of thousands
// of ALNS iterations.
class RouteList {
public:
    RouteList(int n, int m) : n_(n), m_(m), route_of_(static_cast<size_t>(n), -1),
                               route_length_(static_cast<size_t>(m), 0.0),
                               dirty_(static_cast<size_t>(m), 0) {}

    void LoadFrom(const RouteSet& routes, DistanceOracle& d) {
        routes_ = routes;
        if (static_cast<int>(routes_.size()) != m_) {
            // Pad/truncate to m_; caller is expected to provide m_ routes.
            routes_.resize(static_cast<size_t>(m_), std::vector<int>{0, 0});
        }
        std::fill(route_of_.begin(), route_of_.end(), -1);
        route_of_[0] = 0;  // depot lives on route 0 conceptually
        for (int r = 0; r < m_; ++r) {
            const auto& route = routes_[static_cast<size_t>(r)];
            for (size_t i = 1; i + 1 < route.size(); ++i) {
                route_of_[static_cast<size_t>(route[i])] = r;
            }
            route_length_[static_cast<size_t>(r)] = ComputeLength(route, d);
            dirty_[static_cast<size_t>(r)] = 0;
        }
    }

    void StoreTo(RouteSet& out) const { out = routes_; }

    int RouteOf(int city) const { return route_of_[static_cast<size_t>(city)]; }

    const std::vector<int>& Route(int r) const { return routes_[static_cast<size_t>(r)]; }
    std::vector<int>& MutableRoute(int r) { dirty_[static_cast<size_t>(r)] = 1; return routes_[static_cast<size_t>(r)]; }

    int RouteCount() const { return m_; }
    int NodeCount() const { return n_; }

    // Number of customers in route r (excluding the two depot endpoints).
    // O(1). Used by capacity-aware repair/relocate variants for high-m MINSUM
    // (FILO2-inspired stabilization).
    int RouteSize(int r) const {
        const auto& route = routes_[static_cast<size_t>(r)];
        return route.size() >= 2 ? static_cast<int>(route.size()) - 2 : 0;
    }

    double RouteLength(int r) const { return route_length_[static_cast<size_t>(r)]; }
    double TotalLength() const {
        double s = 0.0;
        for (int r = 0; r < m_; ++r) s += route_length_[static_cast<size_t>(r)];
        return s;
    }
    double MaxLength() const {
        double m = 0.0;
        for (int r = 0; r < m_; ++r) m = std::max(m, route_length_[static_cast<size_t>(r)]);
        return m;
    }
    int LongestRoute() const {
        int best = 0; double bm = -1.0;
        for (int r = 0; r < m_; ++r) {
            if (route_length_[static_cast<size_t>(r)] > bm) { bm = route_length_[static_cast<size_t>(r)]; best = r; }
        }
        return best;
    }

    void RecomputeAllLengths(DistanceOracle& d) {
        for (int r = 0; r < m_; ++r)
            route_length_[static_cast<size_t>(r)] = ComputeLength(routes_[static_cast<size_t>(r)], d);
    }
    void RecomputeLength(int r, DistanceOracle& d) {
        route_length_[static_cast<size_t>(r)] = ComputeLength(routes_[static_cast<size_t>(r)], d);
    }

    // Remove city from its current route. Returns the route id it was in.
    int Remove(int city, DistanceOracle& d) {
        const int r = route_of_[static_cast<size_t>(city)];
        if (r < 0) return -1;
        auto& route = routes_[static_cast<size_t>(r)];
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            if (route[i] == city) {
                const int prev = route[i - 1];
                const int next = route[i + 1];
                route_length_[static_cast<size_t>(r)] += d(prev, next) - d(prev, city) - d(city, next);
                route.erase(route.begin() + static_cast<std::ptrdiff_t>(i));
                route_of_[static_cast<size_t>(city)] = -1;
                dirty_[static_cast<size_t>(r)] = 1;
                return r;
            }
        }
        return -1;
    }

    // Insert `city` into route r at position `after_pos` (so city becomes route[after_pos+1]).
    // after_pos is 0..route.size()-2 (i.e. after some existing element, before depot tail).
    void InsertAt(int r, int after_pos, int city, DistanceOracle& d) {
        auto& route = routes_[static_cast<size_t>(r)];
        const int a = route[static_cast<size_t>(after_pos)];
        const int b = route[static_cast<size_t>(after_pos + 1)];
        route_length_[static_cast<size_t>(r)] += d(a, city) + d(city, b) - d(a, b);
        route.insert(route.begin() + static_cast<std::ptrdiff_t>(after_pos + 1), city);
        route_of_[static_cast<size_t>(city)] = r;
        dirty_[static_cast<size_t>(r)] = 1;
    }

    // Set entire route r (overwrite). Updates route_of_ and length cache.
    void ReplaceRoute(int r, std::vector<int> new_route, DistanceOracle& d) {
        // Clear old route_of mappings
        const auto& old_route = routes_[static_cast<size_t>(r)];
        for (size_t i = 1; i + 1 < old_route.size(); ++i) {
            if (route_of_[static_cast<size_t>(old_route[i])] == r)
                route_of_[static_cast<size_t>(old_route[i])] = -1;
        }
        routes_[static_cast<size_t>(r)] = std::move(new_route);
        const auto& new_r = routes_[static_cast<size_t>(r)];
        for (size_t i = 1; i + 1 < new_r.size(); ++i) {
            route_of_[static_cast<size_t>(new_r[i])] = r;
        }
        route_length_[static_cast<size_t>(r)] = ComputeLength(new_r, d);
        dirty_[static_cast<size_t>(r)] = 1;
    }

    // Wipe a route except for depot endpoints — used by DestroyCriticalRoute.
    // Returns the customers removed.
    std::vector<int> WipeRouteCustomers(int r, DistanceOracle& d) {
        std::vector<int> removed;
        auto& route = routes_[static_cast<size_t>(r)];
        if (route.size() <= 2) return removed;
        removed.reserve(route.size() - 2);
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            removed.push_back(route[i]);
            route_of_[static_cast<size_t>(route[i])] = -1;
        }
        route = {0, 0};
        route_length_[static_cast<size_t>(r)] = ComputeLength(route, d);
        dirty_[static_cast<size_t>(r)] = 1;
        return removed;
    }

    void ClearDirty() { std::fill(dirty_.begin(), dirty_.end(), 0); }
    void MarkDirty(int r) { dirty_[static_cast<size_t>(r)] = 1; }
    bool IsDirty(int r) const { return dirty_[static_cast<size_t>(r)] != 0; }
    const std::vector<char>& DirtyMask() const { return dirty_; }

    // O(1) swap of state with another RouteList — used by Parallel Tempering
    // to exchange (current solution + cached lengths) between replicas. Both
    // RouteLists must have the same n_ and m_.
    void Swap(RouteList& other) {
        std::swap(routes_, other.routes_);
        std::swap(route_of_, other.route_of_);
        std::swap(route_length_, other.route_length_);
        std::swap(dirty_, other.dirty_);
    }

private:
    static double ComputeLength(const std::vector<int>& route, DistanceOracle& d) {
        double s = 0.0;
        for (size_t i = 1; i < route.size(); ++i) s += d(route[i - 1], route[i]);
        return s;
    }

    int n_ = 0;
    int m_ = 0;
    RouteSet routes_;
    std::vector<int> route_of_;       // city -> route id, -1 if not placed (depot=0)
    std::vector<double> route_length_;
    std::vector<char> dirty_;
};

}  // namespace mtsp::v21
