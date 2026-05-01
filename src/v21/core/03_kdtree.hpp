#pragma once

// 2D KDTree used by candidate-set construction and zone-based destroy. Built
// once over the instance's coords; supports k-NN and radius queries. Memory
// ~24*n bytes — fits at n=100k. Thread-safe for queries after Build.

#include "00_types.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace mtsp::v21 {

class KDTree2D {
public:
    // (squared distance, node id) pair used by the bounded k-NN heap.
    struct Item { double d2; int idx; bool operator<(const Item& o) const { return d2 < o.d2; } };

    KDTree2D() = default;
    explicit KDTree2D(const std::vector<Coord>& coords) { Build(coords); }

    // Build/rebuild over the given coord array. The array must outlive the tree.
    void Build(const std::vector<Coord>& coords) {
        coords_ = &coords;
        const int n = static_cast<int>(coords.size());
        idx_.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) idx_[static_cast<size_t>(i)] = i;
        if (n > 0) BuildRec(0, n, 0);
    }

    // k nearest neighbors of `node` (excluding `node` itself), ascending by distance.
    void Knn(int node, int k, std::vector<int>& out) const {
        out.clear();
        if (!coords_ || k <= 0 || coords_->empty()) return;
        std::priority_queue<Item> heap;
        const Coord origin = (*coords_)[static_cast<size_t>(node)];
        KnnRec(0, static_cast<int>(idx_.size()), 0, origin, node, k, heap);
        out.reserve(heap.size());
        while (!heap.empty()) { out.push_back(heap.top().idx); heap.pop(); }
        std::reverse(out.begin(), out.end());
    }

    std::vector<int> Knn(int node, int k) const {
        std::vector<int> r; Knn(node, k, r); return r;
    }

    // All neighbors of `node` within Euclidean distance `r` (excluding `node` itself).
    void RangeRadius(int node, double r, std::vector<int>& out) const {
        out.clear();
        if (!coords_ || r <= 0.0) return;
        const Coord origin = (*coords_)[static_cast<size_t>(node)];
        const double r2 = r * r;
        RangeRec(0, static_cast<int>(idx_.size()), 0, origin, node, r2, out);
    }

private:
    static double SqDist(const Coord& a, const Coord& b) {
        const double dx = a.first - b.first, dy = a.second - b.second;
        return dx * dx + dy * dy;
    }

    void BuildRec(int lo, int hi, int depth) {
        if (lo >= hi) return;
        const int axis = depth & 1;
        const int mid = lo + (hi - lo) / 2;
        std::nth_element(idx_.begin() + lo, idx_.begin() + mid, idx_.begin() + hi,
            [this, axis](int a, int b) {
                const Coord& A = (*coords_)[static_cast<size_t>(a)];
                const Coord& B = (*coords_)[static_cast<size_t>(b)];
                return (axis == 0 ? A.first : A.second) < (axis == 0 ? B.first : B.second);
            });
        BuildRec(lo, mid, depth + 1);
        BuildRec(mid + 1, hi, depth + 1);
    }

    void KnnRec(int lo, int hi, int depth, const Coord& origin, int self_node, int k,
                std::priority_queue<Item>& heap) const {
        if (lo >= hi) return;
        const int axis = depth & 1;
        const int mid = lo + (hi - lo) / 2;
        const int node_idx = idx_[static_cast<size_t>(mid)];
        if (node_idx != self_node) {
            const double d2 = SqDist((*coords_)[static_cast<size_t>(node_idx)], origin);
            if (static_cast<int>(heap.size()) < k) heap.push({d2, node_idx});
            else if (d2 < heap.top().d2) { heap.pop(); heap.push({d2, node_idx}); }
        }
        const double pivot_v = (axis == 0 ? (*coords_)[static_cast<size_t>(node_idx)].first
                                          : (*coords_)[static_cast<size_t>(node_idx)].second);
        const double origin_v = (axis == 0 ? origin.first : origin.second);
        const double diff = origin_v - pivot_v;
        if (diff <= 0.0) {
            KnnRec(lo, mid, depth + 1, origin, self_node, k, heap);
            if (static_cast<int>(heap.size()) < k || diff * diff < heap.top().d2)
                KnnRec(mid + 1, hi, depth + 1, origin, self_node, k, heap);
        } else {
            KnnRec(mid + 1, hi, depth + 1, origin, self_node, k, heap);
            if (static_cast<int>(heap.size()) < k || diff * diff < heap.top().d2)
                KnnRec(lo, mid, depth + 1, origin, self_node, k, heap);
        }
    }

    void RangeRec(int lo, int hi, int depth, const Coord& origin, int self_node, double r2,
                  std::vector<int>& out) const {
        if (lo >= hi) return;
        const int axis = depth & 1;
        const int mid = lo + (hi - lo) / 2;
        const int node_idx = idx_[static_cast<size_t>(mid)];
        if (node_idx != self_node) {
            const double d2 = SqDist((*coords_)[static_cast<size_t>(node_idx)], origin);
            if (d2 <= r2) out.push_back(node_idx);
        }
        const double pivot_v = (axis == 0 ? (*coords_)[static_cast<size_t>(node_idx)].first
                                          : (*coords_)[static_cast<size_t>(node_idx)].second);
        const double origin_v = (axis == 0 ? origin.first : origin.second);
        const double diff = origin_v - pivot_v;
        if (diff <= 0.0) {
            RangeRec(lo, mid, depth + 1, origin, self_node, r2, out);
            if (diff * diff <= r2) RangeRec(mid + 1, hi, depth + 1, origin, self_node, r2, out);
        } else {
            RangeRec(mid + 1, hi, depth + 1, origin, self_node, r2, out);
            if (diff * diff <= r2) RangeRec(lo, mid, depth + 1, origin, self_node, r2, out);
        }
    }

    const std::vector<Coord>* coords_ = nullptr;
    std::vector<int> idx_;
};

}  // namespace mtsp::v21
