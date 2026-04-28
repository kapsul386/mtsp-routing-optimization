// mtsp_lkh_wrapper_v10.cpp
// MINSUM-oriented successor to v9. It keeps the fast modular machinery from v9,
// but starts the improvement budget after the first solution and accepts
// cross-route changes by total route length, matching the reported objective.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

#include "lkh_wrapper_v9/00_common.cpp"
#include "lkh_wrapper_v8/10_candidate_sets.cpp"
#include "lkh_wrapper_v9/20_route_local_search.cpp"
#include "lkh_wrapper_v9/30_cluster_model.cpp"
#include "lkh_wrapper_v9/40_seed_routes.cpp"
#include "lkh_wrapper_v9/50_rebalance.cpp"

namespace {

int DesiredClusterCountLegacyV10(int node_count, int salesman_count) {
    const int min_clusters = std::max(4 * salesman_count, 1);
    const int max_clusters = std::max(12 * salesman_count, min_clusters);
    int desired = 6 * salesman_count + std::max(0, node_count / 12000);
    desired = std::clamp(desired, min_clusters, max_clusters);
    desired = std::min(desired, std::max(1, node_count - 1));
    return desired;
}

ClusterModelV6 BuildLightweightClustersLegacyV10(const mtsp::Instance& inst,
                                                 int desired_clusters,
                                                 std::mt19937& rng,
                                                 SearchBudgetV5& budget) {
    const auto& coords = inst.GetCoords();
    const int node_count = inst.GetNodeCount();
    ClusterModelV6 model;
    model.node_to_cluster.assign(static_cast<size_t>(node_count), -1);
    model.is_outlier.assign(static_cast<size_t>(node_count), 0);
    if (node_count <= 1) {
        return model;
    }

    const int k = std::max(1, std::min(desired_clusters, node_count - 1));
    std::vector<int> cities;
    cities.reserve(static_cast<size_t>(node_count - 1));
    for (int city = 1; city < node_count; ++city) {
        cities.push_back(city);
    }
    std::shuffle(cities.begin(), cities.end(), rng);

    std::vector<Coord> centroids;
    centroids.reserve(static_cast<size_t>(k));
    for (int i = 0; i < k; ++i) {
        centroids.push_back(coords[static_cast<size_t>(cities[static_cast<size_t>(i % cities.size())])]);
    }

    std::vector<int> assign(static_cast<size_t>(node_count), -1);
    const int iterations = node_count >= 100000 ? 4 : 5;
    for (int it = 0; it < iterations && !budget.ShouldStop(); ++it) {
        std::vector<double> sum_x(static_cast<size_t>(k), 0.0);
        std::vector<double> sum_y(static_cast<size_t>(k), 0.0);
        std::vector<int> count(static_cast<size_t>(k), 0);

        for (int city = 1; city < node_count; ++city) {
            if ((city & 255) == 0 && budget.ForceCheck()) {
                break;
            }
            double best = std::numeric_limits<double>::max();
            int best_cluster = 0;
            for (int c = 0; c < k; ++c) {
                const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], centroids[static_cast<size_t>(c)]);
                if (d2 + kEps < best) {
                    best = d2;
                    best_cluster = c;
                }
            }
            assign[static_cast<size_t>(city)] = best_cluster;
            sum_x[static_cast<size_t>(best_cluster)] += coords[static_cast<size_t>(city)].first;
            sum_y[static_cast<size_t>(best_cluster)] += coords[static_cast<size_t>(city)].second;
            ++count[static_cast<size_t>(best_cluster)];
        }

        for (int c = 0; c < k; ++c) {
            if (count[static_cast<size_t>(c)] == 0) {
                centroids[static_cast<size_t>(c)] = coords[static_cast<size_t>(cities[static_cast<size_t>(c % cities.size())])];
            } else {
                centroids[static_cast<size_t>(c)] = {
                    sum_x[static_cast<size_t>(c)] / count[static_cast<size_t>(c)],
                    sum_y[static_cast<size_t>(c)] / count[static_cast<size_t>(c)]
                };
            }
        }
    }

    std::vector<ClusterInfoV6> raw(static_cast<size_t>(k));
    for (int c = 0; c < k; ++c) {
        raw[static_cast<size_t>(c)].centroid = centroids[static_cast<size_t>(c)];
    }
    for (int city = 1; city < node_count; ++city) {
        const int cluster = assign[static_cast<size_t>(city)];
        if (cluster >= 0) {
            raw[static_cast<size_t>(cluster)].members.push_back(city);
        }
    }

    std::vector<int> remap(static_cast<size_t>(k), -1);
    for (int c = 0; c < k; ++c) {
        if (!raw[static_cast<size_t>(c)].members.empty()) {
            remap[static_cast<size_t>(c)] = static_cast<int>(model.clusters.size());
            model.clusters.push_back(std::move(raw[static_cast<size_t>(c)]));
        }
    }

    for (int city = 1; city < node_count; ++city) {
        const int old_cluster = assign[static_cast<size_t>(city)];
        if (old_cluster >= 0) {
            model.node_to_cluster[static_cast<size_t>(city)] = remap[static_cast<size_t>(old_cluster)];
        }
    }

    for (auto& cluster : model.clusters) {
        if (cluster.members.empty()) {
            continue;
        }
        double sum = 0.0;
        double sum_sq = 0.0;
        double max_dist = 0.0;
        for (int city : cluster.members) {
            const double d = DistanceCoordToPointV6(coords[static_cast<size_t>(city)], cluster.centroid);
            sum += d;
            sum_sq += d * d;
            max_dist = std::max(max_dist, d);
        }
        cluster.radius = max_dist;
        cluster.avg_dist = sum / static_cast<double>(cluster.members.size());
        const double mean_sq = sum_sq / static_cast<double>(cluster.members.size());
        cluster.std_dist = std::sqrt(std::max(0.0, mean_sq - cluster.avg_dist * cluster.avg_dist));
        cluster.depot_distance = DistanceCoordToPointV6(cluster.centroid, coords[0]);

        for (int city : cluster.members) {
            const double d = DistanceCoordToPointV6(coords[static_cast<size_t>(city)], cluster.centroid);
            if (cluster.members.size() <= 2 || d > cluster.avg_dist + 1.5 * cluster.std_dist + kEps) {
                model.is_outlier[static_cast<size_t>(city)] = 1;
                ++cluster.outlier_count;
            }
        }

        const double spread_term = cluster.radius * (1.0 + 0.15 * std::sqrt(static_cast<double>(cluster.members.size())));
        const double outlier_term = cluster.outlier_count * std::max(cluster.avg_dist, cluster.radius * 0.5);
        cluster.estimate = 2.0 * cluster.depot_distance + spread_term + 0.35 * outlier_term;
    }

    return model;
}

