#include <solver.h>
#include <factory.h>
#include <random>
#include <algorithm>

namespace tsp {

    // Greedy nearest-neighbor solver for the classical TSP.
    // Simplest baseline: at each step, the nearest unvisited client is selected.
    // Registered as "nearest".
    class GreedyNearest : public Solver {
    public:
        int start = 0;  // starting vertex index (default 0)

        // CLI parameter "start" — sets the starting vertex; clamped to [0, n-1] in Solve.
        void Configure(const std::unordered_map<std::string, std::string> &opts) override {
            if (opts.count("start")) start = std::stoi(opts.at("start"));
        }

        // Main loop: linear scan for the nearest unvisited client at each step.
        // Complexity O(n^2). The route is closed by returning to the starting vertex.
        void Solve(std::vector<int> &out) override {
            const Instance &inst = Instance::GetInstance();
            int n = inst.GetN();
            out.clear();
            out.reserve(n + 1);
            std::vector<char> used(n, 0);
            int cur = std::clamp(start, 0, n - 1);
            out.push_back(cur);
            used[cur] = 1;
            for (int step = 1; step < n; ++step) {
                // Linear scan for the nearest unvisited client from the current position.
                int best = -1;
                double bestd = 1e300;
                for (int j = 0; j < n; ++j)
                    if (!used[j]) {
                        double d = inst.Distance(cur, j);
                        if (d < bestd) {
                            bestd = d;
                            best = j;
                        }
                    }
                out.push_back(best);
                used[best] = 1;
                cur = best;
            }
            out.push_back(out[0]);  // close the route by returning to the start
        }
    };

    // Register in the factory under the name "nearest".
    static bool reg = (SolverFactory::RegisterSolver("nearest", []() {
        return std::make_unique<GreedyNearest>();
    }), true);

} // namespace tsp
