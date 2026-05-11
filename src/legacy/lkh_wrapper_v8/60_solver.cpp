// v8/60_solver.cpp — orchestration of all phases: seed → rebalance → candidate-set →
// route-local-search → inter-route swap → final polish. Registers the solver
// in SolverFactory under the name "lkh-wrapper-v8". Per the source comments:
// "first modular wrapper (predecessor of the src/v21/core/ architecture)".

// First modular wrapper (predecessor of the src/v21/core/ architecture). The
// directory layout numbered the modules 00..60 (00_common, 10_candidate_sets,
// 20_route_local_search, 30_cluster_model, 40_seed_routes, 50_rebalance,
// 60_solver — this file). Same pattern was kept in src/v21/core/.
// Registered as "lkh-wrapper-v8".

namespace mtsp {

class LkhWrapperSolverV8 : public Solver {
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
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        SearchBudgetV5 budget(time_budget_ms_, reserve_budget_ms_, 32);
        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const int effective_local_candidates = EffectiveLocalCandidateCount(inst.GetNodeCount());
        const int effective_global_candidates = EffectiveGlobalCandidateCount(inst.GetNodeCount(), effective_local_candidates);
        const int cheap_global_candidates = std::max(effective_local_candidates + 2, 10);
        const int effective_rounds = EffectiveRounds(inst.GetNodeCount());
        const int effective_popmusic_solutions = EffectivePopmusicSolutions(inst.GetNodeCount());
        const int effective_popmusic_sample = EffectivePopmusicSampleSize(inst.GetNodeCount());
        const int effective_popmusic_window = EffectivePopmusicWindow(inst.GetNodeCount());

        // Stage A: very cheap valid seed first.
        CandidateSets global_candidates = BuildGeometricCandidatesV5(inst, cheap_global_candidates, exact_candidate_threshold_);
        CandidateSets local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);
        BuildFastSeedRoutesV7(out, inst, global_candidates, distance, budget, route_size_slack_, lookahead_weight_, depot_weight_);

        ClusterModelV6 cluster_model;
        if (!budget.ShouldStop() && budget.RemainingMs() > 1000) {
            const int cluster_count = forced_cluster_count_ > 0
                ? std::min(forced_cluster_count_, std::max(1, inst.GetNodeCount() - 1))
                : DesiredClusterCountV6(inst.GetNodeCount(), inst.GetSalesmanCount());
            const int cluster_phase_ms = std::min(inst.GetNodeCount() >= 100000 ? 15000 : (inst.GetNodeCount() >= 50000 ? 9000 : 6000),
                                                 std::max(500, budget.RemainingMs() / 6));
            SearchBudgetV5 cluster_budget(cluster_phase_ms, 0, 16);
            cluster_model = BuildLightweightClustersV6(inst, cluster_count, rng, cluster_budget);

            if (!cluster_model.clusters.empty() && !budget.ShouldStop() && budget.RemainingMs() > 1000) {
                mtsp::RouteSet cluster_routes;
                const int route_phase_ms = std::min(inst.GetNodeCount() >= 100000 ? 12000 : (inst.GetNodeCount() >= 50000 ? 8000 : 5000),
                                                   std::max(500, budget.RemainingMs() / 8));
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

        // Stage B: progressive candidate preprocessing.
        if (!budget.ShouldStop() && budget.RemainingMs() > 1000) {
            const int enrichment_phase_ms = std::min(inst.GetNodeCount() >= 100000 ? 25000 : (inst.GetNodeCount() >= 50000 ? 15000 : 10000),
                                                    std::max(1000, budget.RemainingMs() / 4));
            SearchBudgetV5 enrich_budget(enrichment_phase_ms, 0, 16);
            global_candidates = BuildHybridCandidateSetsV8(inst,
                                                           effective_global_candidates,
                                                           std::max(effective_global_candidates * 2, cheap_global_candidates + 4),
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

        if (!cluster_model.clusters.empty() && !budget.ShouldStop()) {
            const int rebalance_phase_ms = std::min(inst.GetNodeCount() >= 100000 ? 15000 : (inst.GetNodeCount() >= 50000 ? 8000 : 5000),
                                                   std::max(500, budget.RemainingMs() / 10));
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

        for (auto& route : out) {
            if (budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV5(route, rng, effective_rounds, local_candidates, inst.GetNodeCount(), distance, budget);
        }

        if (!budget.ShouldStop()) {
            ImproveInterRoute(out, local_candidates, global_candidates, effective_rounds, distance, budget);
        }

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
    int reserve_budget_ms_ = 10000;
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
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v8 = (SolverFactory::RegisterSolver("lkh-wrapper-v8", []() {
    return std::make_unique<LkhWrapperSolverV8>();
}),
                               true);

} // namespace mtsp
