// v4: the first "big-instance" version. Adds:
//   - geometric candidate sets via a uniform spatial grid;
//   - on-demand distance cache (distances computed on demand, no full matrix stored);
//   - an explicit time budget surface (instead of an unconditional ILS pass).
// For n >= 10K it returns valid solutions in seconds where v1--v3 either exceeded
// the time limit or ran out of memory. MINSUM quality is lower than v2/v3, but this is
// the only viable path to large n at this stage. Registered as "lkh-wrapper-v4".

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mtsp_factory.h>
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <mtsp_utils.h>

namespace {

using CandidateSets = std::vector<std::vector<int>>;
using Coord = std::pair<double, double>;

constexpr double kEps = 1e-9;
constexpr double kCoordEps = 1e-12;
constexpr long long kLargeInstanceDistancePairs = 4'000'000LL;

// Time budget for v4. Takes time_budget_ms; a value <= 0 disables the limit.
// ShouldStop() uses an amortised check (once every 256 calls), making it cheap
// to call in hot loops. ForceCheck() performs an unconditional deadline check.
class SearchBudgetV4 {
public:
    explicit SearchBudgetV4(int time_budget_ms)
        : enabled_(time_budget_ms > 0),
          deadline_(enabled_ ? std::chrono::steady_clock::now() + std::chrono::milliseconds(time_budget_ms)
                             : std::chrono::steady_clock::time_point::max()) {}

    // Cheap check for inner loops: one real clock call every 256 iterations.
    bool ShouldStop() {
        if (!enabled_) {
            return false;
        }
        if (stop_requested_) {
            return true;
        }
        if (--polls_until_check_ > 0) {
            return false;
        }
        polls_until_check_ = 256;
        stop_requested_ = std::chrono::steady_clock::now() >= deadline_;
        return stop_requested_;
    }

    // Unconditional clock call. Used at phase boundaries where one clock access
    // is negligible compared to the cost of the phase itself.
    bool ForceCheck() {
        if (!enabled_) {
            return false;
        }
        stop_requested_ = std::chrono::steady_clock::now() >= deadline_;
        return stop_requested_;
    }

private:
    bool enabled_ = false;
    std::chrono::steady_clock::time_point deadline_{};
    int polls_until_check_ = 256;
    bool stop_requested_ = false;
};

// On-demand distance oracle: for small n (n^2 <= 4M) delegates to Instance;
// for large n, caches actually requested pairs in an unordered_map<uint64, double>.
// Key is a packed uint64 from the index pair; PackPair ensures order-independence.
class DistanceOracleV4 {
public:
    // Constructor: automatically selects mode (cache vs raw) based on instance size.
    // Reserves cache capacity for ~16 pairs per vertex (typical density for k-NN access).
    explicit DistanceOracleV4(const mtsp::Instance& inst)
        : inst_(inst),
          coords_(inst.GetCoords()),
          cache_enabled_(static_cast<long long>(inst.GetNodeCount()) * static_cast<long long>(inst.GetNodeCount()) >
                         kLargeInstanceDistancePairs) {
        if (cache_enabled_) {
            cache_.reserve(std::max<size_t>(1024, coords_.size() * 16ULL));
        }
    }

    // Main operator(): cache-hit O(1); cache-miss computes sqrt and caches the result.
    double operator()(int a, int b) {
        if (!cache_enabled_) {
            return inst_.Distance(a, b);
        }
        if (a > b) {
            std::swap(a, b);
        }

        const uint64_t key = PackPair(a, b);
        const auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }

        const double value = std::sqrt(SquaredDistance(a, b));
        cache_.emplace(key, value);
        return value;
    }

    // Squared distance — avoids sqrt where only relative distance comparisons are needed.
    [[nodiscard]] double SquaredDistance(int a, int b) const {
        const double dx = coords_[static_cast<size_t>(a)].first - coords_[static_cast<size_t>(b)].first;
        const double dy = coords_[static_cast<size_t>(a)].second - coords_[static_cast<size_t>(b)].second;
        return dx * dx + dy * dy;
    }

    [[nodiscard]] const std::vector<Coord>& GetCoords() const {
        return coords_;
    }

private:
    static uint64_t PackPair(int a, int b) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32U) |
               static_cast<uint32_t>(b);
    }

    const mtsp::Instance& inst_;
    const std::vector<Coord>& coords_;
    bool cache_enabled_ = false;
    std::unordered_map<uint64_t, double> cache_;
};

