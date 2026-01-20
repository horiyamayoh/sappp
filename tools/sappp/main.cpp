/**
 * @file main.cpp
 * @brief SAP++ CLI entry point
 *
 * C++23 modernization:
 * - Using std::print/std::println (C++23)
 * - Using std::string_view where appropriate
 *
 * Commands:
 *   capture   - Capture build conditions from compile_commands.json
 *   analyze   - Run static analysis
 *   validate  - Validate certificates and confirm SAFE/BUG
 *   pack      - Create reproducibility pack
 *   diff      - Compare analysis results
 *   explain   - Explain UNKNOWN entries
 *   version   - Show version information
 */

#include "po_generator.hpp"
#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
    #include "frontend_clang/frontend.hpp"
#endif
#include <charconv>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] [[maybe_unused]] std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] [[maybe_unused]] nlohmann::json tool_metadata_json()
{
    return nlohmann::json{
        {    "name",         "sappp"},
        { "version", sappp::kVersion},
        {"build_id", sappp::kBuildId}
    };
}

void print_version()
{
    std::println("sappp {} ({})", sappp::kVersion, sappp::kBuildId);
    std::println("  semantics:    {}", sappp::kSemanticsVersion);
    std::println("  proof_system: {}", sappp::kProofSystemVersion);
    std::println("  profile:      {}", sappp::kProfileVersion);
}

void print_help()
{
    std::print(R"(SAP++ - Sound, Static Absence-Proving Analyzer for C++

Usage: sappp <command> [options]

Commands:
  capture     Capture build conditions from compile_commands.json
  analyze     Run static analysis on captured build
  validate    Validate certificates and confirm SAFE/BUG results
  pack        Create reproducibility pack (tar.gz + manifest)
  diff        Compare before/after analysis results
  explain     Explain UNKNOWN entries in human-readable form
  version     Show version information

Global Options:
  --help, -h              Show this help message
  --version               Show version information
  -v, --verbose           Verbose logging
  -q, --quiet             Quiet mode (errors only)
  --json-logs PATH        Write JSONL logs to file
  --jobs N, -j N           Number of parallel jobs (default: auto)
  --schema-dir DIR        Path to schema directory
  --semantics VERSION     Semantics version (default: sem.v1)
  --proof VERSION         Proof system version (default: proof.v1)
  --profile VERSION       Profile version (default: safety.core.v1)

Run 'sappp <command> --help' for command-specific options.
)");
}

void print_capture_help()
{
    std::print(R"(Usage: sappp capture [options]

Capture build conditions from compile_commands.json

Options:
  --compile-commands FILE   Path to compile_commands.json (required)
  --out FILE, -o            Output file (default: build_snapshot.json)
  --repo-root DIR           Repository root for relative paths
  --help, -h                Show this help

Output:
  build_snapshot.json
)");
}

void print_analyze_help()
{
    std::print(R"(Usage: sappp analyze [options]

Run static analysis on captured build

Options:
  --build FILE              Path to build_snapshot.json (required)
  --spec FILE               Path to Spec DB snapshot
  --out DIR, -o             Output directory (required)
  --jobs N, -j N            Number of parallel jobs
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --analysis-config FILE    Analysis configuration file
  --emit-sarif FILE         SARIF output path
  --repro-level LEVEL       Repro asset level (L0/L1/L2/L3)
  --help, -h                Show this help

Output:
  <output>/frontend/nir.json
  <output>/frontend/source_map.json
  <output>/po/po_list.json
  <output>/analyzer/unknown_ledger.json
  <output>/certstore/
  <output>/config/analysis_config.json
)");
}

void print_validate_help()
{
    std::print(R"(Usage: sappp validate [options]

Validate certificates and confirm SAFE/BUG results

Options:
  --input DIR, --in DIR     Input directory containing analysis outputs (required)
  --out FILE, -o            Output file (default: <input>/results/validated_results.json)
  --strict                  Fail on any validation error (no downgrade)
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --help, -h                Show this help

Output:
  validated_results.json
)");
}

void print_pack_help()
{
    std::print(R"(Usage: sappp pack [options]

Create reproducibility pack

Options:
  --input DIR               Input directory containing analysis outputs (required)
  --output FILE, -o         Output file (default: pack.tar.gz)
  --help, -h                Show this help

Output:
  <output>.tar.gz
  <output>.manifest.json
)");
}

