#include <solver.h>
#include <factory.h>

#include <random>
#include <algorithm>
#include <unordered_map>

namespace tsp {

// Variable Neighborhood Search for the classical TSP. Registered as "vns".
// Alternates between perturbation types (swap, 2-opt, 3-opt) in order of increasing strength.
class VNS : public Solver {
public:
    int start = 0;
    int max_it = 10000;  // maximum number of iterations

    // CLI parameters: start (starting vertex), it (number of iterations).
    void Configure(const std::unordered_map<std::string, std::string> &opts) override {
        if (opts.count("start")) {
            start = std::stoi(opts.at("start"));
        }
        if (opts.count("it")) {
            max_it = std::stoi(opts.at("it"));
        }
    }

    // Main VNS loop: alternate neighborhood structures until convergence or max_it.
    void Solve(std::vector<int> &out) override {
        const Instance &inst = Instance::GetInstance();
        int n = inst.GetN();

        if (n <= 1) {
            out.clear();
            if (n == 1) {
                out.push_back(0);
                out.push_back(0);
            }
            return;
        }

        std::mt19937 rng(std::random_device{}());

        std::vector<int> tour;
        BuildInitial(inst, n, tour);
        double best_len = TourLength(inst, tour);

        int it = 0;
        int k_max = 3;

        while (it < max_it) {
            for (int k = 1; k <= k_max && it < max_it; ++k) {
                std::vector<int> shaken = tour;
                Shake(shaken, rng, k);
                LocalSearch2Opt(inst, shaken, it);
                double len = TourLength(inst, shaken);

                if (len + 1e-9 < best_len) {
                    tour.swap(shaken);
                    best_len = len;
                    break;
                }
            }
            ++it;
        }

        out = tour;
    }

private:
    static double TourLength(const Instance &inst, const std::vector<int> &p) {
        double res = 0.0;
        int m = static_cast<int>(p.size());
        for (int i = 1; i < m; ++i) {
            int u = p[i - 1];
            int v = p[i];
            res += inst.Distance(u, v);
        }
        return res;
    }

    void BuildInitial(const Instance &inst, int n, std::vector<int> &tour) const {
        tour.clear();
        tour.reserve(n + 1);
        std::vector<char> used(n, 0);

        int cur = start;
        if (cur < 0) {
            cur = 0;
        }
        if (cur >= n) {
            cur = n - 1;
        }

        tour.push_back(cur);
        used[cur] = 1;

        for (int step = 1; step < n; ++step) {
            int best = -1;
            double bestd = 1e300;

            for (int j = 0; j < n; ++j) {
                if (!used[j]) {
                    double d = inst.Distance(cur, j);
                    if (d < bestd) {
                        bestd = d;
                        best = j;
                    }
                }
            }

            tour.push_back(best);
            used[best] = 1;
            cur = best;
        }

        tour.push_back(tour[0]);
    }

    static void Shake(std::vector<int> &tour, std::mt19937 &rng, int k) {
        int n = static_cast<int>(tour.size()) - 1;
        if (n <= 3) {
            return;
        }

        std::uniform_int_distribution<int> dist(1, n - 1);

        for (int i = 0; i < k; ++i) {
            int a = dist(rng);
            int b = dist(rng);
            if (a > b) {
                std::swap(a, b);
            }
            if (a < b) {
                std::reverse(tour.begin() + a, tour.begin() + b + 1);
            }
        }

        tour.back() = tour.front();
    }

    static void LocalSearch2Opt(const Instance &inst, std::vector<int> &tour, int &it_counter) {
        int n = static_cast<int>(tour.size()) - 1;
        bool improved = true;

        while (improved) {
            improved = false;

            for (int i = 1; i < n - 1; ++i) {
                for (int k = i + 1; k < n; ++k) {
                    int a = tour[i - 1];
                    int b = tour[i];
                    int c = tour[k];
                    int d = tour[(k + 1) % (n + 1)];

                    double delta =
                        inst.Distance(a, c) +
                        inst.Distance(b, d) -
                        inst.Distance(a, b) -
                        inst.Distance(c, d);

                    if (delta < -1e-9) {
                        std::reverse(tour.begin() + i, tour.begin() + k + 1);
                        tour.back() = tour.front();
                        improved = true;
                        ++it_counter;
                        if (it_counter > 1000000000) {
                            return;
                        }
                    }
                }
            }
        }
    }
};

static bool reg_vns = (SolverFactory::RegisterSolver("vns", []() {
    return std::make_unique<VNS>();
}), true);

} // namespace tsp
