#include <solver.h>
#include <factory.h>

#include <random>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace tsp {

// Memetic algorithm for the classical TSP — a hybrid of GA with local search.
// Registered as "memetic". Each offspring (with probability localProb)
// is additionally improved by a short local search pass (localIterPerChild).
// This gives better convergence at a smaller populationSize than a pure GA.
class Memetic : public Solver {
public:
    // ===== Parameters tuned for n ~ 3000 =====
    int start = 0;

    int populationSize = 220;
    int generations    = 900;
    int tournamentSize = 5;

    double crossoverRate = 0.95;
    double mutationRate  = 0.22;

    // memetic part
    int localIterPerChild = 12;   // very limited for large n
    double localProb      = 0.25; // only part of children improved

    std::mt19937 rng;

    void Configure(const std::unordered_map<std::string, std::string> &opts) override {
        if (opts.count("start")) {
            start = std::stoi(opts.at("start"));
        }
        if (opts.count("pop")) {
            populationSize = std::stoi(opts.at("pop"));
        }
        if (opts.count("gen")) {
            generations = std::stoi(opts.at("gen"));
        }
        if (opts.count("tour")) {
            tournamentSize = std::stoi(opts.at("tour"));
        }
        if (opts.count("cx")) {
            crossoverRate = std::stod(opts.at("cx"));
        }
        if (opts.count("mut")) {
            mutationRate = std::stod(opts.at("mut"));
        }
        if (opts.count("local_iter")) {
            localIterPerChild = std::stoi(opts.at("local_iter"));
        }
        if (opts.count("local_prob")) {
            localProb = std::stod(opts.at("local_prob"));
        }
        if (opts.count("seed")) {
            rng.seed(std::stoul(opts.at("seed")));
        } else {
            rng.seed(std::random_device{}());
        }
    }

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

        NormalizeStart(n);

        std::vector<std::vector<int>> population(populationSize);
        std::vector<double> fitness(populationSize);

        BuildInitialPopulation(inst, population, fitness);

        int bestIdx = BestIndex(fitness);
        std::vector<int> bestPerm = population[bestIdx];
        double bestLen = fitness[bestIdx];

        std::vector<std::vector<int>> nextPopulation(populationSize);
        std::vector<double> nextFitness(populationSize);

        std::uniform_real_distribution<double> pr(0.0, 1.0);

        for (int gen = 0; gen < generations; ++gen) {
            // elitism
            nextPopulation[0] = bestPerm;
            nextFitness[0] = bestLen;

            for (int i = 1; i < populationSize; ++i) {
                const std::vector<int> &p1 = Tournament(population, fitness);
                const std::vector<int> &p2 = Tournament(population, fitness);

                std::vector<int> child = p1;

                if (pr(rng) < crossoverRate) {
                    OrderCrossover(p1, p2, child);
                }

                Mutate(child);

                if (pr(rng) < localProb) {
                    LocalImprove(inst, child);
                }

                double len = TourLength(inst, child);
                nextPopulation[i] = std::move(child);
                nextFitness[i] = len;

                if (len < bestLen) {
                    bestLen = len;
                    bestPerm = nextPopulation[i];
                }
            }

            population.swap(nextPopulation);
            fitness.swap(nextFitness);
        }

        NormalizeCycle(bestPerm);
        BuildTour(bestPerm, out);
    }