void print_diff_help()
{
    std::print(R"(Usage: sappp diff [options]

Compare before/after analysis results

Options:
  --before FILE             Path to before validated_results.json (required)
  --after FILE              Path to after validated_results.json (required)
  --output FILE, -o         Output file (default: diff.json)
  --help, -h                Show this help

Output:
  diff.json
)");
}

struct LoggingOptions
{
    bool verbose = false;
    bool quiet = false;
    std::string json_logs;
};

struct CaptureOptions
{
    std::string compile_commands;
    std::string repo_root;
    std::string output_path;
    sappp::VersionTriple versions;
    LoggingOptions logging;
    bool show_help;
};

struct AnalyzeOptions
{
    std::string build;
    std::string spec;
    int jobs;
    std::string output;
    std::string schema_dir;
    std::string analysis_config;
    std::string emit_sarif;
    std::string repro_level;
    sappp::VersionTriple versions;
    LoggingOptions logging;
    bool show_help;
};

struct ValidateOptions
{
    std::string input;
    bool strict;
    std::string output;
    std::string schema_dir;
    sappp::VersionTriple versions;
    LoggingOptions logging;
    bool show_help;
};

struct PackOptions
{
    std::string input;
    std::string output;
    bool show_help;
};

struct DiffOptions
{
    std::string before;
    std::string after;
    std::string output;
    bool show_help;
};

struct AnalyzePaths
{
    std::filesystem::path output_dir;
    std::filesystem::path frontend_dir;
    std::filesystem::path po_dir;
    std::filesystem::path analyzer_dir;
    std::filesystem::path certstore_dir;
    std::filesystem::path certstore_objects_dir;
    std::filesystem::path certstore_index_dir;
    std::filesystem::path config_dir;
    std::filesystem::path nir_path;
    std::filesystem::path source_map_path;
    std::filesystem::path po_path;
    std::filesystem::path unknown_ledger_path;
    std::filesystem::path analysis_config_path;
};

[[nodiscard]] sappp::Result<std::string>
read_option_value(std::span<char*> args, std::size_t index, std::string_view option)
{
    const std::size_t value_index = index + 1;
    if (value_index >= args.size()) {
        return std::unexpected(
            sappp::Error::make("MissingArgument",
                               std::string("Missing value for option: ") + std::string(option)));
    }
    const char* value_ptr = args[value_index];
    if (value_ptr == nullptr) {
        return std::unexpected(
            sappp::Error::make("MissingArgument",
                               std::string("Missing value for option: ") + std::string(option)));
    }
    return std::string(value_ptr);
}

[[nodiscard]] sappp::Result<int> parse_jobs_value(std::string_view value)
{
    int parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        return std::unexpected(
            sappp::Error::make("InvalidArgument",
                               std::string("Invalid --jobs value: ") + std::string(value)));
    }
    return parsed;
}

[[nodiscard]] sappp::Result<bool> set_logging_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     LoggingOptions& logging,
                                                     bool& skip_next)
{
    if (arg == "-v" || arg == "--verbose") {
        logging.verbose = true;
        logging.quiet = false;
        return true;
    }
    if (arg == "-q" || arg == "--quiet") {
        logging.quiet = true;
        logging.verbose = false;
        return true;
    }
    if (arg == "--json-logs") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        logging.json_logs = *value;
        skip_next = true;
        return true;
    }
    return false;
}

[[nodiscard]] sappp::Result<bool> set_version_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     sappp::VersionTriple& versions,
                                                     bool& skip_next)
{
    if (arg == "--semantics") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        versions.semantics = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--proof") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        versions.proof_system = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--profile") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        versions.profile = *value;
        skip_next = true;
        return true;
    }
    return false;
}

enum class ExitCode { kOk = 0, kCliError = 1, kInputError = 2, kInternalError = 3 };

[[nodiscard]] int exit_code_for_error(const sappp::Error& error)
{
    if (error.code == "MissingArgument" || error.code == "InvalidArgument") {
        return static_cast<int>(ExitCode::kCliError);
    }
    if (error.code == "ClangToolFailed" || error.code == "PoGenerationFailed"
        || error.code == "RuleViolation" || error.code == "NirEmpty") {
        return static_cast<int>(ExitCode::kInternalError);
    }
    return static_cast<int>(ExitCode::kInputError);
}

