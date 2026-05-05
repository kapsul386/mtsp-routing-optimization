#pragma once

// Solution validation and post-processing: SanitizeRoutes (last-resort fix
// for any structural anomaly), ValidateRoutes (boolean check), RouteSumLength
// / MaxRouteLength / ComputeLexCost (scalar evaluators used by accept
// policies and final reporting). Independent of RouteList — operates on raw
// RouteSet.

#include "00_types.hpp"
#include "02_distance.hpp"
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

namespace mtsp::v21 {

// Lightweight sanitizer: ensures every route has depot endpoints, every
// non-depot city in 1..n-1 appears exactly once across routes, and the route
// count equals m. If duplicates/missing cities are detected we reassign them
// to the shortest route (last resort — should not happen with correct ALNS).
inline void SanitizeRoutes(RouteSet& routes, const mtsp::Instance& inst) {
    const int n = inst.GetNodeCount();
    const int m = std::max(1, inst.GetSalesmanCount());
    if (static_cast<int>(routes.size()) < m) routes.resize(static_cast<size_t>(m), std::vector<int>{0, 0});
    if (static_cast<int>(routes.size()) > m) routes.resize(static_cast<size_t>(m));
    for (auto& r : routes) {
        if (r.empty()) { r = {0, 0}; continue; }
        if (r.front() != 0) r.insert(r.begin(), 0);
        if (r.size() < 2 || r.back() != 0) r.push_back(0);
    }
    std::vector<int> seen(static_cast<size_t>(n), 0);
    seen[0] = 1;
    for (auto& r : routes) {
        std::vector<int> cleaned;
        cleaned.reserve(r.size());
        cleaned.push_back(0);
        for (size_t i = 1; i + 1 < r.size(); ++i) {
            const int v = r[i];
            if (v <= 0 || v >= n) continue;
            if (seen[static_cast<size_t>(v)] == 1) continue;
            seen[static_cast<size_t>(v)] = 1;
            cleaned.push_back(v);
        }
        cleaned.push_back(0);
        r = std::move(cleaned);
    }
    // Append any missing cities to the shortest route.
    auto append_to_shortest = [&](int v) {
        size_t best = 0; size_t blen = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < routes.size(); ++i) if (routes[i].size() < blen) { blen = routes[i].size(); best = i; }
        routes[best].insert(routes[best].end() - 1, v);
    };
    for (int v = 1; v < n; ++v) if (!seen[static_cast<size_t>(v)]) {
        append_to_shortest(v);
        seen[static_cast<size_t>(v)] = 1;
    }
}

inline int CountEmptyRoutes(const RouteSet& routes) {
    int empty = 0;
    for (const auto& route : routes) if (route.size() <= 2) ++empty;
    return empty;
}

inline int RouteCustomerCount(const std::vector<int>& route) {
    return std::max(0, static_cast<int>(route.size()) - 2);
}

inline double RebalanceRawDistance(DistanceOracle& d, int a, int b) {
    return (a == b) ? 0.0 : d.Raw(a, b);
}

inline void CloseRoutesForValidation(RouteSet& routes) {
    for (auto& route : routes) {
        if (route.empty()) {
            route = {0, 0};
            continue;
        }
        if (route.front() != 0) route.insert(route.begin(), 0);
        if (route.size() < 2 || route.back() != 0) route.push_back(0);
    }
}

inline double RouteSumLengthForRebalance(const RouteSet& routes, DistanceOracle& d) {
    double total = 0.0;
    for (const auto& route : routes) {
        for (size_t i = 1; i < route.size(); ++i) {
            total += RebalanceRawDistance(d, route[i - 1], route[i]);
        }
    }
    return total;
}

