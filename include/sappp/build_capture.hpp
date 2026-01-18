#pragma once

/**
 * @file build_capture.hpp
 * @brief Build capture from compile_commands.json
 *
 * C++23 modernization:
 * - Using [[nodiscard]] consistently
 */

#include "sappp/common.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::build_capture {

class BuildSnapshot
{
public:
    explicit BuildSnapshot(nlohmann::json json);

    /**
     * @brief Access the JSON data (const version)
     * @return Const reference to the JSON data
     */
    [[nodiscard]] const nlohmann::json& json() const noexcept { return m_json; }

    /**
     * @brief Access the JSON data (mutable version)
     * @return Mutable reference to the JSON data
     */
    [[nodiscard]] nlohmann::json& json() noexcept { return m_json; }

private:
    nlohmann::json m_json;
};

class BuildCapture
{
public:
    explicit BuildCapture(std::string repo_root = {}, std::string schema_dir = "schemas");

    [[nodiscard]] sappp::Result<BuildSnapshot> capture(const std::string& compile_commands_path);

private:
    std::string m_repo_root;
    std::string m_schema_dir;
};

}  // namespace sappp::build_capture
