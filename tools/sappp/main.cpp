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
#include "sappp/report/explain.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
    #include "frontend_clang/frontend.hpp"
#endif
#include <charconv>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

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
  --help, -h          Show this help message
  --version, -v       Show version information
  --schema-dir DIR    Path to schema directory
  --jobs N, -j N      Number of parallel jobs (default: auto)
  --output DIR, -o    Output directory

Run 'sappp <command> --help' for command-specific options.
)");
}

void print_capture_help()
{
    std::print(R"(Usage: sappp capture [options]

Capture build conditions from compile_commands.json

Options:
  --compile-commands FILE   Path to compile_commands.json (required)
  --output DIR, -o          Output directory (default: ./out)
  --repo-root DIR           Repository root for relative paths
  --help, -h                Show this help

Output:
  <output>/build_snapshot.json
)");
}

void print_analyze_help()
{
    std::print(R"(Usage: sappp analyze [options]

Run static analysis on captured build

Options:
  --snapshot FILE           Path to build_snapshot.json (required)
  --output DIR, -o          Output directory (default: ./out)
  --jobs N, -j N            Number of parallel jobs
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --config FILE             Analysis configuration file
  --specdb DIR              Path to Spec DB directory
  --help, -h                Show this help

Output:
  <output>/frontend/nir.json
  <output>/frontend/source_map.json
  <output>/po/po_list.json
  <output>/analyzer/unknown_ledger.json
  <output>/certstore/
)");
}

void print_validate_help()
{
    std::print(R"(Usage: sappp validate [options]

Validate certificates and confirm SAFE/BUG results

Options:
  --input DIR, --in DIR     Input directory containing analysis outputs (required)
  --output FILE, -o         Output file (default: validated_results.json)
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

void print_explain_help()
{
    std::print(R"(Usage: sappp explain [options]

Explain UNKNOWN entries in human-readable form

Options:
  --unknown FILE           Path to unknown_ledger.json (required)
  --validated FILE         Path to validated_results.json (optional)
  --po PO_ID               Filter by PO ID
  --unknown-id ID          Filter by unknown_stable_id
  --format text|json       Output format (default: text)
  --out FILE               Output file (required for json)
  --schema-dir DIR         Path to schema directory (default: ./schemas)
  --help, -h               Show this help
)");
}

struct CaptureOptions
{
    std::string compile_commands;
    std::string repo_root;
    std::string output;
    bool show_help;
};

struct AnalyzeOptions
{
    std::string snapshot;
    int jobs;
    std::string output;
    std::string schema_dir;
    bool show_help;
};

struct ValidateOptions
{
    std::string input;
    bool strict;
    std::string output;
    std::string schema_dir;
    bool show_help;
};

struct PackOptions
{
    std::string input;
    std::string output;
    bool show_help;
};

struct ExplainOptions
{
    std::string unknown_path;
    std::optional<std::string> validated_path;
    std::optional<std::string> po_id;
    std::optional<std::string> unknown_id;
    std::optional<std::string> output;
    std::string schema_dir;
    sappp::report::ExplainFormat format;
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
    std::filesystem::path nir_path;
    std::filesystem::path source_map_path;
    std::filesystem::path po_path;
};

// CLI parsing signature is stable.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto read_option_value(std::span<char*> args,
                                     std::size_t index,
                                     std::string_view option) -> sappp::Result<std::string>
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
    if (auto result = ensure_directory(frontend_dir, "frontend"); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = ensure_directory(po_dir, "po"); !result) {
        return std::unexpected(result.error());
    }
    auto nir_path = frontend_dir / "nir.json";
    auto source_map_path = frontend_dir / "source_map.json";
    auto po_path = po_dir / "po_list.json";
    return AnalyzePaths{output_dir, frontend_dir, po_dir, nir_path, source_map_path, po_path};
}
#endif