// Uniform spatial grid for fast candidate lookup.
// Each cell is keyed by a 64-bit packed (cell_x, cell_y); buckets stores vertex indices per cell.
// This is the key difference from v1--v3: instead of an O(n^2) pairwise scan during
// candidate-list construction, only nearby cells are queried in an O(n) pass.
struct SpatialGridV4 {
    double min_x = 0.0;
    double min_y = 0.0;
    double cell_size = 1.0;
    int width = 1;
    int height = 1;
    std::unordered_map<uint64_t, std::vector<int>> buckets;

    // Cell index for coordinate X (clamped to [0, width-1]).
    [[nodiscard]] int CellX(double x) const {
        if (width <= 1) {
            return 0;
        }
        const int idx = static_cast<int>((x - min_x) / cell_size);
        return std::clamp(idx, 0, width - 1);
    }

    // Cell index for coordinate Y (clamped to [0, height-1]).
    [[nodiscard]] int CellY(double y) const {
        if (height <= 1) {
            return 0;
        }
        const int idx = static_cast<int>((y - min_y) / cell_size);
        return std::clamp(idx, 0, height - 1);
    }

    // Returns the vertex list for the given cell, or nullptr if the cell is empty.
    [[nodiscard]] const std::vector<int>* Find(int cell_x, int cell_y) const {
        const auto it = buckets.find(PackKey(cell_x, cell_y));
        return it == buckets.end() ? nullptr : &it->second;
    }

    static uint64_t PackKey(int cell_x, int cell_y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cell_x)) << 32U) |
               static_cast<uint32_t>(cell_y);
    }
};

// Squared distance between two coordinate pairs (no sqrt). Used in grid-based nearest-neighbor
// search: comparing squared distances is sufficient for ranking purposes.
double SquaredDistanceCoordsV4(const Coord& lhs, const Coord& rhs) {
    const double dx = lhs.first - rhs.first;
    const double dy = lhs.second - rhs.second;
    return dx * dx + dy * dy;
}

// Sum of route edge lengths (templated on the distance functor, as in v3).
template <typename DistanceFn>
double RouteLengthGenericV4(const std::vector<int>& route, DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

bool IsInCandidateSet(int from, int to, const CandidateSets& candidate_sets) {
    const auto& cand = candidate_sets[static_cast<size_t>(from)];
    return std::find(cand.begin(), cand.end(), to) != cand.end();
}

bool IsPromisingSwapPair(int city_a, int city_b, const CandidateSets& candidate_sets) {
    return IsInCandidateSet(city_a, city_b, candidate_sets) ||
           IsInCandidateSet(city_b, city_a, candidate_sets);
}

template <typename DistanceFn>
double SwapDeltaClosedRoutes(const std::vector<int>& route_a,
                             size_t idx_a,
                             const std::vector<int>& route_b,
                             size_t idx_b,
                             DistanceFn& distance) {
    const int prev_a = route_a[idx_a - 1];
    const int city_a = route_a[idx_a];
    const int next_a = route_a[idx_a + 1];

    const int prev_b = route_b[idx_b - 1];
    const int city_b = route_b[idx_b];
    const int next_b = route_b[idx_b + 1];

    const double before_a = distance(prev_a, city_a) + distance(city_a, next_a);
    const double after_a = distance(prev_a, city_b) + distance(city_b, next_a);

    const double before_b = distance(prev_b, city_b) + distance(city_b, next_b);
    const double after_b = distance(prev_b, city_a) + distance(city_a, next_b);

    return (after_a - before_a) + (after_b - before_b);
}

// Trims a vector of (distance, index) pairs to the `limit` nearest.
// Uses nth_element + sort: O(n) average, O(limit log limit) for the sorted tail.
template <typename DistanceValue>
void TrimNearestCandidatesV4(std::vector<std::pair<DistanceValue, int>>& nearest, size_t limit) {
    if (nearest.empty() || limit == 0) {
        nearest.clear();
        return;
    }

    const auto cmp = [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first || (lhs.first == rhs.first && lhs.second < rhs.second);
    };

    if (nearest.size() > limit) {
        std::nth_element(nearest.begin(), nearest.begin() + static_cast<std::ptrdiff_t>(limit), nearest.end(), cmp);
        nearest.resize(limit);
    }
    std::sort(nearest.begin(), nearest.end(), cmp);
}

// Attempts to build a spatial grid. Returns false if the instance is too small
// (n <= candidate_count + 1), in which case the O(n^2) fallback is used.
// Otherwise computes the bounding box, selects cell_size so that each cell holds
// ~candidate_count points on average, and assigns all vertices to cells.
bool TryBuildSpatialGridV4(const std::vector<Coord>& coords, int candidate_count, SpatialGridV4& out_grid) {
    if (coords.size() <= static_cast<size_t>(candidate_count + 1)) {
        return false;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();

    for (const auto& [x, y] : coords) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }

    const double range_x = max_x - min_x;
    const double range_y = max_y - min_y;
    if (range_x <= kCoordEps || range_y <= kCoordEps) {
        return false;
    }

    const double area = range_x * range_y;
    const double target_points_per_cell = std::max(8.0, static_cast<double>(candidate_count) * 2.0);
    const double target_cell_area = std::max(kCoordEps, area * target_points_per_cell / coords.size());
    const double cell_size = std::sqrt(target_cell_area);
    if (!(cell_size > kCoordEps)) {
        return false;
    }

    out_grid.min_x = min_x;
    out_grid.min_y = min_y;
    out_grid.cell_size = cell_size;
    out_grid.width = std::max(1, static_cast<int>(std::floor(range_x / cell_size)) + 1);
    out_grid.height = std::max(1, static_cast<int>(std::floor(range_y / cell_size)) + 1);
    out_grid.buckets.clear();
    out_grid.buckets.reserve(coords.size() * 2ULL);

    for (int node = 0; node < static_cast<int>(coords.size()); ++node) {
        const int cell_x = out_grid.CellX(coords[static_cast<size_t>(node)].first);
        const int cell_y = out_grid.CellY(coords[static_cast<size_t>(node)].second);
        out_grid.buckets[SpatialGridV4::PackKey(cell_x, cell_y)].push_back(node);
    }

    return true;
}

// Exact fallback candidate-list search: O(n) per vertex via a full pairwise scan.
// Used when the spatial grid was not built (small instances) or as a back-up.
std::vector<int> CollectNearestCandidatesExactV4(int node,
                                                 int candidate_count,
                                                 const std::vector<Coord>& coords) {
    std::vector<std::pair<double, int>> nearest;
    nearest.reserve(coords.size() - 1);

    for (int other = 0; other < static_cast<int>(coords.size()); ++other) {
        if (other == node) {
            continue;
        }
        nearest.emplace_back(SquaredDistanceCoordsV4(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(other)]),
                             other);
    }

    TrimNearestCandidatesV4(nearest, static_cast<size_t>(candidate_count));

    std::vector<int> result;
    result.reserve(nearest.size());
    for (const auto& [_, city] : nearest) {
        result.push_back(city);
    }
    return result;
}

