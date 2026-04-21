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
constexpr double kEps = 1e-9;

template <typename DistanceFn>
double RouteLengthGenericV3(const std::vector<int>& route, const DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

bool IsInCandidateSet(int from, int to, const CandidateSets& candidate_sets) {
    const auto& cand = candidate_sets[from];
    return std::find(cand.begin(), cand.end(), to) != cand.end();
}

bool IsPromisingSwapPair(int city_a, int city_b, const CandidateSets& candidate_sets) {
    return IsInCandidateSet(city_a, city_b, candidate_sets) ||
           IsInCandidateSet(city_b, city_a, candidate_sets);
}

template <typename DistanceFn>
double SwapDeltaClosedRoutes(const std::vector<int>& route_a,
                             size_t idx_a,
                             const std::vector<int>& route_b,
                             size_t idx_b,
                             const DistanceFn& distance) {
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
void ImproveRoute2OptGenericV3(std::vector<int>& route, const DistanceFn& distance) {
    if (route.size() <= 4) {
        return;
    }

    std::vector<char> dont_look(route.size(), 0);

    bool improved = true;
    while (improved) {
        improved = false;

        for (size_t i = 1; i + 2 < route.size(); ++i) {
            if (dont_look[i]) {
                continue;
            }

            bool improved_from_i = false;
            for (size_t j = i + 1; j + 1 < route.size(); ++j) {
                const double before = distance(route[i - 1], route[i]) + distance(route[j], route[j + 1]);
                const double after = distance(route[i - 1], route[j]) + distance(route[i], route[j + 1]);
                if (after + kEps < before) {
                    std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                                 route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    std::fill(dont_look.begin(), dont_look.end(), 0);
                    improved = true;
                    improved_from_i = true;
                    break; // first-improvement restart
                }
            }

            if (improved_from_i) {
                break;
            }

            dont_look[i] = 1;
        }
    }
}

CandidateSets BuildCandidateSetsV3(const mtsp::Instance& inst, int candidate_count) {
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

std::vector<int> CollectConstructionCandidatesV3(int from,
                                                 const std::vector<char>& visited,
                                                 const CandidateSets& candidate_sets,
                                                 const mtsp::Instance& inst,
                                                 int fallback_limit) {
    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(fallback_limit));

    for (int city : candidate_sets[from]) {
        if (!visited[city]) {
            candidates.push_back(city);
            if (static_cast<int>(candidates.size()) >= fallback_limit) {
                return candidates;
            }
        }
    }

    std::vector<std::pair<double, int>> fallback;
    fallback.reserve(inst.GetNodeCount());
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[city] &&
            std::find(candidates.begin(), candidates.end(), city) == candidates.end()) {
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

double ForwardPotentialV3(int node,
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
bool ApplyFirstImproving2OptV3(std::vector<int>& route,
                               const CandidateSets& candidate_sets,
                               int node_count,
                               const DistanceFn& distance,
                               std::vector<int>& position,
                               std::vector<char>& dont_look) {
    if (route.size() <= 4) {
        return false;
    }

    std::fill(position.begin(), position.end(), -1);
    for (size_t idx = 0; idx < route.size(); ++idx) {
        position[route[idx]] = static_cast<int>(idx);
    }

    for (size_t i = 1; i + 2 < route.size(); ++i) {
        const int t1 = route[i - 1];
        const int t2 = route[i];

        if (dont_look[t1]) {
            continue;
        }

        bool improved_from_anchor = false;
        for (int t3 : candidate_sets[t1]) {
            const int j = position[t3];
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
                improved_from_anchor = true;
                break; // first-improvement is faster here
            }
        }

        if (improved_from_anchor) {
            return true;
        }

        dont_look[t1] = 1;
    }

    return false;
}

template <typename DistanceFn>
void ImproveRouteGuidedV3(std::vector<int>& route,
                          const CandidateSets& candidate_sets,
                          int node_count,
                          const DistanceFn& distance) {
    if (route.size() <= 4) {
        return;
    }

    std::vector<int> position(node_count, -1);
    std::vector<char> dont_look(node_count, 0);

    bool improved = true;
    while (improved) {
        improved = ApplyFirstImproving2OptV3(route, candidate_sets, node_count, distance, position, dont_look);
        if (!improved && route.size() > 4) {
            std::vector<int> reversed = route;
            std::reverse(reversed.begin() + 1, reversed.end() - 1);
            std::fill(dont_look.begin(), dont_look.end(), 0);
            if (ApplyFirstImproving2OptV3(reversed, candidate_sets, node_count, distance, position, dont_look)) {
                route.swap(reversed);
                improved = true;
            }
        }
    }

    // A bounded cleanup pass is kept, but now it is first-improvement + don't-look bits.
    ImproveRoute2OptGenericV3(route, distance);
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
void IteratedLocalSearchV3(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const CandidateSets& candidate_sets,
                           int node_count,
                           const DistanceFn& distance) {
    ImproveRouteGuidedV3(route, candidate_sets, node_count, distance);
    double best_length = RouteLengthGenericV3(route, distance);
    std::vector<int> best = route;

    for (int round = 0; round < rounds; ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV2(candidate, rng, distance);
        ImproveRouteGuidedV3(candidate, candidate_sets, node_count, distance);
        const double candidate_length = RouteLengthGenericV3(candidate, distance);
        if (candidate_length + kEps < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }

    route.swap(best);
}

} // namespace

namespace mtsp {

class LkhWrapperSolverV3 : public Solver {
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
        const CandidateSets candidate_sets = BuildCandidateSetsV3(inst, candidate_count_);
        std::mt19937 rng(seed_);

        BuildInitialRoutes(out, candidate_sets);

        for (auto& route : out) {
            route.push_back(0);
            IteratedLocalSearchV3(route,
                                 rng,
                                 rounds_,
                                 candidate_sets,
                                 inst.GetNodeCount(),
                                 [&inst](int a, int b) { return inst.Distance(a, b); });
        }

        ImproveInterRoute(out, candidate_sets);
    }

private:
    void BuildInitialRoutes(RouteSet& out, const CandidateSets& candidate_sets) const {
        const Instance& inst = Instance::GetInstance();

        out.assign(inst.GetSalesmanCount(), std::vector<int>{0});
        std::vector<int> current(inst.GetSalesmanCount(), 0);
        std::vector<int> route_sizes(inst.GetSalesmanCount(), 0);
        std::vector<char> visited(inst.GetNodeCount(), 0);
        visited[0] = 1;

        int remaining = inst.GetNodeCount() - 1;
        while (remaining > 0) {
            int best_salesman = -1;
            int best_city = -1;
            double best_score = std::numeric_limits<double>::max();
            double best_immediate = std::numeric_limits<double>::max();

            for (int salesman = 0; salesman < inst.GetSalesmanCount(); ++salesman) {
                const std::vector<int> move_candidates = CollectConstructionCandidatesV3(
                    current[salesman], visited, candidate_sets, inst, std::max(candidate_count_, 6));

                for (int city : move_candidates) {
                    const double immediate = inst.Distance(current[salesman], city);
                    const double forward = ForwardPotentialV3(city, visited, candidate_sets, inst);
                    const double depot = inst.Distance(city, 0);

                    // Light balancing term: keeps the greedy construction but avoids starving routes.
                    const double balance_penalty = 0.05 * static_cast<double>(route_sizes[salesman]);
                    const double score = immediate + lookahead_weight_ * forward + depot_weight_ * depot + balance_penalty;

                    if (score + kEps < best_score ||
                        (std::abs(score - best_score) <= kEps && immediate + kEps < best_immediate)) {
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
            ++route_sizes[best_salesman];
            visited[best_city] = 1;
            --remaining;
        }
    }

    void ImproveInterRoute(RouteSet& routes, const CandidateSets& candidate_sets) const {
        const Instance& inst = Instance::GetInstance();
        const auto distance = [&inst](int u, int v) { return inst.Distance(u, v); };

        bool improved = true;
        while (improved) {
            improved = false;

            for (size_t a = 0; a < routes.size() && !improved; ++a) {
                for (size_t b = a + 1; b < routes.size() && !improved; ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size() && !improved; ++i) {
                        for (size_t j = 1; j + 1 < routes[b].size(); ++j) {
                            const int city_a = routes[a][i];
                            const int city_b = routes[b][j];

                            if (!IsPromisingSwapPair(city_a, city_b, candidate_sets)) {
                                continue;
                            }

                            const double delta = SwapDeltaClosedRoutes(routes[a], i, routes[b], j, distance);
                            if (delta >= -kEps) {
                                continue;
                            }

                            std::swap(routes[a][i], routes[b][j]);

                            IteratedLocalSearchV3(routes[a],
                                                 local_rng_,
                                                 std::max(4, rounds_ / 2),
                                                 candidate_sets,
                                                 inst.GetNodeCount(),
                                                 distance);
                            IteratedLocalSearchV3(routes[b],
                                                 local_rng_,
                                                 std::max(4, rounds_ / 2),
                                                 candidate_sets,
                                                 inst.GetNodeCount(),
                                                 distance);

                            improved = true;
                            break; // first-improvement restart after accepted inter-route move
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

static bool reg_lkh_mtsp_v3 = (SolverFactory::RegisterSolver("lkh-wrapper-v3", []() {
    return std::make_unique<LkhWrapperSolverV3>();
}),
                               true);

} // namespace mtsp
