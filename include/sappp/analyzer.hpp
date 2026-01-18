#pragma once

/**
 * @file analyzer.hpp
 * @brief Analyzer v0: produce certificate candidates and UNKNOWN ledger
 */

#include "sappp/common.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace sappp::analyzer {

struct AnalyzerOutput {
    nlohmann::json unknown_ledger;
    std::vector<std::pair<std::string, std::string>> cert_index;
};

class Analyzer {
public:
    explicit Analyzer(std::string schema_dir = "schemas");

    [[nodiscard]] sappp::Result<AnalyzerOutput> analyze(const nlohmann::json& po_list,
                                                        const std::string& output_dir) const;

private:
    std::string m_schema_dir;
};

} // namespace sappp::analyzer
