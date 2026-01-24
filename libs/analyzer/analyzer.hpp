#pragma once

/**
 * @file analyzer.hpp
 * @brief Analyzer v0 stub implementation
 */

#include "sappp/common.hpp"
#include "sappp/version.hpp"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sappp::analyzer {

struct AnalyzerConfig
{
    std::string schema_dir;
    std::string certstore_dir;
    sappp::VersionTriple versions;
};

struct AnalyzeOutput
{
    nlohmann::json unknown_ledger;
};

struct ContractMatchContext
{
    std::string abi{};
    std::string library_version{};
    std::vector<std::string> conditions{};
};

class Analyzer
{
public:
    explicit Analyzer(AnalyzerConfig config);

    [[nodiscard]] sappp::Result<AnalyzeOutput>
    analyze(const nlohmann::json& nir_json,
            const nlohmann::json& po_list_json,
            const nlohmann::json* specdb_snapshot,
            const ContractMatchContext& match_context = ContractMatchContext{}) const;

private:
    AnalyzerConfig m_config;
};

}  // namespace sappp::analyzer
