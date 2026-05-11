// Experimental MINSUM preset built on the fast depot-2m seed.
// Registered as "lkh_v21_minsum_depot2m_plus" (reported as alns_depot2m).
// Goal: leave the base alns_minsum untouched and provide a separate preset for
// scenarios where minimising the sum is the priority (even at the cost of balance).
// Uses the LKH-3 default degeneration strategy (one large TSP loop), bounded
// by the given time limit.

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
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace mtsp::v21 {

// Boolean CLI parameter parser for the depot2m-plus variant.
inline bool ParseBoolOptionV21Depot2mPlus(const std::string& v) {
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

// Parser for a comma- or semicolon-separated list of doubles from a CLI parameter
// (e.g., "0.1,0.2,0.3" or "0.1;0.2;0.3"). Used to pass multiple values as a
// single CLI option that maps to a vector<double>.
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

// Parser for a comma- or semicolon-separated list of integers from a CLI parameter.
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
    p.single_route_start_variants = 0;
    p.pre_final_rebalance = true;
    p.valid_rebalance_tracking = true;
    p.valid_rebalance_track_every = 25;
    p.valid_rebalance_track_max = 8;
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
    p.granular_endpoint_bias_depth = 0;
    p.granular_2optstar_every = 3;
    p.granular_2optstar_max_moves = 4;
    p.granular_2optstar_scan_customers = 1280;
    p.granular_oropt_every = 6;
    p.granular_oropt_max_moves = 3;
    p.granular_oropt_scan_customers = 896;
    p.granular_oropt_max_len = 3;
    // Kept as an opt-in experiment: route-aware candidates around endpoints
    // and expensive route edges increased candidate noise on clustered 100k
    // seed 1, so the stable preset leaves the graph kNN-only by default.
    p.route_candidate_augmentation = false;
    p.route_candidate_endpoint_depth = 3;
    p.route_candidate_expensive_edges_per_route = 48;
    p.route_candidate_knn_probe = 128;
    p.route_candidate_per_anchor = 4;
    p.route_candidate_max_extra_per_node = 6;

    // The earlier experiment showed region/pair/POPMUSIC mostly produced
    // calls without accepted moves on uniform 100k/m10, so this preset spends
    // that time on granular route mixing instead.
    p.region_reopt_every = 0;
    p.region_reopt_K = 0;
    p.pair_reopt_every = 0;
    p.popmusic_every = 0;
    p.popmusic_K = 0;

    // Endpoint-focused route-pair 2-opt* (opt-in, default OFF). Targets the
    // depot-bridge / endpoint-stitch case the global k-NN graph misses.
    // The shortlist is built on demand from the current RouteList; it does
    // not modify the global candidate graph. Stable preset leaves this off
    // until the empirical benchmark confirms it helps clustered seeds 1/2/3
    // average without hurting uniform.
    p.route_pair_2optstar_every = 0;
    p.route_pair_2optstar_max_moves = 0;
    p.route_pair_2optstar_k = 2;
    p.route_pair_2optstar_window = 4;
    p.route_pair_2optstar_max_pairs = 0;

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

// Concrete Solver implementation for the "lkh_v21_minsum_depot2m_plus" preset.
// Wraps RunPipeline<MinsumAccept> with the depot-2m seed strategy, FILO2-style
// granular intensification, and optional post-solve smart rebalance. All knobs
// are exposed as string-keyed options via Configure so the factory can pass
// benchmark overrides without recompiling.
class LkhWrapperSolverV21MinsumDepot2mPlus : public mtsp::Solver {
public:
    // Parse string key-value options into solver-level overrides. Unrecognised
    // keys are silently ignored; recognised keys shadow the preset defaults.
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
            else if (k == "single-route-start-variants") single_route_start_variants_override_ = std::stoi(v);
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
            else if (k == "granular-endpoint-bias-depth") granular_endpoint_bias_depth_override_ = std::stoi(v);
            else if (k == "granular-2optstar-every") granular_2optstar_every_override_ = std::stoi(v);
            else if (k == "granular-2optstar-max-moves") granular_2optstar_max_moves_override_ = std::stoi(v);
            else if (k == "granular-2optstar-scan-customers") granular_2optstar_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-oropt-every") granular_oropt_every_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-moves") granular_oropt_max_moves_override_ = std::stoi(v);
            else if (k == "granular-oropt-scan-customers") granular_oropt_scan_customers_override_ = std::stoi(v);
            else if (k == "granular-oropt-max-len") granular_oropt_max_len_override_ = std::stoi(v);
            else if (k == "route-candidate-augmentation") {
                route_candidate_augmentation_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            }
            else if (k == "route-candidate-endpoint-depth") route_candidate_endpoint_depth_override_ = std::stoi(v);
            else if (k == "route-candidate-expensive-edges") {
                route_candidate_expensive_edges_override_ = std::stoi(v);
            }
            else if (k == "route-candidate-knn-probe") route_candidate_knn_probe_override_ = std::stoi(v);
            else if (k == "route-candidate-per-anchor") route_candidate_per_anchor_override_ = std::stoi(v);
            else if (k == "route-candidate-max-extra") route_candidate_max_extra_override_ = std::stoi(v);
            else if (k == "region-reopt-every") region_reopt_every_override_ = std::stoi(v);
            else if (k == "region-reopt-k") region_reopt_k_override_ = std::stoi(v);
            else if (k == "pair-reopt-every") pair_reopt_every_override_ = std::stoi(v);
            else if (k == "popmusic-every") popmusic_every_override_ = std::stoi(v);
            else if (k == "popmusic-k") popmusic_k_override_ = std::stoi(v);
            else if (k == "rebalance-empty-routes") rebalance_empty_routes_ = ParseBoolOptionV21Depot2mPlus(v);
            else if (k == "pre-final-rebalance") pre_final_rebalance_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            else if (k == "valid-rebalance-tracking") {
                valid_rebalance_tracking_override_ = ParseBoolOptionV21Depot2mPlus(v) ? 1 : 0;
            }
            else if (k == "valid-rebalance-track-every") valid_rebalance_track_every_override_ = std::stoi(v);
            else if (k == "valid-rebalance-track-max") valid_rebalance_track_max_override_ = std::stoi(v);
            else if (k == "rebalance-post-ms") rebalance_post_ms_ = std::stoi(v);
            else if (k == "rebalance-post-polish") rebalance_post_polish_ = ParseBoolOptionV21Depot2mPlus(v);
            else if (k == "rebalance-compensate-search-time") {
                rebalance_compensate_search_time_ = ParseBoolOptionV21Depot2mPlus(v);
            }
            else if (k == "route-pair-2optstar-every") route_pair_2optstar_every_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-max-moves") route_pair_2optstar_max_moves_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-k") route_pair_2optstar_k_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-window") route_pair_2optstar_window_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-max-pairs") route_pair_2optstar_max_pairs_override_ = std::stoi(v);
            else if (k == "route-pair-2optstar-post-rounds") route_pair_post_rounds_override_ = std::stoi(v);
            else if (k == "rebalance-post-intra-oropt") {
                rebalance_post_intra_oropt_ = ParseBoolOptionV21Depot2mPlus(v);
            }
            else if (k == "rebalance-post-intra-oropt-share") {
                // Denominator: cap = post_budget.RemainingMs() / share.
                // Default 4 (= 25% of remaining post-rebalance phase).
                rebalance_post_intra_oropt_share_ = std::max(1, std::stoi(v));
            }
        }
    }

    // Always returns "ok" — this solver does not produce recoverable error states.
    std::string GetLastStatus() const override { return "ok"; }
    // Always returns an empty string — no diagnostic message is produced.
    std::string GetLastMessage() const override { return ""; }
    // Return the metadata map populated during the most recent Solve call.
    std::unordered_map<std::string, std::string> GetLastMetadata() const override { return last_metadata_; }

    // Run the full depot-2m-plus pipeline on the global Instance and write the
    // result to `out`. Applies AutoTuneParams, the depot2m-plus preset, all CLI
    // overrides, and optionally a post-solve empty-route rebalance pass.
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
        if (single_route_start_variants_override_ >= 0) {
            params.single_route_start_variants = single_route_start_variants_override_;
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
        if (granular_endpoint_bias_depth_override_ >= 0) {
            params.granular_endpoint_bias_depth = granular_endpoint_bias_depth_override_;
        }
        if (granular_2optstar_every_override_ >= 0) params.granular_2optstar_every = granular_2optstar_every_override_;
        if (granular_2optstar_max_moves_override_ >= 0) params.granular_2optstar_max_moves = granular_2optstar_max_moves_override_;
        if (granular_2optstar_scan_customers_override_ >= 0) params.granular_2optstar_scan_customers = granular_2optstar_scan_customers_override_;
        if (granular_oropt_every_override_ >= 0) params.granular_oropt_every = granular_oropt_every_override_;
        if (granular_oropt_max_moves_override_ >= 0) params.granular_oropt_max_moves = granular_oropt_max_moves_override_;
        if (granular_oropt_scan_customers_override_ >= 0) params.granular_oropt_scan_customers = granular_oropt_scan_customers_override_;
        if (granular_oropt_max_len_override_ >= 0) params.granular_oropt_max_len = granular_oropt_max_len_override_;
        if (route_candidate_augmentation_override_ >= 0) {
            params.route_candidate_augmentation = (route_candidate_augmentation_override_ != 0);
        }
        if (route_candidate_endpoint_depth_override_ >= 0) {
            params.route_candidate_endpoint_depth = route_candidate_endpoint_depth_override_;
        }
        if (route_candidate_expensive_edges_override_ >= 0) {
            params.route_candidate_expensive_edges_per_route = route_candidate_expensive_edges_override_;
        }
        if (route_candidate_knn_probe_override_ >= 0) {
            params.route_candidate_knn_probe = route_candidate_knn_probe_override_;
        }
        if (route_candidate_per_anchor_override_ >= 0) {
            params.route_candidate_per_anchor = route_candidate_per_anchor_override_;
        }
        if (route_candidate_max_extra_override_ >= 0) {
            params.route_candidate_max_extra_per_node = route_candidate_max_extra_override_;
        }
        if (region_reopt_every_override_ >= 0) params.region_reopt_every = region_reopt_every_override_;
        if (region_reopt_k_override_ >= 0) params.region_reopt_K = region_reopt_k_override_;
        if (pair_reopt_every_override_ >= 0) params.pair_reopt_every = pair_reopt_every_override_;
        if (popmusic_every_override_ >= 0) params.popmusic_every = popmusic_every_override_;
        if (popmusic_k_override_ >= 0) params.popmusic_K = popmusic_k_override_;
        if (route_pair_2optstar_every_override_ >= 0) {
            params.route_pair_2optstar_every = route_pair_2optstar_every_override_;
        }
        if (route_pair_2optstar_max_moves_override_ >= 0) {
            params.route_pair_2optstar_max_moves = route_pair_2optstar_max_moves_override_;
        }
        if (route_pair_2optstar_k_override_ >= 0) {
            params.route_pair_2optstar_k = route_pair_2optstar_k_override_;
        }
        if (route_pair_2optstar_window_override_ >= 0) {
            params.route_pair_2optstar_window = route_pair_2optstar_window_override_;
        }
        if (route_pair_2optstar_max_pairs_override_ >= 0) {
            params.route_pair_2optstar_max_pairs = route_pair_2optstar_max_pairs_override_;
        }
        if (pre_final_rebalance_override_ >= 0) params.pre_final_rebalance = (pre_final_rebalance_override_ != 0);
        if (valid_rebalance_tracking_override_ >= 0) {
            params.valid_rebalance_tracking = (valid_rebalance_tracking_override_ != 0);
        }
        if (valid_rebalance_track_every_override_ > 0) {
            params.valid_rebalance_track_every = valid_rebalance_track_every_override_;
        }
        if (valid_rebalance_track_max_override_ >= 0) {
            params.valid_rebalance_track_max = valid_rebalance_track_max_override_;
        }
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
                if (params.route_candidate_augmentation) {
                    const auto t_post_aug = std::chrono::steady_clock::now();
                    CandidateAugmentStats post_aug = AugmentWithRouteBoundaryCandidates(
                        inst, out, post_candidates, d,
                        params.route_candidate_endpoint_depth,
                        params.route_candidate_expensive_edges_per_route,
                        params.route_candidate_knn_probe,
                        params.route_candidate_per_anchor,
                        params.route_candidate_max_extra_per_node);
                    meta.SetInt("rebalance_post_route_candidate_aug_ms", ElapsedMs(t_post_aug));
                    meta.SetInt("rebalance_post_route_candidate_aug_anchors", post_aug.anchors);
                    meta.SetInt("rebalance_post_route_candidate_aug_edges", post_aug.edges_added);
                    meta.SetDouble("rebalance_post_route_candidate_avg", AverageCandidateListSize(post_candidates));
                }

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

                // Up-front intra-route Or-opt pass on the freshly-polished routes.
                // ParallelFinal2Opt above only does 2-opt edge swaps and skips
                // Exhaustive2Opt for routes >4500 customers — the n=100k/m=10 case
                // every time. Or-opt with segment lengths 1..3 is structurally
                // disjoint from 2-opt and catches residual short-segment moves
                // that 2-opt cannot reach. This runs once before the granular
                // inter-route loop so granular operates on a richer local minimum.
                int post_intra_initial_routes_improved = 0;
                int post_intra_initial_ms = 0;
                meta.Set("rebalance_post_intra_oropt_enabled",
                         rebalance_post_intra_oropt_ ? "true" : "false");
                if (rebalance_post_intra_oropt_ && !post_budget.ForceCheck()) {
                    const auto t_intra = std::chrono::steady_clock::now();
                    // Time-budget fraction: cap = max(300ms, post_budget / 4),
                    // i.e. 25% of the remaining post-rebalance phase. This is
                    // the formula that produced the validated paired-A/B win
                    // (clustered avg -0.48%, uniform avg -0.50%). SubBudget
                    // clamps to the parent deadline.
                    const int intra_cap_share =
                        std::max(1, rebalance_post_intra_oropt_share_);
                    const int intra_cap =
                        std::max(300, post_budget.RemainingMs() / intra_cap_share);
                    SearchBudget intra_budget = post_budget.SubBudget(intra_cap);
                    RawDistFn intra_raw{inst};
                    post_intra_initial_routes_improved = ParallelFinalIntraOrOpt(
                        out, post_candidates, intra_raw, intra_budget, n);
                    EnsureClosedDepot(out);
                    post_intra_initial_ms = ElapsedMs(t_intra);
                    meta.SetInt("rebalance_post_intra_oropt_initial_cap_ms", intra_cap);
                    meta.SetInt("rebalance_post_intra_oropt_share", intra_cap_share);
                }
                meta.SetInt("rebalance_post_intra_oropt_initial_routes_improved",
                            post_intra_initial_routes_improved);
                meta.SetInt("rebalance_post_intra_oropt_initial_ms", post_intra_initial_ms);
                meta.SetDouble("after_rebalance_post_intra_oropt_minsum", RouteSumLength(out, d));

                RouteList rl(n, m);
                rl.LoadFrom(out, d);
                std::mt19937 post_rng(seed_ ^ 0x9e3779b9u);
                GranularMoveStats post_stats;
                int post_moves = 0;
                int post_rounds = 0;
                int post_route_pair_moves = 0;
                int post_route_pair_rounds = 0;
                int post_intra_oropt_calls = 0;
                int post_intra_oropt_route_improvements = 0;
                int post_intra_oropt_rescues = 0;
                const int post_route_pair_max_rounds =
                    (route_pair_post_rounds_override_ >= 0)
                        ? route_pair_post_rounds_override_
                        : (params.route_pair_2optstar_every > 0 &&
                           params.route_pair_2optstar_max_moves > 0
                               ? 4 : 0);
                RawDistFn post_raw_dist{inst};
                while (!post_budget.ForceCheck() && post_rounds < 200) {
                    int round_moves = 0;
                    if (post_route_pair_rounds < post_route_pair_max_rounds &&
                        !post_budget.ForceCheck()) {
                        SearchBudget rp_budget = post_budget.SubBudget(
                            std::max(100, post_budget.RemainingMs() / 6));
                        const int rp_moved = BuildAndRunRoutePair2OptStar(
                            rl, accept, d, rp_budget,
                            std::max(1, params.route_pair_2optstar_k),
                            params.route_pair_2optstar_max_pairs,
                            std::max(1, params.route_pair_2optstar_max_moves),
                            std::max(1, params.route_pair_2optstar_window),
                            &post_stats);
                        post_route_pair_moves += rp_moved;
                        round_moves += rp_moved;
                        ++post_route_pair_rounds;
                    }
                    SearchBudget star_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                    round_moves += TryGranularTwoOptStarPass(rl, accept, d, post_candidates, star_budget,
                                                             post_rng, 8, 4096, &post_stats,
                                                             params.granular_endpoint_bias_depth);
                    if (!post_budget.ForceCheck()) {
                        SearchBudget or_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                        round_moves += TryGranularOrOptPass(rl, accept, d, post_candidates, or_budget,
                                                            post_rng, 6, 4096, params.route_cap, 3, &post_stats,
                                                            params.granular_endpoint_bias_depth);
                    }
                    if (!post_budget.ForceCheck()) {
                        SearchBudget rel_budget = post_budget.SubBudget(std::max(200, post_budget.RemainingMs() / 3));
                        round_moves += TryGranularInterRoutePass(rl, accept, d, post_candidates, rel_budget,
                                                                 post_rng, 12, 8192, params.route_cap, &post_stats,
                                                                 params.granular_endpoint_bias_depth);
                    }

                    // Intra-route Or-opt rescue: when the inter-route granular passes
                    // converge in this round (round_moves == 0), the existing loop
                    // would break and waste the rest of post_budget. ParallelFinal2Opt
                    // at the start of post-polish handled 2-opt only; on big routes
                    // (size > 4500) it skipped Exhaustive2Opt entirely, leaving
                    // residual short-segment improvements that 2-opt edge swaps
                    // structurally cannot reach. Run intra Or-opt (lengths 1..3,
                    // strict-improve, parallel-per-route). If anything moved, those
                    // intra changes can in turn unlock cross-route improvements,
                    // so we set round_moves > 0 to keep the outer loop alive.
                    if (rebalance_post_intra_oropt_ &&
                        round_moves == 0 && !post_budget.ForceCheck()) {
                        // Same fraction-of-post-budget shape as the upfront
                        // pass (default 25%, configurable). Each rescue call
                        // is sized off the *remaining* post-budget, so as
                        // post_budget shrinks the rescue cap shrinks with it.
                        const int intra_cap_share =
                            std::max(1, rebalance_post_intra_oropt_share_);
                        const int intra_cap =
                            std::max(200, post_budget.RemainingMs() / intra_cap_share);
                        SearchBudget intra_budget = post_budget.SubBudget(intra_cap);
                        RouteSet intra_set;
                        rl.StoreTo(intra_set);
                        const double pre_intra_cost = RouteSumLength(intra_set, d);
                        const int improved_routes = ParallelFinalIntraOrOpt(
                            intra_set, post_candidates, post_raw_dist,
                            intra_budget, n);
                        ++post_intra_oropt_calls;
                        post_intra_oropt_route_improvements += improved_routes;
                        if (improved_routes > 0) {
                            for (int r = 0; r < rl.RouteCount(); ++r) {
                                rl.ReplaceRoute(r, intra_set[static_cast<size_t>(r)], d);
                            }
                            const double post_intra_cost = RouteSumLength(intra_set, d);
                            if (post_intra_cost + kEps < pre_intra_cost) {
                                ++post_intra_oropt_rescues;
                                round_moves += improved_routes;
                            }
                        }
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
                meta.SetInt("rebalance_post_route_pair_calls", post_stats.route_pair_calls);
                meta.SetInt("rebalance_post_route_pair_pairs", post_stats.route_pair_pairs);
                meta.SetInt("rebalance_post_route_pair_accepts", post_stats.route_pair_2optstar_accepts);
                meta.SetInt("rebalance_post_route_pair_rounds", post_route_pair_rounds);
                meta.SetInt("rebalance_post_route_pair_max_rounds", post_route_pair_max_rounds);
                meta.SetInt("rebalance_post_route_pair_moves", post_route_pair_moves);
                meta.SetInt("rebalance_post_intra_oropt_calls", post_intra_oropt_calls);
                meta.SetInt("rebalance_post_intra_oropt_route_improvements",
                            post_intra_oropt_route_improvements);
                meta.SetInt("rebalance_post_intra_oropt_rescues", post_intra_oropt_rescues);
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
    int single_route_start_variants_override_ = -1;
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
    int granular_endpoint_bias_depth_override_ = -1;
    int granular_2optstar_every_override_ = -1;
    int granular_2optstar_max_moves_override_ = -1;
    int granular_2optstar_scan_customers_override_ = -1;
    int granular_oropt_every_override_ = -1;
    int granular_oropt_max_moves_override_ = -1;
    int granular_oropt_scan_customers_override_ = -1;
    int granular_oropt_max_len_override_ = -1;
    int route_candidate_augmentation_override_ = -1;
    int route_candidate_endpoint_depth_override_ = -1;
    int route_candidate_expensive_edges_override_ = -1;
    int route_candidate_knn_probe_override_ = -1;
    int route_candidate_per_anchor_override_ = -1;
    int route_candidate_max_extra_override_ = -1;
    int region_reopt_every_override_ = -1;
    int region_reopt_k_override_ = -1;
    int pair_reopt_every_override_ = -1;
    int popmusic_every_override_ = -1;
    int popmusic_k_override_ = -1;
    int pre_final_rebalance_override_ = -1;
    int valid_rebalance_tracking_override_ = -1;
    int valid_rebalance_track_every_override_ = -1;
    int valid_rebalance_track_max_override_ = -1;
    bool rebalance_empty_routes_ = true;
    int rebalance_post_ms_ = 30000;
    bool rebalance_post_polish_ = true;
    bool rebalance_compensate_search_time_ = true;
    bool rebalance_post_intra_oropt_ = true;
    int rebalance_post_intra_oropt_share_ = 4;  // cap = post_budget / share.
    int route_pair_2optstar_every_override_ = -1;
    int route_pair_2optstar_max_moves_override_ = -1;
    int route_pair_2optstar_k_override_ = -1;
    int route_pair_2optstar_window_override_ = -1;
    int route_pair_2optstar_max_pairs_override_ = -1;
    int route_pair_post_rounds_override_ = -1;
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
