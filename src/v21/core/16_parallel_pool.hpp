#pragma once

// Multi-start coordinator used by the Parallel Tempering branch in 17_pipeline.
// Owns a pool of independent ALNS+SA replicas; the pipeline runs them in
// parallel via OpenMP and periodically attempts Metropolis swaps between
// adjacent replicas on the geometric T ladder. Single-replica mode is the
// fast path (zero PT overhead); PT is enabled only on the medium-n tier
// where the cost of ensemble swaps amortizes (see 18_autotune.hpp).

#include "00_types.hpp"
#include <mtsp_solver.h>
#include <algorithm>
#include <random>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mtsp::v21 {

// Lightweight multi-start coordinator. Each replica gets its own seed and
// works independently on a copy of the seed RouteSet. After all complete, we
// pick the best by user-provided cost function. Parallel Tempering hooks can
// be added later (currently just multi-start).
//
// This is intentionally simple — for n=100k, the per-replica memory cost is
// significant (each holds RouteList + DistanceOracle cache), so we keep
// num_replicas low.
struct ReplicaResult {
    RouteSet routes;
    double cost = 0.0;
    bool ok = false;
};

template <typename WorkerFn>
inline std::vector<ReplicaResult> RunReplicas(int num_replicas, unsigned base_seed, WorkerFn worker) {
    std::vector<ReplicaResult> results(static_cast<size_t>(num_replicas));
    if (num_replicas <= 1) {
        results[0] = worker(0, base_seed);
        return results;
    }
#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic) num_threads(num_replicas)
    for (int r = 0; r < num_replicas; ++r) {
        results[static_cast<size_t>(r)] = worker(r, base_seed + static_cast<unsigned>(r) * 1009u);
    }
#else
    for (int r = 0; r < num_replicas; ++r) {
        results[static_cast<size_t>(r)] = worker(r, base_seed + static_cast<unsigned>(r) * 1009u);
    }
#endif
    return results;
}

inline ReplicaResult PickBestReplica(const std::vector<ReplicaResult>& results) {
    ReplicaResult best;
    best.cost = std::numeric_limits<double>::max();
    for (const auto& r : results) {
        if (r.ok && r.cost < best.cost) best = r;
    }
    return best;
}

}  // namespace mtsp::v21
