#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "../include/json.hpp"
#include <instance.h>

namespace tsp {

namespace {

std::unique_ptr<Instance> g_instance;
constexpr long long kMaxPrecomputedDistances = 4'000'000LL;
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

std::string ReadAllStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

Instance::MetricType ParseMetricName(const std::string& metric_name_raw) {
    std::string metric_name = metric_name_raw;
    for (char& ch : metric_name) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        } else if (ch == '_') {
            ch = '-';
        }
    }

    if (metric_name == "euclidean") {
        return Instance::MetricType::Euclidean;
    }
    if (metric_name == "great-circle" || metric_name == "latlon" || metric_name == "geo") {
        return Instance::MetricType::GreatCircle;
    }
    throw std::runtime_error("Unsupported TSP metric: " + metric_name_raw);
}

double GreatCircleDistanceKm(const std::pair<double, double>& lhs, const std::pair<double, double>& rhs) {
    const double lat_a = lhs.first * kDegToRad;
    const double lon_a = lhs.second * kDegToRad;
    const double lat_b = rhs.first * kDegToRad;
    const double lon_b = rhs.second * kDegToRad;

    const double dlat_sin = std::sin((lat_b - lat_a) / 2.0);
    const double dlon_sin = std::sin((lon_b - lon_a) / 2.0);
    const double h = dlat_sin * dlat_sin + std::cos(lat_a) * std::cos(lat_b) * dlon_sin * dlon_sin;
    return kEarthRadiusKm * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

double EuclideanDistance(const std::pair<double, double>& lhs, const std::pair<double, double>& rhs) {
    const double dx = lhs.first - rhs.first;
    const double dy = lhs.second - rhs.second;
    return std::sqrt(dx * dx + dy * dy);
}

std::tuple<int, Instance::MetricType, std::vector<std::pair<double, double>>> ParseJsonPayload(const std::string& payload) {
    const auto input = nlohmann::json::parse(payload);
    const int node_count = input.at("n").get<int>();
    if (node_count <= 0) {
        throw std::runtime_error("TSP instance must contain a positive number of nodes.");
    }

    if (input.contains("latlon")) {
        if (!input.at("latlon").is_array() || input.at("latlon").size() != 2) {
            throw std::runtime_error("TSP input must contain latlon with two coordinate rows.");
        }
        if (input.at("latlon")[0].size() != static_cast<size_t>(node_count) ||
            input.at("latlon")[1].size() != static_cast<size_t>(node_count)) {
            throw std::runtime_error("TSP input latlon size does not match n.");
        }

        std::vector<std::pair<double, double>> coords;
        coords.reserve(static_cast<size_t>(node_count));
        const nlohmann::json& row_lat = input.at("latlon")[0];
        const nlohmann::json& row_lon = input.at("latlon")[1];
        for (int idx = 0; idx < node_count; ++idx) {
            coords.emplace_back(row_lat[static_cast<size_t>(idx)].get<double>(),
                                row_lon[static_cast<size_t>(idx)].get<double>());
        }
        return {node_count, Instance::MetricType::GreatCircle, std::move(coords)};
    }

    if (input.contains("coords")) {
        if (!input.at("coords").is_array() || input.at("coords").size() != static_cast<size_t>(node_count)) {
            throw std::runtime_error("TSP input coords must be an array of size n.");
        }

        std::vector<std::pair<double, double>> coords;
        coords.reserve(static_cast<size_t>(node_count));
        for (const auto& point : input.at("coords")) {
            if (!point.is_array() || point.size() != 2) {
                throw std::runtime_error("Each TSP coordinate must contain exactly two values.");
            }
            coords.emplace_back(point.at(0).get<double>(), point.at(1).get<double>());
        }

        Instance::MetricType metric = Instance::MetricType::Euclidean;
        if (input.contains("metric")) {
            metric = ParseMetricName(input.at("metric").get<std::string>());
        }
        return {node_count, metric, std::move(coords)};
    }

    throw std::runtime_error("TSP input must contain either latlon or coords.");
}

std::tuple<int, Instance::MetricType, std::vector<std::pair<double, double>>> ParseTextInstance(std::istream& stream,
                                                                                                 const std::string& source) {
    std::string header_line;
    if (!std::getline(stream, header_line)) {
        throw std::runtime_error("Could not read TSP instance header: " + source);
    }
    if (header_line.size() >= 3 && static_cast<unsigned char>(header_line[0]) == 0xEF &&
        static_cast<unsigned char>(header_line[1]) == 0xBB &&
        static_cast<unsigned char>(header_line[2]) == 0xBF) {
        header_line.erase(0, 3);
    }

    std::istringstream header(header_line);
    int node_count = 0;
    std::string metric_name = "euclidean";
    if (!(header >> node_count)) {
        throw std::runtime_error("Could not parse TSP node count from: " + source);
    }
    if (node_count <= 0) {
        throw std::runtime_error("TSP instance must contain a positive number of nodes.");
    }
    if (header >> metric_name) {
        // Optional metric token is supported for future reuse.
    }

    std::vector<std::pair<double, double>> coords;
    coords.reserve(static_cast<size_t>(node_count));
    for (int idx = 0; idx < node_count; ++idx) {
        double first = 0.0;
        double second = 0.0;
        if (!(stream >> first >> second)) {
            throw std::runtime_error("Could not read coordinate " + std::to_string(idx) + " from TSP instance: " +
                                     source);
        }
        coords.emplace_back(first, second);
    }

    return {node_count, ParseMetricName(metric_name), std::move(coords)};
}

} // namespace

