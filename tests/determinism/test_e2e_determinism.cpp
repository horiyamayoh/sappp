/**
 * @file test_e2e_determinism.cpp
 * @brief E2E determinism test for sappp analyze/validate/pack
 *
 * Validates that running with --jobs=1 and --jobs=8 yields identical
 * PO IDs, UNKNOWN IDs, validated results, and pack manifest digests.
 */

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::determinism::tests {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

#if defined(SAPPP_HAS_CLANG_FRONTEND)
constexpr std::string_view kBuildSnapshotName = "build_snapshot.json";
constexpr std::string_view kSapppBinary = SAPPP_TEST_SAPPP_BIN;
constexpr std::string_view kSchemaDir = SAPPP_SCHEMA_DIR;
#endif

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

#if defined(SAPPP_HAS_CLANG_FRONTEND)
[[nodiscard]] std::string quote_arg(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return std::format("\"{}\"", escaped);
}

[[nodiscard]] std::string join_command(const std::vector<std::string>& args)
{
    std::string result;
    for (auto [idx, arg] : std::views::enumerate(args)) {
        if (idx > 0) {
            result.push_back(' ');
        }
        result += quote_arg(arg);
    }
    return result;
}

[[nodiscard]] int run_command(const std::vector<std::string>& args)
{
    const auto command = join_command(args);
    return std::system(command.c_str());
}

[[nodiscard]] int run_command_in_dir(const fs::path& dir, const std::vector<std::string>& args)
{
    const auto command = std::format("cd {} && {}", quote_arg(dir.string()), join_command(args));
    return std::system(command.c_str());
}
[[nodiscard]] std::vector<std::string> make_sappp_command(std::string_view subcommand)
{
    return {std::string(kSapppBinary), std::string(subcommand)};
}

[[nodiscard]] fs::path write_source_file(const fs::path& dir)
{
    fs::path source_path = dir / "input.cpp";
    std::ofstream out(source_path);
    out << R"(#include <cstddef>

void sappp_sink(const char* kind);
void sappp_check(const char* kind, bool predicate);

struct Guard {
    ~Guard() {}
};

struct Base {
    virtual int value() { return 1; }
    virtual ~Base() = default;
};

struct Derived : Base {
    int value() override { return 2; }
};

void may_throw();

int div0(int x) {
    return 1 / x;
}

int use_after_lifetime() {
    int* ptr = nullptr;
    {
        int use_after_lifetime = 7;
        ptr = &use_after_lifetime;
    }
    sappp_sink("use-after-lifetime");
    return *ptr;
}

int double_free() {
    int* ptr = new int(1);
    delete ptr;
    sappp_sink("double_free");
    delete ptr;
    return 0;
}

int uninit_read() {
    int value;
    sappp_sink("uninit_read");
    return value;
}

int virtual_call() {
    Base* ptr = new Derived();
    int out = ptr->value();
    delete ptr;
    return out;
}

int exception_raii() {
    try {
        Guard guard;
        may_throw();
        sappp_check("shift", false);
        throw 1;
    } catch (...) {
        return 0;
    }
}

int main() {
    int* p = nullptr;
    if (p) {
        return *p;
    }
    int arr[2] = {0, 1};
    return arr[0] + div0(1) + use_after_lifetime() + double_free() + uninit_read()
           + virtual_call() + exception_raii();
}
)";
    return source_path;
}

[[nodiscard]] fs::path write_compile_commands(const fs::path& root, const fs::path& source_path)
{
    fs::path build_dir = root / "build";
    fs::create_directories(build_dir);

    Json compile_commands = Json::array();
    compile_commands.push_back({
        {"directory",                                    build_dir.string()},
        {     "file",                                  source_path.string()},
        {"arguments", {"clang++", "-std=c++23", "-c", source_path.string()}}
    });

    fs::path compile_commands_path = build_dir / "compile_commands.json";
    std::ofstream out(compile_commands_path);
    out << compile_commands.dump(2);
    return compile_commands_path;
}

[[nodiscard]] Json load_json(const fs::path& path)
{
    std::ifstream in(path);
    return Json::parse(in);
}

