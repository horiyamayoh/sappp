/**
 * @file test_build_capture.cpp
 * @brief Tests for build capture from compile_commands.json
 */

#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"

#include <filesystem>
#include <fstream>
#include <ranges>

#include <gtest/gtest.h>

namespace sappp::build_capture::tests {

namespace {

std::filesystem::path write_compile_commands(const std::filesystem::path& repo_root)
{
    std::filesystem::path build_dir = repo_root / "build";
    std::filesystem::path src_dir = repo_root / "src";
    std::filesystem::create_directories(build_dir);
    std::filesystem::create_directories(src_dir);

    nlohmann::json compile_db = nlohmann::json::array();
    compile_db.push_back({
        {"directory",                                         build_dir.string()},
        {     "file",                              (src_dir / "main.c").string()},
        {"arguments", {"clang", "-std=c11", "-c", (src_dir / "main.c").string()}}
    });
    compile_db.push_back({
        {"directory",                            build_dir.string()                     },
        {     "file",                                     (src_dir / "app.cpp").string()},
        {  "command",
         std::string("clang++ -std=c++20 -c \"") + (src_dir / "app.cpp").string() + "\""}
    });

    std::filesystem::path compile_commands = build_dir / "compile_commands.json";
    std::ofstream out(compile_commands);
    out << compile_db.dump(2);
    return compile_commands;
}

}  // namespace

TEST(BuildCaptureTest, GeneratesSnapshotFromCompileCommands)
{
    std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "sappp_build_capture_test";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    std::filesystem::path repo_root = temp_root / "repo";
    std::filesystem::create_directories(repo_root);
    std::filesystem::path compile_commands = write_compile_commands(repo_root);

    BuildCapture capture(repo_root.string(), SAPPP_SCHEMA_DIR);
    auto snapshot_result = capture.capture(compile_commands.string());
    ASSERT_TRUE(snapshot_result);
    BuildSnapshot snapshot = std::move(*snapshot_result);

    const nlohmann::json& json = snapshot.json();
    ASSERT_EQ(json.at("schema_version"), "build_snapshot.v1");
    ASSERT_EQ(json.at("compile_units").size(), 2U);

    const auto& units = json.at("compile_units");

    // Units are sorted by tu_id (hash), not input order.
    // Find each unit by language to verify properties.
    const nlohmann::json* c_unit = nullptr;
    const nlohmann::json* cpp_unit = nullptr;
    for (const auto& unit : units) {
        if (unit.at("lang") == "c") {
            c_unit = &unit;
        } else if (unit.at("lang") == "c++") {
            cpp_unit = &unit;
        }
    }
    ASSERT_NE(c_unit, nullptr) << "C unit not found";
    ASSERT_NE(cpp_unit, nullptr) << "C++ unit not found";

    EXPECT_EQ(c_unit->at("cwd"), "build");
    EXPECT_EQ(c_unit->at("std"), "c11");
    EXPECT_EQ(cpp_unit->at("cwd"), "build");
    EXPECT_EQ(cpp_unit->at("std"), "c++20");

    for (const auto& unit : units) {
        nlohmann::json hash_input = {
            {           "cwd",            unit.at("cwd")},
            {          "argv",           unit.at("argv")},
            {     "env_delta",      unit.at("env_delta")},
            {"response_files", unit.at("response_files")},
            {          "lang",           unit.at("lang")},
            {           "std",            unit.at("std")},
            {        "target",         unit.at("target")}
        };
        auto expected = canonical::hash_canonical(hash_input);
        ASSERT_TRUE(expected);
        EXPECT_EQ(unit.at("tu_id"), *expected);
    }

    for (auto [i, unit] : std::views::enumerate(units)) {
        if (i == 0) {
            continue;
        }
        const auto idx = static_cast<std::size_t>(i);
        EXPECT_LE(units.at(idx - 1).at("tu_id").get<std::string>(),
                  unit.at("tu_id").get<std::string>());
    }

    auto schema_result =
        common::validate_json(json,
                              std::string(SAPPP_SCHEMA_DIR) + "/build_snapshot.v1.schema.json");
    EXPECT_TRUE(schema_result) << (schema_result ? "" : schema_result.error().message);
}

}  // namespace sappp::build_capture::tests
