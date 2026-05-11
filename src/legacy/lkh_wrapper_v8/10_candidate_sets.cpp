// v8/10_candidate_sets.cpp — building candidate sets for k-opt search:
// geometric candidates via a spatial grid and POPMUSIC-style filtering.
// Candidates are computed once per instance and reused by all subsequent phases.

CandidateSets BuildGeometricCandidatesV5(const mtsp::Instance& inst, int candidate_count, int exact_threshold) {
    const int node_count = inst.GetNodeCount();
    candidate_count = std::max(1, std::min(candidate_count, node_count - 1));

    CandidateSets sets(static_cast<size_t>(node_count));
    const auto& coords = inst.GetCoords();

    if (node_count <= exact_threshold) {
#pragma omp parallel for schedule(static) if(node_count > 1024)
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV5(node, candidate_count, coords);
        }
        return sets;
    }

    SpatialGridV5 grid;
    if (!TryBuildSpatialGridV5(coords, candidate_count, grid)) {
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV5(node, candidate_count, coords);
        }
        return sets;
    }

#pragma omp parallel for schedule(static) if(node_count > 1024)
    for (int node = 0; node < node_count; ++node) {
        sets[static_cast<size_t>(node)] = CollectNearestCandidatesGridV5(node, candidate_count, grid, coords);
    }
    return sets;
}

void NormalizeCandidateSymmetryV5(CandidateSets& sets, size_t max_per_node) {
    for (size_t from = 0; from < sets.size(); ++from) {
        for (int to : sets[from]) {
            auto& back = sets[static_cast<size_t>(to)];
            if (std::find(back.begin(), back.end(), static_cast<int>(from)) == back.end()) {
                back.push_back(static_cast<int>(from));
            }
        }
    }
    for (auto& vec : sets) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        if (vec.size() > max_per_node) {
            vec.resize(max_per_node);
        }
    }
}

template <typename DistanceFn>
void AddTourEdgesToFrequencyV5(const std::vector<int>& closed_tour, EdgeFreqMap& freq, const DistanceFn&) {
    for (size_t i = 1; i < closed_tour.size(); ++i) {
        const int a = closed_tour[i - 1];
        const int b = closed_tour[i];
        ++freq[static_cast<size_t>(a)][b];
        ++freq[static_cast<size_t>(b)][a];
    }
}

std::vector<int> BuildSampleTourGreedyV5(const std::vector<int>& sample, const std::vector<Coord>& coords) {
    if (sample.empty()) {
        return {};
    }
    std::vector<int> remaining = sample;
    std::vector<int> order;
    order.reserve(sample.size());

    auto it0 = std::find(remaining.begin(), remaining.end(), 0);
    if (it0 != remaining.end()) {
        std::swap(*it0, remaining.front());
    }
    order.push_back(remaining.front());
    remaining.erase(remaining.begin());

    while (!remaining.empty()) {
        const int last = order.back();
        auto best_it = remaining.begin();
        double best_dist = std::numeric_limits<double>::max();
        for (auto it = remaining.begin(); it != remaining.end(); ++it) {
            const double dist = SquaredDistanceCoordsV5(coords[static_cast<size_t>(last)], coords[static_cast<size_t>(*it)]);
            if (dist + kEps < best_dist) {
                best_dist = dist;
                best_it = it;
            }
        }
        order.push_back(*best_it);
        remaining.erase(best_it);
    }

    if (order.front() != 0) {
        auto found = std::find(order.begin(), order.end(), 0);
        if (found != order.end()) {
            std::rotate(order.begin(), found, order.end());
        }
    }
    return order;
}

