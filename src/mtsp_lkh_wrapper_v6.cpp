// Aggressive-speed variant pushing v5's trade-offs further. Documented in
// report 5.5 as failing to maintain validity on parts of the n=25k/50k
// uniform sweep (~25% valid runs); kept in the codebase as a negative result
// — illustrates the cost of trading too much quality for throughput.
// Registered as "lkh-wrapper-v6".

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

class DistanceOracleV5 {
public:
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

struct RouteIndexV5 {
    explicit RouteIndexV5(int node_count) : position(static_cast<size_t>(node_count), -1), seen(static_cast<size_t>(node_count), 0) {}

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



struct ClusterInfoV6 {
    std::vector<int> members;
    Coord centroid{0.0, 0.0};
    double radius = 0.0;
    double avg_dist = 0.0;
    double std_dist = 0.0;
    double depot_distance = 0.0;
    double estimate = 0.0;
    int outlier_count = 0;
};

struct ClusterModelV6 {
    std::vector<int> node_to_cluster;
    std::vector<char> is_outlier;
    std::vector<ClusterInfoV6> clusters;
};

double DistanceCoordToPointV6(const Coord& lhs, const Coord& rhs) {
    return std::sqrt(SquaredDistanceCoordsV5(lhs, rhs));
}

int DesiredClusterCountV6(int node_count, int salesman_count) {
    const int min_clusters = std::max(4 * salesman_count, 1);
    const int max_clusters = std::max(12 * salesman_count, min_clusters);
    int desired = 6 * salesman_count + std::max(0, node_count / 12000);
    desired = std::clamp(desired, min_clusters, max_clusters);
    desired = std::min(desired, std::max(1, node_count - 1));
    return desired;
}

ClusterModelV6 BuildLightweightClustersV6(const mtsp::Instance& inst,
                                          int desired_clusters,
                                          std::mt19937& rng,
                                          SearchBudgetV5& budget) {
    const auto& coords = inst.GetCoords();
    const int node_count = inst.GetNodeCount();
    ClusterModelV6 model;
    model.node_to_cluster.assign(static_cast<size_t>(node_count), -1);
    model.is_outlier.assign(static_cast<size_t>(node_count), 0);
    if (node_count <= 1) {
        return model;
    }

    const int k = std::max(1, std::min(desired_clusters, node_count - 1));
    std::vector<int> cities;
    cities.reserve(static_cast<size_t>(node_count - 1));
    for (int city = 1; city < node_count; ++city) {
        cities.push_back(city);
    }
    std::shuffle(cities.begin(), cities.end(), rng);

    std::vector<Coord> centroids;
    centroids.reserve(static_cast<size_t>(k));
    for (int i = 0; i < k; ++i) {
        centroids.push_back(coords[static_cast<size_t>(cities[static_cast<size_t>(i % cities.size())])]);
    }

    std::vector<int> assign(static_cast<size_t>(node_count), -1);
    const int iterations = node_count >= 100000 ? 4 : 5;
    for (int it = 0; it < iterations && !budget.ShouldStop(); ++it) {
        std::vector<double> sum_x(static_cast<size_t>(k), 0.0);
        std::vector<double> sum_y(static_cast<size_t>(k), 0.0);
        std::vector<int> count(static_cast<size_t>(k), 0);

        for (int city = 1; city < node_count; ++city) {
            double best = std::numeric_limits<double>::max();
            int best_cluster = 0;
            for (int c = 0; c < k; ++c) {
                const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], centroids[static_cast<size_t>(c)]);
                if (d2 + kEps < best) {
                    best = d2;
                    best_cluster = c;
                }
            }
            assign[static_cast<size_t>(city)] = best_cluster;
            sum_x[static_cast<size_t>(best_cluster)] += coords[static_cast<size_t>(city)].first;
            sum_y[static_cast<size_t>(best_cluster)] += coords[static_cast<size_t>(city)].second;
            ++count[static_cast<size_t>(best_cluster)];
        }

        for (int c = 0; c < k; ++c) {
            if (count[static_cast<size_t>(c)] == 0) {
                centroids[static_cast<size_t>(c)] = coords[static_cast<size_t>(cities[static_cast<size_t>(c % cities.size())])];
            } else {
                centroids[static_cast<size_t>(c)] = {
                    sum_x[static_cast<size_t>(c)] / count[static_cast<size_t>(c)],
                    sum_y[static_cast<size_t>(c)] / count[static_cast<size_t>(c)]
                };
            }
        }
    }

