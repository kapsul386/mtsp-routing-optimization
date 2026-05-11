// v5: continuation of the v4 large-instance line with infrastructure improvements.
// Adds on top of v4:
//   - DistanceOracleV5 with a precomputed depot-distance vector (hot path for
//     seed strategies and rebalance operators);
//   - RouteIndexV5 with a stamp mechanism (instead of std::fill on every Build() call —
//     increments the stamp and lazily checks seen[node] == stamp; saves O(n) per index rebuild);
//   - EdgeFreqMap for accumulating edge frequencies (used at the finalization stage
//     to select the most consistently good routes).
// Faster than v4 on large uniform instances, but loses in MINSUM (inner-loop simplifications
// trade quality for speed). Data point in the large-scale v4--v6 trade-off study (Appendix D).
// Registered as "lkh-wrapper-v5".

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
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
using EdgeFreqMap = std::vector<std::unordered_map<int, int>>;

constexpr double kEps = 1e-9;
constexpr double kCoordEps = 1e-12;
constexpr long long kLargeInstanceDistancePairs = 4'000'000LL;

// Time budget for v5: adds reserve_budget_ms (reserved for post-phase work: validation,
// final snapshot). The effective limit is total - reserve.
// This lets the inner phases stay within total - reserve, leaving margin for cleanup.
class SearchBudgetV5 {
public:
    SearchBudgetV5(int total_budget_ms, int reserve_budget_ms)
        : enabled_(total_budget_ms > 0) {
        if (!enabled_) {
            deadline_ = std::chrono::steady_clock::time_point::max();
            return;
        }
        const int effective_budget = std::max(1, total_budget_ms - std::max(0, reserve_budget_ms));
        deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(effective_budget);
    }

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

// Distance oracle v5 — extends v4: adds a precomputed depot-distance vector.
// This is the hot path in seed strategies (selecting start points by proximity to the depot)
// and in rebalance. Pair caching for large instances follows the same principles as in v4.
class DistanceOracleV5 {
public:
    // Constructor: when cache is enabled, reserves ~24 pairs per vertex (for denser
    // query traffic than v4); then precomputes depot_dist_ for every vertex.
    explicit DistanceOracleV5(const mtsp::Instance& inst)
        : inst_(inst),
          coords_(inst.GetCoords()),
          cache_enabled_(static_cast<long long>(inst.GetNodeCount()) * static_cast<long long>(inst.GetNodeCount()) >
                         kLargeInstanceDistancePairs),
          depot_dist_(coords_.size(), 0.0) {
        if (cache_enabled_) {
            cache_.reserve(std::max<size_t>(4096, coords_.size() * 24ULL));
        }
        for (size_t node = 0; node < coords_.size(); ++node) {
            depot_dist_[node] = std::sqrt(SquaredDistance(static_cast<int>(node), 0));
        }
    }

    double operator()(int a, int b) {
        if (a == b) {
            return 0.0;
        }
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

    [[nodiscard]] double SquaredDistance(int a, int b) const {
        const double dx = coords_[static_cast<size_t>(a)].first - coords_[static_cast<size_t>(b)].first;
        const double dy = coords_[static_cast<size_t>(a)].second - coords_[static_cast<size_t>(b)].second;
        return dx * dx + dy * dy;
    }

    [[nodiscard]] double DepotDistance(int node) const {
        return depot_dist_[static_cast<size_t>(node)];
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
    std::vector<double> depot_dist_;
};

// Stamped route index: stores (position, seen_stamp) for each vertex.
// Build() increments the stamp instead of calling std::fill(seen, 0) — O(n) -> O(L) per rebuild.
// Get() returns the position only if seen[node] == current stamp (lazy invalidation).
// On uint32_t overflow a one-off sweep is performed — safe.
struct RouteIndexV5 {
    explicit RouteIndexV5(int node_count) : position(static_cast<size_t>(node_count), -1), seen(static_cast<size_t>(node_count), 0) {}

    // Rebuilds the index for a new route. Old entries become invisible
    // via the rotated stamp — no unnecessary std::fill calls.
    void Build(const std::vector<int>& route) {
        ++stamp;
        if (stamp == 0) {
            std::fill(seen.begin(), seen.end(), 0);
            stamp = 1;
        }
        for (size_t idx = 0; idx < route.size(); ++idx) {
            const int node = route[idx];
            position[static_cast<size_t>(node)] = static_cast<int>(idx);
            seen[static_cast<size_t>(node)] = stamp;
        }
    }

    // Returns the position of `node` in the current route, or -1 if it is not present.
    [[nodiscard]] int Get(int node) const {
        return seen[static_cast<size_t>(node)] == stamp ? position[static_cast<size_t>(node)] : -1;
    }

    std::vector<int> position;
    std::vector<uint32_t> seen;
    uint32_t stamp = 1;
};

struct SpatialGridV5 {
    double min_x = 0.0;
    double min_y = 0.0;
    double cell_size = 1.0;
    int width = 1;
    int height = 1;
    std::unordered_map<uint64_t, std::vector<int>> buckets;

    [[nodiscard]] int CellX(double x) const {
        if (width <= 1) {
            return 0;
        }
        const int idx = static_cast<int>((x - min_x) / cell_size);
        return std::clamp(idx, 0, width - 1);
    }

    [[nodiscard]] int CellY(double y) const {
        if (height <= 1) {
            return 0;
        }
        const int idx = static_cast<int>((y - min_y) / cell_size);
        return std::clamp(idx, 0, height - 1);
    }

    [[nodiscard]] const std::vector<int>* Find(int cell_x, int cell_y) const {
        const auto it = buckets.find(PackKey(cell_x, cell_y));
        return it == buckets.end() ? nullptr : &it->second;
    }

    static uint64_t PackKey(int cell_x, int cell_y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cell_x)) << 32U) |
               static_cast<uint32_t>(cell_y);
    }
};