#if defined(SAPPP_HAS_CLANG_FRONTEND)
[[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open JSON file: " + path.string()));
    }
    nlohmann::json payload;
    try {
        in >> payload;
    } catch (const std::exception& ex) {
        return std::unexpected(
            sappp::Error::make("ParseError",
                               "Failed to parse JSON file: " + path.string() + ": " + ex.what()));
    }
    return payload;
}
#endif

[[nodiscard]] sappp::VoidResult write_canonical_json_file(const std::filesystem::path& path,
                                                          const nlohmann::json& payload)
{
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open output file: " + path.string()));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical << "\n";
    if (!out) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to write output file: " + path.string()));
    }
    return {};
}

[[nodiscard]] sappp::VoidResult ensure_directory(const std::filesystem::path& dir,
                                                 std::string_view label)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(sappp::Error::make(
            "IOError",
            std::string("Failed to create ") + std::string(label) + " directory: " + ec.message()));
    }
    return {};
}

#if defined(SAPPP_HAS_CLANG_FRONTEND)
[[nodiscard]] sappp::Result<AnalyzePaths> prepare_analyze_paths(std::string_view output)
{
    auto output_dir = std::filesystem::path(output);
    auto frontend_dir = output_dir / "frontend";
    auto po_dir = output_dir / "po";
    auto analyzer_dir = output_dir / "analyzer";
    auto certstore_dir = output_dir / "certstore";
    auto certstore_objects_dir = certstore_dir / "objects";
    auto certstore_index_dir = certstore_dir / "index";
    auto config_dir = output_dir / "config";
    if (auto result = ensure_directory(frontend_dir, "frontend"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(po_dir, "po"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(analyzer_dir, "analyzer"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(certstore_objects_dir, "certstore objects"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(certstore_index_dir, "certstore index"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(config_dir, "config"); !result) {
        return std::unexpected(result.error());
    }
    auto nir_path = frontend_dir / "nir.json";
    auto source_map_path = frontend_dir / "source_map.json";
    auto po_path = po_dir / "po_list.json";
    auto unknown_ledger_path = analyzer_dir / "unknown_ledger.json";
    auto analysis_config_path = config_dir / "analysis_config.json";
    return AnalyzePaths{output_dir,
                        frontend_dir,
                        po_dir,
                        analyzer_dir,
                        certstore_dir,
                        certstore_objects_dir,
                        certstore_index_dir,
                        config_dir,
                        nir_path,
                        source_map_path,
                        po_path,
                        unknown_ledger_path,
                        analysis_config_path};
}

[[nodiscard]] sappp::Result<nlohmann::json> load_analysis_config(const AnalyzeOptions& options)
{
    std::filesystem::path schema_dir(options.schema_dir);
    const auto schema_path = (schema_dir / "analysis_config.v1.schema.json").string();
    if (!options.analysis_config.empty()) {
        auto config_json = read_json_file(options.analysis_config);
        if (!config_json) {
            return std::unexpected(config_json.error());
        }
        if (auto validation = sappp::common::validate_json(*config_json, schema_path);
            !validation) {
            return std::unexpected(validation.error());
        }
        return *config_json;
    }

    nlohmann::json analysis_settings = {
        {"budget", nlohmann::json::object()}
    };
    if (options.jobs > 0) {
        analysis_settings["jobs"] = options.jobs;
    }

    nlohmann::json config_json = {
        {      "schema_version",          "analysis_config.v1"},
        {                "tool",          tool_metadata_json()},
        {        "generated_at",            current_time_utc()},
        {   "semantics_version",    options.versions.semantics},
        {"proof_system_version", options.versions.proof_system},
        {     "profile_version",      options.versions.profile},
        {            "analysis",             analysis_settings}
    };

    if (auto validation = sappp::common::validate_json(config_json, schema_path); !validation) {
        return std::unexpected(validation.error());
    }
    return config_json;
}

[[nodiscard]] sappp::Result<nlohmann::json>
build_unknown_ledger_json(const nlohmann::json& nir_json, const AnalyzeOptions& options)
{
    nlohmann::json unknown_json = {
        {      "schema_version",                  "unknown.v1"},
        {                "tool",           nir_json.at("tool")},
        {        "generated_at",            current_time_utc()},
        {               "tu_id",          nir_json.at("tu_id")},
        {            "unknowns",       nlohmann::json::array()},
        {   "semantics_version",    options.versions.semantics},
        {"proof_system_version", options.versions.proof_system},
        {     "profile_version",      options.versions.profile}
    };

    if (nir_json.contains("input_digest")) {
        unknown_json["input_digest"] = nir_json.at("input_digest");
    }

    const std::filesystem::path schema_path =
        std::filesystem::path(options.schema_dir) / "unknown.v1.schema.json";
    if (auto validation = sappp::common::validate_json(unknown_json, schema_path.string());
        !validation) {
        return std::unexpected(validation.error());
    }
    return unknown_json;
}
#endif

[[nodiscard]] sappp::Result<bool> set_analyze_primary_option(std::string_view arg,
                                                             std::span<char*> args,
                                                             std::size_t idx,
                                                             AnalyzeOptions& options,
                                                             bool& skip_next)
{
    if (arg == "--build" || arg == "--snapshot") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.build = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--spec") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.spec = *value;
        skip_next = true;
        return true;
    }
    return false;
}

[[nodiscard]] sappp::Result<bool> set_analyze_job_option(std::string_view arg,
                                                         std::span<char*> args,
                                                         std::size_t idx,
                                                         AnalyzeOptions& options,
                                                         bool& skip_next)
{
    if (arg != "--jobs" && arg != "-j") {
        return false;
    }
    auto value = read_option_value(args, idx, arg);
    if (!value) {
        return std::unexpected(value.error());
    }
    auto parsed = parse_jobs_value(*value);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    options.jobs = *parsed;
    skip_next = true;
    return true;
}

[[nodiscard]] sappp::Result<bool> set_analyze_extra_option(std::string_view arg,
                                                           std::span<char*> args,
                                                           std::size_t idx,
                                                           AnalyzeOptions& options,
                                                           bool& skip_next)
{
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--analysis-config") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.analysis_config = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--emit-sarif") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.emit_sarif = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--repro-level") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.repro_level = *value;
        skip_next = true;
        return true;
    }
    return false;
}

[[nodiscard]] sappp::Result<bool> set_analyze_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     AnalyzeOptions& options,
                                                     bool& skip_next)
{
    auto handled = set_analyze_primary_option(arg, args, idx, options, skip_next);
    if (!handled) {
        return std::unexpected(handled.error());
    }
    if (*handled) {
        return true;
    }
    handled = set_analyze_job_option(arg, args, idx, options, skip_next);
    if (!handled) {
        return std::unexpected(handled.error());
    }
    if (*handled) {
        return true;
    }
    return set_analyze_extra_option(arg, args, idx, options, skip_next);
}

[[nodiscard]] sappp::Result<bool> set_validate_option(std::string_view arg,
                                                      std::span<char*> args,
                                                      std::size_t idx,
                                                      ValidateOptions& options,
                                                      bool& skip_next)
{
    if (arg == "--input" || arg == "--in") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.input = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--strict") {
        options.strict = true;
        return true;
    }
    return false;
}