    std::vector<ClusterInfoV6> raw(static_cast<size_t>(k));
    for (int c = 0; c < k; ++c) {
        raw[static_cast<size_t>(c)].centroid = centroids[static_cast<size_t>(c)];
    }
    for (int city = 1; city < node_count; ++city) {
        const int cluster = assign[static_cast<size_t>(city)];
        if (cluster >= 0) {
            raw[static_cast<size_t>(cluster)].members.push_back(city);
        }
    }

    std::vector<int> remap(static_cast<size_t>(k), -1);
    for (int c = 0; c < k; ++c) {
        if (!raw[static_cast<size_t>(c)].members.empty()) {
            remap[static_cast<size_t>(c)] = static_cast<int>(model.clusters.size());
            model.clusters.push_back(std::move(raw[static_cast<size_t>(c)]));
        }
    }

    for (int city = 1; city < node_count; ++city) {
        const int old_cluster = assign[static_cast<size_t>(city)];
        model.node_to_cluster[static_cast<size_t>(city)] = remap[static_cast<size_t>(old_cluster)];
    }

    for (auto& cluster : model.clusters) {
        if (cluster.members.empty()) {
            continue;
        }
        double sum = 0.0;
        double sum_sq = 0.0;
        double max_dist = 0.0;
        for (int city : cluster.members) {
            const double d = DistanceCoordToPointV6(coords[static_cast<size_t>(city)], cluster.centroid);
            sum += d;
            sum_sq += d * d;
            max_dist = std::max(max_dist, d);
        }
        cluster.radius = max_dist;
        cluster.avg_dist = sum / static_cast<double>(cluster.members.size());
        const double mean_sq = sum_sq / static_cast<double>(cluster.members.size());
        cluster.std_dist = std::sqrt(std::max(0.0, mean_sq - cluster.avg_dist * cluster.avg_dist));
        cluster.depot_distance = DistanceCoordToPointV6(cluster.centroid, coords[0]);

        for (int city : cluster.members) {
            const double d = DistanceCoordToPointV6(coords[static_cast<size_t>(city)], cluster.centroid);
            if (cluster.members.size() <= 2 || d > cluster.avg_dist + 1.5 * cluster.std_dist + kEps) {
                model.is_outlier[static_cast<size_t>(city)] = 1;
                ++cluster.outlier_count;
            }
        }

        const double spread_term = cluster.radius * (1.0 + 0.15 * std::sqrt(static_cast<double>(cluster.members.size())));
        const double outlier_term = cluster.outlier_count * std::max(cluster.avg_dist, cluster.radius * 0.5);
        cluster.estimate = 2.0 * cluster.depot_distance + spread_term + 0.35 * outlier_term;
    }

    return model;
}

void AddUniqueCandidateV6(CandidateSets& sets, int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(sets.size()) || to >= static_cast<int>(sets.size()) || from == to) {
        return;
    }
    auto& vec = sets[static_cast<size_t>(from)];
    if (std::find(vec.begin(), vec.end(), to) == vec.end()) {
        vec.push_back(to);
    }
}

void ReorderCandidatesByDistanceV6(CandidateSets& sets, const std::vector<Coord>& coords, int max_per_node) {
    for (size_t from = 0; from < sets.size(); ++from) {
        auto& vec = sets[from];
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        std::sort(vec.begin(), vec.end(), [&](int lhs, int rhs) {
            const double dl = SquaredDistanceCoordsV5(coords[from], coords[static_cast<size_t>(lhs)]);
            const double dr = SquaredDistanceCoordsV5(coords[from], coords[static_cast<size_t>(rhs)]);
            if (std::abs(dl - dr) > kEps) {
                return dl < dr;
            }
            return lhs < rhs;
        });
        if (static_cast<int>(vec.size()) > max_per_node) {
            vec.resize(static_cast<size_t>(max_per_node));
        }
    }
}

