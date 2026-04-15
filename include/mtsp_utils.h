#pragma once

#include "mtsp_solver.h"

namespace mtsp {

double RouteLength(const std::vector<int>& route);
double ObjectiveMinsum(const RouteSet& routes);
std::vector<int> BuildNearestOrder(const std::vector<int>& assigned);
void ImproveRoute2Opt(std::vector<int>& route);
bool ValidateRoutes(const RouteSet& routes);

} // namespace mtsp
