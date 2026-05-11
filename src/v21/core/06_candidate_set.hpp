#pragma once

// Candidate-set construction. Every move generator in v21 (repair, relocate,
// 2-opt, intra-3opt) restricts itself to the per-node candidate list, which
// is the entire reason large instances stay tractable (Toth--Vigo granular
// neighborhoods, see references in the report). Built once at instance load
// and reused for the rest of the run.

#include "00_types.hpp"
#include "01_budget.hpp"
#include "02_distance.hpp"
#include "03_kdtree.hpp"
#include <mtsp_instance.h>
#include <mtsp_solver.h>
#include <algorithm>
#include <random>
#include <unordered_set>
#include <vector>

namespace mtsp::v21 {

// Diagnostic counters returned by AugmentWithRouteBoundaryCandidates.
// All fields are cumulative over a single augmentation call.
struct CandidateAugmentStats {
    int anchors = 0;
    int endpoint_anchors = 0;
    int expensive_anchors = 0;
    int edges_added = 0;
};

// Build per-node k-NN candidate lists using the KDTree. The depot (0) is
// excluded from neighbour lists for non-depot nodes.
inline CandidateSets BuildKnnCandidates(const mtsp::Instance& inst,
                                        int k,
                                        SearchBudget& /*budget*/) {
    const auto& coords = inst.GetCoords();
    const int n = static_cast<int>(coords.size());
    KDTree2D tree(coords);
    CandidateSets out(static_cast<size_t>(n));
    std::vector<int> tmp;
    for (int i = 0; i < n; ++i) {
        tree.Knn(i, k + 4, tmp);  // get a few extra to filter depot if needed
        auto& cs = out[static_cast<size_t>(i)];
        cs.reserve(static_cast<size_t>(k));
        for (int j : tmp) {
            if (i != 0 && j == 0) continue;
            if (static_cast<int>(cs.size()) >= k) break;
            cs.push_back(j);
        }
    }
    return out;
}

// Symmetrize candidate lists (if j in C(i), ensure i in C(j)).
inline void SymmetrizeCandidates(CandidateSets& cs) {
    const int n = static_cast<int>(cs.size());
    std::vector<std::unordered_set<int>> as_sets(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        for (int j : cs[static_cast<size_t>(i)])
            as_sets[static_cast<size_t>(i)].insert(j);
    for (int i = 0; i < n; ++i) {
        for (int j : cs[static_cast<size_t>(i)]) {
            if (!as_sets[static_cast<size_t>(j)].count(i)) {
                cs[static_cast<size_t>(j)].push_back(i);
                as_sets[static_cast<size_t>(j)].insert(i);
            }
        }
    }
}

// Add route-aware cross-route edges around route endpoints and expensive
// intra-route links. kNN alone often spends all slots inside one large route;
// this gives granular relocate/2-opt*/Or-opt a few explicit bridge candidates
// without widening the global candidate graph for every city.
inline CandidateAugmentStats AugmentWithRouteBoundaryCandidates(
    const mtsp::Instance& inst,
    const RouteSet& routes,
    CandidateSets& candidates,
    DistanceOracle& d,
    int endpoint_depth,
    int expensive_edges_per_route,
    int knn_probe,
    int per_anchor,
    int max_extra_per_node) {
    CandidateAugmentStats stats;
    const int n = inst.GetNodeCount();
    if (n <= 2 || routes.empty() || candidates.size() < static_cast<size_t>(n)) return stats;
    endpoint_depth = std::max(0, endpoint_depth);
    expensive_edges_per_route = std::max(0, expensive_edges_per_route);
    knn_probe = std::max(8, knn_probe);
    per_anchor = std::max(0, per_anchor);
    max_extra_per_node = std::max(0, max_extra_per_node);
    if (per_anchor == 0 || max_extra_per_node == 0) return stats;

    std::vector<int> route_of(static_cast<size_t>(n), -1);
    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
        const auto& route = routes[static_cast<size_t>(r)];
        for (size_t i = 1; i + 1 < route.size(); ++i) {
            const int c = route[i];
            if (c > 0 && c < n) route_of[static_cast<size_t>(c)] = r;
        }
    }

    std::vector<int> anchors;
    anchors.reserve(static_cast<size_t>(std::max(16, static_cast<int>(routes.size()) *
                                                     (endpoint_depth * 2 + expensive_edges_per_route * 2))));
    std::vector<unsigned char> seen(static_cast<size_t>(n), 0);
    auto add_anchor = [&](int city, bool endpoint) {
        if (city <= 0 || city >= n || route_of[static_cast<size_t>(city)] < 0) return;
        if (seen[static_cast<size_t>(city)]) return;
        seen[static_cast<size_t>(city)] = 1;
        anchors.push_back(city);
        if (endpoint) ++stats.endpoint_anchors;
        else ++stats.expensive_anchors;
    };

    struct EdgeScore {
        double score = 0.0;
        int a = -1;
        int b = -1;
    };
    for (const auto& route : routes) {
        const int customers = route.size() >= 2 ? static_cast<int>(route.size()) - 2 : 0;
        if (customers <= 0) continue;
        const int depth = std::min(endpoint_depth, customers);
        for (int i = 0; i < depth; ++i) {
            add_anchor(route[static_cast<size_t>(1 + i)], true);
            add_anchor(route[static_cast<size_t>(customers - i)], true);
        }
        if (expensive_edges_per_route <= 0) continue;
        std::vector<EdgeScore> scored;
        scored.reserve(static_cast<size_t>(std::max(0, static_cast<int>(route.size()) - 1)));
        for (size_t i = 0; i + 1 < route.size(); ++i) {
            const int a = route[i];
            const int b = route[i + 1];
            if (a == 0 && b == 0) continue;
            const double score = d(a, b);
            scored.push_back(EdgeScore{score, a, b});
        }
        const int take = std::min(expensive_edges_per_route, static_cast<int>(scored.size()));
        if (take <= 0) continue;
        if (take < static_cast<int>(scored.size())) {
            std::nth_element(scored.begin(), scored.begin() + take, scored.end(),
                             [](const EdgeScore& x, const EdgeScore& y) {
                                 return x.score > y.score;
                             });
        }
        for (int i = 0; i < take; ++i) {
            add_anchor(scored[static_cast<size_t>(i)].a, false);
            add_anchor(scored[static_cast<size_t>(i)].b, false);
        }
    }
    stats.anchors = static_cast<int>(anchors.size());
    if (anchors.empty()) return stats;

    std::vector<int> added_per_node(static_cast<size_t>(n), 0);
    auto add_directed = [&](int a, int b) {
        if (a <= 0 || b <= 0 || a >= n || b >= n || a == b) return false;
        if (added_per_node[static_cast<size_t>(a)] >= max_extra_per_node) return false;
        auto& list = candidates[static_cast<size_t>(a)];
        if (std::find(list.begin(), list.end(), b) != list.end()) return false;
        list.push_back(b);
        ++added_per_node[static_cast<size_t>(a)];
        return true;
    };
    auto add_edge = [&](int a, int b) {
        bool any = false;
        any = add_directed(a, b) || any;
        any = add_directed(b, a) || any;
        if (any) ++stats.edges_added;
    };

    KDTree2D tree(inst.GetCoords());
    std::vector<int> near;
    near.reserve(static_cast<size_t>(knn_probe));
    for (int city : anchors) {
        if (added_per_node[static_cast<size_t>(city)] >= max_extra_per_node) continue;
        const int r_city = route_of[static_cast<size_t>(city)];
        int added_for_anchor = 0;
        tree.Knn(city, std::min(knn_probe, n - 1), near);
        for (int nb : near) {
            if (nb <= 0 || nb == city || nb >= n) continue;
            const int r_nb = route_of[static_cast<size_t>(nb)];
            if (r_nb < 0 || r_nb == r_city) continue;
            const int before = stats.edges_added;
            add_edge(city, nb);
            if (stats.edges_added > before) ++added_for_anchor;
            if (added_for_anchor >= per_anchor ||
                added_per_node[static_cast<size_t>(city)] >= max_extra_per_node) {
                break;
            }
        }
    }
    return stats;
}

// Returns the average per-node candidate-list length across the whole set.
// Useful as a diagnostic after augmentation to verify the graph did not grow too wide.
inline double AverageCandidateListSize(const CandidateSets& candidates) {
    if (candidates.empty()) return 0.0;
    long long total = 0;
    for (const auto& list : candidates) total += static_cast<long long>(list.size());
    return static_cast<double>(total) / static_cast<double>(candidates.size());
}

// Augment KNN candidates with edges drawn from a few rough nearest-neighbour
// tours (POPMUSIC-lite). Skips heavy POPMUSIC reduction; just runs a small
// random-NN tour and adds traversed edges to candidate frequency.
inline void AugmentWithPopmusicEdges(const mtsp::Instance& inst,
                                     CandidateSets& candidates,
                                     int popmusic_solutions,
                                     int max_extra_per_node,
                                     std::mt19937& rng,
                                     SearchBudget& budget) {
    if (popmusic_solutions <= 0) return;
    const auto& coords = inst.GetCoords();
    const int n = static_cast<int>(coords.size());
    if (n < 16) return;
    KDTree2D tree(coords);
    std::vector<std::unordered_set<int>> as_sets(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        for (int j : candidates[static_cast<size_t>(i)])
            as_sets[static_cast<size_t>(i)].insert(j);

    std::vector<int> visited;
    std::vector<char> seen(static_cast<size_t>(n), 0);
    std::vector<int> nbr;
    for (int t = 0; t < popmusic_solutions; ++t) {
        if (budget.ForceCheck()) return;
        std::fill(seen.begin(), seen.end(), 0);
        const int start = static_cast<int>(rng() % static_cast<unsigned>(n));
        seen[static_cast<size_t>(start)] = 1;
        int cur = start;
        for (int step = 0; step + 1 < n; ++step) {
            if ((step & 4095) == 0 && budget.ForceCheck()) return;
            tree.Knn(cur, 16, nbr);
            int nxt = -1;
            for (int j : nbr) if (!seen[static_cast<size_t>(j)]) { nxt = j; break; }
            if (nxt < 0) {
                for (int j = 0; j < n; ++j) if (!seen[static_cast<size_t>(j)]) { nxt = j; break; }
                if (nxt < 0) break;
            }
            // Add (cur, nxt) edge as candidate
            if (cur != 0 && nxt != 0) {
                if (!as_sets[static_cast<size_t>(cur)].count(nxt) &&
                    static_cast<int>(candidates[static_cast<size_t>(cur)].size()) < max_extra_per_node) {
                    candidates[static_cast<size_t>(cur)].push_back(nxt);
                    as_sets[static_cast<size_t>(cur)].insert(nxt);
                }
                if (!as_sets[static_cast<size_t>(nxt)].count(cur) &&
                    static_cast<int>(candidates[static_cast<size_t>(nxt)].size()) < max_extra_per_node) {
                    candidates[static_cast<size_t>(nxt)].push_back(cur);
                    as_sets[static_cast<size_t>(nxt)].insert(cur);
                }
            }
            seen[static_cast<size_t>(nxt)] = 1;
            cur = nxt;
        }
    }
}

// Filter global candidates down to a smaller local list (top-k by distance).
inline CandidateSets BuildLocalFromGlobal(const CandidateSets& global,
                                          int k_local,
                                          DistanceOracle& d) {
    CandidateSets out(global.size());
    std::vector<std::pair<double, int>> tmp;
    for (size_t i = 0; i < global.size(); ++i) {
        tmp.clear();
        tmp.reserve(global[i].size());
        for (int j : global[i]) tmp.emplace_back(d(static_cast<int>(i), j), j);
        std::sort(tmp.begin(), tmp.end());
        const int take = std::min(k_local, static_cast<int>(tmp.size()));
        out[i].reserve(static_cast<size_t>(take));
        for (int t = 0; t < take; ++t) out[i].push_back(tmp[static_cast<size_t>(t)].second);
    }
    return out;
}

}  // namespace mtsp::v21
