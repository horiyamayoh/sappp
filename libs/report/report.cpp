/**
 * @file report.cpp
 * @brief Reporting helpers for diff/explain outputs
 */

#include "sappp/report.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <vector>

namespace sappp::report {

namespace {

[[nodiscard]] std::string category_of(const nlohmann::json& result)
{
    if (!result.contains("category")) {
        return "UNKNOWN";
    }
    return result.at("category").get<std::string>();
}

[[nodiscard]] std::optional<std::string> certificate_root_of(const nlohmann::json& result)
{
    if (!result.contains("certificate_root")) {
        return std::nullopt;
    }
    return result.at("certificate_root").get<std::string>();
}

[[nodiscard]] nlohmann::json side_result_of(const nlohmann::json& result)
{
    nlohmann::json side = {
        {"category", category_of(result)}
    };
    if (auto cert = certificate_root_of(result)) {
        side["certificate_root"] = *cert;
    }
    return side;
}

[[nodiscard]] std::string classify_change(std::string_view before_category,
                                          std::string_view after_category,
                                          bool before_present,
                                          bool after_present)
{
    if (!before_present && after_present) {
        return "New";
    }
    if (before_present && !after_present) {
        return "Resolved";
    }
    if (before_category == after_category) {
        return "Unchanged";
    }
    if (before_category == "SAFE" && after_category != "SAFE") {
        return "Regressed";
    }
    if (before_category == "BUG" && after_category == "UNKNOWN") {
        return "Regressed";
    }
    if (before_category == "UNKNOWN" && after_category != "UNKNOWN") {
        return "Resolved";
    }
    return "Reclassified";
}

[[nodiscard]] std::map<std::string, nlohmann::json>
index_results_by_po_id(const nlohmann::json& results)
{
    std::map<std::string, nlohmann::json> index;
    if (!results.contains("results")) {
        return index;
    }
    for (const auto& item : results.at("results")) {
        if (!item.contains("po_id")) {
            continue;
        }
        index.emplace(item.at("po_id").get<std::string>(), item);
    }
    return index;
}

}  // namespace

sappp::Result<nlohmann::json> build_diff_changes(const nlohmann::json& before_results,
                                                 const nlohmann::json& after_results,
                                                 std::string_view reason)
{
    auto before_index = index_results_by_po_id(before_results);
    auto after_index = index_results_by_po_id(after_results);

    std::set<std::string> po_ids;
    for (const auto& [po_id, _] : before_index) {
        po_ids.insert(po_id);
    }
    for (const auto& [po_id, _] : after_index) {
        po_ids.insert(po_id);
    }

    std::vector<nlohmann::json> changes;
    changes.reserve(po_ids.size());

    for (const auto& po_id : po_ids) {
        const bool before_present = before_index.contains(po_id);
        const bool after_present = after_index.contains(po_id);

        nlohmann::json from = {
            {"category", "UNKNOWN"}
        };
        nlohmann::json to = {
            {"category", "UNKNOWN"}
        };
        std::string_view before_category = "UNKNOWN";
        std::string_view after_category = "UNKNOWN";

        if (before_present) {
            const auto& item = before_index.at(po_id);
            from = side_result_of(item);
            before_category = category_of(item);
        }
        if (after_present) {
            const auto& item = after_index.at(po_id);
            to = side_result_of(item);
            after_category = category_of(item);
        }

        nlohmann::json change = {
            {"po_id", po_id},
            {"from", from},
            {"to", to},
            {"change_kind",
             classify_change(before_category, after_category, before_present, after_present)}
        };
        if (!reason.empty()) {
            change["reason"] = std::string(reason);
        }
        changes.push_back(std::move(change));
    }

    std::ranges::stable_sort(changes, [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
        return lhs.at("po_id").get<std::string>() < rhs.at("po_id").get<std::string>();
    });

    return nlohmann::json(changes);
}

// NOLINTBEGIN(readability-function-size) - Keeps filtering logic co-located.
sappp::Result<nlohmann::json>
filter_unknowns(const nlohmann::json& unknown_ledger,
                const std::optional<nlohmann::json>& validated_results,
                std::optional<std::string_view> po_id,
                std::optional<std::string_view> unknown_id)
{
    std::set<std::string> allowed_po_ids;
    if (validated_results) {
        if (validated_results->contains("results")) {
            for (const auto& result : validated_results->at("results")) {
                if (!result.contains("category") || !result.contains("po_id")) {
                    continue;
                }
                if (result.at("category").get_ref<const std::string&>() != "UNKNOWN") {
                    continue;
                }
                allowed_po_ids.insert(result.at("po_id").get<std::string>());
            }
        }
    }

    std::vector<nlohmann::json> filtered;
    if (!unknown_ledger.contains("unknowns")) {
        return nlohmann::json(filtered);
    }

    for (const auto& entry : unknown_ledger.at("unknowns")) {
        if (po_id && entry.contains("po_id")
            && entry.at("po_id").get_ref<const std::string&>() != *po_id) {
            continue;
        }
        if (unknown_id && entry.contains("unknown_stable_id")
            && entry.at("unknown_stable_id").get_ref<const std::string&>() != *unknown_id) {
            continue;
        }
        if (!allowed_po_ids.empty()) {
            if (!entry.contains("po_id")) {
                continue;
            }
            if (!allowed_po_ids.contains(entry.at("po_id").get<std::string>())) {
                continue;
            }
        }
        filtered.push_back(entry);
    }

    std::ranges::stable_sort(filtered, [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
        auto lhs_id = lhs.value("unknown_stable_id", "");
        auto rhs_id = rhs.value("unknown_stable_id", "");
        if (lhs_id != rhs_id) {
            return lhs_id < rhs_id;
        }
        return lhs.value("po_id", "") < rhs.value("po_id", "");
    });

    return nlohmann::json(filtered);
}
// NOLINTEND(readability-function-size)

}  // namespace sappp::report