// Fast fallback: while any route is empty, cut a short contiguous segment from
// a donor route and turn it into the empty route. Segment moves are a better
// MINSUM repair than single-customer moves because the segment's internal tour
// edges are preserved and only the two depot links plus donor shortcut matter.
inline bool FillEmptyRoutesByGreedySegments(RouteSet& routes, DistanceOracle& d, int max_segment_len) {
    struct SegmentMove {
        int donor = -1;
        int empty = -1;
        size_t pos = 0;
        int len = 0;
        double delta = std::numeric_limits<double>::max();
    };

    const int max_len = std::max(1, max_segment_len);
    const int safety_max = static_cast<int>(routes.size()) * 4;
    for (int iter = 0; iter < safety_max; ++iter) {
        int empty_r = -1;
        for (size_t r = 0; r < routes.size(); ++r) {
            if (routes[r].size() <= 2) { empty_r = static_cast<int>(r); break; }
        }
        if (empty_r < 0) break;

        SegmentMove best;
        best.empty = empty_r;
        for (size_t r = 0; r < routes.size(); ++r) {
            const auto& route = routes[r];
            const int customers = static_cast<int>(route.size()) - 2;
            if (customers <= 1) continue;
            const int lim = std::min(max_len, customers - 1);
            for (size_t i = 1; i + 1 < route.size(); ++i) {
                const int prev = route[i - 1];
                const int first = route[i];
                for (int len = 1; len <= lim && i + static_cast<size_t>(len) < route.size(); ++len) {
                    const int last = route[i + static_cast<size_t>(len) - 1];
                    const int next = route[i + static_cast<size_t>(len)];
                    const double donor_delta = RebalanceRawDistance(d, prev, next) -
                                               RebalanceRawDistance(d, prev, first) -
                                               RebalanceRawDistance(d, last, next);
                    const double inserted = d.DepotDistance(first) + d.DepotDistance(last);
                    const double delta = donor_delta + inserted;
                    if (delta < best.delta) {
                        best.donor = static_cast<int>(r);
                        best.pos = i;
                        best.len = len;
                        best.delta = delta;
                    }
                }
            }
        }
        if (best.donor < 0 || best.len <= 0) break;

        auto& donor = routes[static_cast<size_t>(best.donor)];
        std::vector<int> segment(donor.begin() + static_cast<std::ptrdiff_t>(best.pos),
                                 donor.begin() + static_cast<std::ptrdiff_t>(best.pos + best.len));
        donor.erase(donor.begin() + static_cast<std::ptrdiff_t>(best.pos),
                    donor.begin() + static_cast<std::ptrdiff_t>(best.pos + best.len));
        auto& target = routes[static_cast<size_t>(best.empty)];
        target.clear();
        target.push_back(0);
        target.insert(target.end(), segment.begin(), segment.end());
        target.push_back(0);
    }
    return CountEmptyRoutes(routes) == 0;
}

// One-route partition repair: split the largest donor route into contiguous
// depot-to-depot arcs using the cheapest cut edges. This is cheap and can beat
// satellite extraction when the single tour repeatedly passes close to depot.
inline bool FillEmptyRoutesByBestCuts(RouteSet& routes, DistanceOracle& d) {
    std::vector<int> empties;
    int donor = -1;
    int donor_customers = 0;
    for (size_t r = 0; r < routes.size(); ++r) {
        if (routes[r].size() <= 2) {
            empties.push_back(static_cast<int>(r));
            continue;
        }
        const int customers = RouteCustomerCount(routes[r]);
        if (customers > donor_customers) {
            donor_customers = customers;
            donor = static_cast<int>(r);
        }
    }
    const int need = static_cast<int>(empties.size());
    if (need <= 0) return true;
    if (donor < 0 || donor_customers <= need) return false;

    struct Cut {
        int pos = 0;  // route index after which to cut
        double delta = 0.0;
    };
    std::vector<Cut> cuts;
    cuts.reserve(static_cast<size_t>(donor_customers - 1));
    const std::vector<int> route = routes[static_cast<size_t>(donor)];
    for (int pos = 1; pos < donor_customers; ++pos) {
        const int a = route[static_cast<size_t>(pos)];
        const int b = route[static_cast<size_t>(pos + 1)];
        const double delta = d.DepotDistance(a) + d.DepotDistance(b) - RebalanceRawDistance(d, a, b);
        cuts.push_back(Cut{pos, delta});
    }
    if (static_cast<int>(cuts.size()) < need) return false;
    if (need < static_cast<int>(cuts.size())) {
        std::nth_element(cuts.begin(), cuts.begin() + need, cuts.end(),
                         [](const Cut& a, const Cut& b) { return a.delta < b.delta; });
    }
    cuts.resize(static_cast<size_t>(need));
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.pos < b.pos; });

    std::vector<std::pair<int, int>> segments;
    segments.reserve(static_cast<size_t>(need + 1));
    int start = 1;
    for (const auto& cut : cuts) {
        segments.emplace_back(start, cut.pos);
        start = cut.pos + 1;
    }
    segments.emplace_back(start, donor_customers);
    for (const auto& [l, r] : segments) if (l > r) return false;

    auto assign_segment = [&](int route_idx, int l, int r) {
        auto& target = routes[static_cast<size_t>(route_idx)];
        target.clear();
        target.push_back(0);
        for (int pos = l; pos <= r; ++pos) target.push_back(route[static_cast<size_t>(pos)]);
        target.push_back(0);
    };

    assign_segment(donor, segments[0].first, segments[0].second);
    for (int e = 0; e < need; ++e) {
        assign_segment(empties[static_cast<size_t>(e)], segments[static_cast<size_t>(e + 1)].first,
                       segments[static_cast<size_t>(e + 1)].second);
    }
    return CountEmptyRoutes(routes) == 0;
}

