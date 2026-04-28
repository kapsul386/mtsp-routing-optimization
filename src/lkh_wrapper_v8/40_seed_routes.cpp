std::vector<int> OrderClusterNodesV6(const ClusterInfoV6& cluster,
                                     const Coord& entry_anchor,
                                     const Coord& exit_anchor,
                                     const std::vector<Coord>& coords,
                                     std::mt19937& rng) {
    if (cluster.members.empty()) {
        return {};
    }
    if (cluster.members.size() == 1) {
        return cluster.members;
    }

    int entry_city = cluster.members.front();
    double best_entry = std::numeric_limits<double>::max();
    for (int city : cluster.members) {
        const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], entry_anchor);
        if (d2 + kEps < best_entry) {
            best_entry = d2;
            entry_city = city;
        }
    }

    std::vector<std::pair<double, int>> angular;
    angular.reserve(cluster.members.size());
    for (int city : cluster.members) {
        const auto& p = coords[static_cast<size_t>(city)];
        const double angle = std::atan2(p.second - cluster.centroid.second, p.first - cluster.centroid.first);
        angular.emplace_back(angle, city);
    }
    std::sort(angular.begin(), angular.end(), [](const auto& lhs, const auto& rhs) {
        if (std::abs(lhs.first - rhs.first) > kEps) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });

    std::vector<int> cyclic;
    cyclic.reserve(angular.size());
    for (const auto& [_, city] : angular) {
        cyclic.push_back(city);
    }
    auto it = std::find(cyclic.begin(), cyclic.end(), entry_city);
    if (it != cyclic.end()) {
        std::rotate(cyclic.begin(), it, cyclic.end());
    }

    std::vector<int> forward = cyclic;
    std::vector<int> backward;
    backward.reserve(cyclic.size());
    backward.push_back(entry_city);
    for (auto rit = cyclic.rbegin(); rit != cyclic.rend() - 1; ++rit) {
        backward.push_back(*rit);
    }

    auto tail_distance = [&](const std::vector<int>& order) {
        return SquaredDistanceCoordsV5(coords[static_cast<size_t>(order.back())], exit_anchor);
    };

    const double forward_score = tail_distance(forward);
    const double backward_score = tail_distance(backward);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    if (prob(rng) < 0.2) {
        return forward_score <= backward_score ? backward : forward;
    }
    return forward_score <= backward_score ? forward : backward;
}

