#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "../include/json.hpp"
#include <mtsp_instance.h>

namespace mtsp {

namespace {

std::unique_ptr<Instance> g_instance;
constexpr long long kMaxPrecomputedDistances = 4'000'000LL;

std::string ReadAllStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

std::tuple<int, int, std::vector<std::pair<double, double>>> ParseJsonPayload(const std::string& payload) {
    const auto input = nlohmann::json::parse(payload);

    const int node_count = input.at("n").get<int>();
    const int salesman_count = input.at("m").get<int>();
    if (node_count <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of nodes.");
    }
    if (salesman_count <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of salesmen.");
    }
    if (!input.contains("coords") || !input.at("coords").is_array() ||
        input.at("coords").size() != static_cast<size_t>(node_count)) {
        throw std::runtime_error("mTSP input coords must be an array of size n.");
    }

    std::vector<std::pair<double, double>> coords;
    coords.reserve(static_cast<size_t>(node_count));
    for (const auto& point : input.at("coords")) {
        if (!point.is_array() || point.size() != 2) {
            throw std::runtime_error("Each mTSP coordinate must contain exactly two values.");
        }
        coords.emplace_back(point.at(0).get<double>(), point.at(1).get<double>());
    }
    return {node_count, salesman_count, std::move(coords)};
}

std::tuple<int, int, std::vector<std::pair<double, double>>> ParseTextInstance(std::istream& stream,
                                                                                const std::string& source) {
    int node_count = 0;
    int salesman_count = 0;
    if (!(stream >> node_count >> salesman_count)) {
        throw std::runtime_error("Could not read n and m from mTSP instance: " + source);
    }
    if (node_count <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of nodes.");
    }
    if (salesman_count <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of salesmen.");
    }

    std::vector<std::pair<double, double>> coords;
    coords.reserve(static_cast<size_t>(node_count));
    for (int idx = 0; idx < node_count; ++idx) {
        double x = 0.0;
        double y = 0.0;
        if (!(stream >> x >> y)) {
            throw std::runtime_error("Could not read coordinate " + std::to_string(idx) + " from mTSP instance: " +
                                     source);
        }
        coords.emplace_back(x, y);
    }
    return {node_count, salesman_count, std::move(coords)};
}

} // namespace

Instance& Instance::GetInstance() {
    if (!g_instance) {
        g_instance = std::unique_ptr<Instance>(new Instance());
    }
    return *g_instance;
}

void Instance::Reset() {
    g_instance.reset();
}

void Instance::LoadFromFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw std::runtime_error("Could not open mTSP instance file: " + path);
    }
    auto [node_count, salesman_count, coords] = ParseTextInstance(stream, path);
    g_instance = std::unique_ptr<Instance>(new Instance(node_count, salesman_count, std::move(coords)));
}

void Instance::LoadFromJsonString(const std::string& payload) {
    auto [node_count, salesman_count, coords] = ParseJsonPayload(payload);
    g_instance = std::unique_ptr<Instance>(new Instance(node_count, salesman_count, std::move(coords)));
}

Instance::Instance() : n_(0), m_(0) {
    auto [node_count, salesman_count, coords] = ParseJsonPayload(ReadAllStdin());
    n_ = node_count;
    m_ = salesman_count;
    coords_ = std::move(coords);
    BuildDistanceStorage();
}

Instance::Instance(int node_count, int salesman_count, std::vector<std::pair<double, double>> coords)
    : n_(node_count), m_(salesman_count), coords_(std::move(coords)) {
    BuildDistanceStorage();
}

void Instance::BuildDistanceStorage() {
    const long long pair_count = static_cast<long long>(n_) * static_cast<long long>(n_);
    if (pair_count > kMaxPrecomputedDistances) {
        // Large instances would spend hundreds of MB on the full matrix, so we fall back to on-demand distances.
        dist_.clear();
        return;
    }

    dist_.assign(static_cast<size_t>(n_), std::vector<double>(static_cast<size_t>(n_), 0.0));
    for (int i = 0; i < n_; ++i) {
        for (int j = 0; j < n_; ++j) {
            const double dx = coords_[i].first - coords_[j].first;
            const double dy = coords_[i].second - coords_[j].second;
            dist_[static_cast<size_t>(i)][static_cast<size_t>(j)] = std::sqrt(dx * dx + dy * dy);
        }
    }
}

int Instance::GetNodeCount() const {
    return n_;
}

int Instance::GetSalesmanCount() const {
    return m_;
}

double Instance::Distance(int i, int j) const {
    if (!dist_.empty()) {
        return dist_[static_cast<size_t>(i)][static_cast<size_t>(j)];
    }
    const double dx = coords_[static_cast<size_t>(i)].first - coords_[static_cast<size_t>(j)].first;
    const double dy = coords_[static_cast<size_t>(i)].second - coords_[static_cast<size_t>(j)].second;
    return std::sqrt(dx * dx + dy * dy);
}

const std::vector<std::pair<double, double>>& Instance::GetCoords() const {
    return coords_;
}

} // namespace mtsp
