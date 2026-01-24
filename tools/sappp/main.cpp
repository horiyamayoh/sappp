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

#include "analyzer.hpp"
#include "po_generator.hpp"
#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/report.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/specdb.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
    #include "frontend_clang/frontend.hpp"
#endif
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace {

[[nodiscard]] [[maybe_unused]] std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

constexpr std::string_view kDeterministicGeneratedAt = "1970-01-01T00:00:00Z";

[[nodiscard]] std::string generated_at_from_json(const nlohmann::json& json)
{
    if (json.contains("generated_at") && json.at("generated_at").is_string()) {
        return json.at("generated_at").get<std::string>();
    }
    return std::string(kDeterministicGeneratedAt);
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
  --schema-dir DIR          Path to schema directory (default: ./schemas)
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
  --spec PATH               Path to Spec DB snapshot or directory
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
  <output>/specdb/snapshot.json
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
  --input DIR, --in DIR     Input directory containing analysis outputs (required)
  --out FILE, --output FILE, -o  Output file (default: pack.tar.gz)
  --manifest FILE           Manifest output (default: manifest.json)
  --repro-level LEVEL       Repro asset level (L0/L1/L2/L3)
  --include-analyzer-candidates  Include analyzer cert candidates
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --help, -h                Show this help

Output:
  <output>.tar.gz
  manifest.json
)");
}

void print_diff_help()
{
    std::print(R"(Usage: sappp diff [options]

Compare before/after analysis results

Options:
  --before FILE             Path to before pack.tar.gz or directory (required)
  --after FILE              Path to after pack.tar.gz or directory (required)
  --out FILE, --output FILE, -o  Output file (default: diff.json)
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --help, -h                Show this help

Output:
  diff.json
)");
}

void print_explain_help()
{
    std::print(R"(Usage: sappp explain [options]

Explain UNKNOWN entries

Options:
  --unknown FILE           Path to unknown_ledger.json (required)
  --validated FILE         Path to validated_results.json (optional)
  --po PO_ID               Filter by PO ID
  --unknown-id UNKNOWN_ID  Filter by unknown stable ID
  --format FORMAT          Output format: text|json (default: text)
  --out FILE               Output file for JSON (format=json only)
  --schema-dir DIR         Path to schema directory (default: ./schemas)
  --help, -h               Show this help
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
    std::string schema_dir;
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
    std::string manifest;
    std::string schema_dir;
    std::string repro_level;
    bool include_analyzer_candidates;
    bool show_help;
};

struct DiffOptions
{
    std::string before;
    std::string after;
    std::string output;
    std::string schema_dir;
    bool show_help;
};

struct ExplainOptions
{
    std::string unknown;
    std::string validated;
    std::string po_id;
    std::string unknown_id;
    std::string format;
    std::string output;
    std::string schema_dir;
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
    std::filesystem::path specdb_dir;
    std::filesystem::path nir_path;
    std::filesystem::path source_map_path;
    std::filesystem::path po_path;
    std::filesystem::path unknown_ledger_path;
    std::filesystem::path analysis_config_path;
    std::filesystem::path specdb_snapshot_path;
};

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI signature matches call sites.
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
// NOLINTEND(bugprone-easily-swappable-parameters)

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

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_logging_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     LoggingOptions& logging,
                                                     bool& skip_next)
{
    if (arg == "-v" || arg == "--verbose") {
        logging.verbose = true;
        logging.quiet = false;
        return sappp::Result<bool>{true};
    }
    if (arg == "-q" || arg == "--quiet") {
        logging.quiet = true;
        logging.verbose = false;
        return sappp::Result<bool>{true};
    }
    if (arg == "--json-logs") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        logging.json_logs = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    if (arg == "--proof") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        versions.proof_system = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--profile") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        versions.profile = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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

[[nodiscard]] sappp::Result<std::string> read_file_binary(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to read file: " + path.string()));
    }
    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (!in.good() && !in.eof()) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to read file: " + path.string()));
    }
    return content;
}

