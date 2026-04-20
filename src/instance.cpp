#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

#include "../include/json.hpp"
#include <instance.h>

namespace tsp {

inline std::string ReadAllStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

inline std::vector<std::vector<double>> CalculateDistances(const std::vector<double>& lat_deg,
                                                           const std::vector<double>& lon_deg) {
    const double earth_radius_km = 6371.0;
    const double deg_to_rad = 3.14159265358979323846 / 180.0;
    const size_t n = lat_deg.size();

    std::vector<double> lat(n), lon(n);
    for (size_t i = 0; i < n; ++i) {
        lat[i] = lat_deg[i] * deg_to_rad;
        lon[i] = lon_deg[i] * deg_to_rad;
    }

    std::vector<std::vector<double>> dist(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            double dlat_sin = sin((lat[j] - lat[i]) / 2.0);
            double dlon_sin = sin((lon[j] - lon[i]) / 2.0);
            double h = dlat_sin * dlat_sin + cos(lat[i]) * cos(lat[j]) * dlon_sin * dlon_sin;
            dist[i][j] = earth_radius_km * 2.0 * atan2(sqrt(h), sqrt(1.0 - h));
        }
    }

    return dist;
}

inline std::vector<std::vector<double>> ParseCalculateDistances(const nlohmann::json& input) {
    size_t n = input["n"];
    std::vector<double> lat(n), lon(n);

    const nlohmann::json& row_lat = input["latlon"][0];
    const nlohmann::json& row_lon = input["latlon"][1];
    for (size_t i = 0; i < n; ++i) {
        lat[i] = row_lat[i].get<double>();
        lon[i] = row_lon[i].get<double>();
    }

    return CalculateDistances(lat, lon);
}

Instance::Instance() {
    auto input = nlohmann::json::parse(ReadAllStdin());
    n = input.at("n").get<int>();
    if (n <= 0) {
        throw std::runtime_error("TSP instance must contain a positive number of nodes.");
    }
    if (!input.contains("latlon") || !input.at("latlon").is_array() || input.at("latlon").size() != 2) {
        throw std::runtime_error("TSP input must contain latlon with two coordinate rows.");
    }
    if (input.at("latlon")[0].size() != static_cast<size_t>(n) || input.at("latlon")[1].size() != static_cast<size_t>(n)) {
        throw std::runtime_error("TSP input latlon size does not match n.");
    }
    mat = ParseCalculateDistances(input);
}

int Instance::GetN() const {
    return n;
}

double Instance::Distance(int i, int j) const {
    return mat[i][j];
}

} // namespace tsp
