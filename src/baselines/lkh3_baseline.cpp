// C++ wrapper implementation around the LKH-3 binary.
// Execution proceeds via WSL2 (Windows Subsystem for Linux): a temporary
// directory is created with .tsp/.par files, the process wsl.exe LKH params.par is launched,
// stdout/stderr are captured, and result.txt is parsed in MTSP format.

#include "lkh3_baseline.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mtsp_factory.h>
#include <mtsp_utils.h>

namespace mtsp::baselines {

namespace {

// Path to the WSL executable. Used to launch LKH-3 from Windows.
constexpr std::wstring_view kWslExecutable = L"C:\\Windows\\System32\\wsl.exe";
// Atomic run counter for uniquifying temporary directory names.
std::atomic<unsigned long long> g_run_counter{0};

// RAII wrapper over a WinAPI HANDLE. Ensures CloseHandle on destruction.
// Non-copyable; movable to allow returning from factory methods.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.Release()) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    ~UniqueHandle() {
        Reset();
    }

    [[nodiscard]] HANDLE Get() const { return handle_; }

    [[nodiscard]] bool IsValid() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Release() {
        HANDLE value = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return value;
    }

    void Reset(HANDLE handle = INVALID_HANDLE_VALUE) {
        if (IsValid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// Returns a copy of the string with leading and trailing whitespace removed.
std::string TrimCopy(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

// Returns a copy of the string converted to uppercase (ASCII-only).
std::string ToUpperCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

// Case-insensitive prefix check (used when parsing result.txt from LKH-3).
bool StartsWithCaseInsensitive(const std::string& text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (size_t idx = 0; idx < prefix.size(); ++idx) {
        if (std::toupper(static_cast<unsigned char>(text[idx])) !=
            std::toupper(static_cast<unsigned char>(prefix[idx]))) {
            return false;
        }
    }
    return true;
}

// Read an environment variable with whitespace trimming; returns empty string if not set.
// Used for parameters such as MTSP_LKH3_PATH (to override the binary path).
std::string ReadEnvironmentVariable(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : TrimCopy(value);
}

void WriteTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + path.string());
    }
    out << contents;
}

void AppendTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out.is_open()) {
        throw std::runtime_error("Could not open file for appending: " + path.string());
    }
    out << contents;
}