void AugmentCandidatesWithClusterBridgesV6(CandidateSets& global_sets,
                                           const mtsp::Instance& inst,
                                           const ClusterModelV6& model,
                                           int max_per_node) {
    const auto& coords = inst.GetCoords();
    if (model.clusters.empty()) {
        ReorderCandidatesByDistanceV6(global_sets, coords, max_per_node);
        return;
    }

    const int cluster_count = static_cast<int>(model.clusters.size());
    std::vector<std::vector<int>> nearest_clusters(static_cast<size_t>(cluster_count));
    for (int c = 0; c < cluster_count; ++c) {
        std::vector<std::pair<double, int>> ranked;
        ranked.reserve(static_cast<size_t>(std::max(0, cluster_count - 1)));
        for (int d = 0; d < cluster_count; ++d) {
            if (c == d) {
                continue;
            }
            ranked.emplace_back(SquaredDistanceCoordsV5(model.clusters[static_cast<size_t>(c)].centroid,
                                                        model.clusters[static_cast<size_t>(d)].centroid),
                                d);
        }
        TrimNearestV5(ranked, 2);
        for (const auto& [_, d] : ranked) {
            nearest_clusters[static_cast<size_t>(c)].push_back(d);
        }
    }

    auto representative_towards = [&](int cluster_from, const Coord& target) {
        int best_city = model.clusters[static_cast<size_t>(cluster_from)].members.front();
        double best = std::numeric_limits<double>::max();
        for (int city : model.clusters[static_cast<size_t>(cluster_from)].members) {
            const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], target);
            if (d2 + kEps < best) {
                best = d2;
                best_city = city;
            }
        }
        return best_city;
    };

    for (int c = 0; c < cluster_count; ++c) {
        for (int d : nearest_clusters[static_cast<size_t>(c)]) {
            const int rep_c = representative_towards(c, model.clusters[static_cast<size_t>(d)].centroid);
            const int rep_d = representative_towards(d, model.clusters[static_cast<size_t>(c)].centroid);
            AddUniqueCandidateV6(global_sets, rep_c, rep_d);
            AddUniqueCandidateV6(global_sets, rep_d, rep_c);
        }
    }

    for (int city = 1; city < inst.GetNodeCount(); ++city) {
        if (!model.is_outlier[static_cast<size_t>(city)]) {
            continue;
        }
        AddUniqueCandidateV6(global_sets, city, 0);
        AddUniqueCandidateV6(global_sets, 0, city);
        const int cluster = model.node_to_cluster[static_cast<size_t>(city)];
        if (cluster >= 0) {
            for (int d : nearest_clusters[static_cast<size_t>(cluster)]) {
                const int bridge = representative_towards(d, coords[static_cast<size_t>(city)]);
                AddUniqueCandidateV6(global_sets, city, bridge);
                AddUniqueCandidateV6(global_sets, bridge, city);
            }
        }
    }

    ReorderCandidatesByDistanceV6(global_sets, coords, max_per_node);
}

CandidateSets BuildLocalCandidatesFromGlobalV6(const CandidateSets& global_sets, int local_candidate_count) {
    CandidateSets local = global_sets;
    for (auto& vec : local) {
        if (static_cast<int>(vec.size()) > local_candidate_count) {
            vec.resize(static_cast<size_t>(local_candidate_count));
        }
    }
    return local;
}

std::vector<int> OrderClusterNodesV6(const ClusterInfoV6& cluster,
                                     const Coord& entry_anchor,
                                     const Coord& exit_anchor,
                                     const std::vector<Coord>& coords,
                                     std::mt19937& rng) {
    if (cluster.members.empty()) {
        return {};
    }
    if (cluster.members.size() == 1) {
        return cluster.members;
    }

    int entry_city = cluster.members.front();
    double best_entry = std::numeric_limits<double>::max();
    for (int city : cluster.members) {
        const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], entry_anchor);
        if (d2 + kEps < best_entry) {
            best_entry = d2;
            entry_city = city;
        }
    }

    std::vector<std::pair<double, int>> angular;
    angular.reserve(cluster.members.size());
    for (int city : cluster.members) {
        const auto& p = coords[static_cast<size_t>(city)];
        const double angle = std::atan2(p.second - cluster.centroid.second, p.first - cluster.centroid.first);
        angular.emplace_back(angle, city);
    }
    std::sort(angular.begin(), angular.end(), [](const auto& lhs, const auto& rhs) {
        if (std::abs(lhs.first - rhs.first) > kEps) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });

    std::vector<int> cyclic;
    cyclic.reserve(angular.size());
    for (const auto& [_, city] : angular) {
        cyclic.push_back(city);
    }
    auto it = std::find(cyclic.begin(), cyclic.end(), entry_city);
    if (it != cyclic.end()) {
        std::rotate(cyclic.begin(), it, cyclic.end());
    }

    std::vector<int> forward = cyclic;
    std::vector<int> backward;
    backward.reserve(cyclic.size());
    backward.push_back(entry_city);
    for (auto rit = cyclic.rbegin(); rit != cyclic.rend() - 1; ++rit) {
        backward.push_back(*rit);
    }

    auto tail_distance = [&](const std::vector<int>& order) {
        return SquaredDistanceCoordsV5(coords[static_cast<size_t>(order.back())], exit_anchor);
    };

    const double forward_score = tail_distance(forward);
    const double backward_score = tail_distance(backward);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    if (prob(rng) < 0.2) {
        return forward_score <= backward_score ? backward : forward;
    }
    return forward_score <= backward_score ? forward : backward;
}