// Nearest-neighbor search via the spatial grid.
// Expanding radius: starting from the vertex's own cell, cells at distance 0, 1, 2, ...
// are scanned until target_pool candidates are collected.
// This is the key speedup in v4: for uniform instances one query costs O(k)
// compared to O(n) for the exact variant.
std::vector<int> CollectNearestCandidatesGridV4(int node,
                                                int candidate_count,
                                                const SpatialGridV4& grid,
                                                const std::vector<Coord>& coords) {
    std::vector<std::pair<double, int>> nearest;
    nearest.reserve(static_cast<size_t>(std::max(candidate_count * 4, candidate_count + 8)));

    const Coord& origin = coords[static_cast<size_t>(node)];
    const int base_x = grid.CellX(origin.first);
    const int base_y = grid.CellY(origin.second);
    const int target_pool = std::max(candidate_count * 3, candidate_count + 8);
    const int max_radius = std::max(grid.width, grid.height);

    for (int radius = 0; radius < max_radius && static_cast<int>(nearest.size()) < target_pool; ++radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                if (radius > 0 && std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }

                const int cell_x = base_x + dx;
                const int cell_y = base_y + dy;
                if (cell_x < 0 || cell_x >= grid.width || cell_y < 0 || cell_y >= grid.height) {
                    continue;
                }

                const auto* bucket = grid.Find(cell_x, cell_y);
                if (!bucket) {
                    continue;
                }

                for (int other : *bucket) {
                    if (other == node) {
                        continue;
                    }
                    nearest.emplace_back(SquaredDistanceCoordsV4(origin, coords[static_cast<size_t>(other)]), other);
                }
            }
        }
    }

    if (static_cast<int>(nearest.size()) < candidate_count) {
        for (int other = 0; other < static_cast<int>(coords.size()); ++other) {
            if (other == node) {
                continue;
            }
            const auto duplicate = std::find_if(nearest.begin(), nearest.end(),
                                                [other](const auto& pair) { return pair.second == other; });
            if (duplicate == nearest.end()) {
                nearest.emplace_back(SquaredDistanceCoordsV4(origin, coords[static_cast<size_t>(other)]), other);
            }
        }
    }

    TrimNearestCandidatesV4(nearest, static_cast<size_t>(candidate_count));

    std::vector<int> result;
    result.reserve(nearest.size());
    for (const auto& [_, city] : nearest) {
        result.push_back(city);
    }
    return result;
}