std::wstring ToWide(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring QuoteWindowsArg(const std::wstring& arg) {
    if (arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos) {
        std::wstring quoted = L"\"";
        size_t backslash_count = 0;
        for (const wchar_t ch : arg) {
            if (ch == L'\\') {
                ++backslash_count;
                continue;
            }
            if (ch == L'"') {
                quoted.append(backslash_count * 2 + 1, L'\\');
                quoted.push_back(L'"');
                backslash_count = 0;
                continue;
            }
            quoted.append(backslash_count, L'\\');
            backslash_count = 0;
            quoted.push_back(ch);
        }
        quoted.append(backslash_count * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }
    return arg;
}

std::string FormatWindowsError(const DWORD error_code) {
    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(flags,
                                        nullptr,
                                        error_code,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPSTR>(&buffer),
                                        0,
                                        nullptr);

    std::string message;
    if (length == 0 || buffer == nullptr) {
        message = "Windows error " + std::to_string(error_code);
    } else {
        message.assign(buffer, buffer + length);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

bool LooksLikeDrivePath(const std::string& path) {
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
}

std::vector<int> ParseIntegerTokens(const std::string& text) {
    std::vector<int> tokens;
    std::istringstream in(text);
    long long value = 0;
    while (in >> value) {
        tokens.push_back(static_cast<int>(value));
    }
    return tokens;
}

std::vector<int> ParseSintefRoute(const std::string& payload, const int node_count) {
    std::vector<int> route{0};
    for (const int node : ParseIntegerTokens(payload)) {
        if (node == 0) {
            continue;
        }
        if (node < 0 || node >= node_count) {
            throw std::runtime_error("SINTEF route contains node out of range: " + std::to_string(node));
        }
        route.push_back(node);
    }
    route.push_back(0);
    return route;
}

std::vector<int> ParseMtspSolutionRoute(const std::string& line, const int node_count) {
    size_t prefix_end = line.find("(#");
    if (prefix_end == std::string::npos) {
        prefix_end = line.find("Cost:");
    }
    const std::string prefix = TrimCopy(line.substr(0, prefix_end));
    const std::vector<int> tokens = ParseIntegerTokens(prefix);
    if (tokens.size() < 2) {
        throw std::runtime_error("Could not parse MTSP route line: " + line);
    }
    if (tokens.front() != 1 || tokens.back() != 1) {
        throw std::runtime_error("MTSP route line must start and end with depot 1: " + line);
    }

    std::vector<int> route{0};
    for (size_t idx = 1; idx + 1 < tokens.size(); ++idx) {
        const int tsplib_node = tokens[idx];
        if (tsplib_node == 1) {
            continue;
        }
        if (tsplib_node < 1 || tsplib_node > node_count) {
            throw std::runtime_error("MTSP route line contains node out of range: " + std::to_string(tsplib_node));
        }
        route.push_back(tsplib_node - 1);
    }
    route.push_back(0);
    return route;
}

struct ProcessLaunchResult {
    bool launched = false;
    long long exit_code = -1;
    std::string message;
};

UniqueHandle OpenInheritableFile(const std::filesystem::path& path, const DWORD access, const DWORD creation) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    attributes.lpSecurityDescriptor = nullptr;

    HANDLE handle = CreateFileW(path.c_str(),
                                access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &attributes,
                                creation,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateFileW failed for " + path.string() + ": " +
                                 FormatWindowsError(GetLastError()));
    }
    return UniqueHandle(handle);
}

ProcessLaunchResult LaunchWslProcess(const std::string& lkh3_wsl_bin,
                                     const std::string& params_wsl_path,
                                     const Lkh3BaselineArtifacts& artifacts) {
    ProcessLaunchResult result;

    UniqueHandle stdout_handle = OpenInheritableFile(artifacts.stdout_log, GENERIC_WRITE, CREATE_ALWAYS);
    UniqueHandle stderr_handle = OpenInheritableFile(artifacts.stderr_log, GENERIC_WRITE, CREATE_ALWAYS);
    UniqueHandle stdin_handle = OpenInheritableFile(L"NUL", GENERIC_READ, OPEN_EXISTING);

    std::wstring command_line =
        QuoteWindowsArg(std::wstring(kWslExecutable)) + L" " + QuoteWindowsArg(ToWide(lkh3_wsl_bin)) + L" " +
        QuoteWindowsArg(ToWide(params_wsl_path));
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = stdin_handle.Get();
    startup_info.hStdOutput = stdout_handle.Get();
    startup_info.hStdError = stderr_handle.Get();

    PROCESS_INFORMATION process_info{};
    const BOOL ok = CreateProcessW(std::wstring(kWslExecutable).c_str(),
                                   command_buffer.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   artifacts.work_dir.c_str(),
                                   &startup_info,
                                   &process_info);
    if (!ok) {
        result.message = "CreateProcessW failed: " + FormatWindowsError(GetLastError());
        return result;
    }

    result.launched = true;
    UniqueHandle process_handle(process_info.hProcess);
    UniqueHandle thread_handle(process_info.hThread);

    const DWORD wait_code = WaitForSingleObject(process_handle.Get(), INFINITE);
    if (wait_code != WAIT_OBJECT_0) {
        result.message = "WaitForSingleObject failed: " + FormatWindowsError(GetLastError());
        return result;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_handle.Get(), &exit_code)) {
        result.message = "GetExitCodeProcess failed: " + FormatWindowsError(GetLastError());
        return result;
    }

    result.exit_code = static_cast<long long>(exit_code);
    return result;
}

} // namespace

std::filesystem::path CreateLkh3BaselineRunDirectory() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "mtsp_lkh3_baseline_runs";
    std::filesystem::create_directories(root);

    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        const unsigned long long counter = g_run_counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path run_dir =
            root / ("run_" + std::to_string(millis) + "_" + std::to_string(GetCurrentProcessId()) + "_" +
                    std::to_string(counter));
        if (std::filesystem::create_directory(run_dir)) {
            return run_dir;
        }
    }

    throw std::runtime_error("Could not create a unique LKH3 baseline working directory under " + root.string());
}

