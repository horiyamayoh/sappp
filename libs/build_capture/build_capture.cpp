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
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace sappp::build_capture {

namespace {

[[nodiscard]] sappp::Result<std::string> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(Error::make("CompileCommandsOpenFailed",
                                           "Failed to open compile_commands.json: " + path));
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] std::string detect_os()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

[[nodiscard]] std::string detect_arch()
{
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

struct TripleEntry
{
    std::string_view os;
    std::string_view arch;
    std::string_view triple;
};

constexpr std::array<TripleEntry, 10> kDefaultTriples = {
    {
     {.os = "linux", .arch = "x86_64", .triple = "x86_64-unknown-linux-gnu"},
     {.os = "linux", .arch = "aarch64", .triple = "aarch64-unknown-linux-gnu"},
     {.os = "linux", .arch = "arm", .triple = "arm-unknown-linux-gnueabihf"},
     {.os = "linux", .arch = "x86", .triple = "i386-unknown-linux-gnu"},
     {.os = "macos", .arch = "x86_64", .triple = "x86_64-apple-darwin"},
     {.os = "macos", .arch = "aarch64", .triple = "arm64-apple-darwin"},
     {.os = "windows", .arch = "x86_64", .triple = "x86_64-pc-windows-msvc"},
     {.os = "windows", .arch = "x86", .triple = "i386-pc-windows-msvc"},
     {.os = "windows", .arch = "arm", .triple = "arm-pc-windows-msvc"},
     {.os = "windows", .arch = "aarch64", .triple = "aarch64-pc-windows-msvc"},
     }
};

[[nodiscard]] std::string default_triple(std::string_view os, std::string_view arch)
{
    for (const auto& entry : kDefaultTriples) {
        if (entry.os == os && entry.arch == arch) {
            return std::string(entry.triple);
        }
    }
    return "unknown-unknown-unknown";
}

[[nodiscard]] nlohmann::json default_target()
{
    std::string os = detect_os();
    std::string arch = detect_arch();
    nlohmann::json data_layout = {
        { "ptr_bits",                    static_cast<int>(sizeof(void*) * 8)},
        {"long_bits",                     static_cast<int>(sizeof(long) * 8)},
        {    "align", {{"max", static_cast<int>(alignof(std::max_align_t))}}}
    };
    return {
        {"triple", default_triple(os, arch)},
        {"abi", os == "windows" ? "msvc" : "sysv"},
        {"data_layout", data_layout}
    };
}

[[nodiscard]] nlohmann::json default_frontend(const std::vector<std::string>& argv)
{
    std::string kind = "clang";
    if (!argv.empty()) {
        std::string lower = argv.front();
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) noexcept {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.contains("clang-cl")) {
            kind = "clang-cl";
        }
    }
    return {
        {   "kind",      kind},
        {"version", "unknown"}
    };
}

[[nodiscard]] std::vector<std::string> parse_command_line(const std::string& command)
{
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
        if (std::isspace(static_cast<unsigned char>(c)) != 0 && !in_single && !in_double) {
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

[[nodiscard]] std::string detect_lang_from_file(const std::string& file_path)
{
    auto pos = file_path.find_last_of('.');
    if (pos == std::string::npos) {
        return "c++";
    }
    std::string ext = file_path.substr(pos + 1);
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) noexcept {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == "c") {
        return "c";
    }
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++" || ext == "cp") {
        return "c++";
    }
    return "c++";
}

[[nodiscard]] std::string extract_std(const std::vector<std::string>& argv, const std::string& lang)
{
    for (auto [i, arg] : std::views::enumerate(argv)) {
        if (arg.starts_with("-std=")) {
            return arg.substr(5);
        }
        if (arg == "-std" && i + 1 < std::ssize(argv)) {
            return argv[static_cast<std::size_t>(i + 1)];
        }
    }
    return lang == "c" ? "c23" : "c++23";
}

[[nodiscard]] nlohmann::json build_hash_input(const nlohmann::json& compile_unit)
{
    nlohmann::json hash_input = {
        {           "cwd",            compile_unit.at("cwd")},
        {          "argv",           compile_unit.at("argv")},
        {     "env_delta",      compile_unit.at("env_delta")},
        {"response_files", compile_unit.at("response_files")},
        {          "lang",           compile_unit.at("lang")},
        {           "std",            compile_unit.at("std")},
        {        "target",         compile_unit.at("target")}
    };
    return hash_input;
}

[[nodiscard]] sappp::Result<nlohmann::json> parse_compile_database(const std::string& raw)
{
    nlohmann::json compile_db;
    try {
        compile_db = nlohmann::json::parse(raw);
    } catch (const std::exception& ex) {
        return std::unexpected(
            Error::make("CompileCommandsParseFailed",
                        std::string("Failed to parse compile_commands.json: ") + ex.what()));
    }
    if (!compile_db.is_array() || compile_db.empty()) {
        return std::unexpected(Error::make("CompileCommandsInvalid",
                                           "compile_commands.json must be a non-empty array"));
    }
    return compile_db;
}

[[nodiscard]] sappp::Result<std::vector<std::string>> extract_argv(const nlohmann::json& entry,
                                                                   std::size_t index)
{
    if (entry.contains("arguments")) {
        const auto& args_json = entry.at("arguments");
        if (!args_json.is_array()) {
            return std::unexpected(Error::make(
                "CompileCommandsEntryInvalid",
                std::format("compile_commands entry {} arguments must be an array", index)));
        }
        std::vector<std::string> argv;
        argv.reserve(args_json.size());
        for (const auto& arg : args_json) {
            argv.push_back(arg.get<std::string>());
        }
        return argv;
    }
    if (entry.contains("command")) {
        return parse_command_line(entry.at("command").get<std::string>());
    }
    return std::unexpected(
        Error::make("CompileCommandsEntryInvalid",
                    std::format("compile_commands entry {} missing arguments/command", index)));
}

[[nodiscard]] sappp::Result<nlohmann::json> build_compile_unit(const nlohmann::json& entry,
                                                               std::string_view repo_root,
                                                               const nlohmann::json& target,
                                                               std::size_t index)
{
    if (!entry.is_object()) {
        return std::unexpected(
            Error::make("CompileCommandsEntryInvalid",
                        std::format("compile_commands entry {} is not an object", index)));
    }
    if (!entry.contains("directory") || !entry.contains("file")) {
        return std::unexpected(
            Error::make("CompileCommandsEntryInvalid",
                        std::format("compile_commands entry {} missing directory or file", index)));
    }

    std::string directory = entry.at("directory").get<std::string>();
    std::string file_path = entry.at("file").get<std::string>();

    auto argv = extract_argv(entry, index);
    if (!argv) {
        return std::unexpected(argv.error());
    }
    if (argv->empty()) {
        return std::unexpected(
            Error::make("CompileCommandsEntryInvalid",
                        std::format("compile_commands entry {} has empty argv", index)));
    }

    std::string cwd = common::normalize_path(directory, repo_root);
    std::string normalized_file = common::normalize_path(file_path, repo_root);
    std::string lang = detect_lang_from_file(normalized_file);
    std::string std_value = extract_std(*argv, lang);
    nlohmann::json frontend = default_frontend(*argv);

    nlohmann::json unit = {
        {           "cwd",                      cwd},
        {          "argv",                    *argv},
        {     "env_delta", nlohmann::json::object()},
        {"response_files",  nlohmann::json::array()},
        {          "lang",                     lang},
        {           "std",                std_value},
        {        "target",                   target},
        {      "frontend",                 frontend}
    };

    nlohmann::json hash_input = build_hash_input(unit);
    auto tu_id_result = canonical::hash_canonical(hash_input);
    if (!tu_id_result) {
        return std::unexpected(tu_id_result.error());
    }
    unit["tu_id"] = *tu_id_result;

    return unit;
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
build_compile_units(const nlohmann::json& compile_db,
                    std::string_view repo_root,
                    const nlohmann::json& target)
{
    std::vector<nlohmann::json> units;
    units.reserve(compile_db.size());
    for (auto [index, entry] : std::views::enumerate(compile_db)) {
        auto unit = build_compile_unit(entry, repo_root, target, static_cast<std::size_t>(index));
        if (!unit) {
            return std::unexpected(unit.error());
        }
        units.push_back(std::move(*unit));
    }
    return units;
}

}  // namespace

BuildSnapshot::BuildSnapshot(nlohmann::json json)
    : m_json(std::move(json))
{}

BuildCapture::BuildCapture(std::string repo_root, std::string schema_dir)
    : m_repo_root(std::move(repo_root))
    , m_schema_dir(std::move(schema_dir))
{}

sappp::Result<BuildSnapshot> BuildCapture::capture(const std::string& compile_commands_path)
{
    auto raw = read_file(compile_commands_path);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    auto compile_db = parse_compile_database(*raw);
    if (!compile_db) {
        return std::unexpected(compile_db.error());
    }

    nlohmann::json target = default_target();

    auto units = build_compile_units(*compile_db, m_repo_root, target);
    if (!units) {
        return std::unexpected(units.error());
    }

    std::ranges::stable_sort(*units, [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("tu_id").get<std::string>() < b.at("tu_id").get<std::string>();
    });

    nlohmann::json snapshot = {
        {"schema_version",                                                              "build_snapshot.v1"},
        {          "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {  "generated_at",                                                               current_time_utc()},
        {          "host",                                   {{"os", detect_os()}, {"arch", detect_arch()}}},
        { "compile_units",                                                                           *units}
    };
    snapshot["input_digest"] = common::sha256_prefixed(*raw);

    std::filesystem::path schema_path =
        std::filesystem::path(m_schema_dir) / "build_snapshot.v1.schema.json";
    if (auto result = common::validate_json(snapshot, schema_path.string()); !result) {
        return std::unexpected(result.error());
    }

    return BuildSnapshot(std::move(snapshot));
}

}  // namespace sappp::build_capture