// Main candidate-list factory for v4. Strategy:
//   - n <= exact_threshold -> exact (full pairwise scan);
//   - otherwise try to build a spatial grid; on success -> grid-search;
//   - if the grid cannot be built (degenerate geometry) -> exact fallback.
// Returns the top-k nearest neighbors for each vertex.
CandidateSets BuildCandidateSetsV4(const mtsp::Instance& inst,
                                   int candidate_count,
                                   int exact_threshold) {
    const int node_count = inst.GetNodeCount();
    candidate_count = std::max(1, std::min(candidate_count, node_count - 1));

    CandidateSets sets(static_cast<size_t>(node_count));
    const auto& coords = inst.GetCoords();

    if (node_count <= exact_threshold) {
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV4(node, candidate_count, coords);
        }
        return sets;
    }

    SpatialGridV4 grid;
    if (!TryBuildSpatialGridV4(coords, candidate_count, grid)) {
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV4(node, candidate_count, coords);
        }
        return sets;
    }

    for (int node = 0; node < node_count; ++node) {
        sets[static_cast<size_t>(node)] = CollectNearestCandidatesGridV4(node, candidate_count, grid, coords);
    }

    return sets;
}

// Collect candidates for extending the route at the current construction step.
// First from the precomputed candidate list of vertex `from`; if fewer than fallback_limit
// are found, the shortfall is filled by linear sort. Equivalent to CollectConstructionCandidatesV3
// with one difference: the distance functor is passed as a parameter (for DistanceOracleV4).
template <typename DistanceFn>
std::vector<int> CollectConstructionCandidatesV4(int from,
                                                 const std::vector<char>& visited,
                                                 const CandidateSets& candidate_sets,
                                                 const mtsp::Instance& inst,
                                                 int fallback_limit,
                                                 DistanceFn& distance) {
    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(fallback_limit));

    for (int city : candidate_sets[static_cast<size_t>(from)]) {
        if (!visited[static_cast<size_t>(city)]) {
            candidates.push_back(city);
            if (static_cast<int>(candidates.size()) >= fallback_limit) {
                return candidates;
            }
        }
    }

    std::vector<std::pair<double, int>> fallback;
    fallback.reserve(inst.GetNodeCount());
    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[static_cast<size_t>(city)] &&
            std::find(candidates.begin(), candidates.end(), city) == candidates.end()) {
            fallback.emplace_back(distance(from, city), city);
        }
    }

    const size_t limit = std::min(static_cast<size_t>(fallback_limit - static_cast<int>(candidates.size())),
                                  fallback.size());
    TrimNearestCandidatesV4(fallback, limit);

    for (const auto& [_, city] : fallback) {
        candidates.push_back(city);
    }

    return candidates;
}

// Forward-look estimate for `node`: minimum distance to the nearest reachable
// unvisited client (or to the depot if no candidates remain).
// Used as the lookahead weight when selecting a move in BuildInitialRoutes.
template <typename DistanceFn>
double ForwardPotentialV4(int node,
                          const std::vector<char>& visited,
                          const CandidateSets& candidate_sets,
                          const mtsp::Instance& inst,
                          DistanceFn& distance) {
    double best = distance(node, 0);
    for (int candidate : candidate_sets[static_cast<size_t>(node)]) {
        if (!visited[static_cast<size_t>(candidate)]) {
            best = std::min(best, distance(node, candidate));
        }
    }

    if (best < std::numeric_limits<double>::max() / 4.0) {
        return best;
    }

    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!visited[static_cast<size_t>(city)]) {
            best = std::min(best, distance(node, city));
        }
    }

    return best;
}