double OpenRouteLengthV6(const std::vector<int>& route, DistanceOracleV5& distance) {
    if (route.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        total += distance(route[i - 1], route[i]);
    }
    total += distance(route.back(), 0);
    return total;
}

void BuildInitialRoutesClusterAwareV6(mtsp::RouteSet& out,
                                      const ClusterModelV6& model,
                                      const mtsp::Instance& inst,
                                      DistanceOracleV5& distance,
                                      SearchBudgetV5& budget,
                                      std::mt19937& rng,
                                      double route_size_slack) {
    const int m = inst.GetSalesmanCount();
    out.assign(static_cast<size_t>(m), std::vector<int>{0});
    if (model.clusters.empty()) {
        return;
    }

    const int target_size = std::max(1, (inst.GetNodeCount() - 1 + m - 1) / m);
    const int hard_max_size = std::max(target_size, static_cast<int>(std::ceil(target_size * (1.0 + route_size_slack))));

    std::vector<int> cluster_ids(model.clusters.size());
    std::iota(cluster_ids.begin(), cluster_ids.end(), 0);
    std::sort(cluster_ids.begin(), cluster_ids.end(), [&](int lhs, int rhs) {
        if (std::abs(model.clusters[static_cast<size_t>(lhs)].estimate - model.clusters[static_cast<size_t>(rhs)].estimate) > kEps) {
            return model.clusters[static_cast<size_t>(lhs)].estimate > model.clusters[static_cast<size_t>(rhs)].estimate;
        }
        return lhs < rhs;
    });

    std::vector<std::vector<int>> assigned_clusters(static_cast<size_t>(m));
    std::vector<double> route_load(static_cast<size_t>(m), 0.0);
    std::vector<int> route_nodes(static_cast<size_t>(m), 0);
    std::vector<int> route_outliers(static_cast<size_t>(m), 0);

    for (int cluster_id : cluster_ids) {
        if (budget.ShouldStop()) {
            break;
        }
        const auto& cluster = model.clusters[static_cast<size_t>(cluster_id)];
        int best_route = 0;
        double best_score = std::numeric_limits<double>::max();
        for (int s = 0; s < m; ++s) {
            const double projected_load = route_load[static_cast<size_t>(s)] + cluster.estimate;
            double projected_max = projected_load;
            for (int t = 0; t < m; ++t) {
                if (t == s) continue;
                projected_max = std::max(projected_max, route_load[static_cast<size_t>(t)]);
            }
            const int projected_nodes = route_nodes[static_cast<size_t>(s)] + static_cast<int>(cluster.members.size());
            const double balance_penalty = 0.35 * std::max(0, projected_nodes - target_size);
            const double hard_penalty = projected_nodes > hard_max_size ? 1e9 : 0.0;
            const double outlier_penalty = 0.8 * (route_outliers[static_cast<size_t>(s)] + cluster.outlier_count);
            const double score = projected_max + balance_penalty + outlier_penalty + hard_penalty;
            if (score + kEps < best_score) {
                best_score = score;
                best_route = s;
            }
        }
        assigned_clusters[static_cast<size_t>(best_route)].push_back(cluster_id);
        route_load[static_cast<size_t>(best_route)] += cluster.estimate;
        route_nodes[static_cast<size_t>(best_route)] += static_cast<int>(cluster.members.size());
        route_outliers[static_cast<size_t>(best_route)] += cluster.outlier_count;
    }

    std::uniform_real_distribution<double> prob(0.0, 1.0);
    const auto& coords = inst.GetCoords();
    for (int s = 0; s < m; ++s) {
        auto remaining = assigned_clusters[static_cast<size_t>(s)];
        Coord current_anchor = coords[0];
        while (!remaining.empty()) {
            if (budget.ShouldStop()) {
                break;
            }
            std::vector<std::pair<double, int>> ranked;
            ranked.reserve(remaining.size());
            for (int cluster_id : remaining) {
                const auto& cluster = model.clusters[static_cast<size_t>(cluster_id)];
                const double bridge = DistanceCoordToPointV6(current_anchor, cluster.centroid);
                const double score = bridge + 0.12 * cluster.depot_distance + 0.05 * cluster.estimate;
                ranked.emplace_back(score, cluster_id);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
                if (std::abs(lhs.first - rhs.first) > kEps) {
                    return lhs.first < rhs.first;
                }
                return lhs.second < rhs.second;
            });

            int chosen_cluster = ranked.front().second;
            if (ranked.size() >= 2 && prob(rng) < 0.2) {
                const int limit = std::min<int>(3, ranked.size());
                std::uniform_int_distribution<int> pick(0, limit - 1);
                chosen_cluster = ranked[static_cast<size_t>(pick(rng))].second;
            }

            const auto it_remaining = std::find(remaining.begin(), remaining.end(), chosen_cluster);
            if (it_remaining != remaining.end()) {
                remaining.erase(it_remaining);
            }

            const Coord next_anchor = remaining.empty()
                ? coords[0]
                : model.clusters[static_cast<size_t>(remaining.front())].centroid;
            std::vector<int> block = OrderClusterNodesV6(model.clusters[static_cast<size_t>(chosen_cluster)],
                                                         current_anchor,
                                                         next_anchor,
                                                         coords,
                                                         rng);
            for (int city : block) {
                out[static_cast<size_t>(s)].push_back(city);
            }
            if (!block.empty()) {
                current_anchor = coords[static_cast<size_t>(block.back())];
            }
        }
    }
}

