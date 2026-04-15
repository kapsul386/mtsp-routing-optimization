#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

class RandomNearestSolver : public Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        std::vector<int> cities(inst.GetNodeCount() - 1);
        std::iota(cities.begin(), cities.end(), 1);

        std::mt19937 rng(seed_);
        std::shuffle(cities.begin(), cities.end(), rng);

        std::vector<std::vector<int>> assigned(inst.GetSalesmanCount());
        for (size_t idx = 0; idx < cities.size(); ++idx) {
            assigned[idx % assigned.size()].push_back(cities[idx]);
        }

        out.clear();
        out.reserve(assigned.size());
        for (const auto& group : assigned) {
            out.push_back(BuildNearestOrder(group));
        }
    }

private:
    unsigned int seed_ = 42U;
};

static bool reg_rand_nn = (SolverFactory::RegisterSolver("rand+nn", []() {
    return std::make_unique<RandomNearestSolver>();
}),
                           true);

} // namespace mtsp
