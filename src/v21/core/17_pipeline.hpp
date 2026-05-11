#pragma once

// Top-level pipeline driving the v21 search. Templated on AcceptPolicy so the
// same code services both `MinsumAccept` and `MinmaxAccept`. RunPipeline
// orchestrates six phases under one wall-clock SearchBudget:
//
//   1. Candidate set construction (k-NN via KDTree2D, optionally augmented by
//      POPMUSIC-lite tour edges). Built once.
//   2. Multi-seed portfolio (round-robin NN, fast NN, polar sweep, optionally
//      k-means and savings); PickBestSeed picks the lowest-cost seed.
//   3. Parallel polish: ParallelFinal2Opt + ParallelPolish (intra-route ILS
//      under OpenMP), one OMP team across routes.
//   4. (skipped — phase numbering predates this file; phases 3 & 4 are merged.)
//   5. Main search: RunAlnsSaLoop in single-replica mode, or RunPtAlnsSaLoop
//      with K replicas on a geometric T ladder when autotune enables PT. The
//      ALNS body alternates destroy/repair/selective-LS and consults
//      SaEngine for non-improving acceptance + reheat decisions.
//   6. Final polish: ParallelFinal2Opt over the best solution; the budget
//      reserve guarantees this phase actually runs.
//
// PipelineMetadata accumulates phase timings, operator stats, anytime trace,
// and post-run scalars (final_minsum / final_max). All metadata is
// string-typed so the C-side mtsp_main can hand it to JSON unmodified.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include "04_route_index.hpp"
#include "05_route_list.hpp"
#include "06_candidate_set.hpp"
#include "07_seed_routes.hpp"
#include "08_route_local_search.hpp"
#include "09_intra_3opt_light.hpp"
#include "10_inter_route_moves.hpp"
#include "11_validation.hpp"
#include "12_alns_framework.hpp"
#include "13_destroy_ops.hpp"
#include "14_repair_ops.hpp"
#include "15_sa_engine.hpp"
#include "18_autotune.hpp"
#include "19_gls.hpp"
#include "20_route_pair_reopt.hpp"
#include "21_popmusic.hpp"
#include "22_region_granular_ops.hpp"

#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mtsp::v21 {

struct PipelineMetadata {
    std::unordered_map<std::string, std::string> data;

    // Anytime trace: monotone non-increasing (elapsed_ms, best_cost) samples
    // recorded at every best-update event. Day-1 reads this to spot plateau
    // onset on n=100k vs continuous-progress on smaller instances.
    std::vector<std::pair<long long, double>> anytime_trace;
    std::chrono::steady_clock::time_point anytime_t0{};
    bool anytime_started = false;

    // Store a string-typed key-value pair in the metadata map.
    void Set(const std::string& k, const std::string& v) { data[k] = v; }
    // Store an integer as a string-typed metadata entry.
    void SetInt(const std::string& k, long long v) { data[k] = std::to_string(v); }
    // Store a floating-point value as a string-typed metadata entry.
    void SetDouble(const std::string& k, double v) { data[k] = std::to_string(v); }

    // Begin the anytime trace clock at the current wall time. Must be called
    // before the first EmitAnytimeBest to record meaningful timestamps.
    void StartAnytime() {
        anytime_t0 = std::chrono::steady_clock::now();
        anytime_trace.clear();
        anytime_started = true;
    }

    // Record a new best-cost sample in the anytime trace if it strictly
    // improves on the previous sample. No-ops when tracing has not started.
    void EmitAnytimeBest(double cost) {
        if (!anytime_started) return;
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - anytime_t0).count();
        if (!anytime_trace.empty() && cost >= anytime_trace.back().second - 1e-9) return;
        anytime_trace.emplace_back(ms, cost);
    }

    // Serialize trace as a JSON array literal under key "anytime_trace".
    // Downstream Python parses with json.loads(meta["anytime_trace"]).
    void FlushAnytimeToData() {
        std::string out;
        out.reserve(16 + anytime_trace.size() * 24);
        out += "[";
        for (size_t i = 0; i < anytime_trace.size(); ++i) {
            if (i > 0) out += ",";
            out += "[";
            out += std::to_string(anytime_trace[i].first);
            out += ",";
            out += std::to_string(anytime_trace[i].second);
            out += "]";
        }
        out += "]";
        data["anytime_trace"] = std::move(out);
        SetInt("anytime_trace_points", static_cast<long long>(anytime_trace.size()));
    }
};

// Returns elapsed ms from a steady_clock time_point.
inline long long ElapsedMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

// Pick the best of multiple seeds by AcceptPolicy::ScalarCost.
template <typename AcceptPolicy>
RouteSet PickBestSeed(std::vector<RouteSet>& seeds, DistanceOracle& d, AcceptPolicy& accept) {
    RouteSet best;
    double best_cost = std::numeric_limits<double>::max();
    for (auto& s : seeds) {
        if (s.empty()) continue;
        const double c = accept.ScalarCostOfRoutes(s, d);
        if (c < best_cost) { best_cost = c; best = s; }
    }
    return best;
}

// Pick a constructive seed, optionally after a short granular-only seed race.
template <typename AcceptPolicy>
RouteSet PickBestSeedWithGranularRace(std::vector<RouteSet>& seeds,
                                      DistanceOracle& d,
                                      AcceptPolicy& accept,
                                      const CandidateSets& candidates,
                                      SearchBudget& budget,
                                      unsigned base_seed,
                                      const AutoTuneParams& params,
                                      int n_total,
                                      int m_total,
                                      PipelineMetadata& meta) {
    struct RankedSeed {
        double cost = std::numeric_limits<double>::max();
        int idx = -1;
    };

    std::vector<RankedSeed> ranked;
    ranked.reserve(seeds.size());
    for (int i = 0; i < static_cast<int>(seeds.size()); ++i) {
        if (seeds[static_cast<size_t>(i)].empty()) continue;
        ranked.push_back({accept.ScalarCostOfRoutes(seeds[static_cast<size_t>(i)], d), i});
    }
    if (ranked.empty()) return {};
    std::sort(ranked.begin(), ranked.end(), [](const RankedSeed& a, const RankedSeed& b) {
        return a.cost < b.cost;
    });

    meta.SetDouble("seed_construct_best_cost", ranked.front().cost);
    if (params.seed_race_count <= 1 || params.seed_race_ms <= 0 ||
        static_cast<int>(ranked.size()) <= 1) {
        meta.SetInt("seed_race_contenders", 0);
        meta.SetInt("seed_chosen_index", ranked.front().idx);
        return seeds[static_cast<size_t>(ranked.front().idx)];
    }

    const int contenders = std::min<int>(params.seed_race_count, static_cast<int>(ranked.size()));
    SearchBudget race_budget = budget.SubBudget(params.seed_race_ms);
    RouteSet best_routes = seeds[static_cast<size_t>(ranked.front().idx)];
    double best_cost = ranked.front().cost;
    int best_rank = 0;
    int best_seed_idx = ranked.front().idx;
    int total_relocate_moves = 0;
    int total_two_optstar_moves = 0;
    int total_oropt_moves = 0;
    const int race_granular_max_moves = std::min(params.granular_max_moves, 4);
    const int race_granular_scan = std::min(params.granular_scan_customers, 512);
    const int race_two_optstar_max_moves = std::min(params.granular_2optstar_max_moves, 1);
    const int race_two_optstar_scan = std::min(params.granular_2optstar_scan_customers, 256);
    const int race_oropt_max_moves = std::min(params.granular_oropt_max_moves, 1);
    const int race_oropt_scan = std::min(params.granular_oropt_scan_customers, 256);
    DistanceOracle race_d(d.GetInstance());

    for (int rank = 0; rank < contenders && !race_budget.ForceCheck(); ++rank) {
        const int remaining = std::max(1, contenders - rank);
        SearchBudget one_budget = race_budget.SubBudget(std::max(100, race_budget.RemainingMs() / remaining));
        std::seed_seq race_seq{
            static_cast<uint32_t>(base_seed),
            static_cast<uint32_t>(0xA511E9B3u),
            static_cast<uint32_t>(rank * 1009 + 17),
            static_cast<uint32_t>(ranked[static_cast<size_t>(rank)].idx * 9176 + 31),
            static_cast<uint32_t>(n_total),
            static_cast<uint32_t>(m_total)
        };
        std::mt19937 race_rng(race_seq);

        RouteList rl(n_total, m_total);
        rl.LoadFrom(seeds[static_cast<size_t>(ranked[static_cast<size_t>(rank)].idx)], race_d);
        GranularMoveStats stats;
        int local_relocate = 0;
        int local_two_optstar = 0;
        int local_oropt = 0;

        while (!one_budget.ForceCheck()) {
            bool moved_any = false;
            if (race_granular_max_moves > 0 && race_granular_scan > 0) {
                SearchBudget pass_budget = one_budget.SubBudget(std::max(50, one_budget.RemainingMs() / 8));
                const int moved = TryGranularInterRoutePass(rl, accept, race_d, candidates, pass_budget,
                                                            race_rng, race_granular_max_moves,
                                                            race_granular_scan,
                                                            params.route_cap, &stats,
                                                            params.granular_endpoint_bias_depth);
                local_relocate += moved;
                moved_any = moved_any || moved > 0;
            }
            if (race_two_optstar_max_moves > 0 && race_two_optstar_scan > 0) {
                SearchBudget pass_budget = one_budget.SubBudget(std::max(50, one_budget.RemainingMs() / 8));
                const int moved = TryGranularTwoOptStarPass(rl, accept, race_d, candidates, pass_budget,
                                                            race_rng, race_two_optstar_max_moves,
                                                            race_two_optstar_scan,
                                                            &stats,
                                                            params.granular_endpoint_bias_depth);
                local_two_optstar += moved;
                moved_any = moved_any || moved > 0;
            }
            if (race_oropt_max_moves > 0 && race_oropt_scan > 0) {
                SearchBudget pass_budget = one_budget.SubBudget(std::max(50, one_budget.RemainingMs() / 8));
                const int moved = TryGranularOrOptPass(rl, accept, race_d, candidates, pass_budget,
                                                       race_rng, race_oropt_max_moves,
                                                       race_oropt_scan,
                                                       params.route_cap,
                                                       params.granular_oropt_max_len,
                                                       &stats,
                                                       params.granular_endpoint_bias_depth);
                local_oropt += moved;
                moved_any = moved_any || moved > 0;
            }
            if (!moved_any) break;
        }

        RouteSet raced;
        rl.StoreTo(raced);
        EnsureClosedDepot(raced);
        const double race_cost = accept.ScalarCostOfRoutes(raced, race_d);
        total_relocate_moves += local_relocate;
        total_two_optstar_moves += local_two_optstar;
        total_oropt_moves += local_oropt;
        meta.SetDouble("seed_race_rank" + std::to_string(rank) + "_initial", ranked[static_cast<size_t>(rank)].cost);
        meta.SetDouble("seed_race_rank" + std::to_string(rank) + "_cost", race_cost);
        meta.SetInt("seed_race_rank" + std::to_string(rank) + "_seed_index",
                    ranked[static_cast<size_t>(rank)].idx);
        meta.SetInt("seed_race_rank" + std::to_string(rank) + "_moves", local_relocate);
        meta.SetInt("seed_race_rank" + std::to_string(rank) + "_2optstar", local_two_optstar);
        meta.SetInt("seed_race_rank" + std::to_string(rank) + "_oropt", local_oropt);

        if (race_cost < best_cost - kEps) {
            best_cost = race_cost;
            best_routes = std::move(raced);
            best_rank = rank;
            best_seed_idx = ranked[static_cast<size_t>(rank)].idx;
        }
    }

    meta.SetInt("seed_race_contenders", contenders);
    meta.SetInt("seed_race_budget_ms", params.seed_race_ms);
    meta.SetInt("seed_race_start_from_raced", params.seed_race_start_from_raced ? 1 : 0);
    meta.SetInt("seed_race_granular_scan", race_granular_scan);
    meta.SetInt("seed_race_2optstar_scan", race_two_optstar_scan);
    meta.SetInt("seed_race_oropt_scan", race_oropt_scan);
    meta.SetDouble("seed_race_best_cost", best_cost);
    meta.SetInt("seed_race_best_rank", best_rank);
    meta.SetInt("seed_race_best_seed_index", best_seed_idx);
    meta.SetInt("seed_chosen_index", best_seed_idx);
    meta.SetInt("seed_race_moves", total_relocate_moves);
    meta.SetInt("seed_race_2optstar_moves", total_two_optstar_moves);
    meta.SetInt("seed_race_oropt_moves", total_oropt_moves);
    if (!params.seed_race_start_from_raced) {
        return seeds[static_cast<size_t>(best_seed_idx)];
    }
    return best_routes;
}