std::vector<double> ComputeOpenRouteLengthsV6(const mtsp::RouteSet& routes, DistanceOracleV5& distance) {
    std::vector<double> lengths(routes.size(), 0.0);
    for (size_t r = 0; r < routes.size(); ++r) {
        lengths[r] = OpenRouteLengthV6(routes[r], distance);
    }
    return lengths;
}

double InternalBlockLengthV6(const std::vector<int>& route, size_t begin, size_t end, DistanceOracleV5& distance) {
    double total = 0.0;
    for (size_t i = begin + 1; i <= end; ++i) {
        total += distance(route[i - 1], route[i]);
    }
    return total;
}

void RebalanceOpenRoutesClusterAwareV6(mtsp::RouteSet& routes,
                                       const ClusterModelV6& model,
                                       DistanceOracleV5& distance,
                                       SearchBudgetV5& budget,
                                       int max_passes) {
    if (routes.empty()) {
        return;
    }
    auto route_lengths = ComputeOpenRouteLengthsV6(routes, distance);
    for (int pass = 0; pass < max_passes && !budget.ShouldStop(); ++pass) {
        const auto longest_it = std::max_element(route_lengths.begin(), route_lengths.end());
        const size_t from_route = static_cast<size_t>(std::distance(route_lengths.begin(), longest_it));
        const double old_max = *longest_it;
        bool improved = false;

        for (size_t i = 1; i < routes[from_route].size() && !improved; ) {
            const int cluster = model.node_to_cluster[static_cast<size_t>(routes[from_route][i])];
            size_t j = i;
            while (j + 1 < routes[from_route].size() &&
                   model.node_to_cluster[static_cast<size_t>(routes[from_route][j + 1])] == cluster) {
                ++j;
            }

            const int prev = routes[from_route][i - 1];
            const int first = routes[from_route][i];
            const int last = routes[from_route][j];
            const bool at_end = (j + 1 == routes[from_route].size());
            const int next = at_end ? 0 : routes[from_route][j + 1];
            const double block_len = InternalBlockLengthV6(routes[from_route], i, j, distance);
            const double removed = distance(prev, first) + block_len + distance(last, next) - distance(prev, next);
            const double new_from_len = route_lengths[from_route] - removed;

            size_t best_target = routes.size();
            double best_new_max = old_max;
            for (size_t to_route = 0; to_route < routes.size(); ++to_route) {
                if (to_route == from_route) {
                    continue;
                }
                const int tail = routes[to_route].back();
                const double added = distance(tail, first) + block_len + distance(last, 0) - distance(tail, 0);
                const double new_to_len = route_lengths[to_route] + added;
                double new_max = std::max(new_from_len, new_to_len);
                for (size_t r = 0; r < routes.size(); ++r) {
                    if (r != from_route && r != to_route) {
                        new_max = std::max(new_max, route_lengths[r]);
                    }
                }
                if (new_max + kEps < best_new_max) {
                    best_new_max = new_max;
                    best_target = to_route;
                }
            }

            if (best_target != routes.size()) {
                std::vector<int> block(routes[from_route].begin() + static_cast<std::ptrdiff_t>(i),
                                       routes[from_route].begin() + static_cast<std::ptrdiff_t>(j + 1));
                routes[from_route].erase(routes[from_route].begin() + static_cast<std::ptrdiff_t>(i),
                                         routes[from_route].begin() + static_cast<std::ptrdiff_t>(j + 1));
                routes[best_target].insert(routes[best_target].end(), block.begin(), block.end());
                route_lengths = ComputeOpenRouteLengthsV6(routes, distance);
                improved = true;
                break;
            }
            i = j + 1;
        }

        if (!improved) {
            break;
        }
    }
}

