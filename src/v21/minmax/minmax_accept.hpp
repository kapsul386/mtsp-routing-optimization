#pragma once

// AcceptPolicy plug-in for MINMAX. Pure max-of-routes is a discrete
// landscape (changing the longest route by epsilon may not change "max"),
// so we use a soft α-blend with λ·sum to give SA a usable gradient: early
// iterations explore via the sum component, late iterations focus on max.
// α is reduced over the run (controlled externally from the pipeline).

#include "../core/02_distance.hpp"
#include "../core/05_route_list.hpp"
#include "../core/11_validation.hpp"
#include <mtsp_solver.h>
#include <algorithm>

namespace mtsp::v21 {

// AcceptPolicy implementation for MINMAX mTSP. The scalar cost is a soft
// α-blend: (1-α)·max + α·λ·sum + λ·sum/2. The trailing term provides a
// tie-breaker so that moves with equal max prefer lower total distance.
// effective_cost = (1 - alpha) * max + alpha * lambda * sum + (lambda * sum / 2)
// The trailing tie-breaker term keeps Δmax=0 moves discriminable by Δsum.
class MinmaxAccept {
public:
    // Construct the accept policy with a fixed lambda tie-breaker coefficient.
    explicit MinmaxAccept(double lambda) : lambda_(lambda) {}

    // Returns true — identifies this object as a MINMAX accept policy.
    bool IsMinMax() const { return true; }
    // The lambda coefficient used in the λ·sum tie-breaker term.
    double Lambda() const { return lambda_; }
    // Set the soft-blend factor α ∈ [0, 1] that mixes max and sum objectives.
    void SetSoftAlpha(double a) { soft_alpha_ = std::clamp(a, 0.0, 1.0); }

    double ScalarCost(const RouteList& rl) const {
        const double mx = rl.MaxLength();
        const double sm = rl.TotalLength();
        return (1.0 - soft_alpha_) * mx + soft_alpha_ * lambda_ * sm + lambda_ * sm * 0.5;
        // The trailing 0.5*lambda*sm gives a tie-breaker on equal max so that
        // among Δmax=0 moves we still prefer the lower-sum candidate.
    }

    double ScalarCostOfRoutes(const RouteSet& routes, DistanceOracle& d) const {
        const LexCost c = ComputeLexCost(routes, d, lambda_);
        return (1.0 - soft_alpha_) * c.max + soft_alpha_ * lambda_ * c.sum + lambda_ * c.sum * 0.5;
    }

    // Cross-route move: known route-length deltas dL_from and dL_to. We need
    // to estimate Δmax: that requires knowing current route lengths. For
    // efficiency, we approximate using the RouteList's cached lengths.
    double DeltaForCrossRouteMove(const RouteList& rl, int from, int to,
                                   double dL_from, double dL_to) const {
        const double m_old = rl.MaxLength();
        const double Lf_new = rl.RouteLength(from) + dL_from;
        const double Lt_new = rl.RouteLength(to) + dL_to;
        // Compute new max: scan all routes (m operations).
        double m_new = 0.0;
        for (int r = 0; r < rl.RouteCount(); ++r) {
            double L = rl.RouteLength(r);
            if (r == from) L = Lf_new;
            else if (r == to) L = Lt_new;
            if (L > m_new) m_new = L;
        }
        const double dM = m_new - m_old;
        const double dS = dL_from + dL_to;
        return (1.0 - soft_alpha_) * dM + soft_alpha_ * lambda_ * dS + lambda_ * dS * 0.5;
    }

    // Strict acceptance: returns true iff `delta` is strictly negative by kEps.
    bool StrictAccept(double delta) const { return delta < -kEps; }

private:
    double lambda_ = 1e-3;
    double soft_alpha_ = 0.05;
};

}  // namespace mtsp::v21
