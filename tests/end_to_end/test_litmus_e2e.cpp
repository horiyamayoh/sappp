/**
 * @file test_litmus_e2e.cpp
 * @brief End-to-end litmus tests for div0, null, out-of-bounds, use-after-lifetime,
 *        double-free, uninitialized read, exception RAII, and virtual calls.
 */

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;

class TempDir
{
public:
    explicit TempDir(const std::string& name)
        : m_path(fs::temp_directory_path() / name)
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

[[nodiscard]] std::string quote_path(const fs::path& path)
{
    return std::format("\"{}\"", path.string());
}

[[nodiscard]] int run_command(const std::string& command)
{
    return std::system(command.c_str());
}

void write_compile_commands(const fs::path& output,
                            const fs::path& repo_root,
                            const fs::path& source)
{
    std::string extension = source.extension().string();
    for (char& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const bool is_c = extension == ".c";
    const std::string compiler = is_c ? "clang" : "clang++";
    const std::string standard = is_c ? "-std=c11" : "-std=c++23";

    nlohmann::json compile_db = nlohmann::json::array();
    compile_db.push_back({
        {"directory",                          repo_root.string()},
        {     "file",                             source.string()},
        {"arguments", {compiler, standard, "-c", source.string()}}
    });

    std::ofstream out(output);
    out << compile_db.dump(2);
}

struct LitmusCase
{
    std::string name;
    fs::path source_path;
    std::vector<std::string> expected_po_kinds;
    std::vector<std::string> expected_categories;
    std::vector<std::string> required_ops;
    std::vector<std::string> required_edge_kinds;
    bool require_vcall_candidates = false;
    std::vector<std::string> expected_unknown_codes;
};

nlohmann::json load_json_file(const fs::path& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open " + path.string());
    }
    return nlohmann::json::parse(in);
}

void expect_categories(const nlohmann::json& validated_results,
                       const std::vector<std::string>& expected_categories)
{
    ASSERT_TRUE(validated_results.contains("results")) << "validated_results missing results";
    ASSERT_TRUE(validated_results.at("results").is_array());

    std::unordered_set<std::string> categories;
    for (const auto& result : validated_results.at("results")) {
        if (result.contains("category") && result.at("category").is_string()) {
            categories.insert(result.at("category").get<std::string>());
        }
    }
    for (const auto& category : expected_categories) {
        EXPECT_NE(categories.find(category), categories.end())
            << "Expected category " << category << " in validated_results";
    }
}

void expect_unknown_codes(const nlohmann::json& unknown_ledger,
                          const std::vector<std::string>& expected_unknown_codes)
{
    ASSERT_TRUE(unknown_ledger.contains("unknowns")) << "unknown_ledger missing unknowns";
    ASSERT_TRUE(unknown_ledger.at("unknowns").is_array());

    std::unordered_set<std::string> codes;
    for (const auto& unknown : unknown_ledger.at("unknowns")) {
        if (unknown.contains("unknown_code") && unknown.at("unknown_code").is_string()) {
            codes.insert(unknown.at("unknown_code").get<std::string>());
        }
    }
    for (const auto& code : expected_unknown_codes) {
        EXPECT_NE(codes.find(code), codes.end())
            << "Expected unknown_code " << code << " in unknown_ledger";
    }
}

void expect_po_kinds(const nlohmann::json& po_list,
                     const std::vector<std::string>& expected_po_kinds)
{
    ASSERT_TRUE(po_list.contains("pos")) << "po_list missing pos";
    ASSERT_TRUE(po_list.at("pos").is_array());

    std::unordered_set<std::string> po_kinds;
    for (const auto& po : po_list.at("pos")) {
        if (po.contains("po_kind") && po.at("po_kind").is_string()) {
            po_kinds.insert(po.at("po_kind").get<std::string>());
        }
    }
    for (const auto& po_kind : expected_po_kinds) {
        EXPECT_NE(po_kinds.find(po_kind), po_kinds.end())
            << "Expected po_kind " << po_kind << " in po_list";
    }
}

std::unordered_set<std::string> collect_ops(const nlohmann::json& nir)
{
    std::unordered_set<std::string> ops;
    for (const auto& func : nir.at("functions")) {
        for (const auto& block : func.at("cfg").at("blocks")) {
            for (const auto& inst : block.at("insts")) {
                ops.insert(inst.at("op").get<std::string>());
            }
        }
    }
    return ops;
}

std::unordered_set<std::string> collect_edge_kinds(const nlohmann::json& nir)
{
    std::unordered_set<std::string> kinds;
    for (const auto& func : nir.at("functions")) {
        for (const auto& edge : func.at("cfg").at("edges")) {
            kinds.insert(edge.at("kind").get<std::string>());
        }
    }
    return kinds;
}

bool has_vcall_candidates(const nlohmann::json& nir)
{
    bool has_vcall = false;
    bool has_candidate_set = false;
    for (const auto& func : nir.at("functions")) {
        for (const auto& block : func.at("cfg").at("blocks")) {
            for (const auto& inst : block.at("insts")) {
                if (inst.at("op").get<std::string>() == "vcall") {
                    has_vcall = true;
                }
            }
        }
        if (func.contains("tables") && func.at("tables").contains("vcall_candidates")) {
            const auto& candidates = func.at("tables").at("vcall_candidates");
            if (candidates.is_array() && !candidates.empty()) {
                has_candidate_set = true;
            }
        }
    }
    return has_vcall && has_candidate_set;
}

void run_litmus_case(const LitmusCase& test_case)
{
    const fs::path repo_root = SAPPP_REPO_ROOT;
    const fs::path sappp_bin = fs::path(SAPPP_BIN_DIR) / "sappp";
    const fs::path schema_dir = repo_root / "schemas";

    TempDir temp_dir(std::format("sappp_e2e_{}", test_case.name));
    fs::path work_dir = temp_dir.path() / test_case.name;
    fs::create_directories(work_dir);

    fs::path compile_commands = work_dir / "compile_commands.json";
    fs::path out_dir = work_dir / "out";
    fs::path build_snapshot = out_dir / "build_snapshot.json";
    fs::path pack_path = work_dir / "pack.tar.gz";
    fs::path diff_path = work_dir / "diff.json";

    write_compile_commands(compile_commands, repo_root, test_case.source_path);

    std::string base = std::format("cd {} && {}", quote_path(repo_root), quote_path(sappp_bin));

    std::string capture_cmd =
        std::format("{} capture --compile-commands {} --out {} --repo-root {}",
                    base,
                    quote_path(compile_commands),
                    quote_path(build_snapshot),
                    quote_path(repo_root));
    ASSERT_EQ(run_command(capture_cmd), 0) << capture_cmd;

    std::string analyze_cmd = std::format("{} analyze --build {} --out {} --schema-dir {} --jobs 1",
                                          base,
                                          quote_path(build_snapshot),
                                          quote_path(out_dir),
                                          quote_path(schema_dir));
    ASSERT_EQ(run_command(analyze_cmd), 0) << analyze_cmd;

    if (!test_case.expected_po_kinds.empty()) {
        fs::path po_list = out_dir / "po" / "po_list.json";
        ASSERT_TRUE(fs::exists(po_list)) << po_list.string();
        expect_po_kinds(load_json_file(po_list), test_case.expected_po_kinds);
    }

    if (!test_case.expected_unknown_codes.empty()) {
        fs::path unknown_path = out_dir / "analyzer" / "unknown_ledger.json";
        ASSERT_TRUE(fs::exists(unknown_path)) << unknown_path.string();
        expect_unknown_codes(load_json_file(unknown_path), test_case.expected_unknown_codes);
    }

    if (!test_case.required_ops.empty() || !test_case.required_edge_kinds.empty()
        || test_case.require_vcall_candidates) {
        fs::path nir_path = out_dir / "frontend" / "nir.json";
        ASSERT_TRUE(fs::exists(nir_path)) << nir_path.string();
        auto nir = load_json_file(nir_path);
        auto ops = collect_ops(nir);
        for (const auto& op : test_case.required_ops) {
            EXPECT_NE(ops.find(op), ops.end()) << "Expected op " << op << " in NIR";
        }
        auto edge_kinds = collect_edge_kinds(nir);
        for (const auto& edge_kind : test_case.required_edge_kinds) {
            EXPECT_NE(edge_kinds.find(edge_kind), edge_kinds.end())
                << "Expected edge kind " << edge_kind << " in NIR";
        }
        if (test_case.require_vcall_candidates) {
            EXPECT_TRUE(has_vcall_candidates(nir)) << "Expected vcall candidates in NIR";
        }
    }

    std::string validate_cmd = std::format("{} validate --in {} --schema-dir {}",
                                           base,
                                           quote_path(out_dir),
                                           quote_path(schema_dir));
    ASSERT_EQ(run_command(validate_cmd), 0) << validate_cmd;

    fs::path validated_results = out_dir / "results" / "validated_results.json";
    ASSERT_TRUE(fs::exists(validated_results)) << validated_results.string();
    if (!test_case.expected_categories.empty()) {
        expect_categories(load_json_file(validated_results), test_case.expected_categories);
    }

    std::string pack_cmd =
        std::format("{} pack --in {} --out {}", base, quote_path(out_dir), quote_path(pack_path));
    ASSERT_EQ(run_command(pack_cmd), 0) << pack_cmd;
    ASSERT_TRUE(fs::exists(pack_path)) << pack_path.string();

    std::string diff_cmd = std::format("{} diff --before {} --after {} --out {}",
                                       base,
                                       quote_path(pack_path),
                                       quote_path(pack_path),
                                       quote_path(diff_path));
    ASSERT_EQ(run_command(diff_cmd), 0) << diff_cmd;
    ASSERT_TRUE(fs::exists(diff_path)) << diff_path.string();
}

}  // namespace

