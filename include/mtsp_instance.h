#pragma once

#include <utility>
#include <vector>

namespace mtsp {

class Instance {
public:
    static Instance& GetInstance();
    static void Reset();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    [[nodiscard]] int GetNodeCount() const;
    [[nodiscard]] int GetSalesmanCount() const;
    [[nodiscard]] double Distance(int i, int j) const;
    [[nodiscard]] const std::vector<std::pair<double, double>>& GetCoords() const;

private:
    Instance();

    int n_;
    int m_;
    std::vector<std::pair<double, double>> coords_;
    std::vector<std::vector<double>> dist_;
};

} // namespace mtsp
