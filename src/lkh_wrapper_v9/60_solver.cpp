// Successor to the modular v8 wrapper (see src/lkh_wrapper_v8/). Same module
// layout (00..60); v9 collapsed 10_candidate_sets into 00_common and tuned
// the cluster model. Last of the 00..60-style modular wrappers before the
// pipeline was condensed back into a single .cpp (v10..v20) and finally
// re-modularized as src/v21/core/ with header-only modules (00..21) plus
// objective-specific entry points. Registered as "lkh-wrapper-v9".

namespace mtsp {

class LkhWrapperSolverV9 : public Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
            local_rng_.seed(seed_ ^ 0xA511E9B3U);
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("local-candidate-count")) {
            local_candidate_count_ = std::max(4, std::stoi(opts.at("local-candidate-count")));
        }
        if (opts.count("global-candidate-count")) {
            global_candidate_count_ = std::max(8, std::stoi(opts.at("global-candidate-count")));
        }
        if (opts.count("time-budget-ms")) {
            time_budget_ms_ = std::max(0, std::stoi(opts.at("time-budget-ms")));
        }
        if (opts.count("reserve-budget-ms")) {
            reserve_budget_ms_ = std::max(0, std::stoi(opts.at("reserve-budget-ms")));
        }
        if (opts.count("guided-cleanup-passes")) {
            guided_cleanup_passes_ = std::max(0, std::stoi(opts.at("guided-cleanup-passes")));
        }
        if (opts.count("inter-route-batch")) {
            inter_route_batch_ = std::max(1, std::stoi(opts.at("inter-route-batch")));
        }
        if (opts.count("exact-candidate-threshold")) {
            exact_candidate_threshold_ = std::max(32, std::stoi(opts.at("exact-candidate-threshold")));
        }
        if (opts.count("popmusic-solutions")) {
            popmusic_solutions_ = std::max(0, std::stoi(opts.at("popmusic-solutions")));
        }
        if (opts.count("popmusic-sample-size")) {
            popmusic_sample_size_ = std::max(8, std::stoi(opts.at("popmusic-sample-size")));
        }
        if (opts.count("popmusic-window")) {
            popmusic_window_ = std::max(8, std::stoi(opts.at("popmusic-window")));
        }
        if (opts.count("route-size-slack")) {
            route_size_slack_ = std::max(0.0, std::stod(opts.at("route-size-slack")));
        }
        if (opts.count("cluster-relocate-passes")) {
            cluster_relocate_passes_ = std::max(0, std::stoi(opts.at("cluster-relocate-passes")));
        }
        if (opts.count("cluster-count")) {
            forced_cluster_count_ = std::max(0, std::stoi(opts.at("cluster-count")));
        }
        if (opts.count("lookahead-weight")) {
            lookahead_weight_ = std::stod(opts.at("lookahead-weight"));
        }
        if (opts.count("depot-weight")) {
            depot_weight_ = std::stod(opts.at("depot-weight"));
        }
        if (opts.count("first-solution-budget-ms")) {
            first_solution_budget_ms_ = std::max(1, std::stoi(opts.at("first-solution-budget-ms")));
        }
        if (opts.count("improvement-budget-ms")) {
            improvement_budget_ms_ = std::max(1, std::stoi(opts.at("improvement-budget-ms")));
        }
        if (opts.count("threads")) {
            thread_count_ = std::max(0, std::stoi(opts.at("threads")));
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
#ifdef _OPENMP
        if (thread_count_ > 0) {
            omp_set_num_threads(thread_count_);
        }
#endif
        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const bool unlimited = time_budget_ms_ <= 0;
        const int effective_total_ms = unlimited
            ? 0
            : std::max(1, time_budget_ms_ - std::max(0, reserve_budget_ms_));
        const int improve_phase_ms = unlimited
            ? 0
            : std::clamp(improvement_budget_ms_, 1, std::max(1, effective_total_ms / 2));
        const int first_phase_ms = unlimited
            ? 0
            : std::clamp(first_solution_budget_ms_, 1, std::max(1, effective_total_ms - improve_phase_ms));

        SearchBudgetV5 first_budget(first_phase_ms, 0, 32);
        SearchBudgetV5 improve_budget(improve_phase_ms, 0, 32);

        const int node_count = inst.GetNodeCount();
        const int effective_local_candidates = EffectiveLocalCandidateCount(node_count);
        const int effective_global_candidates = EffectiveGlobalCandidateCount(node_count, effective_local_candidates);
        const int cheap_global_candidates = std::max(effective_local_candidates + 2, 10);
        const int effective_rounds = EffectiveRounds(node_count);
        const int effective_popmusic_solutions = EffectivePopmusicSolutions(node_count);
        const int effective_popmusic_sample = EffectivePopmusicSampleSize(node_count);
        const int effective_popmusic_window = EffectivePopmusicWindow(node_count);

        // Phase 1 (target: <=200 seconds by default): produce the first already-good MTSP solution.
        CandidateSets global_candidates = BuildGeometricCandidatesV5(inst, cheap_global_candidates, exact_candidate_threshold_);
        CandidateSets local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);
        BuildFastSeedRoutesV7(out, inst, global_candidates, distance, first_budget, route_size_slack_, lookahead_weight_, depot_weight_);

        ClusterModelV6 cluster_model;
        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1000) {
            const int cluster_count = forced_cluster_count_ > 0
                ? std::min(forced_cluster_count_, std::max(1, node_count - 1))
                : DesiredClusterCountV6(node_count, inst.GetSalesmanCount());
            const int cluster_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 18000 : (node_count >= 50000 ? 10000 : 7000),
                           std::max(500, first_budget.RemainingMs() / 5));
            SearchBudgetV5 cluster_budget(cluster_phase_ms, 0, 16);
            cluster_model = BuildLightweightClustersV6(inst, cluster_count, rng, cluster_budget);

            if (!cluster_model.clusters.empty() && !first_budget.ShouldStop() && first_budget.RemainingMs() > 1000) {
                mtsp::RouteSet cluster_routes;
                const int route_phase_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 15000 : (node_count >= 50000 ? 9000 : 6000),
                               std::max(500, first_budget.RemainingMs() / 6));
                SearchBudgetV5 route_budget(route_phase_ms, 0, 16);
                BuildInitialRoutesClusterAwareV6(cluster_routes, cluster_model, inst, distance, route_budget, rng, route_size_slack_);
                SanitizeAndCompleteRoutesV7(cluster_routes, inst, distance, route_size_slack_);
                SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
                const double old_max = MaxRouteLengthV7(out, distance);
                const double new_max = MaxRouteLengthV7(cluster_routes, distance);
                if (new_max + kEps < old_max || RouteSumLengthV7(cluster_routes, distance) + kEps < RouteSumLengthV7(out, distance)) {
                    out.swap(cluster_routes);
                }
            }
        }

        // Still in phase 1: enrich candidates lightly, so the first solution is not just a baseline greedy route.
        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1500) {
            const int enrichment_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 35000 : (node_count >= 50000 ? 22000 : 12000),
                           std::max(1000, first_budget.RemainingMs() / 3));
            SearchBudgetV5 enrich_budget(enrichment_phase_ms, 250, 16);
            global_candidates = BuildHybridCandidateSetsV8(inst,
                                                           effective_global_candidates,
                                                           EffectiveGeometricCandidateCount(node_count, effective_global_candidates),
                                                           exact_candidate_threshold_,
                                                           effective_popmusic_solutions,
                                                           effective_popmusic_sample,
                                                           effective_popmusic_window,
                                                           rng,
                                                           distance,
                                                           enrich_budget);
        }

        if (!cluster_model.clusters.empty()) {
            AugmentCandidatesWithClusterBridgesV6(global_candidates,
                                                  inst,
                                                  cluster_model,
                                                  std::max(effective_global_candidates + 4, effective_global_candidates));
        }
        local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);

        if (!cluster_model.clusters.empty() && !first_budget.ShouldStop()) {
            const int rebalance_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 16000 : (node_count >= 50000 ? 9000 : 5000),
                           std::max(500, first_budget.RemainingMs() / 8));
            SearchBudgetV5 rebalance_budget(rebalance_phase_ms, 0, 16);
            SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
            for (auto& route : out) {
                if (!route.empty() && route.back() == 0) {
                    route.pop_back();
                }
            }
            RebalanceOpenRoutesClusterAwareV6(out, cluster_model, distance, rebalance_budget, cluster_relocate_passes_);
        }

        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
        const int first_phase_rounds = std::max(1, effective_rounds - 1);
        for (auto& route : out) {
            if (first_budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV5(route, rng, first_phase_rounds, local_candidates, node_count, distance, first_budget);
        }
        if (!first_budget.ShouldStop()) {
            ImproveInterRoute(out, local_candidates, global_candidates, std::max(1, effective_rounds / 2), distance, first_budget);
        }
        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);

        mtsp::RouteSet best_routes = out;
        double best_max = MaxRouteLengthV7(best_routes, distance);
        double best_sum = RouteSumLengthV7(best_routes, distance);

        // Phase 2 (target: <=100 seconds by default): spend the remaining budget on heavier improvement only.
        if (unlimited || improve_phase_ms > 0) {
            if (!improve_budget.ShouldStop() && improve_budget.RemainingMs() > 1000) {
                const int richer_pop_solutions = node_count >= 100000
                    ? std::max(effective_popmusic_solutions, 6)
                    : std::max(effective_popmusic_solutions, 8);
                const int improve_enrichment_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 30000 : (node_count >= 50000 ? 22000 : 15000),
                               std::max(1000, improve_budget.RemainingMs() / 3));
                SearchBudgetV5 improve_enrich_budget(improve_enrichment_ms, 250, 16);
                global_candidates = BuildHybridCandidateSetsV8(inst,
                                                               effective_global_candidates,
                                                               EffectiveGeometricCandidateCount(node_count, effective_global_candidates),
                                                               exact_candidate_threshold_,
                                                               richer_pop_solutions,
                                                               effective_popmusic_sample,
                                                               effective_popmusic_window,
                                                               rng,
                                                               distance,
                                                               improve_enrich_budget);
                if (!cluster_model.clusters.empty()) {
                    AugmentCandidatesWithClusterBridgesV6(global_candidates,
                                                          inst,
                                                          cluster_model,
                                                          std::max(effective_global_candidates + 4, effective_global_candidates));
                }
                local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);
            }

            if (!cluster_model.clusters.empty() && !improve_budget.ShouldStop()) {
                mtsp::RouteSet open_routes = best_routes;
                for (auto& route : open_routes) {
                    if (!route.empty() && route.back() == 0) {
                        route.pop_back();
                    }
                }
                const int rebalance_phase_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 12000 : (node_count >= 50000 ? 8000 : 5000),
                               std::max(500, improve_budget.RemainingMs() / 8));
                SearchBudgetV5 rebalance_budget(rebalance_phase_ms, 0, 16);
                RebalanceOpenRoutesClusterAwareV6(open_routes, cluster_model, distance, rebalance_budget, cluster_relocate_passes_ + 1);
                SanitizeAndCompleteRoutesV7(open_routes, inst, distance, route_size_slack_);
                const double candidate_max = MaxRouteLengthV7(open_routes, distance);
                const double candidate_sum = RouteSumLengthV7(open_routes, distance);
                if (candidate_max + kEps < best_max ||
                    (std::abs(candidate_max - best_max) <= kEps && candidate_sum + kEps < best_sum)) {
                    best_routes.swap(open_routes);
                    best_max = candidate_max;
                    best_sum = candidate_sum;
                }
            }

            mtsp::RouteSet candidate_routes = best_routes;
            for (auto& route : candidate_routes) {
                if (improve_budget.ShouldStop()) {
                    break;
                }
                IteratedLocalSearchV5(route, rng, effective_rounds, local_candidates, node_count, distance, improve_budget);
            }
            if (!improve_budget.ShouldStop()) {
                ImproveInterRoute(candidate_routes, local_candidates, global_candidates, effective_rounds, distance, improve_budget);
            }
            SanitizeAndCompleteRoutesV7(candidate_routes, inst, distance, route_size_slack_);

            const double candidate_max = MaxRouteLengthV7(candidate_routes, distance);
            const double candidate_sum = RouteSumLengthV7(candidate_routes, distance);
            if (candidate_max + kEps < best_max ||
                (std::abs(candidate_max - best_max) <= kEps && candidate_sum + kEps < best_sum)) {
                best_routes.swap(candidate_routes);
                best_max = candidate_max;
                best_sum = candidate_sum;
            }
        }

        out.swap(best_routes);
        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
    }