[[nodiscard]] auto set_analyze_option(std::string_view arg,
                                      // CLI parsing signature is stable.
                                      // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                      std::span<char*> args,
                                      std::size_t idx,
                                      AnalyzeOptions& options,
                                      bool& skip_next) -> sappp::Result<bool>
{
    if (arg == "--snapshot") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.snapshot = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--output" || arg == "-o") {
        auto value = read_option_value(args, idx, arg);
        if (!value) {
            return std::unexpected(value.error());
        }
        options.output = *value;
        skip_next = true;
        return sappp::Result<bool>{true};
    }
    if (arg == "--jobs" || arg == "-j") {
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

[[nodiscard]] auto set_validate_option(std::string_view arg,
                                       // CLI parsing signature is stable.
                                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                       std::span<char*> args,
                                       std::size_t idx,
                                       ValidateOptions& options,
                                       bool& skip_next) -> sappp::Result<bool>
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
    if (arg == "--output" || arg == "-o") {
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

[[nodiscard]] sappp::Result<CaptureOptions> parse_capture_args(std::span<char*> args)
{
    CaptureOptions options{.compile_commands = std::string{},
                           .repo_root = std::string{},
                           .output = "./out",
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
        if (arg == "--compile-commands") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.compile_commands = *value;
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
        if (arg == "--repo-root") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.repo_root = *value;
            skip_next = true;
            continue;
        }
    }
    return options;
}

[[nodiscard]] sappp::Result<AnalyzeOptions> parse_analyze_args(std::span<char*> args)
{
    AnalyzeOptions options{.snapshot = std::string{},
                           .jobs = 0,
                           .output = "./out",
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
                            .output = "validated_results.json",
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

[[nodiscard]] sappp::Result<ExplainOptions> parse_explain_args(std::span<char*> args)
{
    ExplainOptions options{.unknown_path = std::string{},
                           .validated_path = std::nullopt,
                           .po_id = std::nullopt,
                           .unknown_id = std::nullopt,
                           .output = std::nullopt,
                           .schema_dir = "schemas",
                           .format = sappp::report::ExplainFormat::kText,
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
        if (arg == "--unknown") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.unknown_path = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--validated") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.validated_path = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--po") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.po_id = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--unknown-id") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.unknown_id = *value;
            skip_next = true;
            continue;
        }
        if (arg == "--format") {
            auto value = read_option_value(args, idx, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            if (*value == "text") {
                options.format = sappp::report::ExplainFormat::kText;
            } else if (*value == "json") {
                options.format = sappp::report::ExplainFormat::kJson;
            } else {
                return std::unexpected(
                    sappp::Error::make("InvalidArgument", "Invalid --format value: " + *value));
            }
            skip_next = true;
            continue;
        }
        if (arg == "--out") {
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
        return 1;
    }

    std::filesystem::path output_dir(options.output);
    if (auto result = ensure_directory(output_dir, "output"); !result) {
        std::println(stderr, "Error: {}", result.error().message);
        return 1;
    }
    std::filesystem::path output_file = output_dir / "build_snapshot.json";

    if (auto result = write_canonical_json_file(output_file, snapshot->json()); !result) {
        std::println(stderr,
                     "Error: failed to serialize build snapshot: {}",
                     result.error().message);
        return 1;
    }

    std::println("[capture] Wrote build_snapshot.json");
    std::println("  input: {}", options.compile_commands);
    std::println("  output: {}", output_file.string());
    return 0;
}

[[nodiscard]] int run_analyze(const AnalyzeOptions& options)
{
#if !defined(SAPPP_HAS_CLANG_FRONTEND)
    (void)options;
    std::println(
        stderr,
        "Error: frontend_clang is not built. Reconfigure with -DSAPPP_BUILD_CLANG_FRONTEND=ON");
    return 1;
#else
    (void)options.jobs;
    auto snapshot_json = read_json_file(options.snapshot);
    if (!snapshot_json) {
        std::println(stderr, "Error: {}", snapshot_json.error().message);
        return 1;
    }

    sappp::frontend_clang::FrontendClang frontend(options.schema_dir);
    auto result = frontend.analyze(*snapshot_json);
    if (!result) {
        std::println(stderr, "Error: analyze failed: {}", result.error().message);
        return 1;
    }

    auto paths = prepare_analyze_paths(options.output);
    if (!paths) {
        std::println(stderr, "Error: {}", paths.error().message);
        return 1;
    }

    if (auto write = write_canonical_json_file(paths->nir_path, result->nir); !write) {
        std::println(stderr, "Error: failed to serialize NIR: {}", write.error().message);
        return 1;
    }
    if (auto write = write_canonical_json_file(paths->source_map_path, result->source_map);
        !write) {
        std::println(stderr, "Error: failed to serialize source map: {}", write.error().message);
        return 1;
    }

    sappp::po::PoGenerator po_generator;
    auto po_list_result = po_generator.generate(result->nir);
    if (!po_list_result) {
        std::println(stderr, "Error: PO generation failed: {}", po_list_result.error().message);
        return 1;
    }

    const std::filesystem::path po_schema_path =
        std::filesystem::path(options.schema_dir) / "po.v1.schema.json";
    if (auto validation = sappp::common::validate_json(*po_list_result, po_schema_path.string());
        !validation) {
        std::println(stderr, "Error: po schema validation failed: {}", validation.error().message);
        return 1;
    }

    if (auto write = write_canonical_json_file(paths->po_path, *po_list_result); !write) {
        std::println(stderr, "Error: failed to serialize PO list: {}", write.error().message);
        return 1;
    }

    std::println("[analyze] Wrote frontend outputs");
    std::println("  snapshot: {}", options.snapshot);
    std::println("  output: {}", paths->output_dir.string());
    std::println("  nir: {}", paths->nir_path.string());
    std::println("  source_map: {}", paths->source_map_path.string());
    std::println("  po: {}", paths->po_path.string());
    return 0;
#endif
}

[[nodiscard]] int run_validate(const ValidateOptions& options)
{
    sappp::validator::Validator validator(options.input, options.schema_dir);
    auto results = validator.validate(options.strict);
    if (!results) {
        std::println(stderr, "Error: validate failed: {}", results.error().message);
        return 1;
    }
    if (auto write = validator.write_results(*results, options.output); !write) {
        std::println(stderr, "Error: failed to write validated results: {}", write.error().message);
        return 1;
    }

    std::println("[validate] Wrote validated_results.json");
    std::println("  input: {}", options.input);
    std::println("  output: {}", options.output);
    std::println("  strict: {}", options.strict ? "yes" : "no");
    return 0;
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

[[nodiscard]] int run_explain(const ExplainOptions& options)
{
    sappp::report::ExplainOptions report_options{
        .unknown_path = std::filesystem::path(options.unknown_path),
        .validated_path = options.validated_path
                              ? std::optional<std::filesystem::path>(*options.validated_path)
                              : std::nullopt,
        .po_id = options.po_id,
        .unknown_id = options.unknown_id,
        .output_path =
            options.output ? std::optional<std::filesystem::path>(*options.output) : std::nullopt,
        .schema_dir = options.schema_dir,
        .format = options.format,
    };
    auto output = sappp::report::explain_unknowns(report_options);
    if (!output) {
        std::println(stderr, "Error: explain failed: {}", output.error().message);
        return 1;
    }
    if (auto write = sappp::report::write_explain_output(report_options, *output); !write) {
        std::println(stderr, "Error: explain output failed: {}", write.error().message);
        return 1;
    }

    std::println("[explain] {}", output->summary);
    if (options.output) {
        std::println("  output: {}", *options.output);
    }
    return 0;
}
int cmd_capture(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_capture_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_capture_help();
        return 0;
    }
    if (options->compile_commands.empty()) {
        std::println(stderr, "Error: --compile-commands is required");
        print_capture_help();
        return 1;
    }
    return run_capture(*options);
}

int cmd_analyze(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_analyze_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_analyze_help();
        return 0;
    }
    if (options->snapshot.empty()) {
        std::println(stderr, "Error: --snapshot is required");
        print_analyze_help();
        return 1;
    }
    return run_analyze(*options);
}

int cmd_validate(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_validate_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_validate_help();
        return 0;
    }
    if (options->input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_validate_help();
        return 1;
    }
    return run_validate(*options);
}

int cmd_pack(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_pack_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_pack_help();
        return 0;
    }
    if (options->input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_pack_help();
        return 1;
    }
    return run_pack(*options);
}

