// v8/50_rebalance.cpp — cluster block rebalancing.
// If after the seed phase one cluster is significantly larger than another,
// boundary clients are exchanged between neighboring clusters to equalize load.

double InternalBlockLengthV6(const std::vector<int>& route, size_t begin, size_t end, DistanceOracleV5& distance) {
    double total = 0.0;
    for (size_t i = begin + 1; i <= end; ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

void RebalanceOpenRoutesClusterAwareV6(mtsp::RouteSet& routes,
                                       const ClusterModelV6& model,
                                       DistanceOracleV5& distance,
                                       SearchBudgetV5& budget,
                                       int max_passes) {
    if (routes.empty()) {
        return;
    }
    auto route_lengths = ComputeOpenRouteLengthsV6(routes, distance);
    for (int pass = 0; pass < max_passes && !budget.ShouldStop(); ++pass) {
        const auto longest_it = std::max_element(route_lengths.begin(), route_lengths.end());
        const size_t from_route = static_cast<size_t>(std::distance(route_lengths.begin(), longest_it));
        const double old_max = *longest_it;
        bool improved = false;

        for (size_t i = 1; i < routes[from_route].size() && !improved; ) {
            const int cluster = model.node_to_cluster[static_cast<size_t>(routes[from_route][i])];
            size_t j = i;
            while (j + 1 < routes[from_route].size() &&
                   model.node_to_cluster[static_cast<size_t>(routes[from_route][j + 1])] == cluster) {
                ++j;
            }

            const int prev = routes[from_route][i - 1];
            const int first = routes[from_route][i];
            const int last = routes[from_route][j];
            const bool at_end = (j + 1 == routes[from_route].size());
            const int next = at_end ? 0 : routes[from_route][j + 1];
            const double block_len = InternalBlockLengthV6(routes[from_route], i, j, distance);
            const double removed = distance(prev, first) + block_len + distance(last, next) - distance(prev, next);
            const double new_from_len = route_lengths[from_route] - removed;

            size_t best_target = routes.size();
            double best_new_max = old_max;
            for (size_t to_route = 0; to_route < routes.size(); ++to_route) {
                if (to_route == from_route) {
                    continue;
                }
                const int tail = routes[to_route].back();
                const double added = distance(tail, first) + block_len + distance(last, 0) - distance(tail, 0);
                const double new_to_len = route_lengths[to_route] + added;
                double new_max = std::max(new_from_len, new_to_len);
                for (size_t r = 0; r < routes.size(); ++r) {
                    if (r != from_route && r != to_route) {
                        new_max = std::max(new_max, route_lengths[r]);
                    }
                }
                if (new_max + kEps < best_new_max) {
                    best_new_max = new_max;
                    best_target = to_route;
                }
            }

            if (best_target != routes.size()) {
                std::vector<int> block(routes[from_route].begin() + static_cast<std::ptrdiff_t>(i),
                                       routes[from_route].begin() + static_cast<std::ptrdiff_t>(j + 1));
                routes[from_route].erase(routes[from_route].begin() + static_cast<std::ptrdiff_t>(i),
                                         routes[from_route].begin() + static_cast<std::ptrdiff_t>(j + 1));
                routes[best_target].insert(routes[best_target].end(), block.begin(), block.end());
                route_lengths = ComputeOpenRouteLengthsV6(routes, distance);
                improved = true;
                break;
            }
            i = j + 1;
        }

        if (!improved) {
            break;
        }
    }
}

double RouteLengthClosedV6(const std::vector<int>& route, DistanceOracleV5& distance) {
    return RouteLengthGenericV5(route, distance);
}

bool TryBalancedRelocateV6(mtsp::RouteSet& routes,
                           const CandidateSets& global_candidates,
                           DistanceOracleV5& distance,
                           SearchBudgetV5& budget) {
    if (routes.size() < 2) {
        return false;
    }
    std::vector<double> lengths(routes.size(), 0.0);
    for (size_t r = 0; r < routes.size(); ++r) {
        lengths[r] = RouteLengthClosedV6(routes[r], distance);
    }
    const auto longest_it = std::max_element(lengths.begin(), lengths.end());
    const size_t from_route = static_cast<size_t>(std::distance(lengths.begin(), longest_it));
    const double old_max = *longest_it;
    const double old_sum = std::accumulate(lengths.begin(), lengths.end(), 0.0);

    std::vector<RouteIndexV5> indices;
    indices.reserve(routes.size());
    for (size_t r = 0; r < routes.size(); ++r) {
        indices.emplace_back(static_cast<int>(global_candidates.size()));
        indices.back().Build(routes[r]);
    }

    size_t best_i = 0;
    size_t best_to = routes.size();
    size_t best_after = 0;
    double best_score = 0.0;
    bool found = false;

    for (size_t i = 1; i + 1 < routes[from_route].size() && !budget.ShouldStop(); ++i) {
        const int city = routes[from_route][i];
        const int prev = routes[from_route][i - 1];
        const int next = routes[from_route][i + 1];
        const double removal = distance(prev, city) + distance(city, next) - distance(prev, next);
        const double new_from = lengths[from_route] - removal;

        for (size_t to_route = 0; to_route < routes.size(); ++to_route) {
            if (to_route == from_route) {
                continue;
            }
            std::vector<size_t> positions;
            positions.push_back(0);
            for (int cand : global_candidates[static_cast<size_t>(city)]) {
                const int pos = indices[to_route].Get(cand);
                if (pos >= 0 && pos + 1 < static_cast<int>(routes[to_route].size())) {
                    positions.push_back(static_cast<size_t>(pos));
                }
            }
            positions.push_back(routes[to_route].size() - 2);
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

            for (size_t after : positions) {
                const int a = routes[to_route][after];
                const int b = routes[to_route][after + 1];
                const double insert = distance(a, city) + distance(city, b) - distance(a, b);
                const double new_to = lengths[to_route] + insert;
                double new_max = std::max(new_from, new_to);
                for (size_t r = 0; r < routes.size(); ++r) {
                    if (r != from_route && r != to_route) {
                        new_max = std::max(new_max, lengths[r]);
                    }
                }
                const double new_sum = old_sum - removal + insert;
                const double score = (old_max - new_max) * 1000.0 + (old_sum - new_sum);
                if (score > best_score + kEps) {
                    best_score = score;
                    best_i = i;
                    best_to = to_route;
                    best_after = after;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return false;
    }

    const int city = routes[from_route][best_i];
    routes[from_route].erase(routes[from_route].begin() + static_cast<std::ptrdiff_t>(best_i));
    routes[best_to].insert(routes[best_to].begin() + static_cast<std::ptrdiff_t>(best_after + 1), city);
    return true;
}

} // namespace
