#pragma once

/**
 * @file analyzer.hpp
 * @brief Analyzer v0 stub implementation
 */

#include "sappp/common.hpp"
#include "sappp/specdb.hpp"
#include "sappp/version.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::analyzer {

struct AnalyzerConfig
{
    std::string schema_dir;
    std::string certstore_dir;
    sappp::VersionTriple versions;
    sappp::specdb::VersionScopeContext contract_scope;
};

struct AnalyzeOutput
{
    nlohmann::json unknown_ledger;
};

class Analyzer
{
public:
    explicit Analyzer(AnalyzerConfig config);

    [[nodiscard]] sappp::Result<AnalyzeOutput> analyze(const nlohmann::json& nir_json,
                                                       const nlohmann::json& po_list_json,
                                                       const nlohmann::json* specdb_snapshot) const;

private:
    AnalyzerConfig m_config;
};

}  // namespace sappp::analyzer
