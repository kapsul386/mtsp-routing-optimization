#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>

#include "../include/json.hpp"
#include <mtsp_instance.h>

namespace mtsp {

namespace {

std::unique_ptr<Instance> g_instance;

std::string ReadAllStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
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

Instance::Instance() : n_(0), m_(0) {
    const auto input = nlohmann::json::parse(ReadAllStdin());
    n_ = input.at("n").get<int>();
    m_ = input.at("m").get<int>();
    if (n_ <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of nodes.");
    }
    if (m_ <= 0) {
        throw std::runtime_error("mTSP instance must contain a positive number of salesmen.");
    }
    if (!input.contains("coords") || !input.at("coords").is_array() || input.at("coords").size() != static_cast<size_t>(n_)) {
        throw std::runtime_error("mTSP input coords must be an array of size n.");
    }
    coords_.reserve(n_);
    for (const auto& point : input.at("coords")) {
        if (!point.is_array() || point.size() != 2) {
            throw std::runtime_error("Each mTSP coordinate must contain exactly two values.");
        }
        coords_.emplace_back(point.at(0).get<double>(), point.at(1).get<double>());
    }

    dist_.assign(n_, std::vector<double>(n_, 0.0));
    for (int i = 0; i < n_; ++i) {
        for (int j = 0; j < n_; ++j) {
            const double dx = coords_[i].first - coords_[j].first;
            const double dy = coords_[i].second - coords_[j].second;
            dist_[i][j] = std::sqrt(dx * dx + dy * dy);
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
    return dist_[i][j];
}

const std::vector<std::pair<double, double>>& Instance::GetCoords() const {
    return coords_;
}

} // namespace mtsp
