#include <algorithm>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <factory.h>
#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>
#include <solver.h>

// === Early LKH-inspired solver (predecessor of the v1..v21 line) ===
// Uses greedy nearest-neighbor construction + iterated local search with double-bridge kicks.
// Exists only as a historical starting point; not part of the main experiment grid
// (superseded by the custom v1..v21 line and by ALNS-mTSP).

namespace {

// Computes route length by calling a templated distance functor.
// This allows one implementation to be reused for both tsp::Instance and mtsp::Instance.
template <typename DistanceFn>
double RouteLengthGeneric(const std::vector<int>& route, const DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

// Basic 2-opt route optimization to convergence (first-improvement strategy).
// Templated on the distance functor so it can be reused in TSP and mTSP modes.
template <typename DistanceFn>
void ImproveRoute2OptGeneric(std::vector<int>& route, const DistanceFn& distance) {
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

// Double-bridge perturbation: cuts the route into 4 random segments and
// reassembles them in the order [0, B, D, C, A]. This is the classic way to escape
// a 2-opt local optimum: a 4-opt perturbation that 2-opt cannot undo.
template <typename DistanceFn>
void DoubleBridgeKick(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
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

// Iterated Local Search: 2-opt → double-bridge kick → 2-opt → compare.
// The best solution across all rounds is kept and returned.
// This is the classical scheme of Lourenço, Martin, Stützle (Handbook of Metaheuristics).
template <typename DistanceFn>
void IteratedLocalSearch(std::vector<int>& route,
                         std::mt19937& rng,
                         int rounds,
                         const DistanceFn& distance) {
    ImproveRoute2OptGeneric(route, distance);
    double best_length = RouteLengthGeneric(route, distance);
    std::vector<int> best = route;

    for (int round = 0; round < rounds; ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKick(candidate, rng, distance);
        ImproveRoute2OptGeneric(candidate, distance);
        const double candidate_length = RouteLengthGeneric(candidate, distance);
        if (candidate_length + 1e-9 < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }

    route.swap(best);
}

} // namespace

namespace tsp {

// Classical TSP solver in the LKH style (simplified version). Registered
// as "lkh" for the --step lkh command in the main TSP harness.
class LkhInspiredSolver : public Solver {
public:
    // CLI parameters:
    //   start  — starting vertex index (default 0);
    //   rounds — number of ILS iterations (default 32);
    //   seed   — seed for the random number generator.
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("start")) {
            start_ = std::stoi(opts.at("start"));
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
    }

    // Main loop: build a nearest-neighbor route + ILS improvement.
    void Solve(std::vector<int>& out) override {
        const Instance& inst = Instance::GetInstance();
        const int n = inst.GetN();
        if (n <= 1) {
            out = {0, 0};
            return;
        }

        // Greedy initial route construction: select the nearest unvisited city at each step.
        start_ = std::clamp(start_, 0, n - 1);
        std::vector<int> route;
        route.reserve(n + 1);
        route.push_back(start_);

        std::vector<char> used(n, 0);
        used[start_] = 1;
        int current = start_;
        for (int step = 1; step < n; ++step) {
            int best = -1;
            double best_dist = std::numeric_limits<double>::max();
            for (int city = 0; city < n; ++city) {
                if (!used[city]) {
                    const double d = inst.Distance(current, city);
                    if (d < best_dist) {
                        best_dist = d;
                        best = city;
                    }
                }
            }
            route.push_back(best);
            used[best] = 1;
            current = best;
        }
        route.push_back(start_);

        std::mt19937 rng(seed_);
        IteratedLocalSearch(route, rng, rounds_, [&inst](int a, int b) { return inst.Distance(a, b); });
        out = route;
    }

private:
    int start_ = 0;
    int rounds_ = 32;
    unsigned int seed_ = 42U;
};

static bool reg_lkh_tsp = (SolverFactory::RegisterSolver("lkh", []() {
    return std::make_unique<LkhInspiredSolver>();
}),
                           true);

} // namespace tsp

namespace mtsp {

// Early mTSP wrapper in the LKH style. Registered as "lkh-wrapper" in SolverFactory.
// Algorithm: round-robin nearest-neighbor + ILS on each route + inter-route swap.
// This is the predecessor of the entire v1..v21 line; not used in the main experiment grid
// (superseded by the custom versions).
class LkhWrapperSolver : public Solver {
public:
    // CLI parameters: seed (RNG seed) and rounds (number of ILS iterations per route).
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
    }

    // Main scheme:
    //   (1) round-robin nearest-neighbor builds m initial routes;
    //   (2) ILS (rounds iterations) is applied to each route;
    //   (3) inter-route client-pair swaps are run to convergence.
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
            IteratedLocalSearch(route, rng, rounds_, [&inst](int a, int b) { return inst.Distance(a, b); });
        }

        ImproveInterRoute(out);
    }

private:
    // Inter-route optimization: enumerate pairs (a, b) and position pairs (i, j); if
    // swapping clients between routes improves MINSUM, commit the swap and re-run
    // ILS on both affected routes. Complexity O(m^2 L^2) per pass.
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
                                IteratedLocalSearch(routes[a], local_rng_, std::max(4, rounds_ / 2),
                                                    [&inst](int u, int v) { return inst.Distance(u, v); });
                                IteratedLocalSearch(routes[b], local_rng_, std::max(4, rounds_ / 2),
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

static bool reg_lkh_mtsp = (SolverFactory::RegisterSolver("lkh-wrapper", []() {
    return std::make_unique<LkhWrapperSolver>();
}),
                            true);

} // namespace mtsp
