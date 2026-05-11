// mtsp_lkh_wrapper_v8.cpp
// Thin entry point for the modular lkh-wrapper-v8 implementation.
// This is the architectural predecessor of the flagship ALNS-mTSP core (src/v21/core/):
// the 00..60 file-numbering scheme used here was later reproduced in the 22-module v21 structure.
// The implementation is split across 7 submodules in src/legacy/lkh_wrapper_v8/:
//   00_common.cpp           — shared infrastructure (budget, distances, route index, grid);
//   10_candidate_sets.cpp   — candidate-set construction (geometric + POPMUSIC);
//   20_route_local_search.cpp — 2-opt, kicks, ILS, completion logic;
//   30_cluster_model.cpp    — lightweight clustering for the seed phase;
//   40_seed_routes.cpp      — initial route construction accounting for clusters;
//   50_rebalance.cpp        — cluster block rebalancing;
//   60_solver.cpp           — phase orchestration and SolverFactory registration.
// Files are included in dependency order (some functions are templated).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>


// Inclusion order matters: several functions below are templated, so their definitions
// must be visible in the translation unit before use.
#include "lkh_wrapper_v8/00_common.cpp"
#include "lkh_wrapper_v8/10_candidate_sets.cpp"
#include "lkh_wrapper_v8/20_route_local_search.cpp"
#include "lkh_wrapper_v8/30_cluster_model.cpp"
#include "lkh_wrapper_v8/40_seed_routes.cpp"
#include "lkh_wrapper_v8/50_rebalance.cpp"
#include "lkh_wrapper_v8/60_solver.cpp"
