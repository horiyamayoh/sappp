/**
 * @file test_litmus_e2e.cpp
 * @brief End-to-end litmus tests for div0/null/oob.
 */

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

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
    nlohmann::json compile_db = nlohmann::json::array();
    compile_db.push_back({
        {"directory",                           repo_root.string()},
        {     "file",                              source.string()},
        {"arguments", {"clang", "-std=c11", "-c", source.string()}}
    });

    std::ofstream out(output);
    out << compile_db.dump(2);
}

void expect_bug_result(const fs::path& validated_results)
{
    std::ifstream in(validated_results);
    ASSERT_TRUE(in.is_open()) << "Failed to open " << validated_results.string();
    nlohmann::json json = nlohmann::json::parse(in);

    ASSERT_TRUE(json.contains("results")) << "validated_results missing results";
    ASSERT_TRUE(json.at("results").is_array());

    bool has_bug = false;
    for (const auto& result : json.at("results")) {
        if (result.contains("category") && result.at("category") == "BUG") {
            has_bug = true;
            break;
        }
    }
    EXPECT_TRUE(has_bug) << "Expected BUG entry in validated_results";
}

void run_litmus_case(const std::string& case_name, const fs::path& source_path)
{
    const fs::path repo_root = SAPPP_REPO_ROOT;
    const fs::path sappp_bin = fs::path(SAPPP_BIN_DIR) / "sappp";
    const fs::path schema_dir = repo_root / "schemas";

    TempDir temp_dir(std::format("sappp_e2e_{}", case_name));
    fs::path work_dir = temp_dir.path() / case_name;
    fs::create_directories(work_dir);

    fs::path compile_commands = work_dir / "compile_commands.json";
    fs::path out_dir = work_dir / "out";
    fs::path build_snapshot = out_dir / "build_snapshot.json";
    fs::path pack_path = work_dir / "pack.tar.gz";
    fs::path diff_path = work_dir / "diff.json";

    write_compile_commands(compile_commands, repo_root, source_path);

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

    std::string validate_cmd = std::format("{} validate --in {} --schema-dir {}",
                                           base,
                                           quote_path(out_dir),
                                           quote_path(schema_dir));
    ASSERT_EQ(run_command(validate_cmd), 0) << validate_cmd;

    fs::path validated_results = out_dir / "results" / "validated_results.json";
    ASSERT_TRUE(fs::exists(validated_results)) << validated_results.string();
    expect_bug_result(validated_results);

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
    run_litmus_case("div0", fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_div0.c");
}

TEST(LitmusE2E, NullDeref)
{
    run_litmus_case("null", fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_null.c");
}

TEST(LitmusE2E, OutOfBounds)
{
    run_litmus_case("oob", fs::path(SAPPP_REPO_ROOT) / "tests/end_to_end/litmus_oob.c");
}