// Candidate-restricted 2-opt with time budget support.
// Checks budget.ShouldStop() in the inner loop — the phase can be interrupted at any point.
// Don't-look bits + first-improvement as in v3, plus budget cancellation.
template <typename DistanceFn>
bool ApplyFirstImproving2OptV4(std::vector<int>& route,
                               const CandidateSets& candidate_sets,
                               int node_count,
                               DistanceFn& distance,
                               std::vector<int>& position,
                               std::vector<char>& dont_look,
                               SearchBudgetV4& budget) {
    if (route.size() <= 4) {
        return false;
    }

    std::fill(position.begin(), position.end(), -1);
    for (size_t idx = 0; idx < route.size(); ++idx) {
        position[static_cast<size_t>(route[idx])] = static_cast<int>(idx);
    }

    for (size_t i = 1; i + 2 < route.size(); ++i) {
        if (budget.ShouldStop()) {
            return false;
        }

        const int t1 = route[i - 1];
        const int t2 = route[i];

        if (dont_look[static_cast<size_t>(t1)]) {
            continue;
        }

        bool improved_from_anchor = false;
        for (int t3 : candidate_sets[static_cast<size_t>(t1)]) {
            const int j = position[static_cast<size_t>(t3)];
            if (j <= static_cast<int>(i) || j + 1 >= static_cast<int>(route.size())) {
                continue;
            }

            const int t4 = route[static_cast<size_t>(j) + 1];
            const double removed = distance(t1, t2) + distance(t3, t4);
            const double added = distance(t1, t3) + distance(t2, t4);
            if (added + kEps < removed) {
                std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                             route.begin() + static_cast<std::ptrdiff_t>(j + 1));
                std::fill(dont_look.begin(), dont_look.end(), 0);
                improved_from_anchor = true;
                break;
            }
        }

        if (improved_from_anchor) {
            return true;
        }

        dont_look[static_cast<size_t>(t1)] = 1;
    }

    return false;
}

// Lightweight candidate-restricted 2-opt cleanup: up to max_moves passes or until budget cancel.
// Used in inter-route swaps to quickly restore local optimality after a client is relocated
// (not a full ILS — just a bounded number of passes).
template <typename DistanceFn>
void QuickRouteCleanupV4(std::vector<int>& route,
                         const CandidateSets& candidate_sets,
                         int node_count,
                         DistanceFn& distance,
                         int max_moves,
                         SearchBudgetV4& budget) {
    if (route.size() <= 4 || max_moves <= 0) {
        return;
    }

    std::vector<int> position(static_cast<size_t>(node_count), -1);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);

    for (int move = 0; move < max_moves && !budget.ShouldStop(); ++move) {
        if (!ApplyFirstImproving2OptV4(route, candidate_sets, node_count, distance, position, dont_look, budget)) {
            return;
        }
    }
}

// Full guided route improvement: repeats ApplyFirstImproving2OptV4 to convergence;
// if no improvement is found, tries reversing the interior (reflection) and repeating.
// Checks the budget on every outer-loop iteration.
template <typename DistanceFn>
void ImproveRouteGuidedV4(std::vector<int>& route,
                          const CandidateSets& candidate_sets,
                          int node_count,
                          DistanceFn& distance,
                          SearchBudgetV4& budget) {
    if (route.size() <= 4) {
        return;
    }

    std::vector<int> position(static_cast<size_t>(node_count), -1);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);

    while (!budget.ShouldStop()) {
        if (ApplyFirstImproving2OptV4(route, candidate_sets, node_count, distance, position, dont_look, budget)) {
            continue;
        }

        if (route.size() > 4) {
            std::vector<int> reversed = route;
            std::reverse(reversed.begin() + 1, reversed.end() - 1);
            std::fill(dont_look.begin(), dont_look.end(), 0);
            if (ApplyFirstImproving2OptV4(reversed, candidate_sets, node_count, distance, position, dont_look,
                                          budget)) {
                route.swap(reversed);
                continue;
            }
        }

        break;
    }
}

// Double-bridge perturbation, identical to v3. 4-opt move to escape 2-opt local optima.
template <typename DistanceFn>
void DoubleBridgeKickV4(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
    if (route.size() < 10) {
        return;
    }

    const int n = static_cast<int>(route.size()) - 1;
    std::uniform_int_distribution<int> cut1_dist(1, n / 4);
    std::uniform_int_distribution<int> cut2_dist(n / 4 + 1, n / 2);
    std::uniform_int_distribution<int> cut3_dist(n / 2 + 1, (3 * n) / 4);

    const int a = cut1_dist(rng);
    const int b = cut2_dist(rng);
    const int c = cut3_dist(rng);

    std::vector<int> kicked;
    kicked.reserve(route.size());
    kicked.push_back(route.front());
    kicked.insert(kicked.end(), route.begin() + a, route.begin() + b);
    kicked.insert(kicked.end(), route.begin() + c, route.end() - 1);
    kicked.insert(kicked.end(), route.begin() + b, route.begin() + c);
    kicked.insert(kicked.end(), route.begin() + 1, route.begin() + a);
    kicked.push_back(route.front());
    route.swap(kicked);
}

