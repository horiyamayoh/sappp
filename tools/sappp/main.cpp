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

#include "sappp/version.hpp"
#include "sappp/common.hpp"
#include "sappp/analyzer.hpp"
#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/validator.hpp"
#include "po_generator.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
#include "frontend_clang/frontend.hpp"
#endif
#include <charconv>
#include <filesystem>
#include <fstream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

void print_version() {
    std::println("sappp {} ({})", sappp::kVersion, sappp::kBuildId);
    std::println("  semantics:    {}", sappp::kSemanticsVersion);
    std::println("  proof_system: {}", sappp::kProofSystemVersion);
    std::println("  profile:      {}", sappp::kProfileVersion);
}

void print_help() {
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

void print_capture_help() {
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

void print_analyze_help() {
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

void print_validate_help() {
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

void print_pack_help() {
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

void print_diff_help() {
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

int cmd_capture(int argc, char** argv) {
    std::string compile_commands;
    std::string output = "./out";
    std::string repo_root;

    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg = arg_ptr ? std::string_view(arg_ptr) : std::string_view();
        if (arg == "--help" || arg == "-h") {
            print_capture_help();
            return 0;
        } else if (arg == "--compile-commands" && i + 1 < std::ssize(args)) {
            compile_commands = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < std::ssize(args)) {
            output = args[idx + 1];
            skip_next = true;
        } else if (arg == "--repo-root" && i + 1 < std::ssize(args)) {
            repo_root = args[idx + 1];
            skip_next = true;
        }
    }

    if (compile_commands.empty()) {
        std::println(stderr, "Error: --compile-commands is required");
        print_capture_help();
        return 1;
    }

    sappp::build_capture::BuildCapture capture(repo_root);
    auto snapshot = capture.capture(compile_commands);
    if (!snapshot) {
        std::println(stderr, "Error: capture failed: {}", snapshot.error().message);
        return 1;
    }

    std::filesystem::path output_dir(output);
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        std::println(stderr, "Error: failed to create output directory: {}", ec.message());
        return 1;
    }
    std::filesystem::path output_file = output_dir / "build_snapshot.json";

    std::ofstream out(output_file);
    if (!out) {
        std::println(stderr, "Error: failed to open output file: {}", output_file.string());
        return 1;
    }
    auto canonical = sappp::canonical::canonicalize(snapshot->json());
    if (!canonical) {
        std::println(stderr, "Error: failed to serialize build snapshot: {}", canonical.error().message);
        return 1;
    }
    out << *canonical << "\n";

    std::println("[capture] Wrote build_snapshot.json");
    std::println("  input: {}", compile_commands);
    std::println("  output: {}", output_file.string());
    return 0;
}

int cmd_analyze(int argc, char** argv) {
    std::string snapshot;
    std::string output = "./out";
    [[maybe_unused]] int jobs = 0;
    std::string schema_dir = "schemas";

    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg = arg_ptr ? std::string_view(arg_ptr) : std::string_view();
        if (arg == "--help" || arg == "-h") {
            print_analyze_help();
            return 0;
        } else if (arg == "--snapshot" && i + 1 < std::ssize(args)) {
            snapshot = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < std::ssize(args)) {
            output = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--jobs" || arg == "-j") && i + 1 < std::ssize(args)) {
            std::string_view value = args[idx + 1] ? std::string_view(args[idx + 1]) : std::string_view();
            int parsed = 0;
            auto* begin = value.begin();
            auto* end = value.end();
            auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || ptr != end) {
                std::println(stderr, "Error: invalid --jobs value: {}", value);
                return 1;
            }
            jobs = parsed;
            skip_next = true;
        } else if (arg == "--schema-dir" && i + 1 < std::ssize(args)) {
            schema_dir = args[idx + 1];
            skip_next = true;
        }
    }

    if (snapshot.empty()) {
        std::println(stderr, "Error: --snapshot is required");
        print_analyze_help();
        return 1;
    }

#if !defined(SAPPP_HAS_CLANG_FRONTEND)
    std::println(stderr, "Error: frontend_clang is not built. Reconfigure with -DSAPPP_BUILD_CLANG_FRONTEND=ON");
    return 1;
#else
    (void)jobs;
    std::ifstream in(snapshot);
    if (!in) {
        std::println(stderr, "Error: failed to open snapshot file: {}", snapshot);
        return 1;
    }
    nlohmann::json snapshot_json;
    try {
        in >> snapshot_json;
    } catch (const std::exception& ex) {
        std::println(stderr, "Error: failed to parse snapshot JSON: {}", ex.what());
        return 1;
    }

    sappp::frontend_clang::FrontendClang frontend(schema_dir);
    auto result = frontend.analyze(snapshot_json);
    if (!result) {
        std::println(stderr, "Error: analyze failed: {}", result.error().message);
        return 1;
    }

    std::filesystem::path output_dir(output);
    std::filesystem::path frontend_dir = output_dir / "frontend";
    std::filesystem::path po_dir = output_dir / "po";
    std::error_code ec;
    std::filesystem::create_directories(frontend_dir, ec);
    if (ec) {
        std::println(stderr, "Error: failed to create frontend output dir: {}", ec.message());
        return 1;
    }
    std::filesystem::create_directories(po_dir, ec);
    if (ec) {
        std::println(stderr, "Error: failed to create PO output dir: {}", ec.message());
        return 1;
    }

    std::filesystem::path nir_path = frontend_dir / "nir.json";
    std::filesystem::path source_map_path = frontend_dir / "source_map.json";

    std::ofstream nir_out(nir_path);
    if (!nir_out) {
        std::println(stderr, "Error: failed to write NIR output: {}", nir_path.string());
        return 1;
    }
    auto nir_canonical = sappp::canonical::canonicalize(result->nir);
    if (!nir_canonical) {
        std::println(stderr, "Error: failed to serialize NIR: {}", nir_canonical.error().message);
        return 1;
    }
    nir_out << *nir_canonical << "\n";

    std::ofstream source_out(source_map_path);
    if (!source_out) {
        std::println(stderr, "Error: failed to write source map output: {}", source_map_path.string());
        return 1;
    }
    auto source_canonical = sappp::canonical::canonicalize(result->source_map);
    if (!source_canonical) {
        std::println(stderr, "Error: failed to serialize source map: {}", source_canonical.error().message);
        return 1;
    }
    source_out << *source_canonical << "\n";

    sappp::po::PoGenerator po_generator;
    auto po_list_result = po_generator.generate(result->nir);
    if (!po_list_result) {
        std::println(stderr, "Error: PO generation failed: {}", po_list_result.error().message);
        return 1;
    }

    const std::filesystem::path po_schema_path =
        std::filesystem::path(schema_dir) / "po.v1.schema.json";
    if (auto validation = sappp::common::validate_json(*po_list_result, po_schema_path.string()); !validation) {
        std::println(stderr, "Error: po schema validation failed: {}", validation.error().message);
        return 1;
    }

    std::filesystem::path po_path = po_dir / "po_list.json";
    std::ofstream po_out(po_path);
    if (!po_out) {
        std::println(stderr, "Error: failed to write PO output: {}", po_path.string());
        return 1;
    }
    auto po_canonical = sappp::canonical::canonicalize(*po_list_result);
    if (!po_canonical) {
        std::println(stderr, "Error: failed to serialize PO list: {}", po_canonical.error().message);
        return 1;
    }
    po_out << *po_canonical << "\n";

    sappp::analyzer::Analyzer analyzer(schema_dir);
    auto analyzer_result = analyzer.analyze(*po_list_result, output_dir.string());
    if (!analyzer_result) {
        std::println(stderr, "Error: analyzer failed: {}", analyzer_result.error().message);
        return 1;
    }

    std::filesystem::path unknown_path = output_dir / "analyzer" / "unknown_ledger.json";
    std::filesystem::path certstore_dir = output_dir / "certstore";

    std::println("[analyze] Wrote frontend outputs");
    std::println("  snapshot: {}", snapshot);
    std::println("  output: {}", output_dir.string());
    std::println("  nir: {}", nir_path.string());
    std::println("  source_map: {}", source_map_path.string());
    std::println("  po: {}", po_path.string());
    std::println("  unknown: {}", unknown_path.string());
    std::println("  certstore: {}", certstore_dir.string());
    return 0;
#endif
}

int cmd_validate(int argc, char** argv) {
    std::string input;
    std::string output = "validated_results.json";
    bool strict = false;
    std::string schema_dir = "schemas";

    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg = arg_ptr ? std::string_view(arg_ptr) : std::string_view();
        if (arg == "--help" || arg == "-h") {
            print_validate_help();
            return 0;
        } else if ((arg == "--input" || arg == "--in") && i + 1 < std::ssize(args)) {
            input = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < std::ssize(args)) {
            output = args[idx + 1];
            skip_next = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--schema-dir" && i + 1 < std::ssize(args)) {
            schema_dir = args[idx + 1];
            skip_next = true;
        }
    }

    if (input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_validate_help();
        return 1;
    }

    sappp::validator::Validator validator(input, schema_dir);
    auto results = validator.validate(strict);
    if (!results) {
        std::println(stderr, "Error: validate failed: {}", results.error().message);
        return 1;
    }
    if (auto write = validator.write_results(*results, output); !write) {
        std::println(stderr, "Error: failed to write validated results: {}", write.error().message);
        return 1;
    }

    std::println("[validate] Wrote validated_results.json");
    std::println("  input: {}", input);
    std::println("  output: {}", output);
    std::println("  strict: {}", strict ? "yes" : "no");
    return 0;
}

int cmd_pack(int argc, char** argv) {
    std::string input;
    std::string output = "pack.tar.gz";

    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg = arg_ptr ? std::string_view(arg_ptr) : std::string_view();
        if (arg == "--help" || arg == "-h") {
            print_pack_help();
            return 0;
        } else if (arg == "--input" && i + 1 < std::ssize(args)) {
            input = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < std::ssize(args)) {
            output = args[idx + 1];
            skip_next = true;
        }
    }

    if (input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_pack_help();
        return 1;
    }

    std::println("[pack] Not yet implemented");
    std::println("  input: {}", input);
    std::println("  output: {}", output);
    return 0;
}

