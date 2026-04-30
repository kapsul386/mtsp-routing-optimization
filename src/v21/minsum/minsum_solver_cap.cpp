// v21 MINSUM solver with FILO2-inspired capacity-aware repair.
//
// Identical to lkh_v21_minsum except for one knob: a hard cap on the number
// of customers any single route may hold. The cap is computed from the
// instance as
//
//     target_size = ceil((n - 1) / m)            // ideal balanced load
//     cap         = ceil(target_size * (1 + slack))   // slack default 0.25
//
// During ALNS, repair operators (cheapest-insertion, regret-2) skip routes
// already at cap, forcing inserted customers into under-loaded routes. This
// is FILO2's CVRP capacity check ported to MINSUM mTSP. It adds no algorithm,
// just a guard rail. The motivation: pure MINSUM mathematically may prefer
// imbalanced solutions where one route absorbs many customers, but on
// instances with high m (few customers per agent) such imbalance hurts both
// the practical workload distribution AND the actual MINSUM, because a
// hugely overfilled route carries non-trivial intra-route detour cost.
//
// FILO2 reference: data\baseline\filo2\solution\savings.hpp (Clarke-Wright
// merge gated by capacity), data\baseline\filo2\localsearch\OneZeroExchange.hpp
// (relocate gated by capacity).
//
// CLI options accepted via Configure (all optional):
//   seed                — RNG seed (default 1)
//   time-budget-ms      — wall-clock budget (default 60000)
//   threads             — override AutoTune num_threads
//   route-cap           — explicit absolute cap; overrides slack-derived cap
//   route-cap-slack     — fractional slack on top of target_size, e.g. 0.25
//                          for cap = target_size * 1.25 (default 0.25)
//
// Registered as "lkh_v21_minsum_cap" in mtsp::SolverFactory.

#include "../core/00_types.hpp"
#include "../core/01_budget.hpp"
#include "../core/02_distance.hpp"
#include "../core/03_kdtree.hpp"
#include "../core/04_route_index.hpp"
#include "../core/05_route_list.hpp"
#include "../core/06_candidate_set.hpp"
#include "../core/07_seed_routes.hpp"
#include "../core/08_route_local_search.hpp"
#include "../core/09_intra_3opt_light.hpp"
#include "../core/10_inter_route_moves.hpp"
#include "../core/11_validation.hpp"
#include "../core/12_alns_framework.hpp"
#include "../core/13_destroy_ops.hpp"
#include "../core/14_repair_ops.hpp"
#include "../core/15_sa_engine.hpp"
#include "../core/16_parallel_pool.hpp"
#include "../core/18_autotune.hpp"
#include "../core/17_pipeline.hpp"

#include "minsum_accept.hpp"

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

namespace mtsp::v21 {

class LkhWrapperSolverV21MinsumCap : public mtsp::Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        for (const auto& [k, v] : opts) {
            if (k == "seed") seed_ = static_cast<unsigned>(std::stoul(v));
            else if (k == "time-budget-ms") time_budget_ms_ = std::stoi(v);
            else if (k == "threads") threads_override_ = std::stoi(v);
            else if (k == "route-cap") route_cap_override_ = std::stoi(v);
            else if (k == "route-cap-slack") route_cap_slack_ = std::stod(v);
        }
    }

    std::string GetLastStatus() const override { return "ok"; }
    std::string GetLastMessage() const override { return ""; }
    std::unordered_map<std::string, std::string> GetLastMetadata() const override { return last_metadata_; }

    void Solve(mtsp::RouteSet& out) override {
        const auto& inst = mtsp::Instance::GetInstance();
        const int n = inst.GetNodeCount();
        const int m = std::max(1, inst.GetSalesmanCount());
        AutoTuneParams params = ResolveParamsForInstance(n, m, /*is_minmax=*/false);
        if (threads_override_ > 0) params.num_threads = threads_override_;

        // Compute capacity. n-1 customers to distribute across m routes.
        const int customers = std::max(0, n - 1);
        const int target_size = (m > 0) ? (customers + m - 1) / m : customers;  // ceil((n-1)/m)
        int cap;
        if (route_cap_override_ > 0) {
            cap = route_cap_override_;
        } else {
            const double slack = std::max(0.0, route_cap_slack_);
            cap = static_cast<int>(std::ceil(static_cast<double>(target_size) * (1.0 + slack)));
            cap = std::max(cap, target_size);  // cap must be >= target_size
        }
        // Clamp: cap must allow distributing n-1 customers across m routes.
        // m * cap >= customers, otherwise repair has no valid placement.
        if (m > 0 && static_cast<long long>(m) * cap < customers) {
            cap = (customers + m - 1) / m;
        }
        params.route_cap = cap;

        MinsumAccept accept;
        PipelineMetadata meta;
        const int budget_ms = (time_budget_ms_ > 0 ? time_budget_ms_ : 60'000);
        RunPipeline(inst, accept, params, budget_ms, seed_, out, meta);

        // Sanitize: ensure exactly m routes with depot endpoints.
        DistanceOracle d(inst);
        SanitizeRoutes(out, inst);
        EnsureClosedDepot(out);
        meta.Set("validation_ok", ValidateRoutes(out, n) ? "true" : "false");
        meta.SetDouble("final_minsum", RouteSumLength(out, d));
        meta.SetDouble("final_max", MaxRouteLength(out, d));
        meta.SetInt("route_cap", cap);
        meta.SetInt("route_cap_target_size", target_size);
        last_metadata_ = std::move(meta.data);
    }

private:
    unsigned seed_ = 1u;
    int time_budget_ms_ = 0;
    int threads_override_ = 0;
    int route_cap_override_ = 0;       // 0 = derive from slack
    // 75% slack chosen empirically: on clustered-offset-depot n=10k m=100,
    // tighter caps (slack=0.10/0.25) hit the cap on many routes, forcing
    // expensive far-route fallbacks. Slack=0.75 lets the algorithm pick
    // best positions naturally while the cap occasionally redirects
    // over-concentration into emptier routes — best MINSUM and best balance.
    // Sweep results (seed=1, 60s, n=10k m=100):
    //   slack 0.10 cap=111: minsum 21641 max=736 (cap too tight)
    //   slack 0.25 cap=125: minsum 21188 max=463
    //   slack 0.50 cap=150: minsum 20502 max=506
    //   slack 0.75 cap=175: minsum 20215 max=343  <- best
    //   slack 1.00 cap=200: minsum 20712 max=309
    //   slack 1.50 cap=250: minsum 20974 max=322
    //   no cap (v21):       minsum 22158 max=513
    double route_cap_slack_ = 0.75;
    std::unordered_map<std::string, std::string> last_metadata_;
};

}  // namespace mtsp::v21

namespace mtsp {

static const bool reg_lkh_v21_minsum_cap = ([]() {
    SolverFactory::RegisterSolver("lkh_v21_minsum_cap", []() {
        return std::make_unique<v21::LkhWrapperSolverV21MinsumCap>();
    });
    return true;
})();

}  // namespace mtsp
