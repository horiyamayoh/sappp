/**
 * @file main.cpp
 * @brief SAP++ CLI entry point
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
#include "sappp/po.hpp"
#include "sappp/validator.hpp"
#if defined(SAPPP_HAS_CLANG_FRONTEND)
#include "frontend_clang/frontend.hpp"
#endif
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

void print_version() {
    std::cout << "sappp " << sappp::VERSION << " (" << sappp::BUILD_ID << ")\n"
              << "  semantics:    " << sappp::SEMANTICS_VERSION << "\n"
              << "  proof_system: " << sappp::PROOF_SYSTEM_VERSION << "\n"
              << "  profile:      " << sappp::PROFILE_VERSION << "\n";
}

void print_help() {
    std::cout << R"(SAP++ - Sound, Static Absence-Proving Analyzer for C++

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
)";
}

void print_capture_help() {
    std::cout << R"(Usage: sappp capture [options]

Capture build conditions from compile_commands.json

Options:
  --compile-commands FILE   Path to compile_commands.json (required)
  --output DIR, -o          Output directory (default: ./out)
  --repo-root DIR           Repository root for relative paths
  --help, -h                Show this help

Output:
  <output>/build_snapshot.json
)";
}

void print_analyze_help() {
    std::cout << R"(Usage: sappp analyze [options]

Run static analysis on captured build

Options:
  --snapshot FILE           Path to build_snapshot.json (required)
  --output DIR, -o          Output directory (default: ./out)
  --jobs N, -j N            Number of parallel jobs
  --config FILE             Analysis configuration file
  --specdb DIR              Path to Spec DB directory
  --help, -h                Show this help

Output:
  <output>/frontend/nir.json
  <output>/frontend/source_map.json
  <output>/po/po_list.json
  <output>/analyzer/unknown_ledger.json
  <output>/certstore/
)";
}

void print_validate_help() {
    std::cout << R"(Usage: sappp validate [options]

Validate certificates and confirm SAFE/BUG results

Options:
  --input DIR, --in DIR     Input directory containing analysis outputs (required)
  --output FILE, -o         Output file (default: validated_results.json)
  --strict                  Fail on any validation error (no downgrade)
  --schema-dir DIR          Path to schema directory (default: ./schemas)
  --help, -h                Show this help

Output:
  validated_results.json
)";
}

void print_pack_help() {
    std::cout << R"(Usage: sappp pack [options]

Create reproducibility pack

Options:
  --input DIR               Input directory containing analysis outputs (required)
  --output FILE, -o         Output file (default: pack.tar.gz)
  --help, -h                Show this help

Output:
  <output>.tar.gz
  <output>.manifest.json
)";
}

void print_diff_help() {
    std::cout << R"(Usage: sappp diff [options]

Compare before/after analysis results

Options:
  --before FILE             Path to before validated_results.json (required)
  --after FILE              Path to after validated_results.json (required)
  --output FILE, -o         Output file (default: diff.json)
  --help, -h                Show this help

Output:
  diff.json
)";
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
        std::cerr << "Error: --compile-commands is required\n";
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
            std::cerr << "Error: failed to open output file: " << output_file << "\n";
            return 1;
        }
        out << sappp::canonical::canonicalize(snapshot.json());
        out << "\n";

        std::cout << "[capture] Wrote build_snapshot.json\n";
        std::cout << "  input: " << compile_commands << "\n";
        std::cout << "  output: " << output_file.string() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: capture failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

int cmd_analyze(int argc, char** argv) {
    std::string snapshot;
    std::string output = "./out";
    int jobs = 0;

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
        }
    }

    if (snapshot.empty()) {
        std::cerr << "Error: --snapshot is required\n";
        print_analyze_help();
        return 1;
    }

#if !defined(SAPPP_HAS_CLANG_FRONTEND)
    std::cerr << "Error: frontend_clang is not built. Reconfigure with -DSAPPP_BUILD_CLANG_FRONTEND=ON\n";
    return 1;
#else
    (void)jobs;
    try {
        std::ifstream in(snapshot);
        if (!in) {
            std::cerr << "Error: failed to open snapshot file: " << snapshot << "\n";
            return 1;
        }
        nlohmann::json snapshot_json;
        in >> snapshot_json;

        sappp::frontend_clang::FrontendClang frontend;
        sappp::frontend_clang::FrontendResult result = frontend.analyze(snapshot_json);

        std::filesystem::path output_dir(output);
        std::filesystem::path frontend_dir = output_dir / "frontend";
        std::filesystem::create_directories(frontend_dir);

        std::filesystem::path nir_path = frontend_dir / "nir.json";
        std::filesystem::path source_map_path = frontend_dir / "source_map.json";
        std::filesystem::path po_dir = output_dir / "po";
        std::filesystem::path po_path = po_dir / "po_list.json";

        std::ofstream nir_out(nir_path);
        if (!nir_out) {
            std::cerr << "Error: failed to write NIR output: " << nir_path << "\n";
            return 1;
        }
        nir_out << sappp::canonical::canonicalize(result.nir) << "\n";

        std::ofstream source_out(source_map_path);
        if (!source_out) {
            std::cerr << "Error: failed to write source map output: " << source_map_path << "\n";
            return 1;
        }
        source_out << sappp::canonical::canonicalize(result.source_map) << "\n";

        std::filesystem::create_directories(po_dir);
        sappp::po::PoGenerator po_generator;
        nlohmann::json po_list = po_generator.generate(result.nir);

        std::ofstream po_out(po_path);
        if (!po_out) {
            std::cerr << "Error: failed to write PO output: " << po_path << "\n";
            return 1;
        }
        po_out << sappp::canonical::canonicalize(po_list) << "\n";

        std::cout << "[analyze] Wrote frontend outputs\n";
        std::cout << "  snapshot: " << snapshot << "\n";
        std::cout << "  output: " << output_dir.string() << "\n";
        std::cout << "  nir: " << nir_path.string() << "\n";
        std::cout << "  source_map: " << source_map_path.string() << "\n";
        std::cout << "  po_list: " << po_path.string() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: analyze failed: " << ex.what() << "\n";
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
        std::cerr << "Error: --input is required\n";
        print_validate_help();
        return 1;
    }

    try {
        sappp::validator::Validator validator(input, schema_dir);
        nlohmann::json results = validator.validate(strict);
        validator.write_results(results, output);

        std::cout << "[validate] Wrote validated_results.json\n";
        std::cout << "  input: " << input << "\n";
        std::cout << "  output: " << output << "\n";
        std::cout << "  strict: " << (strict ? "yes" : "no") << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: validate failed: " << ex.what() << "\n";
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
        std::cerr << "Error: --input is required\n";
        print_pack_help();
        return 1;
    }

    std::cout << "[pack] Not yet implemented\n";
    std::cout << "  input: " << input << "\n";
    std::cout << "  output: " << output << "\n";
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
        std::cerr << "Error: --before and --after are required\n";
        print_diff_help();
        return 1;
    }

    std::cout << "[diff] Not yet implemented\n";
    std::cout << "  before: " << before << "\n";
    std::cout << "  after: " << after << "\n";
    std::cout << "  output: " << output << "\n";
    return 0;
}

int cmd_explain(int argc, char** argv) {
    std::cout << "[explain] Not yet implemented\n";
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

    std::string cmd = argv[1];

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
        std::cerr << "Unknown command: " << cmd << "\n";
        print_help();
        return 1;
    }
}
