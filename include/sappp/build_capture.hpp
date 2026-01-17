#pragma once

/**
 * @file build_capture.hpp
 * @brief Build capture from compile_commands.json to build_snapshot.v1
 */

#include <nlohmann/json.hpp>
#include <string>

namespace sappp {

using BuildSnapshot = nlohmann::json;

class BuildCapture {
public:
    BuildCapture() = default;
    explicit BuildCapture(std::string repo_root);

    BuildSnapshot capture(const std::string& compile_commands_path) const;

private:
    std::string repo_root_;
};

} // namespace sappp
