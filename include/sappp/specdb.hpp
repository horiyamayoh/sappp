#pragma once

/**
 * @file specdb.hpp
 * @brief SpecDB normalization and snapshot builder
 */

#include "sappp/common.hpp"

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace sappp::specdb {

struct BuildOptions
{
    nlohmann::json build_snapshot;
    std::filesystem::path spec_path;
    std::filesystem::path schema_dir;
    std::string generated_at;
    nlohmann::json tool;
};

[[nodiscard]] sappp::Result<nlohmann::json>
normalize_contract_ir(const nlohmann::json& input, const std::filesystem::path& schema_dir);

[[nodiscard]] sappp::Result<nlohmann::json> build_snapshot(const BuildOptions& options);

}  // namespace sappp::specdb