[[nodiscard]] std::vector<std::string> sorted_strings_from(const Json& array, std::string_view key)
{
    std::vector<std::string> values;
    values.reserve(array.size());
    for (const auto& item : array) {
        values.push_back(item.at(std::string(key)).get<std::string>());
    }
    std::ranges::stable_sort(values);
    return values;
}

[[nodiscard]] sappp::Result<std::string> file_digest(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open file: " + path.string()));
    }
    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return sappp::common::sha256_prefixed(content);
}

struct AnalyzeOutputs
{
    std::vector<std::string> po_ids;
    std::vector<std::string> unknown_ids;
    std::vector<std::string> result_ids;
    std::string pack_manifest_digest;

    AnalyzeOutputs()
        : po_ids()
        , unknown_ids()
        , result_ids()
        , pack_manifest_digest()
    {}
};

[[nodiscard]] sappp::Result<AnalyzeOutputs> collect_outputs(const fs::path& output_dir,
                                                            const fs::path& pack_manifest)
{
    AnalyzeOutputs outputs;
    Json po_list = load_json(output_dir / "po" / "po_list.json");
    Json unknowns = load_json(output_dir / "analyzer" / "unknown_ledger.json");
    Json results = load_json(output_dir / "results" / "validated_results.json");

    outputs.po_ids = sorted_strings_from(po_list.at("pos"), "po_id");
    outputs.unknown_ids = sorted_strings_from(unknowns.at("unknowns"), "unknown_stable_id");
    outputs.result_ids = sorted_strings_from(results.at("results"), "po_id");

    auto digest = file_digest(pack_manifest);
    if (!digest) {
        return std::unexpected(digest.error());
    }
    outputs.pack_manifest_digest = std::move(*digest);
    return outputs;
}

[[nodiscard]] sappp::Result<void> run_sappp_capture(const fs::path& compile_commands,
                                                    const fs::path& output_path,
                                                    const fs::path& repo_root)
{
    auto command = make_sappp_command("capture");
    command.push_back("--compile-commands");
    command.push_back(compile_commands.string());
    command.push_back("--out");
    command.push_back(output_path.string());
    command.push_back("--repo-root");
    command.push_back(repo_root.string());
    command.push_back("--schema-dir");
    command.push_back(std::string(kSchemaDir));

    if (run_command(command) != 0) {
        return std::unexpected(sappp::Error::make("CommandFailed", "sappp capture failed"));
    }
    return {};
}

[[nodiscard]] sappp::Result<void> run_sappp_analyze(const fs::path& snapshot,
                                                    const fs::path& output_dir,
                                                    const fs::path& repo_root,
                                                    int jobs)
{
    auto command = make_sappp_command("analyze");
    command.push_back("--build");
    command.push_back(snapshot.string());
    command.push_back("--out");
    command.push_back(output_dir.string());
    command.push_back("--jobs");
    command.push_back(std::to_string(jobs));
    command.push_back("--schema-dir");
    command.push_back(std::string(kSchemaDir));

    if (run_command_in_dir(repo_root, command) != 0) {
        return std::unexpected(sappp::Error::make("CommandFailed", "sappp analyze failed"));
    }
    return {};
}

[[nodiscard]] sappp::Result<void> run_sappp_validate(const fs::path& output_dir)
{
    auto command = make_sappp_command("validate");
    command.push_back("--input");
    command.push_back(output_dir.string());
    command.push_back("--schema-dir");
    command.push_back(std::string(kSchemaDir));

    if (run_command(command) != 0) {
        return std::unexpected(sappp::Error::make("CommandFailed", "sappp validate failed"));
    }
    return {};
}

[[nodiscard]] sappp::Result<void> run_sappp_pack(const fs::path& output_dir,
                                                 const fs::path& output_pack,
                                                 const fs::path& output_manifest)
{
    auto command = make_sappp_command("pack");
    command.push_back("--input");
    command.push_back(output_dir.string());
    command.push_back("--output");
    command.push_back(output_pack.string());
    command.push_back("--manifest");
    command.push_back(output_manifest.string());
    command.push_back("--schema-dir");
    command.push_back(std::string(kSchemaDir));

    if (run_command(command) != 0) {
        return std::unexpected(sappp::Error::make("CommandFailed", "sappp pack failed"));
    }
    return {};
}