std::string WindowsPathToWsl(const std::filesystem::path& path) {
    std::string text = path.lexically_normal().generic_string();
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.starts_with("/")) {
        return text;
    }
    if (!LooksLikeDrivePath(text)) {
        throw std::runtime_error("Cannot convert path to WSL form: " + text);
    }

    const char drive_letter = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    std::string suffix = text.substr(2);
    if (suffix.empty() || suffix.front() != '/') {
        suffix.insert(suffix.begin(), '/');
    }
    return "/mnt/" + std::string(1, drive_letter) + suffix;
}

std::string ToParameterObjective(const Lkh3MtspObjective objective) {
    return objective == Lkh3MtspObjective::Minmax ? "MINMAX" : "MINSUM";
}

void WriteLkh3ProblemFile(const std::filesystem::path& path, const Instance& inst) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Could not write LKH3 problem file: " + path.string());
    }

    out << std::setprecision(17);
    out << "NAME : MTSP_BASELINE\n";
    out << "TYPE : TSP\n";
    out << "DIMENSION : " << inst.GetNodeCount() << "\n";
    out << "EDGE_WEIGHT_TYPE : EXACT_2D\n";
    out << "SALESMEN : " << inst.GetSalesmanCount() << "\n";
    out << "NODE_COORD_SECTION\n";
    const auto& coords = inst.GetCoords();
    for (size_t idx = 0; idx < coords.size(); ++idx) {
        out << (idx + 1) << ' ' << coords[idx].first << ' ' << coords[idx].second << "\n";
    }
    out << "DEPOT_SECTION\n";
    out << "1\n";
    out << "-1\n";
    out << "EOF\n";
}

void WriteLkh3ParameterFile(const std::filesystem::path& path,
                            const int salesman_count,
                            const Lkh3MtspObjective objective,
                            const std::optional<double> time_limit_seconds) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Could not write LKH3 parameter file: " + path.string());
    }

    out << "PROBLEM_FILE = problem.tsp\n";
    out << "OUTPUT_TOUR_FILE = output.tour\n";
    out << "MTSP_SOLUTION_FILE = result.txt\n";
    out << "RUNS = 1\n";
    out << "TRACE_LEVEL = 1\n";
    out << "SEED = 1\n";
    out << "SALESMEN = " << salesman_count << "\n";
    out << "MTSP_OBJECTIVE = " << ToParameterObjective(objective) << "\n";
    out << "MTSP_MIN_SIZE = 1\n";
    out << "MTSP_MAX_SIZE = 999999999\n";
    out << "INITIAL_TOUR_ALGORITHM = MTSP\n";
    out << "CANDIDATE_SET_TYPE = POPMUSIC\n";
    out << "MAX_CANDIDATES = 5\n";
    out << "POPMUSIC_SAMPLE_SIZE = 10\n";
    out << "POPMUSIC_SOLUTIONS = 50\n";
    out << "POPMUSIC_MAX_NEIGHBORS = 5\n";
    out << "POPMUSIC_TRIALS = 1\n";
    out << "POPMUSIC_INITIAL_TOUR = NO\n";
    if (time_limit_seconds.has_value() && *time_limit_seconds > 0.0) {
        out << std::setprecision(6);
        out << "TIME_LIMIT = " << *time_limit_seconds << "\n";
    }
    out << "EOF\n";
}

