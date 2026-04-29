// v21 MIN-MAX solver. Self-contained.
// Registered as "lkh_v21_minmax" in mtsp::SolverFactory.

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

#include "minmax_accept.hpp"

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mtsp::v21 {

class LkhWrapperSolverV21Minmax : public mtsp::Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        for (const auto& [k, v] : opts) {
            if (k == "seed") seed_ = static_cast<unsigned>(std::stoul(v));
            else if (k == "time-budget-ms") time_budget_ms_ = std::stoi(v);
            else if (k == "threads") threads_override_ = std::stoi(v);
            else if (k == "minmax-soft-alpha") soft_alpha_override_ = std::stod(v);
        }
    }

    std::string GetLastStatus() const override { return "ok"; }
    std::string GetLastMessage() const override { return ""; }
    std::unordered_map<std::string, std::string> GetLastMetadata() const override { return last_metadata_; }

    void Solve(mtsp::RouteSet& out) override {
        const auto& inst = mtsp::Instance::GetInstance();
        const int n = inst.GetNodeCount();
        const int m = std::max(1, inst.GetSalesmanCount());
        AutoTuneParams params = ResolveParamsForInstance(n, m, /*is_minmax=*/true);
        if (threads_override_ > 0) params.num_threads = threads_override_;

        MinmaxAccept accept(params.minmax_lambda);
        if (soft_alpha_override_ >= 0.0) accept.SetSoftAlpha(soft_alpha_override_);
        else accept.SetSoftAlpha(params.minmax_soft_alpha_init);

        PipelineMetadata meta;
        const int budget_ms = (time_budget_ms_ > 0 ? time_budget_ms_ : 60'000);
        RunPipeline(inst, accept, params, budget_ms, seed_, out, meta);

        DistanceOracle d(inst);
        SanitizeRoutes(out, inst);
        EnsureClosedDepot(out);
        meta.Set("validation_ok", ValidateRoutes(out, n) ? "true" : "false");
        meta.SetDouble("final_minsum", RouteSumLength(out, d));
        meta.SetDouble("final_max", MaxRouteLength(out, d));
        last_metadata_ = std::move(meta.data);
    }

private:
    unsigned seed_ = 1u;
    int time_budget_ms_ = 0;
    int threads_override_ = 0;
    double soft_alpha_override_ = -1.0;
    std::unordered_map<std::string, std::string> last_metadata_;
};

}  // namespace mtsp::v21

namespace mtsp {

static const bool reg_lkh_v21_minmax = ([]() {
    SolverFactory::RegisterSolver("lkh_v21_minmax", []() {
        return std::make_unique<v21::LkhWrapperSolverV21Minmax>();
    });
    return true;
})();

}  // namespace mtsp
