// v9/00_common.cpp — shared infrastructure for lkh-wrapper-v9 (inherits from v8).
// The key addition in v9 is the two-phase time limit: "first good solution within ~200s,
// then improvement within ~100s". This scheme was carried over to all subsequent versions.

namespace {

using CandidateSets = std::vector<std::vector<int>>;
using Coord = std::pair<double, double>;
using EdgeFreqMap = std::vector<std::unordered_map<int, int>>;

constexpr double kEps = 1e-9;
constexpr double kCoordEps = 1e-12;
constexpr long long kLargeInstanceDistancePairs = 4'000'000LL;

class SearchBudgetV5 {
public:
    SearchBudgetV5(int total_budget_ms, int reserve_budget_ms, int polling_interval = 32)
        : enabled_(total_budget_ms > 0),
          polling_interval_(std::max(1, polling_interval)),
          polls_until_check_(std::max(1, polling_interval)) {
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
        polls_until_check_ = polling_interval_;
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

    [[nodiscard]] int RemainingMs() const {
        if (!enabled_) {
            return std::numeric_limits<int>::max();
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline_) {
            return 0;
        }
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now).count());
    }

    [[nodiscard]] bool Enabled() const {
        return enabled_;
    }

private:
    bool enabled_ = false;
    std::chrono::steady_clock::time_point deadline_{};
    int polling_interval_ = 32;
    int polls_until_check_ = 32;
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