// Stateless distance functor — safe to share across OpenMP threads (no cache).
struct RawDistFn {
    const mtsp::Instance& inst;
    double operator()(int a, int b) const { return inst.Distance(a, b); }
};

// Run intra-route ILS in parallel over routes.
template <typename DistanceFn>
void ParallelPolish(RouteSet& routes, const CandidateSets& candidates, DistanceFn& dist,
                    SearchBudget& budget, int rounds, std::mt19937& rng_seed_source,
                    int n_total) {
#ifdef _OPENMP
    #pragma omp parallel
    {
        // Each thread gets its own RNG and RouteIndex
        std::mt19937 local_rng(static_cast<unsigned>(rng_seed_source() + omp_get_thread_num() * 7919u));
        RouteIndex local_idx(n_total);
        #pragma omp for schedule(dynamic)
        for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
            if (budget.ForceCheck()) continue;
            IteratedLocalSearchSingleRoute(routes[static_cast<size_t>(r)], candidates, dist, budget, local_idx, local_rng, rounds);
        }
    }
#else
    RouteIndex idx(n_total);
    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
        if (budget.ForceCheck()) break;
        IteratedLocalSearchSingleRoute(routes[static_cast<size_t>(r)], candidates, dist, budget, idx, rng_seed_source, rounds);
    }
#endif
}

// Run cheap candidate-list 2-opt + bounded exhaustive 2-opt on all routes (parallel).
// Order: NeighborList2Opt first (cheap, O(L*k) per pass), then Exhaustive2Opt
// always (until convergence or budget exhausted). Exhaustive on large routes
// is expensive but the only way to reach a true 2-opt local optimum, which
// is the single biggest quality lever for the seeded routes.
template <typename DistanceFn>
void ParallelFinal2Opt(RouteSet& routes, const CandidateSets& candidates, DistanceFn& dist,
                       SearchBudget& budget, int n_total) {
#ifdef _OPENMP
    #pragma omp parallel
    {
        RouteIndex local_idx(n_total);
        #pragma omp for schedule(dynamic)
        for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
            if (budget.ForceCheck()) continue;
            auto& route = routes[static_cast<size_t>(r)];
            NeighborList2Opt(route, candidates, dist, budget, local_idx);
            if (!budget.ForceCheck()) Exhaustive2Opt(route, dist, budget);
        }
    }
#else
    RouteIndex idx(n_total);
    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
        if (budget.ForceCheck()) break;
        auto& route = routes[static_cast<size_t>(r)];
        NeighborList2Opt(route, candidates, dist, budget, idx);
        if (!budget.ForceCheck()) Exhaustive2Opt(route, dist, budget);
    }
#endif
}

// Run intra-route Or-opt (lengths 1..3) on all routes in parallel. Strict-
// improve only (TryOrOptAllLengths first-improving). Complements
// ParallelFinal2Opt: 2-opt edge swaps and Or-opt segment relocates handle
// disjoint move classes. On big routes (>4500 customers) where Exhaustive2Opt
// is skipped, intra Or-opt frequently finds residual single/short-segment
// improvements that NeighborList2Opt's edge-swap window cannot reach.
//
// Returns the number of routes that improved at least once. Used both as a
// best-effort polish and as a "did anything change?" signal so the caller
// can re-attempt complementary inter-route operators after intra changes.
template <typename DistanceFn>
inline int ParallelFinalIntraOrOpt(RouteSet& routes, const CandidateSets& candidates,
                                   DistanceFn& dist, SearchBudget& budget, int n_total) {
    int improved_routes = 0;
#ifdef _OPENMP
    #pragma omp parallel reduction(+:improved_routes)
    {
        RouteIndex local_idx(n_total);
        #pragma omp for schedule(dynamic)
        for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
            if (budget.ForceCheck()) continue;
            auto& route = routes[static_cast<size_t>(r)];
            if (route.size() <= 6) continue;
            if (TryOrOptAllLengths(route, candidates, dist, budget, local_idx)) {
                improved_routes += 1;
            }
        }
    }
#else
    RouteIndex idx(n_total);
    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
        if (budget.ForceCheck()) break;
        auto& route = routes[static_cast<size_t>(r)];
        if (route.size() <= 6) continue;
        if (TryOrOptAllLengths(route, candidates, dist, budget, idx)) {
            ++improved_routes;
        }
    }
#endif
    return improved_routes;
}

// AcceptPolicy contract:
//   double ScalarCost(const RouteList&)        // current scalar cost (e.g., sum or max+λsum)
//   double ScalarCostOfRoutes(RouteSet, DistanceOracle&)  // standalone evaluation
//   double DeltaForCrossRouteMove(RouteList&, int from, int to, double dL_from, double dL_to)
//   double DeltaForFullRouteset(RouteList& before, RouteList& after)
//   bool   StrictAccept(double delta)          // strict-improvement acceptance
//   bool   SaAccept(double delta, SaEngine&)   // SA acceptance
//
// AcceptPolicy must also expose: bool IsMinMax() const, double Lambda() const.

// Register the standard ALNS destroy/repair mix on an AlnsFramework. The
// mix differs by objective: minmax biases toward CriticalRoute and includes
// BalanceAware repair; minsum has no CriticalRoute.
template <typename AcceptPolicy>
inline void RegisterAlnsOps(AlnsFramework& alns,
                             DestroyContext& dctx,
                             RepairContext& rctx_normal,
                             RepairContext& rctx_balance,
                             AcceptPolicy& accept) {
    if (accept.IsMinMax()) {
        alns.RegisterDestroy("CriticalRoute",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyCriticalRoute(r, rng, K, dctx); }, 4.0);
        alns.RegisterDestroy("ClusterBfs",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyClusterBfs(r, rng, K, dctx); }, 3.0);
        alns.RegisterDestroy("Random",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyRandom(r, rng, K, dctx); }, 1.0);
        alns.RegisterDestroy("Expensive",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyExpensiveEdges(r, rng, K, dctx); }, 1.0);
        alns.RegisterDestroy("Zone",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyZone(r, rng, K, dctx); }, 1.0);
    } else {
        alns.RegisterDestroy("Random",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyRandom(r, rng, K, dctx); }, 3.0);
        alns.RegisterDestroy("ClusterBfs",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyClusterBfs(r, rng, K, dctx); }, 4.0);
        alns.RegisterDestroy("Expensive",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyExpensiveEdges(r, rng, K, dctx); }, 2.0);
        alns.RegisterDestroy("Zone",
            [&dctx](RouteList& r, std::mt19937& rng, int K) { return DestroyZone(r, rng, K, dctx); }, 1.0);
    }
    alns.RegisterRepair("Cheapest",
        [&rctx_normal](RouteList& r, std::vector<int>& removed, std::mt19937& rng) {
            RepairCheapestInsertion(r, removed, rng, rctx_normal);
        },
        accept.IsMinMax() ? 2.0 : 3.0);
    alns.RegisterRepair("Regret2",
        [&rctx_normal](RouteList& r, std::vector<int>& removed, std::mt19937& rng) {
            RepairRegret2Insertion(r, removed, rng, rctx_normal);
        },
        accept.IsMinMax() ? 3.0 : 2.0);
    if (accept.IsMinMax()) {
        alns.RegisterRepair("BalanceAware",
            [&rctx_balance](RouteList& r, std::vector<int>& removed, std::mt19937& rng) {
                RepairCheapestInsertion(r, removed, rng, rctx_balance);
            }, 2.5);
    }
}

// One Parallel Tempering replica: owns its DistanceOracle (so the cache map
// is thread-local and race-free), ALNS, SA, RouteList, RNG, plus tracked stats.
//
// We hold these by std::unique_ptr so std::vector<std::unique_ptr<PtReplicaCtx>>
// gives us stable addresses for the lambdas captured by AlnsFramework.
struct PtReplicaCtx {
    // Initialise all per-replica resources: build a private DistanceOracle for
    // thread-safety, wire up the destroy/repair contexts, and size the RouteList.
    PtReplicaCtx(const mtsp::Instance& inst, const KDTree2D& kdtree,
                 const CandidateSets& global, int n_total, int m_total)
        : oracle(inst),
          dctx{oracle, kdtree, n_total},
          rctx_normal{oracle, global, false},
          rctx_balance{oracle, global, true},
          rl(n_total, m_total) {}

    DistanceOracle oracle;
    DestroyContext dctx;
    RepairContext rctx_normal;
    RepairContext rctx_balance;
    AlnsFramework alns;
    std::unique_ptr<SaEngine> sa;
    EdgePenalties gls;
    RouteList rl;
    std::mt19937 rng;
    RouteSet best_routes;
    double best_cost = std::numeric_limits<double>::max();
    double current_cost = 0.0;  // updated after each step (for PT swap test)
    int iters = 0;
    int accepts = 0;
    int best_updates = 0;
    int iters_since_best = 0;
    int gls_penalize_calls = 0;
};

// Run one ALNS-SA step on a replica. Mirrors the body of RunAlnsSaLoop's main
// loop iteration. Acceptance uses augmented (real + GLS penalty) cost; best
// tracking uses real cost only. Returns true if the replica's real-best
// improved this step.
//
// GLS interacts with stagnation: every `gls_penalize_after` iterations
// without a real-best update, we add penalty to a few "worst" edges and the
// augmented landscape shifts, pushing LS out of the current basin.
template <typename AcceptPolicy>
inline bool DoOneAlnsStep(PtReplicaCtx& rep, AcceptPolicy& accept,
                           const CandidateSets& candidates, SearchBudget& budget,
                           const AutoTuneParams& params, int n_total,
                           int gls_penalize_after = 50,
                           int gls_penalize_K = 3,
                           double gls_decay_on_best = 0.5) {
    const double pre_real = accept.ScalarCost(rep.rl);
    const double pre_pen = rep.gls.TotalPenalty(rep.rl);
    int K = params.K_destroy_init;
    const int streak = rep.sa->NoImproveStreak();
    if (streak > 50) K = std::min(params.K_destroy_max, params.K_destroy_init * (1 + streak / 50));
    K = std::min(K, std::max(2, n_total / 5));

    const int d_idx = rep.alns.PickDestroy(rep.rng);
    const int r_idx = rep.alns.PickRepair(rep.rng);

    RouteSet snap; rep.rl.StoreTo(snap);
    DestroyResult dr = rep.alns.RunDestroy(d_idx, rep.rl, rep.rng, K);
    rep.alns.RunRepair(r_idx, rep.rl, dr.removed, rep.rng);

    {
        RouteIndex local_idx(n_total);
        for (int rr = 0; rr < rep.rl.RouteCount(); ++rr) {
            if (!rep.rl.IsDirty(rr)) continue;
            auto route_copy = rep.rl.Route(rr);
            NeighborList2Opt(route_copy, candidates, rep.oracle, budget, local_idx);
            rep.rl.ReplaceRoute(rr, std::move(route_copy), rep.oracle);
        }
        rep.rl.ClearDirty();
    }

    const double post_real = accept.ScalarCost(rep.rl);
    const double post_pen = rep.gls.TotalPenalty(rep.rl);
    const double delta_real = post_real - pre_real;
    const double delta_aug = (post_real + post_pen) - (pre_real + pre_pen);

    ++rep.iters;
    int outcome_class = 0;
    bool improved_best = false;
    if (delta_aug < -kEps) {
        ++rep.accepts;
        // Note: we count it as "improving" only if real cost dropped — GLS-only
        // improvements (penalty satisfaction) shouldn't reset stagnation.
        if (delta_real < -kEps) {
            rep.sa->NoteImprovement();
        } else {
            rep.sa->NoteNoImprovement();
        }
        outcome_class = (delta_real < -kEps) ? 2 : 1;
        if (post_real + kEps < rep.best_cost) {
            rep.rl.StoreTo(rep.best_routes);
            rep.best_cost = post_real;
            ++rep.best_updates;
            outcome_class = 3;
            improved_best = true;
            rep.iters_since_best = 0;
            rep.sa->NoteBestUpdate();
            if (gls_decay_on_best > 0.0 && gls_decay_on_best < 1.0) {
                rep.gls.Decay(gls_decay_on_best);
            }
        }
    } else if (rep.sa->Accept(delta_aug)) {
        ++rep.accepts;
        rep.sa->NoteNoImprovement();
        outcome_class = 1;
    } else {
        rep.sa->NoteNoImprovement();
        rep.rl.LoadFrom(snap, rep.oracle);
        outcome_class = 0;
    }
    rep.alns.Reward(d_idx, r_idx, outcome_class);
    rep.sa->Cooldown();
    if (rep.sa->ShouldReheat()) {
        rep.sa->Reheat();
        rep.rl.LoadFrom(rep.best_routes, rep.oracle);
    }
    if (!improved_best) {
        ++rep.iters_since_best;
        if (rep.gls.Lambda() > 0.0 &&
            gls_penalize_after > 0 &&
            rep.iters_since_best >= gls_penalize_after) {
            rep.gls.PenalizeWorstEdges(rep.rl, rep.oracle, gls_penalize_K);
            ++rep.gls_penalize_calls;
            rep.iters_since_best = 0;  // throttle: wait another window before next penalize
        }
    }

    // ---- Periodic route-pair re-optimization ----
    const int pair_reopt_every_pt = params.pair_reopt_every;
    if (pair_reopt_every_pt > 0 && rep.iters > 0 && (rep.iters % pair_reopt_every_pt) == 0 &&
        rep.rl.RouteCount() >= 2 && !budget.ForceCheck()) {
        const int r1_long = rep.rl.LongestRoute();
        const int r2_close = PickClosestRoute(rep.rl, r1_long, rep.oracle.GetCoords());
        if (r2_close >= 0) {
            SearchBudget pair_budget = budget.SubBudget(std::max(200, budget.RemainingMs() / 30));
            if (TryReoptimizeRoutePair(rep.rl, r1_long, r2_close, rep.oracle, candidates,
                                        pair_budget, rep.rng, accept)) {
                const double new_real = accept.ScalarCost(rep.rl);
                if (new_real + kEps < rep.best_cost) {
                    rep.rl.StoreTo(rep.best_routes);
                    rep.best_cost = new_real;
                    ++rep.best_updates;
                    rep.iters_since_best = 0;
                    improved_best = true;
                    rep.sa->NoteBestUpdate();
                }
            }
        }
    }

    rep.current_cost = accept.ScalarCost(rep.rl);
    return improved_best;
}

