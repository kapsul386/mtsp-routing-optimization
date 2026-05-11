// MINSUM entry point for the modular ALNS-mTSP architecture.
// Wires the ALNS+SA+PT pipeline core (src/v21/core/) to the MinsumAccept
// criterion, registers the solver in SolverFactory under the name
// "lkh_v21_minsum" (referred to as `alns_minsum` in the report), and parses
// CLI overrides (seed, time-budget-ms, threads, per-operator knobs) via
// Configure(). Self-contained: pulls in the full header-only ALNS-mTSP core.

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

#include <memory>
#include <string>
#include <unordered_map>

namespace mtsp::v21 {

// Boolean CLI option parser: accepts 1/true/yes/on as true.
inline bool ParseBoolOptionV21Minsum(const std::string& v) {
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

// Baseline ALNS-mTSP solver for MINSUM. Reuses the shared pipeline from
// core/17_pipeline.hpp with the MinsumAccept criterion and default
// parameters from 18_autotune.hpp. Every CLI parameter is an optional
// override; whatever is not set falls back to the autotuned defaults.
class LkhWrapperSolverV21Minsum : public mtsp::Solver {
public:
    // CLI parsing. Supports a wide set of per-operator override flags
    // (granular-*, route-pair-*, region-reopt-*, classic-seeds, depot-seed-mode).
    // A value of -1 means "not set — use the autotuned value".
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        for (const auto& [k, v] : opts) {
            if (k == "seed") seed_ = static_cast<unsigned>(std::stoul(v));
            else if (k == "time-budget-ms") time_budget_ms_ = std::stoi(v);
            else if (k == "threads") threads_override_ = std::stoi(v);
            else if (k == "granular-every") granular_every_override_ = std::stoi(v);
            else if (k == "granular-max-moves") granular_max_moves_override_ = std::stoi(v);
            else if (k == "granular-scan-customers") granular_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-endpoint-bias-depth") granular_endpoint_bias_depth_override_ = std::stoi(v);
            else if (k == "granular-2optstar-every") granular_2optstar_every_override_ = std::stoi(v);
            else if (k == "granular-2optstar-max-moves") granular_2optstar_max_moves_override_ = std::stoi(v);
            else if (k == "granular-2optstar-scan-customers") granular_2optstar_scan_override_ = std::stoi(v);
            else if (k == "granular-oropt-every") granular_oropt_every_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-moves") granular_oropt_max_moves_override_ = std::stoi(v);
            else if (k == "granular-oropt-scan-customers") granular_oropt_scan_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-len") granular_oropt_max_len_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-every") route_pair_every_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-max-moves") route_pair_max_moves_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-k") route_pair_k_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-window") route_pair_window_override_ = std::stoi(v);
            else if (k == "region-reopt-every") region_reopt_every_override_ = std::stoi(v);
            else if (k == "region-reopt-k") region_reopt_k_override_ = std::stoi(v);
            else if (k == "classic-seeds") classic_seeds_override_ = ParseBoolOptionV21Minsum(v) ? 1 : 0;
            else if (k == "depot-seed-mode") depot_seed_mode_override_ = std::stoi(v);
            else if (k == "depot-seed-spread-prob") depot_seed_spread_prob_override_ = std::stod(v);
            else if (k == "rebalance-empty-routes") rebalance_empty_routes_ = ParseBoolOptionV21Minsum(v);
        }
    }

    // Status / message / metadata getters used by the JSON output wrapper.
    // This solver never reports an error, so status is always "ok".
    std::string GetLastStatus() const override { return "ok"; }
    std::string GetLastMessage() const override { return ""; }
    std::unordered_map<std::string, std::string> GetLastMetadata() const override { return last_metadata_; }

    // Main entry point: resolves parameters via autotune, applies the CLI
    // overrides, runs RunPipeline (the shared ALNS+SA loop), then validates
    // the result.
    void Solve(mtsp::RouteSet& out) override {
        const auto& inst = mtsp::Instance::GetInstance();
        const int n = inst.GetNodeCount();
        const int m = std::max(1, inst.GetSalesmanCount());
        AutoTuneParams params = ResolveParamsForInstance(n, m, /*is_minmax=*/false);
        if (threads_override_ > 0) params.num_threads = threads_override_;
        if (granular_every_override_ >= 0) params.granular_every = granular_every_override_;
        if (granular_max_moves_override_ >= 0) params.granular_max_moves = granular_max_moves_override_;
        if (granular_scan_customers_override_ >= 0) params.granular_scan_customers = granular_scan_customers_override_;
        if (region_reopt_every_override_ >= 0) params.region_reopt_every = region_reopt_every_override_;
        if (region_reopt_k_override_ >= 0) params.region_reopt_K = region_reopt_k_override_;
        if (granular_endpoint_bias_depth_override_ >= 0) params.granular_endpoint_bias_depth = granular_endpoint_bias_depth_override_;
        if (granular_2optstar_every_override_ >= 0) params.granular_2optstar_every = granular_2optstar_every_override_;
        if (granular_2optstar_max_moves_override_ >= 0) params.granular_2optstar_max_moves = granular_2optstar_max_moves_override_;
        if (granular_2optstar_scan_override_ >= 0) params.granular_2optstar_scan_customers = granular_2optstar_scan_override_;
        if (granular_oropt_every_override_ >= 0) params.granular_oropt_every = granular_oropt_every_override_;
        if (granular_oropt_max_moves_override_ >= 0) params.granular_oropt_max_moves = granular_oropt_max_moves_override_;
        if (granular_oropt_scan_override_ >= 0) params.granular_oropt_scan_customers = granular_oropt_scan_override_;
        if (granular_oropt_max_len_override_ >= 0) params.granular_oropt_max_len = granular_oropt_max_len_override_;
        if (route_pair_every_override_ >= 0) params.route_pair_2optstar_every = route_pair_every_override_;
        if (route_pair_max_moves_override_ >= 0) params.route_pair_2optstar_max_moves = route_pair_max_moves_override_;
        if (route_pair_k_override_ >= 0) params.route_pair_2optstar_k = route_pair_k_override_;
        if (route_pair_window_override_ >= 0) params.route_pair_2optstar_window = route_pair_window_override_;
        if (classic_seeds_override_ >= 0) params.use_classic_seeds = (classic_seeds_override_ != 0);
        if (depot_seed_mode_override_ >= 0) params.depot_seed_mode = depot_seed_mode_override_;
        if (depot_seed_spread_prob_override_ >= 0.0) params.depot_seed_spread_prob = depot_seed_spread_prob_override_;

        // Run the main pipeline: seed construction → candidate set → polish → ALNS → final 2-opt.
        // Default budget is 60 seconds, overridable via time-budget-ms.
        MinsumAccept accept;
        PipelineMetadata meta;
        const int budget_ms = (time_budget_ms_ > 0 ? time_budget_ms_ : 60'000);
        RunPipeline(inst, accept, params, budget_ms, seed_, out, meta);

        // Post-processing: exactly m routes, each closed back at the depot.
        DistanceOracle d(inst);
        SanitizeRoutes(out, inst);
        EnsureClosedDepot(out);
        const auto count_empty = [](const RouteSet& routes) {
            int empty = 0;
            for (const auto& route : routes) if (route.size() <= 2) ++empty;
            return empty;
        };
        meta.SetInt("empty_routes_before_rebalance", count_empty(out));
        meta.Set("rebalance_empty_routes", rebalance_empty_routes_ ? "true" : "false");
        if (rebalance_empty_routes_) {
            RebalanceEmptyRoutes(out, d);
            EnsureClosedDepot(out);
        }
        meta.SetInt("empty_routes_final", count_empty(out));
        meta.Set("validation_ok", ValidateRoutes(out, n) ? "true" : "false");
        meta.SetDouble("final_minsum", RouteSumLength(out, d));
        meta.SetDouble("final_max", MaxRouteLength(out, d));
        last_metadata_ = std::move(meta.data);
    }

private:
    unsigned seed_ = 1u;
    int time_budget_ms_ = 0;
    int threads_override_ = 0;
    int granular_every_override_ = -1;
    int granular_max_moves_override_ = -1;
    int granular_scan_customers_override_ = -1;
    int region_reopt_every_override_ = -1;
    int region_reopt_k_override_ = -1;
    int granular_endpoint_bias_depth_override_ = -1;
    int granular_2optstar_every_override_ = -1;
    int granular_2optstar_max_moves_override_ = -1;
    int granular_2optstar_scan_override_ = -1;
    int granular_oropt_every_override_ = -1;
    int granular_oropt_max_moves_override_ = -1;
    int granular_oropt_scan_override_ = -1;
    int granular_oropt_max_len_override_ = -1;
    int route_pair_every_override_ = -1;
    int route_pair_max_moves_override_ = -1;
    int route_pair_k_override_ = -1;
    int route_pair_window_override_ = -1;
    int classic_seeds_override_ = -1;
    int depot_seed_mode_override_ = -1;
    double depot_seed_spread_prob_override_ = -1.0;
    bool rebalance_empty_routes_ = false;
    std::unordered_map<std::string, std::string> last_metadata_;
};

}  // namespace mtsp::v21

namespace mtsp {

// Register the solver in SolverFactory under the name "lkh_v21_minsum"
// (matching the report notation `alns_minsum`). Registration happens when
// the translation unit is loaded — classic IIFE pattern.
static const bool reg_lkh_v21_minsum = ([]() {
    SolverFactory::RegisterSolver("lkh_v21_minsum", []() {
        return std::make_unique<v21::LkhWrapperSolverV21Minsum>();
    });
    return true;
})();

}  // namespace mtsp
