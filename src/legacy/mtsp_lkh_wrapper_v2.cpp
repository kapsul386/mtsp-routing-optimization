// Key quality milestone of the early line. Adds on top of v1:
//   - k-NN candidate lists (replacing linear scan);
//   - lookahead weights in move evaluation (accounting for the next neighbor after a potential reposition);
//   - depot-aware weights during initial tour construction (preference for points closer to the depot).
// On the uniform grid, narrows the gap to OR-Tools+GLS by almost half (29.6% -> 14.5%, Appendix D).
// Registered as "lkh-wrapper-v2"; this is the baseline for the candidate-based approach in the early line.

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace {

using CandidateSets = std::vector<std::vector<int>>;

template <typename DistanceFn>
double RouteLengthGenericV2(const std::vector<int>& route, const DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

template <typename DistanceFn>
void ImproveRoute2OptGenericV2(std::vector<int>& route, const DistanceFn& distance) {
    if (route.size() <= 4) {
        return;
    }

    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t i = 1; i + 2 < route.size(); ++i) {
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                const double before = distance(route[i - 1], route[i]) + distance(route[j], route[j + 1]);
                const double after = distance(route[i - 1], route[j]) + distance(route[i], route[j + 1]);
                if (after + 1e-9 < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                }
            }
        }
    }
}

CandidateSets BuildCandidateSetsV2(const mtsp::Instance& inst, int candidate_count) {
    const int node_count = inst.GetNodeCount();
    candidate_count = std::max(1, std::min(candidate_count, node_count - 1));

    CandidateSets sets(node_count);
    for (int node = 0; node < node_count; ++node) {
        std::vector<std::pair<double, int>> nearest;
        nearest.reserve(static_cast<size_t>(std::max(0, node_count - 1)));

        for (int other = 0; other < node_count; ++other) {
            if (other == node) {
                continue;
            }
            nearest.emplace_back(inst.Distance(node, other), other);
        }

        const size_t limit = std::min(static_cast<size_t>(candidate_count), nearest.size());
        std::partial_sort(nearest.begin(), nearest.begin() + static_cast<std::ptrdiff_t>(limit), nearest.end());
        sets[node].reserve(limit);
        for (size_t idx = 0; idx < limit; ++idx) {
            sets[node].push_back(nearest[idx].second);
        }
    }

    return sets;
}

std::vector<int> CollectConstructionCandidatesV2(int from,
                                                 const std::vector<char>& visited,
                                                 const CandidateSets& candidate_sets,
                                                 const mtsp::Instance& inst,
                                                 int fallback_limit) {
    std::vector<int> candidates;
    std::vector<char> added(inst.GetNodeCount(), 0);

    for (int city : candidate_sets[from]) {
        if (!visited[city] && !added[city]) {
            candidates.push_back(city);
            added[city] = 1;
        }
    }

    if (static_cast<int>(candidates.size()) >= fallback_limit) {
        return candidates;
    }

    std::vector<std::pair<double, int>> fallback;
    fallback.reserve(inst.GetNodeCount());
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[city] && !added[city]) {
            fallback.emplace_back(inst.Distance(from, city), city);
        }
    }

    const size_t limit = std::min(static_cast<size_t>(fallback_limit - static_cast<int>(candidates.size())),
                                  fallback.size());
    std::partial_sort(fallback.begin(), fallback.begin() + static_cast<std::ptrdiff_t>(limit), fallback.end());
    for (size_t idx = 0; idx < limit; ++idx) {
        candidates.push_back(fallback[idx].second);
    }

    return candidates;
}

double ForwardPotentialV2(int node,
                          const std::vector<char>& visited,
                          const CandidateSets& candidate_sets,
                          const mtsp::Instance& inst) {
    double best = inst.Distance(node, 0);
    for (int candidate : candidate_sets[node]) {
        if (!visited[candidate]) {
            best = std::min(best, inst.Distance(node, candidate));
        }
    }

    if (best < std::numeric_limits<double>::max() / 4.0) {
        return best;
    }

    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[city]) {
            best = std::min(best, inst.Distance(node, city));
        }
    }

    return best;
}

template <typename DistanceFn>
bool ApplyBestPositiveGain2OptV2(std::vector<int>& route,
                                 const CandidateSets& candidate_sets,
                                 int node_count,
                                 const DistanceFn& distance) {
    if (route.size() <= 4) {
        return false;
    }

    std::vector<int> position(node_count, -1);
    for (size_t idx = 0; idx < route.size(); ++idx) {
        position[route[idx]] = static_cast<int>(idx);
    }

    double best_gain = 0.0;
    size_t best_i = 0;
    size_t best_j = 0;

    for (size_t i = 1; i + 2 < route.size(); ++i) {
        const int t1 = route[i - 1];
        const int t2 = route[i];
        for (int t3 : candidate_sets[t1]) {
            const int j = position[t3];
            if (j <= static_cast<int>(i) || j + 1 >= static_cast<int>(route.size())) {
                continue;
            }

            const int t4 = route[static_cast<size_t>(j) + 1];
            const double partial_gain = distance(t1, t2) - distance(t1, t3);
            if (partial_gain <= 1e-9) {
                continue;
            }

            const double total_gain = partial_gain + distance(t3, t4) - distance(t2, t4);
            if (total_gain > best_gain + 1e-9) {
                best_gain = total_gain;
                best_i = i;
                best_j = static_cast<size_t>(j);
            }
        }
    }

    if (best_gain <= 1e-9) {
        return false;
    }

    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(best_i),
                 route.begin() + static_cast<std::ptrdiff_t>(best_j + 1));
    return true;
}

