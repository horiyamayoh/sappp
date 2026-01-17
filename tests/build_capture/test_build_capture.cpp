/**
 * @file test_build_capture.cpp
 * @brief Tests for build capture from compile_commands.json
 */

#include "sappp/build_capture.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace sappp::build_capture::tests {

namespace {

std::filesystem::path write_compile_commands(const std::filesystem::path& repo_root) {
    std::filesystem::path build_dir = repo_root / "build";
    std::filesystem::path src_dir = repo_root / "src";
    std::filesystem::create_directories(build_dir);
    std::filesystem::create_directories(src_dir);

    nlohmann::json compile_db = nlohmann::json::array();
    compile_db.push_back({
        {"directory", build_dir.string()},
        {"file", (src_dir / "main.c").string()},
        {"arguments", {"clang", "-std=c11", "-c", (src_dir / "main.c").string()}}
    });
    compile_db.push_back({
        {"directory", build_dir.string()},
        {"file", (src_dir / "app.cpp").string()},
        {"command", std::string("clang++ -std=c++20 -c \"") + (src_dir / "app.cpp").string() + "\""}
    });

    std::filesystem::path compile_commands = build_dir / "compile_commands.json";
    std::ofstream out(compile_commands);
    out << compile_db.dump(2);
    return compile_commands;
}

} // namespace

TEST(BuildCaptureTest, GeneratesSnapshotFromCompileCommands) {
    std::filesystem::path temp_root = std::filesystem::temp_directory_path() / "sappp_build_capture_test";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    std::filesystem::path repo_root = temp_root / "repo";
    std::filesystem::create_directories(repo_root);
    std::filesystem::path compile_commands = write_compile_commands(repo_root);

    BuildCapture capture(repo_root.string(), SAPPP_SCHEMA_DIR);
    BuildSnapshot snapshot = capture.capture(compile_commands.string());

    const nlohmann::json& json = snapshot.json();
    ASSERT_EQ(json.at("schema_version"), "build_snapshot.v1");
    ASSERT_EQ(json.at("compile_units").size(), 2U);

    const auto& units = json.at("compile_units");
    EXPECT_EQ(units.at(0).at("cwd"), "build");
    EXPECT_EQ(units.at(0).at("lang"), "c");
    EXPECT_EQ(units.at(0).at("std"), "c11");
    EXPECT_EQ(units.at(1).at("lang"), "c++");
    EXPECT_EQ(units.at(1).at("std"), "c++20");

    for (const auto& unit : units) {
        nlohmann::json hash_input = {
            {"cwd", unit.at("cwd")},
            {"argv", unit.at("argv")},
            {"env_delta", unit.at("env_delta")},
            {"response_files", unit.at("response_files")},
            {"lang", unit.at("lang")},
            {"std", unit.at("std")},
            {"target", unit.at("target")}
        };
        std::string expected = canonical::hash_canonical(hash_input);
        EXPECT_EQ(unit.at("tu_id"), expected);
    }

    for (size_t i = 1; i < units.size(); ++i) {
        EXPECT_LE(units.at(i - 1).at("tu_id").get<std::string>(),
                  units.at(i).at("tu_id").get<std::string>());
    }

    std::string schema_error;
    EXPECT_TRUE(common::validate_json(json, std::string(SAPPP_SCHEMA_DIR) + "/build_snapshot.v1.schema.json",
                                      schema_error))
        << schema_error;
}

} // namespace sappp::build_capture::tests
