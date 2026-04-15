#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/json.hpp"
#include <mtsp_factory.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

inline std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>>
ParseMtspArguments(int argc, char** argv) {
    std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>> steps;
    std::unordered_map<std::string, std::string> args;
    std::string solver_name;

    for (int i = 1; i < argc - 1; i += 2) {
        std::string name = argv[i];
        std::string val = argv[i + 1];
        if (name.size() < 3 || name[0] != '-' || name[1] != '-') {
            throw std::runtime_error("Unexpected argument");
        }

        name = name.substr(2);
        if (name == "step") {
            if (!solver_name.empty()) {
                steps.emplace_back(solver_name, args);
                args.clear();
            }
            solver_name = val;
        } else {
            args[name] = val;
        }
    }

    if (!solver_name.empty()) {
        steps.emplace_back(solver_name, args);
    }

    return steps;
}

int main(int argc, char** argv) {
    auto steps = ParseMtspArguments(argc, argv);

    std::vector<std::unique_ptr<mtsp::Solver>> solvers;
    std::vector<std::string> solver_names;
    for (const auto& [name, args] : steps) {
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
        steps_json.push_back(step_json);
    }
    auto total_stop = std::chrono::high_resolution_clock::now();

    double real_time = static_cast<double>(
                           std::chrono::duration_cast<std::chrono::microseconds>(total_stop - total_start).count()) /
                       1e6;

    nlohmann::json output;
    output["routes"] = routes;
    output["time"] = real_time;
    output["objective"] = mtsp::ObjectiveMinsum(routes);
    output["valid"] = mtsp::ValidateRoutes(routes);
    output["steps"] = steps_json;
    std::cout << output.dump();
    return 0;
}