void BuildInitialRoutesClusterAwareLegacyV10(mtsp::RouteSet& out,
                                             const ClusterModelV6& model,
                                             const mtsp::Instance& inst,
                                             DistanceOracleV5& distance,
                                             SearchBudgetV5& budget,
                                             std::mt19937& rng,
                                             double route_size_slack) {
    const int m = inst.GetSalesmanCount();
    out.assign(static_cast<size_t>(m), std::vector<int>{0});
    if (model.clusters.empty()) {
        return;
    }

    const int target_size = std::max(1, (inst.GetNodeCount() - 1 + m - 1) / m);
    const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack))));

    std::vector<int> cluster_ids(model.clusters.size());
    std::iota(cluster_ids.begin(), cluster_ids.end(), 0);
    std::sort(cluster_ids.begin(), cluster_ids.end(), [&](int lhs, int rhs) {
        if (std::abs(model.clusters[static_cast<size_t>(lhs)].estimate - model.clusters[static_cast<size_t>(rhs)].estimate) > kEps) {
            return model.clusters[static_cast<size_t>(lhs)].estimate > model.clusters[static_cast<size_t>(rhs)].estimate;
        }
        return lhs < rhs;
    });

    std::vector<std::vector<int>> assigned_clusters(static_cast<size_t>(m));
    std::vector<double> route_load(static_cast<size_t>(m), 0.0);
    std::vector<int> route_nodes(static_cast<size_t>(m), 0);
    std::vector<int> route_outliers(static_cast<size_t>(m), 0);

    for (int cluster_id : cluster_ids) {
        if (budget.ShouldStop()) {
            break;
        }
        const auto& cluster = model.clusters[static_cast<size_t>(cluster_id)];
        int best_route = 0;
        double best_score = std::numeric_limits<double>::max();
        for (int s = 0; s < m; ++s) {
            const double projected_load = route_load[static_cast<size_t>(s)] + cluster.estimate;
            double projected_max = projected_load;
            for (int t = 0; t < m; ++t) {
                if (t == s) {
                    continue;
                }
                projected_max = std::max(projected_max, route_load[static_cast<size_t>(t)]);
            }
            const int projected_nodes = route_nodes[static_cast<size_t>(s)] + static_cast<int>(cluster.members.size());
            const double balance_penalty = 0.35 * std::max(0, projected_nodes - target_size);
            const double hard_penalty = projected_nodes > hard_max_size ? 1e9 : 0.0;
            const double outlier_penalty = 0.8 * (route_outliers[static_cast<size_t>(s)] + cluster.outlier_count);
            const double score = projected_max + balance_penalty + outlier_penalty + hard_penalty;
            if (score + kEps < best_score) {
                best_score = score;
                best_route = s;
            }
        }
        assigned_clusters[static_cast<size_t>(best_route)].push_back(cluster_id);
        route_load[static_cast<size_t>(best_route)] += cluster.estimate;
        route_nodes[static_cast<size_t>(best_route)] += static_cast<int>(cluster.members.size());
        route_outliers[static_cast<size_t>(best_route)] += cluster.outlier_count;
    }

    std::uniform_real_distribution<double> prob(0.0, 1.0);
    const auto& coords = inst.GetCoords();
    for (int s = 0; s < m; ++s) {
        auto remaining = assigned_clusters[static_cast<size_t>(s)];
        Coord current_anchor = coords[0];
        while (!remaining.empty()) {
            if (budget.ShouldStop()) {
                break;
            }
            std::vector<std::pair<double, int>> ranked;
            ranked.reserve(remaining.size());
            for (int cluster_id : remaining) {
                const auto& cluster = model.clusters[static_cast<size_t>(cluster_id)];
                const double bridge = DistanceCoordToPointV6(current_anchor, cluster.centroid);
                const double score = bridge + 0.12 * cluster.depot_distance + 0.05 * cluster.estimate;
                ranked.emplace_back(score, cluster_id);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
                if (std::abs(lhs.first - rhs.first) > kEps) {
                    return lhs.first < rhs.first;
                }
                return lhs.second < rhs.second;
            });

            int chosen_cluster = ranked.front().second;
            if (ranked.size() >= 2 && prob(rng) < 0.2) {
                const int limit = std::min<int>(3, ranked.size());
                std::uniform_int_distribution<int> pick(0, limit - 1);
                chosen_cluster = ranked[static_cast<size_t>(pick(rng))].second;
            }

            const auto it_remaining = std::find(remaining.begin(), remaining.end(), chosen_cluster);
            if (it_remaining != remaining.end()) {
                remaining.erase(it_remaining);
            }

            const Coord next_anchor = remaining.empty()
                ? coords[0]
                : model.clusters[static_cast<size_t>(remaining.front())].centroid;
            std::vector<int> block = OrderClusterNodesV6(model.clusters[static_cast<size_t>(chosen_cluster)],
                                                         current_anchor,
                                                         next_anchor,
                                                         coords,
                                                         rng);
            for (int city : block) {
                out[static_cast<size_t>(s)].push_back(city);
            }
            if (!block.empty()) {
                current_anchor = coords[static_cast<size_t>(block.back())];
            }
        }
    }
}

} // namespace

