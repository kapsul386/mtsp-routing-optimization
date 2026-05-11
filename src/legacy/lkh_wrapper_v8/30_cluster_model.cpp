// v8/30_cluster_model.cpp — lightweight cluster model:
// k-means-style partitioning of clients into m clusters for the seed phase.
// Used as a pre-step before constructing initial routes.

namespace {

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
    if (node_count <= 1) {
        return 1;
    }
    if (node_count >= 100000) {
        return std::clamp(4 * salesman_count, 16, 24);
    }
    if (node_count >= 50000) {
        return std::clamp(4 * salesman_count + 2, 18, 28);
    }
    if (node_count >= 25000) {
        return std::clamp(5 * salesman_count, 20, 32);
    }
    const int min_clusters = std::max(4 * salesman_count, 1);
    const int max_clusters = std::max(10 * salesman_count, min_clusters);
    int desired = 5 * salesman_count + std::max(0, node_count / 15000);
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
    if (node_count <= 1 || budget.ForceCheck()) {
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
    const int iterations = node_count >= 100000 ? 2 : (node_count >= 50000 ? 3 : 4);
    const int batch_size = node_count >= 100000 ? std::min(node_count - 1, std::max(k * 256, node_count / 10))
                                             : (node_count >= 50000 ? std::min(node_count - 1, std::max(k * 256, node_count / 8))
                                                                    : node_count - 1);

    for (int it = 0; it < iterations && !budget.ShouldStop(); ++it) {
        std::vector<double> sum_x(static_cast<size_t>(k), 0.0);
        std::vector<double> sum_y(static_cast<size_t>(k), 0.0);
        std::vector<int> count(static_cast<size_t>(k), 0);

        if (batch_size < node_count - 1) {
            std::shuffle(cities.begin(), cities.end(), rng);
        }
        const int use_count = std::min(batch_size, node_count - 1);
        for (int idx = 0; idx < use_count; ++idx) {
            if ((idx & 31) == 0 && budget.ForceCheck()) {
                break;
            }
            const int city = cities[static_cast<size_t>(idx)];
            double best = std::numeric_limits<double>::max();
            int best_cluster = 0;
            for (int c = 0; c < k; ++c) {
                const double d2 = SquaredDistanceCoordsV5(coords[static_cast<size_t>(city)], centroids[static_cast<size_t>(c)]);
                if (d2 + kEps < best) {
                    best = d2;
                    best_cluster = c;
                }
            }
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

    for (int city = 1; city < node_count; ++city) {
        if ((city & 63) == 0 && budget.ForceCheck()) {
            break;
        }
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
        if (old_cluster >= 0) {
            model.node_to_cluster[static_cast<size_t>(city)] = remap[static_cast<size_t>(old_cluster)];
        }
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

        const double spread_term = cluster.radius * (1.0 + 0.12 * std::sqrt(static_cast<double>(cluster.members.size())));
        const double outlier_term = cluster.outlier_count * std::max(cluster.avg_dist, cluster.radius * 0.5);
        cluster.estimate = 2.0 * cluster.depot_distance + spread_term + 0.30 * outlier_term;
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
