#pragma once

// Guided Local Search edge-penalty overlay (Voudouris & Tsang 1996, KGLS in
// VRP literature). Augments the cost function with a per-edge penalty term
// that grows when an edge is "stuck" in many local minima — pushing search
// toward edges it has not been forced to use. Penalties decay on best-cost
// improvement so the augmented landscape converges back to the real cost.
// Used by RunAlnsSaLoop in 17_pipeline as a stagnation-escape mechanism.

#include "00_types.hpp"
#include "02_distance.hpp"
#include "05_route_list.hpp"
#include <mtsp_solver.h>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mtsp::v21 {

// Guided Local Search overlay (Voudouris & Tsang 1996).
//
// Maintains penalties p_e for edges. The augmented cost is:
//
//   c_aug(e) = c(e) + lambda * p_e
//
// LS / acceptance use augmented cost; best-snapshot uses real cost. When the
// real-best stagnates we increase penalties on a few "bad" edges (top-K by
// utility = real_cost / (1 + p_e) among edges currently in the solution),
// shifting the augmented landscape so LS escapes.
//
// We deliberately keep `p_e` sparse (hash map) — at any time only a small
// fraction of edges are penalized. Lambda is set once based on the average
// edge cost so penalty contribution stays in proportion.
class EdgePenalties {
public:
    // Clear all accumulated penalties, resetting to the unaugmented cost function.
    void Reset() { p_.clear(); }

    // Return the integer penalty for edge {a,b}, or 0 if it has never been penalized.
    int Get(int a, int b) const {
        const auto it = p_.find(PackEdgeKey(a, b));
        return it == p_.end() ? 0 : it->second;
    }

    // Increment the penalty counter for edge {a,b} by `delta` (default 1).
    void Add(int a, int b, int delta = 1) {
        p_[PackEdgeKey(a, b)] += delta;
    }

    // Set the scaling coefficient lambda that converts integer penalty counts to cost units.
    void SetLambda(double l) { lambda_ = std::max(0.0, l); }
    // Return the current lambda coefficient.
    double Lambda() const { return lambda_; }

    // Sum of (lambda * p_e) over all edges in `routes` (route set).
    double TotalPenalty(const RouteSet& routes) const {
        if (lambda_ <= 0.0 || p_.empty()) return 0.0;
        double total = 0.0;
        for (const auto& r : routes) {
            for (size_t i = 1; i < r.size(); ++i) {
                total += static_cast<double>(Get(r[i - 1], r[i]));
            }
        }
        return lambda_ * total;
    }

    // Sum of (lambda * p_e) over all edges in `rl` (RouteList view).
    double TotalPenalty(const RouteList& rl) const {
        if (lambda_ <= 0.0 || p_.empty()) return 0.0;
        double total = 0.0;
        for (int r = 0; r < rl.RouteCount(); ++r) {
            const auto& route = rl.Route(r);
            for (size_t i = 1; i < route.size(); ++i) {
                total += static_cast<double>(Get(route[i - 1], route[i]));
            }
        }
        return lambda_ * total;
    }

    // Increment penalty for K edges with highest utility = c(e) / (1 + p_e)
    // among edges currently in `rl`. K = 1..3 typical for GLS bursts.
    void PenalizeWorstEdges(const RouteList& rl, DistanceOracle& d, int K) {
        if (K <= 0) return;
        struct EdgeInfo { double util; int a; int b; };
        std::vector<EdgeInfo> edges;
        edges.reserve(static_cast<size_t>(rl.NodeCount()));
        for (int r = 0; r < rl.RouteCount(); ++r) {
            const auto& route = rl.Route(r);
            for (size_t i = 1; i < route.size(); ++i) {
                const int a = route[i - 1];
                const int b = route[i];
                const double c = d(a, b);
                const int p = Get(a, b);
                const double util = c / (1.0 + p);
                edges.push_back({util, a, b});
            }
        }
        if (edges.empty()) return;
        const int take = std::min(K, static_cast<int>(edges.size()));
        std::nth_element(edges.begin(), edges.begin() + take, edges.end(),
                         [](const EdgeInfo& l, const EdgeInfo& r) { return l.util > r.util; });
        for (int i = 0; i < take; ++i) Add(edges[static_cast<size_t>(i)].a, edges[static_cast<size_t>(i)].b);
    }

    // Optional: shrink penalties (multiplicative decay) when we find a new best.
    // Avoids unbounded penalty accumulation across many stagnation episodes.
    void Decay(double factor) {
        if (factor >= 1.0 || p_.empty()) return;
        for (auto it = p_.begin(); it != p_.end();) {
            it->second = static_cast<int>(static_cast<double>(it->second) * factor);
            if (it->second <= 0) it = p_.erase(it);
            else ++it;
        }
    }

    // Number of distinct edges currently carrying a positive penalty.
    size_t PenalizedEdgeCount() const {
        size_t n = 0;
        for (const auto& [_, v] : p_) if (v > 0) ++n;
        return n;
    }

private:
    std::unordered_map<uint64_t, int> p_;
    double lambda_ = 0.0;
};

// Recommended initial lambda: alpha * (sum / num_edges) where alpha ~ 0.1.
// `sum` is the total tour length, `num_edges` ≈ n + m (routes have n+m edges).
inline double SuggestGlsLambda(double total_sum, int n, int m, double alpha = 0.10) {
    const int num_edges = std::max(1, n + m);
    return alpha * total_sum / num_edges;
}

}  // namespace mtsp::v21
