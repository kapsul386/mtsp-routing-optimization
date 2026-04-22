#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tsp {

class Instance {
public:
    enum class MetricType {
        GreatCircle,
        Euclidean,
    };

    static Instance& GetInstance();
    static void LoadFromFile(const std::string& path);
    static void LoadFromJsonString(const std::string& payload);
    static void Reset();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    [[nodiscard]] int GetN() const;
    [[nodiscard]] double Distance(int i, int j) const;
    ~Instance() = default;

private:
    Instance();
    Instance(int node_count, MetricType metric, std::vector<std::pair<double, double>> coords);

    void BuildDistanceStorage();

    int n;
    MetricType metric_;
    std::vector<std::pair<double, double>> coords_;
    std::vector<std::vector<double>> mat;
};

} // namespace tsp
