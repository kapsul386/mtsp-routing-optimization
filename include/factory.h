#pragma once

#include "solver.h"
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tsp {

class SolverFactory {
    inline static std::unordered_map<std::string, SolverCreator> registry;

public:
    static void RegisterSolver(const std::string& name, SolverCreator creator) {
        registry[name] = std::move(creator);
    }

    static std::unique_ptr<Solver> Create(const std::string& name) {
        auto it = registry.find(name);
        if (it == registry.end()) {
            throw std::runtime_error("No such solver in factory: " + name);
        }
        return it->second();
    }

    static std::vector<std::string> GetList() {
        std::vector<std::string> out;
        out.reserve(registry.size());
        for (const auto& [name, _] : registry) {
            out.push_back(name);
        }
        return out;
    }
};

} // namespace tsp