// Circular variant of best-cuts: the donor customer order is treated as a ring,
// so the original first/last customers are not forced to be route boundaries.
// For a fixed customer order this exactly minimizes the cost of splitting that
// ring into the required number of depot-to-depot routes.
inline bool FillEmptyRoutesByCircularBestCuts(RouteSet& routes, DistanceOracle& d) {
    std::vector<int> empties;
    int donor = -1;
    int donor_customers = 0;
    for (size_t r = 0; r < routes.size(); ++r) {
        if (routes[r].size() <= 2) {
            empties.push_back(static_cast<int>(r));
            continue;
        }
        const int customers = RouteCustomerCount(routes[r]);
        if (customers > donor_customers) {
            donor_customers = customers;
            donor = static_cast<int>(r);
        }
    }
    const int need = static_cast<int>(empties.size());
    const int route_count = need + 1;
    if (need <= 0) return true;
    if (donor < 0 || donor_customers < route_count) return false;

    const std::vector<int> route = routes[static_cast<size_t>(donor)];
    std::vector<int> customers;
    customers.reserve(static_cast<size_t>(donor_customers));
    for (int pos = 1; pos <= donor_customers; ++pos) customers.push_back(route[static_cast<size_t>(pos)]);

    struct Cut {
        int edge = 0;  // cuts between customers[edge] and customers[(edge+1)%c]
        double delta = 0.0;
    };
    const int c = static_cast<int>(customers.size());
    std::vector<Cut> cuts;
    cuts.reserve(static_cast<size_t>(c));
    for (int edge = 0; edge < c; ++edge) {
        const int a = customers[static_cast<size_t>(edge)];
        const int b = customers[static_cast<size_t>((edge + 1) % c)];
        const double delta = d.DepotDistance(a) + d.DepotDistance(b) - RebalanceRawDistance(d, a, b);
        cuts.push_back(Cut{edge, delta});
    }
    if (route_count < static_cast<int>(cuts.size())) {
        std::nth_element(cuts.begin(), cuts.begin() + route_count, cuts.end(),
                         [](const Cut& a, const Cut& b) { return a.delta < b.delta; });
    }
    cuts.resize(static_cast<size_t>(route_count));
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.edge < b.edge; });

    std::vector<std::vector<int>> segments;
    segments.reserve(static_cast<size_t>(route_count));
    for (int t = 0; t < route_count; ++t) {
        const int start_edge = cuts[static_cast<size_t>(t)].edge;
        const int end_edge = cuts[static_cast<size_t>((t + 1) % route_count)].edge;
        std::vector<int> segment;
        int pos = (start_edge + 1) % c;
        while (true) {
            segment.push_back(customers[static_cast<size_t>(pos)]);
            if (pos == end_edge) break;
            pos = (pos + 1) % c;
        }
        if (segment.empty()) return false;
        segments.push_back(std::move(segment));
    }

    auto assign_segment = [&](int route_idx, const std::vector<int>& segment) {
        auto& target = routes[static_cast<size_t>(route_idx)];
        target.clear();
        target.push_back(0);
        target.insert(target.end(), segment.begin(), segment.end());
        target.push_back(0);
    };
    assign_segment(donor, segments[0]);
    for (int e = 0; e < need; ++e) {
        assign_segment(empties[static_cast<size_t>(e)], segments[static_cast<size_t>(e + 1)]);
    }
    return CountEmptyRoutes(routes) == 0;
}

