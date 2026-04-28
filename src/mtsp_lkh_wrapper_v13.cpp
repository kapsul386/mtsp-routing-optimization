// mtsp_lkh_wrapper_v13.cpp
// Portfolio successor to v12. It fixes the non-monotonic budget behavior by
// always archiving the best MINSUM solution across deterministic time slices.

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace mtsp {

namespace {

using Options = std::unordered_map<std::string, std::string>;

int ReadIntOption(const Options& opts, const std::string& key, int fallback) {
    const auto it = opts.find(key);
    if (it == opts.end()) {
        return fallback;
    }
    return std::stoi(it->second);
}

unsigned int ReadSeedOption(const Options& opts, const std::string& key, unsigned int fallback) {
    const auto it = opts.find(key);
    if (it == opts.end()) {
        return fallback;
    }
    return static_cast<unsigned int>(std::stoul(it->second));
}

int RemainingMs(const std::chrono::steady_clock::time_point& start, int total_ms) {
    if (total_ms <= 0) {
        return 0;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    return std::max(0, total_ms - static_cast<int>(elapsed));
}

Options BuildV12Options(unsigned int seed,
                        int budget_ms,
                        int reserve_ms,
                        int local_candidates,
                        int global_candidates,
                        int popmusic_solutions,
                        int popmusic_sample,
                        int popmusic_window,
                        int savings_candidate_count,
                        int savings_seed_ms,
                        int route_repair_ms,
                        int route_repair_ruin,
                        int anneal_route_ms,
                        int minsum_relocate_passes,
                        double route_size_slack,
                        double savings_route_slack,
                        double savings_lambda) {
    return {
        {"seed", std::to_string(seed)},
        {"time-budget-ms", std::to_string(std::max(1, budget_ms))},
        {"reserve-budget-ms", std::to_string(std::max(0, reserve_ms))},
        {"local-candidate-count", std::to_string(local_candidates)},
        {"global-candidate-count", std::to_string(global_candidates)},
        {"popmusic-solutions", std::to_string(popmusic_solutions)},
        {"popmusic-sample-size", std::to_string(popmusic_sample)},
        {"popmusic-window", std::to_string(popmusic_window)},
        {"savings-candidate-count", std::to_string(savings_candidate_count)},
        {"savings-seed-ms", std::to_string(std::max(0, savings_seed_ms))},
        {"route-repair-ms", std::to_string(std::max(0, route_repair_ms))},
        {"route-repair-ruin", std::to_string(std::max(0, route_repair_ruin))},
        {"anneal-route-ms", std::to_string(std::max(0, anneal_route_ms))},
        {"minsum-relocate-passes", std::to_string(std::max(0, minsum_relocate_passes))},
        {"route-size-slack", std::to_string(route_size_slack)},
        {"savings-route-slack", std::to_string(savings_route_slack)},
        {"savings-lambda", std::to_string(savings_lambda)},
        {"lookahead-weight", "0.35"},
        {"depot-weight", "0.12"},
    };
}

struct StagePlan {
    std::string name;
    Options options;
    int requested_budget_ms = 0;
};

std::string JoinStageNames(const std::vector<std::string>& names) {
    std::ostringstream out;
    for (size_t idx = 0; idx < names.size(); ++idx) {
        if (idx > 0) {
            out << ",";
        }
        out << names[idx];
    }
    return out.str();
}

class LkhWrapperSolverv13 : public Solver {
public:
    void Configure(const Options& opts) override {
        seed_ = ReadSeedOption(opts, "seed", seed_);
        time_budget_ms_ = ReadIntOption(opts, "time-budget-ms", time_budget_ms_);
        reserve_budget_ms_ = ReadIntOption(opts, "reserve-budget-ms", reserve_budget_ms_);
        min_stage_ms_ = ReadIntOption(opts, "min-stage-ms", min_stage_ms_);
        stage_reserve_ms_ = ReadIntOption(opts, "stage-reserve-ms", stage_reserve_ms_);
        if (opts.count("large-mode")) {
            large_mode_ = opts.at("large-mode") != "false" && opts.at("large-mode") != "0";
        }
    }

    std::string GetLastStatus() const override {
        return last_status_;
    }

    std::string GetLastMessage() const override {
        return last_message_;
    }

    std::unordered_map<std::string, std::string> GetLastMetadata() const override {
        return last_metadata_;
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        last_status_ = "ok";
        last_message_.clear();
        last_metadata_.clear();

        const int node_count = inst.GetNodeCount();
        const int effective_total_ms = std::max(1, time_budget_ms_ - std::max(0, reserve_budget_ms_));
        const auto started = std::chrono::steady_clock::now();
        std::vector<StagePlan> stages = BuildStages(node_count, effective_total_ms);

        RouteSet best_routes;
        double best_objective = 0.0;
        std::vector<std::string> completed_stages;
        int valid_stage_count = 0;

        for (size_t idx = 0; idx < stages.size(); ++idx) {
            const int remaining = RemainingMs(started, effective_total_ms);
            if (remaining < min_stage_ms_ && !best_routes.empty()) {
                break;
            }

            StagePlan& stage = stages[idx];
            const int stage_budget = std::max(1, std::min(stage.requested_budget_ms, std::max(1, remaining)));
            stage.options["time-budget-ms"] = std::to_string(stage_budget);
            stage.options["reserve-budget-ms"] = std::to_string(std::min(stage_reserve_ms_, std::max(0, stage_budget / 12)));

            RouteSet candidate;
            auto solver = SolverFactory::Create("lkh-wrapper-v12");
            solver->Configure(stage.options);
            const auto stage_started = std::chrono::steady_clock::now();
            solver->Solve(candidate);
            const auto stage_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stage_started).count();

            last_metadata_["stage_" + std::to_string(idx + 1) + "_name"] = stage.name;
            last_metadata_["stage_" + std::to_string(idx + 1) + "_budget_ms"] = std::to_string(stage_budget);
            last_metadata_["stage_" + std::to_string(idx + 1) + "_elapsed_ms"] = std::to_string(stage_elapsed_ms);
            last_metadata_["stage_" + std::to_string(idx + 1) + "_status"] = solver->GetLastStatus();

            if (solver->GetLastStatus() != "ok" || !ValidateRoutes(candidate)) {
                const std::string message = solver->GetLastMessage();
                if (!message.empty()) {
                    last_metadata_["stage_" + std::to_string(idx + 1) + "_message"] = message;
                }
                continue;
            }

            const double objective = ObjectiveMinsum(candidate);
            last_metadata_["stage_" + std::to_string(idx + 1) + "_objective"] = std::to_string(objective);
            ++valid_stage_count;
            completed_stages.push_back(stage.name);
            if (best_routes.empty() || objective + 1e-9 < best_objective) {
                best_routes.swap(candidate);
                best_objective = objective;
                last_metadata_["best_stage"] = stage.name;
                last_metadata_["best_objective"] = std::to_string(best_objective);
            }
        }

        if (best_routes.empty()) {
            last_status_ = "no_valid_stage";
            last_message_ = "v13 portfolio did not produce a valid v12 stage.";
            out.clear();
            return;
        }

        last_metadata_["valid_stage_count"] = std::to_string(valid_stage_count);
        last_metadata_["completed_stages"] = JoinStageNames(completed_stages);
        last_metadata_["effective_total_ms"] = std::to_string(effective_total_ms);
        out.swap(best_routes);
    }

private:
    std::vector<StagePlan> BuildStages(int node_count, int total_ms) const {
        std::vector<StagePlan> stages;
        const bool huge = large_mode_ || node_count >= 100000;
        const bool large = huge || node_count >= 50000;

        if (huge) {
            const int first = std::min(90000, std::max(45000, total_ms / 4));
            const int second = std::min(120000, std::max(60000, total_ms / 3));
            const int third = std::max(1, total_ms - first - second);
            stages.push_back({
                "filo2_savings_large",
                BuildV12Options(seed_, first, stage_reserve_ms_, 6, 12, 4, 48, 40, 64,
                                std::max(25000, first * 2 / 3), std::max(8000, first / 5),
                                192, 0, 18, 0.80, 0.05, 1.0),
                first,
            });
            stages.push_back({
                "popmusic_lkh_large",
                BuildV12Options(seed_ ^ 0x9E3779B9U, second, stage_reserve_ms_, 8, 14, 8, 56, 44, 48,
                                std::max(15000, second / 3), std::max(12000, second / 4),
                                192, 0, 28, 0.30, 0.08, 0.95),
                second,
            });
            if (third >= min_stage_ms_) {
                stages.push_back({
                    "v12_balanced_large",
                    BuildV12Options(seed_ ^ 0x85EBCA6BU, third, stage_reserve_ms_, 7, 13, 6, 48, 40, 56,
                                    std::max(12000, third / 3), std::max(10000, third / 4),
                                    160, 0, 24, 0.45, 0.06, 1.0),
                    third,
                });
            }
            return stages;
        }

        if (large) {
            const int first = std::min(60000, std::max(30000, total_ms / 3));
            const int second = std::min(90000, std::max(30000, total_ms / 3));
            const int third = std::max(1, total_ms - first - second);
            stages.push_back({
                "filo2_savings_medium",
                BuildV12Options(seed_, first, stage_reserve_ms_, 7, 13, 6, 40, 36, 64,
                                std::max(18000, first * 2 / 3), std::max(5000, first / 5),
                                144, 0, 20, 0.60, 0.05, 1.0),
                first,
            });
            stages.push_back({
                "popmusic_lkh_medium",
                BuildV12Options(seed_ ^ 0x9E3779B9U, second, stage_reserve_ms_, 8, 14, 10, 44, 40, 48,
                                std::max(12000, second / 3), std::max(8000, second / 4),
                                144, 0, 28, 0.25, 0.08, 0.95),
                second,
            });
            if (third >= min_stage_ms_) {
                stages.push_back({
                    "v12_balanced_medium",
                    BuildV12Options(seed_ ^ 0x85EBCA6BU, third, stage_reserve_ms_, 8, 14, 8, 40, 36, 56,
                                    std::max(10000, third / 3), std::max(7000, third / 4),
                                    128, 0, 24, 0.35, 0.06, 1.0),
                    third,
                });
            }
            return stages;
        }

        const int first = std::min(28500, std::max(1, total_ms));
        const int second = std::max(1, total_ms - first);
        stages.push_back({
            "filo2_savings_short",
            BuildV12Options(seed_, first, stage_reserve_ms_, 8, 14, 8, 32, 32, 64,
                            std::max(7000, first * 2 / 3), std::max(2000, first / 5),
                            96, 0, 24, 0.45, 0.05, 1.0),
            first,
        });
        if (second >= min_stage_ms_) {
            stages.push_back({
                "popmusic_lkh_short",
                BuildV12Options(seed_ ^ 0x9E3779B9U, second, stage_reserve_ms_, 8, 14, 12, 36, 36, 48,
                                std::max(5000, second / 3), std::max(2000, second / 4),
                                96, 1000, 28, 0.25, 0.08, 0.95),
                second,
            });
        }
        return stages;
    }

    unsigned int seed_ = 42U;
    int time_budget_ms_ = 300000;
    int reserve_budget_ms_ = 10000;
    int min_stage_ms_ = 8000;
    int stage_reserve_ms_ = 3000;
    bool large_mode_ = false;
    std::string last_status_ = "ok";
    std::string last_message_;
    std::unordered_map<std::string, std::string> last_metadata_;
};

} // namespace

static bool reg_lkh_mtsp_v13 = (SolverFactory::RegisterSolver("lkh-wrapper-v13", []() {
    return std::make_unique<LkhWrapperSolverv13>();
}),
                                true);

} // namespace mtsp
