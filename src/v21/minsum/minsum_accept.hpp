#pragma once

#include "../core/02_distance.hpp"
#include "../core/05_route_list.hpp"
#include "../core/11_validation.hpp"
#include "../core/15_sa_engine.hpp"
#include <mtsp_solver.h>

namespace mtsp::v21 {

// MINSUM acceptance policy: scalar = sum of route lengths.
class MinsumAccept {
public:
    bool IsMinMax() const { return false; }
    double Lambda() const { return 0.0; }

    double ScalarCost(const RouteList& rl) const { return rl.TotalLength(); }

    double ScalarCostOfRoutes(const RouteSet& routes, DistanceOracle& d) const {
        return RouteSumLength(routes, d);
    }

    // For MINSUM, the delta of a cross-route move is just dL_from + dL_to.
    double DeltaForCrossRouteMove(const RouteList& /*rl*/, int /*from*/, int /*to*/,
                                   double dL_from, double dL_to) const {
        return dL_from + dL_to;
    }

    // Strict acceptance: any negative delta.
    bool StrictAccept(double delta) const { return delta < -kEps; }
};

}  // namespace mtsp::v21