int cmd_diff(int argc, char** argv) {
    std::string before;
    std::string after;
    std::string output = "diff.json";

    auto args = std::span<char*>(argv, static_cast<std::size_t>(argc));
    bool skip_next = false;
    for (auto [i, arg_ptr] : std::views::enumerate(args)) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        std::string_view arg = arg_ptr ? std::string_view(arg_ptr) : std::string_view();
        if (arg == "--help" || arg == "-h") {
            print_diff_help();
            return 0;
        } else if (arg == "--before" && i + 1 < std::ssize(args)) {
            before = args[idx + 1];
            skip_next = true;
        } else if (arg == "--after" && i + 1 < std::ssize(args)) {
            after = args[idx + 1];
            skip_next = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < std::ssize(args)) {
            output = args[idx + 1];
            skip_next = true;
        }
    }

    if (before.empty() || after.empty()) {
        std::println(stderr, "Error: --before and --after are required");
        print_diff_help();
        return 1;
    }

    std::println("[diff] Not yet implemented");
    std::println("  before: {}", before);
    std::println("  after: {}", after);
    std::println("  output: {}", output);
    return 0;
}

int cmd_explain(int argc, char** argv) {
    std::println("[explain] Not yet implemented");
    (void)argc;
    (void)argv;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
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

    // Dispatch to subcommand
    int sub_argc = argc - 2;
    char** sub_argv = argv + 2;

    if (cmd == "capture") {
        return cmd_capture(sub_argc, sub_argv);
    } else if (cmd == "analyze") {
        return cmd_analyze(sub_argc, sub_argv);
    } else if (cmd == "validate") {
        return cmd_validate(sub_argc, sub_argv);
    } else if (cmd == "pack") {
        return cmd_pack(sub_argc, sub_argv);
    } else if (cmd == "diff") {
        return cmd_diff(sub_argc, sub_argv);
    } else if (cmd == "explain") {
        return cmd_explain(sub_argc, sub_argv);
    } else {
        std::println(stderr, "Unknown command: {}", cmd);
        print_help();
        return 1;
    }
}