double SquaredDistanceCoordsV5(const Coord& lhs, const Coord& rhs) {
    const double dx = lhs.first - rhs.first;
    const double dy = lhs.second - rhs.second;
    return dx * dx + dy * dy;
}

template <typename DistanceFn>
double RouteLengthGenericV5(const std::vector<int>& route, DistanceFn& distance) {
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

template <typename T>
void TrimNearestV5(std::vector<std::pair<T, int>>& values, size_t limit) {
    if (limit == 0 || values.empty()) {
        values.clear();
        return;
    }
    const auto cmp = [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first || (lhs.first == rhs.first && lhs.second < rhs.second);
    };
    if (values.size() > limit) {
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(limit), values.end(), cmp);
        values.resize(limit);
    }
    std::sort(values.begin(), values.end(), cmp);
}

bool TryBuildSpatialGridV5(const std::vector<Coord>& coords, int candidate_count, SpatialGridV5& out_grid) {
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
    const double target_points_per_cell = std::max(10.0, static_cast<double>(candidate_count) * 2.5);
    const double target_cell_area = std::max(kCoordEps, area * target_points_per_cell / static_cast<double>(coords.size()));
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
        out_grid.buckets[SpatialGridV5::PackKey(cell_x, cell_y)].push_back(node);
    }

    return true;
}

// Exact fallback for candidate-list construction (O(n^2) time). Used when the spatial
// grid could not be built or for small instances.
std::vector<int> CollectNearestCandidatesExactV5(int node, int candidate_count, const std::vector<Coord>& coords) {
    std::vector<std::pair<double, int>> nearest;
    nearest.reserve(coords.size() - 1);
    for (int other = 0; other < static_cast<int>(coords.size()); ++other) {
        if (other == node) {
            continue;
        }
        nearest.emplace_back(SquaredDistanceCoordsV5(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(other)]), other);
    }
    TrimNearestV5(nearest, static_cast<size_t>(candidate_count));

    std::vector<int> result;
    result.reserve(nearest.size());
    for (const auto& [_, city] : nearest) {
        result.push_back(city);
    }
    return result;
}

// Grid-based nearest-neighbor search with an expanding radius.
// Accumulates target_pool candidates and trims to the best candidate_count.
// If too few candidates are found in the neighborhood (degenerate case), falls back to an exact scan.
std::vector<int> CollectNearestCandidatesGridV5(int node,
                                                int candidate_count,
                                                const SpatialGridV5& grid,
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
                    nearest.emplace_back(SquaredDistanceCoordsV5(origin, coords[static_cast<size_t>(other)]), other);
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
                nearest.emplace_back(SquaredDistanceCoordsV5(origin, coords[static_cast<size_t>(other)]), other);
            }
        }
    }

    TrimNearestV5(nearest, static_cast<size_t>(candidate_count));

    std::vector<int> result;
    result.reserve(nearest.size());
    for (const auto& [_, city] : nearest) {
        result.push_back(city);
    }
    return result;
}

// Main candidate-list factory for v5 (analogous to BuildCandidateSetsV4).
// The name BuildGeometricCandidatesV5 emphasizes that the geometric grid search,
// not an abstract distance-based method, is the primary approach.
CandidateSets BuildGeometricCandidatesV5(const mtsp::Instance& inst, int candidate_count, int exact_threshold) {
    const int node_count = inst.GetNodeCount();
    candidate_count = std::max(1, std::min(candidate_count, node_count - 1));

    CandidateSets sets(static_cast<size_t>(node_count));
    const auto& coords = inst.GetCoords();

    if (node_count <= exact_threshold) {
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV5(node, candidate_count, coords);
        }
        return sets;
    }

    SpatialGridV5 grid;
    if (!TryBuildSpatialGridV5(coords, candidate_count, grid)) {
        for (int node = 0; node < node_count; ++node) {
            sets[static_cast<size_t>(node)] = CollectNearestCandidatesExactV5(node, candidate_count, coords);
        }
        return sets;
    }

    for (int node = 0; node < node_count; ++node) {
        sets[static_cast<size_t>(node)] = CollectNearestCandidatesGridV5(node, candidate_count, grid, coords);
    }
    return sets;
}

