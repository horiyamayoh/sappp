#pragma once

/**
 * @file build_capture.hpp
 * @brief Build capture from compile_commands.json
 */

#include <nlohmann/json.hpp>
#include <string>

namespace sappp::build_capture {

class BuildSnapshot {
public:
    explicit BuildSnapshot(nlohmann::json json);

    const nlohmann::json& json() const;
    nlohmann::json& json();

private:
    nlohmann::json m_json;
};

class BuildCapture {
public:
    explicit BuildCapture(std::string repo_root = {}, std::string schema_dir = "schemas");

    BuildSnapshot capture(const std::string& compile_commands_path);

private:
    std::string m_repo_root;
    std::string m_schema_dir;
};

} // namespace sappp::build_capture
