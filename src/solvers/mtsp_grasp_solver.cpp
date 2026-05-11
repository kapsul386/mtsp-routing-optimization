#include <algorithm>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

// GRASP solver (Greedy Randomised Adaptive Search Procedure) — classic two-phase
// metaheuristic: on each iteration (1) a randomised greedy solution is built via a
// Restricted Candidate List, then (2) improved by local search.
// The best MINSUM solution across all iterations is returned.
class GraspSolver : public Solver {
public:
    // CLI parameters:
    //   iters — number of iterations (default 50);
    //   rcl   — size of the restricted candidate list (default 3);
    //   seed  — random number generator seed.
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("iters")) {
            iterations_ = std::max(1, std::stoi(opts.at("iters")));
        }
        if (opts.count("rcl")) {
            rcl_size_ = std::max(1, std::stoi(opts.at("rcl")));
        }
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
    }

    // Main GRASP loop: construct-improve-compare, repeated iterations_ times.
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        std::mt19937 rng(seed_);

        double best_objective = std::numeric_limits<double>::max();
        RouteSet best_routes;

        for (int iter = 0; iter < iterations_; ++iter) {
            // === Phase 1: build a randomised greedy solution ===
            RouteSet candidate(inst.GetSalesmanCount(), std::vector<int>{0});
            std::vector<char> visited(inst.GetNodeCount(), 0);
            std::vector<int> current(inst.GetSalesmanCount(), 0);
            visited[0] = 1;
            int remaining = inst.GetNodeCount() - 1;

            // Round-robin with random selection from the top rcl_size_ nearest unvisited clients.
            while (remaining > 0) {
                for (int salesman = 0; salesman < inst.GetSalesmanCount() && remaining > 0; ++salesman) {
                    // Collect all unvisited clients and their distances to the current position.
                    std::vector<std::pair<double, int>> choices;
                    for (int city = 1; city < inst.GetNodeCount(); ++city) {
                        if (!visited[city]) {
                            choices.push_back({inst.Distance(current[salesman], city), city});
                        }
                    }
                    if (choices.empty()) {
                        continue;
                    }
                    // Sort by distance, pick uniformly at random from the rcl_bound best.
                    std::sort(choices.begin(), choices.end());
                    const int rcl_bound = std::min<int>(rcl_size_, static_cast<int>(choices.size()));
                    std::uniform_int_distribution<int> pick(0, rcl_bound - 1);
                    const int next_city = choices[pick(rng)].second;
                    candidate[salesman].push_back(next_city);
                    current[salesman] = next_city;
                    visited[next_city] = 1;
                    --remaining;
                }
            }

            // Close routes with a return to the depot and apply 2-opt.
            for (auto& route : candidate) {
                route.push_back(0);
                ImproveRoute2Opt(route);
            }

            // === Phase 2: inter-route pairwise client swaps until convergence ===
            // Enumerate all route pairs (a, b) and position pairs (i, j); if a swap
            // improves MINSUM, commit it and re-run 2-opt on both routes.
            bool improved = true;
            while (improved) {
                improved = false;
                for (size_t a = 0; a < candidate.size(); ++a) {
                    for (size_t b = a + 1; b < candidate.size(); ++b) {
                        for (size_t i = 1; i + 1 < candidate[a].size(); ++i) {
                            for (size_t j = 1; j + 1 < candidate[b].size(); ++j) {
                                std::swap(candidate[a][i], candidate[b][j]);
                                const double swapped = ObjectiveMinsum(candidate);
                                std::swap(candidate[a][i], candidate[b][j]);
                                const double current_obj = ObjectiveMinsum(candidate);
                                if (swapped + 1e-9 < current_obj) {
                                    std::swap(candidate[a][i], candidate[b][j]);
                                    ImproveRoute2Opt(candidate[a]);
                                    ImproveRoute2Opt(candidate[b]);
                                    improved = true;
                                }
                            }
                        }
                    }
                }
            }

            // Compare the candidate against the global best solution.
            const double objective = ObjectiveMinsum(candidate);
            if (objective < best_objective) {
                best_objective = objective;
                best_routes = candidate;
            }
        }

        out = best_routes;
    }

private:
    int iterations_ = 50;
    int rcl_size_ = 3;
    unsigned int seed_ = 42U;
};

static bool reg_grasp = (SolverFactory::RegisterSolver("grasp", []() {
    return std::make_unique<GraspSolver>();
}),
                         true);

} // namespace mtsp