[[nodiscard]] sappp::Result<bool> set_capture_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     CaptureOptions& options,
                                                     bool& skip_next)
{
    if (arg == "--compile-commands") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.compile_commands = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output_path = *value;
        skip_next = true;
        return true;
    }
    if (arg == "--repo-root") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.repo_root = *value;
        skip_next = true;
        return true;
    }
    return false;
}

[[nodiscard]] sappp::Result<CaptureOptions> parse_capture_args(std::span<char*> args)
{
    CaptureOptions options{.compile_commands = std::string{},
                           .repo_root = std::string{},
                           .output_path = "build_snapshot.json",
                           .versions = sappp::default_version_triple(),
                           .logging = LoggingOptions{},
                           .show_help = false};
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (arg_ptr == nullptr) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg(arg_ptr);
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        auto logging = set_logging_option(arg, args, idx, options.logging, skip_next);
        if (!logging) {
            return std::unexpected(logging.error());
        }
        if (*logging) {
            continue;
        }
        auto version = set_version_option(arg, args, idx, options.versions, skip_next);
        if (!version) {
            return std::unexpected(version.error());
        }
        if (*version) {
            continue;
        }
        auto handled = set_capture_option(arg, args, idx, options, skip_next);
        if (!handled) {
            return std::unexpected(handled.error());
        }
        if (*handled) {
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<AnalyzeOptions> parse_analyze_args(std::span<char*> args)
{
    AnalyzeOptions options{.build = std::string{},
                           .spec = std::string{},
                           .jobs = 0,
                           .output = std::string{},
                           .schema_dir = "schemas",
                           .analysis_config = std::string{},
                           .emit_sarif = std::string{},
                           .repro_level = std::string{},
                           .versions = sappp::default_version_triple(),
                           .logging = LoggingOptions{},
                           .show_help = false};
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (arg_ptr == nullptr) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg(arg_ptr);
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        auto logging = set_logging_option(arg, args, idx, options.logging, skip_next);
        if (!logging) {
            return std::unexpected(logging.error());
        }
        if (*logging) {
            continue;
        }
        auto version = set_version_option(arg, args, idx, options.versions, skip_next);
        if (!version) {
            return std::unexpected(version.error());
        }
        if (*version) {
            continue;
        }
        auto handled = set_analyze_option(arg, args, idx, options, skip_next);
        if (!handled) {
            return std::unexpected(handled.error());
        }
        if (*handled) {
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<ValidateOptions> parse_validate_args(std::span<char*> args)
{
    ValidateOptions options{.input = std::string{},
                            .strict = false,
                            .output = std::string{},
                            .schema_dir = "schemas",
                            .versions = sappp::default_version_triple(),
                            .logging = LoggingOptions{},
                            .show_help = false};
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (arg_ptr == nullptr) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg(arg_ptr);
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        auto logging = set_logging_option(arg, args, idx, options.logging, skip_next);
        if (!logging) {
            return std::unexpected(logging.error());
        }
        if (*logging) {
            continue;
        }
        auto version = set_version_option(arg, args, idx, options.versions, skip_next);
        if (!version) {
            return std::unexpected(version.error());
        }
        if (*version) {
            continue;
        }
        auto handled = set_validate_option(arg, args, idx, options, skip_next);
        if (!handled) {
            return std::unexpected(handled.error());
        }
        if (*handled) {
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<PackOptions> parse_pack_args(std::span<char*> args)
{
    PackOptions options{.input = std::string{}, .output = "pack.tar.gz", .show_help = false};
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (arg_ptr == nullptr) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg(arg_ptr);
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        if (arg == "--input") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.input = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--output" || arg == "-o") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.output = *value;
            skip_next = true;
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<DiffOptions> parse_diff_args(std::span<char*> args)
{
    DiffOptions options{.before = std::string{},
                        .after = std::string{},
                        .output = "diff.json",
                        .show_help = false};
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        if (arg_ptr == nullptr) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg(arg_ptr);
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        if (arg == "--before") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.before = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--after") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.after = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--output" || arg == "-o") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.output = *value;
            skip_next = true;
            continue;
        }
    }
    return options;
}

[[nodiscard]] int run_capture(const CaptureOptions& options)
{
    sappp::build_capture::BuildCapture capture(options.repo_root);
    auto snapshot = capture.capture(options.compile_commands);
    if (!snapshot) {
        std::println(stderr, "Error: capture failed: {}", snapshot.error().message);
        return exit_code_for_error(snapshot.error());
    }

    std::filesystem::path output_file(options.output_path);
    auto output_parent = output_file.parent_path();
    if (!output_parent.empty()) {
        if (auto result = ensure_directory(output_parent, "output"); !result) {
            std::println(stderr, "Error: {}", result.error().message);
            return exit_code_for_error(result.error());
        }
    }

    if (auto result = write_canonical_json_file(output_file, snapshot->json()); !result) {
        std::println(stderr,
                     "Error: failed to serialize build snapshot: {}",
                     result.error().message);
        return exit_code_for_error(result.error());
    }

    std::println("[capture] Wrote build_snapshot.json");
    std::println("  input: {}", options.compile_commands);
    std::println("  output: {}", output_file.string());
    return static_cast<int>(ExitCode::kOk);
}

[[nodiscard]] int run_analyze(const AnalyzeOptions& options)
{
#if !defined(SAPPP_HAS_CLANG_FRONTEND)
    (void)options;
    std::println(
        stderr,
        "Error: frontend_clang is not built. Reconfigure with -DSAPPP_BUILD_CLANG_FRONTEND=ON");
    return static_cast<int>(ExitCode::kInternalError);
#else
    auto snapshot_json = read_json_file(options.build);
    if (!snapshot_json) {
        std::println(stderr, "Error: {}", snapshot_json.error().message);
        return exit_code_for_error(snapshot_json.error());
    }

    sappp::frontend_clang::FrontendClang frontend(options.schema_dir);
    auto result = frontend.analyze(*snapshot_json, options.versions);
    if (!result) {
        std::println(stderr, "Error: analyze failed: {}", result.error().message);
        return exit_code_for_error(result.error());
    }

    auto paths = prepare_analyze_paths(options.output);
    if (!paths) {
        std::println(stderr, "Error: {}", paths.error().message);
        return exit_code_for_error(paths.error());
    }

    if (auto write = write_canonical_json_file(paths->nir_path, result->nir); !write) {
        std::println(stderr, "Error: failed to serialize NIR: {}", write.error().message);
        return exit_code_for_error(write.error());
    }
    if (auto write = write_canonical_json_file(paths->source_map_path, result->source_map);
        !write) {
        std::println(stderr, "Error: failed to serialize source map: {}", write.error().message);
        return exit_code_for_error(write.error());
    }

    sappp::po::PoGenerator po_generator;
    auto po_list_result = po_generator.generate(result->nir);
    if (!po_list_result) {
        std::println(stderr, "Error: PO generation failed: {}", po_list_result.error().message);
        return exit_code_for_error(po_list_result.error());
    }

    const std::filesystem::path po_schema_path =
        std::filesystem::path(options.schema_dir) / "po.v1.schema.json";
    if (auto validation = sappp::common::validate_json(*po_list_result, po_schema_path.string());
        !validation) {
        std::println(stderr, "Error: po schema validation failed: {}", validation.error().message);
        return exit_code_for_error(validation.error());
    }

    if (auto write = write_canonical_json_file(paths->po_path, *po_list_result); !write) {
        std::println(stderr, "Error: failed to serialize PO list: {}", write.error().message);
        return exit_code_for_error(write.error());
    }

    auto analysis_config = load_analysis_config(options);
    if (!analysis_config) {
        std::println(stderr,
                     "Error: analysis_config validation failed: {}",
                     analysis_config.error().message);
        return exit_code_for_error(analysis_config.error());
    }

    if (auto write = write_canonical_json_file(paths->analysis_config_path, *analysis_config);
        !write) {
        std::println(stderr,
                     "Error: failed to serialize analysis config: {}",
                     write.error().message);
        return exit_code_for_error(write.error());
    }

    auto unknown_ledger = build_unknown_ledger_json(result->nir, options);
    if (!unknown_ledger) {
        std::println(stderr,
                     "Error: unknown ledger validation failed: {}",
                     unknown_ledger.error().message);
        return exit_code_for_error(unknown_ledger.error());
    }
    if (auto write = write_canonical_json_file(paths->unknown_ledger_path, *unknown_ledger);
        !write) {
        std::println(stderr,
                     "Error: failed to serialize unknown ledger: {}",
                     write.error().message);
        return exit_code_for_error(write.error());
    }

    std::println("[analyze] Wrote frontend outputs");
    std::println("  build: {}", options.build);
    std::println("  output: {}", paths->output_dir.string());
    std::println("  nir: {}", paths->nir_path.string());
    std::println("  source_map: {}", paths->source_map_path.string());
    std::println("  po: {}", paths->po_path.string());
    std::println("  unknown_ledger: {}", paths->unknown_ledger_path.string());
    std::println("  analysis_config: {}", paths->analysis_config_path.string());
    return static_cast<int>(ExitCode::kOk);
#endif
}

[[nodiscard]] int run_validate(const ValidateOptions& options)
{
    std::filesystem::path output_path(options.output);
    if (output_path.empty()) {
        output_path = std::filesystem::path(options.input) / "results" / "validated_results.json";
    }

    auto output_parent = output_path.parent_path();
    if (!output_parent.empty()) {
        if (auto result = ensure_directory(output_parent, "results"); !result) {
            std::println(stderr, "Error: {}", result.error().message);
            return exit_code_for_error(result.error());
        }
    }

    sappp::validator::Validator validator(options.input, options.schema_dir, options.versions);
    auto results = validator.validate(options.strict);
    if (!results) {
        std::println(stderr, "Error: validate failed: {}", results.error().message);
        return exit_code_for_error(results.error());
    }
    if (auto write = validator.write_results(*results, output_path.string()); !write) {
        std::println(stderr, "Error: failed to write validated results: {}", write.error().message);
        return exit_code_for_error(write.error());
    }

    std::println("[validate] Wrote validated_results.json");
    std::println("  input: {}", options.input);
    std::println("  output: {}", output_path.string());
    std::println("  strict: {}", options.strict ? "yes" : "no");
    return static_cast<int>(ExitCode::kOk);
}

[[nodiscard]] int run_pack(const PackOptions& options)
{
    std::println("[pack] Not yet implemented");
    std::println("  input: {}", options.input);
    std::println("  output: {}", options.output);
    return 0;
}

[[nodiscard]] int run_diff(const DiffOptions& options)
{
    std::println("[diff] Not yet implemented");
    std::println("  before: {}", options.before);
    std::println("  after: {}", options.after);
    std::println("  output: {}", options.output);
    return 0;
}
int cmd_capture(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_capture_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_capture_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->compile_commands.empty()) {
        std::println(stderr, "Error: --compile-commands is required");
        print_capture_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    return run_capture(*options);
}

int cmd_analyze(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_analyze_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_analyze_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->build.empty()) {
        std::println(stderr, "Error: --build is required");
        print_analyze_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    if (options->output.empty()) {
        std::println(stderr, "Error: --out is required");
        print_analyze_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    return run_analyze(*options);
}

int cmd_validate(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_validate_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_validate_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_validate_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    return run_validate(*options);
}

int cmd_pack(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_pack_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_pack_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_pack_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    return run_pack(*options);
}

int cmd_diff(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_diff_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_diff_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->before.empty() || options->after.empty()) {
        std::println(stderr, "Error: --before and --after are required");
        print_diff_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    return run_diff(*options);
}

int cmd_explain(int argc, char** argv)
{
    std::println("[explain] Not yet implemented");
    (void)argc;
    (void)argv;
    return 0;
}

}  // namespace

namespace {

[[nodiscard]] int run_cli(int argc, char** argv)
{
    try {
        if (argc < 2) {
            print_help();
            return static_cast<int>(ExitCode::kCliError);
        }

        std::string_view cmd = argv[1];

        if (cmd == "--help" || cmd == "-h") {
            print_help();
            return 0;
        }
        if (cmd == "--version" || cmd == "version") {
            print_version();
            return 0;
        }

        int sub_argc = argc - 2;
        char** sub_argv = argv + 2;

        if (cmd == "capture") {
            return cmd_capture(sub_argc, sub_argv);
        }
        if (cmd == "analyze") {
            return cmd_analyze(sub_argc, sub_argv);
        }
        if (cmd == "validate") {
            return cmd_validate(sub_argc, sub_argv);
        }
        if (cmd == "pack") {
            return cmd_pack(sub_argc, sub_argv);
        }
        if (cmd == "diff") {
            return cmd_diff(sub_argc, sub_argv);
        }
        if (cmd == "explain") {
            return cmd_explain(sub_argc, sub_argv);
        }

        std::println(stderr, "Unknown command: {}", cmd);
        print_help();
        return static_cast<int>(ExitCode::kCliError);
    } catch (const std::exception& ex) {
        try {
            std::println(stderr, "Error: {}", ex.what());
        } catch (...) {
            std::terminate();
        }
        return static_cast<int>(ExitCode::kInternalError);
    } catch (...) {
        try {
            std::println(stderr, "Error: unknown exception");
        } catch (...) {
            std::terminate();
        }
        return static_cast<int>(ExitCode::kInternalError);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    return run_cli(argc, argv);
}