void NormalizeCandidateSymmetryV5(CandidateSets& sets, size_t max_per_node) {
    for (size_t from = 0; from < sets.size(); ++from) {
        for (int to : sets[from]) {
            auto& back = sets[static_cast<size_t>(to)];
            if (std::find(back.begin(), back.end(), static_cast<int>(from)) == back.end()) {
                back.push_back(static_cast<int>(from));
            }
        }
    }
    for (auto& vec : sets) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        if (vec.size() > max_per_node) {
            vec.resize(max_per_node);
        }
    }
}

template <typename DistanceFn>
void AddTourEdgesToFrequencyV5(const std::vector<int>& closed_tour, EdgeFreqMap& freq, const DistanceFn&) {
    for (size_t i = 1; i < closed_tour.size(); ++i) {
        const int a = closed_tour[i - 1];
        const int b = closed_tour[i];
        ++freq[static_cast<size_t>(a)][b];
        ++freq[static_cast<size_t>(b)][a];
    }
}

std::vector<int> BuildSampleTourGreedyV5(const std::vector<int>& sample, const std::vector<Coord>& coords) {
    if (sample.empty()) {
        return {};
    }
    std::vector<int> remaining = sample;
    std::vector<int> order;
    order.reserve(sample.size());

    auto it0 = std::find(remaining.begin(), remaining.end(), 0);
    if (it0 != remaining.end()) {
        std::swap(*it0, remaining.front());
    }
    order.push_back(remaining.front());
    remaining.erase(remaining.begin());

    while (!remaining.empty()) {
        const int last = order.back();
        auto best_it = remaining.begin();
        double best_dist = std::numeric_limits<double>::max();
        for (auto it = remaining.begin(); it != remaining.end(); ++it) {
            const double dist = SquaredDistanceCoordsV5(coords[static_cast<size_t>(last)], coords[static_cast<size_t>(*it)]);
            if (dist + kEps < best_dist) {
                best_dist = dist;
                best_it = it;
            }
        }
        order.push_back(*best_it);
        remaining.erase(best_it);
    }

    if (order.front() != 0) {
        auto found = std::find(order.begin(), order.end(), 0);
        if (found != order.end()) {
            std::rotate(order.begin(), found, order.end());
        }
    }
    return order;
}