namespace mtsp {

struct RouteAnnealStatsV10 {
    double before_sum = 0.0;
    double after_sum = 0.0;
    double before_energy = 0.0;
    double after_energy = 0.0;
    int accepted_moves = 0;
    int best_updates = 0;
};

class LkhWrapperSolverV10 : public Solver {
public:
    std::unordered_map<std::string, std::string> GetLastMetadata() const override {
        return last_metadata_;
    }

    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
            local_rng_.seed(seed_ ^ 0xB5297A4DU);
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
        if (opts.count("cluster-seed-restarts")) {
            cluster_seed_restarts_ = std::max(1, std::stoi(opts.at("cluster-seed-restarts")));
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
        if (opts.count("minsum-relocate-passes")) {
            minsum_relocate_passes_ = std::max(0, std::stoi(opts.at("minsum-relocate-passes")));
        }
        if (opts.count("anneal-route-ms")) {
            anneal_route_ms_ = std::max(0, std::stoi(opts.at("anneal-route-ms")));
        }
        if (opts.count("anneal-route-iters")) {
            anneal_route_iters_ = std::max(0, std::stoi(opts.at("anneal-route-iters")));
        }
        if (opts.count("threads")) {
            thread_count_ = std::max(0, std::stoi(opts.at("threads")));
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        last_metadata_.clear();
#ifdef _OPENMP
        if (thread_count_ > 0) {
            omp_set_num_threads(thread_count_);
        }
#endif
        const auto solve_start = std::chrono::steady_clock::now();
        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const bool unlimited = time_budget_ms_ <= 0;
        const int effective_total_ms = unlimited
            ? 0
            : std::max(1, time_budget_ms_ - std::max(0, reserve_budget_ms_));
        int configured_improve_ms = unlimited
            ? 0
            : std::clamp(improvement_budget_ms_, 1, effective_total_ms);
        int configured_first_ms = unlimited
            ? 0
            : std::clamp(first_solution_budget_ms_, 1, effective_total_ms);
        const int target_improve_ms = unlimited
            ? 0
            : std::min(effective_total_ms, std::max(configured_improve_ms, effective_total_ms - configured_first_ms));
        last_metadata_["first_budget_ms"] = std::to_string(configured_first_ms);
        last_metadata_["improve_budget_ms"] = std::to_string(target_improve_ms);

        SearchBudgetV5 first_budget(configured_first_ms, 0, 32);

        const int node_count = inst.GetNodeCount();
        const int effective_local_candidates = EffectiveLocalCandidateCount(node_count);
        const int effective_global_candidates = EffectiveGlobalCandidateCount(node_count, effective_local_candidates);
        const int cheap_global_candidates = std::max(effective_local_candidates + 2, 10);
        const int effective_rounds = EffectiveRounds(node_count);
        const int effective_popmusic_solutions = EffectivePopmusicSolutions(node_count);
        const int effective_popmusic_sample = EffectivePopmusicSampleSize(node_count);
        const int effective_popmusic_window = EffectivePopmusicWindow(node_count);

        CandidateSets global_candidates = BuildGeometricCandidatesV5(inst, cheap_global_candidates, exact_candidate_threshold_);
        CandidateSets local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);
        BuildFastSeedRoutesV7(out, inst, global_candidates, distance, first_budget, route_size_slack_, lookahead_weight_, depot_weight_);

        ClusterModelV6 cluster_model;
        bool candidates_enriched = false;
        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1000) {
            const int cluster_count = forced_cluster_count_ > 0
                ? std::min(forced_cluster_count_, std::max(1, node_count - 1))
                : DesiredClusterCountLegacyV10(node_count, inst.GetSalesmanCount());
            const int cluster_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 18000 : (node_count >= 50000 ? 10000 : 7000),
                           std::max(500, first_budget.RemainingMs() / 5));
            SearchBudgetV5 cluster_budget(cluster_phase_ms, 0, 16);
            cluster_model = BuildLightweightClustersLegacyV10(inst, cluster_count, rng, cluster_budget);
            last_metadata_["cluster_count"] = std::to_string(cluster_model.clusters.size());

