#pragma once

/**
 * @file report.hpp
 * @brief Reporting helpers for diff/explain outputs
 */

#include "sappp/common.hpp"

#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sappp::report {

[[nodiscard]] sappp::Result<nlohmann::json> build_diff_changes(const nlohmann::json& before_results,
                                                               const nlohmann::json& after_results,
                                                               std::string_view reason);

[[nodiscard]] sappp::Result<nlohmann::json>
filter_unknowns(const nlohmann::json& unknown_ledger,
                const std::optional<nlohmann::json>& validated_results,
                std::optional<std::string_view> po_id,
                std::optional<std::string_view> unknown_id);

}  // namespace sappp::report