template <typename DistanceFn>
void ImproveClosedTour2OptLimitedV5(std::vector<int>& closed_tour,
                                    const CandidateSets& candidate_sets,
                                    DistanceFn& distance,
                                    int max_passes,
                                    SearchBudgetV5& budget) {
    if (closed_tour.size() <= 5 || max_passes <= 0) {
        return;
    }

    RouteIndexV5 index(static_cast<int>(candidate_sets.size()));
    for (int pass = 0; pass < max_passes && !budget.ShouldStop(); ++pass) {
        bool improved = false;
        index.Build(closed_tour);
        for (size_t i = 1; i + 2 < closed_tour.size(); ++i) {
            if (budget.ShouldStop()) {
                return;
            }
            const int a = closed_tour[i - 1];
            const int b = closed_tour[i];
            for (int c : candidate_sets[static_cast<size_t>(a)]) {
                const int j = index.Get(c);
                if (j <= static_cast<int>(i) || j + 1 >= static_cast<int>(closed_tour.size())) {
                    continue;
                }
                if (j == static_cast<int>(closed_tour.size()) - 1) {
                    continue;
                }
                const int d = closed_tour[static_cast<size_t>(j) + 1];
                const double removed = distance(a, b) + distance(c, d);
                const double added = distance(a, c) + distance(b, d);
                if (added + kEps < removed) {
                    std::reverse(closed_tour.begin() + static_cast<std::ptrdiff_t>(i),
                                 closed_tour.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                    break;
                }
            }
            if (improved) {
                break;
            }
        }
        if (!improved) {
            break;
        }
    }
    closed_tour.back() = closed_tour.front();
}

template <typename DistanceFn>
void OptimizeWindowOpenPathV5(std::vector<int>& closed_tour,
                              size_t left,
                              size_t right,
                              const CandidateSets& candidate_sets,
                              RouteIndexV5& index,
                              DistanceFn& distance,
                              SearchBudgetV5& budget) {
    if (right <= left + 2 || right >= closed_tour.size()) {
        return;
    }

    bool improved = true;
    while (improved && !budget.ShouldStop()) {
        improved = false;
        index.Build(closed_tour);
        for (size_t i = left + 1; i + 1 < right; ++i) {
            const int t1 = closed_tour[i - 1];
            const int t2 = closed_tour[i];
            for (int t3 : candidate_sets[static_cast<size_t>(t1)]) {
                const int j = index.Get(t3);
                if (j <= static_cast<int>(i) || j >= static_cast<int>(right)) {
                    continue;
                }
                if (j + 1 > static_cast<int>(right)) {
                    continue;
                }
                const int t4 = closed_tour[static_cast<size_t>(j) + 1];
                const double removed = distance(t1, t2) + distance(t3, t4);
                const double added = distance(t1, t3) + distance(t2, t4);
                if (added + kEps < removed) {
                    std::reverse(closed_tour.begin() + static_cast<std::ptrdiff_t>(i),
                                 closed_tour.begin() + static_cast<std::ptrdiff_t>(j + 1));
                    improved = true;
                    break;
                }
            }
            if (improved) {
                break;
            }
        }
    }
    closed_tour.back() = closed_tour.front();
}

template <typename DistanceFn>
void FastPopmusicPassV5(std::vector<int>& closed_tour,
                        int window_len,
                        const CandidateSets& candidate_sets,
                        DistanceFn& distance,
                        SearchBudgetV5& budget) {
    const int n = static_cast<int>(closed_tour.size()) - 1;
    if (n <= window_len + 2) {
        ImproveClosedTour2OptLimitedV5(closed_tour, candidate_sets, distance, 2, budget);
        return;
    }

    RouteIndexV5 index(static_cast<int>(candidate_sets.size()));
    for (int scan = 0; scan < 2 && !budget.ShouldStop(); ++scan) {
        const int shift = (scan == 0 ? 0 : window_len / 2);
        if (shift > 0) {
            std::rotate(closed_tour.begin() + 1,
                        closed_tour.begin() + 1 + std::min(shift, n - 1),
                        closed_tour.end() - 1);
            closed_tour.back() = closed_tour.front();
        }
        for (int start = 0; start < n && !budget.ShouldStop(); start += window_len) {
            const size_t left = static_cast<size_t>(start);
            const size_t right = static_cast<size_t>(std::min(n - 1, start + window_len - 1));
            OptimizeWindowOpenPathV5(closed_tour, left, right, candidate_sets, index, distance, budget);
        }
    }
}

template <typename DistanceFn>
std::vector<int> BuildPopmusicTourV5(const mtsp::Instance& inst,
                                     const CandidateSets& geometric_candidates,
                                     int sample_size,
                                     int window_len,
                                     std::mt19937& rng,
                                     DistanceFn& distance,
                                     SearchBudgetV5& budget) {
    const int node_count = inst.GetNodeCount();
    std::vector<int> all_nodes(static_cast<size_t>(node_count));
    std::iota(all_nodes.begin(), all_nodes.end(), 0);
    if (node_count > 2) {
        std::shuffle(all_nodes.begin() + 1, all_nodes.end(), rng);
    }

    sample_size = std::max(8, std::min(sample_size, node_count));
    std::vector<int> sample;
    sample.reserve(static_cast<size_t>(sample_size));
    sample.push_back(0);
    for (int i = 1; i < sample_size; ++i) {
        sample.push_back(all_nodes[static_cast<size_t>(i)]);
    }

    std::vector<int> anchors = BuildSampleTourGreedyV5(sample, inst.GetCoords());
    if (anchors.empty()) {
        anchors = {0};
    }

    std::unordered_map<int, size_t> anchor_pos;
    anchor_pos.reserve(anchors.size() * 2ULL);
    for (size_t pos = 0; pos < anchors.size(); ++pos) {
        anchor_pos[anchors[pos]] = pos;
    }

    std::vector<std::vector<int>> buckets(anchors.size());
    for (int city = 1; city < node_count; ++city) {
        if (anchor_pos.find(city) != anchor_pos.end()) {
            continue;
        }
        size_t best_anchor = 0;
        double best_dist = std::numeric_limits<double>::max();
        for (size_t pos = 0; pos < anchors.size(); ++pos) {
            const double dist = distance(city, anchors[pos]);
            if (dist + kEps < best_dist) {
                best_dist = dist;
                best_anchor = pos;
            }
        }
        buckets[best_anchor].push_back(city);
    }

    for (size_t pos = 0; pos < buckets.size(); ++pos) {
        auto& bucket = buckets[pos];
        const int anchor = anchors[pos];
        std::sort(bucket.begin(), bucket.end(), [&](int lhs, int rhs) {
            const double dl = distance(anchor, lhs);
            const double dr = distance(anchor, rhs);
            if (std::abs(dl - dr) > kEps) {
                return dl < dr;
            }
            return lhs < rhs;
        });
    }

    std::vector<int> closed_tour;
    closed_tour.reserve(static_cast<size_t>(node_count + 1));
    for (size_t pos = 0; pos < anchors.size(); ++pos) {
        closed_tour.push_back(anchors[pos]);
        for (int city : buckets[pos]) {
            closed_tour.push_back(city);
        }
    }

    auto zero_it = std::find(closed_tour.begin(), closed_tour.end(), 0);
    if (zero_it != closed_tour.end()) {
        std::rotate(closed_tour.begin(), zero_it, closed_tour.end());
    }
    if (closed_tour.front() != 0) {
        closed_tour.insert(closed_tour.begin(), 0);
    }
    closed_tour.push_back(closed_tour.front());

    ImproveClosedTour2OptLimitedV5(closed_tour, geometric_candidates, distance, 2, budget);
    FastPopmusicPassV5(closed_tour, std::max(12, window_len), geometric_candidates, distance, budget);
    ImproveClosedTour2OptLimitedV5(closed_tour, geometric_candidates, distance, 1, budget);
    return closed_tour;
}

template <typename DistanceFn>
CandidateSets BuildHybridCandidateSetsV5(const mtsp::Instance& inst,
                                         int final_candidate_count,
                                         int geometric_candidate_count,
                                         int exact_threshold,
                                         int popmusic_solutions,
                                         int popmusic_sample_size,
                                         int popmusic_window,
                                         std::mt19937& rng,
                                         DistanceFn& distance,
                                         SearchBudgetV5& budget) {
    CandidateSets geometric = BuildGeometricCandidatesV5(inst, geometric_candidate_count, exact_threshold);
    EdgeFreqMap freq(static_cast<size_t>(inst.GetNodeCount()));

    for (int node = 0; node < inst.GetNodeCount(); ++node) {
        auto& map = freq[static_cast<size_t>(node)];
        map.reserve(geometric[static_cast<size_t>(node)].size() * 2ULL + 8ULL);
        for (int other : geometric[static_cast<size_t>(node)]) {
            map[other] += 2;
        }
    }

    for (int s = 0; s < popmusic_solutions && !budget.ShouldStop(); ++s) {
        std::vector<int> tour = BuildPopmusicTourV5(inst,
                                                    geometric,
                                                    popmusic_sample_size,
                                                    popmusic_window,
                                                    rng,
                                                    distance,
                                                    budget);
        if (tour.size() >= 2) {
            AddTourEdgesToFrequencyV5(tour, freq, distance);
        }
    }

    CandidateSets result(static_cast<size_t>(inst.GetNodeCount()));
    const auto& coords = inst.GetCoords();
    for (int node = 0; node < inst.GetNodeCount(); ++node) {
        std::vector<std::pair<std::pair<int, double>, int>> ranked;
        ranked.reserve(freq[static_cast<size_t>(node)].size());
        for (const auto& [other, count] : freq[static_cast<size_t>(node)]) {
            if (other == node) {
                continue;
            }
            const double sq = SquaredDistanceCoordsV5(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(other)]);
            ranked.push_back({{-count, sq}, other});
        }

        const auto cmp = [](const auto& lhs, const auto& rhs) {
            if (lhs.first.first != rhs.first.first) {
                return lhs.first.first < rhs.first.first;
            }
            if (std::abs(lhs.first.second - rhs.first.second) > kEps) {
                return lhs.first.second < rhs.first.second;
            }
            return lhs.second < rhs.second;
        };

        if (ranked.size() > static_cast<size_t>(final_candidate_count)) {
            std::nth_element(ranked.begin(),
                             ranked.begin() + static_cast<std::ptrdiff_t>(final_candidate_count),
                             ranked.end(),
                             cmp);
            ranked.resize(static_cast<size_t>(final_candidate_count));
        }
        std::sort(ranked.begin(), ranked.end(), cmp);

        auto& out = result[static_cast<size_t>(node)];
        out.reserve(ranked.size());
        for (const auto& item : ranked) {
            out.push_back(item.second);
        }

        if (out.size() < static_cast<size_t>(final_candidate_count)) {
            for (int other : geometric[static_cast<size_t>(node)]) {
                if (std::find(out.begin(), out.end(), other) == out.end()) {
                    out.push_back(other);
                    if (out.size() >= static_cast<size_t>(final_candidate_count)) {
                        break;
                    }
                }
            }
        }
    }

    NormalizeCandidateSymmetryV5(result, static_cast<size_t>(std::max(final_candidate_count + 2, final_candidate_count)));
    return result;
}

