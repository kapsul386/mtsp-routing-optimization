#include <limits>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

// Greedy solver: round-robin nearest-neighbor + 2-opt local search.
// Used as the lower-quality end of the quality/time Pareto curve in comparisons.
class GreedyTwoOptSolver : public Solver {
public:
    // Two-step algorithm:
    //   (1) Round-robin: each salesman in turn picks the nearest unvisited client;
    //   (2) 2-opt is applied to each assembled route until convergence.
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        // Each route starts at the depot (vertex 0).
        out.assign(inst.GetSalesmanCount(), std::vector<int>{0});

        // current[s] — most recent vertex visited by salesman s.
        std::vector<int> current(inst.GetSalesmanCount(), 0);
        std::vector<char> visited(inst.GetNodeCount(), 0);
        visited[0] = 1;  // depot is treated as already visited
        int remaining = inst.GetNodeCount() - 1;

        // Main round-robin loop: run while any clients remain unvisited.
        while (remaining > 0) {
            for (int salesman = 0; salesman < inst.GetSalesmanCount() && remaining > 0; ++salesman) {
                // Find the nearest unvisited client to the salesman's current position.
                double best_dist = std::numeric_limits<double>::max();
                int best_city = -1;
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
                // Append the selected client to the route and mark it visited.
                out[salesman].push_back(best_city);
                current[salesman] = best_city;
                visited[best_city] = 1;
                --remaining;
            }
        }

        // Close each route with a return to the depot and apply 2-opt.
        for (auto& route : out) {
            route.push_back(0);
            ImproveRoute2Opt(route);
        }
    }
};

// Register the solver in the factory under the name "2opt+greed".
static bool reg_greedy_2opt = (SolverFactory::RegisterSolver("2opt+greed", []() {
    return std::make_unique<GreedyTwoOptSolver>();
}),
                               true);

} // namespace mtsp
