#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/json.hpp"
#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

// CLI parameters for the main mTSP harness.
// emit_routes=true — the final JSON includes the routes themselves (disabling is needed
// for large instances where the route data can weigh several megabytes).
struct MtspCliOptions {
    std::string input_file;
    bool emit_routes = true;
    std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>> steps;
};

// Boolean CLI flag parser: accepts "1"/"true"/"yes"/"on" as true.
inline bool ParseBoolFlag(const std::string& value) {
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

// CLI argument parser for --key value pairs for the main mTSP harness.
// Supports a chain of steps via --step <solver>; --emit-routes controls
// whether routes are included in the JSON output.
inline MtspCliOptions ParseMtspArguments(int argc, char** argv) {
    if ((argc - 1) % 2 != 0) {
        throw std::runtime_error("Arguments must be passed as --key value pairs.");
    }

    MtspCliOptions options;
    std::unordered_map<std::string, std::string> args;
    std::string solver_name;

    for (int i = 1; i < argc - 1; i += 2) {
        std::string name = argv[i];
        std::string val = argv[i + 1];
        if (name.size() < 3 || name[0] != '-' || name[1] != '-') {
            throw std::runtime_error("Unexpected argument format. Expected --key value pairs.");
        }

        name = name.substr(2);
        if (name == "input-file") {
            if (val.empty()) {
                throw std::runtime_error("Path after --input-file must not be empty.");
            }
            options.input_file = val;
            continue;
        }
        if (name == "emit-routes") {
            options.emit_routes = ParseBoolFlag(val);
            continue;
        }
        if (name == "step") {
            if (val.empty()) {
                throw std::runtime_error("Solver name after --step must not be empty.");
            }
            if (!solver_name.empty()) {
                options.steps.emplace_back(solver_name, args);
                args.clear();
            }
            solver_name = val;
        } else {
            if (solver_name.empty()) {
                throw std::runtime_error("Solver options must come after --step <solver>.");
            }
            args[name] = val;
        }
    }

    if (!solver_name.empty()) {
        options.steps.emplace_back(solver_name, args);
    }
    if (options.steps.empty()) {
        throw std::runtime_error("At least one --step <solver> must be provided.");
    }

    return options;
}

// Entry point for the main mTSP harness (mtsp.exe).
// Reads arguments, loads the instance, applies the solver chain in order,
// measures the time of each step, and outputs the final JSON with all metadata.
int main(int argc, char** argv) {
    try {
        auto options = ParseMtspArguments(argc, argv);
        mtsp::Instance::Reset();
        if (!options.input_file.empty()) {
            mtsp::Instance::LoadFromFile(options.input_file);
        }

        // Create solvers by name via SolverFactory; options are passed to Configure.
        std::vector<std::unique_ptr<mtsp::Solver>> solvers;
        std::vector<std::string> solver_names;
        for (const auto& [name, args] : options.steps) {
            solvers.push_back(mtsp::SolverFactory::Create(name));
            solvers.back()->Configure(args);
            solver_names.push_back(name);
        }

        mtsp::RouteSet routes;
        nlohmann::json steps_json = nlohmann::json::array();
        auto total_start = std::chrono::high_resolution_clock::now();
        for (size_t idx = 0; idx < solvers.size(); ++idx) {
            auto step_start = std::chrono::high_resolution_clock::now();
            solvers[idx]->Solve(routes);
            auto step_stop = std::chrono::high_resolution_clock::now();

            double step_time = static_cast<double>(
                                   std::chrono::duration_cast<std::chrono::microseconds>(step_stop - step_start).count()) /
                               1e6;
            nlohmann::json step_json;
            step_json["name"] = solver_names[idx];
            step_json["time"] = step_time;
            step_json["objective"] = mtsp::ObjectiveMinsum(routes);
            step_json["valid"] = mtsp::ValidateRoutes(routes);
            step_json["status"] = solvers[idx]->GetLastStatus();
            if (!solvers[idx]->GetLastMessage().empty()) {
                step_json["message"] = solvers[idx]->GetLastMessage();
            }
            const auto metadata = solvers[idx]->GetLastMetadata();
            if (!metadata.empty()) {
                step_json["metadata"] = metadata;
            }
            steps_json.push_back(step_json);
        }
        auto total_stop = std::chrono::high_resolution_clock::now();

        double real_time = static_cast<double>(
                               std::chrono::duration_cast<std::chrono::microseconds>(total_stop - total_start).count()) /
                           1e6;

        nlohmann::json output;
        if (options.emit_routes) {
            output["routes"] = routes;
        } else {
            output["routes_omitted"] = true;
        }
        output["time"] = real_time;
        output["objective"] = mtsp::ObjectiveMinsum(routes);
        output["valid"] = mtsp::ValidateRoutes(routes);
        output["steps"] = steps_json;
        output["status"] = solvers.empty() ? "ok" : solvers.back()->GetLastStatus();
        if (!solvers.empty() && !solvers.back()->GetLastMessage().empty()) {
            output["message"] = solvers.back()->GetLastMessage();
        }
        if (!solvers.empty()) {
            const auto metadata = solvers.back()->GetLastMetadata();
            if (!metadata.empty()) {
                output["metadata"] = metadata;
            }
        }
        std::cout << output.dump();
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << exc.what();
        return 1;
    }
}