Instance& Instance::GetInstance() {
    if (!g_instance) {
        g_instance = std::unique_ptr<Instance>(new Instance());
    }
    return *g_instance;
}

void Instance::LoadFromFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        throw std::runtime_error("Could not open TSP instance file: " + path);
    }
    auto [node_count, metric, coords] = ParseTextInstance(stream, path);
    g_instance = std::unique_ptr<Instance>(new Instance(node_count, metric, std::move(coords)));
}

void Instance::LoadFromJsonString(const std::string& payload) {
    auto [node_count, metric, coords] = ParseJsonPayload(payload);
    g_instance = std::unique_ptr<Instance>(new Instance(node_count, metric, std::move(coords)));
}

void Instance::Reset() {
    g_instance.reset();
}

Instance::Instance() : n(0), metric_(MetricType::GreatCircle) {
    auto [node_count, metric, coords] = ParseJsonPayload(ReadAllStdin());
    n = node_count;
    metric_ = metric;
    coords_ = std::move(coords);
    BuildDistanceStorage();
}

Instance::Instance(int node_count, MetricType metric, std::vector<std::pair<double, double>> coords)
    : n(node_count), metric_(metric), coords_(std::move(coords)) {
    BuildDistanceStorage();
}

void Instance::BuildDistanceStorage() {
    const long long pair_count = static_cast<long long>(n) * static_cast<long long>(n);
    if (pair_count > kMaxPrecomputedDistances) {
        // Large Euclidean transform baselines should not allocate a full dense matrix by default.
        mat.clear();
        return;
    }

    mat.assign(static_cast<size_t>(n), std::vector<double>(static_cast<size_t>(n), 0.0));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const auto& lhs = coords_[static_cast<size_t>(i)];
            const auto& rhs = coords_[static_cast<size_t>(j)];
            if (metric_ == MetricType::GreatCircle) {
                mat[static_cast<size_t>(i)][static_cast<size_t>(j)] = GreatCircleDistanceKm(lhs, rhs);
            } else {
                mat[static_cast<size_t>(i)][static_cast<size_t>(j)] = EuclideanDistance(lhs, rhs);
            }
        }
    }
}

int Instance::GetN() const {
    return n;
}

double Instance::Distance(int i, int j) const {
    if (!mat.empty()) {
        return mat[static_cast<size_t>(i)][static_cast<size_t>(j)];
    }

    const auto& lhs = coords_[static_cast<size_t>(i)];
    const auto& rhs = coords_[static_cast<size_t>(j)];
    if (metric_ == MetricType::GreatCircle) {
        return GreatCircleDistanceKm(lhs, rhs);
    }
    return EuclideanDistance(lhs, rhs);
}

} // namespace tsp
