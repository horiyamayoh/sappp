/**
 * @file build_capture.cpp
 * @brief Build capture implementation for compile_commands.json
 */

#include "sappp/build_capture.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sappp::build_capture {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open compile_commands.json: " + path);
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string current_time_utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &now_time);
#else
    gmtime_r(&now_time, &utc_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string detect_os() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string detect_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

std::string default_triple(const std::string& os, const std::string& arch) {
    if (os == "linux") {
        if (arch == "x86_64") return "x86_64-unknown-linux-gnu";
        if (arch == "aarch64") return "aarch64-unknown-linux-gnu";
        if (arch == "arm") return "arm-unknown-linux-gnueabihf";
        if (arch == "x86") return "i386-unknown-linux-gnu";
    } else if (os == "macos") {
        if (arch == "x86_64") return "x86_64-apple-darwin";
        if (arch == "aarch64") return "arm64-apple-darwin";
    } else if (os == "windows") {
        if (arch == "x86_64") return "x86_64-pc-windows-msvc";
        if (arch == "x86") return "i386-pc-windows-msvc";
        if (arch == "arm") return "arm-pc-windows-msvc";
        if (arch == "aarch64") return "aarch64-pc-windows-msvc";
    }
    return "unknown-unknown-unknown";
}

nlohmann::json default_target() {
    std::string os = detect_os();
    std::string arch = detect_arch();
    nlohmann::json data_layout = {
        {"ptr_bits", static_cast<int>(sizeof(void*) * 8)},
        {"long_bits", static_cast<int>(sizeof(long) * 8)},
        {"align", {{"max", static_cast<int>(alignof(std::max_align_t))}}}
    };
    return {
        {"triple", default_triple(os, arch)},
        {"abi", os == "windows" ? "msvc" : "sysv"},
        {"data_layout", data_layout}
    };
}

nlohmann::json default_frontend(const std::vector<std::string>& argv) {
    std::string kind = "clang";
    if (!argv.empty()) {
        std::string lower = argv.front();
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("clang-cl") != std::string::npos) {
            kind = "clang-cl";
        }
    }
    return {
        {"kind", kind},
        {"version", "unknown"}
    };
}

std::vector<std::string> parse_command_line(const std::string& command) {
    std::vector<std::string> args;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escape = false;

    for (char c : command) {
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\' && !in_single) {
            escape = true;
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !in_single && !in_double) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (escape) {
        current += '\\';
    }
    if (!current.empty()) {
        args.push_back(current);
    }
    return args;
}

std::string detect_lang_from_file(const std::string& file_path) {
    auto pos = file_path.find_last_of('.');
    if (pos == std::string::npos) {
        return "c++";
    }
    std::string ext = file_path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "c") {
        return "c";
    }
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++" || ext == "cp") {
        return "c++";
    }
    return "c++";
}

std::string extract_std(const std::vector<std::string>& argv, const std::string& lang) {
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg.rfind("-std=", 0) == 0) {
            return arg.substr(5);
        }
        if (arg == "-std" && i + 1 < argv.size()) {
            return argv[i + 1];
        }
    }
    return lang == "c" ? "c11" : "c++17";
}

nlohmann::json build_hash_input(const nlohmann::json& compile_unit) {
    nlohmann::json hash_input = {
        {"cwd", compile_unit.at("cwd")},
        {"argv", compile_unit.at("argv")},
        {"env_delta", compile_unit.at("env_delta")},
        {"response_files", compile_unit.at("response_files")},
        {"lang", compile_unit.at("lang")},
        {"std", compile_unit.at("std")},
        {"target", compile_unit.at("target")}
    };
    return hash_input;
}

} // namespace

BuildSnapshot::BuildSnapshot(nlohmann::json json)
    : m_json(std::move(json)) {}

const nlohmann::json& BuildSnapshot::json() const {
    return m_json;
}

nlohmann::json& BuildSnapshot::json() {
    return m_json;
}

BuildCapture::BuildCapture(std::string repo_root, std::string schema_dir)
    : m_repo_root(std::move(repo_root)),
      m_schema_dir(std::move(schema_dir)) {}

BuildSnapshot BuildCapture::capture(const std::string& compile_commands_path) {
    std::string raw = read_file(compile_commands_path);
    nlohmann::json compile_db = nlohmann::json::parse(raw);
    if (!compile_db.is_array() || compile_db.empty()) {
        throw std::runtime_error("compile_commands.json must be a non-empty array");
    }

    nlohmann::json target = default_target();

    std::vector<nlohmann::json> units;
    units.reserve(compile_db.size());

    for (const auto& entry : compile_db) {
        if (!entry.is_object()) {
            throw std::runtime_error("compile_commands entry is not an object");
        }
        if (!entry.contains("directory") || !entry.contains("file")) {
            throw std::runtime_error("compile_commands entry missing directory or file");
        }

        std::string directory = entry.at("directory").get<std::string>();
        std::string file_path = entry.at("file").get<std::string>();

        std::vector<std::string> argv;
        if (entry.contains("arguments")) {
            const auto& args_json = entry.at("arguments");
            if (!args_json.is_array()) {
                throw std::runtime_error("compile_commands arguments must be an array");
            }
            for (const auto& arg : args_json) {
                argv.push_back(arg.get<std::string>());
            }
        } else if (entry.contains("command")) {
            argv = parse_command_line(entry.at("command").get<std::string>());
        } else {
            throw std::runtime_error("compile_commands entry missing arguments/command");
        }

        if (argv.empty()) {
            throw std::runtime_error("compile_commands entry has empty argv");
        }

        std::string cwd = common::normalize_path(directory, m_repo_root);
        std::string normalized_file = common::normalize_path(file_path, m_repo_root);
        std::string lang = detect_lang_from_file(normalized_file);
        std::string std_value = extract_std(argv, lang);
        nlohmann::json frontend = default_frontend(argv);

        nlohmann::json unit = {
            {"cwd", cwd},
            {"argv", argv},
            {"env_delta", nlohmann::json::object()},
            {"response_files", nlohmann::json::array()},
            {"lang", lang},
            {"std", std_value},
            {"target", target},
            {"frontend", frontend}
        };

        nlohmann::json hash_input = build_hash_input(unit);
        std::string tu_id = canonical::hash_canonical(hash_input);
        unit["tu_id"] = tu_id;

        units.push_back(std::move(unit));
    }

    std::stable_sort(units.begin(), units.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.at("tu_id").get<std::string>() < b.at("tu_id").get<std::string>();
                     });

    nlohmann::json snapshot = {
        {"schema_version", "build_snapshot.v1"},
        {"tool", {{"name", "sappp"}, {"version", sappp::VERSION}, {"build_id", sappp::BUILD_ID}}},
        {"generated_at", current_time_utc()},
        {"host", {{"os", detect_os()}, {"arch", detect_arch()}}},
        {"compile_units", units}
    };
    snapshot["input_digest"] = common::sha256_prefixed(raw);

    std::filesystem::path schema_path = std::filesystem::path(m_schema_dir) / "build_snapshot.v1.schema.json";
    std::string schema_error;
    if (!common::validate_json(snapshot, schema_path.string(), schema_error)) {
        throw std::runtime_error("build_snapshot schema validation failed: " + schema_error);
    }

    return BuildSnapshot(std::move(snapshot));
}

} // namespace sappp::build_capture
