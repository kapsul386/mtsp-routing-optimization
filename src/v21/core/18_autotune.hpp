#pragma once

#include <algorithm>
#include <cmath>

namespace mtsp::v21 {

struct AutoTuneParams {
    int k_NN = 20;
    int K_destroy_init = 30;
    int K_destroy_max = 200;
    double T_frac_init = 0.05;
    double sa_cooling = 0.995;
    int reheat_after = 200;
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
    // Min-max specific
    int pt_replicas = 1;
    double minmax_lambda = 1e-3;
    double minmax_soft_alpha_init = 0.05;
    double minmax_soft_alpha_final = 0.001;
    bool use_savings_seed = true;
    bool use_kmeans_seed = false;
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
    } else {
        p.k_NN = 18;
        p.K_destroy_init = 150;
        p.K_destroy_max = 2500;
        p.T_frac_init = 0.03;
        p.sa_cooling = 0.99;
        p.reheat_after = 120;
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
    }

    // m-dependent tweaks
    if (m >= 50) {
        p.k_NN = std::max(14, p.k_NN - 2);
    }
    if (m >= 100) {
        p.k_NN = std::max(12, p.k_NN - 2);
        p.ils_rounds = std::max(2, p.ils_rounds - 1);
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