inline bool FillEmptyRoutesByIntervalDp(RouteSet& routes, DistanceOracle& d,
                                        int endpoint_pool = 3072,
                                        int interval_cap = 60000) {
    std::vector<int> empties;
    int donor = -1;
    int donor_customers = 0;
    for (size_t r = 0; r < routes.size(); ++r) {
        if (routes[r].size() <= 2) {
            empties.push_back(static_cast<int>(r));
            continue;
        }
        const int customers = RouteCustomerCount(routes[r]);
        if (customers > donor_customers) {
            donor_customers = customers;
            donor = static_cast<int>(r);
        }
    }
    const int need = static_cast<int>(empties.size());
    if (need <= 0) return true;
    if (donor < 0 || donor_customers <= need) return false;

    const std::vector<int> route = routes[static_cast<size_t>(donor)];
    const int c = donor_customers;
    const int pool = std::max(64, std::min(endpoint_pool, c));

    std::vector<int> starts;
    std::vector<int> ends;
    starts.reserve(static_cast<size_t>(pool * 2));
    ends.reserve(static_cast<size_t>(pool * 2));

    auto add_best_positions = [&](std::vector<int>& out, bool as_start, bool depot_only, int limit) {
        std::vector<std::pair<double, int>> scored;
        scored.reserve(static_cast<size_t>(c));
        for (int pos = 1; pos <= c; ++pos) {
            const int v = route[static_cast<size_t>(pos)];
            double score = d.DepotDistance(v);
            if (!depot_only) {
                if (as_start) {
                    const int prev = route[static_cast<size_t>(pos - 1)];
                    score -= RebalanceRawDistance(d, prev, v);
                } else {
                    const int next = route[static_cast<size_t>(pos + 1)];
                    score -= RebalanceRawDistance(d, v, next);
                }
            }
            scored.emplace_back(score, pos);
        }
        const int take = std::min(limit, static_cast<int>(scored.size()));
        if (take < static_cast<int>(scored.size())) {
            std::nth_element(scored.begin(), scored.begin() + take, scored.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
        }
        for (int i = 0; i < take; ++i) out.push_back(scored[static_cast<size_t>(i)].second);
    };

    add_best_positions(starts, true, false, pool);
    add_best_positions(ends, false, false, pool);
    add_best_positions(starts, true, true, std::max(64, pool / 2));
    add_best_positions(ends, false, true, std::max(64, pool / 2));
    auto uniq_positions = [](std::vector<int>& xs) {
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    };
    uniq_positions(starts);
    uniq_positions(ends);

    struct Interval {
        int start = 0;
        int end = 0;
        double delta = 0.0;
    };
    struct WorseInterval {
        bool operator()(const Interval& a, const Interval& b) const {
            return a.delta < b.delta;
        }
    };
    std::priority_queue<Interval, std::vector<Interval>, WorseInterval> heap;
    const int cap = std::max(need * 64, interval_cap);
    for (int i : starts) {
        const int prev = route[static_cast<size_t>(i - 1)];
        const int first = route[static_cast<size_t>(i)];
        const double start_loss = RebalanceRawDistance(d, prev, first);
        for (int j : ends) {
            if (j < i) continue;
            const int len = j - i + 1;
            if (len >= c) continue;
            const int last = route[static_cast<size_t>(j)];
            const int next = route[static_cast<size_t>(j + 1)];
            const double delta = d.DepotDistance(first) + d.DepotDistance(last) +
                                 RebalanceRawDistance(d, prev, next) -
                                 start_loss - RebalanceRawDistance(d, last, next);
            if (static_cast<int>(heap.size()) < cap) {
                heap.push(Interval{i, j, delta});
            } else if (delta < heap.top().delta) {
                heap.pop();
                heap.push(Interval{i, j, delta});
            }
        }
    }
    if (static_cast<int>(heap.size()) < need) return false;

    std::vector<Interval> intervals;
    intervals.reserve(heap.size());
    while (!heap.empty()) {
        intervals.push_back(heap.top());
        heap.pop();
    }
    std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
        if (a.end != b.end) return a.end < b.end;
        if (a.start != b.start) return a.start < b.start;
        return a.delta < b.delta;
    });
    intervals.erase(std::unique(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
                        return a.start == b.start && a.end == b.end;
                    }),
                    intervals.end());
    const int n_intervals = static_cast<int>(intervals.size());
    if (n_intervals < need) return false;

    std::vector<int> ends_by_idx(static_cast<size_t>(n_intervals));
    for (int i = 0; i < n_intervals; ++i) ends_by_idx[static_cast<size_t>(i)] = intervals[static_cast<size_t>(i)].end;
    std::vector<int> prev_idx(static_cast<size_t>(n_intervals), -1);
    for (int i = 0; i < n_intervals; ++i) {
        const int max_prev_end = intervals[static_cast<size_t>(i)].start - 2;
        prev_idx[static_cast<size_t>(i)] =
            static_cast<int>(std::upper_bound(ends_by_idx.begin(), ends_by_idx.end(), max_prev_end) -
                             ends_by_idx.begin()) -
            1;
    }

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(static_cast<size_t>(need + 1),
                                        std::vector<double>(static_cast<size_t>(n_intervals + 1), inf));
    std::vector<std::vector<unsigned char>> take(static_cast<size_t>(need + 1),
                                                 std::vector<unsigned char>(static_cast<size_t>(n_intervals + 1), 0));
    for (int i = 0; i <= n_intervals; ++i) dp[0][static_cast<size_t>(i)] = 0.0;
    for (int t = 1; t <= need; ++t) {
        for (int i = 1; i <= n_intervals; ++i) {
            dp[static_cast<size_t>(t)][static_cast<size_t>(i)] =
                dp[static_cast<size_t>(t)][static_cast<size_t>(i - 1)];
            const int interval_idx = i - 1;
            const int p = prev_idx[static_cast<size_t>(interval_idx)] + 1;
            const double prev_cost = dp[static_cast<size_t>(t - 1)][static_cast<size_t>(p)];
            if (prev_cost < inf) {
                const double cand = prev_cost + intervals[static_cast<size_t>(interval_idx)].delta;
                if (cand + kEps < dp[static_cast<size_t>(t)][static_cast<size_t>(i)]) {
                    dp[static_cast<size_t>(t)][static_cast<size_t>(i)] = cand;
                    take[static_cast<size_t>(t)][static_cast<size_t>(i)] = 1;
                }
            }
        }
    }
    if (!(dp[static_cast<size_t>(need)][static_cast<size_t>(n_intervals)] < inf)) return false;

    std::vector<Interval> chosen;
    chosen.reserve(static_cast<size_t>(need));
    int t = need;
    int i = n_intervals;
    while (t > 0 && i > 0) {
        if (!take[static_cast<size_t>(t)][static_cast<size_t>(i)]) {
            --i;
            continue;
        }
        const int interval_idx = i - 1;
        chosen.push_back(intervals[static_cast<size_t>(interval_idx)]);
        i = prev_idx[static_cast<size_t>(interval_idx)] + 1;
        --t;
    }
    if (static_cast<int>(chosen.size()) != need) return false;
    std::sort(chosen.begin(), chosen.end(), [](const Interval& a, const Interval& b) {
        return a.start < b.start;
    });

    std::vector<unsigned char> removed(static_cast<size_t>(c + 1), 0);
    for (const auto& interval : chosen) {
        for (int pos = interval.start; pos <= interval.end; ++pos) removed[static_cast<size_t>(pos)] = 1;
    }

    auto& donor_route = routes[static_cast<size_t>(donor)];
    donor_route.clear();
    donor_route.push_back(0);
    for (int pos = 1; pos <= c; ++pos) {
        if (!removed[static_cast<size_t>(pos)]) donor_route.push_back(route[static_cast<size_t>(pos)]);
    }
    donor_route.push_back(0);
    if (donor_route.size() <= 2) return false;

    for (int e = 0; e < need; ++e) {
        const auto& interval = chosen[static_cast<size_t>(e)];
        auto& target = routes[static_cast<size_t>(empties[static_cast<size_t>(e)])];
        target.clear();
        target.push_back(0);
        for (int pos = interval.start; pos <= interval.end; ++pos) target.push_back(route[static_cast<size_t>(pos)]);
        target.push_back(0);
    }
    return CountEmptyRoutes(routes) == 0;
}