double RouteLengthClosedV6(const std::vector<int>& route, DistanceOracleV5& distance) {
    return RouteLengthGenericV5(route, distance);
}

bool TryBalancedRelocateV6(mtsp::RouteSet& routes,
                           const CandidateSets& global_candidates,
                           DistanceOracleV5& distance,
                           SearchBudgetV5& budget) {
    if (routes.size() < 2) {
        return false;
    }
    std::vector<double> lengths(routes.size(), 0.0);
    for (size_t r = 0; r < routes.size(); ++r) {
        lengths[r] = RouteLengthClosedV6(routes[r], distance);
    }
    const auto longest_it = std::max_element(lengths.begin(), lengths.end());
    const size_t from_route = static_cast<size_t>(std::distance(lengths.begin(), longest_it));
    const double old_max = *longest_it;
    const double old_sum = std::accumulate(lengths.begin(), lengths.end(), 0.0);

    std::vector<RouteIndexV5> indices;
    indices.reserve(routes.size());
    for (size_t r = 0; r < routes.size(); ++r) {
        indices.emplace_back(static_cast<int>(global_candidates.size()));
        indices.back().Build(routes[r]);
    }

    size_t best_i = 0;
    size_t best_to = routes.size();
    size_t best_after = 0;
    double best_score = 0.0;
    bool found = false;

    for (size_t i = 1; i + 1 < routes[from_route].size() && !budget.ShouldStop(); ++i) {
        const int city = routes[from_route][i];
        const int prev = routes[from_route][i - 1];
        const int next = routes[from_route][i + 1];
        const double removal = distance(prev, city) + distance(city, next) - distance(prev, next);
        const double new_from = lengths[from_route] - removal;

        for (size_t to_route = 0; to_route < routes.size(); ++to_route) {
            if (to_route == from_route) {
                continue;
            }
            std::vector<size_t> positions;
            positions.push_back(0);
            for (int cand : global_candidates[static_cast<size_t>(city)]) {
                const int pos = indices[to_route].Get(cand);
                if (pos >= 0 && pos + 1 < static_cast<int>(routes[to_route].size())) {
                    positions.push_back(static_cast<size_t>(pos));
                }
            }
            positions.push_back(routes[to_route].size() - 2);
            std::sort(positions.begin(), positions.end());
            positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

            for (size_t after : positions) {
                const int a = routes[to_route][after];
                const int b = routes[to_route][after + 1];
                const double insert = distance(a, city) + distance(city, b) - distance(a, b);
                const double new_to = lengths[to_route] + insert;
                double new_max = std::max(new_from, new_to);
                for (size_t r = 0; r < routes.size(); ++r) {
                    if (r != from_route && r != to_route) {
                        new_max = std::max(new_max, lengths[r]);
                    }
                }
                const double new_sum = old_sum - removal + insert;
                const double score = (old_max - new_max) * 1000.0 + (old_sum - new_sum);
                if (score > best_score + kEps) {
                    best_score = score;
                    best_i = i;
                    best_to = to_route;
                    best_after = after;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return false;
    }

    const int city = routes[from_route][best_i];
    routes[from_route].erase(routes[from_route].begin() + static_cast<std::ptrdiff_t>(best_i));
    routes[best_to].insert(routes[best_to].begin() + static_cast<std::ptrdiff_t>(best_after + 1), city);
    return true;
}

namespace mtsp {

class LkhWrapperSolverV6 : public Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (opts.count("seed")) {
            seed_ = static_cast<unsigned int>(std::stoul(opts.at("seed")));
            local_rng_.seed(seed_ ^ 0xA511E9B3U);
        }
        if (opts.count("rounds")) {
            rounds_ = std::max(1, std::stoi(opts.at("rounds")));
        }
        if (opts.count("local-candidate-count")) {
            local_candidate_count_ = std::max(4, std::stoi(opts.at("local-candidate-count")));
        }
        if (opts.count("global-candidate-count")) {
            global_candidate_count_ = std::max(8, std::stoi(opts.at("global-candidate-count")));
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
        if (opts.count("cluster-relocate-passes")) {
            cluster_relocate_passes_ = std::max(0, std::stoi(opts.at("cluster-relocate-passes")));
        }
        if (opts.count("cluster-count")) {
            forced_cluster_count_ = std::max(0, std::stoi(opts.at("cluster-count")));
        }
        if (opts.count("lookahead-weight")) {
            lookahead_weight_ = std::stod(opts.at("lookahead-weight"));
        }
        if (opts.count("depot-weight")) {
            depot_weight_ = std::stod(opts.at("depot-weight"));
        }
    }

    void Solve(RouteSet& out) override {
        const Instance& inst = Instance::GetInstance();
        SearchBudgetV5 budget(time_budget_ms_, reserve_budget_ms_);
        DistanceOracleV5 distance(inst);
        std::mt19937 rng(seed_);

        const int cluster_count = forced_cluster_count_ > 0
            ? std::min(forced_cluster_count_, std::max(1, inst.GetNodeCount() - 1))
            : DesiredClusterCountV6(inst.GetNodeCount(), inst.GetSalesmanCount());
        const ClusterModelV6 cluster_model = BuildLightweightClustersV6(inst, cluster_count, rng, budget);

        const int effective_local_candidates = EffectiveLocalCandidateCount(inst.GetNodeCount());
        const int effective_global_candidates = EffectiveGlobalCandidateCount(inst.GetNodeCount(), effective_local_candidates);
        const int effective_geometric_candidates = EffectiveGeometricCandidateCount(inst.GetNodeCount(), effective_global_candidates);
        const int effective_rounds = EffectiveRounds(inst.GetNodeCount());
        const int effective_popmusic_solutions = EffectivePopmusicSolutions(inst.GetNodeCount());
        const int effective_popmusic_sample = EffectivePopmusicSampleSize(inst.GetNodeCount());
        const int effective_popmusic_window = EffectivePopmusicWindow(inst.GetNodeCount());

        CandidateSets global_candidates = BuildHybridCandidateSetsV5(inst,
                                                                     effective_global_candidates,
                                                                     effective_geometric_candidates,
                                                                     exact_candidate_threshold_,
                                                                     effective_popmusic_solutions,
                                                                     effective_popmusic_sample,
                                                                     effective_popmusic_window,
                                                                     rng,
                                                                     distance,
                                                                     budget);
        AugmentCandidatesWithClusterBridgesV6(global_candidates,
                                              inst,
                                              cluster_model,
                                              std::max(effective_global_candidates + 4, effective_global_candidates));
        CandidateSets local_candidates = BuildLocalCandidatesFromGlobalV6(global_candidates, effective_local_candidates);

        BuildInitialRoutesClusterAwareV6(out, cluster_model, inst, distance, budget, rng, route_size_slack_);
        if (!budget.ShouldStop()) {
            RebalanceOpenRoutesClusterAwareV6(out, cluster_model, distance, budget, cluster_relocate_passes_);
        }

        for (auto& route : out) {
            route.push_back(0);
        }

        for (auto& route : out) {
            if (budget.ShouldStop()) {
                break;
            }
            IteratedLocalSearchV5(route, rng, effective_rounds, local_candidates, inst.GetNodeCount(), distance, budget);
        }

        if (!budget.ShouldStop()) {
            ImproveInterRoute(out, local_candidates, global_candidates, effective_rounds, distance, budget);
        }
    }

private:
    int EffectiveLocalCandidateCount(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(local_candidate_count_, 6, 8);
        }
        if (node_count >= 50000) {
            return std::clamp(local_candidate_count_, 7, 8);
        }
        return std::clamp(local_candidate_count_, 8, 10);
    }

    int EffectiveGlobalCandidateCount(int node_count, int local_count) const {
        if (node_count >= 100000) {
            return std::max(12, std::max(global_candidate_count_, local_count + 4));
        }
        if (node_count >= 50000) {
            return std::max(13, std::max(global_candidate_count_, local_count + 5));
        }
        return std::max(14, std::max(global_candidate_count_, local_count + 6));
    }

    int EffectiveGeometricCandidateCount(int node_count, int final_candidate_count) const {
        if (node_count >= 100000) {
            return std::max(final_candidate_count * 2, 22);
        }
        if (node_count >= 50000) {
            return std::max(final_candidate_count * 2, 20);
        }
        return std::max(final_candidate_count * 2, 18);
    }

    int EffectiveRounds(int node_count) const {
        if (node_count >= 100000) {
            return std::clamp(rounds_, 2, 3);
        }
        if (node_count >= 50000) {
            return std::clamp(rounds_, 3, 4);
        }
        return std::clamp(rounds_, 4, 6);
    }

    int EffectivePopmusicSolutions(int node_count) const {
        if (node_count >= 100000) {
            return std::min(popmusic_solutions_, 8);
        }
        if (node_count >= 50000) {
            return std::min(popmusic_solutions_, 10);
        }
        return std::min(popmusic_solutions_, 12);
    }

    int EffectivePopmusicSampleSize(int node_count) const {
        if (node_count >= 100000) {
            return std::max(popmusic_sample_size_, 48);
        }
        if (node_count >= 50000) {
            return std::max(popmusic_sample_size_, 40);
        }
        return std::max(popmusic_sample_size_, 32);
    }

    int EffectivePopmusicWindow(int node_count) const {
        if (node_count >= 100000) {
            return std::max(popmusic_window_, 40);
        }
        if (node_count >= 50000) {
            return std::max(popmusic_window_, 36);
        }
        return std::max(popmusic_window_, 32);
    }

    void RunDeferredInterRouteIls(RouteSet& routes,
                                  std::vector<char>& dirty_routes,
                                  const CandidateSets& local_candidates,
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
                                  local_candidates,
                                  inst.GetNodeCount(),
                                  distance,
                                  budget);
            dirty_routes[route_idx] = 0;
        }
    }

    void ImproveInterRoute(RouteSet& routes,
                           const CandidateSets& local_candidates,
                           const CandidateSets& global_candidates,
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

            if (TryBalancedRelocateV6(routes, global_candidates, distance, budget)) {
                for (auto& route : routes) {
                    QuickRouteCleanupV5(route, local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                }
                std::fill(dirty_routes.begin(), dirty_routes.end(), 1);
                ++accepted_since_ils;
                improved = true;
            }

            for (size_t a = 0; a < routes.size() && !budget.ShouldStop(); ++a) {
                indices[a].Build(routes[a]);
            }

            for (size_t a = 0; a < routes.size() && !improved; ++a) {
                for (size_t b = a + 1; b < routes.size() && !improved; ++b) {
                    for (size_t i = 1; i + 1 < routes[a].size() && !improved; ++i) {
                        if (budget.ShouldStop()) {
                            break;
                        }
                        const int city_a = routes[a][i];
                        for (int city_b : global_candidates[static_cast<size_t>(city_a)]) {
                            const int j = indices[b].Get(city_b);
                            if (j <= 0 || j + 1 >= static_cast<int>(routes[b].size())) {
                                continue;
                            }
                            const double delta = SwapDeltaClosedRoutesV5(routes[a], i, routes[b], static_cast<size_t>(j), distance);
                            if (delta >= -kEps) {
                                continue;
                            }

                            std::swap(routes[a][i], routes[b][static_cast<size_t>(j)]);
                            QuickRouteCleanupV5(routes[a], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                            QuickRouteCleanupV5(routes[b], local_candidates, distance, inst.GetNodeCount(), guided_cleanup_passes_, budget);
                            dirty_routes[a] = 1;
                            dirty_routes[b] = 1;
                            ++accepted_since_ils;
                            improved = true;
                            break;
                        }
                    }
                }
            }

            if (accepted_since_ils >= inter_route_batch_ && !budget.ShouldStop()) {
                RunDeferredInterRouteIls(routes, dirty_routes, local_candidates, effective_rounds, distance, budget);
                accepted_since_ils = 0;
            }
        }

        if (accepted_since_ils > 0 && !budget.ShouldStop()) {
            RunDeferredInterRouteIls(routes, dirty_routes, local_candidates, effective_rounds, distance, budget);
        }
    }

    unsigned int seed_ = 42U;
    int rounds_ = 4;
    int local_candidate_count_ = 8;
    int global_candidate_count_ = 14;
    int time_budget_ms_ = 300000;
    int reserve_budget_ms_ = 20000;
    int guided_cleanup_passes_ = 2;
    int inter_route_batch_ = 3;
    int exact_candidate_threshold_ = 512;
    int popmusic_solutions_ = 12;
    int popmusic_sample_size_ = 32;
    int popmusic_window_ = 32;
    double route_size_slack_ = 0.15;
    int cluster_relocate_passes_ = 2;
    int forced_cluster_count_ = 0;
    double lookahead_weight_ = 0.35;
    double depot_weight_ = 0.12;
    mutable std::mt19937 local_rng_{1337U};
};

static bool reg_lkh_mtsp_v6 = (SolverFactory::RegisterSolver("lkh-wrapper-v6", []() {
    return std::make_unique<LkhWrapperSolverV6>();
}),
                               true);

} // namespace mtsp