double OpenRouteLengthV6(const std::vector<int>& route, DistanceOracleV5& distance) {
    if (route.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    total += distance(route.back(), 0);
    return total;
}

double RouteSumLengthV7(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    double total = 0.0;
    for (const auto& route : routes) {
        total += RouteLengthGenericV5(route, distance);
    }
    return total;
}

double MaxRouteLengthV7(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    double best = 0.0;
    for (const auto& route : routes) {
        best = std::max(best, RouteLengthGenericV5(route, distance));
    }
    return best;
}

double ApproxClusterRouteCostV7(const std::vector<int>& clusters, const ClusterModelV6& model) {
    if (clusters.empty()) {
        return 0.0;
    }
    double total = 0.0;
    bool first = true;
    int prev_cluster = -1;
    for (int cluster_id : clusters) {
        const auto& cluster = model.clusters[static_cast<size_t>(cluster_id)];
        if (first) {
            total += cluster.depot_distance;
            first = false;
        } else {
            total += DistanceCoordToPointV6(model.clusters[static_cast<size_t>(prev_cluster)].centroid, cluster.centroid);
        }
        total += 0.5 * cluster.estimate;
        prev_cluster = cluster_id;
    }
    total += model.clusters[static_cast<size_t>(prev_cluster)].depot_distance;
    return total;
}

double ClusterAssignmentEnergyV7(const std::vector<std::vector<int>>& assigned_clusters,
                                 const ClusterModelV6& model) {
    if (assigned_clusters.empty()) {
        return 0.0;
    }
    double max_len = 0.0;
    double sum_len = 0.0;
    int max_nodes = 0;
    int min_nodes = std::numeric_limits<int>::max();
    for (const auto& route : assigned_clusters) {
        const double len = ApproxClusterRouteCostV7(route, model);
        max_len = std::max(max_len, len);
        sum_len += len;
        int nodes = 0;
        for (int cid : route) {
            nodes += static_cast<int>(model.clusters[static_cast<size_t>(cid)].members.size());
        }
        max_nodes = std::max(max_nodes, nodes);
        min_nodes = std::min(min_nodes, nodes);
    }
    if (min_nodes == std::numeric_limits<int>::max()) {
        min_nodes = 0;
    }
    const double imbalance = static_cast<double>(max_nodes - min_nodes);
    return max_len + 0.05 * sum_len + 0.5 * imbalance;
}

void SimulatedAnnealClusterAssignmentV7(std::vector<std::vector<int>>& assigned_clusters,
                                        const ClusterModelV6& model,
                                        const mtsp::Instance& inst,
                                        std::mt19937& rng,
                                        SearchBudgetV5& budget) {
    if (assigned_clusters.empty() || model.clusters.empty()) {
        return;
    }
    int remaining = budget.RemainingMs();
    if (remaining <= 1500) {
        return;
    }
    int max_ms = inst.GetNodeCount() >= 100000 ? 12000 : (inst.GetNodeCount() >= 50000 ? 7000 : 3500);
    const int sa_ms = std::min(max_ms, std::max(500, remaining / 20));
    SearchBudgetV5 sa_budget(sa_ms, 0, 16);

    auto current = assigned_clusters;
    auto best = current;
    double current_energy = ClusterAssignmentEnergyV7(current, model);
    double best_energy = current_energy;

    const int max_iter = inst.GetNodeCount() >= 100000 ? 5000 : (inst.GetNodeCount() >= 50000 ? 3500 : 2000);
    const double start_temp = std::max(1.0, current_energy * 0.01);
    const double end_temp = std::max(1e-3, start_temp * 0.02);
    std::uniform_real_distribution<double> prob(0.0, 1.0);

    for (int iter = 0; iter < max_iter && !budget.ShouldStop() && !sa_budget.ShouldStop(); ++iter) {
        if ((iter & 31) == 0) {
            sa_budget.ForceCheck();
            budget.ForceCheck();
        }
        auto cand = current;
        const double alpha = static_cast<double>(iter) / std::max(1, max_iter - 1);
        const double temp = start_temp * std::pow(end_temp / start_temp, alpha);

        std::vector<double> approx(cand.size(), 0.0);
        size_t longest = 0;
        for (size_t r = 0; r < cand.size(); ++r) {
            approx[r] = ApproxClusterRouteCostV7(cand[r], model);
            if (approx[r] > approx[longest]) {
                longest = r;
            }
        }

        std::uniform_int_distribution<int> move_dist(0, 3);
        const int move = move_dist(rng);
        bool changed = false;

        if (move == 0 || move == 1) {
            if (!cand[longest].empty()) {
                std::uniform_int_distribution<int> pos_dist(0, static_cast<int>(cand[longest].size()) - 1);
                const int pos = pos_dist(rng);
                const int cluster_id = cand[longest][static_cast<size_t>(pos)];
                std::uniform_int_distribution<int> route_dist(0, static_cast<int>(cand.size()) - 1);
                int target = route_dist(rng);
                if (target == static_cast<int>(longest) && cand.size() > 1) {
                    target = (target + 1) % static_cast<int>(cand.size());
                }
                cand[longest].erase(cand[longest].begin() + pos);
                if (move == 0 || cand[target].empty()) {
                    cand[static_cast<size_t>(target)].push_back(cluster_id);
                } else {
                    std::uniform_int_distribution<int> ins_dist(0, static_cast<int>(cand[target].size()));
                    cand[static_cast<size_t>(target)].insert(cand[static_cast<size_t>(target)].begin() + ins_dist(rng), cluster_id);
                }
                changed = true;
            }
        } else if (move == 2) {
            if (cand.size() > 1 && !cand[longest].empty()) {
                std::uniform_int_distribution<int> route_dist(0, static_cast<int>(cand.size()) - 1);
                int other = route_dist(rng);
                if (other == static_cast<int>(longest) && cand.size() > 1) {
                    other = (other + 1) % static_cast<int>(cand.size());
                }
                if (!cand[static_cast<size_t>(other)].empty()) {
                    std::uniform_int_distribution<int> pos_a(0, static_cast<int>(cand[longest].size()) - 1);
                    std::uniform_int_distribution<int> pos_b(0, static_cast<int>(cand[static_cast<size_t>(other)].size()) - 1);
                    std::swap(cand[longest][static_cast<size_t>(pos_a(rng))],
                              cand[static_cast<size_t>(other)][static_cast<size_t>(pos_b(rng))]);
                    changed = true;
                }
            }
        } else {
            size_t r = longest;
            if (cand[r].size() >= 2) {
                std::uniform_int_distribution<int> pos_dist(0, static_cast<int>(cand[r].size()) - 2);
                const int pos = pos_dist(rng);
                std::swap(cand[r][static_cast<size_t>(pos)], cand[r][static_cast<size_t>(pos + 1)]);
                changed = true;
            }
        }

        if (!changed) {
            continue;
        }

        const double cand_energy = ClusterAssignmentEnergyV7(cand, model);
        const double delta = cand_energy - current_energy;
        if (delta <= 0.0 || prob(rng) < std::exp(-delta / std::max(1e-6, temp))) {
            current.swap(cand);
            current_energy = cand_energy;
            if (current_energy + kEps < best_energy) {
                best = current;
                best_energy = current_energy;
            }
        }
    }

    assigned_clusters.swap(best);
}

template <typename DistanceFn>
void BuildFastSeedRoutesV7(mtsp::RouteSet& out,
                           const mtsp::Instance& inst,
                           const CandidateSets& candidate_sets,
                           DistanceFn& distance,
                           SearchBudgetV5& budget,
                           double route_size_slack,
                           double lookahead_weight,
                           double depot_weight) {
    const int m = inst.GetSalesmanCount();
    out.assign(static_cast<size_t>(m), std::vector<int>{0});
    const int target_size = std::max(1, (inst.GetNodeCount() - 1 + m - 1) / m);
    const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack))));
    std::vector<int> current(static_cast<size_t>(m), 0);
    std::vector<int> route_sizes(static_cast<size_t>(m), 0);
    std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
    visited[0] = 1;

    std::vector<int> order;
    order.reserve(static_cast<size_t>(inst.GetNodeCount() - 1));
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        order.push_back(city);
    }
    std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        const double dl = distance.DepotDistance(lhs);
        const double dr = distance.DepotDistance(rhs);
        if (std::abs(dl - dr) > kEps) {
            return dl > dr;
        }
        return lhs < rhs;
    });

    for (size_t idx = 0; idx < order.size(); ++idx) {
        if ((idx & 63U) == 0 && budget.ForceCheck()) {
            break;
        }
        const int city = order[idx];
        if (visited[static_cast<size_t>(city)]) {
            continue;
        }
        int best_salesman = 0;
        double best_score = std::numeric_limits<double>::max();
        for (int s = 0; s < m; ++s) {
            const int size = route_sizes[static_cast<size_t>(s)];
            const double overload = size >= hard_max_size ? 1e9 : 0.0;
            const double balance = 0.25 * std::max(0, size - target_size + 1);
            const double forward = ForwardPotentialV5(city, visited, candidate_sets, inst, distance);
            const double score = overload + distance(current[static_cast<size_t>(s)], city) +
                                 lookahead_weight * forward + depot_weight * distance(city, 0) + balance;
            if (score + kEps < best_score) {
                best_score = score;
                best_salesman = s;
            }
        }
        out[static_cast<size_t>(best_salesman)].push_back(city);
        current[static_cast<size_t>(best_salesman)] = city;
        ++route_sizes[static_cast<size_t>(best_salesman)];
        visited[static_cast<size_t>(city)] = 1;
    }

    CompleteRemainingAssignmentsV5(out, current, route_sizes, visited, target_size, hard_max_size, distance);
}