int cmd_diff(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_diff_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_diff_help();
        return 0;
    }
    if (options->before.empty() || options->after.empty()) {
        std::println(stderr, "Error: --before and --after are required");
        print_diff_help();
        return 1;
    }
    return run_diff(*options);
}

int cmd_explain(int argc, char** argv)
{
    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    auto options = parse_explain_args(args);
    if (!options) {
        std::println(stderr, "Error: {}", options.error().message);
        return 1;
    }
    if (options->show_help) {
        print_explain_help();
        return 0;
    }
    if (options->unknown_path.empty()) {
        std::println(stderr, "Error: --unknown is required");
        print_explain_help();
        return 1;
    }
    return run_explain(*options);
}

}  // namespace

namespace {

[[nodiscard]] int run_cli(int argc, char** argv)
{
    try {
        if (argc < 2) {
            print_help();
            return 1;
        }

        std::string_view cmd = argv[1];

        if (cmd == "--help" || cmd == "-h") {
            print_help();
            return 0;
        }
        if (cmd == "--version" || cmd == "-v" || cmd == "version") {
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
        return 1;
    } catch (const std::exception& ex) {
        try {
            std::println(stderr, "Error: {}", ex.what());
        } catch (...) {
            std::terminate();
        }
        return 1;
    } catch (...) {
        try {
            std::println(stderr, "Error: unknown exception");
        } catch (...) {
            std::terminate();
        }
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    return run_cli(argc, argv);
}