TEST(LitmusE2E, Div0)
{
    run_litmus_case(LitmusCase{
        .name = "div0",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_div0.c",
        .expected_po_kinds = {"UB.DivZero"},
        .expected_categories = {"UNKNOWN"},
        .required_ops = {},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, NullDeref)
{
    run_litmus_case(LitmusCase{
        .name = "null",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_null.c",
        .expected_po_kinds = {"UB.NullDeref"},
        .expected_categories = {"BUG"},
        .required_ops = {},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, OutOfBounds)
{
    run_litmus_case(LitmusCase{
        .name = "oob",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_oob.c",
        .expected_po_kinds = {"UB.OutOfBounds"},
        .expected_categories = {"BUG"},
        .required_ops = {},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, UseAfterLifetime)
{
    run_litmus_case(LitmusCase{
        .name = "use_after_lifetime",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_use_after_lifetime.cpp",
        .expected_po_kinds = {"UseAfterLifetime"},
        .expected_categories = {"BUG"},
        .required_ops = {"dtor"},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, DoubleFree)
{
    run_litmus_case(LitmusCase{
        .name = "double_free",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_double_free.cpp",
        .expected_po_kinds = {"DoubleFree"},
        .expected_categories = {"BUG"},
        .required_ops = {"alloc", "free", "dtor"},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, InvalidFree)
{
    run_litmus_case(LitmusCase{
        .name = "invalid_free",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_invalid_free.cpp",
        .expected_po_kinds = {"InvalidFree"},
        .expected_categories = {"BUG"},
        .required_ops = {"free"},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, UninitRead)
{
    run_litmus_case(LitmusCase{
        .name = "uninit_read",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_uninit_read.cpp",
        .expected_po_kinds = {"UninitRead"},
        .expected_categories = {"UNKNOWN"},
        .required_ops = {},
        .required_edge_kinds = {},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, ExceptionRaii)
{
    run_litmus_case(LitmusCase{
        .name = "exception_raii",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_exception_raii.cpp",
        .expected_po_kinds = {"UB.Shift"},
        .expected_categories = {"SAFE"},
        .required_ops = {"invoke", "throw", "landingpad", "dtor"},
        .required_edge_kinds = {"exception"},
        .require_vcall_candidates = false,
        .expected_unknown_codes = {},
    });
}

TEST(LitmusE2E, VirtualCall)
{
    run_litmus_case(LitmusCase{
        .name = "vcall",
        .source_path = fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_vcall.cpp",
        .expected_po_kinds = {},
        .expected_categories = {"UNKNOWN"},
        .required_ops = {"vcall"},
        .required_edge_kinds = {},
        .require_vcall_candidates = true,
        .expected_unknown_codes = {"VirtualCall.MissingContract.Pre"},
    });
}
