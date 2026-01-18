#include "frontend_clang/frontend.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::frontend_clang::test {

namespace {

std::string schema_path(const std::string& name)
{
    return std::string(SAPPP_SCHEMA_DIR) + "/" + name;
}

nlohmann::json make_build_snapshot(const std::string& cwd, const std::string& source_path)
{
    nlohmann::json compile_unit = {
        {         "tu_id",               sappp::common::sha256_prefixed("tu")                          },
        {           "cwd",                                           sappp::common::normalize_path(cwd)},
        {          "argv", nlohmann::json::array({SAPPP_CXX_COMPILER, "-std=c++23", "-c", source_path})},
        {     "env_delta",                                                     nlohmann::json::object()},
        {"response_files",                                                      nlohmann::json::array()},
        {          "lang",                                                                        "c++"},
        {           "std",                                                                      "c++23"},
        {        "target",
         {{"triple", "x86_64-unknown-linux-gnu"},
         {"abi", "sysv"},
         {"data_layout", {{"ptr_bits", 64}, {"long_bits", 64}, {"align", {{"max", 16}}}}}}             },
        {      "frontend",                                     {{"kind", "clang"}, {"version", "test"}}}
    };

    return nlohmann::json{
        {"schema_version",                       "build_snapshot.v1"},
        {          "tool", {{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                    "2024-01-01T00:00:00Z"},
        {          "host",     {{"os", "linux"}, {"arch", "x86_64"}}},
        { "compile_units",     nlohmann::json::array({compile_unit})}
    };
}

}  // namespace

TEST(FrontendClangTest, GeneratesValidNirAndSourceMap)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_test_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    std::ofstream source_file(source_path);
    ASSERT_TRUE(source_file.good());
    source_file << "int add(int a, int b) { return a + b; }\n";
    source_file << "int main() { return add(1, 2); }\n";
    source_file.close();

    nlohmann::json build_snapshot = make_build_snapshot(temp_dir.string(), source_path.string());

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

}  // namespace sappp::frontend_clang::test
