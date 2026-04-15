#pragma once

#include "instance.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsp {

class Solver {
public:
    virtual void Configure(const std::unordered_map<std::string, std::string>& opts) {}
    virtual void Solve(std::vector<int>& out) = 0;
    virtual ~Solver() = default;
};

using SolverCreator = std::function<std::unique_ptr<Solver>()>;

} // namespace tsp
