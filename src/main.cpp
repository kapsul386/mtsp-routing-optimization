#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/json.hpp"
#include <factory.h>
#include <instance.h>
#include <solver.h>

struct TspCliOptions {
    std::string input_file;
    std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>> steps;
};

inline TspCliOptions ParseArguments(int argc, char** argv) {
    if ((argc - 1) % 2 != 0) {
        throw std::runtime_error("Arguments must be passed as --key value pairs.");
    }

    TspCliOptions options;
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

inline double CalculateRouteLength(const std::vector<int>& route) {
    const tsp::Instance& inst = tsp::Instance::GetInstance();
    double len = 0.0;
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        len += inst.Distance(route[i], route[i + 1]);
    }
    return len;
}

int main(int argc, char** argv) {
    try {
        auto options = ParseArguments(argc, argv);
        tsp::Instance::Reset();
        if (!options.input_file.empty()) {
            tsp::Instance::LoadFromFile(options.input_file);
        }

        std::vector<std::unique_ptr<tsp::Solver>> solvers;
        std::vector<std::string> solver_names;
        for (const auto& [name, args] : options.steps) {
            solvers.push_back(tsp::SolverFactory::Create(name));
            solvers.back()->Configure(args);
            solver_names.push_back(name);
        }

        const tsp::Instance& inst = tsp::Instance::GetInstance();
        std::vector<int> solution(inst.GetN() + 1);
        std::iota(solution.begin(), solution.end(), 0);
        solution.back() = 0;

        nlohmann::json steps_json = nlohmann::json::array();
        auto total_start = std::chrono::high_resolution_clock::now();
        for (size_t idx = 0; idx < solvers.size(); ++idx) {
            auto step_start = std::chrono::high_resolution_clock::now();
            solvers[idx]->Solve(solution);
            auto step_stop = std::chrono::high_resolution_clock::now();

            double step_time = static_cast<double>(
                                   std::chrono::duration_cast<std::chrono::microseconds>(step_stop - step_start).count()) /
                               1e6;
            nlohmann::json step_json;
            step_json["name"] = solver_names[idx];
            step_json["time"] = step_time;
            step_json["len"] = CalculateRouteLength(solution);
            steps_json.push_back(step_json);
        }
        auto total_stop = std::chrono::high_resolution_clock::now();

        double real_time = static_cast<double>(
                               std::chrono::duration_cast<std::chrono::microseconds>(total_stop - total_start).count()) /
                           1e6;

        nlohmann::json output;
        output["route"] = solution;
        output["time"] = real_time;
        output["len"] = CalculateRouteLength(solution);
        output["steps"] = steps_json;
        std::cout << output.dump();
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << exc.what();
        return 1;
    }
}