template <typename DistanceFn>
std::vector<int> CollectConstructionCandidatesV5(int from,
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

    const size_t limit = std::min(static_cast<size_t>(std::max(0, fallback_limit - static_cast<int>(candidates.size()))),
                                  fallback.size());
    TrimNearestV5(fallback, limit);

    for (const auto& [_, city] : fallback) {
        candidates.push_back(city);
    }
    return candidates;
}

template <typename DistanceFn>
double ForwardPotentialV5(int node,
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

template <typename DistanceFn>
bool ApplyFirstImproving2OptV5(std::vector<int>& route,
                               const CandidateSets& candidate_sets,
                               DistanceFn& distance,
                               RouteIndexV5& index,
                               std::vector<char>& dont_look,
                               SearchBudgetV5& budget) {
    if (route.size() <= 4) {
        return false;
    }

    index.Build(route);
    for (size_t i = 1; i + 2 < route.size(); ++i) {
        if (budget.ShouldStop()) {
            return false;
        }
        const int t1 = route[i - 1];
        const int t2 = route[i];
        if (dont_look[static_cast<size_t>(t1)]) {
            continue;
        }

        bool improved = false;
        for (int t3 : candidate_sets[static_cast<size_t>(t1)]) {
            const int j = index.Get(t3);
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
                improved = true;
                break;
            }
        }

        if (improved) {
            return true;
        }
        dont_look[static_cast<size_t>(t1)] = 1;
    }
    return false;
}

