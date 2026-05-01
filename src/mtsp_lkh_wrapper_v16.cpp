// Third of the v14--v17 A/B sequence on the v12 pipeline. Same surface as
// v14/v15 with another iteration of metadata/parameter tweaks. Kept for
// CSV-reference compatibility; not a milestone version. Superseded by v18 /
// v21. Registered as "lkh-wrapper-v16".
// Inter-route improvement, savings seed, candidate enrichment, repair stages
// and final route anneal stay logically identical to v12.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
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

int DesiredClusterCountLegacyv16(int node_count, int salesman_count) {
    const int min_clusters = std::max(4 * salesman_count, 1);
    const int max_clusters = std::max(12 * salesman_count, min_clusters);
    int desired = 6 * salesman_count + std::max(0, node_count / 12000);
    desired = std::clamp(desired, min_clusters, max_clusters);
    desired = std::min(desired, std::max(1, node_count - 1));
    return desired;
}

ClusterModelV6 BuildLightweightClustersLegacyv16(const mtsp::Instance& inst,
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

void BuildInitialRoutesClusterAwareLegacyv16(mtsp::RouteSet& out,
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

struct RouteAnnealStatsv16 {
    double before_sum = 0.0;
    double after_sum = 0.0;
    double before_energy = 0.0;
    double after_energy = 0.0;
    int accepted_moves = 0;
    int best_updates = 0;
};

struct RouteRepairStatsV16 {
    double before_sum = 0.0;
    double after_sum = 0.0;
    int attempts = 0;
    int accepted = 0;
    long long insert_loop_us = 0;     // total time inside the ruin re-insertion loop
    long long cleanup_loop_us = 0;    // total time inside QuickRouteCleanupV5 of touched routes
};

class LkhWrapperSolverv16 : public Solver {
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
        if (opts.count("savings-seed-ms")) {
            savings_seed_ms_ = std::max(0, std::stoi(opts.at("savings-seed-ms")));
        }
        if (opts.count("savings-candidate-count")) {
            savings_candidate_count_ = std::max(8, std::stoi(opts.at("savings-candidate-count")));
        }
        if (opts.count("savings-route-slack")) {
            savings_route_slack_ = std::max(0.0, std::stod(opts.at("savings-route-slack")));
        }
        if (opts.count("savings-lambda")) {
            savings_lambda_ = std::max(0.05, std::stod(opts.at("savings-lambda")));
        }
        if (opts.count("route-repair-ms")) {
            route_repair_ms_ = std::max(0, std::stoi(opts.at("route-repair-ms")));
        }
        if (opts.count("route-repair-ruin")) {
            route_repair_ruin_ = std::max(1, std::stoi(opts.at("route-repair-ruin")));
        }
        if (opts.count("route-repair-size-slack")) {
            route_repair_size_slack_ = std::max(0.0, std::stod(opts.at("route-repair-size-slack")));
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
        if (opts.count("omp-polish")) {
            const std::string val = opts.at("omp-polish");
            omp_polish_enabled_ = !(val == "0" || val == "false" || val == "no");
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        last_metadata_.clear();
#ifdef _OPENMP
        if (thread_count_ > 0) {
            omp_set_num_threads(thread_count_);
        }
        last_metadata_["omp_polish"] = omp_polish_enabled_ ? "true" : "false";
        last_metadata_["omp_max_threads"] = std::to_string(omp_get_max_threads());
#else
        last_metadata_["omp_polish"] = "unavailable";
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

        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1200) {
            const int savings_phase_ms = unlimited
                ? EffectiveSavingsSeedMs(node_count)
                : std::min(EffectiveSavingsSeedMs(node_count), std::max(500, (first_budget.RemainingMs() * 2) / 3));
            if (savings_phase_ms > 0) {
                SearchBudgetV5 savings_budget(savings_phase_ms, 0, 32);
                CandidateSets savings_candidates = BuildGeometricCandidatesV5(inst,
                                                                              EffectiveSavingsCandidateCount(node_count),
                                                                              exact_candidate_threshold_);
                NormalizeCandidateSymmetryV5(savings_candidates, static_cast<size_t>(EffectiveSavingsCandidateCount(node_count)));
                mtsp::RouteSet savings_routes;
                const bool built_savings = BuildSavingsSeedRoutesV16(savings_routes,
                                                                      inst,
                                                                      savings_candidates,
                                                                      distance,
                                                                      savings_budget);
                last_metadata_["savings_seed_ms"] = std::to_string(savings_phase_ms);
                last_metadata_["savings_candidate_count"] = std::to_string(EffectiveSavingsCandidateCount(node_count));
                if (built_savings) {
                    SanitizeAndCompleteRoutesV7(savings_routes, inst, distance, std::max(route_size_slack_, route_repair_size_slack_));
                    EnsureClosedDepotRoutes(savings_routes);
                    SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
                    EnsureClosedDepotRoutes(out);
                    const double fast_seed_sum = RouteSumLengthV7(out, distance);
                    const double savings_seed_sum = RouteSumLengthV7(savings_routes, distance);
                    last_metadata_["savings_seed_sum"] = std::to_string(savings_seed_sum);
                    if (savings_seed_sum + kEps < fast_seed_sum) {
                        out.swap(savings_routes);
                        last_metadata_["savings_seed_selected"] = "true";
                    } else {
                        last_metadata_["savings_seed_selected"] = "false";
                    }
                } else {
                    last_metadata_["savings_seed_selected"] = "false";
                }
            }
        }

        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1500) {
            const int early_repair_phase_ms = unlimited
                ? std::max(500, EffectiveRouteRepairMs(node_count) / 3)
                : std::min(std::max(500, EffectiveRouteRepairMs(node_count) / 2),
                           std::max(500, first_budget.RemainingMs() / 5));
            if (early_repair_phase_ms > 0) {
                mtsp::RouteSet repaired_routes = out;
                SearchBudgetV5 repair_budget(early_repair_phase_ms, 0, 32);
                RouteRepairStatsV16 repair_stats;
                const bool repaired = RepairRoutesMinsumV16(repaired_routes,
                                                            local_candidates,
                                                            global_candidates,
                                                            distance,
                                                            repair_budget,
                                                            repair_stats);
                last_metadata_["route_repair_early_ms"] = std::to_string(early_repair_phase_ms);
                last_metadata_["route_repair_early_attempts"] = std::to_string(repair_stats.attempts);
                last_metadata_["route_repair_early_accepts"] = std::to_string(repair_stats.accepted);
                last_metadata_["route_repair_early_delta"] = std::to_string(repair_stats.before_sum - repair_stats.after_sum);
                last_metadata_["route_repair_early_insert_us"] = std::to_string(repair_stats.insert_loop_us);
                last_metadata_["route_repair_early_cleanup_us"] = std::to_string(repair_stats.cleanup_loop_us);
                if (repaired && RouteSumLengthV7(repaired_routes, distance) + kEps < RouteSumLengthV7(out, distance)) {
                    out.swap(repaired_routes);
                }
            }
        }

        mtsp::RouteSet seed_minsum_routes = out;
        double seed_minsum_sum = RouteSumLengthV7(seed_minsum_routes, distance);
        double seed_minsum_max = MaxRouteLengthV7(seed_minsum_routes, distance);

        ClusterModelV6 cluster_model;
        bool candidates_enriched = false;
        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1000) {
            const int cluster_count = forced_cluster_count_ > 0
                ? std::min(forced_cluster_count_, std::max(1, node_count - 1))
                : DesiredClusterCountLegacyv16(node_count, inst.GetSalesmanCount());
            const int cluster_phase_ms = unlimited
                ? 0
                : std::min(node_count >= 100000 ? 18000 : (node_count >= 50000 ? 10000 : 7000),
                           std::max(500, first_budget.RemainingMs() / 5));
            SearchBudgetV5 cluster_budget(cluster_phase_ms, 0, 16);
            cluster_model = BuildLightweightClustersLegacyv16(inst, cluster_count, rng, cluster_budget);
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
                    BuildInitialRoutesClusterAwareLegacyv16(candidate_cluster_routes,
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
        PolishRoutesParallel(out, inst, first_phase_rounds, local_candidates, node_count, first_budget,
                             seed_ ^ 0x6A09E667U);  // distinct seed_base from improve-phase polish
        if (!first_budget.ShouldStop()) {
            ImproveInterRoute(out, local_candidates, global_candidates, std::max(1, effective_rounds / 2), distance, first_budget);
        }
        SanitizeAndCompleteRoutesV7(out, inst, distance, route_size_slack_);
        EnsureClosedDepotRoutes(out);

        if (!first_budget.ShouldStop() && first_budget.RemainingMs() > 1500) {
            const int repair_phase_ms = unlimited
                ? EffectiveRouteRepairMs(node_count)
                : std::min(EffectiveRouteRepairMs(node_count), std::max(500, first_budget.RemainingMs() / 3));
            if (repair_phase_ms > 0) {
                mtsp::RouteSet repaired_routes = out;
                SearchBudgetV5 repair_budget(repair_phase_ms, 0, 32);
                RouteRepairStatsV16 repair_stats;
                const bool repaired = RepairRoutesMinsumV16(repaired_routes,
                                                            local_candidates,
                                                            global_candidates,
                                                            distance,
                                                            repair_budget,
                                                            repair_stats);
                last_metadata_["route_repair_first_ms"] = std::to_string(repair_phase_ms);
                last_metadata_["route_repair_first_attempts"] = std::to_string(repair_stats.attempts);
                last_metadata_["route_repair_first_accepts"] = std::to_string(repair_stats.accepted);
                last_metadata_["route_repair_first_delta"] = std::to_string(repair_stats.before_sum - repair_stats.after_sum);
                last_metadata_["route_repair_first_insert_us"] = std::to_string(repair_stats.insert_loop_us);
                last_metadata_["route_repair_first_cleanup_us"] = std::to_string(repair_stats.cleanup_loop_us);
                if (repaired && RouteSumLengthV7(repaired_routes, distance) + kEps < RouteSumLengthV7(out, distance)) {
                    out.swap(repaired_routes);
                }
            }
        }

        mtsp::RouteSet best_routes = out;
        double best_sum = RouteSumLengthV7(best_routes, distance);
        double best_max = MaxRouteLengthV7(best_routes, distance);
        mtsp::RouteSet best_minsum_routes = seed_minsum_routes.empty() ? best_routes : seed_minsum_routes;
        double best_minsum_sum = seed_minsum_routes.empty() ? best_sum : seed_minsum_sum;
        double best_minsum_max = seed_minsum_routes.empty() ? best_max : seed_minsum_max;
        UpdateMinsumArchive(best_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);

        const int remaining_after_first_ms = unlimited ? 0 : RemainingTotalMs(solve_start, effective_total_ms);
        const int improve_phase_ms = unlimited
            ? 0
            : std::min(target_improve_ms, remaining_after_first_ms);

        // filo_mode: for large instances skip expensive POPMUSIC re-enrichment and
        // second polish pass, then spend the saved time on OrOpt sweeps + FILO SA loop.
        const bool filo_mode = node_count >= 50000;

        if (unlimited || improve_phase_ms > 0) {
            SearchBudgetV5 improve_budget(improve_phase_ms, 0, 32);
            const double improve_phase_start_minsum = best_minsum_sum;
            const double improve_phase_start_balanced = best_sum;
            const auto improve_phase_started = std::chrono::steady_clock::now();
            int polish_passes_completed = 0;
            int polish_passes_improved_minsum = 0;
            int polish_passes_improved_balanced = 0;
            last_metadata_["improve_phase_budget_ms"] = std::to_string(improve_phase_ms);
            last_metadata_["improve_phase_start_minsum"] = std::to_string(improve_phase_start_minsum);
            last_metadata_["filo_mode"] = filo_mode ? "true" : "false";

            // Skip POPMUSIC re-enrichment for large instances: it costs 30-50s and
            // yields little improvement, leaving no time for FILO iterations.
            if (!filo_mode && !improve_budget.ShouldStop() && improve_budget.RemainingMs() > 1000) {
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

            // In filo_mode, cap the polish sub-budget so the bulk of improve time
            // goes to OrOpt + FILO SA rather than a single ImproveInterRoute marathon.
            // Reserve ~70% of remaining improve budget for FILO; use at most 30% for polish.
            const int polish_rounds = filo_mode ? 1 : std::max(1, effective_rounds);
            const int filo_reserve_ms = filo_mode
                ? static_cast<int>(improve_budget.RemainingMs() * 0.70)
                : 0;
            const int polish_cap_ms = filo_mode
                ? std::max(5000, improve_budget.RemainingMs() - filo_reserve_ms)
                : 0;
            for (int pass = 0; pass < polish_rounds && !improve_budget.ShouldStop(); ++pass) {
                mtsp::RouteSet candidate_routes = best_routes;
                if (filo_mode) {
                    // Use a capped sub-budget for intra-route polish.
                    SearchBudgetV5 polish_budget(polish_cap_ms, 0, 32);
                    PolishRoutesParallel(candidate_routes, inst, effective_rounds, local_candidates, node_count, polish_budget,
                                         seed_ ^ 0xBB67AE85U ^ static_cast<unsigned int>(pass) * 0xC2B2AE3DU);
                    if (!polish_budget.ShouldStop() && !improve_budget.ShouldStop()) {
                        ImproveInterRoute(candidate_routes, local_candidates, global_candidates, effective_rounds, distance, polish_budget);
                    }
                } else {
                    PolishRoutesParallel(candidate_routes, inst, effective_rounds, local_candidates, node_count, improve_budget,
                                         seed_ ^ 0xBB67AE85U ^ static_cast<unsigned int>(pass) * 0xC2B2AE3DU);
                    if (!improve_budget.ShouldStop()) {
                        ImproveInterRoute(candidate_routes, local_candidates, global_candidates, effective_rounds, distance, improve_budget);
                    }
                }
                SanitizeAndCompleteRoutesV7(candidate_routes, inst, distance, route_size_slack_);
                EnsureClosedDepotRoutes(candidate_routes);

                const double before_balanced_sum = best_sum;
                const double before_minsum = best_minsum_sum;
                const bool balanced_improved = UpdateBestByBalanced(candidate_routes, best_routes, best_sum, best_max, distance);
                const bool minsum_improved = UpdateMinsumArchive(candidate_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);
                ++polish_passes_completed;
                if (minsum_improved) ++polish_passes_improved_minsum;
                if (balanced_improved) ++polish_passes_improved_balanced;
                last_metadata_["polish_pass_" + std::to_string(pass + 1) + "_minsum"] = std::to_string(best_minsum_sum);
                last_metadata_["polish_pass_" + std::to_string(pass + 1) + "_delta"] = std::to_string(before_minsum - best_minsum_sum);
                if (!(best_sum + kEps < before_balanced_sum) && !(best_minsum_sum + kEps < before_minsum)) {
                    break;
                }
            }

            // --- FILO phase (large instances only) ---
            if (filo_mode && !improve_budget.ShouldStop()) {
                // OrOpt-1 cross-route sweep: fast greedy convergence, typically <2s.
                // Modifies best_minsum_routes in-place; recompute sum after each pass.
                const double before_oropt1 = best_minsum_sum;
                while (!improve_budget.ShouldStop()) {
                    if (!OrOpt1CrossRouteSweepV16(best_minsum_routes, global_candidates, distance, improve_budget)) {
                        break;
                    }
                }
                best_minsum_sum = RouteSumLengthV7(best_minsum_routes, distance);
                best_minsum_max = MaxRouteLengthV7(best_minsum_routes, distance);
                last_metadata_["oropt1_delta"] = std::to_string(before_oropt1 - best_minsum_sum);

                // OrOpt-2 cross-route sweep: greedy 2-node segment relocation.
                const double before_oropt2 = best_minsum_sum;
                while (!improve_budget.ShouldStop()) {
                    if (!OrOpt2CrossRouteSweepV16(best_minsum_routes, global_candidates, distance, improve_budget)) {
                        break;
                    }
                }
                best_minsum_sum = RouteSumLengthV7(best_minsum_routes, distance);
                best_minsum_max = MaxRouteLengthV7(best_minsum_routes, distance);
                last_metadata_["oropt2_delta"] = std::to_string(before_oropt2 - best_minsum_sum);

                // Sync balanced archive with minsum after OrOpt sweeps.
                UpdateBestByBalanced(best_minsum_routes, best_routes, best_sum, best_max, distance);

                // FILO SA ruin-recreate: continuous improvement until budget exhausted.
                FiloMainLoopV16(best_minsum_routes,
                                best_minsum_sum,
                                best_minsum_max,
                                best_routes,
                                best_sum,
                                best_max,
                                local_candidates,
                                global_candidates,
                                inst,
                                distance,
                                improve_budget);
            }

            const auto improve_phase_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - improve_phase_started).count();
            last_metadata_["improve_phase_passes_completed"] = std::to_string(polish_passes_completed);
            last_metadata_["improve_phase_passes_improved_minsum"] = std::to_string(polish_passes_improved_minsum);
            last_metadata_["improve_phase_passes_improved_balanced"] = std::to_string(polish_passes_improved_balanced);
            last_metadata_["improve_phase_minsum_delta"] = std::to_string(improve_phase_start_minsum - best_minsum_sum);
            last_metadata_["improve_phase_balanced_delta"] = std::to_string(improve_phase_start_balanced - best_sum);
            last_metadata_["improve_phase_elapsed_ms"] = std::to_string(improve_phase_elapsed_ms);
            last_metadata_["improve_phase_end_minsum"] = std::to_string(best_minsum_sum);
        }

        UpdateMinsumArchive(best_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);

        const int remaining_before_late_repair_ms = unlimited ? 0 : RemainingTotalMs(solve_start, effective_total_ms);
        const int late_repair_phase_ms = unlimited
            ? EffectiveRouteRepairMs(node_count)
            : std::min(EffectiveRouteRepairMs(node_count), std::max(0, remaining_before_late_repair_ms - 500));
        if (late_repair_phase_ms > 0 && !best_minsum_routes.empty()) {
            mtsp::RouteSet repaired_routes = best_minsum_routes;
            SearchBudgetV5 repair_budget(late_repair_phase_ms, 0, 32);
            RouteRepairStatsV16 repair_stats;
            const bool repaired = RepairRoutesMinsumV16(repaired_routes,
                                                        local_candidates,
                                                        global_candidates,
                                                        distance,
                                                        repair_budget,
                                                        repair_stats);
            last_metadata_["route_repair_late_ms"] = std::to_string(late_repair_phase_ms);
            last_metadata_["route_repair_late_attempts"] = std::to_string(repair_stats.attempts);
            last_metadata_["route_repair_late_accepts"] = std::to_string(repair_stats.accepted);
            last_metadata_["route_repair_late_delta"] = std::to_string(repair_stats.before_sum - repair_stats.after_sum);
            last_metadata_["route_repair_late_insert_us"] = std::to_string(repair_stats.insert_loop_us);
            last_metadata_["route_repair_late_cleanup_us"] = std::to_string(repair_stats.cleanup_loop_us);
            if (repaired) {
                SanitizeAndCompleteRoutesV7(repaired_routes, inst, distance, std::max(route_size_slack_, route_repair_size_slack_));
                EnsureClosedDepotRoutes(repaired_routes);
                UpdateMinsumArchive(repaired_routes, best_minsum_routes, best_minsum_sum, best_minsum_max, distance);
            }
        }

        const int anneal_remaining_ms = unlimited ? std::numeric_limits<int>::max() : RemainingTotalMs(solve_start, effective_total_ms);
        const int requested_anneal_ms = EffectiveRouteAnnealMs(node_count);
        // Cap by what is actually left of the global budget. Without this cap
        // the anneal phase can overshoot --time-budget-ms whenever previous
        // phases ran late, which used to silently happen in v12.
        const int route_anneal_ms = unlimited ? requested_anneal_ms : std::max(0, std::min(requested_anneal_ms, anneal_remaining_ms - 250));
        const int route_anneal_iters = EffectiveRouteAnnealIterations(node_count);
        if (route_anneal_ms > 0 && route_anneal_iters > 0 && !best_minsum_routes.empty()) {
            mtsp::RouteSet annealed_routes = best_minsum_routes;
            RouteAnnealStatsv16 route_stats;
            SearchBudgetV5 route_anneal_budget(route_anneal_ms, 0, 32);
            const bool route_annealed = AnnealRoutesMinsumv16(annealed_routes,
                                                              global_candidates,
                                                              distance,
                                                              route_anneal_budget,
                                                              route_anneal_iters,
                                                              route_stats);
            last_metadata_["route_anneal_ms"] = std::to_string(route_anneal_ms);
            last_metadata_["route_anneal_requested_ms"] = std::to_string(requested_anneal_ms);
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

    int EffectiveSavingsCandidateCount(int node_count) const {
        const int requested = std::max(8, savings_candidate_count_);
        if (node_count >= 100000) {
            return std::clamp(requested, 32, 64);
        }
        if (node_count >= 50000) {
            return std::clamp(requested, 32, 64);
        }
        if (node_count >= 10000) {
            return std::clamp(requested, 32, 64);
        }
        return std::clamp(requested, 16, 48);
    }

    int EffectiveSavingsExtraRouteCount(int node_count, int m) const {
        const int base = std::max(m + 4, m);
        if (node_count >= 100000) {
            return std::max(base, m + 6);
        }
        if (node_count >= 10000) {
            return base;
        }
        return std::max(m, m + 2);
    }

    int EffectiveSavingsSeedMs(int node_count) const {
        if (savings_seed_ms_ >= 0) {
            return savings_seed_ms_;
        }
        if (node_count >= 100000) {
            return 90000;
        }
        if (node_count >= 50000) {
            return 45000;
        }
        if (node_count >= 10000) {
            return 35000;
        }
        return 5000;
    }

    int EffectiveRouteRepairMs(int node_count) const {
        if (route_repair_ms_ >= 0) {
            return route_repair_ms_;
        }
        if (node_count >= 100000) {
            return 60000;
        }
        if (node_count >= 50000) {
            return 30000;
        }
        if (node_count >= 10000) {
            return 10000;
        }
        return 2500;
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
        // FIX-ANNEAL: in v12 this returned 0 for n>=50000, leaving the SA
        // disabled on exactly the instance sizes where it has the most room
        // to work. EffectiveRouteAnnealIterations was already non-zero for
        // these sizes (2M for 100k, 1M for 50k), so the only thing missing
        // was the time budget. We give the anneal phase a sensible default
        // budget here; it will still be capped by RemainingTotalMs at the
        // call site, so we never overrun the user's --time-budget-ms.
        if (node_count >= 100000) {
            return 60000;
        }
        if (node_count >= 50000) {
            return 30000;
        }
        if (node_count >= 10000) {
            return 5000;
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

    // FIX-D: parallel intra-route polish.
    //
    // The intra-route polish phase calls IteratedLocalSearchV5 once per
    // route. The m routes are independent (each only modifies its own
    // sequence) so they parallelise embarrassingly along m. The shared
    // distance oracle has an internal unordered_map cache that is NOT
    // thread-safe, so each thread builds its own DistanceOracleV5 from the
    // (read-only) Instance. SearchBudgetV5 is POD-like; the default copy
    // ctor produces a per-thread copy that shares only the deadline_
    // value, which is what we want - any thread can decide to stop based
    // on the global deadline without coordinating with peers.
    void PolishRoutesParallel(RouteSet& routes,
                              const Instance& inst,
                              int rounds_per_route,
                              const CandidateSets& local_candidates,
                              int node_count,
                              SearchBudgetV5& shared_budget,
                              unsigned int seed_base) const {
        if (routes.empty()) {
            return;
        }
#ifdef _OPENMP
        if (omp_polish_enabled_ && routes.size() > 1) {
            // schedule(dynamic) because route lengths can be quite uneven
            // when one route swallowed most cluster-aware work.
            #pragma omp parallel for schedule(dynamic) if(routes.size() > 1)
            for (long long r = 0; r < static_cast<long long>(routes.size()); ++r) {
                if (shared_budget.ShouldStop()) {
                    continue;
                }
                DistanceOracleV5 thread_distance(inst);
                SearchBudgetV5 thread_budget = shared_budget;
                std::mt19937 thread_rng(seed_base ^ static_cast<unsigned int>(r * 0x9E3779B9U + 0x85EBCA6BU));
                IteratedLocalSearchV5(routes[static_cast<size_t>(r)],
                                      thread_rng,
                                      rounds_per_route,
                                      local_candidates,
                                      node_count,
                                      thread_distance,
                                      thread_budget);
            }
            return;
        }
#endif
        // Fall-through serial path: same loop the original v12 had, used
        // when OMP is off, the user disabled it via --omp-polish=false, or
        // there is only a single route to work on.
        DistanceOracleV5 serial_distance(inst);
        std::mt19937 serial_rng(seed_base);
        for (auto& route : routes) {
            if (shared_budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV5(route, serial_rng, rounds_per_route, local_candidates, node_count, serial_distance, shared_budget);
        }
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
        return cand_sum + kEps < inc_sum ||
               (std::abs(cand_sum - inc_sum) <= kEps && cand_max + kEps < inc_max);
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

    struct SavingsEdgeV16 {
        int from = 0;
        int to = 0;
        double saving = 0.0;
    };

    struct SavingsRouteV16 {
        std::vector<int> nodes;
        bool active = true;
    };

    static bool IsRouteEndpointV16(const std::vector<int>& route, int city) {
        return !route.empty() && (route.front() == city || route.back() == city);
    }

    bool MergeSavingsRoutesV16(std::vector<SavingsRouteV16>& routes,
                               std::vector<int>& route_of,
                               int lhs_city,
                               int rhs_city,
                               int hard_max_size) const {
        const int lhs_route = route_of[static_cast<size_t>(lhs_city)];
        const int rhs_route = route_of[static_cast<size_t>(rhs_city)];
        if (lhs_route < 0 || rhs_route < 0 || lhs_route == rhs_route) {
            return false;
        }

        auto& lhs = routes[static_cast<size_t>(lhs_route)];
        auto& rhs = routes[static_cast<size_t>(rhs_route)];
        if (!lhs.active || !rhs.active || lhs.nodes.empty() || rhs.nodes.empty()) {
            return false;
        }
        if (!IsRouteEndpointV16(lhs.nodes, lhs_city) || !IsRouteEndpointV16(rhs.nodes, rhs_city)) {
            return false;
        }
        if (static_cast<int>(lhs.nodes.size() + rhs.nodes.size()) > hard_max_size) {
            return false;
        }

        std::vector<int> merged;
        merged.reserve(lhs.nodes.size() + rhs.nodes.size());
        if (lhs.nodes.back() == lhs_city && rhs.nodes.front() == rhs_city) {
            merged.insert(merged.end(), lhs.nodes.begin(), lhs.nodes.end());
            merged.insert(merged.end(), rhs.nodes.begin(), rhs.nodes.end());
        } else if (lhs.nodes.front() == lhs_city && rhs.nodes.back() == rhs_city) {
            merged.insert(merged.end(), rhs.nodes.begin(), rhs.nodes.end());
            merged.insert(merged.end(), lhs.nodes.begin(), lhs.nodes.end());
        } else if (lhs.nodes.front() == lhs_city && rhs.nodes.front() == rhs_city) {
            merged.insert(merged.end(), lhs.nodes.rbegin(), lhs.nodes.rend());
            merged.insert(merged.end(), rhs.nodes.begin(), rhs.nodes.end());
        } else if (lhs.nodes.back() == lhs_city && rhs.nodes.back() == rhs_city) {
            merged.insert(merged.end(), lhs.nodes.begin(), lhs.nodes.end());
            merged.insert(merged.end(), rhs.nodes.rbegin(), rhs.nodes.rend());
        } else {
            return false;
        }

        lhs.nodes.swap(merged);
        rhs.nodes.clear();
        rhs.active = false;
        for (int city : lhs.nodes) {
            route_of[static_cast<size_t>(city)] = lhs_route;
        }
        return true;
    }

    struct MergeChoiceV16 {
        size_t lhs = 0;
        size_t rhs = 0;
        int orientation = 0;
        double delta = std::numeric_limits<double>::max();
        double score = std::numeric_limits<double>::max();
    };

    struct EndpointMergeMoveV16 {
        double delta = 0.0;
        int lhs_route = -1;
        int rhs_route = -1;
        int lhs_front = 0;
        int lhs_back = 0;
        int rhs_front = 0;
        int rhs_back = 0;
        int orientation = 0;
    };

    struct EndpointMergeMoveGreaterV16 {
        bool operator()(const EndpointMergeMoveV16& lhs, const EndpointMergeMoveV16& rhs) const {
            if (std::abs(lhs.delta - rhs.delta) > kEps) {
                return lhs.delta > rhs.delta;
            }
            if (lhs.lhs_route != rhs.lhs_route) {
                return lhs.lhs_route > rhs.lhs_route;
            }
            return lhs.rhs_route > rhs.rhs_route;
        }
    };

    static void AppendReversedV16(std::vector<int>& target, const std::vector<int>& source) {
        target.insert(target.end(), source.rbegin(), source.rend());
    }

    double MergeDeltaV16(const std::vector<int>& lhs,
                         const std::vector<int>& rhs,
                         int orientation,
                         DistanceOracleV5& distance) const {
        if (lhs.empty() || rhs.empty()) {
            return std::numeric_limits<double>::max();
        }
        if (orientation == 0) {
            return distance(lhs.back(), rhs.front()) - distance(lhs.back(), 0) - distance(0, rhs.front());
        }
        if (orientation == 1) {
            return distance(lhs.back(), rhs.back()) - distance(lhs.back(), 0) - distance(0, rhs.back());
        }
        if (orientation == 2) {
            return distance(lhs.front(), rhs.front()) - distance(lhs.front(), 0) - distance(0, rhs.front());
        }
        return distance(rhs.back(), lhs.front()) - distance(rhs.back(), 0) - distance(0, lhs.front());
    }

    std::vector<int> ApplyOpenRouteMergeV16(const std::vector<int>& lhs,
                                            const std::vector<int>& rhs,
                                            int orientation) const {
        std::vector<int> merged;
        merged.reserve(lhs.size() + rhs.size());
        if (orientation == 0) {
            merged.insert(merged.end(), lhs.begin(), lhs.end());
            merged.insert(merged.end(), rhs.begin(), rhs.end());
        } else if (orientation == 1) {
            merged.insert(merged.end(), lhs.begin(), lhs.end());
            AppendReversedV16(merged, rhs);
        } else if (orientation == 2) {
            AppendReversedV16(merged, lhs);
            merged.insert(merged.end(), rhs.begin(), rhs.end());
        } else {
            merged.insert(merged.end(), rhs.begin(), rhs.end());
            merged.insert(merged.end(), lhs.begin(), lhs.end());
        }
        return merged;
    }

    void PushEndpointMergeMovesV16(
        int route_idx,
        const std::vector<SavingsRouteV16>& routes,
        const std::vector<int>& route_of,
        const CandidateSets& savings_candidates,
        DistanceOracleV5& distance,
        std::priority_queue<EndpointMergeMoveV16,
                            std::vector<EndpointMergeMoveV16>,
                            EndpointMergeMoveGreaterV16>& heap) const {
        if (route_idx < 0 || route_idx >= static_cast<int>(routes.size())) {
            return;
        }
        const auto& route = routes[static_cast<size_t>(route_idx)];
        if (!route.active || route.nodes.empty()) {
            return;
        }

        const int endpoints[2] = {route.nodes.front(), route.nodes.back()};
        for (int endpoint : endpoints) {
            if (endpoint <= 0 || endpoint >= static_cast<int>(savings_candidates.size())) {
                continue;
            }
            for (int neighbor : savings_candidates[static_cast<size_t>(endpoint)]) {
                if (neighbor <= 0 || neighbor >= static_cast<int>(route_of.size())) {
                    continue;
                }
                const int other_idx = route_of[static_cast<size_t>(neighbor)];
                if (other_idx < 0 || other_idx == route_idx ||
                    other_idx >= static_cast<int>(routes.size())) {
                    continue;
                }
                const auto& other = routes[static_cast<size_t>(other_idx)];
                if (!other.active || other.nodes.empty() ||
                    !IsRouteEndpointV16(other.nodes, neighbor)) {
                    continue;
                }

                int lhs_idx = route_idx;
                int rhs_idx = other_idx;
                if (rhs_idx < lhs_idx) {
                    std::swap(lhs_idx, rhs_idx);
                }
                const auto& lhs = routes[static_cast<size_t>(lhs_idx)].nodes;
                const auto& rhs = routes[static_cast<size_t>(rhs_idx)].nodes;
                EndpointMergeMoveV16 best;
                best.lhs_route = lhs_idx;
                best.rhs_route = rhs_idx;
                best.lhs_front = lhs.front();
                best.lhs_back = lhs.back();
                best.rhs_front = rhs.front();
                best.rhs_back = rhs.back();
                best.delta = std::numeric_limits<double>::max();
                for (int orientation = 0; orientation < 4; ++orientation) {
                    const double delta = MergeDeltaV16(lhs, rhs, orientation, distance);
                    if (delta + kEps < best.delta) {
                        best.delta = delta;
                        best.orientation = orientation;
                    }
                }
                if (best.delta < std::numeric_limits<double>::max()) {
                    heap.push(best);
                }
            }
        }
    }

    bool EndpointMergeMoveStillValidV16(const EndpointMergeMoveV16& move,
                                        const std::vector<SavingsRouteV16>& routes) const {
        if (move.lhs_route < 0 || move.rhs_route < 0 ||
            move.lhs_route >= static_cast<int>(routes.size()) ||
            move.rhs_route >= static_cast<int>(routes.size()) ||
            move.lhs_route == move.rhs_route) {
            return false;
        }
        const auto& lhs = routes[static_cast<size_t>(move.lhs_route)];
        const auto& rhs = routes[static_cast<size_t>(move.rhs_route)];
        return lhs.active && rhs.active &&
               !lhs.nodes.empty() && !rhs.nodes.empty() &&
               lhs.nodes.front() == move.lhs_front &&
               lhs.nodes.back() == move.lhs_back &&
               rhs.nodes.front() == move.rhs_front &&
               rhs.nodes.back() == move.rhs_back;
    }

    bool ApplyEndpointMergeMoveV16(const EndpointMergeMoveV16& move,
                                   std::vector<SavingsRouteV16>& routes,
                                   std::vector<int>& route_of) const {
        if (!EndpointMergeMoveStillValidV16(move, routes)) {
            return false;
        }
        auto& lhs = routes[static_cast<size_t>(move.lhs_route)];
        auto& rhs = routes[static_cast<size_t>(move.rhs_route)];
        std::vector<int> merged = ApplyOpenRouteMergeV16(lhs.nodes, rhs.nodes, move.orientation);
        lhs.nodes.swap(merged);
        rhs.nodes.clear();
        rhs.active = false;
        for (int city : lhs.nodes) {
            route_of[static_cast<size_t>(city)] = move.lhs_route;
        }
        return true;
    }

    bool ReduceSavingsRoutesByEndpointQueueV16(std::vector<SavingsRouteV16>& routes,
                                               std::vector<int>& route_of,
                                               int& active_count,
                                               int target_count,
                                               const CandidateSets& savings_candidates,
                                               DistanceOracleV5& distance,
                                               SearchBudgetV5& budget) const {
        if (active_count <= target_count) {
            return true;
        }
        std::priority_queue<EndpointMergeMoveV16,
                            std::vector<EndpointMergeMoveV16>,
                            EndpointMergeMoveGreaterV16> heap;
        for (int route_idx = 0; route_idx < static_cast<int>(routes.size()) && !budget.ShouldStop(); ++route_idx) {
            PushEndpointMergeMovesV16(route_idx, routes, route_of, savings_candidates, distance, heap);
        }

        int stale_pops = 0;
        while (active_count > target_count && !heap.empty() && !budget.ShouldStop()) {
            EndpointMergeMoveV16 move = heap.top();
            heap.pop();
            if (!EndpointMergeMoveStillValidV16(move, routes)) {
                if (++stale_pops > active_count * 64) {
                    stale_pops = 0;
                    while (!heap.empty()) {
                        heap.pop();
                    }
                    for (int route_idx = 0; route_idx < static_cast<int>(routes.size()) && !budget.ShouldStop(); ++route_idx) {
                        PushEndpointMergeMovesV16(route_idx, routes, route_of, savings_candidates, distance, heap);
                    }
                }
                continue;
            }
            const int survivor = move.lhs_route;
            if (ApplyEndpointMergeMoveV16(move, routes, route_of)) {
                --active_count;
                PushEndpointMergeMovesV16(survivor, routes, route_of, savings_candidates, distance, heap);
            }
        }
        return active_count <= target_count;
    }

    bool MergeOpenRoutesToCountV16(std::vector<std::vector<int>>& open_routes,
                                   int target_count,
                                   DistanceOracleV5& distance,
                                   SearchBudgetV5& budget,
                                   bool prefer_short_merged_route = false) const {
        if (target_count <= 0) {
            return false;
        }
        const size_t safe_pair_scan_limit = static_cast<size_t>(std::max(target_count * 96, target_count + 256));
        while (open_routes.size() > static_cast<size_t>(target_count) && !budget.ShouldStop()) {
            if (open_routes.size() > safe_pair_scan_limit) {
                return false;
            }

            MergeChoiceV16 best;
            std::vector<double> route_lengths;
            if (prefer_short_merged_route) {
                route_lengths.reserve(open_routes.size());
                for (const auto& route : open_routes) {
                    double length = 0.0;
                    int prev = 0;
                    for (int city : route) {
                        length += distance(prev, city);
                        prev = city;
                    }
                    length += distance(prev, 0);
                    route_lengths.push_back(length);
                }
            }
            for (size_t lhs = 0; lhs < open_routes.size() && !budget.ShouldStop(); ++lhs) {
                for (size_t rhs = lhs + 1; rhs < open_routes.size(); ++rhs) {
                    for (int orientation = 0; orientation < 4; ++orientation) {
                        const double delta = MergeDeltaV16(open_routes[lhs], open_routes[rhs], orientation, distance);
                        const double score = prefer_short_merged_route
                            ? route_lengths[lhs] + route_lengths[rhs] + delta
                            : delta;
                        if (score + kEps < best.score ||
                            (std::abs(score - best.score) <= kEps && delta + kEps < best.delta)) {
                            best = {lhs, rhs, orientation, delta, score};
                        }
                    }
                }
            }
            if (best.score == std::numeric_limits<double>::max()) {
                return false;
            }

            open_routes[best.lhs] = ApplyOpenRouteMergeV16(open_routes[best.lhs], open_routes[best.rhs], best.orientation);
            open_routes.erase(open_routes.begin() + static_cast<std::ptrdiff_t>(best.rhs));
        }
        return open_routes.size() == static_cast<size_t>(target_count);
    }

    void SplitOpenRoutesToCountV16(std::vector<std::vector<int>>& open_routes, int target_count) const {
        while (open_routes.size() < static_cast<size_t>(target_count)) {
            size_t best = open_routes.size();
            for (size_t route_idx = 0; route_idx < open_routes.size(); ++route_idx) {
                if (open_routes[route_idx].size() > 1 &&
                    (best == open_routes.size() || open_routes[route_idx].size() > open_routes[best].size())) {
                    best = route_idx;
                }
            }
            if (best == open_routes.size()) {
                open_routes.push_back({});
                continue;
            }
            auto& route = open_routes[best];
            const size_t mid = std::max<size_t>(1, route.size() / 2);
            std::vector<int> tail(route.begin() + static_cast<std::ptrdiff_t>(mid), route.end());
            route.erase(route.begin() + static_cast<std::ptrdiff_t>(mid), route.end());
            open_routes.push_back(std::move(tail));
        }
    }

    void CleanupOpenRoutesV16(std::vector<std::vector<int>>& open_routes,
                              const CandidateSets& candidate_sets,
                              int node_count,
                              DistanceOracleV5& distance,
                              SearchBudgetV5& budget) const {
        for (auto& open_route : open_routes) {
            if (budget.ShouldStop() || open_route.empty()) {
                break;
            }
            std::vector<int> closed;
            closed.reserve(open_route.size() + 2ULL);
            closed.push_back(0);
            closed.insert(closed.end(), open_route.begin(), open_route.end());
            closed.push_back(0);
            QuickRouteCleanupV5(closed,
                                candidate_sets,
                                distance,
                                node_count,
                                std::max(1, guided_cleanup_passes_ + 1),
                                budget);
            if (closed.size() >= 2 && closed.front() == 0 && closed.back() == 0) {
                open_route.assign(closed.begin() + 1, closed.end() - 1);
            }
        }
    }

    double OpenRoutesTotalLengthV16(const std::vector<std::vector<int>>& open_routes,
                                    DistanceOracleV5& distance) const {
        double total = 0.0;
        for (const auto& route : open_routes) {
            int prev = 0;
            for (int city : route) {
                total += distance(prev, city);
                prev = city;
            }
            total += distance(prev, 0);
        }
        return total;
    }

    struct OpenInsertionChoiceV16 {
        int route = -1;
        int after = -1;
        double delta = std::numeric_limits<double>::max();
    };

    void BuildOpenRoutePositionIndexV16(const std::vector<std::vector<int>>& open_routes,
                                        int node_count,
                                        std::vector<int>& route_of,
                                        std::vector<int>& pos_in_route) const {
        route_of.assign(static_cast<size_t>(node_count), -1);
        pos_in_route.assign(static_cast<size_t>(node_count), -1);
        for (int route_idx = 0; route_idx < static_cast<int>(open_routes.size()); ++route_idx) {
            const auto& route = open_routes[static_cast<size_t>(route_idx)];
            for (int pos = 0; pos < static_cast<int>(route.size()); ++pos) {
                const int city = route[static_cast<size_t>(pos)];
                if (city > 0 && city < node_count) {
                    route_of[static_cast<size_t>(city)] = route_idx;
                    pos_in_route[static_cast<size_t>(city)] = pos;
                }
            }
        }
    }

    void ReindexOpenRouteRangeV16(const std::vector<std::vector<int>>& open_routes,
                                  int route_idx,
                                  int start_pos,
                                  std::vector<int>& route_of,
                                  std::vector<int>& pos_in_route) const {
        if (route_idx < 0 || route_idx >= static_cast<int>(open_routes.size())) {
            return;
        }
        const auto& route = open_routes[static_cast<size_t>(route_idx)];
        for (int pos = std::max(0, start_pos); pos < static_cast<int>(route.size()); ++pos) {
            const int city = route[static_cast<size_t>(pos)];
            if (city > 0 && city < static_cast<int>(route_of.size())) {
                route_of[static_cast<size_t>(city)] = route_idx;
                pos_in_route[static_cast<size_t>(city)] = pos;
            }
        }
    }

    double OpenInsertionDeltaV16(const std::vector<int>& route,
                                 int city,
                                 int after,
                                 DistanceOracleV5& distance) const {
        const int prev = after < 0 ? 0 : route[static_cast<size_t>(after)];
        const int next = (after + 1 >= static_cast<int>(route.size())) ? 0 : route[static_cast<size_t>(after + 1)];
        return distance(prev, city) + distance(city, next) - distance(prev, next);
    }

    OpenInsertionChoiceV16 FindBestOpenInsertionV16(int city,
                                                    const std::vector<std::vector<int>>& open_routes,
                                                    const CandidateSets& candidate_sets,
                                                    const std::vector<int>& route_of,
                                                    const std::vector<int>& pos_in_route,
                                                    int hard_max_size,
                                                    DistanceOracleV5& distance) const {
        std::vector<std::pair<int, int>> positions;
        positions.reserve(candidate_sets[static_cast<size_t>(city)].size() * 4ULL + 16ULL);
        for (int neighbor : candidate_sets[static_cast<size_t>(city)]) {
            if (neighbor <= 0 || neighbor >= static_cast<int>(route_of.size())) {
                continue;
            }
            const int route_idx = route_of[static_cast<size_t>(neighbor)];
            if (route_idx < 0 || route_idx >= static_cast<int>(open_routes.size())) {
                continue;
            }
            const int pos = pos_in_route[static_cast<size_t>(neighbor)];
            positions.emplace_back(route_idx, pos);
            positions.emplace_back(route_idx, pos - 1);
            positions.emplace_back(route_idx, -1);
            positions.emplace_back(route_idx, static_cast<int>(open_routes[static_cast<size_t>(route_idx)].size()) - 1);
        }
        if (positions.empty()) {
            for (int route_idx = 0; route_idx < static_cast<int>(open_routes.size()); ++route_idx) {
                positions.emplace_back(route_idx, -1);
                positions.emplace_back(route_idx, static_cast<int>(open_routes[static_cast<size_t>(route_idx)].size()) - 1);
            }
        }
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

        OpenInsertionChoiceV16 best;
        OpenInsertionChoiceV16 best_overflow;
        for (const auto& [route_idx, after] : positions) {
            if (route_idx < 0 || route_idx >= static_cast<int>(open_routes.size())) {
                continue;
            }
            const auto& route = open_routes[static_cast<size_t>(route_idx)];
            if (after < -1 || after >= static_cast<int>(route.size())) {
                continue;
            }
            const double delta = OpenInsertionDeltaV16(route, city, after, distance);
            if (static_cast<int>(route.size()) < hard_max_size) {
                if (delta + kEps < best.delta) {
                    best = {route_idx, after, delta};
                }
            } else if (delta + kEps < best_overflow.delta) {
                best_overflow = {route_idx, after, delta};
            }
        }
        return best.route >= 0 ? best : best_overflow;
    }

    bool AbsorbSmallOpenRoutesV16(std::vector<std::vector<int>>& open_routes,
                                  int target_count,
                                  int hard_max_size,
                                  const CandidateSets& candidate_sets,
                                  int node_count,
                                  DistanceOracleV5& distance,
                                  SearchBudgetV5& budget) const {
        if (target_count <= 0) {
            return false;
        }
        while (open_routes.size() > static_cast<size_t>(target_count) && !budget.ShouldStop()) {
            size_t remove_idx = open_routes.size();
            double best_remove_score = std::numeric_limits<double>::max();
            for (size_t route_idx = 0; route_idx < open_routes.size(); ++route_idx) {
                if (open_routes[route_idx].empty()) {
                    remove_idx = route_idx;
                    best_remove_score = -1.0;
                    break;
                }
                const double length = OpenRoutesTotalLengthV16(std::vector<std::vector<int>>{open_routes[route_idx]}, distance);
                const double score = static_cast<double>(open_routes[route_idx].size()) * 1000.0 + length;
                if (score + kEps < best_remove_score) {
                    best_remove_score = score;
                    remove_idx = route_idx;
                }
            }
            if (remove_idx == open_routes.size()) {
                return false;
            }

            std::vector<int> removed = std::move(open_routes[remove_idx]);
            open_routes.erase(open_routes.begin() + static_cast<std::ptrdiff_t>(remove_idx));
            if (removed.empty()) {
                continue;
            }

            std::vector<int> route_of;
            std::vector<int> pos_in_route;
            BuildOpenRoutePositionIndexV16(open_routes, node_count, route_of, pos_in_route);

            bool failed = false;
            for (int city : removed) {
                OpenInsertionChoiceV16 choice = FindBestOpenInsertionV16(city,
                                                                         open_routes,
                                                                         candidate_sets,
                                                                         route_of,
                                                                         pos_in_route,
                                                                         hard_max_size,
                                                                         distance);
                if (choice.route < 0) {
                    failed = true;
                    break;
                }
                auto& target_route = open_routes[static_cast<size_t>(choice.route)];
                const int insert_pos = choice.after + 1;
                target_route.insert(target_route.begin() + static_cast<std::ptrdiff_t>(insert_pos), city);
                ReindexOpenRouteRangeV16(open_routes, choice.route, insert_pos, route_of, pos_in_route);
            }
            if (failed) {
                open_routes.push_back(std::move(removed));
                return false;
            }
        }
        return open_routes.size() <= static_cast<size_t>(target_count);
    }

    bool BuildSavingsSeedRoutesV16(RouteSet& out,
                                   const Instance& inst,
                                   const CandidateSets& savings_candidates,
                                   DistanceOracleV5& distance,
                                   SearchBudgetV5& budget) const {
        const int node_count = inst.GetNodeCount();
        const int m = inst.GetSalesmanCount();
        if (node_count <= 1 || m <= 0 || savings_candidates.size() < static_cast<size_t>(node_count)) {
            return false;
        }

        const int customers = node_count - 1;
        const int target_size = std::max(1, (customers + m - 1) / m);
        const int hard_max_size = std::max(1, static_cast<int>(std::ceil(target_size * (1.0 + savings_route_slack_))));
        const int extra_route_target = std::max(m, EffectiveSavingsExtraRouteCount(node_count, m));

        std::vector<SavingsRouteV16> routes(static_cast<size_t>(customers));
        std::vector<int> route_of(static_cast<size_t>(node_count), -1);
        for (int city = 1; city < node_count; ++city) {
            const int route_idx = city - 1;
            routes[static_cast<size_t>(route_idx)].nodes.push_back(city);
            route_of[static_cast<size_t>(city)] = route_idx;
        }

        std::vector<SavingsEdgeV16> savings;
        const size_t reserve_per_node = static_cast<size_t>(std::min(EffectiveSavingsCandidateCount(node_count), 64));
        savings.reserve(static_cast<size_t>(customers) * reserve_per_node);
        for (int city = 1; city < node_count && !budget.ShouldStop(); ++city) {
            for (int neighbor : savings_candidates[static_cast<size_t>(city)]) {
                if (neighbor <= city || neighbor >= node_count) {
                    continue;
                }
                const double saving = distance.DepotDistance(city) +
                                      distance.DepotDistance(neighbor) -
                                      savings_lambda_ * distance(city, neighbor);
                if (saving > kEps) {
                    savings.push_back({city, neighbor, saving});
                }
            }
        }
        if (savings.empty() || budget.ForceCheck()) {
            return false;
        }

        std::sort(savings.begin(), savings.end(), [](const SavingsEdgeV16& lhs, const SavingsEdgeV16& rhs) {
            if (std::abs(lhs.saving - rhs.saving) > kEps) {
                return lhs.saving > rhs.saving;
            }
            if (lhs.from != rhs.from) {
                return lhs.from < rhs.from;
            }
            return lhs.to < rhs.to;
        });

        int active_count = customers;
        for (const auto& edge : savings) {
            if (active_count <= extra_route_target || budget.ShouldStop()) {
                break;
            }
            if (MergeSavingsRoutesV16(routes, route_of, edge.from, edge.to, hard_max_size)) {
                --active_count;
            }
        }
        last_metadata_["savings_active_after_cw"] = std::to_string(active_count);

        const int pairwise_safe_limit = static_cast<int>(
            std::max(static_cast<size_t>(extra_route_target * 96), static_cast<size_t>(extra_route_target + 256)));
        if (active_count > pairwise_safe_limit && !budget.ShouldStop()) {
            ReduceSavingsRoutesByEndpointQueueV16(routes,
                                                  route_of,
                                                  active_count,
                                                  pairwise_safe_limit,
                                                  savings_candidates,
                                                  distance,
                                                  budget);
        }
        last_metadata_["savings_active_after_endpoint_merge"] = std::to_string(active_count);

        std::vector<std::vector<int>> open_routes;
        open_routes.reserve(static_cast<size_t>(active_count));
        for (const auto& route : routes) {
            if (route.active && !route.nodes.empty()) {
                open_routes.push_back(route.nodes);
            }
        }
        if (open_routes.empty()) {
            return false;
        }
        last_metadata_["savings_open_routes_before_exact_m"] = std::to_string(open_routes.size());
        if (open_routes.size() > static_cast<size_t>(extra_route_target) && !budget.ShouldStop()) {
            AbsorbSmallOpenRoutesV16(open_routes,
                                     extra_route_target,
                                     hard_max_size,
                                     savings_candidates,
                                     node_count,
                                     distance,
                                     budget);
        }
        last_metadata_["savings_open_routes_after_absorb"] = std::to_string(open_routes.size());
        if (open_routes.size() > static_cast<size_t>(extra_route_target)) {
            if (!MergeOpenRoutesToCountV16(open_routes, extra_route_target, distance, budget)) {
                return false;
            }
        }
        CleanupOpenRoutesV16(open_routes, savings_candidates, node_count, distance, budget);
        last_metadata_["savings_extra_routes_count"] = std::to_string(open_routes.size());
        last_metadata_["savings_extra_routes_sum"] = std::to_string(OpenRoutesTotalLengthV16(open_routes, distance));
        if (open_routes.size() > static_cast<size_t>(m)) {
            if (!MergeOpenRoutesToCountV16(open_routes, m, distance, budget, true)) {
                return false;
            }
        }
        SplitOpenRoutesToCountV16(open_routes, m);
        CleanupOpenRoutesV16(open_routes, savings_candidates, node_count, distance, budget);
        last_metadata_["savings_exact_m_sum"] = std::to_string(OpenRoutesTotalLengthV16(open_routes, distance));

        out.clear();
        out.reserve(static_cast<size_t>(m));
        for (int route_idx = 0; route_idx < m; ++route_idx) {
            std::vector<int> closed;
            closed.reserve(open_routes[static_cast<size_t>(route_idx)].size() + 2ULL);
            closed.push_back(0);
            closed.insert(closed.end(),
                          open_routes[static_cast<size_t>(route_idx)].begin(),
                          open_routes[static_cast<size_t>(route_idx)].end());
            closed.push_back(0);
            out.push_back(std::move(closed));
        }
        return out.size() == static_cast<size_t>(m);
    }

    int EffectiveRepairRuinCount(int node_count) const {
        if (route_repair_ruin_ > 0) {
            return route_repair_ruin_;
        }
        if (node_count >= 100000) {
            return 192;
        }
        if (node_count >= 50000) {
            return 144;
        }
        if (node_count >= 10000) {
            return 96;
        }
        return 32;
    }

    struct RepairInsertChoiceV16 {
        size_t route = 0;
        size_t after = 0;
        double delta = std::numeric_limits<double>::max();
        bool found = false;
    };

    RepairInsertChoiceV16 FindBestRepairInsertionV16(int city,
                                                     const RouteSet& routes,
                                                     const CandidateSets& global_candidates,
                                                     const std::vector<int>& route_of,
                                                     const std::vector<size_t>& pos_in_route,
                                                     const std::vector<int>& route_sizes,
                                                     int hard_max_size,
                                                     DistanceOracleV5& distance,
                                                     std::mt19937& rng) const {
        RepairInsertChoiceV16 best;
        RepairInsertChoiceV16 best_overflow;
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            const auto& route = routes[route_idx];
            if (route.size() < 2) {
                continue;
            }

            std::vector<size_t> positions;
            positions.reserve(global_candidates[static_cast<size_t>(city)].size() * 2ULL + 8ULL);
            positions.push_back(0);
            positions.push_back(route.size() - 2);

            if (route.size() > 3) {
                std::uniform_int_distribution<size_t> random_after(0, route.size() - 2);
                for (int sample = 0; sample < 3; ++sample) {
                    positions.push_back(random_after(rng));
                }
            }

            for (int neighbor : global_candidates[static_cast<size_t>(city)]) {
                if (neighbor <= 0 || neighbor >= static_cast<int>(route_of.size()) ||
                    route_of[static_cast<size_t>(neighbor)] != static_cast<int>(route_idx)) {
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
            for (size_t after : positions) {
                if (after + 1 >= route.size()) {
                    continue;
                }
                const int lhs = route[after];
                const int rhs = route[after + 1];
                const double delta = distance(lhs, city) + distance(city, rhs) - distance(lhs, rhs);
                RepairInsertChoiceV16 choice{route_idx, after, delta, true};
                if (route_sizes[route_idx] < hard_max_size) {
                    if (!best.found || delta + kEps < best.delta) {
                        best = choice;
                    }
                } else if (!best_overflow.found || delta + kEps < best_overflow.delta) {
                    best_overflow = choice;
                }
            }
        }
        return best.found ? best : best_overflow;
    }

    bool RepairRoutesMinsumV16(RouteSet& routes,
                               const CandidateSets& local_candidates,
                               const CandidateSets& global_candidates,
                               DistanceOracleV5& distance,
                               SearchBudgetV5& budget,
                               RouteRepairStatsV16& stats) const {
        const Instance& inst = Instance::GetInstance();
        const int node_count = inst.GetNodeCount();
        const int route_count = static_cast<int>(routes.size());
        if (route_count < 2 || node_count <= 2 || global_candidates.size() < static_cast<size_t>(node_count)) {
            stats.before_sum = RouteSumLengthV7(routes, distance);
            stats.after_sum = stats.before_sum;
            return false;
        }

        EnsureClosedDepotRoutes(routes);
        RouteSet current = routes;
        RouteSet best = routes;
        double current_sum = RouteSumLengthV7(current, distance);
        double best_sum = current_sum;
        stats.before_sum = current_sum;
        stats.after_sum = current_sum;

        const int target_size = std::max(1, (node_count - 1 + route_count - 1) / route_count);
        const int hard_max_size = std::max(target_size + 1,
                                           static_cast<int>(std::ceil(target_size * (1.0 + route_repair_size_slack_))));
        const int ruin_limit = EffectiveRepairRuinCount(node_count);
        std::mt19937 repair_rng(seed_ ^ 0x7F4A7C15U ^ static_cast<unsigned int>(node_count));

        while (!budget.ShouldStop()) {
            std::vector<double> route_lengths(current.size(), 0.0);
            std::vector<int> route_sizes(current.size(), 0);
            size_t ruin_route = current.size();
            for (size_t route_idx = 0; route_idx < current.size(); ++route_idx) {
                route_lengths[route_idx] = RouteLengthGenericV5(current[route_idx], distance);
                route_sizes[route_idx] = std::max(0, static_cast<int>(current[route_idx].size()) - 2);
                if (route_sizes[route_idx] > 1 &&
                    (ruin_route == current.size() || route_lengths[route_idx] > route_lengths[ruin_route])) {
                    ruin_route = route_idx;
                }
            }
            if (ruin_route == current.size()) {
                break;
            }

            RouteSet candidate = current;
            const int available = std::max(0, static_cast<int>(candidate[ruin_route].size()) - 2);
            const int take = std::min(std::max(1, available - 1), ruin_limit);
            if (take <= 0) {
                break;
            }

            std::uniform_int_distribution<int> center_dist(1, available);
            const int center = center_dist(repair_rng);
            int left = std::max(1, center - take / 2);
            if (left + take > available + 1) {
                left = std::max(1, available + 1 - take);
            }
            const int right = left + take;

            std::vector<int> removed;
            removed.reserve(static_cast<size_t>(take));
            for (int pos = left; pos < right; ++pos) {
                removed.push_back(candidate[ruin_route][static_cast<size_t>(pos)]);
            }
            candidate[ruin_route].erase(candidate[ruin_route].begin() + left,
                                        candidate[ruin_route].begin() + right);
            std::shuffle(removed.begin(), removed.end(), repair_rng);

            std::vector<char> touched(candidate.size(), 0);
            touched[ruin_route] = 1;
            // FIX-B: build the route/position index ONCE per ruin attempt, not
            // once per re-inserted city. The original v12 loop rebuilt the
            // entire O(n) index inside the inner loop, costing O(ruin * n) per
            // attempt - on n=100000 with ruin=192 that is ~19.2M ops per
            // attempt just on indexing. Now: build once + ReindexRouteRange
            // from insert_pos in only the touched route after every insert.
            // The removed cities are temporarily marked as orphaned (route_of
            // = -1) so FindBestRepairInsertion does not pick insertion slots
            // adjacent to a city that has not been re-inserted yet.
            const auto insert_loop_start = std::chrono::steady_clock::now();
            std::vector<int> route_of;
            std::vector<size_t> pos_in_route;
            std::vector<int> current_sizes;
            BuildRoutePositionIndex(candidate, node_count, route_of, pos_in_route, current_sizes);
            for (int city : removed) {
                route_of[static_cast<size_t>(city)] = -1;
                pos_in_route[static_cast<size_t>(city)] = 0;
            }

            bool failed = false;
            for (int city : removed) {
                RepairInsertChoiceV16 choice = FindBestRepairInsertionV16(city,
                                                                          candidate,
                                                                          global_candidates,
                                                                          route_of,
                                                                          pos_in_route,
                                                                          current_sizes,
                                                                          hard_max_size,
                                                                          distance,
                                                                          repair_rng);
                if (!choice.found) {
                    failed = true;
                    break;
                }
                const size_t insert_pos = choice.after + 1;
                candidate[choice.route].insert(candidate[choice.route].begin() + static_cast<std::ptrdiff_t>(insert_pos), city);
                touched[choice.route] = 1;
                ++current_sizes[choice.route];
                ReindexRouteRange(candidate, choice.route, insert_pos, route_of, pos_in_route);
            }
            stats.insert_loop_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - insert_loop_start).count();
            if (failed) {
                break;
            }

            const auto cleanup_loop_start = std::chrono::steady_clock::now();
            for (size_t route_idx = 0; route_idx < candidate.size(); ++route_idx) {
                if (touched[route_idx]) {
                    QuickRouteCleanupV5(candidate[route_idx],
                                        local_candidates,
                                        distance,
                                        node_count,
                                        std::max(1, guided_cleanup_passes_),
                                        budget);
                }
            }
            stats.cleanup_loop_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cleanup_loop_start).count();
            EnsureClosedDepotRoutes(candidate);
            const double candidate_sum = RouteSumLengthV7(candidate, distance);
            ++stats.attempts;
            if (candidate_sum + kEps < current_sum) {
                current.swap(candidate);
                current_sum = candidate_sum;
                ++stats.accepted;
                if (candidate_sum + kEps < best_sum) {
                    best = current;
                    best_sum = candidate_sum;
                }
            } else if (stats.attempts > 6 && stats.accepted == 0) {
                break;
            }
        }

        stats.after_sum = best_sum;
        routes.swap(best);
        return best_sum + kEps < stats.before_sum;
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

    bool AnnealRoutesMinsumv16(RouteSet& routes,
                               const CandidateSets& global_candidates,
                               DistanceOracleV5& distance,
                               SearchBudgetV5& budget,
                               int max_iterations,
                               RouteAnnealStatsv16& stats) const {
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

    // ------------------------------------------------------------------ //
    //  NEW in v15: OrOpt1 cross-route sweep                               //
    // ------------------------------------------------------------------ //
    // Greedy first-improving single-node relocate across routes.
    // For each node u, checks all k-NN neighbors v in different routes.
    // Applies move if removal_gain > insertion_cost, repeats until no improvement.
    // O(n * k) per sweep. Uses global_candidates for neighbor lookup.
    bool OrOpt1CrossRouteSweepV16(RouteSet& routes,
                                   const CandidateSets& global_candidates,
                                   DistanceOracleV5& distance,
                                   SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        const int node_count = inst.GetNodeCount();
        if (routes.size() < 2 || node_count <= 2) return false;

        std::vector<int> route_of;
        std::vector<size_t> pos_in_route;
        std::vector<int> route_sizes;
        BuildRoutePositionIndex(routes, node_count, route_of, pos_in_route, route_sizes);

        bool any = false;
        bool improved = true;
        while (improved && !budget.ShouldStop()) {
            improved = false;
            for (int u = 1; u < node_count && !budget.ShouldStop(); ++u) {
                const int r_from = route_of[static_cast<size_t>(u)];
                if (r_from < 0 || route_sizes[r_from] <= 1) continue;
                const size_t pos_u = pos_in_route[static_cast<size_t>(u)];
                if (pos_u == 0 || pos_u + 1 >= routes[static_cast<size_t>(r_from)].size()) continue;

                const int prev_u = routes[static_cast<size_t>(r_from)][pos_u - 1];
                const int next_u = routes[static_cast<size_t>(r_from)][pos_u + 1];
                const double removal_gain =
                    distance(prev_u, u) + distance(u, next_u) - distance(prev_u, next_u);

                for (int v : global_candidates[static_cast<size_t>(u)]) {
                    if (v <= 0 || v >= node_count) continue;
                    const int r_to = route_of[static_cast<size_t>(v)];
                    if (r_to < 0 || r_to == r_from) continue;
                    const size_t pos_v = pos_in_route[static_cast<size_t>(v)];
                    const auto& to_route = routes[static_cast<size_t>(r_to)];

                    // Try insert after v
                    if (pos_v + 1 < to_route.size()) {
                        const int rhs = to_route[pos_v + 1];
                        const double ins = distance(v, u) + distance(u, rhs) - distance(v, rhs);
                        if (removal_gain - ins > kEps) {
                            routes[static_cast<size_t>(r_from)].erase(
                                routes[static_cast<size_t>(r_from)].begin() +
                                static_cast<std::ptrdiff_t>(pos_u));
                            --route_sizes[r_from];
                            const size_t ins_at = pos_v + 1;
                            routes[static_cast<size_t>(r_to)].insert(
                                routes[static_cast<size_t>(r_to)].begin() +
                                static_cast<std::ptrdiff_t>(ins_at), u);
                            ++route_sizes[r_to];
                            route_of[static_cast<size_t>(u)] = r_to;
                            for (size_t p = pos_u; p < routes[static_cast<size_t>(r_from)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_from)][p];
                                if (nd > 0) pos_in_route[static_cast<size_t>(nd)] = p;
                            }
                            for (size_t p = ins_at; p < routes[static_cast<size_t>(r_to)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_to)][p];
                                if (nd > 0) {
                                    route_of[static_cast<size_t>(nd)] = r_to;
                                    pos_in_route[static_cast<size_t>(nd)] = p;
                                }
                            }
                            improved = any = true;
                            goto next_u_or1;
                        }
                    }
                    // Try insert before v
                    if (pos_v > 0) {
                        const int lhs = to_route[pos_v - 1];
                        const double ins = distance(lhs, u) + distance(u, v) - distance(lhs, v);
                        if (removal_gain - ins > kEps) {
                            routes[static_cast<size_t>(r_from)].erase(
                                routes[static_cast<size_t>(r_from)].begin() +
                                static_cast<std::ptrdiff_t>(pos_u));
                            --route_sizes[r_from];
                            const size_t ins_at = pos_v;
                            routes[static_cast<size_t>(r_to)].insert(
                                routes[static_cast<size_t>(r_to)].begin() +
                                static_cast<std::ptrdiff_t>(ins_at), u);
                            ++route_sizes[r_to];
                            route_of[static_cast<size_t>(u)] = r_to;
                            for (size_t p = pos_u; p < routes[static_cast<size_t>(r_from)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_from)][p];
                                if (nd > 0) pos_in_route[static_cast<size_t>(nd)] = p;
                            }
                            for (size_t p = ins_at; p < routes[static_cast<size_t>(r_to)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_to)][p];
                                if (nd > 0) {
                                    route_of[static_cast<size_t>(nd)] = r_to;
                                    pos_in_route[static_cast<size_t>(nd)] = p;
                                }
                            }
                            improved = any = true;
                            goto next_u_or1;
                        }
                    }
                }
                next_u_or1:;
            }
        }
        return any;
    }

    // ------------------------------------------------------------------ //
    //  NEW in v15: OrOpt2 cross-route sweep                               //
    // ------------------------------------------------------------------ //
    // Greedy first-improving 2-node segment relocate across routes.
    // Tries both orientations (forward and reversed) of the segment.
    bool OrOpt2CrossRouteSweepV16(RouteSet& routes,
                                   const CandidateSets& global_candidates,
                                   DistanceOracleV5& distance,
                                   SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        const int node_count = inst.GetNodeCount();
        if (routes.size() < 2 || node_count <= 2) return false;

        std::vector<int> route_of;
        std::vector<size_t> pos_in_route;
        std::vector<int> route_sizes;
        BuildRoutePositionIndex(routes, node_count, route_of, pos_in_route, route_sizes);

        bool any = false;
        bool improved = true;
        while (improved && !budget.ShouldStop()) {
            improved = false;
            for (int u = 1; u < node_count && !budget.ShouldStop(); ++u) {
                const int r_from = route_of[static_cast<size_t>(u)];
                if (r_from < 0 || route_sizes[r_from] <= 2) continue;
                const size_t pos_u = pos_in_route[static_cast<size_t>(u)];
                const auto& from_route = routes[static_cast<size_t>(r_from)];
                if (pos_u == 0 || pos_u + 2 >= from_route.size()) continue;

                const int prev_u = from_route[pos_u - 1];
                const int u_next = from_route[pos_u + 1];
                const int next_next = from_route[pos_u + 2];
                if (u_next == 0) continue;  // u_next is depot
                // removal_part = change in r_from when pair (u, u_next) removed
                // = d(prev_u, next_next) - d(prev_u, u) - d(u_next, next_next)
                // note: d(u, u_next) stays in solution
                const double removal_part =
                    distance(prev_u, next_next) - distance(prev_u, u) - distance(u_next, next_next);

                for (int v : global_candidates[static_cast<size_t>(u)]) {
                    if (v <= 0 || v >= node_count) continue;
                    const int r_to = route_of[static_cast<size_t>(v)];
                    if (r_to < 0 || r_to == r_from) continue;
                    const size_t pos_v = pos_in_route[static_cast<size_t>(v)];
                    const auto& to_route = routes[static_cast<size_t>(r_to)];

                    // Try insert pair after v
                    if (pos_v + 1 < to_route.size()) {
                        const int rhs = to_route[pos_v + 1];
                        // Forward: v → u → u_next → rhs
                        const double ins_fwd = distance(v, u) + distance(u_next, rhs) - distance(v, rhs);
                        // Reversed: v → u_next → u → rhs
                        const double ins_rev = distance(v, u_next) + distance(u, rhs) - distance(v, rhs);
                        const bool use_rev = ins_rev + kEps < ins_fwd;
                        const double best_ins = use_rev ? ins_rev : ins_fwd;
                        if (removal_part + best_ins < -kEps) {
                            routes[static_cast<size_t>(r_from)].erase(
                                routes[static_cast<size_t>(r_from)].begin() +
                                    static_cast<std::ptrdiff_t>(pos_u),
                                routes[static_cast<size_t>(r_from)].begin() +
                                    static_cast<std::ptrdiff_t>(pos_u + 2));
                            route_sizes[r_from] -= 2;
                            const size_t ins_at = pos_v + 1;
                            if (!use_rev) {
                                routes[static_cast<size_t>(r_to)].insert(
                                    routes[static_cast<size_t>(r_to)].begin() +
                                        static_cast<std::ptrdiff_t>(ins_at),
                                    {u, u_next});
                            } else {
                                routes[static_cast<size_t>(r_to)].insert(
                                    routes[static_cast<size_t>(r_to)].begin() +
                                        static_cast<std::ptrdiff_t>(ins_at),
                                    {u_next, u});
                            }
                            route_sizes[r_to] += 2;
                            for (size_t p = pos_u; p < routes[static_cast<size_t>(r_from)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_from)][p];
                                if (nd > 0) pos_in_route[static_cast<size_t>(nd)] = p;
                            }
                            for (size_t p = ins_at; p < routes[static_cast<size_t>(r_to)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_to)][p];
                                if (nd > 0) {
                                    route_of[static_cast<size_t>(nd)] = r_to;
                                    pos_in_route[static_cast<size_t>(nd)] = p;
                                }
                            }
                            improved = any = true;
                            goto next_u_or2;
                        }
                    }
                    // Try insert pair before v
                    if (pos_v > 0) {
                        const int lhs = to_route[pos_v - 1];
                        const double ins_fwd = distance(lhs, u) + distance(u_next, v) - distance(lhs, v);
                        const double ins_rev = distance(lhs, u_next) + distance(u, v) - distance(lhs, v);
                        const bool use_rev = ins_rev + kEps < ins_fwd;
                        const double best_ins = use_rev ? ins_rev : ins_fwd;
                        if (removal_part + best_ins < -kEps) {
                            routes[static_cast<size_t>(r_from)].erase(
                                routes[static_cast<size_t>(r_from)].begin() +
                                    static_cast<std::ptrdiff_t>(pos_u),
                                routes[static_cast<size_t>(r_from)].begin() +
                                    static_cast<std::ptrdiff_t>(pos_u + 2));
                            route_sizes[r_from] -= 2;
                            const size_t ins_at = pos_v;
                            if (!use_rev) {
                                routes[static_cast<size_t>(r_to)].insert(
                                    routes[static_cast<size_t>(r_to)].begin() +
                                        static_cast<std::ptrdiff_t>(ins_at),
                                    {u, u_next});
                            } else {
                                routes[static_cast<size_t>(r_to)].insert(
                                    routes[static_cast<size_t>(r_to)].begin() +
                                        static_cast<std::ptrdiff_t>(ins_at),
                                    {u_next, u});
                            }
                            route_sizes[r_to] += 2;
                            for (size_t p = pos_u; p < routes[static_cast<size_t>(r_from)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_from)][p];
                                if (nd > 0) pos_in_route[static_cast<size_t>(nd)] = p;
                            }
                            for (size_t p = ins_at; p < routes[static_cast<size_t>(r_to)].size(); ++p) {
                                const int nd = routes[static_cast<size_t>(r_to)][p];
                                if (nd > 0) {
                                    route_of[static_cast<size_t>(nd)] = r_to;
                                    pos_in_route[static_cast<size_t>(nd)] = p;
                                }
                            }
                            improved = any = true;
                            goto next_u_or2;
                        }
                    }
                }
                next_u_or2:;
            }
        }
        return any;
    }

    // ------------------------------------------------------------------ //
    //  NEW in v15: FILO-style SA ruin-recreate main loop                  //
    // ------------------------------------------------------------------ //
    // Inspired by FILO2 (Accorsi 2022): iterative destroy-repair with
    // Simulated Annealing acceptance. Runs until budget exhausted.
    // Key differences from RepairRoutesMinsumV16:
    //   - SA acceptance (not greedy-only)
    //   - Keeps running after non-improving iterations
    //   - Source route selected proportional to route length (not always longest)
    //   - Adaptive ruin size
    //   - Periodic intra-route 2-opt cleanup every cleanup_interval iters
    void FiloMainLoopV16(RouteSet& best_minsum,
                          double& best_minsum_sum,
                          double& best_minsum_max,
                          RouteSet& best_balanced,
                          double& best_balanced_sum,
                          double& best_balanced_max,
                          const CandidateSets& local_candidates,
                          const CandidateSets& global_candidates,
                          const Instance& inst,
                          DistanceOracleV5& distance,
                          SearchBudgetV5& budget) const {
        const int node_count = inst.GetNodeCount();
        const int route_count = static_cast<int>(best_minsum.size());
        if (route_count < 2 || node_count <= 2) return;

        EnsureClosedDepotRoutes(best_minsum);
        RouteSet current = best_minsum;

        const int target_size = std::max(1, (node_count - 1 + route_count - 1) / route_count);
        const int hard_max_size = std::max(target_size + 1,
            static_cast<int>(std::ceil(target_size * (1.0 + route_repair_size_slack_))));

        // Incremental route lengths
        std::vector<double> route_lengths(static_cast<size_t>(route_count), 0.0);
        for (int r = 0; r < route_count; ++r)
            route_lengths[static_cast<size_t>(r)] = RouteLengthGenericV5(current[static_cast<size_t>(r)], distance);
        double current_sum = std::accumulate(route_lengths.begin(), route_lengths.end(), 0.0);

        // Position index
        std::vector<int> route_of;
        std::vector<size_t> pos_in_route;
        std::vector<int> route_sizes;
        BuildRoutePositionIndex(current, node_count, route_of, pos_in_route, route_sizes);

        // SA temperature schedule: linear cooling calibrated to expected move cost.
        // start_temp ≈ typical worsening delta so we accept ~60% of avg bad moves initially.
        // end_temp = 4% of start so we accept ~2% at the end — slow fade, not hard cutoff.
        const auto filo_start = std::chrono::steady_clock::now();
        const double avg_edge = current_sum / std::max(1, node_count - 1);
        const int min_ruin = std::max(3, node_count / 35000);
        const int max_ruin = std::max(40, node_count / 2500);
        const double start_temp = avg_edge * std::max(3, min_ruin) * 3.0;
        const double end_temp   = start_temp * 0.04;

        // Start with larger ruin to diversify early; shrink toward min when improving.
        int ruin_size = (min_ruin + max_ruin) / 2;

        // Cleanup interval: every N iters run a quick 2-opt pass on all routes.
        // Less frequent than before (was n/3000) to leave more time for FILO iters.
        const int cleanup_interval = std::max(80, node_count / 1000);

        std::mt19937 filo_rng(seed_ ^ 0xFAB10A7EU ^ static_cast<unsigned int>(node_count));
        std::uniform_real_distribution<double> prob_dist(0.0, 1.0);

        int iter = 0;
        int no_improve = 0;

        while (!budget.ShouldStop()) {
            // Time-based temperature: linear decay (stays warm much longer than geometric).
            const auto now_tp = std::chrono::steady_clock::now();
            const double elapsed_ms = static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - filo_start).count());
            const double total_filo_ms = elapsed_ms +
                static_cast<double>(budget.Enabled() ? budget.RemainingMs() : 60000);
            const double frac = std::min(1.0, elapsed_ms / std::max(1.0, total_filo_ms));
            const double temperature = start_temp + (end_temp - start_temp) * frac;

            // Shaw removal: select ruin_size spatially close cities across all routes.
            // Pick a random seed city, collect its nearest neighbours via k-NN expansion.
            // This enables cross-route ruin that consecutive-segment ruin cannot reach.
            std::uniform_int_distribution<int> node_dist(1, node_count - 1);
            const int seed_city = node_dist(filo_rng);
            if (route_of[static_cast<size_t>(seed_city)] < 0) { ++iter; continue; }

            // Build pool: seed's k-NN candidates, expanded one hop if not enough
            struct ShawCand { int city; double d; };
            std::vector<ShawCand> shaw_pool;
            shaw_pool.reserve(static_cast<size_t>(ruin_size * 10));
            shaw_pool.push_back({seed_city, 0.0});
            for (int nb : global_candidates[static_cast<size_t>(seed_city)]) {
                if (nb > 0 && route_of[static_cast<size_t>(nb)] >= 0)
                    shaw_pool.push_back({nb, distance(seed_city, nb)});
            }
            if (static_cast<int>(shaw_pool.size()) < ruin_size + 4) {
                for (int nb1 : global_candidates[static_cast<size_t>(seed_city)]) {
                    if (nb1 <= 0 || route_of[static_cast<size_t>(nb1)] < 0) continue;
                    for (int nb2 : global_candidates[static_cast<size_t>(nb1)]) {
                        if (nb2 > 0 && route_of[static_cast<size_t>(nb2)] >= 0)
                            shaw_pool.push_back({nb2, distance(seed_city, nb2)});
                    }
                }
            }
            std::sort(shaw_pool.begin(), shaw_pool.end(),
                      [](const ShawCand& a, const ShawCand& b) {
                          return a.d < b.d || (a.d == b.d && a.city < b.city);
                      });
            shaw_pool.erase(
                std::unique(shaw_pool.begin(), shaw_pool.end(),
                            [](const ShawCand& a, const ShawCand& b) { return a.city == b.city; }),
                shaw_pool.end());

            const int take = std::min(ruin_size, static_cast<int>(shaw_pool.size()));
            if (take <= 0) { ++iter; continue; }

            std::vector<int> removed;
            removed.reserve(static_cast<size_t>(take));
            for (int k = 0; k < take; ++k)
                removed.push_back(shaw_pool[static_cast<size_t>(k)].city);

            // Group removed positions by route (sorted) — needed for delta and erase
            std::vector<std::vector<size_t>> removed_pos_by_route(static_cast<size_t>(route_count));
            for (int city : removed)
                removed_pos_by_route[static_cast<size_t>(route_of[static_cast<size_t>(city)])].push_back(
                    pos_in_route[static_cast<size_t>(city)]);
            for (auto& v : removed_pos_by_route) std::sort(v.begin(), v.end());

            // Incremental ruin delta: walk each affected route, compute per-block edge delta
            double ruin_delta = 0.0;
            for (int r = 0; r < route_count; ++r) {
                const auto& rpos = removed_pos_by_route[static_cast<size_t>(r)];
                if (rpos.empty()) continue;
                const auto& route_r = current[static_cast<size_t>(r)];
                size_t pi = 0;
                while (pi < rpos.size()) {
                    const size_t blk_s = rpos[pi];
                    size_t blk_e = blk_s;
                    size_t pj = pi + 1;
                    while (pj < rpos.size() && rpos[pj] == blk_e + 1) blk_e = rpos[pj++];
                    // Predecessor and successor in the kept sequence are guaranteed valid:
                    // blk_s >= 1 (no depot removal) and blk_e+1 <= route end (depot stays).
                    const int pred = route_r[blk_s - 1];
                    const int succ = route_r[blk_e + 1];
                    ruin_delta += distance(pred, succ)
                                - distance(pred, route_r[blk_s])
                                - distance(route_r[blk_e], succ);
                    for (size_t k2 = blk_s; k2 < blk_e; ++k2)
                        ruin_delta -= distance(route_r[k2], route_r[k2 + 1]);
                    pi = pj;
                }
            }

            // Build candidate: erase removed cities from each affected route
            RouteSet candidate = current;
            std::vector<int> cand_route_of = route_of;
            std::vector<size_t> cand_pos = pos_in_route;
            std::vector<int> cand_sizes = route_sizes;
            for (int city : removed) cand_route_of[static_cast<size_t>(city)] = -1;
            for (int r = 0; r < route_count; ++r) {
                const auto& rpos = removed_pos_by_route[static_cast<size_t>(r)];
                if (rpos.empty()) continue;
                // Erase in reverse position order to keep indices valid
                for (auto it = rpos.rbegin(); it != rpos.rend(); ++it)
                    candidate[static_cast<size_t>(r)].erase(
                        candidate[static_cast<size_t>(r)].begin() +
                        static_cast<std::ptrdiff_t>(*it));
                cand_sizes[static_cast<size_t>(r)] -= static_cast<int>(rpos.size());
                // Reindex from first affected position
                const size_t first_aff = rpos.front() > 0 ? rpos.front() - 1 : 0;
                for (size_t p = first_aff; p < candidate[static_cast<size_t>(r)].size(); ++p) {
                    const int nd = candidate[static_cast<size_t>(r)][p];
                    if (nd > 0) {
                        cand_route_of[static_cast<size_t>(nd)] = r;
                        cand_pos[static_cast<size_t>(nd)] = p;
                    }
                }
            }

            // Recreate: shuffle and greedily reinsert using global candidates
            std::shuffle(removed.begin(), removed.end(), filo_rng);
            std::vector<char> touched(static_cast<size_t>(route_count), 0);
            for (int city : removed)
                touched[static_cast<size_t>(route_of[static_cast<size_t>(city)])] = 1;

            double insert_delta = 0.0;
            bool failed = false;
            for (int city : removed) {
                RepairInsertChoiceV16 choice = FindBestRepairInsertionV16(
                    city, candidate, global_candidates, cand_route_of, cand_pos,
                    cand_sizes, hard_max_size, distance, filo_rng);
                if (!choice.found) { failed = true; break; }

                const int lhs = candidate[choice.route][choice.after];
                const int rhs = candidate[choice.route][choice.after + 1];
                insert_delta += distance(lhs, city) + distance(city, rhs) - distance(lhs, rhs);

                const size_t ins_pos = choice.after + 1;
                candidate[choice.route].insert(
                    candidate[choice.route].begin() + static_cast<std::ptrdiff_t>(ins_pos), city);
                ++cand_sizes[choice.route];
                touched[static_cast<size_t>(choice.route)] = 1;
                ReindexRouteRange(candidate, choice.route, ins_pos, cand_route_of, cand_pos);
            }
            if (failed) { ++iter; ++no_improve; continue; }

            const double candidate_sum = current_sum + ruin_delta + insert_delta;
            const double energy_delta = candidate_sum - current_sum;

            // SA acceptance
            bool accepted = false;
            if (energy_delta < -kEps) {
                accepted = true;
            } else if (temperature > 1e-9) {
                accepted = prob_dist(filo_rng) < std::exp(-energy_delta / temperature);
            }

            if (accepted) {
                current.swap(candidate);
                route_of.swap(cand_route_of);
                pos_in_route.swap(cand_pos);
                route_sizes.swap(cand_sizes);
                // Update route lengths for touched routes exactly
                double new_sum = 0.0;
                for (int r = 0; r < route_count; ++r) {
                    if (touched[static_cast<size_t>(r)]) {
                        route_lengths[static_cast<size_t>(r)] =
                            RouteLengthGenericV5(current[static_cast<size_t>(r)], distance);
                    }
                    new_sum += route_lengths[static_cast<size_t>(r)];
                }
                current_sum = new_sum;
                no_improve = 0;

                if (current_sum + kEps < best_minsum_sum) {
                    best_minsum = current;
                    best_minsum_sum = current_sum;
                    best_minsum_max = MaxRouteLengthV7(current, distance);
                }
                // Update balanced archive
                const double cur_max = MaxRouteLengthV7(current, distance);
                if (cur_max + kEps < best_balanced_max ||
                    (std::abs(cur_max - best_balanced_max) <= kEps && current_sum + kEps < best_balanced_sum)) {
                    best_balanced = current;
                    best_balanced_sum = current_sum;
                    best_balanced_max = cur_max;
                }
                // Shrink ruin only on strict improvement (not just SA acceptance).
                if (energy_delta < -kEps && ruin_size > min_ruin) --ruin_size;
            } else {
                ++no_improve;
                // Grow ruin on stagnation: longer window (200 vs 150) to avoid thrashing.
                if (no_improve > 200 && ruin_size < max_ruin) {
                    ++ruin_size;
                    no_improve = 0;
                }
            }

            // Periodic cleanup: intra-route 2-opt on all routes
            if (iter % cleanup_interval == 0 && !budget.ShouldStop()) {
                for (int r = 0; r < route_count; ++r) {
                    QuickRouteCleanupV5(current[static_cast<size_t>(r)],
                                        local_candidates, distance, node_count,
                                        std::max(1, guided_cleanup_passes_), budget);
                }
                // Recompute costs exactly after cleanup
                current_sum = 0.0;
                for (int r = 0; r < route_count; ++r) {
                    route_lengths[static_cast<size_t>(r)] =
                        RouteLengthGenericV5(current[static_cast<size_t>(r)], distance);
                    current_sum += route_lengths[static_cast<size_t>(r)];
                }
                BuildRoutePositionIndex(current, node_count, route_of, pos_in_route, route_sizes);
                if (current_sum + kEps < best_minsum_sum) {
                    best_minsum = current;
                    best_minsum_sum = current_sum;
                    best_minsum_max = MaxRouteLengthV7(current, distance);
                }
            }
            ++iter;
        }
        last_metadata_["filo_iterations"] = std::to_string(iter);
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
    int savings_seed_ms_ = -1;
    int savings_candidate_count_ = 48;
    double savings_route_slack_ = 0.05;
    double savings_lambda_ = 1.0;
    int route_repair_ms_ = -1;
    int route_repair_ruin_ = 0;
    double route_repair_size_slack_ = 0.80;
    int thread_count_ = 0;
    bool omp_polish_enabled_ = true;
    mutable std::mt19937 local_rng_{1337U};
    mutable std::unordered_map<std::string, std::string> last_metadata_;
};

static bool reg_lkh_mtsp_v16 = (SolverFactory::RegisterSolver("lkh-wrapper-v16", []() {
    return std::make_unique<LkhWrapperSolverv16>();
}),
                                true);

} // namespace mtsp
