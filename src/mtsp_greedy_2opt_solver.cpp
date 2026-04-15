#include <limits>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

class GreedyTwoOptSolver : public Solver {
public:
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        out.assign(inst.GetSalesmanCount(), std::vector<int>{0});

        std::vector<int> current(inst.GetSalesmanCount(), 0);
        std::vector<char> visited(inst.GetNodeCount(), 0);
        visited[0] = 1;
        int remaining = inst.GetNodeCount() - 1;

        while (remaining > 0) {
            for (int salesman = 0; salesman < inst.GetSalesmanCount() && remaining > 0; ++salesman) {
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
                out[salesman].push_back(best_city);
                current[salesman] = best_city;
                visited[best_city] = 1;
                --remaining;
            }
        }

        for (auto& route : out) {
            route.push_back(0);
            ImproveRoute2Opt(route);
        }
    }
};

static bool reg_greedy_2opt = (SolverFactory::RegisterSolver("2opt+greed", []() {
    return std::make_unique<GreedyTwoOptSolver>();
}),
                               true);

} // namespace mtsp
