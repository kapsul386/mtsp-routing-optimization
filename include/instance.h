#pragma once

#include <vector>

namespace tsp {

class Instance {
public:
    static Instance& GetInstance() {
        static Instance inst;
        return inst;
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    [[nodiscard]] int GetN() const;
    [[nodiscard]] double Distance(int i, int j) const;

private:
    Instance();
    ~Instance() = default;

    int n;
    std::vector<std::vector<double>> mat;
};

} // namespace tsp