template <typename DistanceFn>
void QuickRouteCleanupV5(std::vector<int>& route,
                         const CandidateSets& candidate_sets,
                         DistanceFn& distance,
                         int node_count,
                         int max_moves,
                         SearchBudgetV5& budget) {
    if (route.size() <= 4 || max_moves <= 0) {
        return;
    }

    RouteIndexV5 index(node_count);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);
    for (int move = 0; move < max_moves && !budget.ShouldStop(); ++move) {
        if (!ApplyFirstImproving2OptV5(route, candidate_sets, distance, index, dont_look, budget)) {
            return;
        }
    }
}

template <typename DistanceFn>
void ImproveRouteGuidedV5(std::vector<int>& route,
                          const CandidateSets& candidate_sets,
                          DistanceFn& distance,
                          int node_count,
                          SearchBudgetV5& budget) {
    if (route.size() <= 4) {
        return;
    }

    RouteIndexV5 index(node_count);
    std::vector<char> dont_look(static_cast<size_t>(node_count), 0);
    while (!budget.ShouldStop()) {
        if (ApplyFirstImproving2OptV5(route, candidate_sets, distance, index, dont_look, budget)) {
            continue;
        }
        if (route.size() > 4) {
            std::vector<int> reversed = route;
            std::reverse(reversed.begin() + 1, reversed.end() - 1);
            std::fill(dont_look.begin(), dont_look.end(), 0);
            if (ApplyFirstImproving2OptV5(reversed, candidate_sets, distance, index, dont_look, budget)) {
                route.swap(reversed);
                continue;
            }
        }
        break;
    }
}

