std::vector<int> CollectConstructionCandidatesV5(int from,
                                                 const std::vector<char>& visited,
                                                 const CandidateSets& candidate_sets,
                                                 const mtsp::Instance& inst,
                                                 int fallback_limit,
                                                 DistanceFn& distance) {
    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(fallback_limit));

    for (int city : candidate_sets[static_cast<size_t>(from)]) {
        if (!visited[static_cast<size_t>(city)]) {
            candidates.push_back(city);
            if (static_cast<int>(candidates.size()) >= fallback_limit) {
                return candidates;
            }
        }
    }

    std::vector<std::pair<double, int>> fallback;
    fallback.reserve(inst.GetNodeCount());
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[static_cast<size_t>(city)] &&
            std::find(candidates.begin(), candidates.end(), city) == candidates.end()) {
            fallback.emplace_back(distance(from, city), city);
        }
    }

    const size_t limit = std::min(static_cast<size_t>(std::max(0, fallback_limit - static_cast<int>(candidates.size()))),
                                  fallback.size());
    TrimNearestV5(fallback, limit);

    for (const auto& [_, city] : fallback) {
        candidates.push_back(city);
    }
    return candidates;
}

template <typename DistanceFn>
double ForwardPotentialV5(int node,
                          const std::vector<char>& visited,
                          const CandidateSets& candidate_sets,
                          const mtsp::Instance& inst,
                          DistanceFn& distance) {
    double best = distance(node, 0);
    for (int candidate : candidate_sets[static_cast<size_t>(node)]) {
        if (!visited[static_cast<size_t>(candidate)]) {
            best = std::min(best, distance(node, candidate));
        }
    }
    if (best < std::numeric_limits<double>::max() / 4.0) {
        return best;
    }
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[static_cast<size_t>(city)]) {
            best = std::min(best, distance(node, city));
        }
    }
    return best;
}

template <typename DistanceFn>
bool ApplyFirstImproving2OptV5(std::vector<int>& route,
                               const CandidateSets& candidate_sets,
                               DistanceFn& distance,
                               RouteIndexV5& index,
                               std::vector<char>& dont_look,
                               SearchBudgetV5& budget) {
    if (route.size() <= 4) {
        return false;
    }

    index.Build(route);
    for (size_t i = 1; i + 2 < route.size(); ++i) {
        if (budget.ShouldStop()) {
            return false;
        }
        const int t1 = route[i - 1];
        const int t2 = route[i];
        if (dont_look[static_cast<size_t>(t1)]) {
            continue;
        }

        bool improved = false;
        for (int t3 : candidate_sets[static_cast<size_t>(t1)]) {
            const int j = index.Get(t3);
            if (j <= static_cast<int>(i) || j + 1 >= static_cast<int>(route.size())) {
                continue;
            }
            const int t4 = route[static_cast<size_t>(j) + 1];
            const double removed = distance(t1, t2) + distance(t3, t4);
            const double added = distance(t1, t3) + distance(t2, t4);
            if (added + kEps < removed) {
                std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                             route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                std::fill(dont_look.begin(), dont_look.end(), 0);
                improved = true;
                break;
            }
        }

        if (improved) {
            return true;
        }
        dont_look[static_cast<size_t>(t1)] = 1;
    }
    return false;
}

template <typename DistanceFn>
void QuickRouteCleanupV5(std::vector<int>& route,
                         const CandidateSets& candidate_sets,
                         DistanceFn& distance,
                         int node_count,
                         int max_moves,
                         SearchBudgetV5& budget) {
    if (route.size() <= 4 || max_moves <= 0) {
        return;
    }

    RouteIndexV5 index(node_count);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);
    for (int move = 0; move < max_moves && !budget.ShouldStop(); ++move) {
        if (!ApplyFirstImproving2OptV5(route, candidate_sets, distance, index, dont_look, budget)) {
            return;
        }
    }
}

template <typename DistanceFn>
void ImproveRouteGuidedV5(std::vector<int>& route,
                          const CandidateSets& candidate_sets,
                          DistanceFn& distance,
                          int node_count,
                          SearchBudgetV5& budget) {
    if (route.size() <= 4) {
        return;
    }

    RouteIndexV5 index(node_count);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);
    while (!budget.ShouldStop()) {
        if (ApplyFirstImproving2OptV5(route, candidate_sets, distance, index, dont_look, budget)) {
            continue;
        }
        if (route.size() > 4) {
            std::vector<int> reversed = route;
            std::reverse(reversed.begin() + 1, reversed.end() - 1);
            std::fill(dont_look.begin(), dont_look.end(), 0);
            if (ApplyFirstImproving2OptV5(reversed, candidate_sets, distance, index, dont_look, budget)) {
                route.swap(reversed);
                continue;
            }
        }
        break;
    }
}