private:
    int EffectiveLocalCandidateCount(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(local_candidate_count_, 6, 8);
        }
        if (node_count >= 50000) {
            return std::clamp(local_candidate_count_, 7, 8);
        }
        return std::clamp(local_candidate_count_, 8, 10);
    }

    int EffectiveGlobalCandidateCount(int node_count, int local_count) const {
        if (node_count >= 100000) {
            return std::max(12, std::max(global_candidate_count_, local_count + 4));
        }
        if (node_count >= 50000) {
            return std::max(13, std::max(global_candidate_count_, local_count + 5));
        }
        return std::max(14, std::max(global_candidate_count_, local_count + 6));
    }

    int EffectiveGeometricCandidateCount(int node_count, int final_candidate_count) const {
        if (node_count >= 100000) {
            return std::max(final_candidate_count * 2, 22);
        }
        if (node_count >= 50000) {
            return std::max(final_candidate_count * 2, 20);
        }
        return std::max(final_candidate_count * 2, 18);
    }

    int EffectiveRounds(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(rounds_, 2, 3);
        }
        if (node_count >= 50000) {
            return std::clamp(rounds_, 3, 4);
        }
        return std::clamp(rounds_, 4, 6);
    }

    int EffectivePopmusicSolutions(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(popmusic_solutions_, 4, 6);
        }
        if (node_count >= 50000) {
            return std::clamp(popmusic_solutions_, 5, 8);
        }
        return std::clamp(popmusic_solutions_, 6, 10);
    }

    int EffectivePopmusicSampleSize(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(popmusic_sample_size_, 16, 24);
        }
        if (node_count >= 50000) {
            return std::clamp(popmusic_sample_size_, 18, 28);
        }
        return std::clamp(popmusic_sample_size_, 20, 32);
    }

    int EffectivePopmusicWindow(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(popmusic_window_, 16, 24);
        }
        if (node_count >= 50000) {
            return std::clamp(popmusic_window_, 18, 28);
        }
        return std::clamp(popmusic_window_, 20, 32);
    }

    void RunDeferredInterRouteIls(RouteSet& routes,
                                  std::vector<char>& dirty_routes,
                                  const CandidateSets& local_candidates,
                                  int effective_rounds,
                                  DistanceOracleV5& distance,
                                  SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            if (!dirty_routes[route_idx] || budget.ShouldStop()) {
                continue;
            }
            IteratedLocalSearchV5(routes[route_idx],
                                  local_rng_,
                                  std::max(2, effective_rounds / 2),
                                  local_candidates,
                                  inst.GetNodeCount(),
                                  distance,
                                  budget);
            dirty_routes[route_idx] = 0;
        }
    }

    void ImproveInterRoute(RouteSet& routes,
                           const CandidateSets& local_candidates,
                           const CandidateSets& global_candidates,
                           int effective_rounds,
                           DistanceOracleV5& distance,
                           SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        std::vector<char> dirty_routes(routes.size(), 0);
        std::vector<RouteIndexV5> indices;
        indices.reserve(routes.size());
        for (size_t i = 0; i < routes.size(); ++i) {
            indices.emplace_back(inst.GetNodeCount());
        }

        int accepted_since_ils = 0;
        bool improved = true;
        while (improved && !budget.ShouldStop()) {
            improved = false;

            if (TryBalancedRelocateV6(routes, global_candidates, distance, budget)) {
                for (auto& route : routes) {
                    QuickRouteCleanupV5(route, local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                }
                std::fill(dirty_routes.begin(), dirty_routes.end(), 1);
                ++accepted_since_ils;
                improved = true;
            }

            for (size_t a = 0; a < routes.size() && !budget.ShouldStop(); ++a) {
                indices[a].Build(routes[a]);
            }

            for (size_t a = 0; a < routes.size() && !improved; ++a) {
                for (size_t b = a + 1; b < routes.size() && !improved; ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size() && !improved; ++i) {
                        if (budget.ShouldStop()) {
                            break;
                        }
                        const int city_a = routes[a][i];
                        for (int city_b : global_candidates[static_cast<size_t>(city_a)]) {
                            const int j = indices[b].Get(city_b);
                            if (j <= 0 || j + 1 >= static_cast<int>(routes[b].size())) {
                                continue;
                            }
                            const double delta = SwapDeltaClosedRoutesV5(routes[a], i, routes[b], static_cast<size_t>(j), distance);
                            if (delta >= -kEps) {
                                continue;
                            }

                            std::swap(routes[a][i], routes[b][static_cast<size_t>(j)]);
                            QuickRouteCleanupV5(routes[a], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                            QuickRouteCleanupV5(routes[b], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                            dirty_routes[a] = 1;
                            dirty_routes[b] = 1;
                            ++accepted_since_ils;
                            improved = true;
                            break;
                        }
                    }
                }
            }

            if (accepted_since_ils >= inter_route_batch_ && !budget.ShouldStop()) {
                RunDeferredInterRouteIls(routes, dirty_routes, local_candidates, effective_rounds, distance, budget);
                accepted_since_ils = 0;
            }
        }

        if (accepted_since_ils > 0 && !budget.ShouldStop()) {
            RunDeferredInterRouteIls(routes, dirty_routes, local_candidates, effective_rounds, distance, budget);
        }
    }

    unsigned int seed_ = 42U;
    int rounds_ = 4;
    int local_candidate_count_ = 8;
    int global_candidate_count_ = 14;
    int time_budget_ms_ = 300000;
    int reserve_budget_ms_ = 0;
    int guided_cleanup_passes_ = 2;
    int inter_route_batch_ = 3;
    int exact_candidate_threshold_ = 512;
    int popmusic_solutions_ = 12;
    int popmusic_sample_size_ = 32;
    int popmusic_window_ = 32;
    double route_size_slack_ = 0.15;
    int cluster_relocate_passes_ = 2;
    int forced_cluster_count_ = 0;
    double lookahead_weight_ = 0.35;
    double depot_weight_ = 0.12;
    int first_solution_budget_ms_ = 200000;
    int improvement_budget_ms_ = 100000;
    int thread_count_ = 0;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v9 = (SolverFactory::RegisterSolver("lkh-wrapper-v9", []() {
    return std::make_unique<LkhWrapperSolverV9>();
}),
                               true);

} // namespace mtsp