[[nodiscard]] sappp::Result<std::string> sha256_for_file(const std::filesystem::path& path)
{
    auto content = read_file_binary(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return sappp::common::sha256_prefixed(*content);
}

[[nodiscard]] sappp::Result<std::string>
input_digest_from_build_snapshot(const nlohmann::json& snapshot)
{
    if (snapshot.contains("input_digest")) {
        return snapshot.at("input_digest").get<std::string>();
    }
    return sappp::canonical::hash_canonical(snapshot);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - Signature groups path + schema.
[[nodiscard]] sappp::Result<nlohmann::json>
read_and_validate_json(const std::filesystem::path& path,
                       const std::filesystem::path& schema_dir,
                       std::string_view schema_name)
{
    auto json = read_json_file(path);
    if (!json) {
        return std::unexpected(json.error());
    }
    auto schema_path = (schema_dir / schema_name).string();
    if (auto validation = sappp::common::validate_json(*json, schema_path); !validation) {
        return std::unexpected(
            sappp::Error::make("SchemaInvalid",
                               std::string(schema_name) + ": " + validation.error().message));
    }
    return *json;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

[[nodiscard]] sappp::VoidResult copy_file_checked(const std::filesystem::path& source,
                                                  const std::filesystem::path& destination)
{
    std::error_code ec;
    std::filesystem::copy_file(source,
                               destination,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
        return std::unexpected(sappp::Error::make("IOError",
                                                  "Failed to copy file: " + source.string() + " -> "
                                                      + destination.string() + ": "
                                                      + ec.message()));
    }
    return {};
}

[[nodiscard]] sappp::Result<std::filesystem::path>
prepare_pack_root(const std::filesystem::path& base_dir)
{
    std::filesystem::path pack_root = base_dir / "pack";
    if (auto result = ensure_directory(pack_root, "pack"); !result) {
        return std::unexpected(result.error());
    }
    return pack_root;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - Label/seed pairing is explicit.
[[nodiscard]] sappp::Result<std::filesystem::path> prepare_temp_dir(std::string_view label,
                                                                    std::string_view seed)
{
    const auto hash = sappp::common::sha256_prefixed(seed);
    const auto suffix_length =
        hash.size() < static_cast<std::size_t>(12) ? hash.size() : static_cast<std::size_t>(12);
    const auto suffix = hash.substr(hash.size() - suffix_length);
    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / std::format("sappp_{}_{}", label, suffix);
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    if (ec) {
        return std::unexpected(sappp::Error::make("IOError",
                                                  "Failed to remove existing temp dir '"
                                                      + temp_dir.string() + "': " + ec.message()));
    }
    std::filesystem::create_directories(temp_dir, ec);
    if (ec) {
        return std::unexpected(sappp::Error::make("IOError",
                                                  "Failed to create temp dir '" + temp_dir.string()
                                                      + "': " + ec.message()));
    }
    return temp_dir;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

[[nodiscard]] sappp::Result<nlohmann::json>
build_pack_manifest(const std::vector<nlohmann::json>& files,
                    const nlohmann::json& build_snapshot,
                    std::string_view repro_level,
                    std::string_view generated_at)
{
    auto digest = input_digest_from_build_snapshot(build_snapshot);
    if (!digest) {
        return std::unexpected(digest.error());
    }

    nlohmann::json manifest = {
        {      "schema_version",         "pack_manifest.v1"},
        {                "tool",       tool_metadata_json()},
        {        "generated_at",  std::string(generated_at)},
        {   "semantics_version",   sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {     "profile_version",     sappp::kProfileVersion},
        {        "input_digest",                    *digest},
        {         "repro_level",   std::string(repro_level)},
        {               "files",      nlohmann::json(files)}
    };

    return manifest;
}

[[nodiscard]] sappp::Result<nlohmann::json> build_diff_side(const nlohmann::json& manifest,
                                                            const nlohmann::json& results,
                                                            std::string_view results_digest)
{
    auto input_digest = manifest.value("input_digest", results.value("input_digest", ""));
    if (input_digest.empty()) {
        return std::unexpected(
            sappp::Error::make("MissingField", "input_digest is missing in manifest/results"));
    }
    std::string semantics =
        manifest.value("semantics_version", results.value("semantics_version", ""));
    std::string proof =
        manifest.value("proof_system_version", results.value("proof_system_version", ""));
    std::string profile = manifest.value("profile_version", results.value("profile_version", ""));
    if (semantics.empty() || proof.empty() || profile.empty()) {
        return std::unexpected(
            sappp::Error::make("MissingField", "version info missing for diff side"));
    }
    return nlohmann::json{
        {        "input_digest",     std::move(input_digest)},
        {   "semantics_version",        std::move(semantics)},
        {"proof_system_version",            std::move(proof)},
        {     "profile_version",          std::move(profile)},
        {      "results_digest", std::string(results_digest)}
    };
}

[[nodiscard]] std::string diff_reason_for(const nlohmann::json& before, const nlohmann::json& after)
{
    if (before.value("semantics_version", "") != after.value("semantics_version", "")) {
        return "SemanticsUpdated";
    }
    if (before.value("proof_system_version", "") != after.value("proof_system_version", "")) {
        return "ProofRuleUpdated";
    }
    if (before.value("profile_version", "") != after.value("profile_version", "")) {
        return "ProfileUpdated";
    }
    if (before.value("input_digest", "") != after.value("input_digest", "")) {
        return "InputDigestChanged";
    }
    return {};
}

[[nodiscard]] sappp::Result<std::filesystem::path>
extract_pack_if_needed(const std::filesystem::path& input_path)
{
    const auto filename = input_path.filename().string();
    if (!filename.ends_with(".tar.gz")) {
        return input_path;
    }
    auto temp_dir = prepare_temp_dir("pack", input_path.string());
    if (!temp_dir) {
        return std::unexpected(temp_dir.error());
    }

    const auto command =
        std::format(R"(tar -xzf "{}" -C "{}")", input_path.string(), temp_dir->string());
    if (std::system(command.c_str()) != 0) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to extract pack: " + input_path.string()));
    }
    return *temp_dir / "pack";
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
    auto specdb_dir = output_dir / "specdb";
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
    if (auto result = ensure_directory(specdb_dir, "specdb"); !result) {
        return std::unexpected(result.error());
    }
    auto nir_path = frontend_dir / "nir.json";
    auto source_map_path = frontend_dir / "source_map.json";
    auto po_path = po_dir / "po_list.json";
    auto unknown_ledger_path = analyzer_dir / "unknown_ledger.json";
    auto analysis_config_path = config_dir / "analysis_config.json";
    auto specdb_snapshot_path = specdb_dir / "snapshot.json";
    return AnalyzePaths{output_dir,
                        frontend_dir,
                        po_dir,
                        analyzer_dir,
                        certstore_dir,
                        certstore_objects_dir,
                        certstore_index_dir,
                        config_dir,
                        specdb_dir,
                        nir_path,
                        source_map_path,
                        po_path,
                        unknown_ledger_path,
                        analysis_config_path,
                        specdb_snapshot_path};
}

[[nodiscard]] sappp::Result<nlohmann::json> load_analysis_config(const AnalyzeOptions& options,
                                                                 std::string_view generated_at)
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

    std::string resolved_generated_at =
        generated_at.empty() ? std::string(kDeterministicGeneratedAt) : std::string(generated_at);
    nlohmann::json analysis_settings = {
        {"budget", nlohmann::json::object()}
    };

    nlohmann::json config_json = {
        {      "schema_version",          "analysis_config.v1"},
        {                "tool",          tool_metadata_json()},
        {        "generated_at",         resolved_generated_at},
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
load_specdb_snapshot(const AnalyzeOptions& options,
                     std::string_view generated_at,
                     const nlohmann::json& build_snapshot)
{
    std::string resolved_generated_at =
        generated_at.empty() ? std::string(kDeterministicGeneratedAt) : std::string(generated_at);
    sappp::specdb::BuildOptions specdb_options{.build_snapshot = build_snapshot,
                                               .spec_path = options.spec,
                                               .schema_dir = options.schema_dir,
                                               .generated_at = resolved_generated_at,
                                               .tool = tool_metadata_json()};
    return sappp::specdb::build_snapshot(specdb_options);
}

[[nodiscard]] sappp::analyzer::ContractMatchContext
build_contract_match_context(const nlohmann::json& build_snapshot)
{
    sappp::analyzer::ContractMatchContext context;
    if (!build_snapshot.contains("compile_units")
        || !build_snapshot.at("compile_units").is_array()) {
        return context;
    }
    std::unordered_set<std::string> abis;
    for (const auto& unit : build_snapshot.at("compile_units")) {
        if (!unit.is_object() || !unit.contains("target") || !unit.at("target").is_object()) {
            continue;
        }
        const auto& target = unit.at("target");
        if (target.contains("abi") && target.at("abi").is_string()) {
            abis.insert(target.at("abi").get<std::string>());
        }
    }
    if (abis.size() == 1U) {
        context.abi = *abis.begin();
    }
    return context;
}

[[nodiscard]] sappp::VoidResult write_analysis_config_output(const AnalyzePaths& paths,
                                                             const AnalyzeOptions& options,
                                                             std::string_view generated_at)
{
    auto analysis_config = load_analysis_config(options, generated_at);
    if (!analysis_config) {
        return std::unexpected(analysis_config.error());
    }
    if (auto write = write_canonical_json_file(paths.analysis_config_path, *analysis_config);
        !write) {
        return std::unexpected(write.error());
    }
    return {};
}

[[nodiscard]] sappp::VoidResult write_specdb_snapshot_output(const AnalyzePaths& paths,
                                                             const AnalyzeOptions& options,
                                                             std::string_view generated_at,
                                                             const nlohmann::json& build_snapshot)
{
    auto specdb_snapshot = load_specdb_snapshot(options, generated_at, build_snapshot);
    if (!specdb_snapshot) {
        return std::unexpected(specdb_snapshot.error());
    }
    if (auto write = write_canonical_json_file(paths.specdb_snapshot_path, *specdb_snapshot);
        !write) {
        return std::unexpected(write.error());
    }
    return {};
}

#endif

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--spec") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.spec = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_analyze_job_option(std::string_view arg,
                                                         std::span<char*> args,
                                                         std::size_t idx,
                                                         AnalyzeOptions& options,
                                                         bool& skip_next)
{
    if (arg != "--jobs" && arg != "-j") {
        return sappp::Result<bool>{false};
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
    return sappp::Result<bool>{true};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    if (arg == "--analysis-config") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.analysis_config = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--emit-sarif") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.emit_sarif = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--repro-level") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.repro_level = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    handled = set_analyze_job_option(arg, args, idx, options, skip_next);
    if (!handled) {
        return std::unexpected(handled.error());
    }
    if (*handled) {
        return sappp::Result<bool>{true};
    }
    return set_analyze_extra_option(arg, args, idx, options, skip_next);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--strict") {
        options.strict = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
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
        return sappp::Result<bool>{true};
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output_path = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--repo-root") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.repo_root = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
// NOLINTNEXTLINE(readability-function-size) - CLI parsing is kept in one place for clarity.
[[nodiscard]] sappp::Result<bool> set_pack_option(std::string_view arg,
                                                  std::span<char*> args,
                                                  std::size_t idx,
                                                  PackOptions& options,
                                                  bool& skip_next)
{
    if (arg == "--input" || arg == "--in") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.input = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--out" || arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--manifest") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.manifest = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--repro-level") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.repro_level = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--include-analyzer-candidates") {
        options.include_analyzer_candidates = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_explain_input_option(std::string_view arg,
                                                           std::span<char*> args,
                                                           std::size_t idx,
                                                           ExplainOptions& options,
                                                           bool& skip_next)
{
    if (arg == "--unknown") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.unknown = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--validated") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.validated = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--schema-dir") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.schema_dir = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_explain_filter_option(std::string_view arg,
                                                            std::span<char*> args,
                                                            std::size_t idx,
                                                            ExplainOptions& options,
                                                            bool& skip_next)
{
    if (arg == "--po") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.po_id = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--unknown-id") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.unknown_id = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_explain_output_option(std::string_view arg,
                                                            std::span<char*> args,
                                                            std::size_t idx,
                                                            ExplainOptions& options,
                                                            bool& skip_next)
{
    if (arg == "--format") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.format = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--out") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    return sappp::Result<bool>{false};
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - CLI parsing signature is stable.
[[nodiscard]] sappp::Result<bool> set_explain_option(std::string_view arg,
                                                     std::span<char*> args,
                                                     std::size_t idx,
                                                     ExplainOptions& options,
                                                     bool& skip_next)
{
    auto handled = set_explain_input_option(arg, args, idx, options, skip_next);
    if (!handled) {
        return std::unexpected(handled.error());
    }
    if (*handled) {
        return sappp::Result<bool>{true};
    }
    handled = set_explain_filter_option(arg, args, idx, options, skip_next);
    if (!handled) {
        return std::unexpected(handled.error());
    }
    if (*handled) {
        return sappp::Result<bool>{true};
    }
    return set_explain_output_option(arg, args, idx, options, skip_next);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

[[nodiscard]] sappp::Result<CaptureOptions> parse_capture_args(std::span<char*> args)
{
    CaptureOptions options{.compile_commands = std::string{},
                           .repo_root = std::string{},
                           .output_path = "build_snapshot.json",
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
    PackOptions options{.input = std::string{},
                        .output = "pack.tar.gz",
                        .manifest = "manifest.json",
                        .schema_dir = "schemas",
                        .repro_level = "L0",
                        .include_analyzer_candidates = false,
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
        auto handled = set_pack_option(arg, args, idx, options, skip_next);
        if (!handled) {
            return std::unexpected(handled.error());
        }
        if (*handled) {
            continue;
        }
    }
    return options;
}

// NOLINTNEXTLINE(readability-function-size) - Parsing is intentionally explicit for each flag.
[[nodiscard]] sappp::Result<DiffOptions> parse_diff_args(std::span<char*> args)
{
    DiffOptions options{.before = std::string{},
                        .after = std::string{},
                        .output = "diff.json",
                        .schema_dir = "schemas",
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
        if (arg == "--out" || arg == "--output" || arg == "-o") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.output = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--schema-dir") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.schema_dir = *value;
            skip_next = true;
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<ExplainOptions> parse_explain_args(std::span<char*> args)
{
    ExplainOptions options{.unknown = std::string{},
                           .validated = std::string{},
                           .po_id = std::string{},
                           .unknown_id = std::string{},
                           .format = "text",
                           .output = std::string{},
                           .schema_dir = "schemas",
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
        auto handled = set_explain_option(arg, args, idx, options, skip_next);
        if (!handled) {
            return std::unexpected(handled.error());
        }
        if (*handled) {
            continue;
        }
    }
    return options;
}

[[nodiscard]] int run_capture(const CaptureOptions& options)
{
    sappp::build_capture::BuildCapture capture(options.repo_root, options.schema_dir);
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

// NOLINTNEXTLINE(readability-function-size) - CLI orchestration keeps the flow together.
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

    const std::string generated_at = generated_at_from_json(*snapshot_json);
    if (auto write_result = write_analysis_config_output(*paths, options, generated_at);
        !write_result) {
        std::println(stderr, "Error: analysis_config failed: {}", write_result.error().message);
        return exit_code_for_error(write_result.error());
    }
    if (auto write_result =
            write_specdb_snapshot_output(*paths, options, generated_at, *snapshot_json);
        !write_result) {
        std::println(stderr, "Error: specdb snapshot failed: {}", write_result.error().message);
        return exit_code_for_error(write_result.error());
    }
    auto specdb_snapshot_json = read_json_file(paths->specdb_snapshot_path);
    if (!specdb_snapshot_json) {
        std::println(stderr,
                     "Error: specdb snapshot read failed: {}",
                     specdb_snapshot_json.error().message);
        return exit_code_for_error(specdb_snapshot_json.error());
    }
    sappp::analyzer::Analyzer analyzer({.schema_dir = options.schema_dir,
                                        .certstore_dir = paths->certstore_dir.string(),
                                        .versions = options.versions});
    auto match_context = build_contract_match_context(*snapshot_json);
    auto analyzer_output =
        analyzer.analyze(result->nir, *po_list_result, &*specdb_snapshot_json, match_context);
    if (!analyzer_output) {
        std::println(stderr, "Error: analyzer failed: {}", analyzer_output.error().message);
        return exit_code_for_error(analyzer_output.error());
    }
    if (auto write =
            write_canonical_json_file(paths->unknown_ledger_path, analyzer_output->unknown_ledger);
        !write) {
        std::println(stderr, "Error: unknown ledger failed: {}", write.error().message);
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
    std::println("  specdb_snapshot: {}", paths->specdb_snapshot_path.string());
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

// NOLINTNEXTLINE(readability-function-size) - CLI routine keeps pack flow together.
[[nodiscard]] int run_pack(const PackOptions& options)
{
    const std::filesystem::path input_dir(options.input);
    const std::filesystem::path schema_dir(options.schema_dir);

    auto temp_dir = prepare_temp_dir("pack", options.input);
    if (!temp_dir) {
        std::println(stderr, "Error: {}", temp_dir.error().message);
        return exit_code_for_error(temp_dir.error());
    }
    auto pack_root = prepare_pack_root(*temp_dir);
    if (!pack_root) {
        std::println(stderr, "Error: {}", pack_root.error().message);
        return exit_code_for_error(pack_root.error());
    }

    struct PackItem
    {
        std::filesystem::path source;
        std::filesystem::path dest;
        std::string schema;
    };

    std::vector<PackItem> required_files = {
        {               .source = input_dir / "build_snapshot.json",
         .dest = *pack_root / "inputs" / "build_snapshot.json",
         .schema = "build_snapshot.v1.schema.json"   },
        {             .source = input_dir / "frontend" / "nir.json",
         .dest = *pack_root / "frontend" / "nir.json",
         .schema = "nir.v1.schema.json"              },
        {      .source = input_dir / "frontend" / "source_map.json",
         .dest = *pack_root / "frontend" / "source_map.json",
         .schema = "source_map.v1.schema.json"       },
        {               .source = input_dir / "po" / "po_list.json",
         .dest = *pack_root / "po" / "po_list.json",
         .schema = "po.v1.schema.json"               },
        {  .source = input_dir / "analyzer" / "unknown_ledger.json",
         .dest = *pack_root / "analyzer" / "unknown_ledger.json",
         .schema = "unknown.v1.schema.json"          },
        {          .source = input_dir / "specdb" / "snapshot.json",
         .dest = *pack_root / "specdb" / "snapshot.json",
         .schema = "specdb_snapshot.v1.schema.json"  },
        {.source = input_dir / "results" / "validated_results.json",
         .dest = *pack_root / "results" / "validated_results.json",
         .schema = "validated_results.v1.schema.json"},
        {   .source = input_dir / "config" / "analysis_config.json",
         .dest = *pack_root / "config" / "analysis_config.json",
         .schema = "analysis_config.v1.schema.json"  },
    };

    std::vector<nlohmann::json> file_entries;

    for (const auto& item : required_files) {
        if (auto result = ensure_directory(item.dest.parent_path(), "pack item"); !result) {
            std::println(stderr, "Error: {}", result.error().message);
            return exit_code_for_error(result.error());
        }
        auto json = read_and_validate_json(item.source, schema_dir, item.schema);
        if (!json) {
            std::println(stderr, "Error: {}", json.error().message);
            return exit_code_for_error(json.error());
        }
        if (auto copied = copy_file_checked(item.source, item.dest); !copied) {
            std::println(stderr, "Error: {}", copied.error().message);
            return exit_code_for_error(copied.error());
        }
        auto digest = sha256_for_file(item.dest);
        if (!digest) {
            std::println(stderr, "Error: {}", digest.error().message);
            return exit_code_for_error(digest.error());
        }
        auto size = std::filesystem::file_size(item.dest);
        file_entries.push_back(nlohmann::json{
            {"path", std::filesystem::relative(item.dest, *pack_root).generic_string()},
            {"sha256", *digest},
            {"size_bytes", static_cast<std::int64_t>(size)}
        });
    }

    std::filesystem::path certstore_src = input_dir / "certstore";
    std::filesystem::path certstore_dst = *pack_root / "certstore";
    if (std::filesystem::exists(certstore_src)) {
        std::vector<std::filesystem::path> cert_files;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(certstore_src)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                cert_files.push_back(entry.path());
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::println(stderr,
                         "Error iterating certstore directory '{}': {}",
                         certstore_src.generic_string(),
                         e.what());
            return EXIT_FAILURE;
        }
        std::ranges::stable_sort(cert_files, [](const auto& lhs, const auto& rhs) {
            return lhs.generic_string() < rhs.generic_string();
        });
        for (const auto& path : cert_files) {
            auto relative = std::filesystem::relative(path, certstore_src);
            auto dest = certstore_dst / relative;
            if (auto result = ensure_directory(dest.parent_path(), "certstore"); !result) {
                std::println(stderr, "Error: {}", result.error().message);
                return exit_code_for_error(result.error());
            }
            if (auto copied = copy_file_checked(path, dest); !copied) {
                std::println(stderr, "Error: {}", copied.error().message);
                return exit_code_for_error(copied.error());
            }
            auto digest = sha256_for_file(dest);
            if (!digest) {
                std::println(stderr, "Error: {}", digest.error().message);
                return exit_code_for_error(digest.error());
            }
            auto size = std::filesystem::file_size(dest);
            file_entries.push_back(nlohmann::json{
                {"path", std::filesystem::relative(dest, *pack_root).generic_string()},
                {"sha256", *digest},
                {"size_bytes", static_cast<std::int64_t>(size)}
            });
        }
    }

    if (options.include_analyzer_candidates) {
        std::filesystem::path candidates_src = input_dir / "analyzer" / "cert_candidates";
        std::filesystem::path candidates_dst = *pack_root / "analyzer" / "cert_candidates";
        if (std::filesystem::exists(candidates_src)) {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(candidates_src)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                auto relative = std::filesystem::relative(entry.path(), candidates_src);
                auto dest = candidates_dst / relative;
                if (auto result = ensure_directory(dest.parent_path(), "cert_candidates");
                    !result) {
                    std::println(stderr, "Error: {}", result.error().message);
                    return exit_code_for_error(result.error());
                }
                if (auto copied = copy_file_checked(entry.path(), dest); !copied) {
                    std::println(stderr, "Error: {}", copied.error().message);
                    return exit_code_for_error(copied.error());
                }
                auto digest = sha256_for_file(dest);
                if (!digest) {
                    std::println(stderr, "Error: {}", digest.error().message);
                    return exit_code_for_error(digest.error());
                }
                auto size = std::filesystem::file_size(dest);
                file_entries.push_back(nlohmann::json{
                    {"path", std::filesystem::relative(dest, *pack_root).generic_string()},
                    {"sha256", *digest},
                    {"size_bytes", static_cast<std::int64_t>(size)}
                });
            }
        }
    }

    std::filesystem::path semantics_path = *pack_root / "semantics" / "sem.v1.md";
    if (auto result = ensure_directory(semantics_path.parent_path(), "semantics"); !result) {
        std::println(stderr, "Error: {}", result.error().message);
        return exit_code_for_error(result.error());
    }
    {
        std::ofstream out(semantics_path);
        if (!out) {
            std::println(stderr, "Error: failed to write semantics stub");
            return static_cast<int>(ExitCode::kInputError);
        }
        out << "# sem.v1\n\nThis is a placeholder semantics document.\n";
        out.flush();
        if (!out) {
            std::println(stderr, "Error: failed to flush semantics stub to disk");
            return static_cast<int>(ExitCode::kInputError);
        }
    }
    auto semantics_digest = sha256_for_file(semantics_path);
    if (!semantics_digest) {
        std::println(stderr, "Error: {}", semantics_digest.error().message);
        return exit_code_for_error(semantics_digest.error());
    }
    file_entries.push_back(nlohmann::json{
        {"path", std::filesystem::relative(semantics_path, *pack_root).generic_string()},
        {"sha256", *semantics_digest},
        {"size_bytes", static_cast<std::int64_t>(std::filesystem::file_size(semantics_path))}
    });

    std::ranges::stable_sort(file_entries,
                             [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
                                 return lhs.at("path").get<std::string>()
                                        < rhs.at("path").get<std::string>();
                             });

    auto build_snapshot_json = read_and_validate_json(input_dir / "build_snapshot.json",
                                                      schema_dir,
                                                      "build_snapshot.v1.schema.json");
    if (!build_snapshot_json) {
        std::println(stderr, "Error: {}", build_snapshot_json.error().message);
        return exit_code_for_error(build_snapshot_json.error());
    }

    const std::string generated_at = generated_at_from_json(*build_snapshot_json);
    auto manifest =
        build_pack_manifest(file_entries, *build_snapshot_json, options.repro_level, generated_at);
    if (!manifest) {
        std::println(stderr, "Error: {}", manifest.error().message);
        return exit_code_for_error(manifest.error());
    }

    auto manifest_schema = (schema_dir / "pack_manifest.v1.schema.json").string();
    if (auto validation = sappp::common::validate_json(*manifest, manifest_schema); !validation) {
        std::println(stderr, "Error: manifest schema invalid: {}", validation.error().message);
        return exit_code_for_error(validation.error());
    }

    std::filesystem::path manifest_in_pack = *pack_root / "manifest.json";
    if (auto write = write_canonical_json_file(manifest_in_pack, *manifest); !write) {
        std::println(stderr, "Error: {}", write.error().message);
        return exit_code_for_error(write.error());
    }
    if (auto write = write_canonical_json_file(options.manifest, *manifest); !write) {
        std::println(stderr, "Error: {}", write.error().message);
        return exit_code_for_error(write.error());
    }

    const std::string command =
        std::format("GZIP=-n tar -czf \"{}\" --sort=name --mtime='UTC 1970-01-01' "
                    "--owner=0 --group=0 --numeric-owner -C \"{}\" pack",
                    options.output,
                    temp_dir->string());
    if (std::system(command.c_str()) != 0) {
        std::println(stderr, "Error: failed to create tar.gz");
        return static_cast<int>(ExitCode::kInputError);
    }

    std::println("[pack] Wrote pack");
    std::println("  input: {}", options.input);
    std::println("  output: {}", options.output);
    std::println("  manifest: {}", options.manifest);
    return static_cast<int>(ExitCode::kOk);
}

// NOLINTNEXTLINE(readability-function-size) - CLI routine keeps diff flow together.
[[nodiscard]] int run_diff(const DiffOptions& options)
{
    const std::filesystem::path schema_dir(options.schema_dir);
    auto before_root = extract_pack_if_needed(options.before);
    if (!before_root) {
        std::println(stderr, "Error: {}", before_root.error().message);
        return exit_code_for_error(before_root.error());
    }
    auto after_root = extract_pack_if_needed(options.after);
    if (!after_root) {
        std::println(stderr, "Error: {}", after_root.error().message);
        return exit_code_for_error(after_root.error());
    }

    auto before_results =
        read_and_validate_json(*before_root / "results" / "validated_results.json",
                               schema_dir,
                               "validated_results.v1.schema.json");
    if (!before_results) {
        std::println(stderr, "Error: {}", before_results.error().message);
        return exit_code_for_error(before_results.error());
    }
    auto after_results = read_and_validate_json(*after_root / "results" / "validated_results.json",
                                                schema_dir,
                                                "validated_results.v1.schema.json");
    if (!after_results) {
        std::println(stderr, "Error: {}", after_results.error().message);
        return exit_code_for_error(after_results.error());
    }

    nlohmann::json before_manifest;
    nlohmann::json after_manifest;
    if (auto manifest = read_json_file(*before_root / "manifest.json"); manifest) {
        before_manifest = *manifest;
    }
    if (auto manifest = read_json_file(*after_root / "manifest.json"); manifest) {
        after_manifest = *manifest;
    }
    if (!before_manifest.contains("input_digest")) {
        auto build_snapshot = read_json_file(*before_root / "inputs" / "build_snapshot.json");
        if (!build_snapshot) {
            build_snapshot = read_json_file(*before_root / "build_snapshot.json");
        }
        if (build_snapshot) {
            if (auto digest = input_digest_from_build_snapshot(*build_snapshot); digest) {
                before_manifest["input_digest"] = *digest;
            }
        }
    }
    if (!after_manifest.contains("input_digest")) {
        auto build_snapshot = read_json_file(*after_root / "inputs" / "build_snapshot.json");
        if (!build_snapshot) {
            build_snapshot = read_json_file(*after_root / "build_snapshot.json");
        }
        if (build_snapshot) {
            if (auto digest = input_digest_from_build_snapshot(*build_snapshot); digest) {
                after_manifest["input_digest"] = *digest;
            }
        }
    }

    auto before_digest = sappp::canonical::hash_canonical(*before_results);
    if (!before_digest) {
        std::println(stderr, "Error: {}", before_digest.error().message);
        return exit_code_for_error(before_digest.error());
    }
    auto after_digest = sappp::canonical::hash_canonical(*after_results);
    if (!after_digest) {
        std::println(stderr, "Error: {}", after_digest.error().message);
        return exit_code_for_error(after_digest.error());
    }

    auto before_side = build_diff_side(before_manifest, *before_results, *before_digest);
    if (!before_side) {
        std::println(stderr, "Error: {}", before_side.error().message);
        return exit_code_for_error(before_side.error());
    }
    auto after_side = build_diff_side(after_manifest, *after_results, *after_digest);
    if (!after_side) {
        std::println(stderr, "Error: {}", after_side.error().message);
        return exit_code_for_error(after_side.error());
    }

    const std::string reason = diff_reason_for(*before_side, *after_side);
    auto changes = sappp::report::build_diff_changes(*before_results, *after_results, reason);
    if (!changes) {
        std::println(stderr, "Error: {}", changes.error().message);
        return exit_code_for_error(changes.error());
    }

    std::string generated_at = generated_at_from_json(after_manifest);
    if (generated_at == kDeterministicGeneratedAt) {
        generated_at = generated_at_from_json(before_manifest);
    }
    if (generated_at == kDeterministicGeneratedAt) {
        generated_at = generated_at_from_json(*after_results);
    }
    if (generated_at == kDeterministicGeneratedAt) {
        generated_at = generated_at_from_json(*before_results);
    }

    nlohmann::json diff_json = {
        {"schema_version",            "diff.v1"},
        {          "tool", tool_metadata_json()},
        {  "generated_at",         generated_at},
        {        "before",         *before_side},
        {         "after",          *after_side},
        {       "changes",             *changes}
    };

    auto diff_schema = (schema_dir / "diff.v1.schema.json").string();
    if (auto validation = sappp::common::validate_json(diff_json, diff_schema); !validation) {
        std::println(stderr, "Error: diff schema invalid: {}", validation.error().message);
        return exit_code_for_error(validation.error());
    }

    if (auto write = write_canonical_json_file(options.output, diff_json); !write) {
        std::println(stderr, "Error: failed to write diff: {}", write.error().message);
        return exit_code_for_error(write.error());
    }

    std::println("[diff] Wrote diff.json");
    std::println("  before: {}", options.before);
    std::println("  after: {}", options.after);
    std::println("  output: {}", options.output);
    return static_cast<int>(ExitCode::kOk);
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
    if (options->repro_level != "L0" && options->repro_level != "L1" && options->repro_level != "L2"
        && options->repro_level != "L3") {
        std::println(stderr, "Error: --repro-level must be L0/L1/L2/L3");
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

// NOLINTNEXTLINE(readability-function-size) - CLI routine keeps explain flow together.
int cmd_explain(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_explain_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return exit_code_for_error(options.error());
    }
    if (options->show_help) {
        print_explain_help();
        return static_cast<int>(ExitCode::kOk);
    }
    if (options->unknown.empty()) {
        std::println(stderr, "Error: --unknown is required");
        print_explain_help();
        return static_cast<int>(ExitCode::kCliError);
    }
    if (options->format != "text" && options->format != "json") {
        std::println(stderr, "Error: --format must be text or json");
        return static_cast<int>(ExitCode::kCliError);
    }

    const std::filesystem::path schema_dir(options->schema_dir);
    auto unknown_ledger =
        read_and_validate_json(options->unknown, schema_dir, "unknown.v1.schema.json");
    if (!unknown_ledger) {
        std::println(stderr, "Error: {}", unknown_ledger.error().message);
        return exit_code_for_error(unknown_ledger.error());
    }

    std::optional<nlohmann::json> validated_results;
    if (!options->validated.empty()) {
        auto validated = read_and_validate_json(options->validated,
                                                schema_dir,
                                                "validated_results.v1.schema.json");
        if (!validated) {
            std::println(stderr, "Error: {}", validated.error().message);
            return exit_code_for_error(validated.error());
        }
        validated_results = *validated;
    }

    auto filtered = sappp::report::filter_unknowns(
        *unknown_ledger,
        validated_results,
        options->po_id.empty() ? std::nullopt : std::optional<std::string_view>(options->po_id),
        options->unknown_id.empty() ? std::nullopt
                                    : std::optional<std::string_view>(options->unknown_id));
    if (!filtered) {
        std::println(stderr, "Error: {}", filtered.error().message);
        return exit_code_for_error(filtered.error());
    }

    if (options->format == "json") {
        std::string generated_at = generated_at_from_json(*unknown_ledger);
        if (generated_at == kDeterministicGeneratedAt && validated_results) {
            generated_at = generated_at_from_json(*validated_results);
        }
        nlohmann::json explain_json = {
            {"schema_version",         "explain.v1"},
            {          "tool", tool_metadata_json()},
            {  "generated_at",         generated_at},
            {      "unknowns",            *filtered}
        };
        if (!options->output.empty()) {
            if (auto write = write_canonical_json_file(options->output, explain_json); !write) {
                std::println(stderr, "Error: {}", write.error().message);
                return exit_code_for_error(write.error());
            }
        } else {
            auto canonical = sappp::canonical::canonicalize(explain_json);
            if (!canonical) {
                std::println(stderr, "Error: {}", canonical.error().message);
                return exit_code_for_error(canonical.error());
            }
            std::println("{}", *canonical);
        }
        return static_cast<int>(ExitCode::kOk);
    }

    for (const auto& entry : *filtered) {
        std::println("UNKNOWN {}", entry.value("unknown_stable_id", ""));
        std::println("  po_id: {}", entry.value("po_id", ""));
        std::println("  code: {}", entry.value("unknown_code", ""));
        if (entry.contains("missing_lemma")) {
            const auto& lemma = entry.at("missing_lemma");
            std::println("  missing_lemma: {}", lemma.value("pretty", ""));
            if (lemma.contains("symbols")) {
                std::println("    symbols:");
                for (const auto& sym : lemma.at("symbols")) {
                    std::println("      - {}", sym.get<std::string>());
                }
            }
        }
        if (entry.contains("refinement_plan")) {
            const auto& plan = entry.at("refinement_plan");
            std::println("  refinement: {}", plan.value("message", ""));
            if (plan.contains("actions")) {
                std::println("    actions:");
                for (const auto& action : plan.at("actions")) {
                    std::println("      - {}", action.value("action", ""));
                }
            }
        }
    }

    return static_cast<int>(ExitCode::kOk);
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
