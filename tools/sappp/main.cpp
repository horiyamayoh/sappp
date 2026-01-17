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
#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/validator.hpp"
#include "po_generator.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
#include "frontend_clang/frontend.hpp"
#endif
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_version() {
    std::println("sappp {} ({})", sappp::VERSION, sappp::BUILD_ID);
    std::println("  semantics:    {}", sappp::SEMANTICS_VERSION);
    std::println("  proof_system: {}", sappp::PROOF_SYSTEM_VERSION);
    std::println("  profile:      {}", sappp::PROFILE_VERSION);
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

    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_capture_help();
            return 0;
        } else if (std::strcmp(argv[i], "--compile-commands") == 0 && i + 1 < argc) {
            compile_commands = argv[++i];
        } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "--repo-root") == 0 && i + 1 < argc) {
            repo_root = argv[++i];
        }
    }

    if (compile_commands.empty()) {
        std::println(stderr, "Error: --compile-commands is required");
        print_capture_help();
        return 1;
    }

    try {
        sappp::build_capture::BuildCapture capture(repo_root);
        sappp::build_capture::BuildSnapshot snapshot = capture.capture(compile_commands);

        std::filesystem::path output_dir(output);
        std::filesystem::create_directories(output_dir);
        std::filesystem::path output_file = output_dir / "build_snapshot.json";

        std::ofstream out(output_file);
        if (!out) {
            std::println(stderr, "Error: failed to open output file: {}", output_file.string());
            return 1;
        }
        out << sappp::canonical::canonicalize(snapshot.json());
        out << "\n";

        std::println("[capture] Wrote build_snapshot.json");
        std::println("  input: {}", compile_commands);
        std::println("  output: {}", output_file.string());
    } catch (const std::exception& ex) {
        std::println(stderr, "Error: capture failed: {}", ex.what());
        return 1;
    }
    return 0;
}

int cmd_analyze(int argc, char** argv) {
    std::string snapshot;
    std::string output = "./out";
    [[maybe_unused]] int jobs = 0;
    std::string schema_dir = "schemas";

    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_analyze_help();
            return 0;
        } else if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshot = argv[++i];
        } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output = argv[++i];
        } else if ((std::strcmp(argv[i], "--jobs") == 0 || std::strcmp(argv[i], "-j") == 0) && i + 1 < argc) {
            jobs = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--schema-dir") == 0 && i + 1 < argc) {
            schema_dir = argv[++i];
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
    try {
        std::ifstream in(snapshot);
        if (!in) {
            std::println(stderr, "Error: failed to open snapshot file: {}", snapshot);
            return 1;
        }
        nlohmann::json snapshot_json;
        in >> snapshot_json;

        sappp::frontend_clang::FrontendClang frontend(schema_dir);
        sappp::frontend_clang::FrontendResult result = frontend.analyze(snapshot_json);

        std::filesystem::path output_dir(output);
        std::filesystem::path frontend_dir = output_dir / "frontend";
        std::filesystem::path po_dir = output_dir / "po";
        std::filesystem::create_directories(frontend_dir);
        std::filesystem::create_directories(po_dir);

        std::filesystem::path nir_path = frontend_dir / "nir.json";
        std::filesystem::path source_map_path = frontend_dir / "source_map.json";

        std::ofstream nir_out(nir_path);
        if (!nir_out) {
            std::println(stderr, "Error: failed to write NIR output: {}", nir_path.string());
            return 1;
        }
        nir_out << sappp::canonical::canonicalize(result.nir) << "\n";

        std::ofstream source_out(source_map_path);
        if (!source_out) {
            std::println(stderr, "Error: failed to write source map output: {}", source_map_path.string());
            return 1;
        }
        source_out << sappp::canonical::canonicalize(result.source_map) << "\n";

        sappp::po::PoGenerator po_generator;
        nlohmann::json po_list = po_generator.generate(result.nir);

        std::string schema_error;
        const std::filesystem::path po_schema_path =
            std::filesystem::path(schema_dir) / "po.v1.schema.json";
        if (!sappp::common::validate_json(po_list, po_schema_path.string(), schema_error)) {
            throw std::runtime_error("po schema validation failed: " + schema_error);
        }

        std::filesystem::path po_path = po_dir / "po_list.json";
        std::ofstream po_out(po_path);
        if (!po_out) {
            std::println(stderr, "Error: failed to write PO output: {}", po_path.string());
            return 1;
        }
        po_out << sappp::canonical::canonicalize(po_list) << "\n";

        std::println("[analyze] Wrote frontend outputs");
        std::println("  snapshot: {}", snapshot);
        std::println("  output: {}", output_dir.string());
        std::println("  nir: {}", nir_path.string());
        std::println("  source_map: {}", source_map_path.string());
        std::println("  po: {}", po_path.string());
    } catch (const std::exception& ex) {
        std::println(stderr, "Error: analyze failed: {}", ex.what());
        return 1;
    }
    return 0;
#endif
}

int cmd_validate(int argc, char** argv) {
    std::string input;
    std::string output = "validated_results.json";
    bool strict = false;
    std::string schema_dir = "schemas";

    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_validate_help();
            return 0;
        } else if ((std::strcmp(argv[i], "--input") == 0 || std::strcmp(argv[i], "--in") == 0) && i + 1 < argc) {
            input = argv[++i];
        } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "--strict") == 0) {
            strict = true;
        } else if (std::strcmp(argv[i], "--schema-dir") == 0 && i + 1 < argc) {
            schema_dir = argv[++i];
        }
    }

    if (input.empty()) {
        std::println(stderr, "Error: --input is required");
        print_validate_help();
        return 1;
    }

    try {
        sappp::validator::Validator validator(input, schema_dir);
        nlohmann::json results = validator.validate(strict);
        validator.write_results(results, output);

        std::println("[validate] Wrote validated_results.json");
        std::println("  input: {}", input);
        std::println("  output: {}", output);
        std::println("  strict: {}", strict ? "yes" : "no");
    } catch (const std::exception& ex) {
        std::println(stderr, "Error: validate failed: {}", ex.what());
        return 1;
    }
    return 0;
}

int cmd_pack(int argc, char** argv) {
    std::string input;
    std::string output = "pack.tar.gz";

    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_pack_help();
            return 0;
        } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output = argv[++i];
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

    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_diff_help();
            return 0;
        } else if (std::strcmp(argv[i], "--before") == 0 && i + 1 < argc) {
            before = argv[++i];
        } else if (std::strcmp(argv[i], "--after") == 0 && i + 1 < argc) {
            after = argv[++i];
        } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            output = argv[++i];
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
