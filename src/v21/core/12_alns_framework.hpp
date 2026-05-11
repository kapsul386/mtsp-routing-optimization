#pragma once

// Adaptive Large Neighborhood Search framework (Ropke--Pisinger style).
// Operator registry (destroy/repair) + adaptive weights based on per-segment
// reward. Operators are registered as type-erased lambdas; the framework
// itself is agnostic to AcceptPolicy and to the concrete move semantics. The
// ALNS loop in 17_pipeline.hpp drives this: PickDestroy + PickRepair sample
// proportional to weights, RunDestroy + RunRepair execute, Reward feeds the
// weight update, EndSegment normalizes weights for the next segment.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "05_route_list.hpp"
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace mtsp::v21 {

// Result of a destroy operator: list of removed customers + bitmask of routes
// touched (for selective LS during repair-cleanup).
struct DestroyResult {
    std::vector<int> removed;
    std::vector<char> dirty_routes;  // size m
};

// Per-operator lifetime counters and current adaptive weight.
// Updated by AlnsFramework::Reward and AlnsFramework::EndSegment.
struct OperatorStats {
    int calls = 0;
    int accepts = 0;
    int best_finds = 0;     // strictly better than best-known
    double weight = 1.0;
    double score = 0.0;     // accumulated reward this segment
    int seg_calls = 0;      // calls in current segment
};

class AlnsFramework {
public:
    using DestroyFn = std::function<DestroyResult(RouteList&, std::mt19937&, int K)>;
    using RepairFn = std::function<void(RouteList&, std::vector<int>& removed, std::mt19937&)>;

    // Register a named destroy operator with initial weight `w0`.
    void RegisterDestroy(std::string name, DestroyFn fn, double w0) {
        destroy_names_.push_back(std::move(name));
        destroy_fns_.push_back(std::move(fn));
        OperatorStats s; s.weight = w0;
        destroy_stats_.push_back(s);
    }

    // Register a named repair operator with initial weight `w0`.
    void RegisterRepair(std::string name, RepairFn fn, double w0) {
        repair_names_.push_back(std::move(name));
        repair_fns_.push_back(std::move(fn));
        OperatorStats s; s.weight = w0;
        repair_stats_.push_back(s);
    }

    // Sample a destroy operator index proportional to current weights (roulette wheel).
    int PickDestroy(std::mt19937& rng) {
        return Roulette(destroy_stats_, rng);
    }
    // Sample a repair operator index proportional to current weights (roulette wheel).
    int PickRepair(std::mt19937& rng) {
        return Roulette(repair_stats_, rng);
    }

    // Execute the destroy operator at `idx`, incrementing its call counters.
    DestroyResult RunDestroy(int idx, RouteList& rl, std::mt19937& rng, int K) {
        ++destroy_stats_[static_cast<size_t>(idx)].calls;
        ++destroy_stats_[static_cast<size_t>(idx)].seg_calls;
        return destroy_fns_[static_cast<size_t>(idx)](rl, rng, K);
    }

    // Execute the repair operator at `idx`, incrementing its call counters.
    void RunRepair(int idx, RouteList& rl, std::vector<int>& removed, std::mt19937& rng) {
        ++repair_stats_[static_cast<size_t>(idx)].calls;
        ++repair_stats_[static_cast<size_t>(idx)].seg_calls;
        repair_fns_[static_cast<size_t>(idx)](rl, removed, rng);
    }

    // Reward outcome: 33 for new global best, 13 for accepted improving, 9 for
    // accepted non-improving (diversification), 0 for rejected.
    void Reward(int destroy_idx, int repair_idx, int outcome_class) {
        double r = 0.0;
        switch (outcome_class) {
            case 3: r = 33.0; ++destroy_stats_[static_cast<size_t>(destroy_idx)].best_finds;
                                ++repair_stats_[static_cast<size_t>(repair_idx)].best_finds; break;
            case 2: r = 13.0; ++destroy_stats_[static_cast<size_t>(destroy_idx)].accepts;
                                ++repair_stats_[static_cast<size_t>(repair_idx)].accepts; break;
            case 1: r = 9.0; break;
            default: r = 0.0;
        }
        destroy_stats_[static_cast<size_t>(destroy_idx)].score += r;
        repair_stats_[static_cast<size_t>(repair_idx)].score += r;
    }

    // End of segment: update weights via w_new = (1-rho) * w_old + rho * (score / calls).
    void EndSegment(double rho = 0.2) {
        for (auto& s : destroy_stats_) {
            if (s.seg_calls > 0) {
                const double pi = s.score / static_cast<double>(s.seg_calls);
                s.weight = (1.0 - rho) * s.weight + rho * pi;
                if (s.weight < 0.05) s.weight = 0.05;
            }
            s.seg_calls = 0;
            s.score = 0.0;
        }
        for (auto& s : repair_stats_) {
            if (s.seg_calls > 0) {
                const double pi = s.score / static_cast<double>(s.seg_calls);
                s.weight = (1.0 - rho) * s.weight + rho * pi;
                if (s.weight < 0.05) s.weight = 0.05;
            }
            s.seg_calls = 0;
            s.score = 0.0;
        }
    }

    // Returns the number of registered destroy operators.
    int DestroyCount() const { return static_cast<int>(destroy_fns_.size()); }
    // Returns the number of registered repair operators.
    int RepairCount() const { return static_cast<int>(repair_fns_.size()); }

    // Read-only access to per-operator statistics and names for metadata reporting.
    const std::vector<OperatorStats>& DestroyStats() const { return destroy_stats_; }
    const std::vector<OperatorStats>& RepairStats() const { return repair_stats_; }
    const std::vector<std::string>& DestroyNames() const { return destroy_names_; }
    const std::vector<std::string>& RepairNames() const { return repair_names_; }

private:
    int Roulette(const std::vector<OperatorStats>& stats, std::mt19937& rng) const {
        double total = 0.0;
        for (const auto& s : stats) total += s.weight;
        if (total <= 0.0 || stats.empty()) return 0;
        std::uniform_real_distribution<double> dist(0.0, total);
        const double pick = dist(rng);
        double acc = 0.0;
        for (size_t i = 0; i < stats.size(); ++i) {
            acc += stats[i].weight;
            if (pick <= acc) return static_cast<int>(i);
        }
        return static_cast<int>(stats.size() - 1);
    }

    std::vector<std::string> destroy_names_;
    std::vector<std::string> repair_names_;
    std::vector<DestroyFn> destroy_fns_;
    std::vector<RepairFn> repair_fns_;
    std::vector<OperatorStats> destroy_stats_;
    std::vector<OperatorStats> repair_stats_;
};

}  // namespace mtsp::v21