[[nodiscard]] sappp::Result<void> copy_snapshot_to(const fs::path& snapshot,
                                                   const fs::path& output_dir)
{
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to create output dir: " + output_dir.string()));
    }
    fs::copy_file(snapshot,
                  output_dir / std::string(kBuildSnapshotName),
                  fs::copy_options::overwrite_existing,
                  ec);
    if (ec) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to copy snapshot: " + snapshot.string()));
    }
    return {};
}
#endif

}  // namespace

TEST(EndToEndDeterminism, Jobs1And8ProduceSameIdsAndDigest)
{
#if !defined(SAPPP_HAS_CLANG_FRONTEND)
    GTEST_SKIP() << "frontend_clang not built; skipping E2E determinism";
#else
    TempDir temp_dir("sappp_e2e_determinism");
    const fs::path repo_root = temp_dir.path() / "repo";
    fs::create_directories(repo_root);

    const fs::path source_path = write_source_file(repo_root);
    const fs::path compile_commands = write_compile_commands(repo_root, source_path);
    const fs::path snapshot_path = repo_root / std::string(kBuildSnapshotName);

    auto capture_result = run_sappp_capture(compile_commands, snapshot_path, repo_root);
    ASSERT_TRUE(capture_result.has_value()) << capture_result.error().message;

    const fs::path out_j1 = repo_root / "out_j1";
    const fs::path out_j8 = repo_root / "out_j8";
    const fs::path pack_j1 = repo_root / "pack_j1.tar.gz";
    const fs::path pack_j8 = repo_root / "pack_j8.tar.gz";
    const fs::path manifest_j1 = repo_root / "manifest_j1.json";
    const fs::path manifest_j8 = repo_root / "manifest_j8.json";

    auto analyze_j1 = run_sappp_analyze(snapshot_path, out_j1, repo_root, 1);
    ASSERT_TRUE(analyze_j1.has_value()) << analyze_j1.error().message;

    auto analyze_j8 = run_sappp_analyze(snapshot_path, out_j8, repo_root, 8);
    ASSERT_TRUE(analyze_j8.has_value()) << analyze_j8.error().message;

    auto copy_j1 = copy_snapshot_to(snapshot_path, out_j1);
    ASSERT_TRUE(copy_j1.has_value()) << copy_j1.error().message;

    auto copy_j8 = copy_snapshot_to(snapshot_path, out_j8);
    ASSERT_TRUE(copy_j8.has_value()) << copy_j8.error().message;

    auto validate_j1 = run_sappp_validate(out_j1);
    ASSERT_TRUE(validate_j1.has_value()) << validate_j1.error().message;

    auto validate_j8 = run_sappp_validate(out_j8);
    ASSERT_TRUE(validate_j8.has_value()) << validate_j8.error().message;

    auto pack_j1_result = run_sappp_pack(out_j1, pack_j1, manifest_j1);
    ASSERT_TRUE(pack_j1_result.has_value()) << pack_j1_result.error().message;

    auto pack_j8_result = run_sappp_pack(out_j8, pack_j8, manifest_j8);
    ASSERT_TRUE(pack_j8_result.has_value()) << pack_j8_result.error().message;

    auto outputs_j1 = collect_outputs(out_j1, manifest_j1);
    ASSERT_TRUE(outputs_j1.has_value()) << outputs_j1.error().message;

    auto outputs_j8 = collect_outputs(out_j8, manifest_j8);
    ASSERT_TRUE(outputs_j8.has_value()) << outputs_j8.error().message;

    EXPECT_EQ(outputs_j1->po_ids, outputs_j8->po_ids);
    EXPECT_EQ(outputs_j1->unknown_ids, outputs_j8->unknown_ids);
    EXPECT_EQ(outputs_j1->result_ids, outputs_j8->result_ids);
    EXPECT_EQ(outputs_j1->pack_manifest_digest, outputs_j8->pack_manifest_digest);
#endif
}

}  // namespace sappp::determinism::tests
