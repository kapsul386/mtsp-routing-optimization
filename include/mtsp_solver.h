#pragma once

#include "mtsp_instance.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mtsp {

using RouteSet = std::vector<std::vector<int>>;

class Solver {
public:
    virtual void Configure(const std::unordered_map<std::string, std::string>& opts) {}
    virtual void Solve(RouteSet& out) = 0;
    virtual ~Solver() = default;
};

using SolverCreator = std::function<std::unique_ptr<Solver>()>;

} // namespace mtsp
