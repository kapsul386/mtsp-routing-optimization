#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <mtsp_solver.h>

namespace mtsp::baselines {

enum class Lkh3MtspObjective {
    Minsum,
    Minmax,
};

struct Lkh3BaselineArtifacts {
    std::filesystem::path work_dir;
    std::filesystem::path problem_file;
    std::filesystem::path params_file;
    std::filesystem::path result_file;
    std::filesystem::path output_tour_file;
    std::filesystem::path stdout_log;
    std::filesystem::path stderr_log;
};

struct Lkh3BaselineRunResult {
    RouteSet routes;
    std::string status = "ok";
    std::string message;
    std::string lkh3_wsl_bin;
    std::string params_wsl_path;
    std::string objective_mode = "MINSUM";
    long long exit_code = -1;
    bool valid = false;
    double objective = 0.0;
    Lkh3BaselineArtifacts artifacts;
};

std::filesystem::path CreateLkh3BaselineRunDirectory();
std::string WindowsPathToWsl(const std::filesystem::path& path);
std::string ToParameterObjective(Lkh3MtspObjective objective);
void WriteLkh3ProblemFile(const std::filesystem::path& path, const Instance& inst);
void WriteLkh3ParameterFile(const std::filesystem::path& path,
                            int salesman_count,
                            Lkh3MtspObjective objective,
                            std::optional<double> time_limit_seconds = std::nullopt);
RouteSet ParseMtspResultFile(const std::filesystem::path& path, int salesman_count, int node_count);
Lkh3BaselineRunResult RunLkh3Baseline(Lkh3MtspObjective objective,
                                      std::optional<double> time_limit_seconds = std::nullopt);

} // namespace mtsp::baselines