// Post-process empty routes by trying several cheap valid repairs and keeping
// the best MINSUM result. This is intentionally deterministic and bounded: the
// heavy search stays in the main pipeline, while this fixes the MINSUM tendency
// to collapse customers into too few routes.
inline void RebalanceEmptyRoutes(RouteSet& routes, DistanceOracle& d, int max_segment_len = 64) {
    if (CountEmptyRoutes(routes) == 0) return;

    RouteSet best = routes;
    bool have_best = false;
    double best_cost = std::numeric_limits<double>::infinity();
    auto consider = [&](RouteSet candidate) {
        CloseRoutesForValidation(candidate);
        if (CountEmptyRoutes(candidate) != 0) return;
        const double cost = RouteSumLengthForRebalance(candidate, d);
        if (!have_best || cost + kEps < best_cost) {
            have_best = true;
            best_cost = cost;
            best = std::move(candidate);
        }
    };

    {
        RouteSet candidate = routes;
        if (FillEmptyRoutesByGreedySegments(candidate, d, max_segment_len)) consider(std::move(candidate));
    }
    {
        RouteSet candidate = routes;
        if (FillEmptyRoutesByBestCuts(candidate, d)) consider(std::move(candidate));
    }
    {
        RouteSet candidate = routes;
        if (FillEmptyRoutesByCircularBestCuts(candidate, d)) consider(std::move(candidate));
    }
    {
        RouteSet candidate = routes;
        if (FillEmptyRoutesByIntervalDp(candidate, d)) consider(std::move(candidate));
    }
    if (have_best) routes = std::move(best);
}

