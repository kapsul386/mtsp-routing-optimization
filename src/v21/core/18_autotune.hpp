#pragma once

// Deterministic parameter dispatcher: ResolveParamsForInstance(n, m, is_minmax)
// returns a fixed AutoTuneParams struct based on instance size brackets and
// objective. Single source of truth for k_NN, destroy sizes, SA cooling,
// reheat thresholds, budget split between phases, PT replica count, and
// related knobs. Numbers are empirically tuned (see comments inline) and
// should change rarely; document any edit with a benchmark reference.

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mtsp::v21 {

// All tunable knobs for one pipeline run. Populated entirely by
// ResolveParamsForInstance; fields are grouped by concern (k-NN, destroy,
// SA cooling, phase budgets, PT replica count, granular operators, etc.).
// Solvers that need non-default behaviour (e.g. capacity cap) set individual
// fields after the call to ResolveParamsForInstance.
struct AutoTuneParams {
    int k_NN = 20;
    int K_destroy_init = 30;
    int K_destroy_max = 200;
    double T_frac_init = 0.05;
    double sa_cooling = 0.995;
    int reheat_after = 200;
    int reheat_after_ms = 0;  // 0 = iter-count-only mode; set >0 for time-based fallback
    int popmusic_solutions = 2;
    int ils_rounds = 4;
    int num_threads = 4;
    int seed_strategies = 4;  // how many seeds to try
    // Budget split (sum to 100): seed, candidates, polish, alns, final
    int budget_seed_pct = 4;
    int budget_cand_pct = 8;
    int budget_polish_pct = 25;
    int budget_alns_pct = 53;
    int budget_final_pct = 10;
    // Extra wall-clock allowance assigned directly to the main ALNS/improvement
    // phase. Used for fair ablations when a solver adds a bounded post-process
    // after the pipeline but we want the main improvement phase to keep its
    // original useful time.
    int extra_alns_ms = 0;
    // Min-max specific
    int pt_replicas = 1;
    double minmax_lambda = 1e-3;
    double minmax_soft_alpha_init = 0.05;
    double minmax_soft_alpha_final = 0.001;
    bool use_classic_seeds = true;
    bool use_savings_seed = true;
    bool use_kmeans_seed = false;
    // Depot-candidate first-step seeds for MINSUM.
    // Bitmask: 1 = m depot candidates, 2 = 2m depot candidates, 3 = both.
    // Uniform 100k/m10: 2m alone was better than m and the m+2m portfolio.
    int depot_seed_mode = 2;
    double depot_seed_spread_prob = 0.5;
    // Optional portfolio for 2m depot seeds. Empty = use depot_seed_spread_prob once.
    std::vector<double> depot_seed_spread_probs;
    int depot_seed_restarts = 1;
    // Extra fixed depot rings: ring=2 uses the 3rd m-nearest depot band, etc.
    std::vector<int> depot_seed_rings;
    // Metric-MINSUM exploratory seed: one fast TSP-like route plus empty routes.
    bool use_single_route_seed = false;
    double single_route_seed_min_gain = 0.0;
    // If true, accepted single-route seeds are split into valid non-empty
    // routes before entering the main search. The raw single-route cost still
    // controls the gain gate so uniform instances can reject it.
    bool single_route_rebalance_seed = false;
    // Number of alternate non-depot starts for the accepted single-route seed.
    // Each alternate chain is smart-rebalanced and only the best valid split is
    // kept, so this changes seed quality without adding a separate search phase.
    int single_route_start_variants = 0;
    // If true, split empty-route MINSUM solutions before phase 6 so the final
    // intra-route polish optimizes the valid mTSP routes, not the raw
    // empty-allowed snapshot.
    bool pre_final_rebalance = false;
    // Bounded valid-aware ALNS tracking: periodically copy the current ALNS
    // state, smart-split it, and keep the best valid snapshot by MINSUM. This
    // guards against raw empty-allowed best being a poor valid solution.
    bool valid_rebalance_tracking = false;
    int valid_rebalance_track_every = 25;
    int valid_rebalance_track_max = 8;
    // Angular-sector depot seeds: agents start from different angular sectors,
    // then continue with the same kNN round-robin fill. Rotations are evenly
    // spaced within one sector; quantiles choose near/inner-sector anchors.
    int angular_seed_rotations = 0;
    int angular_seed_pool_multiplier = 8;
    std::vector<double> angular_seed_quantiles;
    // Optional pre-selection race: top constructive seeds get a short
    // granular-only run, then the best post-race seed enters the full search.
    int seed_race_count = 0;
    int seed_race_ms = 0;
    // Extra wall-clock allowance for the seed race. If this is lower than
    // seed_race_ms, the remaining race time still comes from the main budget.
    int seed_race_extra_ms = 0;
    // false = race only ranks seeds, then the full search starts from the
    // original winning seed; true = continue from the locally improved race result.
    bool seed_race_start_from_raced = false;
    // Periodic route-pair re-optimization frequency (0 = disabled).
    // On very large n, the per-call cost (~6-10s) outweighs the gain since
    // routes are huge — keep it off.
    int pair_reopt_every = 200;
    // GLS penalty trigger: penalize after N iters without a real-best update.
    int gls_penalize_after = 50;
    // POPMUSIC spatial decomposition step frequency (0 = disabled).
    // Useful on n>60k where PT and pair-reopt are off and ALNS makes few
    // iterations per second — POPMUSIC concentrates effort on a spatial
    // neighbourhood of K_pop customers and re-stitches across routes.
    int popmusic_every = 0;
    int popmusic_K = 2500;
    // FILO2-like granular inter-route pass (candidate-list relocate + swap).
    // Strict-improve only; intended as a low-overhead large-n intensifier.
    int granular_every = 0;
    int granular_max_moves = 0;
    int granular_scan_customers = 0;
    int granular_endpoint_bias_depth = 0;
    // Experimental FILO2/LKH-style inter-route 2-opt* tail swaps.
    int granular_2optstar_every = 0;
    int granular_2optstar_max_moves = 0;
    int granular_2optstar_scan_customers = 0;
    // Experimental short inter-route Or-opt segments (len 2..max_len).
    int granular_oropt_every = 0;
    int granular_oropt_max_moves = 0;
    int granular_oropt_scan_customers = 0;
    int granular_oropt_max_len = 3;
    // Route-aware candidate augmentation for granular operators. Adds a small
    // number of cross-route bridge candidates around route endpoints and
    // expensive route edges after a seed/rebalance snapshot is known.
    bool route_candidate_augmentation = false;
    int route_candidate_endpoint_depth = 3;
    int route_candidate_expensive_edges_per_route = 32;
    int route_candidate_knn_probe = 96;
    int route_candidate_per_anchor = 4;
    int route_candidate_max_extra_per_node = 6;
    // DualOpt-style spatial region re-optimization. Strict-improve only.
    int region_reopt_every = 0;
    int region_reopt_K = 0;
    // Endpoint-focused route-pair 2-opt* pass. Builds a small per-route
    // shortlist of nearest-by-endpoint neighbour routes and tries 2-opt*
    // cuts inside an endpoint window on both routes. Strict-improve only.
    // The shortlist is local to the pass — the global candidate graph is not
    // modified, so this is independent of route_candidate_augmentation.
    // 0 = disabled.
    int route_pair_2optstar_every = 0;
    int route_pair_2optstar_max_moves = 0;
    int route_pair_2optstar_k = 2;
    int route_pair_2optstar_window = 4;
    int route_pair_2optstar_max_pairs = 0;
    // FILO2-inspired capacity cap for high-m MINSUM stabilization.
    // 0 = disabled (legacy MINSUM/MINMAX). >0 = max customers per route.
    // Set externally by `lkh_v21_minsum_cap` solver from instance (n,m);
    // ResolveParamsForInstance does NOT auto-set this — only the cap solver
    // populates it.
    int route_cap = 0;
};

// Resolve all knobs from (n, m) — single source of truth.
inline AutoTuneParams ResolveParamsForInstance(int n, int m, bool is_minmax) {
    AutoTuneParams p;
    // Threading
    int hw = 4;
#ifdef _OPENMP
    hw = std::max(1, omp_get_max_threads());
#endif
    if (n <= 10000) p.num_threads = std::min(hw, 8);
    else p.num_threads = hw;

    // Size brackets
    if (n <= 2000) {
        p.k_NN = 26;
        p.K_destroy_init = 8;
        p.K_destroy_max = 50;
        p.T_frac_init = 0.08;
        p.sa_cooling = 0.997;
        p.reheat_after = 250;
        p.popmusic_solutions = 4;
        p.ils_rounds = 6;
        p.budget_seed_pct = 5;
        p.budget_cand_pct = 5;
        p.budget_polish_pct = 30;
        p.budget_alns_pct = 45;
        p.budget_final_pct = 15;
        p.use_savings_seed = true;
        p.granular_every = 60;
        p.granular_max_moves = 4;
        p.granular_scan_customers = 96;
        p.region_reopt_every = 180;
        p.region_reopt_K = 250;
    } else if (n <= 12000) {
        p.k_NN = 22;
        p.K_destroy_init = 25;
        p.K_destroy_max = 250;
        p.T_frac_init = 0.06;
        p.sa_cooling = 0.995;
        p.reheat_after = 200;
        p.popmusic_solutions = 3;
        p.ils_rounds = 5;
        p.budget_seed_pct = 4;
        p.budget_cand_pct = 8;
        p.budget_polish_pct = 28;
        p.budget_alns_pct = 50;
        p.budget_final_pct = 10;
        p.use_savings_seed = (n <= 8000);
        p.granular_every = 50;
        p.granular_max_moves = 4;
        p.granular_scan_customers = 192;
        p.region_reopt_every = 160;
        p.region_reopt_K = 500;
    } else if (n <= 60000) {
        p.k_NN = 20;
        p.K_destroy_init = 80;
        p.K_destroy_max = 1200;
        p.T_frac_init = 0.04;
        p.sa_cooling = 0.992;
        p.reheat_after = 150;
        p.popmusic_solutions = 2;
        p.ils_rounds = 3;
        p.budget_seed_pct = 3;
        p.budget_cand_pct = 8;
        p.budget_polish_pct = 22;
        p.budget_alns_pct = 57;
        p.budget_final_pct = 10;
        p.use_savings_seed = false;
        p.granular_every = 30;
        p.granular_max_moves = 5;
        p.granular_scan_customers = 512;
        p.region_reopt_every = 100;
        p.region_reopt_K = 1000;
        // Cross-route operators that specifically uncross route geometries.
        // On uniform-with-low-m instances (e.g. n=25k m=5, ~5000 cities/route)
        // the visible failure mode is route boundaries that interleave in the
        // central region. Single-customer relocate alone climbs out of those
        // tangles slowly. 2-opt* swaps tails of two routes at candidate-
        // adjacent edges — the direct geometric uncrossing operator. Or-opt
        // moves length-2/3 blocks across routes — catches improvements that
        // single relocate needs several lucky SA accepts to make. The
        // endpoint-focused route-pair pass targets the depot-stitch / long-
        // jumper pattern where two routes share a long bridge through the
        // depot region.
        p.granular_2optstar_every = 60;
        p.granular_2optstar_max_moves = 3;
        p.granular_2optstar_scan_customers = 384;
        p.granular_oropt_every = 80;
        p.granular_oropt_max_moves = 2;
        p.granular_oropt_scan_customers = 384;
        p.granular_oropt_max_len = 3;
        p.route_pair_2optstar_every = 250;
        p.route_pair_2optstar_max_moves = 2;
        p.route_pair_2optstar_k = 2;
        p.route_pair_2optstar_window = 6;
        // Bias granular sampling toward route head/tail customers — that is
        // where most of the depot-bridge / endpoint-stitch improvements live
        // and where uniform sampling under-visits.
        p.granular_endpoint_bias_depth = 8;
    } else {
        p.k_NN = 18;
        p.K_destroy_init = 150;
        p.K_destroy_max = 2500;
        p.T_frac_init = 0.03;
        p.sa_cooling = 0.99;
        // Day-2 finding: on n>60k the per-iter cost is so high (~0.5–1 iter/s)
        // that the iter-count threshold rarely accumulates within the wall-time
        // budget (variance audit on uniform_n100000_m5 showed sa_reheats=0
        // across all 10 seeds, even though 7/10 had plateau gaps of 70–204s).
        // Lowering the iter-count threshold to 40 in B v1 still produced 0
        // reheats because best/iter ~55% kept resetting the streak.
        // Solution: keep iter-count threshold at the original 120 for
        // backwards compat, AND add a 30s wall-time threshold that fires
        // reliably on plateau-prone seeds (gaps 70-200s). Healthy seeds with
        // best every 5-10s never accumulate 30s of stagnation.
        p.reheat_after = 120;
        p.reheat_after_ms = 30000;
        p.popmusic_solutions = 0;
        p.ils_rounds = 2;
        p.budget_seed_pct = 3;
        p.budget_cand_pct = 6;
        p.budget_polish_pct = 16;
        p.budget_alns_pct = 65;
        p.budget_final_pct = 10;
        p.use_savings_seed = false;
        // Pair re-opt is too expensive on n>60k — routes have ~10-20k cities.
        p.pair_reopt_every = 0;
        // POPMUSIC zone decomposition step is implemented (core/21_popmusic.hpp)
        // but disabled by default — empirical testing on uniform_n100000_m5
        // showed 0 accepts because the post-polish solution is already at a
        // local optimum that cheapest-insertion + bounded 2-opt cannot beat
        // on routes ~20k long. Set popmusic_every>0 manually to experiment
        // (e.g. with pre-shaken instances or very different distributions).
        p.popmusic_every = 0;
        p.popmusic_K = 2000;
        // New v21 large-n intensifiers. The granular pass is cheap because it
        // pre-indexes positions once and then evaluates only candidate-list
        // neighbours. Region reopt is more aggressive than the first safe
        // hook, but throttled: it tries several centres and loosens route
        // boundaries without starving the granular relocate loop.
        p.granular_every = 8;
        p.granular_max_moves = 6;
        p.granular_scan_customers = 1024;
        p.region_reopt_every = 24;
        p.region_reopt_K = 1800;
        // Cross-route operators for n>60k. Lower scan/move budgets than the
        // 25k bracket because per-iter cost is already high and single-route
        // length is huge; cap moves so the operators stay strict-improve and
        // do not displace the granular relocate loop. route-pair pass is the
        // cheapest of the three (no kNN scan, just endpoint-distance pairs)
        // so it can fire more often.
        p.granular_2optstar_every = 32;
        p.granular_2optstar_max_moves = 2;
        p.granular_2optstar_scan_customers = 768;
        p.granular_oropt_every = 40;
        p.granular_oropt_max_moves = 2;
        p.granular_oropt_scan_customers = 768;
        p.granular_oropt_max_len = 3;
        p.route_pair_2optstar_every = 120;
        p.route_pair_2optstar_max_moves = 2;
        p.route_pair_2optstar_k = 2;
        p.route_pair_2optstar_window = 6;
        p.granular_endpoint_bias_depth = 10;
    }

    // m-dependent tweaks
    if (m >= 50) {
        p.k_NN = std::max(14, p.k_NN - 2);
        p.granular_scan_customers = std::max(64, p.granular_scan_customers / 2);
        p.granular_2optstar_scan_customers = std::max(64, p.granular_2optstar_scan_customers / 2);
        p.granular_oropt_scan_customers = std::max(64, p.granular_oropt_scan_customers / 2);
        p.region_reopt_K = std::max(200, p.region_reopt_K / 2);
    }
    if (m >= 100) {
        p.k_NN = std::max(12, p.k_NN - 2);
        p.ils_rounds = std::max(2, p.ils_rounds - 1);
        p.granular_max_moves = std::max(2, p.granular_max_moves - 1);
        p.granular_scan_customers = std::max(64, p.granular_scan_customers / 2);
        p.granular_2optstar_scan_customers = std::max(64, p.granular_2optstar_scan_customers / 2);
        p.granular_oropt_scan_customers = std::max(64, p.granular_oropt_scan_customers / 2);
        p.region_reopt_K = std::max(200, p.region_reopt_K / 2);
    }

    // Min-max specifics
    if (is_minmax) {
        p.minmax_lambda = 1.0 / std::max(1, m * 1000);
        p.use_kmeans_seed = true;
        if (n <= 2000) p.pt_replicas = 1;
        else if (n <= 12000) p.pt_replicas = 4;
        else if (n <= 60000) p.pt_replicas = 6;
        else p.pt_replicas = 8;
    } else {
        // MINSUM: PT helps mostly on medium instances where single-replica gets
        // stuck on plateaus. On very large n (>60k), per-step cost is so high
        // that 6 replicas amortize iterations badly — single-replica wins.
        if (n <= 12000) p.pt_replicas = 1;
        else if (n <= 60000) p.pt_replicas = 4;
        else p.pt_replicas = 1;  // single-replica beats PT at 100k (verified)
    }
    // Cap by available hardware threads (PT spawns one OMP thread per replica
    // during its phase). Leave at least 1 thread for residual work.
    p.pt_replicas = std::max(1, std::min(p.pt_replicas, p.num_threads));

    return p;
}

}  // namespace mtsp::v21