            if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1500) {
                const int enrichment_phase_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 80000 : (node_count >= 50000 ? 50000 : 25000),
                               std::max(1000, (first_budget.RemainingMs() * 2) / 3));
                SearchBudgetV5 enrich_budget(enrichment_phase_ms, 250, 16);
                global_candidates = BuildHybridCandidateSetsV5(inst,
                                                               effective_global_candidates,
                                                               EffectiveGeometricCandidateCount(node_count, effective_global_candidates),
                                                               exact_candidate_threshold_,
                                                               effective_popmusic_solutions,
                                                               effective_popmusic_sample,
                                                               effective_popmusic_window,
                                                               rng,
                                                               distance,
                                                               enrich_budget);
                candidates_enriched = true;
            }

            if (!cluster_model.clusters.empty() && !first_budget.ShouldStop() && first_budget.RemainingMs() > 1000) {
                mtsp::RouteSet cluster_routes;
                const int route_phase_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 80000 : (node_count >= 50000 ? 50000 : 25000),
                               std::max(1000, first_budget.RemainingMs() / 2));
                SearchBudgetV5 route_budget(route_phase_ms, 0, 16);
                const int cluster_seed_restarts = EffectiveClusterSeedRestarts(node_count);
                int completed_cluster_seed_restarts = 0;
                double best_cluster_seed_sum = std::numeric_limits<double>::max();
                double best_cluster_seed_max = std::numeric_limits<double>::max();
                for (int attempt = 0; attempt < cluster_seed_restarts && !route_budget.ShouldStop(); ++attempt) {
                    mtsp::RouteSet candidate_cluster_routes;
                    BuildInitialRoutesClusterAwareLegacyV10(candidate_cluster_routes,
                                                            cluster_model,
                                                            inst,
                                                            distance,
                                                            route_budget,
                                                            rng,
                                                            route_size_slack_);
                    SanitizeAndCompleteRoutesV7(candidate_cluster_routes, inst, distance, route_size_slack_);
                    EnsureClosedDepotRoutes(candidate_cluster_routes);
                    const double candidate_sum = RouteSumLengthV7(candidate_cluster_routes, distance);
                    const double candidate_max = MaxRouteLengthV7(candidate_cluster_routes, distance);
                    if (candidate_sum + kEps < best_cluster_seed_sum ||
                        (std::abs(candidate_sum - best_cluster_seed_sum) <= kEps && candidate_max + kEps < best_cluster_seed_max)) {
                        cluster_routes.swap(candidate_cluster_routes);
                        best_cluster_seed_sum = candidate_sum;
                        best_cluster_seed_max = candidate_max;
                    }
                    ++completed_cluster_seed_restarts;
                    if (route_budget.Enabled() && route_budget.RemainingMs() < 500) {
                        break;
                    }
                }
                SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutes(out);
                const double fast_seed_sum = RouteSumLengthV7(out, distance);
                const double cluster_seed_sum = RouteSumLengthV7(cluster_routes, distance);
                last_metadata_["cluster_seed_restarts"] = std::to_string(completed_cluster_seed_restarts);
                last_metadata_["fast_seed_sum"] = std::to_string(fast_seed_sum);
                last_metadata_["cluster_seed_sum"] = std::to_string(cluster_seed_sum);
                if (IsBetterByBalancedBootstrap(cluster_routes, out, distance)) {
                    out.swap(cluster_routes);
                    last_metadata_["cluster_seed_selected"] = "true";
                } else {
                    last_metadata_["cluster_seed_selected"] = "false";
                }
            }
        }

        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1500) {
            if (!candidates_enriched) {
                const int enrichment_phase_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 80000 : (node_count >= 50000 ? 50000 : 25000),
                               std::max(1000, (first_budget.RemainingMs() * 2) / 3));
                SearchBudgetV5 enrich_budget(enrichment_phase_ms, 250, 16);
                global_candidates = BuildHybridCandidateSetsV5(inst,
                                                               effective_global_candidates,
                                                               EffectiveGeometricCandidateCount(node_count, effective_global_candidates),
                                                               exact_candidate_threshold_,
                                                               effective_popmusic_solutions,
                                                               effective_popmusic_sample,
                                                               effective_popmusic_window,
                                                               rng,
                                                               distance,
                                                               enrich_budget);
                candidates_enriched = true;
            }
        }

        if (!cluster_model.clusters.empty()) {
            AugmentCandidatesWithClusterBridgesV6(global_candidates,
                                                  inst,
                                                  cluster_model,
                                                  std::max(effective_global_candidates + 4, effective_global_candidates));
        }
        local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);

        if (!cluster_model.clusters.empty() && !first_budget.ShouldStop()) {
            mtsp::RouteSet rebalanced = out;
            SanitizeAndCompleteRoutesV7(rebalanced, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutes(rebalanced);
            for (auto& route : rebalanced) {
                if (!route.empty() && route.back() == 0) {
                    route.pop_back();
                }
            }
            const int rebalance_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 16000 : (node_count >= 50000 ? 9000 : 5000),
                           std::max(500, first_budget.RemainingMs() / 8));
            SearchBudgetV5 rebalance_budget(rebalance_phase_ms, 0, 16);
            RebalanceOpenRoutesClusterAwareV6(rebalanced, cluster_model, distance, rebalance_budget, cluster_relocate_passes_);
            SanitizeAndCompleteRoutesV7(rebalanced, inst, distance, route_size_slack_);
            EnsureClosedDepotRoutes(rebalanced);
            if (IsBetterByBalancedBootstrap(rebalanced, out, distance)) {
                out.swap(rebalanced);
            }
        }

        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutes(out);
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
        EnsureClosedDepotRoutes(out);

        mtsp::RouteSet best_routes = out;
        double best_sum = RouteSumLengthV7(best_routes, distance);
        double best_max = MaxRouteLengthV7(best_routes, distance);
        mtsp::RouteSet best_minsum_routes = best_routes;
        double best_minsum_sum = best_sum;
        double best_minsum_max = best_max;

        const int remaining_after_first_ms = unlimited ? 0 : RemainingTotalMs(solve_start, effective_total_ms);
        const int improve_phase_ms = unlimited
            ? 0
            : std::min(target_improve_ms, remaining_after_first_ms);

        if (unlimited || improve_phase_ms > 0) {
            SearchBudgetV5 improve_budget(improve_phase_ms, 0, 32);

            if (!improve_budget.ShouldStop() && improve_budget.RemainingMs() > 1000) {
                const int richer_pop_solutions = node_count >= 100000
                    ? std::max(effective_popmusic_solutions, 6)
                    : std::max(effective_popmusic_solutions, 8);
                const int improve_enrichment_ms = unlimited
                    ? 0
                    : std::min(node_count >= 100000 ? 50000 : (node_count >= 50000 ? 35000 : 20000),
                               std::max(1000, improve_budget.RemainingMs() / 2));
                SearchBudgetV5 improve_enrich_budget(improve_enrichment_ms, 250, 16);
                global_candidates = BuildHybridCandidateSetsV5(inst,
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
                EnsureClosedDepotRoutes(open_routes);
                UpdateBestByBalanced(open_routes, best_routes, best_sum, best_max, distance);
                UpdateMinsumArchive(open_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);
            }

            const int polish_rounds = std::max(1, effective_rounds);
            for (int pass = 0; pass < polish_rounds && !improve_budget.ShouldStop(); ++pass) {
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
                EnsureClosedDepotRoutes(candidate_routes);

                const double before_balanced_sum = best_sum;
                const double before_minsum = best_minsum_sum;
                UpdateBestByBalanced(candidate_routes, best_routes, best_sum, best_max, distance);
                UpdateMinsumArchive(candidate_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);
                if (!(best_sum + kEps < before_balanced_sum) && !(best_minsum_sum + kEps < before_minsum)) {
                    break;
                }
            }
        }

        UpdateMinsumArchive(best_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);

        const int route_anneal_ms = EffectiveRouteAnnealMs(node_count);
        const int route_anneal_iters = EffectiveRouteAnnealIterations(node_count);
        if (route_anneal_ms > 0 && route_anneal_iters > 0 && !best_minsum_routes.empty()) {
            mtsp::RouteSet annealed_routes = best_minsum_routes;
            RouteAnnealStatsV10 route_stats;
            SearchBudgetV5 route_anneal_budget(route_anneal_ms, 0, 32);
            const bool route_annealed = AnnealRoutesMinsumV10(annealed_routes,
                                                              global_candidates,
                                                              distance,
                                                              route_anneal_budget,
                                                              route_anneal_iters,
                                                              route_stats);
            last_metadata_["route_anneal_ms"] = std::to_string(route_anneal_ms);
            last_metadata_["route_anneal_iters"] = std::to_string(route_anneal_iters);
            last_metadata_["route_anneal_before_sum"] = std::to_string(route_stats.before_sum);
            last_metadata_["route_anneal_after_sum"] = std::to_string(route_stats.after_sum);
            last_metadata_["route_anneal_before_energy"] = std::to_string(route_stats.before_energy);
            last_metadata_["route_anneal_after_energy"] = std::to_string(route_stats.after_energy);
            last_metadata_["route_anneal_accepted"] = std::to_string(route_stats.accepted_moves);
            last_metadata_["route_anneal_improvements"] = std::to_string(route_stats.best_updates);
            last_metadata_["route_anneal_best_delta"] = std::to_string(route_stats.before_sum - route_stats.after_sum);
            if (route_annealed) {
                SanitizeAndCompleteRoutesV7(annealed_routes, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutes(annealed_routes);
                last_metadata_["route_anneal_raw_sum"] = std::to_string(RouteSumLengthV7(annealed_routes, distance));
                mtsp::RouteSet raw_anneal_candidate = annealed_routes;
                UpdateMinsumArchive(raw_anneal_candidate, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);

                mtsp::RouteSet quenched_routes = annealed_routes;
                for (auto& route : quenched_routes) {
                    if (route_anneal_budget.ShouldStop()) {
                        break;
                    }
                    QuickRouteCleanupV5(route,
                                        local_candidates,
                                        distance,
                                        node_count,
                                        std::max(1, guided_cleanup_passes_),
                                        route_anneal_budget);
                }
                if (!route_anneal_budget.ShouldStop() && route_anneal_budget.RemainingMs() > 500) {
                    ImproveInterRoute(quenched_routes,
                                      local_candidates,
                                      global_candidates,
                                      std::max(1, effective_rounds / 2),
                                      distance,
                                      route_anneal_budget);
                }
                SanitizeAndCompleteRoutesV7(quenched_routes, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutes(quenched_routes);
                last_metadata_["route_anneal_final_sum"] = std::to_string(RouteSumLengthV7(quenched_routes, distance));
                mtsp::RouteSet minsum_candidate = quenched_routes;
                UpdateMinsumArchive(minsum_candidate, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);
                UpdateBestByBalanced(quenched_routes, best_routes, best_sum, best_max, distance);
            }
        }

        out.swap(best_minsum_routes);
        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutes(out);
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
            return std::min(popmusic_solutions_, 8);
        }
        if (node_count >= 50000) {
            return std::min(popmusic_solutions_, 10);
        }
        return std::min(popmusic_solutions_, 12);
    }

    int EffectivePopmusicSampleSize(int node_count) const {
        if (node_count >= 100000) {
            return std::max(popmusic_sample_size_, 48);
        }
        if (node_count >= 50000) {
            return std::max(popmusic_sample_size_, 40);
        }
        return std::max(popmusic_sample_size_, 32);
    }

    int EffectivePopmusicWindow(int node_count) const {
        if (node_count >= 100000) {
            return std::max(popmusic_window_, 40);
        }
        if (node_count >= 50000) {
            return std::max(popmusic_window_, 36);
        }
        return std::max(popmusic_window_, 32);
    }

    int EffectiveClusterSeedRestarts(int node_count) const {
        if (cluster_seed_restarts_ > 0) {
            return cluster_seed_restarts_;
        }
        return 1;
    }

    int EffectiveRouteAnnealMs(int node_count) const {
        if (anneal_route_ms_ >= 0) {
            return anneal_route_ms_;
        }
        if (node_count >= 10000 && node_count < 50000) {
            return 1000;
        }
        return 0;
    }

    int EffectiveRouteAnnealIterations(int node_count) const {
        if (anneal_route_iters_ >= 0) {
            return anneal_route_iters_;
        }
        if (node_count >= 100000) {
            return 2000000;
        }
        if (node_count >= 50000) {
            return 1000000;
        }
        if (node_count >= 10000) {
            return 50000;
        }
        return 0;
    }

    static int RemainingTotalMs(const std::chrono::steady_clock::time_point& start, int total_ms) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        return std::max(0, total_ms - static_cast<int>(elapsed));
    }

    static void EnsureClosedDepotRoutes(RouteSet& routes) {
        for (auto& route : routes) {
            if (route.empty()) {
                route = {0, 0};
                continue;
            }
            if (route.front() != 0) {
                route.insert(route.begin(), 0);
            }
            if (route.size() == 1 && route.front() == 0) {
                route.push_back(0);
                continue;
            }
            if (route.back() != 0) {
                route.push_back(0);
            }
        }
    }

    bool IsBetterByBalancedBootstrap(const RouteSet& candidate, const RouteSet& incumbent, DistanceOracleV5& distance) const {
        const double cand_sum = RouteSumLengthV7(candidate, distance);
        const double inc_sum = RouteSumLengthV7(incumbent, distance);
        const double cand_max = MaxRouteLengthV7(candidate, distance);
        const double inc_max = MaxRouteLengthV7(incumbent, distance);
        return cand_max + kEps < inc_max || cand_sum + kEps < inc_sum;
    }

    bool UpdateBestByBalanced(RouteSet& candidate,
                              RouteSet& best_routes,
                              double& best_sum,
                              double& best_max,
                              DistanceOracleV5& distance) const {
        const double candidate_sum = RouteSumLengthV7(candidate, distance);
        const double candidate_max = MaxRouteLengthV7(candidate, distance);
        if (candidate_max + kEps < best_max ||
            (std::abs(candidate_max - best_max) <= kEps && candidate_sum + kEps < best_sum)) {
            best_routes.swap(candidate);
            best_sum = candidate_sum;
            best_max = candidate_max;
            return true;
        }
        return false;
    }

    bool UpdateMinsumArchive(const RouteSet& candidate,
                             RouteSet& best_minsum_routes,
                             double& best_minsum_sum,
                             double& best_minsum_max,
                             DistanceOracleV5& distance) const {
        const double candidate_sum = RouteSumLengthV7(candidate, distance);
        const double candidate_max = MaxRouteLengthV7(candidate, distance);
        if (candidate_sum + kEps < best_minsum_sum ||
            (std::abs(candidate_sum - best_minsum_sum) <= kEps && candidate_max + kEps < best_minsum_max)) {
            best_minsum_routes = candidate;
            best_minsum_sum = candidate_sum;
            best_minsum_max = candidate_max;
            return true;
        }
        return false;
    }

    double RouteAnnealEnergy(double route_sum,
                             const std::vector<double>& route_lengths,
                             const std::vector<int>& route_sizes,
                             int target_size,
                             int hard_max_size) const {
        double max_length = 0.0;
        double penalty = 0.0;
        for (size_t route_idx = 0; route_idx < route_lengths.size(); ++route_idx) {
            max_length = std::max(max_length, route_lengths[route_idx]);
            const int size = route_sizes[route_idx];
            if (size <= 0) {
                penalty += 2.5e7;
            }
            if (size > target_size) {
                const double over = static_cast<double>(size - target_size);
                penalty += 45.0 * over * over / static_cast<double>(std::max(1, target_size));
            }
            if (size > hard_max_size) {
                const double over = static_cast<double>(size - hard_max_size);
                penalty += 1.0e9 * (over + 1.0);
            }
        }
        return route_sum + 0.003 * max_length + penalty;
    }

    void ReindexRouteRange(const RouteSet& routes,
                           size_t route_idx,
                           size_t start_pos,
                           std::vector<int>& route_of,
                           std::vector<size_t>& pos_in_route) const {
        const auto& route = routes[route_idx];
        for (size_t pos = start_pos; pos < route.size(); ++pos) {
            const int node = route[pos];
            if (node > 0) {
                route_of[static_cast<size_t>(node)] = static_cast<int>(route_idx);
                pos_in_route[static_cast<size_t>(node)] = pos;
            }
        }
    }

    void BuildRoutePositionIndex(const RouteSet& routes,
                                 int node_count,
                                 std::vector<int>& route_of,
                                 std::vector<size_t>& pos_in_route,
                                 std::vector<int>& route_sizes) const {
        route_of.assign(static_cast<size_t>(node_count), -1);
        pos_in_route.assign(static_cast<size_t>(node_count), 0);
        route_sizes.assign(routes.size(), 0);
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            const auto& route = routes[route_idx];
            for (size_t pos = 1; pos + 1 < route.size(); ++pos) {
                const int node = route[pos];
                if (node > 0 && node < node_count) {
                    route_of[static_cast<size_t>(node)] = static_cast<int>(route_idx);
                    pos_in_route[static_cast<size_t>(node)] = pos;
                    ++route_sizes[route_idx];
                }
            }
        }
    }

    bool PickRouteWithInterior(const RouteSet& routes,
                               const std::vector<int>& route_sizes,
                               int min_interior,
                               std::mt19937& rng,
                               size_t& route_idx) const {
        if (routes.empty()) {
            return false;
        }
        std::uniform_int_distribution<int> route_dist(0, static_cast<int>(routes.size()) - 1);
        for (int attempt = 0; attempt < 16; ++attempt) {
            const size_t candidate = static_cast<size_t>(route_dist(rng));
            if (route_sizes[candidate] >= min_interior) {
                route_idx = candidate;
                return true;
            }
        }
        for (size_t candidate = 0; candidate < routes.size(); ++candidate) {
            if (route_sizes[candidate] >= min_interior) {
                route_idx = candidate;
                return true;
            }
        }
        return false;
    }

    size_t BestAnnealInsertAfter(int city,
                                 size_t target_route,
                                 const RouteSet& routes,
                                 const CandidateSets& global_candidates,
                                 const std::vector<int>& route_of,
                                 const std::vector<size_t>& pos_in_route,
                                 DistanceOracleV5& distance,
                                 std::mt19937& rng) const {
        const auto& route = routes[target_route];
        if (route.size() <= 2) {
            return 0;
        }

        std::vector<size_t> positions;
        positions.reserve(global_candidates[static_cast<size_t>(city)].size() * 2ULL + 8ULL);
        positions.push_back(0);
        positions.push_back(route.size() - 2);

        std::uniform_int_distribution<size_t> random_after(0, route.size() - 2);
        for (int sample = 0; sample < 4; ++sample) {
            positions.push_back(random_after(rng));
        }

        for (int neighbor : global_candidates[static_cast<size_t>(city)]) {
            if (neighbor <= 0 || route_of[static_cast<size_t>(neighbor)] != static_cast<int>(target_route)) {
                continue;
            }
            const size_t pos = pos_in_route[static_cast<size_t>(neighbor)];
            if (pos > 0) {
                positions.push_back(pos - 1);
            }
            if (pos + 1 < route.size()) {
                positions.push_back(pos);
            }
        }

        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

        size_t best_after = positions.front();
        double best_delta = std::numeric_limits<double>::max();
        for (size_t after : positions) {
            if (after + 1 >= route.size()) {
                continue;
            }
            const int lhs = route[after];
            const int rhs = route[after + 1];
            const double delta = distance(lhs, city) + distance(city, rhs) - distance(lhs, rhs);
            if (delta + kEps < best_delta) {
                best_delta = delta;
                best_after = after;
            }
        }
        return best_after;
    }

    bool AnnealRoutesMinsumV10(RouteSet& routes,
                               const CandidateSets& global_candidates,
                               DistanceOracleV5& distance,
                               SearchBudgetV5& budget,
                               int max_iterations,
                               RouteAnnealStatsV10& stats) const {
        const Instance& inst = Instance::GetInstance();
        const int node_count = inst.GetNodeCount();
        const int route_count = static_cast<int>(routes.size());
        if (route_count < 2 || node_count <= 2 || max_iterations <= 0 || budget.ForceCheck()) {
            stats.before_sum = RouteSumLengthV7(routes, distance);
            stats.after_sum = stats.before_sum;
            stats.before_energy = stats.before_sum;
            stats.after_energy = stats.before_energy;
            return false;
        }

        const int target_size = std::max(1, (node_count - 1 + route_count - 1) / route_count);
        const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack_))));

        std::vector<int> route_of;
        std::vector<size_t> pos_in_route;
        std::vector<int> route_sizes;
        BuildRoutePositionIndex(routes, node_count, route_of, pos_in_route, route_sizes);

        std::vector<double> route_lengths(routes.size(), 0.0);
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            route_lengths[route_idx] = RouteLengthGenericV5(routes[route_idx], distance);
        }

        double current_sum = std::accumulate(route_lengths.begin(), route_lengths.end(), 0.0);
        double current_energy = RouteAnnealEnergy(current_sum, route_lengths, route_sizes, target_size, hard_max_size);
        stats.before_sum = current_sum;
        stats.after_sum = current_sum;
        stats.before_energy = current_energy;
        stats.after_energy = current_energy;

        RouteSet best_routes = routes;
        double best_sum = current_sum;
        double best_energy = current_energy;

        std::mt19937 anneal_rng(seed_ ^ 0x9E3779B9U ^ static_cast<unsigned int>(node_count));
        std::uniform_real_distribution<double> probability(0.0, 1.0);
        std::uniform_int_distribution<int> route_dist(0, route_count - 1);
        std::uniform_int_distribution<int> move_dist(0, 99);

        const double start_temp = std::max(1.0, current_sum * 0.00004);
        const double end_temp = std::max(0.05, start_temp * 0.002);
        const int quench_reserve_ms = budget.Enabled() && budget.RemainingMs() > 5000 ? 2500 : 0;

        for (int iter = 0; iter < max_iterations && !budget.ShouldStop(); ++iter) {
            if ((iter & 255) == 0) {
                budget.ForceCheck();
                if (quench_reserve_ms > 0 && budget.RemainingMs() <= quench_reserve_ms) {
                    break;
                }
            }

            const double alpha = static_cast<double>(iter) / std::max(1, max_iterations - 1);
            const double temperature = start_temp * std::pow(end_temp / start_temp, alpha);
            const bool try_relocate = move_dist(anneal_rng) < 72;

            std::vector<double> candidate_lengths = route_lengths;
            std::vector<int> candidate_sizes = route_sizes;
            double candidate_sum = current_sum;
            double delta = 0.0;

            enum class MoveKind { Relocate, Swap };
            MoveKind kind = MoveKind::Relocate;
            size_t from = 0;
            size_t to = 0;
            size_t from_pos = 0;
            size_t to_pos = 0;
            size_t insert_after = 0;
            int city = 0;

            if (try_relocate) {
                if (!PickRouteWithInterior(routes, route_sizes, 2, anneal_rng, from)) {
                    continue;
                }
                for (int attempt = 0; attempt < 12; ++attempt) {
                    to = static_cast<size_t>(route_dist(anneal_rng));
                    if (to != from && route_sizes[to] + 1 <= hard_max_size) {
                        break;
                    }
                }
                if (to == from || route_sizes[to] + 1 > hard_max_size) {
                    continue;
                }

                std::uniform_int_distribution<size_t> city_pos_dist(1, routes[from].size() - 2);
                from_pos = city_pos_dist(anneal_rng);
                city = routes[from][from_pos];
                insert_after = BestAnnealInsertAfter(city, to, routes, global_candidates, route_of, pos_in_route, distance, anneal_rng);

                const int prev = routes[from][from_pos - 1];
                const int next = routes[from][from_pos + 1];
                const double remove_delta = distance(prev, next) - distance(prev, city) - distance(city, next);
                const int lhs = routes[to][insert_after];
                const int rhs = routes[to][insert_after + 1];
                const double insert_delta = distance(lhs, city) + distance(city, rhs) - distance(lhs, rhs);
                delta = remove_delta + insert_delta;
                candidate_lengths[from] += remove_delta;
                candidate_lengths[to] += insert_delta;
                --candidate_sizes[from];
                ++candidate_sizes[to];
                candidate_sum += delta;
                kind = MoveKind::Relocate;
            } else {
                if (!PickRouteWithInterior(routes, route_sizes, 1, anneal_rng, from)) {
                    continue;
                }
                for (int attempt = 0; attempt < 12; ++attempt) {
                    to = static_cast<size_t>(route_dist(anneal_rng));
                    if (to != from && route_sizes[to] > 0) {
                        break;
                    }
                }
                if (to == from || route_sizes[to] <= 0) {
                    continue;
                }

                std::uniform_int_distribution<size_t> from_pos_dist(1, routes[from].size() - 2);
                from_pos = from_pos_dist(anneal_rng);
                const int city_a = routes[from][from_pos];

                std::vector<size_t> partner_positions;
                partner_positions.reserve(global_candidates[static_cast<size_t>(city_a)].size() + 4ULL);
                for (int neighbor : global_candidates[static_cast<size_t>(city_a)]) {
                    if (neighbor > 0 && route_of[static_cast<size_t>(neighbor)] == static_cast<int>(to)) {
                        partner_positions.push_back(pos_in_route[static_cast<size_t>(neighbor)]);
                    }
                }
                std::uniform_int_distribution<size_t> to_pos_dist(1, routes[to].size() - 2);
                for (int sample = 0; sample < 3; ++sample) {
                    partner_positions.push_back(to_pos_dist(anneal_rng));
                }
                std::sort(partner_positions.begin(), partner_positions.end());
                partner_positions.erase(std::unique(partner_positions.begin(), partner_positions.end()), partner_positions.end());

                double best_swap_delta = std::numeric_limits<double>::max();
                double best_from_delta = 0.0;
                double best_to_delta = 0.0;
                to_pos = partner_positions.front();
                for (size_t candidate_pos : partner_positions) {
                    const int prev_a = routes[from][from_pos - 1];
                    const int next_a = routes[from][from_pos + 1];
                    const int prev_b = routes[to][candidate_pos - 1];
                    const int next_b = routes[to][candidate_pos + 1];
                    const int city_b = routes[to][candidate_pos];
                    const double from_delta = distance(prev_a, city_b) + distance(city_b, next_a) -
                                              distance(prev_a, city_a) - distance(city_a, next_a);
                    const double to_delta = distance(prev_b, city_a) + distance(city_a, next_b) -
                                            distance(prev_b, city_b) - distance(city_b, next_b);
                    const double swap_delta = from_delta + to_delta;
                    if (swap_delta + kEps < best_swap_delta) {
                        best_swap_delta = swap_delta;
                        best_from_delta = from_delta;
                        best_to_delta = to_delta;
                        to_pos = candidate_pos;
                    }
                }

                delta = best_swap_delta;
                candidate_lengths[from] += best_from_delta;
                candidate_lengths[to] += best_to_delta;
                candidate_sum += delta;
                kind = MoveKind::Swap;
            }

            const double candidate_energy = RouteAnnealEnergy(candidate_sum,
                                                             candidate_lengths,
                                                             candidate_sizes,
                                                             target_size,
                                                             hard_max_size);
            const double energy_delta = candidate_energy - current_energy;
            if (energy_delta > 0.0 && probability(anneal_rng) >= std::exp(-energy_delta / std::max(1e-9, temperature))) {
                continue;
            }

            if (kind == MoveKind::Relocate) {
                routes[from].erase(routes[from].begin() + static_cast<std::ptrdiff_t>(from_pos));
                ReindexRouteRange(routes, from, from_pos, route_of, pos_in_route);
                routes[to].insert(routes[to].begin() + static_cast<std::ptrdiff_t>(insert_after + 1), city);
                ReindexRouteRange(routes, to, insert_after + 1, route_of, pos_in_route);
            } else {
                const int city_a = routes[from][from_pos];
                const int city_b = routes[to][to_pos];
                std::swap(routes[from][from_pos], routes[to][to_pos]);
                route_of[static_cast<size_t>(city_a)] = static_cast<int>(to);
                route_of[static_cast<size_t>(city_b)] = static_cast<int>(from);
                pos_in_route[static_cast<size_t>(city_a)] = to_pos;
                pos_in_route[static_cast<size_t>(city_b)] = from_pos;
            }

            route_lengths.swap(candidate_lengths);
            route_sizes.swap(candidate_sizes);
            current_sum = candidate_sum;
            current_energy = candidate_energy;
            ++stats.accepted_moves;

            if (current_sum + kEps < best_sum ||
                (std::abs(current_sum - best_sum) <= kEps && current_energy + kEps < best_energy)) {
                best_routes = routes;
                best_sum = current_sum;
                best_energy = current_energy;
                ++stats.best_updates;
            }
        }

        stats.after_sum = best_sum;
        stats.after_energy = best_energy;
        if (best_sum + kEps < stats.before_sum) {
            routes.swap(best_routes);
            return true;
        }
        routes.swap(best_routes);
        return false;
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

    bool TryMinsumRelocate(RouteSet& routes,
                           const CandidateSets& global_candidates,
                           DistanceOracleV5& distance,
                           SearchBudgetV5& budget,
                           size_t& changed_from,
                           size_t& changed_to) const {
        if (routes.size() < 2) {
            return false;
        }

        std::vector<RouteIndexV5> indices;
        indices.reserve(routes.size());
        for (const auto& route : routes) {
            indices.emplace_back(static_cast<int>(global_candidates.size()));
            indices.back().Build(route);
        }

        size_t best_from = routes.size();
        size_t best_to = routes.size();
        size_t best_i = 0;
        size_t best_after = 0;
        double best_delta = -kEps;

        for (size_t from = 0; from < routes.size() && !budget.ShouldStop(); ++from) {
            for (size_t i = 1; i + 1 < routes[from].size() && !budget.ShouldStop(); ++i) {
                const int city = routes[from][i];
                const int prev = routes[from][i - 1];
                const int next = routes[from][i + 1];
                const double removal = distance(prev, city) + distance(city, next) - distance(prev, next);

                for (size_t to = 0; to < routes.size(); ++to) {
                    if (to == from || routes[to].size() < 2) {
                        continue;
                    }

                    std::vector<size_t> positions;
                    positions.reserve(global_candidates[static_cast<size_t>(city)].size() + 2ULL);
                    positions.push_back(0);
                    positions.push_back(routes[to].size() - 2);
                    for (int neighbor : global_candidates[static_cast<size_t>(city)]) {
                        const int pos = indices[to].Get(neighbor);
                        if (pos >= 0 && pos + 1 < static_cast<int>(routes[to].size())) {
                            positions.push_back(static_cast<size_t>(pos));
                        }
                    }
                    std::sort(positions.begin(), positions.end());
                    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

                    for (size_t after : positions) {
                        const int a = routes[to][after];
                        const int b = routes[to][after + 1];
                        const double insert = distance(a, city) + distance(city, b) - distance(a, b);
                        const double delta = insert - removal;
                        if (delta + kEps < best_delta) {
                            best_delta = delta;
                            best_from = from;
                            best_to = to;
                            best_i = i;
                            best_after = after;
                        }
                    }
                }
            }
        }

        if (best_from == routes.size()) {
            return false;
        }

        const int city = routes[best_from][best_i];
        routes[best_from].erase(routes[best_from].begin() + static_cast<std::ptrdiff_t>(best_i));
        routes[best_to].insert(routes[best_to].begin() + static_cast<std::ptrdiff_t>(best_after + 1), city);
        changed_from = best_from;
        changed_to = best_to;
        return true;
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
        int relocate_passes = 0;
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

            if (!improved && relocate_passes < minsum_relocate_passes_) {
                size_t from = routes.size();
                size_t to = routes.size();
                if (TryMinsumRelocate(routes, global_candidates, distance, budget, from, to)) {
                    QuickRouteCleanupV5(routes[from], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                    QuickRouteCleanupV5(routes[to], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                    dirty_routes[from] = 1;
                    dirty_routes[to] = 1;
                    ++accepted_since_ils;
                    ++relocate_passes;
                    improved = true;
                }
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
    int cluster_seed_restarts_ = 0;
    double lookahead_weight_ = 0.35;
    double depot_weight_ = 0.12;
    int first_solution_budget_ms_ = 200000;
    int improvement_budget_ms_ = 100000;
    int anneal_route_ms_ = -1;
    int anneal_route_iters_ = -1;
    int minsum_relocate_passes_ = 24;
    int thread_count_ = 0;
    mutable std::mt19937 local_rng_{1337U};
    std::unordered_map<std::string, std::string> last_metadata_;
};

static bool reg_lkh_mtsp_v10 = (SolverFactory::RegisterSolver("lkh-wrapper-v10", []() {
    return std::make_unique<LkhWrapperSolverV10>();
}),
                                true);

} // namespace mtsp