inline bool ValidateRoutes(const RouteSet& routes, int n) {
    std::vector<int> seen(static_cast<size_t>(n), 0);
    seen[0] = 1;
    for (const auto& r : routes) {
        if (r.size() < 2 || r.front() != 0 || r.back() != 0) return false;
        for (size_t i = 1; i + 1 < r.size(); ++i) {
            const int v = r[i];
            if (v <= 0 || v >= n) return false;
            if (seen[static_cast<size_t>(v)] == 1) return false;
            seen[static_cast<size_t>(v)] = 1;
        }
    }
    for (int v = 1; v < n; ++v) if (!seen[static_cast<size_t>(v)]) return false;
    return true;
}

inline double RouteSumLength(const RouteSet& routes, DistanceOracle& d) {
    double total = 0.0;
    for (const auto& r : routes) for (size_t i = 1; i < r.size(); ++i) total += d(r[i - 1], r[i]);
    return total;
}

inline double MaxRouteLength(const RouteSet& routes, DistanceOracle& d) {
    double best = 0.0;
    for (const auto& r : routes) {
        double s = 0.0;
        for (size_t i = 1; i < r.size(); ++i) s += d(r[i - 1], r[i]);
        if (s > best) best = s;
    }
    return best;
}

// Lexicographic cost (max, λ·sum).
struct LexCost {
    double max = 0.0;
    double sum = 0.0;
    double lambda = 1e-3;

    double Scalarized() const { return max + lambda * sum; }
    bool StrictlyBetterThan(const LexCost& other, double tol = kEps) const {
        if (max + tol < other.max) return true;
        if (other.max + tol < max) return false;
        return sum + tol < other.sum;
    }
};

inline LexCost ComputeLexCost(const RouteSet& routes, DistanceOracle& d, double lambda) {
    LexCost c; c.lambda = lambda;
    for (const auto& r : routes) {
        double s = 0.0;
        for (size_t i = 1; i < r.size(); ++i) s += d(r[i - 1], r[i]);
        c.sum += s;
        if (s > c.max) c.max = s;
    }
    return c;
}

}  // namespace mtsp::v21