RouteSet ParseMtspResultFile(const std::filesystem::path& path, const int salesman_count, const int node_count) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Could not open MTSP result file: " + path.string());
    }

    RouteSet routes;
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = TrimCopy(line);
        if (trimmed.empty()) {
            continue;
        }
        if (StartsWithCaseInsensitive(trimmed, "Route")) {
            const size_t colon = trimmed.find(':');
            const std::string payload = colon == std::string::npos ? std::string() : trimmed.substr(colon + 1);
            routes.push_back(ParseSintefRoute(payload, node_count));
            continue;
        }
        if (!trimmed.empty() && std::isdigit(static_cast<unsigned char>(trimmed.front())) &&
            (trimmed.find("(#") != std::string::npos || trimmed.find("Cost:") != std::string::npos)) {
            routes.push_back(ParseMtspSolutionRoute(trimmed, node_count));
        }
    }

    if (routes.empty()) {
        if (node_count == 1) {
            routes.assign(static_cast<size_t>(salesman_count), std::vector<int>{0, 0});
            return routes;
        }
        throw std::runtime_error("Could not parse any routes from MTSP result file: " + path.string());
    }
    if (static_cast<int>(routes.size()) > salesman_count) {
        throw std::runtime_error("Result file contains more routes than requested salesmen.");
    }
    while (static_cast<int>(routes.size()) < salesman_count) {
        routes.push_back({0, 0});
    }
    return routes;
}

Lkh3BaselineRunResult RunLkh3Baseline(const Lkh3MtspObjective objective,
                                      const std::optional<double> time_limit_seconds) {
    const Instance& inst = Instance::GetInstance();

    Lkh3BaselineRunResult result;
    result.objective_mode = ToParameterObjective(objective);

    try {
        result.artifacts.work_dir = CreateLkh3BaselineRunDirectory();
        result.artifacts.problem_file = result.artifacts.work_dir / "problem.tsp";
        result.artifacts.params_file = result.artifacts.work_dir / "params.par";
        result.artifacts.result_file = result.artifacts.work_dir / "result.txt";
        result.artifacts.output_tour_file = result.artifacts.work_dir / "output.tour";
        result.artifacts.stdout_log = result.artifacts.work_dir / "stdout.log";
        result.artifacts.stderr_log = result.artifacts.work_dir / "stderr.log";

        WriteLkh3ProblemFile(result.artifacts.problem_file, inst);
        WriteLkh3ParameterFile(result.artifacts.params_file, inst.GetSalesmanCount(), objective, time_limit_seconds);
        WriteTextFile(result.artifacts.stdout_log, "");
        WriteTextFile(result.artifacts.stderr_log, "");

        result.lkh3_wsl_bin = ReadEnvironmentVariable("LKH3_WSL_BIN");
        if (result.lkh3_wsl_bin.empty()) {
            result.status = "missing_lkh3_wsl_bin";
            result.message = "Environment variable LKH3_WSL_BIN is not set.";
            AppendTextFile(result.artifacts.stderr_log, result.message + "\n");
            return result;
        }

        if (LooksLikeDrivePath(result.lkh3_wsl_bin)) {
            result.lkh3_wsl_bin = WindowsPathToWsl(std::filesystem::path(result.lkh3_wsl_bin));
        }
        result.params_wsl_path = WindowsPathToWsl(result.artifacts.params_file);

        const ProcessLaunchResult launch =
            LaunchWslProcess(result.lkh3_wsl_bin, result.params_wsl_path, result.artifacts);
        result.exit_code = launch.exit_code;

        if (!launch.launched) {
            result.status = "wsl_launch_failed";
            result.message = launch.message;
            AppendTextFile(result.artifacts.stderr_log, result.message + "\n");
            return result;
        }
        if (launch.exit_code != 0) {
            result.status = "baseline_failed";
            result.message = "LKH3 process exited with code " + std::to_string(launch.exit_code) + ".";
            return result;
        }
        if (!std::filesystem::exists(result.artifacts.result_file)) {
            result.status = "missing_result_file";
            result.message = "LKH3 finished without creating result.txt.";
            return result;
        }

        result.routes = ParseMtspResultFile(result.artifacts.result_file, inst.GetSalesmanCount(), inst.GetNodeCount());
        result.valid = ValidateRoutes(result.routes);
        if (!result.valid) {
            result.status = "invalid_routes";
            result.message = "Parsed routes are not valid for the current mTSP instance.";
            result.routes.clear();
            return result;
        }

        result.objective = ObjectiveMinsum(result.routes);
        result.status = "ok";
        result.message.clear();
        return result;
    } catch (const std::exception& exc) {
        const bool has_result_file = std::filesystem::exists(result.artifacts.result_file);
        result.status = has_result_file ? "result_parse_error" : "runner_error";
        result.message = exc.what();
        try {
            AppendTextFile(result.artifacts.stderr_log, result.message + "\n");
        } catch (...) {
        }
        result.routes.clear();
        result.valid = false;
        result.objective = 0.0;
        return result;
    }
}

} // namespace mtsp::baselines