template <typename DistanceFn>
void ImproveRouteGuidedV2(std::vector<int>& route,
                          const CandidateSets& candidate_sets,
                          int node_count,
                          const DistanceFn& distance) {
    if (route.size() <= 4) {
        return;
    }

    bool improved = true;
    while (improved) {
        improved = ApplyBestPositiveGain2OptV2(route, candidate_sets, node_count, distance);
        if (!improved && route.size() > 4) {
            std::vector<int> reversed = route;
            std::reverse(reversed.begin() + 1, reversed.end() - 1);
            if (ApplyBestPositiveGain2OptV2(reversed, candidate_sets, node_count, distance)) {
                route.swap(reversed);
                improved = true;
            }
        }
    }

    // Candidate-guided positive-gain search is followed by a full cleanup pass.
    ImproveRoute2OptGenericV2(route, distance);
}

template <typename DistanceFn>
void DoubleBridgeKickV2(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
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
void IteratedLocalSearchV2(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const CandidateSets& candidate_sets,
                           int node_count,
                           const DistanceFn& distance) {
    ImproveRouteGuidedV2(route, candidate_sets, node_count, distance);
    double best_length = RouteLengthGenericV2(route, distance);
    std::vector<int> best = route;

    for (int round = 0; round < rounds; ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV2(candidate, rng, distance);
        ImproveRouteGuidedV2(candidate, candidate_sets, node_count, distance);
        const double candidate_length = RouteLengthGenericV2(candidate, distance);
        if (candidate_length + 1e-9 < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }

    route.swap(best);
}

} // namespace

namespace mtsp {

class LkhWrapperSolverV2 : public Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
            local_rng_.seed(seed_ ^ 0x9E3779B9U);
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("candidate-count")) {
            candidate_count_ = std::max(4, std::stoi(opts.at("candidate-count")));
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
        const CandidateSets candidate_sets = BuildCandidateSetsV2(inst, candidate_count_);
        std::mt19937 rng(seed_);

        BuildInitialRoutes(out, candidate_sets);

        for (auto& route : out) {
            route.push_back(0);
            IteratedLocalSearchV2(
                route, rng, rounds_, candidate_sets, inst.GetNodeCount(), [&inst](int a, int b) { return inst.Distance(a, b); });
        }

        ImproveInterRoute(out, candidate_sets);
    }

private:
    void BuildInitialRoutes(RouteSet& out, const CandidateSets& candidate_sets) const {
        const Instance& inst = Instance::GetInstance();

        out.assign(inst.GetSalesmanCount(), std::vector<int>{0});
        std::vector<int> current(inst.GetSalesmanCount(), 0);
        std::vector<char> visited(inst.GetNodeCount(), 0);
        visited[0] = 1;

        int remaining = inst.GetNodeCount() - 1;
        while (remaining > 0) {
            int best_salesman = -1;
            int best_city = -1;
            double best_score = std::numeric_limits<double>::max();
            double best_immediate = std::numeric_limits<double>::max();

            for (int salesman = 0; salesman < inst.GetSalesmanCount(); ++salesman) {
                const std::vector<int> move_candidates = CollectConstructionCandidatesV2(
                    current[salesman], visited, candidate_sets, inst, std::max(candidate_count_, 6));
                for (int city : move_candidates) {
                    const double immediate = inst.Distance(current[salesman], city);
                    const double forward = ForwardPotentialV2(city, visited, candidate_sets, inst);
                    const double depot = inst.Distance(city, 0);
                    const double score = immediate + lookahead_weight_ * forward + depot_weight_ * depot;

                    if (score + 1e-9 < best_score ||
                        (std::abs(score - best_score) <= 1e-9 && immediate + 1e-9 < best_immediate)) {
                        best_score = score;
                        best_immediate = immediate;
                        best_salesman = salesman;
                        best_city = city;
                    }
                }
            }

            if (best_city == -1) {
                break;
            }

            out[best_salesman].push_back(best_city);
            current[best_salesman] = best_city;
            visited[best_city] = 1;
            --remaining;
        }
    }

    void ImproveInterRoute(RouteSet& routes, const CandidateSets& candidate_sets) const {
        const Instance& inst = Instance::GetInstance();
        bool improved = true;

        while (improved) {
            improved = false;
            for (size_t a = 0; a < routes.size(); ++a) {
                for (size_t b = a + 1; b < routes.size(); ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size(); ++i) {
                        for (size_t j = 1; j + 1 < routes[b].size(); ++j) {
                            const double current = ObjectiveMinsum(routes);
                            std::swap(routes[a][i], routes[b][j]);
                            const double swapped = ObjectiveMinsum(routes);
                            if (swapped + 1e-9 < current) {
                                IteratedLocalSearchV2(routes[a], local_rng_, std::max(4, rounds_ / 2),
                                                     candidate_sets, inst.GetNodeCount(),
                                                     [&inst](int u, int v) { return inst.Distance(u, v); });
                                IteratedLocalSearchV2(routes[b], local_rng_, std::max(4, rounds_ / 2),
                                                     candidate_sets, inst.GetNodeCount(),
                                                     [&inst](int u, int v) { return inst.Distance(u, v); });
                                improved = true;
                            } else {
                                std::swap(routes[a][i], routes[b][j]);
                            }
                        }
                    }
                }
            }
        }
    }

    unsigned int seed_ = 42U;
    int rounds_ = 24;
    int candidate_count_ = 12;
    double lookahead_weight_ = 0.35;
    double depot_weight_ = 0.12;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v2 = (SolverFactory::RegisterSolver("lkh-wrapper-v2", []() {
    return std::make_unique<LkhWrapperSolverV2>();
}),
                               true);

} // namespace mtsp