template <typename DistanceFn>
void DoubleBridgeKickV5(std::vector<int>& route, std::mt19937& rng, const DistanceFn&) {
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

template <typename DistanceFn>
void IteratedLocalSearchV5(std::vector<int>& route,
                           std::mt19937& rng,
                           int rounds,
                           const CandidateSets& candidate_sets,
                           int node_count,
                           DistanceFn& distance,
                           SearchBudgetV5& budget) {
    ImproveRouteGuidedV5(route, candidate_sets, distance, node_count, budget);
    if (budget.ShouldStop()) {
        return;
    }

    double best_length = RouteLengthGenericV5(route, distance);
    std::vector<int> best = route;
    for (int round = 0; round < rounds && !budget.ShouldStop(); ++round) {
        std::vector<int> candidate = best;
        DoubleBridgeKickV5(candidate, rng, distance);
        ImproveRouteGuidedV5(candidate, candidate_sets, distance, node_count, budget);
        const double candidate_length = RouteLengthGenericV5(candidate, distance);
        if (candidate_length + kEps < best_length) {
            best_length = candidate_length;
            best.swap(candidate);
        }
    }
    route.swap(best);
}

template <typename DistanceFn>
double SwapDeltaClosedRoutesV5(const std::vector<int>& route_a,
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

template <typename DistanceFn>
void CompleteRemainingAssignmentsV5(mtsp::RouteSet& out,
                                    std::vector<int>& current,
                                    std::vector<int>& route_sizes,
                                    std::vector<char>& visited,
                                    int target_size,
                                    int hard_max_size,
                                    DistanceFn& distance) {
    for (int city = 1; city < mtsp::Instance::GetInstance().GetNodeCount(); ++city) {
        if (visited[static_cast<size_t>(city)]) {
            continue;
        }

        int best_salesman = 0;
        double best_score = std::numeric_limits<double>::max();
        for (size_t salesman = 0; salesman < out.size(); ++salesman) {
            const int size = route_sizes[salesman];
            const double overload = size >= hard_max_size ? 1e9 : 0.0;
            const double balance = 0.2 * std::max(0, size - target_size + 1);
            const double score = overload + distance(current[salesman], city) + balance;
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

class LkhWrapperSolverV5 : public Solver {
public:
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
        if (opts.count("reserve-budget-ms")) {
            reserve_budget_ms_ = std::max(0, std::stoi(opts.at("reserve-budget-ms")));
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
        if (opts.count("popmusic-solutions")) {
            popmusic_solutions_ = std::max(0, std::stoi(opts.at("popmusic-solutions")));
        }
        if (opts.count("popmusic-sample-size")) {
            popmusic_sample_size_ = std::max(8, std::stoi(opts.at("popmusic-sample-size")));
        }
        if (opts.count("popmusic-window")) {
            popmusic_window_ = std::max(8, std::stoi(opts.at("popmusic-window")));
        }
        if (opts.count("route-size-slack")) {
            route_size_slack_ = std::max(0.0, std::stod(opts.at("route-size-slack")));
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        SearchBudgetV5 budget(time_budget_ms_, reserve_budget_ms_);
        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const int effective_candidate_count = EffectiveCandidateCount(inst.GetNodeCount());
        const int geometric_candidate_count = EffectiveGeometricCandidateCount(inst.GetNodeCount(), effective_candidate_count);
        const int effective_rounds = EffectiveRounds(inst.GetNodeCount());
        const int effective_popmusic_solutions = EffectivePopmusicSolutions(inst.GetNodeCount());
        const int effective_popmusic_sample = EffectivePopmusicSampleSize(inst.GetNodeCount());
        const int effective_popmusic_window = EffectivePopmusicWindow(inst.GetNodeCount());

        const CandidateSets candidate_sets = BuildHybridCandidateSetsV5(inst,
                                                                        effective_candidate_count,
                                                                        geometric_candidate_count,
                                                                        exact_candidate_threshold_,
                                                                        effective_popmusic_solutions,
                                                                        effective_popmusic_sample,
                                                                        effective_popmusic_window,
                                                                        rng,
                                                                        distance,
                                                                        budget);

        BuildInitialRoutes(out, candidate_sets, effective_candidate_count, distance, budget);
        for (auto& route : out) {
            route.push_back(0);
        }

        for (auto& route : out) {
            if (budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV5(route, rng, effective_rounds, candidate_sets, inst.GetNodeCount(), distance, budget);
        }

        if (!budget.ShouldStop()) {
            ImproveInterRoute(out, candidate_sets, effective_rounds, distance, budget);
        }
    }

private:
    int EffectiveCandidateCount(int node_count) const {
        if (node_count >= 100'000) {
            return std::max(8, std::min(candidate_count_, 10));
        }
        if (node_count >= 50'000) {
            return std::max(10, std::min(candidate_count_, 12));
        }
        if (node_count >= 20'000) {
            return std::max(10, std::min(candidate_count_, 14));
        }
        if (node_count >= 5'000) {
            return std::max(10, candidate_count_);
        }
        return candidate_count_;
    }

    int EffectiveGeometricCandidateCount(int node_count, int final_candidate_count) const {
        if (node_count >= 100'000) {
            return std::max(final_candidate_count * 2, 20);
        }
        if (node_count >= 20'000) {
            return std::max(final_candidate_count * 2, 18);
        }
        return std::max(final_candidate_count + 6, final_candidate_count * 2);
    }

    int EffectiveRounds(int node_count) const {
        if (node_count >= 100'000) {
            return std::max(4, rounds_ / 2);
        }
        if (node_count >= 50'000) {
            return std::max(5, (rounds_ * 2) / 3);
        }
        if (node_count >= 20'000) {
            return std::max(6, rounds_);
        }
        return rounds_;
    }

    int EffectivePopmusicSolutions(int node_count) const {
        if (node_count >= 100'000) {
            return std::min(popmusic_solutions_, 8);
        }
        if (node_count >= 50'000) {
            return std::min(popmusic_solutions_, 10);
        }
        return popmusic_solutions_;
    }

    int EffectivePopmusicSampleSize(int node_count) const {
        if (node_count >= 100'000) {
            return std::max(popmusic_sample_size_, 48);
        }
        if (node_count >= 50'000) {
            return std::max(popmusic_sample_size_, 40);
        }
        return popmusic_sample_size_;
    }

    int EffectivePopmusicWindow(int node_count) const {
        if (node_count >= 100'000) {
            return std::max(popmusic_window_, 40);
        }
        if (node_count >= 50'000) {
            return std::max(popmusic_window_, 36);
        }
        return popmusic_window_;
    }

    void BuildInitialRoutes(RouteSet& out,
                            const CandidateSets& candidate_sets,
                            int effective_candidate_count,
                            DistanceOracleV5& distance,
                            SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        out.assign(static_cast<size_t>(inst.GetSalesmanCount()), std::vector<int>{0});

        std::vector<int> current(static_cast<size_t>(inst.GetSalesmanCount()), 0);
        std::vector<int> route_sizes(static_cast<size_t>(inst.GetSalesmanCount()), 0);
        std::vector<char> visited(static_cast<size_t>(inst.GetNodeCount()), 0);
        visited[0] = 1;

        const int target_size = std::max(1, (inst.GetNodeCount() - 1 + inst.GetSalesmanCount() - 1) / inst.GetSalesmanCount());
        const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack_))));
        int remaining = inst.GetNodeCount() - 1;

        while (remaining > 0) {
            if (budget.ShouldStop()) {
                CompleteRemainingAssignmentsV5(out, current, route_sizes, visited, target_size, hard_max_size, distance);
                return;
            }

            int best_salesman = -1;
            int best_city = -1;
            double best_score = std::numeric_limits<double>::max();
            double best_immediate = std::numeric_limits<double>::max();

            for (int salesman = 0; salesman < inst.GetSalesmanCount(); ++salesman) {
                const int size = route_sizes[static_cast<size_t>(salesman)];
                const bool hard_block = size >= hard_max_size;
                const std::vector<int> move_candidates = CollectConstructionCandidatesV5(
                    current[static_cast<size_t>(salesman)],
                    visited,
                    candidate_sets,
                    inst,
                    std::max(effective_candidate_count, 6),
                    distance);

                for (int city : move_candidates) {
                    const double immediate = distance(current[static_cast<size_t>(salesman)], city);
                    const double forward = ForwardPotentialV5(city, visited, candidate_sets, inst, distance);
                    const double depot = distance.DepotDistance(city);
                    const double overload_penalty = hard_block ? 1e9 : 0.0;
                    const double balance_penalty = 0.12 * std::max(0, size - target_size + 1);
                    const double score = overload_penalty + immediate + lookahead_weight_ * forward + depot_weight_ * depot + balance_penalty;

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
                CompleteRemainingAssignmentsV5(out, current, route_sizes, visited, target_size, hard_max_size, distance);
                return;
            }

            out[static_cast<size_t>(best_salesman)].push_back(best_city);
            current[static_cast<size_t>(best_salesman)] = best_city;
            ++route_sizes[static_cast<size_t>(best_salesman)];
            visited[static_cast<size_t>(best_city)] = 1;
            --remaining;
        }
    }

    void RunDeferredInterRouteIls(RouteSet& routes,
                                  std::vector<char>& dirty_routes,
                                  const CandidateSets& candidate_sets,
                                  int effective_rounds,
                                  DistanceOracleV5& distance,
                                  SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        for (size_t route_idx = 0; route_idx < routes.size(); ++route_idx) {
            if (!dirty_routes[route_idx] || budget.ShouldStop()) {
                continue;
            }
            IteratedLocalSearchV5(routes[route_idx],
                                  local_rng_,
                                  std::max(2, effective_rounds / 2),
                                  candidate_sets,
                                  inst.GetNodeCount(),
                                  distance,
                                  budget);
            dirty_routes[route_idx] = 0;
        }
    }

    void ImproveInterRoute(RouteSet& routes,
                           const CandidateSets& candidate_sets,
                           int effective_rounds,
                           DistanceOracleV5& distance,
                           SearchBudgetV5& budget) const {
        const Instance& inst = Instance::GetInstance();
        std::vector<char> dirty_routes(routes.size(), 0);
        std::vector<RouteIndexV5> indices;
        indices.reserve(routes.size());
        for (size_t i = 0; i < routes.size(); ++i) {
            indices.emplace_back(inst.GetNodeCount());
        }

        int accepted_since_ils = 0;
        bool improved = true;
        while (improved && !budget.ShouldStop()) {
            improved = false;
            for (size_t a = 0; a < routes.size() && !improved; ++a) {
                indices[a].Build(routes[a]);
                for (size_t b = a + 1; b < routes.size() && !improved; ++b) {
                    indices[b].Build(routes[b]);
                    for (size_t i = 1; i + 1 < routes[a].size() && !improved; ++i) {
                        if (budget.ShouldStop()) {
                            break;
                        }
                        const int city_a = routes[a][i];
                        for (int city_b : candidate_sets[static_cast<size_t>(city_a)]) {
                            const int j = indices[b].Get(city_b);
                            if (j <= 0 || j + 1 >= static_cast<int>(routes[b].size())) {
                                continue;
                            }
                            const double delta = SwapDeltaClosedRoutesV5(routes[a], i, routes[b], static_cast<size_t>(j), distance);
                            if (delta >= -kEps) {
                                continue;
                            }

                            std::swap(routes[a][i], routes[b][static_cast<size_t>(j)]);
                            QuickRouteCleanupV5(routes[a], candidate_sets, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                            QuickRouteCleanupV5(routes[b], candidate_sets, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);

                            dirty_routes[a] = 1;
                            dirty_routes[b] = 1;
                            ++accepted_since_ils;

                            if (accepted_since_ils >= inter_route_batch_ && !budget.ShouldStop()) {
                                RunDeferredInterRouteIls(routes, dirty_routes, candidate_sets, effective_rounds, distance, budget);
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
    int reserve_budget_ms_ = 20'000;
    int guided_cleanup_passes_ = 2;
    int inter_route_batch_ = 4;
    int exact_candidate_threshold_ = 512;
    int popmusic_solutions_ = 12;
    int popmusic_sample_size_ = 32;
    int popmusic_window_ = 32;
    double route_size_slack_ = 0.15;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v5 = (SolverFactory::RegisterSolver("lkh-wrapper-v5", []() {
    return std::make_unique<LkhWrapperSolverV5>();
}),
                               true);

} // namespace mtsp