namespace mtsp {

namespace {

baselines::Lkh3MtspObjective ParseObjectiveOption(const std::string& value) {
    std::string normalized = value;
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    normalized.erase(normalized.begin(), std::find_if(normalized.begin(), normalized.end(), not_space));
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), not_space).base(), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (normalized == "MINMAX") {
        return baselines::Lkh3MtspObjective::Minmax;
    }
    if (normalized == "MINSUM") {
        return baselines::Lkh3MtspObjective::Minsum;
    }
    throw std::runtime_error("Unsupported lkh3-baseline objective: " + value);
}

class Lkh3BaselineSolver : public Solver {
public:
    void Configure(const std::unordered_map<std::string, std::string>& opts) override {
        if (const auto it = opts.find("objective"); it != opts.end()) {
            objective_ = ParseObjectiveOption(it->second);
        }
        if (const auto it = opts.find("mtsp-objective"); it != opts.end()) {
            objective_ = ParseObjectiveOption(it->second);
        }
        if (const auto it = opts.find("time-budget-ms"); it != opts.end()) {
            const double budget_ms = std::stod(it->second);
            if (budget_ms > 0.0) {
                time_limit_seconds_ = budget_ms / 1000.0;
            }
        }
        if (const auto it = opts.find("time-limit-seconds"); it != opts.end()) {
            const double time_limit = std::stod(it->second);
            if (time_limit > 0.0) {
                time_limit_seconds_ = time_limit;
            }
        }
    }

    std::string GetLastStatus() const override {
        return last_status_;
    }

    std::string GetLastMessage() const override {
        return last_message_;
    }

    std::unordered_map<std::string, std::string> GetLastMetadata() const override {
        return last_metadata_;
    }

    void Solve(RouteSet& out) override {
        last_status_ = "ok";
        last_message_.clear();
        last_metadata_.clear();

        const baselines::Lkh3BaselineRunResult result = baselines::RunLkh3Baseline(objective_, time_limit_seconds_);
        last_status_ = result.status;
        last_message_ = result.message;

        last_metadata_["objective_mode"] = result.objective_mode;
        last_metadata_["artifacts_dir"] = result.artifacts.work_dir.string();
        last_metadata_["problem_file"] = result.artifacts.problem_file.string();
        last_metadata_["params_file"] = result.artifacts.params_file.string();
        last_metadata_["result_file"] = result.artifacts.result_file.string();
        last_metadata_["output_tour_file"] = result.artifacts.output_tour_file.string();
        last_metadata_["stdout_log"] = result.artifacts.stdout_log.string();
        last_metadata_["stderr_log"] = result.artifacts.stderr_log.string();
        if (!result.lkh3_wsl_bin.empty()) {
            last_metadata_["lkh3_wsl_bin"] = result.lkh3_wsl_bin;
        }
        if (!result.params_wsl_path.empty()) {
            last_metadata_["params_wsl_path"] = result.params_wsl_path;
        }
        if (result.exit_code >= 0) {
            last_metadata_["exit_code"] = std::to_string(result.exit_code);
        }

        if (result.status == "ok") {
            out = result.routes;
            return;
        }
        out.clear();
    }

private:
    baselines::Lkh3MtspObjective objective_ = baselines::Lkh3MtspObjective::Minsum;
    std::optional<double> time_limit_seconds_;
    std::string last_status_ = "ok";
    std::string last_message_;
    std::unordered_map<std::string, std::string> last_metadata_;
};

} // namespace

static bool reg_lkh3_baseline = (SolverFactory::RegisterSolver("lkh3-baseline", []() {
    return std::make_unique<Lkh3BaselineSolver>();
}),
                                 true);

} // namespace mtsp