template <typename DistanceFn>
void SanitizeAndCompleteRoutesV7(mtsp::RouteSet& routes,
                                 const mtsp::Instance& inst,
                                 DistanceFn& distance,
                                 double route_size_slack) {
    const int m = inst.GetSalesmanCount();
    const int target_size = std::max(1, (inst.GetNodeCount() - 1 + m - 1) / m);
    const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack))));
    mtsp::RouteSet sanitized;
    sanitized.reserve(routes.size());
    std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
    visited[0] = 1;
    std::vector<int> current;
    std::vector<int> route_sizes;
    current.reserve(routes.size());
    route_sizes.reserve(routes.size());

    for (const auto& route : routes) {
        std::vector<int> open{0};
        for (int city : route) {
            if (city == 0) {
                continue;
            }
            if (city < 0 || city >= inst.GetNodeCount()) {
                continue;
            }
            if (visited[static_cast<size_t>(city)]) {
                continue;
            }
            open.push_back(city);
            visited[static_cast<size_t>(city)] = 1;
        }
        sanitized.push_back(std::move(open));
        current.push_back(sanitized.back().back());
        route_sizes.push_back(static_cast<int>(sanitized.back().size()) - 1);
    }

    if (sanitized.empty()) {
        sanitized.assign(static_cast<size_t>(m), std::vector<int>{0});
        current.assign(static_cast<size_t>(m), 0);
        route_sizes.assign(static_cast<size_t>(m), 0);
    }

    CompleteRemainingAssignmentsV5(sanitized, current, route_sizes, visited, target_size, hard_max_size, distance);
    for (auto& route : sanitized) {
        if (route.empty() || route.front() != 0) {
            route.insert(route.begin(), 0);
        }
        if (route.back() != 0) {
            route.push_back(0);
        }
    }
    routes.swap(sanitized);
}

void BuildInitialRoutesClusterAwareV6(mtsp::RouteSet& out,
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
                if (t == s) continue;
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

    if (!budget.ShouldStop()) {
        SimulatedAnnealClusterAssignmentV7(assigned_clusters, model, inst, rng, budget);
    }

    std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
    visited[0] = 1;
    std::vector<int> current(static_cast<size_t>(m), 0);
    std::vector<int> route_sizes(static_cast<size_t>(m), 0);

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
                if (!visited[static_cast<size_t>(city)]) {
                    out[static_cast<size_t>(s)].push_back(city);
                    current[static_cast<size_t>(s)] = city;
                    ++route_sizes[static_cast<size_t>(s)];
                    visited[static_cast<size_t>(city)] = 1;
                }
            }
            if (!block.empty()) {
                current_anchor = coords[static_cast<size_t>(block.back())];
            }
        }
    }

    CompleteRemainingAssignmentsV5(out, current, route_sizes, visited, target_size, hard_max_size, distance);
}

std::vector<double> ComputeOpenRouteLengthsV6(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    std::vector<double> lengths(routes.size(), 0.0);
    for (size_t r = 0; r < routes.size(); ++r) {
        lengths[r] = OpenRouteLengthV6(routes[r], distance);
    }
    return lengths;
}

