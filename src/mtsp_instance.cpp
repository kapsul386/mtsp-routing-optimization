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
    coords_.reserve(n_);
    for (const auto& point : input.at("coords")) {
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
