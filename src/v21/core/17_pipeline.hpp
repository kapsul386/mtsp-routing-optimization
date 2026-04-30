#pragma once

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

#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <chrono>
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

    void Set(const std::string& k, const std::string& v) { data[k] = v; }
    void SetInt(const std::string& k, long long v) { data[k] = std::to_string(v); }
    void SetDouble(const std::string& k, double v) { data[k] = std::to_string(v); }

    void StartAnytime() {
        anytime_t0 = std::chrono::steady_clock::now();
        anytime_trace.clear();
        anytime_started = true;
    }

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
                   EdgePenalties* gls = nullptr) {
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
    const int seg_size = 100;
    const int gls_penalize_after = params.gls_penalize_after;
    const int gls_penalize_K = 3;
    const double gls_decay_on_best = 0.5;
    const int pair_reopt_every = params.pair_reopt_every;
    const int popmusic_every = params.popmusic_every;
    const int popmusic_K = params.popmusic_K;
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

    // Effective budget (small reserve for final cleanup)
    const int reserve_ms = std::max(200, time_budget_ms / 50);
    SearchBudget budget(time_budget_ms, reserve_ms);

    // Resolve sub-budgets in ms
    const int total_ms = (time_budget_ms > 0) ? (time_budget_ms - reserve_ms) : 60000;
    const int seed_ms = std::max(50, total_ms * params.budget_seed_pct / 100);
    const int cand_ms = std::max(50, total_ms * params.budget_cand_pct / 100);
    const int polish_ms = std::max(100, total_ms * params.budget_polish_pct / 100);
    const int alns_ms = std::max(100, total_ms * params.budget_alns_pct / 100);
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
    RouteSet current = PickBestSeed(seeds, d, accept);
    if (current.empty()) current = seeds.front();
    EnsureClosedDepot(current);
    meta.SetDouble("seed_chosen_cost", accept.ScalarCostOfRoutes(current, d));
    meta.SetInt("phase2_ms", ElapsedMs(t_phase2));

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
    // Initial anytime point: cost of the post-polish solution (pre-ALNS baseline).
    meta.EmitAnytimeBest(best_cost);

    // GLS lambda based on average real edge cost — same scale for both branches.
    const double polish_real_sum = RouteSumLength(current, d);
    const double gls_lambda = SuggestGlsLambda(polish_real_sum, n, m, /*alpha=*/0.10);

    const int K_pt = std::max(1, params.pt_replicas);
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
                      params, n, best_routes, best_cost, meta, &gls);
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