private:
    void NormalizeStart(int n) {
        if (start < 0) start = 0;
        if (start >= n) start = n - 1;
    }

    void BuildInitialPopulation(const Instance &inst,
                                std::vector<std::vector<int>> &pop,
                                std::vector<double> &fit) {
        int n = inst.GetN();

        std::vector<int> base;
        base.reserve(n - 1);
        for (int i = 0; i < n; ++i) {
            if (i != start) base.push_back(i);
        }

        // greedy + light local search
        pop[0] = GreedyNearest(inst);
        LocalImprove(inst, pop[0]);
        fit[0] = TourLength(inst, pop[0]);

        for (int i = 1; i < populationSize; ++i) {
            pop[i] = base;
            std::shuffle(pop[i].begin(), pop[i].end(), rng);
            Mutate(pop[i]);
            fit[i] = TourLength(inst, pop[i]);
        }
    }

    std::vector<int> GreedyNearest(const Instance &inst) {
        int n = inst.GetN();
        std::vector<int> perm;
        perm.reserve(n - 1);

        std::vector<char> used(n, 0);
        used[start] = 1;

        int cur = start;
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
            used[best] = 1;
            perm.push_back(best);
            cur = best;
        }
        return perm;
    }

    const std::vector<int>& Tournament(const std::vector<std::vector<int>> &pop,
                                       const std::vector<double> &fit) {
        std::uniform_int_distribution<int> dist(0, populationSize - 1);
        int best = dist(rng);
        for (int i = 1; i < tournamentSize; ++i) {
            int idx = dist(rng);
            if (fit[idx] < fit[best]) best = idx;
        }
        return pop[best];
    }

    static void OrderCrossover(const std::vector<int> &p1,
                               const std::vector<int> &p2,
                               std::vector<int> &child) {
        int n = (int)p1.size();
        std::uniform_int_distribution<int> dist(0, n - 1);
        std::mt19937 gen(std::random_device{}());

        int l = dist(gen);
        int r = dist(gen);
        if (l > r) std::swap(l, r);

        std::vector<int> res(n, -1);
        for (int i = l; i <= r; ++i) {
            res[i] = p1[i];
        }

        int idx = (r + 1) % n;
        for (int i = 0; i < n; ++i) {
            int v = p2[(r + 1 + i) % n];
            if (std::find(res.begin(), res.end(), v) == res.end()) {
                res[idx] = v;
                idx = (idx + 1) % n;
            }
        }

        child.swap(res);
    }

    void Mutate(std::vector<int> &perm) {
        std::uniform_real_distribution<double> pr(0.0, 1.0);
        if (pr(rng) > mutationRate) return;

        std::uniform_int_distribution<int> di(0, (int)perm.size() - 1);
        int a = di(rng), b = di(rng);
        if (a > b) std::swap(a, b);
        if (a == b) return;

        if (pr(rng) < 0.5) {
            std::swap(perm[a], perm[b]);
        } else {
            std::reverse(perm.begin() + a, perm.begin() + b + 1);
        }
    }

    void LocalImprove(const Instance &inst, std::vector<int> &perm) {
        int n = inst.GetN();
        std::vector<int> tour;
        BuildTour(perm, tour);

        int improvements = 0;
        bool improved = true;

        while (improved && improvements < localIterPerChild) {
            improved = false;
            for (int i = 1; i < n - 1; ++i) {
                for (int k = i + 1; k < n && improvements < localIterPerChild; ++k) {
                    int a = tour[i - 1];
                    int b = tour[i];
                    int c = tour[k];
                    int d = tour[k + 1];

                    double delta =
                        inst.Distance(a, c) +
                        inst.Distance(b, d) -
                        inst.Distance(a, b) -
                        inst.Distance(c, d);

                    if (delta < -1e-9) {
                        std::reverse(tour.begin() + i, tour.begin() + k + 1);
                        improved = true;
                        ++improvements;
                    }
                }
            }
        }

        // back to perm
        perm.clear();
        for (int i = 1; i < n; ++i) {
            perm.push_back(tour[i]);
        }
    }

    static double TourLength(const Instance &inst, const std::vector<int> &perm) {
        if (perm.empty()) return 0.0;

        double len = inst.Distance(0, perm[0]);
        for (int i = 1; i < (int)perm.size(); ++i) {
            len += inst.Distance(perm[i - 1], perm[i]);
        }
        len += inst.Distance(perm.back(), 0);
        return len;
    }

    static int BestIndex(const std::vector<double> &fit) {
        int idx = 0;
        for (int i = 1; i < (int)fit.size(); ++i) {
            if (fit[i] < fit[idx]) idx = i;
        }
        return idx;
    }

    void NormalizeCycle(std::vector<int> &perm) {
        auto it = std::find(perm.begin(), perm.end(), start);
        if (it != perm.end()) {
            std::rotate(perm.begin(), it, perm.end());
        }
    }

    void BuildTour(const std::vector<int> &perm, std::vector<int> &out) {
        out.clear();
        out.reserve(perm.size() + 2);
        out.push_back(start);
        for (int v : perm) out.push_back(v);
        out.push_back(start);
    }
};

static bool reg_memetic = (SolverFactory::RegisterSolver("memetic", []() {
    return std::make_unique<Memetic>();
}), true);

} // namespace tsp
