// Experimental MINSUM preset around the fast depot-2m seed. Registered as
// "lkh_v21_minsum_depot2m_plus"; it intentionally leaves the stable
// "lkh_v21_minsum" defaults untouched.

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
#include "../core/18_autotune.hpp"
#include "../core/17_pipeline.hpp"

#include "minsum_accept.hpp"

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace mtsp::v21 {

inline bool ParseBoolOptionV21Depot2mPlus(const std::string& v) {
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

inline std::vector<double> ParseDoubleListV21Depot2mPlus(const std::string& v) {
    std::vector<double> out;
    size_t start = 0;
    while (start < v.size()) {
        size_t end = v.find_first_of(",;", start);
        if (end == std::string::npos) end = v.size();
        if (end > start) out.push_back(std::stod(v.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

inline std::vector<int> ParseIntListV21Depot2mPlus(const std::string& v) {
    std::vector<int> out;
    size_t start = 0;
    while (start < v.size()) {
        size_t end = v.find_first_of(",;", start);
        if (end == std::string::npos) end = v.size();
        if (end > start) out.push_back(std::stoi(v.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

inline void ApplyDepot2mPlusPreset(AutoTuneParams& p, int n, int m) {
    p.use_classic_seeds = false;
    p.depot_seed_mode = 2;
    p.depot_seed_spread_prob = 0.5;
    p.depot_seed_spread_probs = {0.0, 0.25, 0.5, 0.75, 1.0};
    p.depot_seed_restarts = 2;
    p.depot_seed_rings.clear();
    // Split-aware opt-in-by-quality seed. A raw single route is not a valid
    // mTSP solution, but with smart rebalance enabled it is a strong start on
    // clustered depot-centric instances. The 10% gain gate rejects uniform
    // 100k/m10 where the single route is not competitive.
    p.use_single_route_seed = true;
    p.single_route_seed_min_gain = 0.10;
    p.single_route_rebalance_seed = true;
    p.angular_seed_rotations = 0;
    p.angular_seed_pool_multiplier = 8;
    p.angular_seed_quantiles.clear();
    // Kept as an opt-in ablation. A +20s top-2 seed race on clustered 100k
    // picked the same constructive seed and worsened the final trajectory.
    p.seed_race_count = 0;
    p.seed_race_ms = 0;
    p.seed_race_extra_ms = 0;
    p.seed_race_start_from_raced = false;

    // Keep the constructive candidate graph identical to the stable depot2m
    // run. Widening k-NN is still exposed as `--k-nn`, but on uniform 100k/m10
    // it made the round-robin depot seed worse by changing the city-stealing
    // order between agents.

    // Spend the time saved by skipping classic seeds in the main search.
    p.budget_seed_pct = 1;
    p.budget_cand_pct = 8;
    p.budget_polish_pct = 9;
    p.budget_alns_pct = 86;
    p.budget_final_pct = 5;

    // More exploratory ALNS/SA: larger destroys, slower cooling, quicker
    // wall-clock reheats, and more frequent GLS edge pressure.
    p.K_destroy_init = std::max(p.K_destroy_init, 220);
    p.K_destroy_max = std::max(p.K_destroy_max, 4200);
    p.T_frac_init = std::max(p.T_frac_init, 0.04);
    p.sa_cooling = std::max(p.sa_cooling, 0.995);
    p.reheat_after = std::min(p.reheat_after, 80);
    p.reheat_after_ms = (p.reheat_after_ms > 0) ? std::min(p.reheat_after_ms, 18000) : 18000;
    p.gls_penalize_after = std::min(p.gls_penalize_after, 25);

    // FILO2-like granular intensification: more relocate attempts, plus
    // experimental inter-route 2-opt* tail swaps.
    p.granular_every = 3;
    p.granular_max_moves = 12;
    p.granular_scan_customers = 2048;
    p.granular_2optstar_every = 3;
    p.granular_2optstar_max_moves = 4;
    p.granular_2optstar_scan_customers = 1280;
    p.granular_oropt_every = 6;
    p.granular_oropt_max_moves = 3;
    p.granular_oropt_scan_customers = 896;
    p.granular_oropt_max_len = 3;

    // The earlier experiment showed region/pair/POPMUSIC mostly produced
    // calls without accepted moves on uniform 100k/m10, so this preset spends
    // that time on granular route mixing instead.
    p.region_reopt_every = 0;
    p.region_reopt_K = 0;
    p.pair_reopt_every = 0;
    p.popmusic_every = 0;
    p.popmusic_K = 0;

    if (m >= 50) {
        p.granular_scan_customers = std::max(512, p.granular_scan_customers / 2);
        p.granular_2optstar_scan_customers = std::max(256, p.granular_2optstar_scan_customers / 2);
        p.granular_oropt_scan_customers = std::max(256, p.granular_oropt_scan_customers / 2);
    }
    if (m >= 100) {
        p.granular_max_moves = std::max(5, p.granular_max_moves - 2);
        p.granular_2optstar_max_moves = 1;
        p.granular_oropt_max_moves = 1;
    }
}

class LkhWrapperSolverV21MinsumDepot2mPlus : public mtsp::Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        for (const auto& [k, v] : opts) {
            if (k == "seed") seed_ = static_cast<unsigned>(std::stoul(v));
            else if (k == "time-budget-ms") time_budget_ms_ = std::stoi(v);
            else if (k == "threads") threads_override_ = std::stoi(v);
            else if (k == "k-nn") k_nn_override_ = std::stoi(v);
            else if (k == "classic-seeds") classic_seeds_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            else if (k == "depot-seed-mode") depot_seed_mode_override_ = std::stoi(v);
            else if (k == "depot-seed-spread-prob") depot_seed_spread_prob_override_ = std::stod(v);
            else if (k == "depot-seed-spread-list") {
                depot_seed_spread_probs_override_ = ParseDoubleListV21Depot2mPlus(v);
                depot_seed_spread_probs_set_ = true;
            }
            else if (k == "depot-seed-restarts") depot_seed_restarts_override_ = std::stoi(v);
            else if (k == "depot-seed-rings") {
                depot_seed_rings_override_ = ParseIntListV21Depot2mPlus(v);
                depot_seed_rings_set_ = true;
            }
            else if (k == "single-route-seed") single_route_seed_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            else if (k == "single-route-min-gain") single_route_min_gain_override_ = std::stod(v);
            else if (k == "single-route-rebalanced-seed") {
                single_route_rebalanced_seed_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            }
            else if (k == "angular-seed-rotations") angular_seed_rotations_override_ = std::stoi(v);
            else if (k == "angular-seed-pool-multiplier") angular_seed_pool_multiplier_override_ = std::stoi(v);
            else if (k == "angular-seed-quantiles") {
                angular_seed_quantiles_override_ = ParseDoubleListV21Depot2mPlus(v);
                angular_seed_quantiles_set_ = true;
            }
            else if (k == "seed-race-count") seed_race_count_override_ = std::stoi(v);
            else if (k == "seed-race-ms") seed_race_ms_override_ = std::stoi(v);
            else if (k == "seed-race-extra-ms") seed_race_extra_ms_override_ = std::stoi(v);
            else if (k == "seed-race-start-raced") seed_race_start_from_raced_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            else if (k == "granular-every") granular_every_override_ = std::stoi(v);
            else if (k == "granular-max-moves") granular_max_moves_override_ = std::stoi(v);
            else if (k == "granular-scan-customers") granular_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-2optstar-every") granular_2optstar_every_override_ = std::stoi(v);
            else if (k == "granular-2optstar-max-moves") granular_2optstar_max_moves_override_ = std::stoi(v);
            else if (k == "granular-2optstar-scan-customers") granular_2optstar_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-oropt-every") granular_oropt_every_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-moves") granular_oropt_max_moves_override_ = std::stoi(v);
            else if (k == "granular-oropt-scan-customers") granular_oropt_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-len") granular_oropt_max_len_override_ = std::stoi(v);
            else if (k == "region-reopt-every") region_reopt_every_override_ = std::stoi(v);
            else if (k == "region-reopt-k") region_reopt_k_override_ = std::stoi(v);
            else if (k == "pair-reopt-every") pair_reopt_every_override_ = std::stoi(v);
            else if (k == "popmusic-every") popmusic_every_override_ = std::stoi(v);
            else if (k == "popmusic-k") popmusic_k_override_ = std::stoi(v);
            else if (k == "rebalance-empty-routes") rebalance_empty_routes_ = ParseBoolOptionV21Depot2mPlus(v);
            else if (k == "rebalance-post-ms") rebalance_post_ms_ = std::stoi(v);
            else if (k == "rebalance-post-polish") rebalance_post_polish_ = ParseBoolOptionV21Depot2mPlus(v);
            else if (k == "rebalance-compensate-search-time") {
                rebalance_compensate_search_time_ = ParseBoolOptionV21Depot2mPlus(v);
            }
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
        ApplyDepot2mPlusPreset(params, n, m);

        if (threads_override_ > 0) params.num_threads = threads_override_;
        if (k_nn_override_ > 0) params.k_NN = k_nn_override_;
        if (classic_seeds_override_ >= 0) params.use_classic_seeds = (classic_seeds_override_ != 0);
        if (depot_seed_mode_override_ >= 0) params.depot_seed_mode = depot_seed_mode_override_;
        if (depot_seed_spread_prob_override_ >= 0.0) {
            params.depot_seed_spread_prob = depot_seed_spread_prob_override_;
            params.depot_seed_spread_probs.clear();
        }
        if (depot_seed_spread_probs_set_) params.depot_seed_spread_probs = depot_seed_spread_probs_override_;
        if (depot_seed_restarts_override_ >= 0) params.depot_seed_restarts = depot_seed_restarts_override_;
        if (depot_seed_rings_set_) params.depot_seed_rings = depot_seed_rings_override_;
        if (single_route_seed_override_ >= 0) params.use_single_route_seed = (single_route_seed_override_ != 0);
        if (single_route_min_gain_override_ >= 0.0) params.single_route_seed_min_gain = single_route_min_gain_override_;
        if (single_route_rebalanced_seed_override_ >= 0) {
            params.single_route_rebalance_seed = (single_route_rebalanced_seed_override_ != 0);
        }
        if (angular_seed_rotations_override_ >= 0) params.angular_seed_rotations = angular_seed_rotations_override_;
        if (angular_seed_pool_multiplier_override_ >= 0) {
            params.angular_seed_pool_multiplier = angular_seed_pool_multiplier_override_;
        }
        if (angular_seed_quantiles_set_) params.angular_seed_quantiles = angular_seed_quantiles_override_;
        if (seed_race_count_override_ >= 0) params.seed_race_count = seed_race_count_override_;
        if (seed_race_ms_override_ >= 0) params.seed_race_ms = seed_race_ms_override_;
        if (seed_race_extra_ms_override_ >= 0) params.seed_race_extra_ms = seed_race_extra_ms_override_;
        if (seed_race_start_from_raced_override_ >= 0) {
            params.seed_race_start_from_raced = (seed_race_start_from_raced_override_ != 0);
        }
        if (granular_every_override_ >= 0) params.granular_every = granular_every_override_;
        if (granular_max_moves_override_ >= 0) params.granular_max_moves = granular_max_moves_override_;
        if (granular_scan_customers_override_ >= 0) params.granular_scan_customers = granular_scan_customers_override_;
        if (granular_2optstar_every_override_ >= 0) params.granular_2optstar_every = granular_2optstar_every_override_;
        if (granular_2optstar_max_moves_override_ >= 0) params.granular_2optstar_max_moves = granular_2optstar_max_moves_override_;
        if (granular_2optstar_scan_customers_override_ >= 0) params.granular_2optstar_scan_customers = granular_2optstar_scan_customers_override_;
        if (granular_oropt_every_override_ >= 0) params.granular_oropt_every = granular_oropt_every_override_;
        if (granular_oropt_max_moves_override_ >= 0) params.granular_oropt_max_moves = granular_oropt_max_moves_override_;
        if (granular_oropt_scan_customers_override_ >= 0) params.granular_oropt_scan_customers = granular_oropt_scan_customers_override_;
        if (granular_oropt_max_len_override_ >= 0) params.granular_oropt_max_len = granular_oropt_max_len_override_;
        if (region_reopt_every_override_ >= 0) params.region_reopt_every = region_reopt_every_override_;
        if (region_reopt_k_override_ >= 0) params.region_reopt_K = region_reopt_k_override_;
        if (pair_reopt_every_override_ >= 0) params.pair_reopt_every = pair_reopt_every_override_;
        if (popmusic_every_override_ >= 0) params.popmusic_every = popmusic_every_override_;
        if (popmusic_k_override_ >= 0) params.popmusic_K = popmusic_k_override_;
        const int rebalance_search_compensation_ms =
            (rebalance_empty_routes_ && rebalance_compensate_search_time_)
                ? std::max(0, rebalance_post_ms_)
                : 0;
        params.extra_alns_ms += rebalance_search_compensation_ms;

        MinsumAccept accept;
        PipelineMetadata meta;
        meta.Set("preset", "depot2m_plus");
        meta.SetInt("rebalance_search_compensation_ms", rebalance_search_compensation_ms);
        meta.SetInt("rebalance_compensate_search_time", rebalance_compensate_search_time_ ? 1 : 0);
        const int budget_ms = (time_budget_ms_ > 0 ? time_budget_ms_ : 60'000);
        RunPipeline(inst, accept, params, budget_ms, seed_, out, meta);

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
            const double after_rebalance = RouteSumLength(out, d);
            meta.SetDouble("after_rebalance_minsum", after_rebalance);

            if (rebalance_post_ms_ > 0 && count_empty(out) == 0 && n > 16) {
                SearchBudget post_budget(rebalance_post_ms_);
                CandidateSets post_candidates = BuildKnnCandidates(inst, std::max(8, params.k_NN), post_budget);
                SymmetrizeCandidates(post_candidates);

                if (rebalance_post_polish_ && !post_budget.ForceCheck()) {
                    const int post_polish_cap = std::max(500, post_budget.RemainingMs() / 2);
                    SearchBudget post_polish_budget = post_budget.SubBudget(post_polish_cap);
                    RawDistFn raw_dist{inst};
                    ParallelFinal2Opt(out, post_candidates, raw_dist, post_polish_budget, n);
                    EnsureClosedDepot(out);
                    meta.SetInt("rebalance_post_polish_ms", post_polish_budget.ElapsedMs());
                    meta.SetDouble("after_rebalance_post_polish_minsum", RouteSumLength(out, d));
                } else {
                    meta.SetInt("rebalance_post_polish_ms", 0);
                }

                RouteList rl(n, m);
                rl.LoadFrom(out, d);
                std::mt19937 post_rng(seed_ ^ 0x9e3779b9u);
                GranularMoveStats post_stats;
                int post_moves = 0;
                int post_rounds = 0;
                while (!post_budget.ForceCheck() && post_rounds < 200) {
                    int round_moves = 0;
                    SearchBudget star_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                    round_moves += TryGranularTwoOptStarPass(rl, accept, d, post_candidates, star_budget,
                                                             post_rng, 8, 4096, &post_stats);
                    if (!post_budget.ForceCheck()) {
                        SearchBudget or_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                        round_moves += TryGranularOrOptPass(rl, accept, d, post_candidates, or_budget,
                                                            post_rng, 6, 4096, params.route_cap, 3, &post_stats);
                    }
                    if (!post_budget.ForceCheck()) {
                        SearchBudget rel_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                        round_moves += TryGranularInterRoutePass(rl, accept, d, post_candidates, rel_budget,
                                                                 post_rng, 12, 8192, params.route_cap, &post_stats);
                    }
                    post_moves += round_moves;
                    ++post_rounds;
                    if (round_moves == 0) break;
                }

                RouteSet refined;
                rl.StoreTo(refined);
                EnsureClosedDepot(refined);
                if (count_empty(refined) > 0) {
                    RebalanceEmptyRoutes(refined, d);
                    EnsureClosedDepot(refined);
                }
                const double refined_cost = RouteSumLength(refined, d);
                if (count_empty(refined) == 0 && refined_cost + kEps < after_rebalance) {
                    out = std::move(refined);
                }
                meta.SetInt("rebalance_post_ms", rebalance_post_ms_);
                meta.SetInt("rebalance_post_rounds", post_rounds);
                meta.SetInt("rebalance_post_moves", post_moves);
                meta.SetInt("rebalance_post_2optstar", post_stats.two_optstar_accepts);
                meta.SetInt("rebalance_post_oropt", post_stats.oropt_accepts);
                meta.SetInt("rebalance_post_relocate", post_stats.relocate_accepts);
                meta.SetInt("rebalance_post_swap", post_stats.swap_accepts);
                meta.SetDouble("after_rebalance_post_minsum", RouteSumLength(out, d));
            }
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
    int k_nn_override_ = 0;
    int classic_seeds_override_ = -1;
    int depot_seed_mode_override_ = -1;
    double depot_seed_spread_prob_override_ = -1.0;
    std::vector<double> depot_seed_spread_probs_override_;
    bool depot_seed_spread_probs_set_ = false;
    int depot_seed_restarts_override_ = -1;
    std::vector<int> depot_seed_rings_override_;
    bool depot_seed_rings_set_ = false;
    int single_route_seed_override_ = -1;
    double single_route_min_gain_override_ = -1.0;
    int single_route_rebalanced_seed_override_ = -1;
    int angular_seed_rotations_override_ = -1;
    int angular_seed_pool_multiplier_override_ = -1;
    std::vector<double> angular_seed_quantiles_override_;
    bool angular_seed_quantiles_set_ = false;
    int seed_race_count_override_ = -1;
    int seed_race_ms_override_ = -1;
    int seed_race_extra_ms_override_ = -1;
    int seed_race_start_from_raced_override_ = -1;
    int granular_every_override_ = -1;
    int granular_max_moves_override_ = -1;
    int granular_scan_customers_override_ = -1;
    int granular_2optstar_every_override_ = -1;
    int granular_2optstar_max_moves_override_ = -1;
    int granular_2optstar_scan_customers_override_ = -1;
    int granular_oropt_every_override_ = -1;
    int granular_oropt_max_moves_override_ = -1;
    int granular_oropt_scan_customers_override_ = -1;
    int granular_oropt_max_len_override_ = -1;
    int region_reopt_every_override_ = -1;
    int region_reopt_k_override_ = -1;
    int pair_reopt_every_override_ = -1;
    int popmusic_every_override_ = -1;
    int popmusic_k_override_ = -1;
    bool rebalance_empty_routes_ = true;
    int rebalance_post_ms_ = 15000;
    bool rebalance_post_polish_ = true;
    bool rebalance_compensate_search_time_ = true;
    std::unordered_map<std::string, std::string> last_metadata_;
};

}  // namespace mtsp::v21

namespace mtsp {

static const bool reg_lkh_v21_minsum_depot2m_plus = ([]() {
    SolverFactory::RegisterSolver("lkh_v21_minsum_depot2m_plus", []() {
        return std::make_unique<v21::LkhWrapperSolverV21MinsumDepot2mPlus>();
    });
    return true;
})();

}  // namespace mtsp