template <typename DistanceFn>
void ImproveClosedTour2OptLimitedV5(std::vector<int>& closed_tour,
                                    const CandidateSets& candidate_sets,
                                    DistanceFn& distance,
                                    int max_passes,
                                    SearchBudgetV5& budget) {
    if (closed_tour.size() <= 5 || max_passes <= 0) {
        return;
    }

    RouteIndexV5 index(static_cast<int>(candidate_sets.size()));
    for (int pass = 0; pass < max_passes && !budget.ShouldStop(); ++pass) {
        bool improved = false;
        index.Build(closed_tour);
        for (size_t i = 1; i + 2 < closed_tour.size(); ++i) {
            if (budget.ShouldStop()) {
                return;
            }
            const int a = closed_tour[i - 1];
            const int b = closed_tour[i];
            for (int c : candidate_sets[static_cast<size_t>(a)]) {
                const int j = index.Get(c);
                if (j <= static_cast<int>(i) || j + 1 >= static_cast<int>(closed_tour.size())) {
                    continue;
                }
                if (j == static_cast<int>(closed_tour.size()) - 1) {
                    continue;
                }
                const int d = closed_tour[static_cast<size_t>(j) + 1];
                const double removed = distance(a, b) + distance(c, d);
                const double added = distance(a, c) + distance(b, d);
                if (added + kEps < removed) {
                    std::reverse(closed_tour.begin() + static_cast<std::ptrdiff_t>(i),
                                 closed_tour.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                    break;
                }
            }
            if (improved) {
                break;
            }
        }
        if (!improved) {
            break;
        }
    }
    closed_tour.back() = closed_tour.front();
}

template <typename DistanceFn>
void OptimizeWindowOpenPathV5(std::vector<int>& closed_tour,
                              size_t left,
                              size_t right,
                              const CandidateSets& candidate_sets,
                              RouteIndexV5& index,
                              DistanceFn& distance,
                              SearchBudgetV5& budget) {
    if (right <= left + 2 || right >= closed_tour.size()) {
        return;
    }

    bool improved = true;
    while (improved && !budget.ShouldStop()) {
        improved = false;
        index.Build(closed_tour);
        for (size_t i = left + 1; i + 1 < right; ++i) {
            const int t1 = closed_tour[i - 1];
            const int t2 = closed_tour[i];
            for (int t3 : candidate_sets[static_cast<size_t>(t1)]) {
                const int j = index.Get(t3);
                if (j <= static_cast<int>(i) || j >= static_cast<int>(right)) {
                    continue;
                }
                if (j + 1 > static_cast<int>(right)) {
                    continue;
                }
                const int t4 = closed_tour[static_cast<size_t>(j) + 1];
                const double removed = distance(t1, t2) + distance(t3, t4);
                const double added = distance(t1, t3) + distance(t2, t4);
                if (added + kEps < removed) {
                    std::reverse(closed_tour.begin() + static_cast<std::ptrdiff_t>(i),
                                 closed_tour.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                    break;
                }
            }
            if (improved) {
                break;
            }
        }
    }
    closed_tour.back() = closed_tour.front();
}

template <typename DistanceFn>
void FastPopmusicPassV5(std::vector<int>& closed_tour,
                        int window_len,
                        const CandidateSets& candidate_sets,
                        DistanceFn& distance,
                        SearchBudgetV5& budget) {
    const int n = static_cast<int>(closed_tour.size()) - 1;
    if (n <= window_len + 2) {
        ImproveClosedTour2OptLimitedV5(closed_tour, candidate_sets, distance, 2, budget);
        return;
    }

    RouteIndexV5 index(static_cast<int>(candidate_sets.size()));
    for (int scan = 0; scan < 2 && !budget.ShouldStop(); ++scan) {
        const int shift = (scan == 0 ? 0 : window_len / 2);
        if (shift > 0) {
            std::rotate(closed_tour.begin() + 1,
                        closed_tour.begin() + 1 + std::min(shift, n - 1),
                        closed_tour.end() - 1);
            closed_tour.back() = closed_tour.front();
        }
        for (int start = 0; start < n && !budget.ShouldStop(); start += window_len) {
            const size_t left = static_cast<size_t>(start);
            const size_t right = static_cast<size_t>(std::min(n - 1, start + window_len - 1));
            OptimizeWindowOpenPathV5(closed_tour, left, right, candidate_sets, index, distance, budget);
        }
    }
}

template <typename DistanceFn>
std::vector<int> BuildPopmusicTourV5(const mtsp::Instance& inst,
                                     const CandidateSets& geometric_candidates,
                                     int sample_size,
                                     int window_len,
                                     std::mt19937& rng,
                                     DistanceFn& distance,
                                     SearchBudgetV5& budget) {
    const int node_count = inst.GetNodeCount();
    std::vector<int> all_nodes(static_cast<size_t>(node_count));
    std::iota(all_nodes.begin(), all_nodes.end(), 0);
    if (node_count > 2) {
        std::shuffle(all_nodes.begin() + 1, all_nodes.end(), rng);
    }

    sample_size = std::max(8, std::min(sample_size, node_count));
    std::vector<int> sample;
    sample.reserve(static_cast<size_t>(sample_size));
    sample.push_back(0);
    for (int i = 1; i < sample_size; ++i) {
        sample.push_back(all_nodes[static_cast<size_t>(i)]);
    }

    std::vector<int> anchors = BuildSampleTourGreedyV5(sample, inst.GetCoords());
    if (anchors.empty()) {
        anchors = {0};
    }

    std::unordered_map<int, size_t> anchor_pos;
    anchor_pos.reserve(anchors.size() * 2ULL);
    for (size_t pos = 0; pos < anchors.size(); ++pos) {
        anchor_pos[anchors[pos]] = pos;
    }

    std::vector<std::vector<int>> buckets(anchors.size());
    for (int city = 1; city < node_count; ++city) {
        if ((city & 63) == 0 && budget.ForceCheck()) {
            break;
        }
        if (anchor_pos.find(city) != anchor_pos.end()) {
            continue;
        }
        size_t best_anchor = 0;
        double best_dist = std::numeric_limits<double>::max();
        for (size_t pos = 0; pos < anchors.size(); ++pos) {
            if ((pos & 15U) == 0 && budget.ForceCheck()) {
                break;
            }
            const double dist = distance(city, anchors[pos]);
            if (dist + kEps < best_dist) {
                best_dist = dist;
                best_anchor = pos;
            }
        }
        buckets[best_anchor].push_back(city);
    }

    for (size_t pos = 0; pos < buckets.size(); ++pos) {
        if ((pos & 7U) == 0 && budget.ForceCheck()) {
            break;
        }
        auto& bucket = buckets[pos];
        const int anchor = anchors[pos];
        std::sort(bucket.begin(), bucket.end(), [&](int lhs, int rhs) {
            const double dl = distance(anchor, lhs);
            const double dr = distance(anchor, rhs);
            if (std::abs(dl - dr) > kEps) {
                return dl < dr;
            }
            return lhs < rhs;
        });
    }

    std::vector<int> closed_tour;
    closed_tour.reserve(static_cast<size_t>(node_count + 1));
    for (size_t pos = 0; pos < anchors.size(); ++pos) {
        if ((pos & 7U) == 0 && budget.ForceCheck()) {
            break;
        }
        closed_tour.push_back(anchors[pos]);
        for (int city : buckets[pos]) {
            closed_tour.push_back(city);
        }
    }

    auto zero_it = std::find(closed_tour.begin(), closed_tour.end(), 0);
    if (zero_it != closed_tour.end()) {
        std::rotate(closed_tour.begin(), zero_it, closed_tour.end());
    }
    if (closed_tour.front() != 0) {
        closed_tour.insert(closed_tour.begin(), 0);
    }
    closed_tour.push_back(closed_tour.front());

    ImproveClosedTour2OptLimitedV5(closed_tour, geometric_candidates, distance, 2, budget);
    FastPopmusicPassV5(closed_tour, std::max(12, window_len), geometric_candidates, distance, budget);
    ImproveClosedTour2OptLimitedV5(closed_tour, geometric_candidates, distance, 1, budget);
    return closed_tour;
}

template <typename DistanceFn>
CandidateSets BuildHybridCandidateSetsV5(const mtsp::Instance& inst,
                                         int final_candidate_count,
                                         int geometric_candidate_count,
                                         int exact_threshold,
                                         int popmusic_solutions,
                                         int popmusic_sample_size,
                                         int popmusic_window,
                                         std::mt19937& rng,
                                         DistanceFn& distance,
                                         SearchBudgetV5& budget) {
    CandidateSets geometric = BuildGeometricCandidatesV5(inst, geometric_candidate_count, exact_threshold);
    EdgeFreqMap freq(static_cast<size_t>(inst.GetNodeCount()));

    for (int node = 0; node < inst.GetNodeCount(); ++node) {
        auto& map = freq[static_cast<size_t>(node)];
        map.reserve(geometric[static_cast<size_t>(node)].size() * 2ULL + 8ULL);
        for (int other : geometric[static_cast<size_t>(node)]) {
            map[other] += 2;
        }
    }

    for (int s = 0; s < popmusic_solutions && !budget.ShouldStop(); ++s) {
        std::vector<int> tour = BuildPopmusicTourV5(inst,
                                                    geometric,
                                                    popmusic_sample_size,
                                                    popmusic_window,
                                                    rng,
                                                    distance,
                                                    budget);
        if (tour.size() >= 2) {
            AddTourEdgesToFrequencyV5(tour, freq, distance);
        }
    }

    CandidateSets result(static_cast<size_t>(inst.GetNodeCount()));
    const auto& coords = inst.GetCoords();
    for (int node = 0; node < inst.GetNodeCount(); ++node) {
        if ((node & 63) == 0 && budget.ForceCheck()) {
            break;
        }
        std::vector<std::pair<std::pair<int, double>, int>> ranked;
        ranked.reserve(freq[static_cast<size_t>(node)].size());
        for (const auto& [other, count] : freq[static_cast<size_t>(node)]) {
            if (other == node) {
                continue;
            }
            const double sq = SquaredDistanceCoordsV5(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(other)]);
            ranked.push_back({{-count, sq}, other});
        }

        const auto cmp = [](const auto& lhs, const auto& rhs) {
            if (lhs.first.first != rhs.first.first) {
                return lhs.first.first < rhs.first.first;
            }
            if (std::abs(lhs.first.second - rhs.first.second) > kEps) {
                return lhs.first.second < rhs.first.second;
            }
            return lhs.second < rhs.second;
        };

        if (ranked.size() > static_cast<size_t>(final_candidate_count)) {
            std::nth_element(ranked.begin(),
                             ranked.begin() + static_cast<std::ptrdiff_t>(final_candidate_count),
                             ranked.end(),
                             cmp);
            ranked.resize(static_cast<size_t>(final_candidate_count));
        }
        std::sort(ranked.begin(), ranked.end(), cmp);

        auto& out = result[static_cast<size_t>(node)];
        out.reserve(ranked.size());
        for (const auto& item : ranked) {
            out.push_back(item.second);
        }

        if (out.size() < static_cast<size_t>(final_candidate_count)) {
            for (int other : geometric[static_cast<size_t>(node)]) {
                if (std::find(out.begin(), out.end(), other) == out.end()) {
                    out.push_back(other);
                    if (out.size() >= static_cast<size_t>(final_candidate_count)) {
                        break;
                    }
                }
            }
        }
    }

    NormalizeCandidateSymmetryV5(result, static_cast<size_t>(std::max(final_candidate_count + 2, final_candidate_count)));
    return result;
}


CandidateSets BuildHybridCandidateSetsV8(const mtsp::Instance& inst,
                                         int final_candidate_count,
                                         int geometric_candidate_count,
                                         int exact_threshold,
                                         int popmusic_solutions,
                                         int popmusic_sample_size,
                                         int popmusic_window,
                                         std::mt19937& rng,
                                         DistanceOracleV5& distance,
                                         SearchBudgetV5& budget) {
#ifndef _OPENMP
    return BuildHybridCandidateSetsV5(inst,
                                      final_candidate_count,
                                      geometric_candidate_count,
                                      exact_threshold,
                                      popmusic_solutions,
                                      popmusic_sample_size,
                                      popmusic_window,
                                      rng,
                                      distance,
                                      budget);
#else
    (void)distance;
    CandidateSets geometric = BuildGeometricCandidatesV5(inst, geometric_candidate_count, exact_threshold);
    const int node_count = inst.GetNodeCount();
    EdgeFreqMap freq(static_cast<size_t>(node_count));

    for (int node = 0; node < node_count; ++node) {
        auto& map = freq[static_cast<size_t>(node)];
        map.reserve(geometric[static_cast<size_t>(node)].size() * 2ULL + 8ULL);
        for (int other : geometric[static_cast<size_t>(node)]) {
            map[other] += 2;
        }
    }

    if (popmusic_solutions <= 0 || budget.ShouldStop()) {
        NormalizeCandidateSymmetryV5(geometric, static_cast<size_t>(std::max(final_candidate_count + 2, final_candidate_count)));
        return geometric;
    }

    std::vector<unsigned int> seeds(static_cast<size_t>(popmusic_solutions));
    for (int s = 0; s < popmusic_solutions; ++s) {
        seeds[static_cast<size_t>(s)] = rng();
    }

    const int per_tour_budget_ms = std::max(100, budget.RemainingMs());
    std::vector<std::vector<std::pair<int, int>>> tour_edges(static_cast<size_t>(popmusic_solutions));

#pragma omp parallel for schedule(dynamic) if(popmusic_solutions > 1 && node_count > 4096)
    for (int s = 0; s < popmusic_solutions; ++s) {
        std::mt19937 local_rng(seeds[static_cast<size_t>(s)] ^ (0x9E3779B9U + static_cast<unsigned int>(s) * 0x85EBCA6BU));
        DistanceOracleV5 local_distance(inst);
        SearchBudgetV5 local_budget(per_tour_budget_ms, 0, 64);
        std::vector<int> tour = BuildPopmusicTourV5(inst,
                                                    geometric,
                                                    popmusic_sample_size,
                                                    popmusic_window,
                                                    local_rng,
                                                    local_distance,
                                                    local_budget);
        auto& edges = tour_edges[static_cast<size_t>(s)];
        if (tour.size() >= 2) {
            edges.reserve(tour.size() - 1);
            for (size_t i = 1; i < tour.size(); ++i) {
                const int a = tour[i - 1];
                const int b = tour[i];
                if (a != b) {
                    edges.emplace_back(a, b);
                }
            }
        }
    }

    for (const auto& edges : tour_edges) {
        for (const auto& [a, b] : edges) {
            ++freq[static_cast<size_t>(a)][b];
            ++freq[static_cast<size_t>(b)][a];
        }
    }

    CandidateSets result(static_cast<size_t>(node_count));
    const auto& coords = inst.GetCoords();
    for (int node = 0; node < node_count; ++node) {
        if ((node & 63) == 0 && budget.ForceCheck()) {
            break;
        }
        std::vector<std::pair<std::pair<int, double>, int>> ranked;
        ranked.reserve(freq[static_cast<size_t>(node)].size());
        for (const auto& [other, count] : freq[static_cast<size_t>(node)]) {
            if (other == node) {
                continue;
            }
            const double sq = SquaredDistanceCoordsV5(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(other)]);
            ranked.push_back({{-count, sq}, other});
        }

        const auto cmp = [](const auto& lhs, const auto& rhs) {
            if (lhs.first.first != rhs.first.first) {
                return lhs.first.first < rhs.first.first;
            }
            if (std::abs(lhs.first.second - rhs.first.second) > kEps) {
                return lhs.first.second < rhs.first.second;
            }
            return lhs.second < rhs.second;
        };

        if (ranked.size() > static_cast<size_t>(final_candidate_count)) {
            std::nth_element(ranked.begin(),
                             ranked.begin() + static_cast<std::ptrdiff_t>(final_candidate_count),
                             ranked.end(),
                             cmp);
            ranked.resize(static_cast<size_t>(final_candidate_count));
        }
        std::sort(ranked.begin(), ranked.end(), cmp);

        auto& out = result[static_cast<size_t>(node)];
        out.reserve(ranked.size());
        for (const auto& item : ranked) {
            out.push_back(item.second);
        }

        if (out.size() < static_cast<size_t>(final_candidate_count)) {
            for (int other : geometric[static_cast<size_t>(node)]) {
                if (std::find(out.begin(), out.end(), other) == out.end()) {
                    out.push_back(other);
                    if (out.size() >= static_cast<size_t>(final_candidate_count)) {
                        break;
                    }
                }
            }
        }
    }

    NormalizeCandidateSymmetryV5(result, static_cast<size_t>(std::max(final_candidate_count + 2, final_candidate_count)));
    return result;
#endif
}

template <typename DistanceFn>