// Run the main ALNS-SA loop on `rl` (modified in place). Records best solution
// found in `best_routes`, with its cost in `best_cost`. Updates metadata.
// Acceptance uses GLS-augmented cost; best-tracking uses real cost.
template <typename AcceptPolicy>
void RunAlnsSaLoop(RouteList& rl, AlnsFramework& alns, AcceptPolicy& accept,
                   SaEngine& sa, DestroyContext& dctx, RepairContext& rctx,
                   const CandidateSets& candidates, DistanceOracle& d,
                   SearchBudget& budget, std::mt19937& rng,
                   const AutoTuneParams& params, int n_total,
                   RouteSet& best_routes, double& best_cost,
                   PipelineMetadata& meta,
                   EdgePenalties* gls = nullptr,
                   RouteSet* valid_best_routes = nullptr,
                   double* valid_best_cost = nullptr) {
    // ---- Pilot phase: estimate average positive delta for Ben-Ameur ----
    // Capped at 5% of remaining budget so it can never starve the main loop.
    {
        const int max_pilot_ms = std::max(50, budget.RemainingMs() / 20);
        SearchBudget pilot_budget = budget.SubBudget(max_pilot_ms);
        const int pilot_moves = std::min(200, std::max(20, n_total / 100));
        double sum_pos = 0.0;
        int n_pos = 0;
        int p_done = 0;
        for (int p = 0; p < pilot_moves && !pilot_budget.ForceCheck(); ++p) {
            const int K_pilot = std::max(2, params.K_destroy_init / 4);
            const int d_idx = static_cast<int>(rng() % static_cast<unsigned>(alns.DestroyCount()));
            const int r_idx = static_cast<int>(rng() % static_cast<unsigned>(alns.RepairCount()));
            const double pre = accept.ScalarCost(rl);
            RouteSet snap; rl.StoreTo(snap);
            DestroyResult dr = alns.RunDestroy(d_idx, rl, rng, K_pilot);
            alns.RunRepair(r_idx, rl, dr.removed, rng);
            const double post = accept.ScalarCost(rl);
            const double delta = post - pre;
            if (delta > 0.0) { sum_pos += delta; ++n_pos; }
            rl.LoadFrom(snap, d);
            ++p_done;
        }
        const double avg_pos = (n_pos > 0 ? sum_pos / n_pos : 0.0);
        sa.AutoTuneT0(avg_pos, 0.5, accept.ScalarCost(rl));
        meta.SetDouble("sa_T_init", sa.InitialTemperature());
        meta.SetInt("sa_pilot_pos_moves", n_pos);
        meta.SetInt("sa_pilot_done", p_done);
    }

    // ---- Main loop ----
    int iters = 0, accepts = 0, best_updates = 0;
    int iters_since_seg = 0;
    int iters_since_best = 0;
    int gls_penalize_calls = 0;
    int pair_reopt_calls = 0, pair_reopt_accepts = 0, pair_reopt_best_updates = 0;
    int popmusic_calls = 0, popmusic_accepts = 0, popmusic_best_updates = 0;
    int granular_calls = 0, granular_moves = 0, granular_best_updates = 0;
    int granular_2optstar_calls = 0, granular_2optstar_moves = 0, granular_2optstar_best_updates = 0;
    int granular_oropt_calls = 0, granular_oropt_moves = 0, granular_oropt_best_updates = 0;
    int granular_short_passes = 0;
    int region_reopt_calls = 0, region_reopt_accepts = 0, region_reopt_best_updates = 0;
    int route_pair_calls = 0, route_pair_moves = 0, route_pair_best_updates = 0;
    GranularMoveStats granular_stats;
    const int seg_size = 100;
    const int gls_penalize_after = params.gls_penalize_after;
    const int gls_penalize_K = 3;
    const double gls_decay_on_best = 0.5;
    const int pair_reopt_every = params.pair_reopt_every;
    const int popmusic_every = params.popmusic_every;
    const int popmusic_K = params.popmusic_K;
    const int granular_every = params.granular_every;
    const int granular_max_moves = params.granular_max_moves;
    const int granular_scan_customers = params.granular_scan_customers;
    const int granular_2optstar_every = params.granular_2optstar_every;
    const int granular_2optstar_max_moves = params.granular_2optstar_max_moves;
    const int granular_2optstar_scan_customers = params.granular_2optstar_scan_customers;
    const int granular_oropt_every = params.granular_oropt_every;
    const int granular_oropt_max_moves = params.granular_oropt_max_moves;
    const int granular_oropt_scan_customers = params.granular_oropt_scan_customers;
    const int granular_oropt_max_len = params.granular_oropt_max_len;
    const int region_reopt_every = params.region_reopt_every;
    const int region_reopt_K = params.region_reopt_K;
    const int route_pair_2optstar_every = params.route_pair_2optstar_every;
    const int route_pair_2optstar_max_moves = params.route_pair_2optstar_max_moves;
    const int route_pair_2optstar_k = std::max(1, params.route_pair_2optstar_k);
    const int route_pair_2optstar_window = std::max(1, params.route_pair_2optstar_window);
    const int route_pair_2optstar_max_pairs = params.route_pair_2optstar_max_pairs;
    int valid_track_checks = 0;
    int valid_track_updates = 0;
    const bool valid_tracking_enabled = !accept.IsMinMax() &&
                                        params.valid_rebalance_tracking &&
                                        valid_best_routes != nullptr &&
                                        valid_best_cost != nullptr &&
                                        params.valid_rebalance_track_max > 0;
    const int valid_track_every = std::max(1, params.valid_rebalance_track_every);
    const int valid_track_max = std::max(0, params.valid_rebalance_track_max);
    auto try_track_valid_snapshot = [&]() {
        if (!valid_tracking_enabled || valid_track_checks >= valid_track_max || budget.ForceCheck()) return;
        ++valid_track_checks;
        RouteSet candidate;
        rl.StoreTo(candidate);
        EnsureClosedDepot(candidate);
        RebalanceEmptyRoutes(candidate, d);
        EnsureClosedDepot(candidate);
        if (CountEmptyRoutes(candidate) != 0) return;
        const double cost = RouteSumLength(candidate, d);
        if (cost + kEps < *valid_best_cost) {
            *valid_best_cost = cost;
            *valid_best_routes = std::move(candidate);
            ++valid_track_updates;
        }
    };
    while (!budget.ForceCheck()) {
        const double pre_real = accept.ScalarCost(rl);
        const double pre_pen = (gls ? gls->TotalPenalty(rl) : 0.0);
        // Adaptive K: grow with no_improve_streak
        int K = params.K_destroy_init;
        const int streak = sa.NoImproveStreak();
        if (streak > 50) K = std::min(params.K_destroy_max, params.K_destroy_init * (1 + streak / 50));
        K = std::min(K, std::max(2, n_total / 5));

        const int d_idx = alns.PickDestroy(rng);
        const int r_idx = alns.PickRepair(rng);

        // Snapshot for revert
        RouteSet snap; rl.StoreTo(snap);
        DestroyResult dr = alns.RunDestroy(d_idx, rl, rng, K);
        alns.RunRepair(r_idx, rl, dr.removed, rng);

        // Selective LS: 2-opt + or-opt only on dirty routes
        {
            RouteIndex local_idx(n_total);
            for (int r = 0; r < rl.RouteCount(); ++r) {
                if (!rl.IsDirty(r)) continue;
                auto route_copy = rl.Route(r);
                NeighborList2Opt(route_copy, candidates, d, budget, local_idx);
                rl.ReplaceRoute(r, std::move(route_copy), d);
            }
            rl.ClearDirty();
        }

        const double post_real = accept.ScalarCost(rl);
        const double post_pen = (gls ? gls->TotalPenalty(rl) : 0.0);
        const double delta_real = post_real - pre_real;
        const double delta_aug = (post_real + post_pen) - (pre_real + pre_pen);

        ++iters;
        ++iters_since_seg;
        int outcome_class = 0;
        bool improved_best = false;
        if (delta_aug < -kEps) {
            ++accepts;
            if (delta_real < -kEps) {
                sa.NoteImprovement();
            } else {
                sa.NoteNoImprovement();
            }
            outcome_class = (delta_real < -kEps) ? 2 : 1;
            if (post_real + kEps < best_cost) {
                rl.StoreTo(best_routes);
                best_cost = post_real;
                ++best_updates;
                outcome_class = 3;
                improved_best = true;
                iters_since_best = 0;
                meta.EmitAnytimeBest(best_cost);
                sa.NoteBestUpdate();
                if (gls && gls_decay_on_best > 0.0 && gls_decay_on_best < 1.0) {
                    gls->Decay(gls_decay_on_best);
                }
            }
        } else if (sa.Accept(delta_aug)) {
            ++accepts;
            sa.NoteNoImprovement();
            outcome_class = 1;
        } else {
            sa.NoteNoImprovement();
            rl.LoadFrom(snap, d);
            outcome_class = 0;
        }
        alns.Reward(d_idx, r_idx, outcome_class);
        sa.Cooldown();
        if (sa.ShouldReheat()) {
            sa.Reheat();
            rl.LoadFrom(best_routes, d);
        }
        if (iters_since_seg >= seg_size) {
            alns.EndSegment();
            iters_since_seg = 0;
        }
        if (!improved_best) {
            ++iters_since_best;
            if (gls && gls->Lambda() > 0.0 && iters_since_best >= gls_penalize_after) {
                gls->PenalizeWorstEdges(rl, d, gls_penalize_K);
                ++gls_penalize_calls;
                iters_since_best = 0;
            }
        }

        // ---- FILO2-like granular inter-route moves ----
        // Candidate-list relocate/swap moves are cheap enough to run
        // frequently on n=100k, and strict-accept means they only ratchet
        // current cost down.
        if (granular_every > 0 && granular_max_moves > 0 && granular_scan_customers > 0 &&
            iters > 0 && (iters % granular_every) == 0 && rl.RouteCount() >= 2 &&
            !budget.ForceCheck()) {
            ++granular_calls;
            SearchBudget granular_budget = budget.SubBudget(std::max(50, budget.RemainingMs() / 120));
            const int moved = TryGranularInterRoutePass(rl, accept, d, candidates, granular_budget,
                                                        rng, granular_max_moves,
                                                        granular_scan_customers,
                                                        params.route_cap,
                                                        &granular_stats,
                                                        params.granular_endpoint_bias_depth);
            if (moved < granular_max_moves) ++granular_short_passes;
            else granular_short_passes = 0;
            if (moved > 0) {
                granular_moves += moved;
                sa.NoteImprovement();
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++granular_best_updates;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        // ---- FILO2-like short segment relocate (Or-opt) ----
        // Moves len-2/3 consecutive customer blocks across routes using only
        // candidate-neighbour insertion gaps. This catches improvements that
        // single-customer relocate needs several lucky accepted steps to make.
        if (granular_oropt_every > 0 &&
            granular_oropt_max_moves > 0 &&
            granular_oropt_scan_customers > 0 &&
            iters > 0 && (iters % granular_oropt_every) == 0 &&
            rl.RouteCount() >= 2 && !budget.ForceCheck()) {
            ++granular_oropt_calls;
            SearchBudget oropt_budget = budget.SubBudget(std::max(100, budget.RemainingMs() / 110));
            const int moved = TryGranularOrOptPass(rl, accept, d, candidates, oropt_budget,
                                                   rng, granular_oropt_max_moves,
                                                   granular_oropt_scan_customers,
                                                   params.route_cap,
                                                   granular_oropt_max_len,
                                                   &granular_stats,
                                                   params.granular_endpoint_bias_depth);
            if (moved > 0) {
                granular_oropt_moves += moved;
                sa.NoteImprovement();
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++granular_oropt_best_updates;
                    granular_short_passes = 0;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        // ---- LKH/FILO2-style 2-opt* tail exchange ----
        // This is heavier than single-customer granular relocate, so it is
        // opt-in. It swaps route tails across two candidate-adjacent edges and
        // accepts only strict MINSUM improvements.
        if (!accept.IsMinMax() &&
            granular_2optstar_every > 0 &&
            granular_2optstar_max_moves > 0 &&
            granular_2optstar_scan_customers > 0 &&
            iters > 0 && (iters % granular_2optstar_every) == 0 &&
            rl.RouteCount() >= 2 && !budget.ForceCheck()) {
            ++granular_2optstar_calls;
            SearchBudget star_budget = budget.SubBudget(std::max(100, budget.RemainingMs() / 100));
            const int moved = TryGranularTwoOptStarPass(rl, accept, d, candidates, star_budget,
                                                        rng, granular_2optstar_max_moves,
                                                        granular_2optstar_scan_customers,
                                                        &granular_stats,
                                                        params.granular_endpoint_bias_depth);
            if (moved > 0) {
                granular_2optstar_moves += moved;
                sa.NoteImprovement();
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++granular_2optstar_best_updates;
                    granular_short_passes = 0;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        // ---- Endpoint-focused route-pair 2-opt* pass ----
        // Builds a small per-route shortlist by endpoint Euclidean distance
        // and tries 2-opt* cuts in an endpoint window. Targets the depot-
        // bridge / endpoint-stitch case that the global k-NN graph and
        // uniformly-sampled granular passes miss. Strict-improve only.
        if (!accept.IsMinMax() &&
            route_pair_2optstar_every > 0 &&
            route_pair_2optstar_max_moves > 0 &&
            iters > 0 && (iters % route_pair_2optstar_every) == 0 &&
            rl.RouteCount() >= 2 && !budget.ForceCheck()) {
            ++route_pair_calls;
            SearchBudget rp_budget = budget.SubBudget(std::max(50, budget.RemainingMs() / 200));
            const int moved = BuildAndRunRoutePair2OptStar(rl, accept, d, rp_budget,
                                                           route_pair_2optstar_k,
                                                           route_pair_2optstar_max_pairs,
                                                           route_pair_2optstar_max_moves,
                                                           route_pair_2optstar_window,
                                                           &granular_stats);
            if (moved > 0) {
                route_pair_moves += moved;
                sa.NoteImprovement();
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++route_pair_best_updates;
                    granular_short_passes = 0;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        // ---- DualOpt-style spatial region re-optimization ----
        // On n>60k it fires on a real plateau, or earlier when granular
        // passes stop filling their small move quota. This makes region
        // aggressive when the cheap FILO2-like relocate loop is drying up,
        // without starving it while it is still productive.
        const bool region_ready = (iters_since_best >= 20) ||
                                  (n_total > 60000 && granular_short_passes >= 2);
        if (region_reopt_every > 0 && region_reopt_K > 0 &&
            iters > 0 && (iters % region_reopt_every) == 0 &&
            region_ready &&
            rl.RouteCount() >= 2 && !budget.ForceCheck()) {
            ++region_reopt_calls;
            SearchBudget region_budget = budget.SubBudget(std::max(250, budget.RemainingMs() / 60));
            if (TryDualOptRegionReopt(rl, accept, dctx.kdtree, d, candidates, region_budget,
                                      rng, region_reopt_K, params.route_cap,
                                      &granular_stats)) {
                ++region_reopt_accepts;
                sa.NoteImprovement();
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++region_reopt_best_updates;
                    granular_short_passes = 0;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        // ---- Periodic route-pair re-optimization ----
        if (pair_reopt_every > 0 && iters > 0 && (iters % pair_reopt_every) == 0 &&
            rl.RouteCount() >= 2 && !budget.ForceCheck()) {
            const int r1_long = rl.LongestRoute();
            const int r2_close = PickClosestRoute(rl, r1_long, d.GetCoords());
            if (r2_close >= 0) {
                ++pair_reopt_calls;
                SearchBudget pair_budget = budget.SubBudget(std::max(200, budget.RemainingMs() / 30));
                if (TryReoptimizeRoutePair(rl, r1_long, r2_close, d, candidates, pair_budget, rng, accept)) {
                    ++pair_reopt_accepts;
                    const double new_real = accept.ScalarCost(rl);
                    if (new_real + kEps < best_cost) {
                        rl.StoreTo(best_routes);
                        best_cost = new_real;
                        ++best_updates;
                        ++pair_reopt_best_updates;
                        iters_since_best = 0;
                        meta.EmitAnytimeBest(best_cost);
                        sa.NoteBestUpdate();
                    }
                }
            }
        }

        // ---- POPMUSIC zone decomposition step ----
        // Trigger only during stagnation (≥40 iters without real-best). Acts
        // as an escape mechanism — when ordinary ALNS plateaus on n>60k where
        // PT and pair-reopt are both off, POPMUSIC's larger spatial destroy
        // is the next step up. Has to actually find improvement (strict
        // accept inside TryPopmusicStep) — never harms cost.
        if (popmusic_every > 0 && iters > 0 && (iters % popmusic_every) == 0 &&
            iters_since_best >= 40 && !budget.ForceCheck()) {
            ++popmusic_calls;
            SearchBudget pop_budget = budget.SubBudget(std::max(500, budget.RemainingMs() / 20));
            if (TryPopmusicStep(rl, accept, dctx.kdtree, d, candidates, pop_budget, rng, popmusic_K)) {
                ++popmusic_accepts;
                const double new_real = accept.ScalarCost(rl);
                if (new_real + kEps < best_cost) {
                    rl.StoreTo(best_routes);
                    best_cost = new_real;
                    ++best_updates;
                    ++popmusic_best_updates;
                    iters_since_best = 0;
                    meta.EmitAnytimeBest(best_cost);
                    sa.NoteBestUpdate();
                }
            }
        }

        if (valid_tracking_enabled && iters > 0 && (iters % valid_track_every) == 0) {
            try_track_valid_snapshot();
        }
    }
    meta.SetInt("alns_iters", iters);
    meta.SetInt("alns_accepts", accepts);
    meta.SetInt("alns_best_updates", best_updates);
    meta.SetInt("sa_reheats", sa.Reheats());
    meta.SetInt("sa_cooldowns", sa.Cooldowns());
    if (gls) {
        meta.SetInt("gls_penalize_calls", gls_penalize_calls);
        meta.SetInt("gls_penalized_edges", static_cast<long long>(gls->PenalizedEdgeCount()));
        meta.SetDouble("gls_lambda", gls->Lambda());
    }
    meta.SetInt("pair_reopt_calls", pair_reopt_calls);
    meta.SetInt("pair_reopt_accepts", pair_reopt_accepts);
    meta.SetInt("pair_reopt_best_updates", pair_reopt_best_updates);
    meta.SetInt("popmusic_calls", popmusic_calls);
    meta.SetInt("popmusic_accepts", popmusic_accepts);
    meta.SetInt("popmusic_best_updates", popmusic_best_updates);
    meta.SetInt("popmusic_K", popmusic_K);
    meta.SetInt("granular_calls", granular_calls);
    meta.SetInt("granular_moves", granular_moves);
    meta.SetInt("granular_relocate_accepts", granular_stats.relocate_accepts);
    meta.SetInt("granular_swap_accepts", granular_stats.swap_accepts);
    meta.SetInt("granular_2optstar_accepts", granular_stats.two_optstar_accepts);
    meta.SetInt("granular_oropt_accepts", granular_stats.oropt_accepts);
    meta.SetInt("granular_best_updates", granular_best_updates);
    meta.SetInt("granular_short_passes", granular_short_passes);
    meta.SetInt("granular_every", granular_every);
    meta.SetInt("granular_scan_customers", granular_scan_customers);
    meta.SetInt("granular_max_moves", granular_max_moves);
    meta.SetInt("granular_2optstar_calls", granular_2optstar_calls);
    meta.SetInt("granular_2optstar_moves", granular_2optstar_moves);
    meta.SetInt("granular_2optstar_best_updates", granular_2optstar_best_updates);
    meta.SetInt("granular_2optstar_every", granular_2optstar_every);
    meta.SetInt("granular_2optstar_scan_customers", granular_2optstar_scan_customers);
    meta.SetInt("granular_2optstar_max_moves", granular_2optstar_max_moves);
    meta.SetInt("granular_oropt_calls", granular_oropt_calls);
    meta.SetInt("granular_oropt_moves", granular_oropt_moves);
    meta.SetInt("granular_oropt_best_updates", granular_oropt_best_updates);
    meta.SetInt("granular_oropt_every", granular_oropt_every);
    meta.SetInt("granular_oropt_scan_customers", granular_oropt_scan_customers);
    meta.SetInt("granular_oropt_max_moves", granular_oropt_max_moves);
    meta.SetInt("granular_oropt_max_len", granular_oropt_max_len);
    meta.SetInt("region_reopt_calls", region_reopt_calls);
    meta.SetInt("region_reopt_accepts", region_reopt_accepts);
    meta.SetInt("region_reopt_best_updates", region_reopt_best_updates);
    meta.SetInt("region_reopt_K", region_reopt_K);
    meta.SetInt("route_pair_2optstar_calls", route_pair_calls);
    meta.SetInt("route_pair_2optstar_moves", route_pair_moves);
    meta.SetInt("route_pair_2optstar_best_updates", route_pair_best_updates);
    meta.SetInt("route_pair_2optstar_accepts", granular_stats.route_pair_2optstar_accepts);
    meta.SetInt("route_pair_2optstar_pairs", granular_stats.route_pair_pairs);
    meta.SetInt("route_pair_2optstar_every", route_pair_2optstar_every);
    meta.SetInt("route_pair_2optstar_max_moves", route_pair_2optstar_max_moves);
    meta.SetInt("route_pair_2optstar_k", route_pair_2optstar_k);
    meta.SetInt("route_pair_2optstar_window", route_pair_2optstar_window);
    meta.SetInt("route_pair_2optstar_max_pairs", route_pair_2optstar_max_pairs);
    meta.SetInt("valid_rebalance_track_enabled", valid_tracking_enabled ? 1 : 0);
    meta.SetInt("valid_rebalance_track_every", valid_track_every);
    meta.SetInt("valid_rebalance_track_max", valid_track_max);
    meta.SetInt("valid_rebalance_track_checks", valid_track_checks);
    meta.SetInt("valid_rebalance_track_updates", valid_track_updates);
    if (valid_best_cost) meta.SetDouble("valid_rebalance_track_best_cost", *valid_best_cost);
    // Per-operator stats
    for (size_t i = 0; i < alns.DestroyNames().size(); ++i) {
        meta.SetInt("destroy_" + alns.DestroyNames()[i] + "_calls", alns.DestroyStats()[i].calls);
        meta.SetInt("destroy_" + alns.DestroyNames()[i] + "_accepts", alns.DestroyStats()[i].accepts);
        meta.SetDouble("destroy_" + alns.DestroyNames()[i] + "_weight", alns.DestroyStats()[i].weight);
    }
    for (size_t i = 0; i < alns.RepairNames().size(); ++i) {
        meta.SetInt("repair_" + alns.RepairNames()[i] + "_calls", alns.RepairStats()[i].calls);
        meta.SetInt("repair_" + alns.RepairNames()[i] + "_accepts", alns.RepairStats()[i].accepts);
        meta.SetDouble("repair_" + alns.RepairNames()[i] + "_weight", alns.RepairStats()[i].weight);
    }
}

// ===== Parallel Tempering loop ===============================================
//
// K replicas run ALNS-SA in parallel, each at its own temperature drawn from a
// geometric T_min..T_max sequence (T_min = coldest, exploitative; T_max =
// hottest, exploratory). After each "epoch" we attempt swaps between adjacent
// replicas (i, i+1) using the standard Metropolis-PT rule:
//
//   accept = min(1, exp((1/T_i - 1/T_j) * (E_i - E_j)))
//
// Swap exchanges current solutions and current_cost between replicas; T's stay
// pinned to replica index. Best-snapshot is per-replica; the global best is
// the min across replicas.
//
// Pre-condition: each replica's `rl` is already loaded with the current
// solution, alns is registered, sa is constructed (T set per-replica below),
// best_routes/best_cost reflect the seed solution.
template <typename AcceptPolicy>
inline void RunPtAlnsSaLoop(std::vector<std::unique_ptr<PtReplicaCtx>>& reps,
                             AcceptPolicy& accept,
                             const CandidateSets& candidates,
                             SearchBudget& budget,
                             const AutoTuneParams& params, int n_total,
                             RouteSet& best_routes_global, double& best_cost_global,
                             PipelineMetadata& meta) {
    const int K = static_cast<int>(reps.size());
    if (K <= 0) return;

    // ---- Pilot phase (replica 0 only) to estimate T_init via Ben-Ameur ----
    double T_base = 0.0;
    {
        const int max_pilot_ms = std::max(50, budget.RemainingMs() / 30);
        SearchBudget pilot_budget = budget.SubBudget(max_pilot_ms);
        const int pilot_moves = std::min(100, std::max(20, n_total / 200));
        auto& rep0 = *reps[0];
        double sum_pos = 0.0;
        int n_pos = 0;
        for (int p = 0; p < pilot_moves && !pilot_budget.ForceCheck(); ++p) {
            const int K_pilot = std::max(2, params.K_destroy_init / 4);
            const int d_idx = static_cast<int>(rep0.rng() % static_cast<unsigned>(rep0.alns.DestroyCount()));
            const int r_idx = static_cast<int>(rep0.rng() % static_cast<unsigned>(rep0.alns.RepairCount()));
            const double pre = accept.ScalarCost(rep0.rl);
            RouteSet snap; rep0.rl.StoreTo(snap);
            DestroyResult dr = rep0.alns.RunDestroy(d_idx, rep0.rl, rep0.rng, K_pilot);
            rep0.alns.RunRepair(r_idx, rep0.rl, dr.removed, rep0.rng);
            const double post = accept.ScalarCost(rep0.rl);
            const double delta = post - pre;
            if (delta > 0.0) { sum_pos += delta; ++n_pos; }
            rep0.rl.LoadFrom(snap, rep0.oracle);
        }
        const double avg_pos = (n_pos > 0 ? sum_pos / n_pos : 0.0);
        // Use replica 0's SaEngine as a thermometer: AutoTuneT0 gives us T_base
        rep0.sa->AutoTuneT0(avg_pos, 0.5, accept.ScalarCost(rep0.rl));
        T_base = rep0.sa->InitialTemperature();
        meta.SetDouble("pt_T_base", T_base);
    }

    // ---- Spread T across replicas: rep[0] = T_base (coldest), rep[K-1] = T_base * 8 (hottest) ----
    const double T_max_factor = 8.0;
    for (int r = 0; r < K; ++r) {
        const double frac = (K > 1) ? static_cast<double>(r) / (K - 1) : 0.0;
        const double T = T_base * std::pow(T_max_factor, frac);
        reps[static_cast<size_t>(r)]->sa->ForceInit(T);
        reps[static_cast<size_t>(r)]->current_cost = accept.ScalarCost(reps[static_cast<size_t>(r)]->rl);
    }

    // ---- Epoch loop ----
    // Aim for ~20 epochs over the available budget so PT swaps have many chances.
    const int target_epoch_ms = std::max(500, budget.RemainingMs() / 20);
    int n_epochs = 0;
    int n_swaps_attempted = 0, n_swaps_accepted = 0;
    std::mt19937 swap_rng(reps[0]->rng() ^ 0xa3b1c2d3u);

    while (!budget.ForceCheck()) {
        const int per_epoch = std::min(target_epoch_ms, budget.RemainingMs());
        if (per_epoch <= 50) break;
        ++n_epochs;

#ifdef _OPENMP
        #pragma omp parallel for schedule(static) num_threads(K)
#endif
        for (int r = 0; r < K; ++r) {
            auto& rep = *reps[static_cast<size_t>(r)];
            SearchBudget ep_budget(per_epoch);  // independent per-replica budget
            while (!ep_budget.ForceCheck()) {
                DoOneAlnsStep(rep, accept, candidates, ep_budget, params, n_total);
            }
        }

        // ---- Sequential PT swap attempts (cheap, K-1 attempts) ----
        // Even/odd alternating sweep (Geyer-style) to mix neighbours fairly.
        const int sweep_start = (n_epochs & 1) ? 0 : 1;
        for (int i = sweep_start; i + 1 < K; i += 2) {
            const auto& rep_i = *reps[static_cast<size_t>(i)];
            const auto& rep_j = *reps[static_cast<size_t>(i + 1)];
            const double E_i = rep_i.current_cost;
            const double E_j = rep_j.current_cost;
            const double T_i = rep_i.sa->Temperature();
            const double T_j = rep_j.sa->Temperature();
            if (T_i <= 0.0 || T_j <= 0.0) continue;
            const double exponent = (1.0 / T_i - 1.0 / T_j) * (E_i - E_j);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            const double rnd = u(swap_rng);
            ++n_swaps_attempted;
            if (exponent >= 0.0 || rnd < std::exp(exponent)) {
                ++n_swaps_accepted;
                reps[static_cast<size_t>(i)]->rl.Swap(reps[static_cast<size_t>(i + 1)]->rl);
                std::swap(reps[static_cast<size_t>(i)]->current_cost,
                          reps[static_cast<size_t>(i + 1)]->current_cost);
                // Per-replica best stays with the replica (it tracks history).
            }
        }

        // ---- Update global best ----
        for (int r = 0; r < K; ++r) {
            const auto& rep = *reps[static_cast<size_t>(r)];
            if (rep.best_cost + kEps < best_cost_global) {
                best_cost_global = rep.best_cost;
                best_routes_global = rep.best_routes;
                meta.EmitAnytimeBest(best_cost_global);
            }
        }
    }

    // ---- Final: extract aggregated stats ----
    int total_iters = 0, total_accepts = 0, total_best_updates = 0;
    int total_reheats = 0, total_cooldowns = 0;
    for (int r = 0; r < K; ++r) {
        const auto& rep = *reps[static_cast<size_t>(r)];
        total_iters += rep.iters;
        total_accepts += rep.accepts;
        total_best_updates += rep.best_updates;
        total_reheats += rep.sa->Reheats();
        total_cooldowns += rep.sa->Cooldowns();
    }
    meta.SetInt("alns_iters", total_iters);
    meta.SetInt("alns_accepts", total_accepts);
    meta.SetInt("alns_best_updates", total_best_updates);
    meta.SetInt("sa_reheats", total_reheats);
    meta.SetInt("sa_cooldowns", total_cooldowns);
    meta.SetInt("pt_replicas", K);
    meta.SetInt("pt_epochs", n_epochs);
    meta.SetInt("pt_swaps_attempted", n_swaps_attempted);
    meta.SetInt("pt_swaps_accepted", n_swaps_accepted);
    meta.SetDouble("sa_T_init", T_base);

    // Per-replica T's and best costs (debug)
    for (int r = 0; r < K; ++r) {
        const auto& rep = *reps[static_cast<size_t>(r)];
        meta.SetDouble("pt_rep" + std::to_string(r) + "_T", rep.sa->Temperature());
        meta.SetDouble("pt_rep" + std::to_string(r) + "_best", rep.best_cost);
        meta.SetInt("pt_rep" + std::to_string(r) + "_iters", rep.iters);
    }
    // Aggregate per-operator stats from replica 0 (proxy)
    if (!reps.empty()) {
        const auto& rep0 = *reps[0];
        for (size_t i = 0; i < rep0.alns.DestroyNames().size(); ++i) {
            meta.SetInt("destroy_" + rep0.alns.DestroyNames()[i] + "_calls", rep0.alns.DestroyStats()[i].calls);
            meta.SetInt("destroy_" + rep0.alns.DestroyNames()[i] + "_accepts", rep0.alns.DestroyStats()[i].accepts);
            meta.SetDouble("destroy_" + rep0.alns.DestroyNames()[i] + "_weight", rep0.alns.DestroyStats()[i].weight);
        }
        for (size_t i = 0; i < rep0.alns.RepairNames().size(); ++i) {
            meta.SetInt("repair_" + rep0.alns.RepairNames()[i] + "_calls", rep0.alns.RepairStats()[i].calls);
            meta.SetInt("repair_" + rep0.alns.RepairNames()[i] + "_accepts", rep0.alns.RepairStats()[i].accepts);
            meta.SetDouble("repair_" + rep0.alns.RepairNames()[i] + "_weight", rep0.alns.RepairStats()[i].weight);
        }
    }
}

// Top-level pipeline. AcceptPolicy plugs in MINSUM or MIN-MAX behavior.
template <typename AcceptPolicy>
void RunPipeline(const mtsp::Instance& inst, AcceptPolicy& accept,
                 const AutoTuneParams& params, int time_budget_ms, unsigned seed,
                 RouteSet& out, PipelineMetadata& meta) {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = inst.GetNodeCount();
    const int m = std::max(1, inst.GetSalesmanCount());

    const bool seed_race_enabled = params.seed_race_count > 1 && params.seed_race_ms > 0;
    const int seed_race_extra_ms = seed_race_enabled ? std::max(0, params.seed_race_extra_ms) : 0;
    const int extra_alns_ms = std::max(0, params.extra_alns_ms);
    const int effective_time_budget_ms = (time_budget_ms > 0)
                                      ? time_budget_ms + seed_race_extra_ms + extra_alns_ms
                                      : time_budget_ms;

    // Effective budget (small reserve for final cleanup). The optional seed
    // race can receive an extra wall-clock allowance without shrinking the
    // normal phase budgets used by the main search.
    const int reserve_ms = std::max(200, time_budget_ms / 50);
    SearchBudget budget(effective_time_budget_ms, reserve_ms);

    // Resolve sub-budgets in ms
    const int total_ms = (time_budget_ms > 0) ? (time_budget_ms - reserve_ms) : 60000;
    const int seed_ms = std::max(50, total_ms * params.budget_seed_pct / 100);
    const int cand_ms = std::max(50, total_ms * params.budget_cand_pct / 100);
    const int polish_ms = std::max(100, total_ms * params.budget_polish_pct / 100);
    const int alns_ms = std::max(100, total_ms * params.budget_alns_pct / 100) + extra_alns_ms;
    const int final_ms = std::max(100, total_ms * params.budget_final_pct / 100);

#ifdef _OPENMP
    omp_set_num_threads(std::max(1, params.num_threads));
#endif

    DistanceOracle d(inst);
    std::mt19937 rng(seed);
    meta.SetInt("node_count", n);
    meta.SetInt("salesman_count", m);
    meta.SetInt("seed", seed);
    meta.SetInt("budget_ms", time_budget_ms);
    meta.SetInt("budget_effective_ms", effective_time_budget_ms);
    meta.SetInt("budget_seed_race_extra_ms", seed_race_extra_ms);
    meta.SetInt("budget_extra_alns_ms", extra_alns_ms);
    meta.SetInt("budget_seed_ms", seed_ms);
    meta.SetInt("budget_cand_ms", cand_ms);
    meta.SetInt("budget_polish_ms", polish_ms);
    meta.SetInt("budget_alns_ms", alns_ms);
    meta.SetInt("budget_final_ms", final_ms);
    meta.StartAnytime();

    // ---- Phase 1: candidate set ----
    const auto t_phase1 = std::chrono::steady_clock::now();
    SearchBudget cand_budget = budget.SubBudget(cand_ms);
    CandidateSets global = BuildKnnCandidates(inst, params.k_NN, cand_budget);
    if (params.popmusic_solutions > 0 && !cand_budget.ForceCheck()) {
        AugmentWithPopmusicEdges(inst, global, params.popmusic_solutions, params.k_NN + 4, rng, cand_budget);
    }
    SymmetrizeCandidates(global);
    meta.SetInt("phase1_ms", ElapsedMs(t_phase1));
    meta.SetInt("candidate_count_avg", static_cast<long long>(params.k_NN));

    // ---- Phase 2: multi-seed portfolio ----
    const auto t_phase2 = std::chrono::steady_clock::now();
    SearchBudget seed_budget = budget.SubBudget(seed_ms);
    std::vector<RouteSet> seeds;
    if (params.use_classic_seeds) {
    // Full O(n*L) round-robin NN — strongest seed for euclidean instances.
    seeds.push_back(BuildRoundRobinNN(inst));
    EnsureClosedDepot(seeds.back());
    // Fast kNN-based variant as backup (cheap)
    seeds.push_back(BuildRoundRobinNNFast(inst, global));
    EnsureClosedDepot(seeds.back());
    seeds.push_back(BuildPolarSweep(inst));
    EnsureClosedDepot(seeds.back());
    if (params.use_kmeans_seed) {
        seeds.push_back(BuildKMeansBalanced(inst, rng, 15));
        EnsureClosedDepot(seeds.back());
    }
    if (params.use_savings_seed && n <= 20000) {
        seeds.push_back(BuildSavingsSeed(inst, global, seed_budget));
        EnsureClosedDepot(seeds.back());
    }
    }
    auto make_depot_rng = [&](int multiplier, int variant) {
        std::seed_seq seq{
            static_cast<uint32_t>(seed),
            static_cast<uint32_t>(0x9E3779B9u),
            static_cast<uint32_t>(multiplier * 1009),
            static_cast<uint32_t>(variant * 9176 + 17)
        };
        return std::mt19937(seq);
    };
    if (!accept.IsMinMax() && (params.depot_seed_mode & 1) != 0) {
        auto depot_rng = make_depot_rng(1, 0);
        RouteSet depot_seed = BuildDepotCandidateNNSeed(inst, global, 1, 0.0, depot_rng);
        EnsureClosedDepot(depot_seed);
        const double cost = accept.ScalarCostOfRoutes(depot_seed, d);
        meta.SetDouble("seed_depot_m_cost", cost);
        seeds.push_back(std::move(depot_seed));
    }
    if (!accept.IsMinMax() && (params.depot_seed_mode & 2) != 0) {
        int depot_2m_count = 0;
        double depot_2m_best = std::numeric_limits<double>::max();
        std::string spread_meta;
        auto add_depot_2m_seed = [&](double spread, std::mt19937& seed_rng) {
            RouteSet depot_seed = BuildDepotCandidateNNSeed(inst, global, 2, spread, seed_rng);
            EnsureClosedDepot(depot_seed);
            const double cost = accept.ScalarCostOfRoutes(depot_seed, d);
            if (cost < depot_2m_best) depot_2m_best = cost;
            seeds.push_back(std::move(depot_seed));
            ++depot_2m_count;
        };

        if (params.depot_seed_spread_probs.empty()) {
            std::mt19937 legacy_rng(seed);
            const double spread = std::clamp(params.depot_seed_spread_prob, 0.0, 1.0);
            add_depot_2m_seed(spread, legacy_rng);
            if (!spread_meta.empty()) spread_meta += ",";
            spread_meta += std::to_string(spread);
        } else {
            std::mt19937 legacy_rng(seed);
            const double legacy_spread = std::clamp(params.depot_seed_spread_prob, 0.0, 1.0);
            add_depot_2m_seed(legacy_spread, legacy_rng);
            spread_meta += "legacy:";
            spread_meta += std::to_string(legacy_spread);

            const auto& spreads = params.depot_seed_spread_probs;
            for (size_t pi = 0; pi < spreads.size(); ++pi) {
                const double spread = std::clamp(spreads[pi], 0.0, 1.0);
                const int restarts = (spread <= 0.0 || spread >= 1.0)
                                   ? 1
                                   : std::max(1, params.depot_seed_restarts);
                spread_meta += ",";
                spread_meta += std::to_string(spread);
                for (int restart = 0; restart < restarts; ++restart) {
                    auto depot_rng = make_depot_rng(2, static_cast<int>(pi) * 31 + restart);
                    add_depot_2m_seed(spread, depot_rng);
                }
            }
        }
        if (depot_2m_count > 0) {
            meta.SetDouble("seed_depot_2m_cost", depot_2m_best);
            meta.SetInt("seed_depot_2m_count", depot_2m_count);
            meta.Set("seed_depot_2m_spreads", spread_meta);
            meta.SetInt("seed_depot_2m_restarts", params.depot_seed_restarts);
        }
    }
    if (!accept.IsMinMax() && !params.depot_seed_rings.empty()) {
        int ring_count = 0;
        double ring_best = std::numeric_limits<double>::max();
        std::string ring_meta;
        meta.SetInt("seed_depot_ring_start_index", static_cast<long long>(seeds.size()));
        for (int raw_ring : params.depot_seed_rings) {
            const int ring = std::max(1, raw_ring);
            auto depot_rng = make_depot_rng(ring + 1, ring * 41);
            RouteSet ring_seed = BuildDepotCandidateNNSeed(inst, global, ring + 1, 0.0,
                                                           depot_rng, ring);
            EnsureClosedDepot(ring_seed);
            const double cost = accept.ScalarCostOfRoutes(ring_seed, d);
            if (cost < ring_best) ring_best = cost;
            seeds.push_back(std::move(ring_seed));
            if (!ring_meta.empty()) ring_meta += ",";
            ring_meta += std::to_string(ring);
            ++ring_count;
        }
        meta.SetDouble("seed_depot_ring_best_cost", ring_best);
        meta.SetInt("seed_depot_ring_count", ring_count);
        meta.Set("seed_depot_rings", ring_meta);
    }
    if (!accept.IsMinMax() && params.use_single_route_seed) {
        double best_before_single = std::numeric_limits<double>::max();
        for (const auto& seed_routes : seeds) {
            if (seed_routes.empty()) continue;
            const double cost = accept.ScalarCostOfRoutes(seed_routes, d);
            if (cost < best_before_single) best_before_single = cost;
        }
        RouteSet single_seed = BuildSingleRouteNNSeed(inst, global);
        EnsureClosedDepot(single_seed);
        const double cost = accept.ScalarCostOfRoutes(single_seed, d);
        const int raw_empty = CountEmptyRoutes(single_seed);
        meta.SetDouble("seed_single_route_cost", cost);
        meta.SetInt("seed_single_route_empty_routes", raw_empty);
        meta.SetDouble("seed_single_route_best_before", best_before_single);
        const double gain = (best_before_single > 0.0 && std::isfinite(best_before_single))
                          ? (best_before_single - cost) / best_before_single
                          : 0.0;
        meta.SetDouble("seed_single_route_gain", gain);
        meta.SetInt("seed_single_route_start_variants", params.single_route_start_variants);
        if (gain + kEps >= std::max(0.0, params.single_route_seed_min_gain)) {
            if (params.single_route_rebalance_seed) {
                RouteSet best_single_seed;
                double best_single_cost = std::numeric_limits<double>::infinity();
                int best_single_start = 0;
                int best_single_variant = -1;
                int variant_evaluated = 0;
                int variant_valid = 0;
                auto consider_single_variant = [&](RouteSet candidate, int variant_id, int start_city) {
                    RebalanceEmptyRoutes(candidate, d);
                    EnsureClosedDepot(candidate);
                    ++variant_evaluated;
                    const int empties = CountEmptyRoutes(candidate);
                    if (empties != 0) return;
                    ++variant_valid;
                    const double candidate_cost = accept.ScalarCostOfRoutes(candidate, d);
                    if (candidate_cost + kEps < best_single_cost) {
                        best_single_cost = candidate_cost;
                        best_single_start = start_city;
                        best_single_variant = variant_id;
                        best_single_seed = std::move(candidate);
                    }
                };

                consider_single_variant(std::move(single_seed), 0, 0);
                const int variant_limit = std::max(0, params.single_route_start_variants);
                if (variant_limit > 0) {
                    const std::vector<int> starts = PickSingleRouteNNStartVariants(inst, variant_limit);
                    int variant_id = 1;
                    for (int start_city : starts) {
                        RouteSet variant = BuildSingleRouteNNSeedFromStart(inst, global, start_city);
                        EnsureClosedDepot(variant);
                        consider_single_variant(std::move(variant), variant_id, start_city);
                        ++variant_id;
                    }
                }

                if (!best_single_seed.empty()) {
                    single_seed = std::move(best_single_seed);
                } else {
                    single_seed = BuildSingleRouteNNSeed(inst, global);
                    RebalanceEmptyRoutes(single_seed, d);
                    EnsureClosedDepot(single_seed);
                }
                meta.SetInt("seed_single_route_variant_evaluated", variant_evaluated);
                meta.SetInt("seed_single_route_variant_valid", variant_valid);
                meta.SetInt("seed_single_route_variant_best_id", best_single_variant);
                meta.SetInt("seed_single_route_variant_best_start", best_single_start);
                if (std::isfinite(best_single_cost)) {
                    meta.SetDouble("seed_single_route_variant_best_cost", best_single_cost);
                }
                meta.SetDouble("seed_single_route_rebalanced_cost",
                               accept.ScalarCostOfRoutes(single_seed, d));
                meta.SetInt("seed_single_route_rebalanced_empty_routes",
                            CountEmptyRoutes(single_seed));
            }
            meta.SetInt("seed_single_route_rebalance_seed", params.single_route_rebalance_seed ? 1 : 0);
            meta.SetInt("seed_single_route_index", static_cast<long long>(seeds.size()));
            meta.SetInt("seed_single_route_accepted", 1);
            seeds.push_back(std::move(single_seed));
        } else {
            meta.SetInt("seed_single_route_rebalance_seed", params.single_route_rebalance_seed ? 1 : 0);
            meta.SetInt("seed_single_route_variant_evaluated", 0);
            meta.SetInt("seed_single_route_variant_valid", 0);
            meta.SetInt("seed_single_route_variant_best_id", -1);
            meta.SetInt("seed_single_route_variant_best_start", 0);
            meta.SetInt("seed_single_route_index", -1);
            meta.SetInt("seed_single_route_accepted", 0);
        }
    }
    if (!accept.IsMinMax() && params.angular_seed_rotations > 0 &&
        !params.angular_seed_quantiles.empty()) {
        int angular_count = 0;
        double angular_best = std::numeric_limits<double>::max();
        std::string angular_meta;
        meta.SetInt("seed_angular_start_index", static_cast<long long>(seeds.size()));
        const int rotations = std::max(1, params.angular_seed_rotations);
        for (int rot = 0; rot < rotations; ++rot) {
            const double rotation = static_cast<double>(rot) / static_cast<double>(rotations);
            for (double raw_q : params.angular_seed_quantiles) {
                const double q = std::clamp(raw_q, 0.0, 1.0);
                auto angular_rng = make_depot_rng(11 + rot, static_cast<int>(q * 1000.0));
                RouteSet angular_seed = BuildAngularSectorNNSeed(inst, global,
                                                                  params.angular_seed_pool_multiplier,
                                                                  rotation, q, angular_rng);
                EnsureClosedDepot(angular_seed);
                const double cost = accept.ScalarCostOfRoutes(angular_seed, d);
                if (cost < angular_best) angular_best = cost;
                seeds.push_back(std::move(angular_seed));
                if (!angular_meta.empty()) angular_meta += ",";
                angular_meta += "r";
                angular_meta += std::to_string(rot);
                angular_meta += ":q";
                angular_meta += std::to_string(q);
                ++angular_count;
            }
        }
        meta.SetDouble("seed_angular_best_cost", angular_best);
        meta.SetInt("seed_angular_count", angular_count);
        meta.SetInt("seed_angular_pool_multiplier", params.angular_seed_pool_multiplier);
        meta.Set("seed_angular_configs", angular_meta);
    }
    if (seeds.empty()) {
        seeds.push_back(BuildRoundRobinNNFast(inst, global));
        EnsureClosedDepot(seeds.back());
    }
    RouteSet current = PickBestSeedWithGranularRace(seeds, d, accept, global, budget,
                                                    seed, params, n, m, meta);
    if (current.empty()) current = seeds.front();
    EnsureClosedDepot(current);
    meta.SetDouble("seed_chosen_cost", accept.ScalarCostOfRoutes(current, d));
    meta.SetInt("seed_count", static_cast<long long>(seeds.size()));
    meta.SetInt("phase2_ms", ElapsedMs(t_phase2));

    auto augment_route_candidates = [&](const RouteSet& routes, const std::string& prefix) {
        if (!params.route_candidate_augmentation) {
            meta.SetInt(prefix + "_enabled", 0);
            return;
        }
        const auto t_aug = std::chrono::steady_clock::now();
        CandidateAugmentStats stats = AugmentWithRouteBoundaryCandidates(
            inst, routes, global, d,
            params.route_candidate_endpoint_depth,
            params.route_candidate_expensive_edges_per_route,
            params.route_candidate_knn_probe,
            params.route_candidate_per_anchor,
            params.route_candidate_max_extra_per_node);
        meta.SetInt(prefix + "_enabled", 1);
        meta.SetInt(prefix + "_ms", ElapsedMs(t_aug));
        meta.SetInt(prefix + "_anchors", stats.anchors);
        meta.SetInt(prefix + "_endpoint_anchors", stats.endpoint_anchors);
        meta.SetInt(prefix + "_expensive_anchors", stats.expensive_anchors);
        meta.SetInt(prefix + "_edges_added", stats.edges_added);
        meta.SetDouble(prefix + "_candidate_avg", AverageCandidateListSize(global));
    };
    augment_route_candidates(current, "route_candidate_seed_aug");

    // ---- Phase 3 & 4: per-route polish + intra ILS (combined) ----
    const auto t_phase3 = std::chrono::steady_clock::now();
    SearchBudget polish_budget = budget.SubBudget(polish_ms);
    RawDistFn raw_dist{inst};  // thread-safe stateless distance for parallel phases
    ParallelFinal2Opt(current, global, raw_dist, polish_budget, n);
    if (!polish_budget.ForceCheck()) {
        ParallelPolish(current, global, raw_dist, polish_budget, params.ils_rounds, rng, n);
    }
    meta.SetInt("phase3_ms", ElapsedMs(t_phase3));
    meta.SetDouble("after_polish_cost", accept.ScalarCostOfRoutes(current, d));

    // ---- Phase 5: ALNS-SA main loop (single-replica or Parallel Tempering) ----
    const auto t_phase5 = std::chrono::steady_clock::now();
    SearchBudget alns_budget = budget.SubBudget(alns_ms);

    KDTree2D kdtree(inst.GetCoords());

    SaConfig sc;
    sc.T_frac_init = params.T_frac_init;
    sc.cooling = params.sa_cooling;
    sc.reheat_after_no_improvement = params.reheat_after;
    sc.reheat_after_no_improvement_ms = params.reheat_after_ms;

    RouteSet best_routes = current;
    double best_cost = accept.ScalarCostOfRoutes(current, d);
    RouteSet valid_tracked_routes = current;
    double valid_tracked_cost = RouteSumLength(current, d);
    // Initial anytime point: cost of the post-polish solution (pre-ALNS baseline).
    meta.EmitAnytimeBest(best_cost);

    // GLS lambda based on average real edge cost — same scale for both branches.
    const double polish_real_sum = RouteSumLength(current, d);
    const double gls_lambda = SuggestGlsLambda(polish_real_sum, n, m, /*alpha=*/0.10);

    const int K_pt = std::max(1, params.pt_replicas);
    RouteSet alns_exit_routes;
    bool have_alns_exit_routes = false;
    if (K_pt <= 1) {
        // ---- Single-replica path (cheap setup, fastest in serial) ----
        RouteList rl(n, m);
        rl.LoadFrom(current, d);
        DestroyContext dctx{d, kdtree, n};
        RepairContext rctx_normal{d, global, false};
        RepairContext rctx_balance{d, global, true};
        // Plumb FILO2-style capacity (0 = disabled in legacy solvers).
        rctx_normal.route_cap = params.route_cap;
        rctx_balance.route_cap = params.route_cap;
        AlnsFramework alns;
        RegisterAlnsOps(alns, dctx, rctx_normal, rctx_balance, accept);
        SaEngine sa(sc, rng);
        sa.InitFromBaseline(accept.ScalarCost(rl));
        rl.StoreTo(best_routes);
        best_cost = accept.ScalarCost(rl);
        EdgePenalties gls;
        gls.SetLambda(gls_lambda);
        RunAlnsSaLoop(rl, alns, accept, sa, dctx, rctx_normal, global, d, alns_budget, rng,
                      params, n, best_routes, best_cost, meta, &gls,
                      &valid_tracked_routes, &valid_tracked_cost);
        rl.StoreTo(alns_exit_routes);
        have_alns_exit_routes = true;
        meta.SetInt("pt_replicas", 1);
    } else {
        // ---- Parallel Tempering path (K replicas, periodic swaps) ----
        std::vector<std::unique_ptr<PtReplicaCtx>> reps;
        reps.reserve(static_cast<size_t>(K_pt));
        for (int r = 0; r < K_pt; ++r) {
            reps.push_back(std::make_unique<PtReplicaCtx>(inst, kdtree, global, n, m));
            auto& rep = *reps.back();
            rep.rng.seed(seed + static_cast<unsigned>(r) * 1009u);
            rep.rl.LoadFrom(current, rep.oracle);
            // Plumb FILO2-style capacity to per-replica repair contexts.
            rep.rctx_normal.route_cap = params.route_cap;
            rep.rctx_balance.route_cap = params.route_cap;
            rep.sa = std::make_unique<SaEngine>(sc, rep.rng);
            rep.sa->InitFromBaseline(accept.ScalarCost(rep.rl));
            rep.gls.SetLambda(gls_lambda);
            RegisterAlnsOps(rep.alns, rep.dctx, rep.rctx_normal, rep.rctx_balance, accept);
            rep.rl.StoreTo(rep.best_routes);
            rep.best_cost = accept.ScalarCost(rep.rl);
            rep.current_cost = rep.best_cost;
        }
        best_routes = reps[0]->best_routes;
        best_cost = reps[0]->best_cost;
        RunPtAlnsSaLoop(reps, accept, global, alns_budget, params, n, best_routes, best_cost, meta);
    }
    meta.SetDouble("gls_lambda", gls_lambda);
    meta.SetInt("phase5_ms", ElapsedMs(t_phase5));
    meta.SetDouble("after_alns_cost", best_cost);

    if (!accept.IsMinMax() && params.pre_final_rebalance) {
        auto repair_cost = [&](RouteSet candidate, const std::string& prefix, RouteSet* repaired_out) {
            EnsureClosedDepot(candidate);
            const int empty_before = CountEmptyRoutes(candidate);
            RebalanceEmptyRoutes(candidate, d);
            EnsureClosedDepot(candidate);
            const int empty_after = CountEmptyRoutes(candidate);
            const double cost = RouteSumLength(candidate, d);
            meta.SetInt(prefix + "_empty_before", empty_before);
            meta.SetInt(prefix + "_empty_after", empty_after);
            meta.SetDouble(prefix + "_cost", cost);
            if (repaired_out) *repaired_out = std::move(candidate);
            return (empty_after == 0) ? cost : std::numeric_limits<double>::infinity();
        };

        RouteSet repaired_best;
        double repaired_best_cost = repair_cost(best_routes, "pre_final_rebalance_best", &repaired_best);
        int selected = 0;
        RouteSet repaired_exit;
        if (have_alns_exit_routes) {
            const double repaired_exit_cost = repair_cost(alns_exit_routes, "pre_final_rebalance_exit", &repaired_exit);
            if (repaired_exit_cost + kEps < repaired_best_cost) {
                repaired_best_cost = repaired_exit_cost;
                repaired_best = std::move(repaired_exit);
                selected = 1;
            }
        }
        if (params.valid_rebalance_tracking && std::isfinite(valid_tracked_cost)) {
            RouteSet repaired_tracked;
            const double repaired_tracked_cost =
                repair_cost(valid_tracked_routes, "pre_final_rebalance_tracked", &repaired_tracked);
            if (repaired_tracked_cost + kEps < repaired_best_cost) {
                repaired_best_cost = repaired_tracked_cost;
                repaired_best = std::move(repaired_tracked);
                selected = 2;
            }
        }
        if (std::isfinite(repaired_best_cost)) {
            best_routes = std::move(repaired_best);
            best_cost = repaired_best_cost;
        }
        meta.SetInt("pre_final_rebalance_selected", selected);
        meta.SetDouble("pre_final_rebalance_selected_cost", repaired_best_cost);
    }

    augment_route_candidates(best_routes, "route_candidate_final_aug");

    // ---- Phase 5.5: cross-route geometric polish (MINSUM only) ----
    // Runs the FILO2-like granular operators (2-opt*, or-opt cross-route,
    // route-pair endpoint 2-opt*, plus a relocate/swap pass) on the current
    // best solution as a strict-improve loop. Activated whenever ANY of the
    // four operators is enabled by autotune or CLI override. The motivation:
    // for the n in (12k, 60k] MINSUM bracket autotune selects PT (4 replicas)
    // which runs through DoOneAlnsStep — that path has no granular passes, so
    // the visible "tangled boundaries between routes" pattern survives the
    // ALNS phase. This phase lets the cross-route operators clean up that
    // geometry on the global best, regardless of single-rep vs PT mode.
    // Time-budgeted as a fraction of the ALNS budget remainder; never starves
    // phase 6 because final_ms is already reserved.
    const bool cross_polish_enabled = !accept.IsMinMax() && (
        (params.granular_2optstar_every > 0 && params.granular_2optstar_max_moves > 0 &&
         params.granular_2optstar_scan_customers > 0) ||
        (params.granular_oropt_every > 0 && params.granular_oropt_max_moves > 0 &&
         params.granular_oropt_scan_customers > 0) ||
        (params.route_pair_2optstar_every > 0 && params.route_pair_2optstar_max_moves > 0) ||
        (params.granular_every > 0 && params.granular_max_moves > 0 &&
         params.granular_scan_customers > 0));
    if (cross_polish_enabled && !budget.ForceCheck()) {
        const auto t_phase55 = std::chrono::steady_clock::now();
        const int phase55_ms = std::max(150, std::min(budget.RemainingMs() - final_ms,
                                                       budget.RemainingMs() / 6));
        if (phase55_ms > 0) {
            SearchBudget cross_budget = budget.SubBudget(phase55_ms);
            RouteList rl_polish(n, m);
            rl_polish.LoadFrom(best_routes, d);

            // Balance guard. The 2-opt* / route-pair operators move entire
            // route tails — repeatedly applied they can empty routes entirely
            // OR shift most customers into one mega-route. Pure MINSUM math
            // may favor that (Steiner-like collapse through depot), but mTSP
            // semantics expect m roughly-balanced routes. We snapshot before
            // each operator pass and revert if it (a) produced a new empty
            // route or (b) drove the smallest route below `min_route_floor`,
            // a fraction of the average route size. This is a soft constraint:
            // moves that shrink the smallest route within the floor still go
            // through, only catastrophic imbalances are rejected.
            const auto count_empty_rl = [](const RouteList& rl) {
                int empty = 0;
                for (int r = 0; r < rl.RouteCount(); ++r) {
                    if (rl.RouteSize(r) <= 0) ++empty;
                }
                return empty;
            };
            const auto min_route_size_rl = [](const RouteList& rl) {
                int mn = std::numeric_limits<int>::max();
                for (int r = 0; r < rl.RouteCount(); ++r) {
                    mn = std::min(mn, rl.RouteSize(r));
                }
                return mn;
            };
            const int initial_empty = count_empty_rl(rl_polish);
            const int initial_min_size = min_route_size_rl(rl_polish);
            // Balance floor: smallest route may shrink by at most ~30% from
            // its initial size. This preserves visually-balanced partitions
            // (the user's reference image had route sizes within ~9% of each
            // other and we want to keep that property after cross-route
            // polish). 2-opt* swaps entire tails — without this floor a single
            // accepted swap can shift thousands of customers and produce
            // 5x:1 imbalance even though no route went empty. avg=(n-1)/m
            // serves as an absolute hard floor (we never let any route fall
            // below avg/4 regardless of the relative ratio).
            const int avg_route_size = (m > 0) ? std::max(1, (n - 1) / m) : 1;
            const int balance_floor_relative = std::max(1, (initial_min_size * 7) / 10);
            const int balance_floor_absolute = std::max(1, avg_route_size / 4);
            const int balance_floor = std::max(balance_floor_relative, balance_floor_absolute);
            const auto guard = [&](auto&& fn) -> int {
                RouteSet snap;
                rl_polish.StoreTo(snap);
                const int moved = fn();
                const bool empty_grew = count_empty_rl(rl_polish) > initial_empty;
                const bool below_floor = min_route_size_rl(rl_polish) < balance_floor;
                if (empty_grew || below_floor) {
                    rl_polish.LoadFrom(snap, d);
                    return 0;
                }
                return moved;
            };

            int xpolish_relocate = 0;
            int xpolish_2optstar = 0;
            int xpolish_oropt = 0;
            int xpolish_routepair = 0;
            int xpolish_intra = 0;
            int xpolish_rounds = 0;
            int xpolish_collapse_reverts = 0;
            GranularMoveStats xstats;
            std::mt19937 xrng(seed ^ 0x5EE5C0D3u);

            const int relocate_caps_max = std::max(params.granular_max_moves, 4);
            const int relocate_scan = std::max(params.granular_scan_customers, 256);
            const int two_optstar_max = std::max(params.granular_2optstar_max_moves, 2);
            const int two_optstar_scan = std::max(params.granular_2optstar_scan_customers, 256);
            const int oropt_max = std::max(params.granular_oropt_max_moves, 2);
            const int oropt_scan = std::max(params.granular_oropt_scan_customers, 256);
            const int oropt_len = std::clamp(params.granular_oropt_max_len, 2, 4);
            const int rp_k = std::max(params.route_pair_2optstar_k, 2);
            const int rp_window = std::max(params.route_pair_2optstar_window, 4);
            const int rp_max = std::max(params.route_pair_2optstar_max_moves, 2);
            const int rp_pairs = params.route_pair_2optstar_max_pairs;

            const int max_rounds = 24;
            for (int round = 0; round < max_rounds && !cross_budget.ForceCheck(); ++round) {
                bool moved_any = false;
                ++xpolish_rounds;
                if (params.granular_every > 0 && params.granular_max_moves > 0 &&
                    params.granular_scan_customers > 0) {
                    SearchBudget bsub = cross_budget.SubBudget(std::max(50, cross_budget.RemainingMs() / 16));
                    const int snap_empty = count_empty_rl(rl_polish);
                    const int moved = guard([&]() {
                        return TryGranularInterRoutePass(rl_polish, accept, d, global,
                                                         bsub, xrng, relocate_caps_max,
                                                         relocate_scan,
                                                         params.route_cap, &xstats,
                                                         params.granular_endpoint_bias_depth);
                    });
                    if (count_empty_rl(rl_polish) == snap_empty) xpolish_relocate += moved;
                    else ++xpolish_collapse_reverts;
                    moved_any = moved_any || moved > 0;
                }
                if (params.granular_2optstar_every > 0 &&
                    params.granular_2optstar_max_moves > 0 &&
                    params.granular_2optstar_scan_customers > 0) {
                    SearchBudget bsub = cross_budget.SubBudget(std::max(50, cross_budget.RemainingMs() / 16));
                    const int snap_empty = count_empty_rl(rl_polish);
                    const int moved = guard([&]() {
                        return TryGranularTwoOptStarPass(rl_polish, accept, d, global,
                                                         bsub, xrng, two_optstar_max,
                                                         two_optstar_scan, &xstats,
                                                         params.granular_endpoint_bias_depth);
                    });
                    if (count_empty_rl(rl_polish) == snap_empty) xpolish_2optstar += moved;
                    else ++xpolish_collapse_reverts;
                    moved_any = moved_any || moved > 0;
                }
                if (params.granular_oropt_every > 0 &&
                    params.granular_oropt_max_moves > 0 &&
                    params.granular_oropt_scan_customers > 0) {
                    SearchBudget bsub = cross_budget.SubBudget(std::max(50, cross_budget.RemainingMs() / 16));
                    const int snap_empty = count_empty_rl(rl_polish);
                    const int moved = guard([&]() {
                        return TryGranularOrOptPass(rl_polish, accept, d, global,
                                                    bsub, xrng, oropt_max, oropt_scan,
                                                    params.route_cap, oropt_len, &xstats,
                                                    params.granular_endpoint_bias_depth);
                    });
                    if (count_empty_rl(rl_polish) == snap_empty) xpolish_oropt += moved;
                    else ++xpolish_collapse_reverts;
                    moved_any = moved_any || moved > 0;
                }
                if (params.route_pair_2optstar_every > 0 &&
                    params.route_pair_2optstar_max_moves > 0) {
                    SearchBudget bsub = cross_budget.SubBudget(std::max(50, cross_budget.RemainingMs() / 16));
                    const int snap_empty = count_empty_rl(rl_polish);
                    const int moved = guard([&]() {
                        return BuildAndRunRoutePair2OptStar(rl_polish, accept, d, bsub,
                                                            rp_k, rp_pairs, rp_max,
                                                            rp_window, &xstats);
                    });
                    if (count_empty_rl(rl_polish) == snap_empty) xpolish_routepair += moved;
                    else ++xpolish_collapse_reverts;
                    moved_any = moved_any || moved > 0;
                }
                if (moved_any && !cross_budget.ForceCheck()) {
                    // Cheap intra polish on dirty routes only — cross-route
                    // moves often leave a route's local order suboptimal.
                    RouteIndex idx(n);
                    SearchBudget bsub = cross_budget.SubBudget(std::max(50, cross_budget.RemainingMs() / 8));
                    for (int r = 0; r < rl_polish.RouteCount(); ++r) {
                        if (!rl_polish.IsDirty(r) || bsub.ForceCheck()) continue;
                        auto route_copy = rl_polish.Route(r);
                        NeighborList2Opt(route_copy, global, d, bsub, idx);
                        rl_polish.ReplaceRoute(r, std::move(route_copy), d);
                        ++xpolish_intra;
                    }
                    rl_polish.ClearDirty();
                }
                if (!moved_any) break;
            }

            RouteSet polished;
            rl_polish.StoreTo(polished);
            EnsureClosedDepot(polished);
            const double polished_cost = accept.ScalarCostOfRoutes(polished, d);
            // Final guard: even with the per-pass anti-collapse check, accept
            // the polished result only if the empty-route count did not grow
            // beyond what the pre-phase-5.5 best already had. This is belt-
            // and-suspenders against any operator path we missed.
            const int polished_empty = CountEmptyRoutes(polished);
            const int best_empty = CountEmptyRoutes(best_routes);
            // Final size-floor check: smallest route in polished must not be
            // below the balance floor. If best_routes already had a small
            // route, the per-pass guard would have used that as the floor —
            // we re-check here with the same threshold.
            int polished_min_size = std::numeric_limits<int>::max();
            for (const auto& r : polished) {
                const int sz = static_cast<int>(r.size()) >= 2
                              ? static_cast<int>(r.size()) - 2 : 0;
                polished_min_size = std::min(polished_min_size, sz);
            }
            if (polished_min_size == std::numeric_limits<int>::max()) polished_min_size = 0;
            const bool empty_ok = polished_empty <= best_empty;
            const bool size_ok = polished_min_size >= balance_floor;
            const bool will_accept = empty_ok && size_ok && polished_cost + kEps < best_cost;
            if (will_accept) {
                best_routes = std::move(polished);
                best_cost = polished_cost;
                meta.EmitAnytimeBest(best_cost);
            }
            meta.SetInt("phase55_ms", ElapsedMs(t_phase55));
            meta.SetInt("phase55_rounds", xpolish_rounds);
            meta.SetInt("phase55_relocate_moves", xpolish_relocate);
            meta.SetInt("phase55_2optstar_moves", xpolish_2optstar);
            meta.SetInt("phase55_oropt_moves", xpolish_oropt);
            meta.SetInt("phase55_routepair_moves", xpolish_routepair);
            meta.SetInt("phase55_intra_polishes", xpolish_intra);
            meta.SetInt("phase55_collapse_reverts", xpolish_collapse_reverts);
            meta.SetInt("phase55_polished_empty", polished_empty);
            meta.SetInt("phase55_polished_min_size", polished_min_size);
            meta.SetInt("phase55_balance_floor", balance_floor);
            meta.SetInt("phase55_initial_min_size", initial_min_size);
            meta.SetInt("phase55_accepted", will_accept ? 1 : 0);
            meta.SetInt("phase55_relocate_accepts", xstats.relocate_accepts);
            meta.SetInt("phase55_swap_accepts", xstats.swap_accepts);
            meta.SetInt("phase55_2optstar_accepts", xstats.two_optstar_accepts);
            meta.SetInt("phase55_oropt_accepts", xstats.oropt_accepts);
            meta.SetInt("phase55_routepair_accepts", xstats.route_pair_2optstar_accepts);
            meta.SetDouble("phase55_cost", polished_cost);
            meta.SetInt("phase55_enabled", 1);
        }
    } else {
        meta.SetInt("phase55_enabled", 0);
    }

    // ---- Phase 6: final polish ----
    const auto t_phase6 = std::chrono::steady_clock::now();
    SearchBudget final_budget = budget.SubBudget(final_ms);
    current = best_routes;
    EnsureClosedDepot(current);
    ParallelFinal2Opt(current, global, raw_dist, final_budget, n);
    const double final_cost = accept.ScalarCostOfRoutes(current, d);
    if (final_cost + kEps < best_cost) {
        best_routes = current;
        best_cost = final_cost;
        meta.EmitAnytimeBest(best_cost);
    }
    meta.SetInt("phase6_ms", ElapsedMs(t_phase6));

    out = best_routes;
    EnsureClosedDepot(out);
    if (!ValidateRoutes(out, n)) {
        // Fallback: replace with the post-polish current solution (pre-ALNS),
        // which is guaranteed valid because seeds were sanitized.
        out = current;
        EnsureClosedDepot(out);
    }
    meta.Set("validation_ok", ValidateRoutes(out, n) ? "true" : "false");
    meta.SetInt("total_elapsed_ms", ElapsedMs(t0));
    meta.SetDouble("final_cost_scalar", accept.ScalarCostOfRoutes(out, d));
    meta.SetDouble("final_sum", RouteSumLength(out, d));
    meta.SetDouble("final_max", MaxRouteLength(out, d));
    meta.FlushAnytimeToData();
}

}  // namespace mtsp::v21
