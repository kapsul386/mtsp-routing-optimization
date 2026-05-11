// First version of the custom mTSP wrapper in the LKH style (lkh-wrapper-v1).
// Algorithm: m greedy initial routes + independent k-opt search on each.
// This is the first step from the pure 2opt+greed heuristic toward candidate-based search.
// No spatial structures (KD-tree, grid) are used yet — only linear nearest-neighbor.
// Kept as a baseline for Pareto comparison in Appendix D.

#include <algorithm>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace {

// Sum of route edge lengths (templated on the distance functor).
template <typename DistanceFn>
double RouteLengthGenericV1(const std::vector<int>& route, const DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

// 2-opt route optimization to convergence (first-improvement strategy).
template <typename DistanceFn>
void ImproveRoute2OptGenericV1(std::vector<int>& route, const DistanceFn& distance) {
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

// Double-bridge perturbation: a 4-opt move for escaping 2-opt local optima.
// The route is cut into 4 random segments and reassembled in the order [head, B, D, C, A, tail].
template <typename DistanceFn>
void DoubleBridgeKickV1(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
    if (route.size() < 10) {
        return;
    }

    const int n = static_cast<int>(route.size()) - 1;
    std::uniform_int_distribution<int> cut1_dist(1, n / 4);
    std::uniform_int_distribution<int> cut2_dist(n / 4 + 1, n / 2);
    std::uniform_int_distribution<int> cut3_dist(n / 2 + 1, (3 * n) / 4);

    int a = cut1_dist(rng);
    int b = cut2_dist(rng);
    int c = cut3_dist(rng);

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

// Iterated Local Search: 2-opt → double-bridge → 2-opt → compare, for rounds iterations.
// The best-length solution is returned via the outparam.
template <typename DistanceFn>
void IteratedLocalSearchV1(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const DistanceFn& distance) {
    ImproveRoute2OptGenericV1(route, distance);
    double best_length = RouteLengthGenericV1(route, distance);
    std::vector<int> best = route;

    for (int round = 0; round < rounds; ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV1(candidate, rng, distance);
        ImproveRoute2OptGenericV1(candidate, distance);
        const double candidate_length = RouteLengthGenericV1(candidate, distance);
        if (candidate_length + 1e-9 < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }

    route.swap(best);
}

} // namespace

namespace mtsp {

// v1: first version of the custom mTSP wrapper in the LKH style.
// Simplest scheme: round-robin nearest-neighbor + ILS on each route + inter-route swap.
class LkhWrapperSolverV1 : public Solver {
public:
    // CLI parameters: seed (for RNG) and rounds (number of ILS iterations per route).
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
    }

    // Main loop of v1: greedy client assignment to m salesmen,
    // then ILS on each route, then inter-route client-pair swaps.
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        std::mt19937 rng(seed_);

        out.assign(inst.GetSalesmanCount(), std::vector<int>{0});
        std::vector<int> current(inst.GetSalesmanCount(), 0);
        std::vector<char> visited(inst.GetNodeCount(), 0);
        visited[0] = 1;

        int remaining = inst.GetNodeCount() - 1;
        while (remaining > 0) {
            for (int salesman = 0; salesman < inst.GetSalesmanCount() && remaining > 0; ++salesman) {
                int best_city = -1;
                double best_dist = std::numeric_limits<double>::max();
                for (int city = 1; city < inst.GetNodeCount(); ++city) {
                    if (!visited[city]) {
                        const double d = inst.Distance(current[salesman], city);
                        if (d < best_dist) {
                            best_dist = d;
                            best_city = city;
                        }
                    }
                }
                if (best_city == -1) {
                    continue;
                }
                out[salesman].push_back(best_city);
                current[salesman] = best_city;
                visited[best_city] = 1;
                --remaining;
            }
        }

        for (auto& route : out) {
            route.push_back(0);
            IteratedLocalSearchV1(route, rng, rounds_, [&inst](int a, int b) { return inst.Distance(a, b); });
        }

        ImproveInterRoute(out);
    }

private:
    // Inter-route optimization: enumerate pairs (a, b) and position pairs (i, j); on improvement,
    // commit the swap and re-run ILS on both affected routes.
    // Complexity O(m^2 * L^2) per pass; several passes may be needed to reach convergence.
    void ImproveInterRoute(RouteSet& routes) const {
        const Instance& inst = Instance::GetInstance();
        bool improved = true;

        while (improved) {
            improved = false;
            for (size_t a = 0; a < routes.size(); ++a) {
                for (size_t b = a + 1; b < routes.size(); ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size(); ++i) {
                        for (size_t j = 1; j + 1 < routes[b].size(); ++j) {
                            std::swap(routes[a][i], routes[b][j]);
                            const double swapped = ObjectiveMinsum(routes);
                            std::swap(routes[a][i], routes[b][j]);
                            const double current = ObjectiveMinsum(routes);
                            if (swapped + 1e-9 < current) {
                                std::swap(routes[a][i], routes[b][j]);
                                IteratedLocalSearchV1(routes[a], local_rng_, std::max(4, rounds_ / 2),
                                                      [&inst](int u, int v) { return inst.Distance(u, v); });
                                IteratedLocalSearchV1(routes[b], local_rng_, std::max(4, rounds_ / 2),
                                                      [&inst](int u, int v) { return inst.Distance(u, v); });
                                improved = true;
                            }
                        }
                    }
                }
            }
        }
    }

    unsigned int seed_ = 42U;
    int rounds_ = 24;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v1 = (SolverFactory::RegisterSolver("lkh-wrapper-v1", []() {
    return std::make_unique<LkhWrapperSolverV1>();
}),
                               true);

} // namespace mtsp
