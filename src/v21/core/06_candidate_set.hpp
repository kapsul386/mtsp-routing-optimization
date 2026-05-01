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
#include <algorithm>
#include <random>
#include <unordered_set>
#include <vector>

namespace mtsp::v21 {

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
