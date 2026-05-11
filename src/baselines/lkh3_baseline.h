#pragma once

// Interface header for the C++ wrapper around the LKH-3 binary, launched via WSL.
// LKH-3 is not available as a library — only as an executable, so
// integration is implemented via temporary .par and .tsp files and parsing result.txt.

#include <filesystem>
#include <optional>
#include <string>

#include <mtsp_solver.h>

namespace mtsp::baselines {

// Objective function for LKH-3: MINSUM (sum of route lengths) or MINMAX (longest route).
enum class Lkh3MtspObjective {
    Minsum,
    Minmax,
};

// Holds the paths to the temporary artifacts of a single LKH-3 run.
// Stores paths to input/output files and stdout/stderr logs; used for debugging and auditing.
struct Lkh3BaselineArtifacts {
    std::filesystem::path work_dir;
    std::filesystem::path problem_file;       // .tsp file with coordinates and metric
    std::filesystem::path params_file;        // .par file with LKH-3 settings
    std::filesystem::path result_file;        // result.txt in MTSP format
    std::filesystem::path output_tour_file;   // (reserved) alternative output format
    std::filesystem::path stdout_log;         // stdout of the LKH-3 process
    std::filesystem::path stderr_log;         // stderr of the LKH-3 process
};

// Result of a single LKH-3 run: routes + diagnostic fields for the JSON report.
struct Lkh3BaselineRunResult {
    RouteSet routes;
    std::string status = "ok";        // "ok" / "crashed" / "timeout"
    std::string message;              // error description text, if any
    std::string lkh3_wsl_bin;         // path to the LKH binary in WSL format
    std::string params_wsl_path;      // path to the .par file in WSL format
    std::string objective_mode = "MINSUM";
    long long exit_code = -1;         // exit code of the LKH-3 process
    bool valid = false;               // whether the resulting partition passed validation
    double objective = 0.0;
    Lkh3BaselineArtifacts artifacts;
};

// Creates a unique temporary directory for a single LKH-3 run.
std::filesystem::path CreateLkh3BaselineRunDirectory();
// Converts a Windows path (C:\foo\bar) to WSL format (/mnt/c/foo/bar).
std::string WindowsPathToWsl(const std::filesystem::path& path);
// Returns the string for the LKH-3 .par file: "MINSUM" or "MINMAX".
std::string ToParameterObjective(Lkh3MtspObjective objective);
// Writes the .tsp file (coordinates, metric, depot section) for LKH-3.
void WriteLkh3ProblemFile(const std::filesystem::path& path, const Instance& inst);
// Writes the .par file with LKH-3 run parameters (objective, min/max-size, time-limit).
void WriteLkh3ParameterFile(const std::filesystem::path& path,
                            int salesman_count,
                            Lkh3MtspObjective objective,
                            std::optional<double> time_limit_seconds = std::nullopt);
// Parses the result.txt file from LKH-3 into a RouteSet. Handles the MTSP_SOLUTION_FILE format.
RouteSet ParseMtspResultFile(const std::filesystem::path& path, int salesman_count, int node_count);
// Main entry function: launches LKH-3 via WSL, waits for completion, parses the result.
Lkh3BaselineRunResult RunLkh3Baseline(Lkh3MtspObjective objective,
                                      std::optional<double> time_limit_seconds = std::nullopt);

} // namespace mtsp::baselines
