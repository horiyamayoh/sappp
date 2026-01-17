/**
 * @file build_capture.cpp
 * @brief Implementation of build capture from compile_commands.json
 */

#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/version.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sappp {

namespace {

std::string to_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<std::string> split_command(const std::string& command) {
    std::vector<std::string> argv;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaping = false;

    auto flush = [&]() {
        if (!current.empty()) {
            argv.push_back(current);
            current.clear();
        }
    };

    for (char ch : command) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }

        if (ch == '\\' && !in_single_quote) {
            escaping = true;
            continue;
        }

        if (ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }

        if (ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        if (!in_single_quote && !in_double_quote && std::isspace(static_cast<unsigned char>(ch))) {
            flush();
            continue;
        }

        current.push_back(ch);
    }

    flush();
    return argv;
}

std::string detect_lang_from_file(const std::string& file_path) {
    std::filesystem::path path(file_path);
    std::string ext = to_lower(path.extension().string());
    if (ext == ".c") {
        return "c";
    }
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
        return "c++";
    }
    return "c++";
}

std::optional<std::string> extract_std_flag(const std::vector<std::string>& argv) {
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg.rfind("-std=", 0) == 0) {
            return arg.substr(5);
        }
        if (arg == "-std" && i + 1 < argv.size()) {
            return argv[i + 1];
        }
        if (arg.rfind("/std:", 0) == 0) {
            return arg.substr(5);
        }
    }
    return std::nullopt;
}

std::optional<std::string> extract_target_triple(const std::vector<std::string>& argv) {
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg.rfind("-target=", 0) == 0) {
            return arg.substr(8);
        }
        if (arg == "-target" && i + 1 < argv.size()) {
            return argv[i + 1];
        }
        if (arg.rfind("--target=", 0) == 0) {
            return arg.substr(9);
        }
    }
    return std::nullopt;
}

std::string detect_compiler_kind(const std::vector<std::string>& argv) {
    if (!argv.empty()) {
        std::string tool = to_lower(std::filesystem::path(argv.front()).filename().string());
        if (tool.find("clang-cl") != std::string::npos) {
            return "clang-cl";
        }
    }
    return "clang";
}

std::string detect_host_os() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string detect_host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

nlohmann::json default_target_for_host(const std::string& os, const std::string& arch) {
    std::string triple;
    std::string abi;

    if (os == "windows") {
        abi = "msvc";
        triple = arch + "-pc-windows-msvc";
    } else if (os == "macos") {
        abi = "darwin";
        triple = arch + "-apple-darwin";
    } else {
        abi = "gnu";
        triple = arch + "-unknown-linux-gnu";
    }

    int ptr_bits = (arch == "x86") ? 32 : 64;
    int long_bits = (os == "windows" && arch != "x86") ? 32 : ptr_bits;

    nlohmann::json data_layout = {
        {"ptr_bits", ptr_bits},
        {"long_bits", long_bits},
        {"align", {{"max", 16}}},
        {"sizes", {
            {"char", 1},
            {"short", 2},
            {"int", 4},
            {"long", long_bits / 8},
            {"long_long", 8},
            {"pointer", ptr_bits / 8}
        }}
    };

    return {
        {"triple", triple},
        {"abi", abi},
        {"data_layout", data_layout}
    };
}

std::string utc_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // namespace

BuildCapture::BuildCapture(std::string repo_root)
    : repo_root_(std::move(repo_root)) {}

BuildSnapshot BuildCapture::capture(const std::string& compile_commands_path) const {
    std::ifstream input(compile_commands_path);
    if (!input) {
        throw std::runtime_error("Failed to open compile_commands.json: " + compile_commands_path);
    }

    nlohmann::json compile_commands;
    input >> compile_commands;

    if (!compile_commands.is_array()) {
        throw std::runtime_error("compile_commands.json must be an array");
    }

    std::string host_os = detect_host_os();
    std::string host_arch = detect_host_arch();

    nlohmann::json snapshot;
    snapshot["schema_version"] = "build_snapshot.v1";
    snapshot["tool"] = {
        {"name", "sappp"},
        {"version", sappp::VERSION},
        {"build_id", sappp::BUILD_ID}
    };
    snapshot["generated_at"] = utc_timestamp();
    snapshot["host"] = {
        {"os", host_os},
        {"arch", host_arch}
    };

    std::vector<nlohmann::json> compile_units;
    compile_units.reserve(compile_commands.size());

    for (const auto& entry : compile_commands) {
        if (!entry.is_object()) {
            continue;
        }
        std::string cwd = entry.value("directory", "");
        std::string file = entry.value("file", "");

        if (cwd.empty()) {
            throw std::runtime_error("compile_commands entry missing directory");
        }
        if (file.empty()) {
            throw std::runtime_error("compile_commands entry missing file");
        }

        std::string normalized_cwd = sappp::common::normalize_path(cwd, repo_root_);

        std::vector<std::string> argv;
        if (entry.contains("arguments")) {
            argv = entry.at("arguments").get<std::vector<std::string>>();
        } else if (entry.contains("command")) {
            argv = split_command(entry.at("command").get<std::string>());
        }

        if (argv.empty()) {
            throw std::runtime_error("compile_commands entry missing arguments/command");
        }

        std::string lang = detect_lang_from_file(file);
        std::optional<std::string> std_opt = extract_std_flag(argv);
        std::string std_value = std_opt.value_or(lang == "c" ? "c17" : "c++17");

        nlohmann::json target = default_target_for_host(host_os, host_arch);
        if (auto triple = extract_target_triple(argv)) {
            target["triple"] = *triple;
        }

        nlohmann::json frontend = {
            {"kind", detect_compiler_kind(argv)},
            {"version", "unknown"}
        };

        nlohmann::json tu_input = {
            {"cwd", normalized_cwd},
            {"argv", argv},
            {"env_delta", nlohmann::json::object()},
            {"response_files_sha", nlohmann::json::array()},
            {"lang", lang},
            {"std", std_value},
            {"target", target}
        };

        std::string tu_id = sappp::canonical::hash_canonical(tu_input);

        nlohmann::json compile_unit = {
            {"tu_id", tu_id},
            {"cwd", normalized_cwd},
            {"argv", argv},
            {"lang", lang},
            {"std", std_value},
            {"target", target},
            {"frontend", frontend},
            {"env_delta", nlohmann::json::object()},
            {"response_files", nlohmann::json::array()}
        };

        compile_units.push_back(std::move(compile_unit));
    }

    if (compile_units.empty()) {
        throw std::runtime_error("compile_commands.json contains no entries");
    }

    snapshot["compile_units"] = compile_units;
    snapshot["input_digest"] = sappp::common::sha256_prefixed(compile_commands.dump());

    return snapshot;
}

} // namespace sappp
