#include "frontend_clang/frontend.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_set>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::frontend_clang::test {

namespace {

std::string schema_path(const std::string& name)
{
    return std::string(SAPPP_SCHEMA_DIR) + "/" + name;
}

struct BuildSnapshotInput
{
    std::string_view cwd;
    std::string_view source_path;
};

nlohmann::json make_build_snapshot(const BuildSnapshotInput& input)
{
    nlohmann::json compile_unit = {
        {         "tu_id",   sappp::common::sha256_prefixed("tu")                          },
        {           "cwd",                         sappp::common::normalize_path(input.cwd)},
        {          "argv",
         nlohmann::json::array(
         {SAPPP_CXX_COMPILER, "-std=c++23", "-c", std::string(input.source_path)})         },
        {     "env_delta",                                         nlohmann::json::object()},
        {"response_files",                                          nlohmann::json::array()},
        {          "lang",                                                            "c++"},
        {           "std",                                                          "c++23"},
        {        "target",
         {{"triple", "x86_64-unknown-linux-gnu"},
         {"abi", "sysv"},
         {"data_layout", {{"ptr_bits", 64}, {"long_bits", 64}, {"align", {{"max", 16}}}}}} },
        {      "frontend",                         {{"kind", "clang"}, {"version", "test"}}}
    };

    return nlohmann::json{
        {"schema_version",                       "build_snapshot.v1"},
        {          "tool", {{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                    "2024-01-01T00:00:00Z"},
        {          "host",     {{"os", "linux"}, {"arch", "x86_64"}}},
        { "compile_units",     nlohmann::json::array({compile_unit})}
    };
}

std::string sink_marker_source()
{
    return "int main() {\n"
           "  int a = 1;\n"
           "  int b = 0;\n"
           "  int c = a / b;\n"
           "  int* p = nullptr;\n"
           "  int d = *p;\n"
           "  int arr[1] = {0};\n"
           "  int e = arr[1];\n"
           "  return c + d + e;\n"
           "}\n";
}

std::string lifetime_source()
{
    return "#include <utility>\n"
           "struct Widget {\n"
           "  Widget() {}\n"
           "  Widget(Widget&&) {}\n"
           "  ~Widget() {}\n"
           "};\n"
           "int main() {\n"
           "  Widget w;\n"
           "  Widget w2;\n"
           "  Widget w3(std::move(w2));\n"
           "  const Widget& w4 = Widget{};\n"
           "  (void)w4;\n"
           "  return 0;\n"
           "}\n";
}

void write_source_file(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream source_file(path);
    ASSERT_TRUE(source_file.good());
    source_file << contents;
}

std::optional<std::string> sink_kind_from_inst(const nlohmann::json& inst)
{
    if (inst.at("op").get<std::string>() != "sink.marker") {
        return std::nullopt;
    }
    if (!inst.contains("args") || !inst.at("args").is_array() || inst.at("args").empty()
        || !inst.at("args").at(0).is_string()) {
        return std::nullopt;
    }
    return inst.at("args").at(0).get<std::string>();
}

void collect_sink_kinds_from_block(const nlohmann::json& block,
                                   std::unordered_set<std::string>& sink_kinds)
{
    for (const auto& inst : block.at("insts")) {
        auto sink_kind = sink_kind_from_inst(inst);
        if (sink_kind.has_value()) {
            sink_kinds.insert(*sink_kind);
        }
    }
}

void collect_sink_kinds_from_function(const nlohmann::json& func,
                                      std::unordered_set<std::string>& sink_kinds)
{
    for (const auto& block : func.at("cfg").at("blocks")) {
        collect_sink_kinds_from_block(block, sink_kinds);
    }
}

std::unordered_set<std::string> collect_sink_kinds(const nlohmann::json& nir)
{
    std::unordered_set<std::string> sink_kinds;
    for (const auto& func : nir.at("functions")) {
        collect_sink_kinds_from_function(func, sink_kinds);
    }
    return sink_kinds;
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

}  // namespace

TEST(FrontendClangTest, GeneratesValidNirAndSourceMap)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_test_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path,
                      "int add(int a, int b) { return a + b; }\n"
                      "int main() { return add(1, 2); }\n");

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    auto nir_validation =
        sappp::common::validate_json(result->nir, schema_path("nir.v1.schema.json"));
    EXPECT_TRUE(nir_validation) << (nir_validation ? "" : nir_validation.error().message);

    auto source_validation =
        sappp::common::validate_json(result->source_map, schema_path("source_map.v1.schema.json"));
    EXPECT_TRUE(source_validation) << (source_validation ? "" : source_validation.error().message);

    EXPECT_TRUE(result->nir.contains("functions"));
    EXPECT_FALSE(result->nir.at("functions").empty());

    EXPECT_TRUE(result->source_map.contains("entries"));
    EXPECT_FALSE(result->source_map.at("entries").empty());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsSinkMarkersForPotentialUb)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_sink_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, sink_marker_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> sink_kinds = collect_sink_kinds(result->nir);

    EXPECT_NE(sink_kinds.find("div0"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("null"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("oob"), sink_kinds.end());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsLifetimeAndCtorEvents)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_lifetime_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, lifetime_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> ops = collect_ops(result->nir);

    EXPECT_NE(ops.find("lifetime.begin"), ops.end());
    EXPECT_NE(ops.find("lifetime.end"), ops.end());
    EXPECT_NE(ops.find("ctor"), ops.end());
    EXPECT_NE(ops.find("dtor"), ops.end());
    EXPECT_NE(ops.find("move"), ops.end());

    std::filesystem::remove_all(temp_dir);
}

}  // namespace sappp::frontend_clang::test