// ILS v4: ImproveRouteGuidedV4 → double-bridge → ImproveRouteGuidedV4, for up to `rounds`
// iterations or until budget cancellation. The best-length solution is returned via outparam.
// Key difference from v3: budget cooperation at every step.
template <typename DistanceFn>
void IteratedLocalSearchV4(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const CandidateSets& candidate_sets,
                           int node_count,
                           DistanceFn& distance,
                           SearchBudgetV4& budget) {
    ImproveRouteGuidedV4(route, candidate_sets, node_count, distance, budget);
    if (budget.ShouldStop()) {
        return;
    }

    double best_length = RouteLengthGenericV4(route, distance);
    std::vector<int> best = route;

    for (int round = 0; round < rounds && !budget.ShouldStop(); ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV4(candidate, rng, distance);
        ImproveRouteGuidedV4(candidate, candidate_sets, node_count, distance, budget);
        const double candidate_length = RouteLengthGenericV4(candidate, distance);
        if (candidate_length + kEps < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }

    route.swap(best);
}

// Assigns remaining unvisited clients after an early stop of the construction phase
// (due to budget cancellation). Each client is appended to the route with the minimum
// insertion cost, guaranteeing that the final solution covers all clients (validity).
template <typename DistanceFn>
void CompleteRemainingAssignmentsV4(mtsp::RouteSet& out,
                                    std::vector<int>& current,
                                    std::vector<int>& route_sizes,
                                    std::vector<char>& visited,
                                    DistanceFn& distance) {
    for (int city = 1; city < mtsp::Instance::GetInstance().GetNodeCount(); ++city) {
        if (visited[static_cast<size_t>(city)]) {
            continue;
        }

        int best_salesman = 0;
        double best_score = std::numeric_limits<double>::max();
        for (size_t salesman = 0; salesman < out.size(); ++salesman) {
            const double score = distance(current[salesman], city) + 0.05 * static_cast<double>(route_sizes[salesman]);
            if (score + kEps < best_score) {
                best_score = score;
                best_salesman = static_cast<int>(salesman);
            }
        }

        out[static_cast<size_t>(best_salesman)].push_back(city);
        current[static_cast<size_t>(best_salesman)] = city;
        ++route_sizes[static_cast<size_t>(best_salesman)];
        visited[static_cast<size_t>(city)] = 1;
    }
}

} // namespace

namespace mtsp {

// v4: the first big-instance version. Registered in SolverFactory as "lkh-wrapper-v4".
// Main additions over v3: spatial-grid candidate search, on-demand distance cache,
// and an explicit time limit surface. Parameters (candidate_count, rounds) are auto-scaled
// to instance size via EffectiveCandidateCount / EffectiveRounds.
class LkhWrapperSolverV4 : public Solver {
public:
    // CLI parameters:
    //   seed                       — RNG seed (default 42);
    //   rounds                     — number of ILS iterations (auto-scaled: fewer for large n);
    //   candidate-count            — k-NN candidate-list size (auto-scaled);
    //   lookahead-weight           — weight of the forward potential in the score;
    //   depot-weight               — weight of the depot distance in the score;
    //   time-budget-ms             — wall-clock time limit (default 0 = no limit);
    //   guided-cleanup-passes      — number of additional guided 2-opt passes;
    //   inter-route-batch          — batch size for inter-route swaps;
    //   exact-candidate-threshold  — n threshold below which the exact fallback is used.
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
            local_rng_.seed(seed_ ^ 0x9E3779B9U);
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("candidate-count")) {
            candidate_count_ = std::max(4, std::stoi(opts.at("candidate-count")));
        }
        if (opts.count("lookahead-weight")) {
            lookahead_weight_ = std::stod(opts.at("lookahead-weight"));
        }
        if (opts.count("depot-weight")) {
            depot_weight_ = std::stod(opts.at("depot-weight"));
        }
        if (opts.count("time-budget-ms")) {
            time_budget_ms_ = std::max(0, std::stoi(opts.at("time-budget-ms")));
        }
        if (opts.count("guided-cleanup-passes")) {
            guided_cleanup_passes_ = std::max(0, std::stoi(opts.at("guided-cleanup-passes")));
        }
        if (opts.count("inter-route-batch")) {
            inter_route_batch_ = std::max(1, std::stoi(opts.at("inter-route-batch")));
        }
        if (opts.count("exact-candidate-threshold")) {
            exact_candidate_threshold_ = std::max(32, std::stoi(opts.at("exact-candidate-threshold")));
        }
    }

    // Main v4 pipeline: builds candidate lists (with scaled parameters),
    // then a seed phase with lookahead weights, then ILS on each route, then inter-route swaps.
    // All stages handle budget cancellation correctly: a complete-fallback is triggered when it fires.
    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        SearchBudgetV4 budget(time_budget_ms_);
        DistanceOracleV4 distance(inst);
        const int effective_candidate_count = EffectiveCandidateCount(inst.GetNodeCount());
        const int effective_rounds = EffectiveRounds(inst.GetNodeCount());
        const CandidateSets candidate_sets = BuildCandidateSetsV4(inst, effective_candidate_count, exact_candidate_threshold_);
        std::mt19937 rng(seed_);

        BuildInitialRoutes(out, candidate_sets, distance, budget);

        for (auto& route : out) {
            route.push_back(0);
        }

        for (auto& route : out) {
            if (budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV4(route, rng, effective_rounds, candidate_sets, inst.GetNodeCount(), distance, budget);
        }

        if (!budget.ShouldStop()) {
            ImproveInterRoute(out, candidate_sets, effective_rounds, distance, budget);
        }
    }

