#pragma once

#include "mtsp_solver.h"
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mtsp {

class SolverFactory {
    inline static std::unordered_map<std::string, SolverCreator> registry_;

public:
    static void RegisterSolver(const std::string& name, SolverCreator creator) {
        registry_[name] = std::move(creator);
    }

    static std::unique_ptr<Solver> Create(const std::string& name) {
        auto it = registry_.find(name);
        if (it == registry_.end()) {
            throw std::runtime_error("No such mTSP solver in factory: " + name);
        }
        return it->second();
    }

    static std::vector<std::string> GetList() {
        std::vector<std::string> out;
        out.reserve(registry_.size());
        for (const auto& [name, _] : registry_) {
            out.push_back(name);
        }
        return out;
    }
};

} // namespace mtsp
