// mtsp_lkh_wrapper_v9.cpp — entry point for v9 (inside the subdirectory).
// Includes all v9 60-blocks in dependency order, the same way as v8.

// mtsp_lkh_wrapper_v9.cpp
// Two-phase MTSP/LKH wrapper: first good solution within ~200s, then improvement within ~100s.
// The module files are included in order to match the lkh-wrapper-v8 project layout.

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

#include "00_common.cpp"
#include "../lkh_wrapper_v8/10_candidate_sets.cpp"
#include "20_route_local_search.cpp"
#include "30_cluster_model.cpp"
#include "40_seed_routes.cpp"
#include "50_rebalance.cpp"
#include "60_solver.cpp"