private:
    // Auto-scale the candidate-list size based on n:
    //   n >= 20K — 50% of base, minimum 6;
    //   n >= 5K  — 75% of base, minimum 8;
    //   otherwise — base unchanged.
    // Goal: reduce cache traffic and build time for large instances.
    int EffectiveCandidateCount(int node_count) const {
        if (node_count >= 20'000) {
            return std::max(6, candidate_count_ / 2);
        }
        if (node_count >= 5'000) {
            return std::max(8, (candidate_count_ * 3) / 4);
        }
        return candidate_count_;
    }

    // Auto-scale the number of ILS iterations. For large n each iteration is more expensive,
    // so fewer are run (rounds/4 for n >= 20K, rounds/2 for n >= 5K).
    int EffectiveRounds(int node_count) const {
        if (node_count >= 20'000) {
            return std::max(2, rounds_ / 4);
        }
        if (node_count >= 5'000) {
            return std::max(4, rounds_ / 2);
        }
        return rounds_;
    }

    // Build initial routes with score = immediate + lookahead*forward
    // + depot_weight*depot + 0.05*size_penalty. The budget is checked at every step;
    // on cancellation, remaining clients are assigned via CompleteRemainingAssignmentsV4
    // to guarantee a valid solution.
    void BuildInitialRoutes(RouteSet& out,
                            const CandidateSets& candidate_sets,
                            DistanceOracleV4& distance,
                            SearchBudgetV4& budget) const {
        const Instance& inst = Instance::GetInstance();

        out.assign(static_cast<size_t>(inst.GetSalesmanCount()), std::vector<int>{0});
        std::vector<int> current(static_cast<size_t>(inst.GetSalesmanCount()), 0);
        std::vector<int> route_sizes(static_cast<size_t>(inst.GetSalesmanCount()), 0);
        std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
        visited[0] = 1;

        int remaining = inst.GetNodeCount() - 1;
        while (remaining > 0) {
            if (budget.ShouldStop()) {
                CompleteRemainingAssignmentsV4(out, current, route_sizes, visited, distance);
                return;
            }

            int best_salesman = -1;
            int best_city = -1;
            double best_score = std::numeric_limits<double>::max();
            double best_immediate = std::numeric_limits<double>::max();

            for (int salesman = 0; salesman < inst.GetSalesmanCount(); ++salesman) {
                const std::vector<int> move_candidates = CollectConstructionCandidatesV4(
                    current[static_cast<size_t>(salesman)],
                    visited,
                    candidate_sets,
                    inst,
                    std::max(candidate_count_, 6),
                    distance);

                for (int city : move_candidates) {
                    const double immediate = distance(current[static_cast<size_t>(salesman)], city);
                    const double forward = ForwardPotentialV4(city, visited, candidate_sets, inst, distance);
                    const double depot = distance(city, 0);

                    const double balance_penalty = 0.05 * static_cast<double>(route_sizes[static_cast<size_t>(salesman)]);
                    const double score = immediate + lookahead_weight_ * forward + depot_weight_ * depot + balance_penalty;

                    if (score + kEps < best_score ||
                        (std::abs(score - best_score) <= kEps && immediate + kEps < best_immediate)) {
                        best_score = score;
                        best_immediate = immediate;
                        best_salesman = salesman;
                        best_city = city;
                    }
                }
            }

            if (best_city == -1) {
                CompleteRemainingAssignmentsV4(out, current, route_sizes, visited, distance);
                return;
            }

            out[static_cast<size_t>(best_salesman)].push_back(best_city);
            current[static_cast<size_t>(best_salesman)] = best_city;
            ++route_sizes[static_cast<size_t>(best_salesman)];
            visited[static_cast<size_t>(best_city)] = 1;
            --remaining;
        }
    }

    // Lazy ILS rerun on routes marked dirty (modified during inter-route swaps).
    // ILS is not called immediately after each swap — that would be too expensive. Instead,
    // routes are marked dirty and a batch rerun is triggered periodically. The ILS itself uses
    // a reduced round count (effective_rounds/3) for speed.
    void RunDeferredInterRouteIls(RouteSet& routes,
                                  std::vector<char>& dirty_routes,
                                  const CandidateSets& candidate_sets,
                                  int effective_rounds,
                                  DistanceOracleV4& distance,
                                  SearchBudgetV4& budget) const {
        const Instance& inst = Instance::GetInstance();
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            if (!dirty_routes[route_idx] || budget.ShouldStop()) {
                continue;
            }
            IteratedLocalSearchV4(routes[route_idx],
                                  local_rng_,
                                  std::max(2, effective_rounds / 3),
                                  candidate_sets,
                                  inst.GetNodeCount(),
                                  distance,
                                  budget);
            dirty_routes[route_idx] = 0;
        }
    }

    // Inter-route swap with candidate filtering and lazy ILS rerun.
    // Only promising pairs (IsPromisingSwapPair) are enumerated; on each accepted swap,
    // both routes are marked dirty and RunDeferredInterRouteIls is triggered periodically
    // (every inter_route_batch swaps) to restore local optimality.
    // The loop continues until convergence or budget cancellation.
    void ImproveInterRoute(RouteSet& routes,
                           const CandidateSets& candidate_sets,
                           int effective_rounds,
                           DistanceOracleV4& distance,
                           SearchBudgetV4& budget) const {
        const Instance& inst = Instance::GetInstance();
        std::vector<char> dirty_routes(routes.size(), 0);
        int accepted_since_ils = 0;

        bool improved = true;
        while (improved && !budget.ShouldStop()) {
            improved = false;

            for (size_t a = 0; a < routes.size() && !improved; ++a) {
                for (size_t b = a + 1; b < routes.size() && !improved; ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size() && !improved; ++i) {
                        if (budget.ShouldStop()) {
                            break;
                        }

                        for (size_t j = 1; j + 1 < routes[b].size(); ++j) {
                            const int city_a = routes[a][i];
                            const int city_b = routes[b][j];

                            if (!IsPromisingSwapPair(city_a, city_b, candidate_sets)) {
                                continue;
                            }

                            const double delta = SwapDeltaClosedRoutes(routes[a], i, routes[b], j, distance);
                            if (delta >= -kEps) {
                                continue;
                            }

                            std::swap(routes[a][i], routes[b][j]);

                            QuickRouteCleanupV4(routes[a],
                                                candidate_sets,
                                                inst.GetNodeCount(),
                                                distance,
                                                guided_cleanup_passes_,
                                                budget);
                            QuickRouteCleanupV4(routes[b],
                                                candidate_sets,
                                                inst.GetNodeCount(),
                                                distance,
                                                guided_cleanup_passes_,
                                                budget);

                            dirty_routes[a] = 1;
                            dirty_routes[b] = 1;
                            ++accepted_since_ils;

                            if (accepted_since_ils >= inter_route_batch_ && !budget.ShouldStop()) {
                                RunDeferredInterRouteIls(routes, dirty_routes, candidate_sets, effective_rounds,
                                                         distance, budget);
                                accepted_since_ils = 0;
                            }

                            improved = true;
                            break;
                        }
                    }
                }
            }
        }

        if (accepted_since_ils > 0 && !budget.ShouldStop()) {
            RunDeferredInterRouteIls(routes, dirty_routes, candidate_sets, effective_rounds, distance, budget);
        }
    }

    unsigned int seed_ = 42U;
    int rounds_ = 8;
    int candidate_count_ = 12;
    double lookahead_weight_ = 0.35;
    double depot_weight_ = 0.12;
    int time_budget_ms_ = 300'000;
    int guided_cleanup_passes_ = 2;
    int inter_route_batch_ = 3;
    int exact_candidate_threshold_ = 512;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v4 = (SolverFactory::RegisterSolver("lkh-wrapper-v4", []() {
    return std::make_unique<LkhWrapperSolverV4>();
}),
                               true);

} // namespace mtsp
