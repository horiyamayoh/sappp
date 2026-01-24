#pragma once

/**
 * @file specdb.hpp
 * @brief SpecDB normalization and snapshot builder
 */

#include "sappp/common.hpp"

#include <filesystem>
#include <string>
#include <vector>

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

struct VersionScopeContext
{
    std::string abi;
    std::string library_version;
    std::vector<std::string> conditions;
};

struct VersionScopeRank
{
    int abi_rank = 0;
    int library_version_rank = 0;
    int conditions_rank = 0;
    int priority = 0;
};

struct VersionScopeMatch
{
    bool matches = false;
    VersionScopeRank rank{};
};

[[nodiscard]] VersionScopeContext normalize_scope_context(VersionScopeContext context);

[[nodiscard]] sappp::Result<VersionScopeMatch>
evaluate_version_scope(const nlohmann::json& version_scope, const VersionScopeContext& context);

[[nodiscard]] sappp::Result<nlohmann::json>
normalize_contract_ir(const nlohmann::json& input, const std::filesystem::path& schema_dir);

[[nodiscard]] sappp::Result<nlohmann::json> build_snapshot(const BuildOptions& options);

}  // namespace sappp::specdb
