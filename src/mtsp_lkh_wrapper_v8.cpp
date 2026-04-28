// mtsp_lkh_wrapper_v8.cpp
// Entry point for the modular lkh-wrapper-v8 implementation.
// The implementation is split into ordered fragments under src/lkh_wrapper_v8/.

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


// The fragments below are included in this order because several hot-path helpers are templated.
#include "lkh_wrapper_v8/00_common.cpp"
#include "lkh_wrapper_v8/10_candidate_sets.cpp"
#include "lkh_wrapper_v8/20_route_local_search.cpp"
#include "lkh_wrapper_v8/30_cluster_model.cpp"
#include "lkh_wrapper_v8/40_seed_routes.cpp"
#include "lkh_wrapper_v8/50_rebalance.cpp"
#include "lkh_wrapper_v8/60_solver.cpp"