template <typename DistanceFn>
void DoubleBridgeKickV5(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
    if (route.size() < 10) {
        return;
    }
    const int n = static_cast<int>(route.size()) - 1;
    std::uniform_int_distribution<int> cut1_dist(1, n / 4);
    std::uniform_int_distribution<int> cut2_dist(n / 4 + 1, n / 2);
    std::uniform_int_distribution<int> cut3_dist(n / 2 + 1, (3 * n) / 4);

    const int a = cut1_dist(rng);
    const int b = cut2_dist(rng);
    const int c = cut3_dist(rng);

    std::vector<int> kicked;
    kicked.reserve(route.size());
    kicked.push_back(route.front());
    kicked.insert(kicked.end(), route.begin() + a, route.begin() + b);
    kicked.insert(kicked.end(), route.begin() + c, route.end() - 1);
    kicked.insert(kicked.end(), route.begin() + b, route.begin() + c);
    kicked.insert(kicked.end(), route.begin() + 1, route.begin() + a);
    kicked.push_back(route.front());
    route.swap(kicked);
}

template <typename DistanceFn>
void IteratedLocalSearchV5(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const CandidateSets& candidate_sets,
                           int node_count,
                           DistanceFn& distance,
                           SearchBudgetV5& budget) {
    ImproveRouteGuidedV5(route, candidate_sets, distance, node_count, budget);
    if (budget.ShouldStop()) {
        return;
    }

    double best_length = RouteLengthGenericV5(route, distance);
    std::vector<int> best = route;
    for (int round = 0; round < rounds && !budget.ShouldStop(); ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV5(candidate, rng, distance);
        ImproveRouteGuidedV5(candidate, candidate_sets, distance, node_count, budget);
        const double candidate_length = RouteLengthGenericV5(candidate, distance);
        if (candidate_length + kEps < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }
    route.swap(best);
}

template <typename DistanceFn>
double SwapDeltaClosedRoutesV5(const std::vector<int>& route_a,
                               size_t idx_a,
                               const std::vector<int>& route_b,
                               size_t idx_b,
                               DistanceFn& distance) {
    const int prev_a = route_a[idx_a - 1];
    const int city_a = route_a[idx_a];
    const int next_a = route_a[idx_a + 1];
    const int prev_b = route_b[idx_b - 1];
    const int city_b = route_b[idx_b];
    const int next_b = route_b[idx_b + 1];

    const double before_a = distance(prev_a, city_a) + distance(city_a, next_a);
    const double after_a = distance(prev_a, city_b) + distance(city_b, next_a);
    const double before_b = distance(prev_b, city_b) + distance(city_b, next_b);
    const double after_b = distance(prev_b, city_a) + distance(city_a, next_b);

    return (after_a - before_a) + (after_b - before_b);
}

template <typename DistanceFn>
void CompleteRemainingAssignmentsV5(mtsp::RouteSet& out,
                                    std::vector<int>& current,
                                    std::vector<int>& route_sizes,
                                    std::vector<char>& visited,
                                    int target_size,
                                    int hard_max_size,
                                    DistanceFn& distance) {
    for (int city = 1; city < mtsp::Instance::GetInstance().GetNodeCount(); ++city) {
        if (visited[static_cast<size_t>(city)]) {
            continue;
        }

        int best_salesman = 0;
        double best_score = std::numeric_limits<double>::max();
        for (size_t salesman = 0; salesman < out.size(); ++salesman) {
            const int size = route_sizes[salesman];
            const double overload = size >= hard_max_size ? 1e9 : 0.0;
            const double balance = 0.2 * std::max(0, size - target_size + 1);
            const double score = overload + distance(current[salesman], city) + balance;
            if (score + kEps < best_score) {
                best_score = score;
                best_salesman = static_cast<int>(salesman);
            }
        }

        out[static_cast<size_t>(best_salesman)].push_back(city);
        current[static_cast<size_t>(best_salesman)] = city;
        ++route_sizes[static_cast<size_t>(best_salesman)];
        visited[static_cast<size_t>(city)] = 1;
    }
}

} // namespace
