#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

// Simplest random round-robin nearest-neighbor solver.
// Used as the quality lower bound for all other solvers.
class RandomNearestSolver : public Solver {
public:
    // Accepts an optional "seed" CLI parameter. Defaults to 42 if not provided.
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
    }

    // Builds a solution in two steps:
    //   1) Shuffle the client list randomly and distribute round-robin across m salesmen;
    //   2) For each group, complete the order greedily by nearest-neighbor (BuildNearestOrder).
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        // Client index list: 1..n-1 (vertex 0 is the depot, handled separately in the route).
        std::vector<int> cities(inst.GetNodeCount() - 1);
        std::iota(cities.begin(), cities.end(), 1);

        std::mt19937 rng(seed_);
        std::shuffle(cities.begin(), cities.end(), rng);

        // Round-robin distribution of clients across salesmen.
        std::vector<std::vector<int>> assigned(inst.GetSalesmanCount());
        for (size_t idx = 0; idx < cities.size(); ++idx) {
            assigned[idx % assigned.size()].push_back(cities[idx]);
        }

        // Build each group's route greedily by nearest-neighbor.
        out.clear();
        out.reserve(assigned.size());
        for (const auto& group : assigned) {
            out.push_back(BuildNearestOrder(group));
        }
    }

private:
    unsigned int seed_ = 42U;
};

// Register the solver in the factory under the name "rand+nn".
static bool reg_rand_nn = (SolverFactory::RegisterSolver("rand+nn", []() {
    return std::make_unique<RandomNearestSolver>();
}),
                           true);

} // namespace mtsp
