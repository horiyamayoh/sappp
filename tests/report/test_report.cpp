/**
 * @file test_report.cpp
 * @brief Tests for report helpers (diff/explain)
 */

#include "sappp/report.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ReportDiff, ClassifiesChangesAndSorts)
{
    nlohmann::json before = {
        {"results",
         nlohmann::json::array(
             {{{"po_id", "sha256:bbbb"}, {"category", "SAFE"}, {"certificate_root", "sha256:aaaa"}},
              {{"po_id", "sha256:cccc"}, {"category", "UNKNOWN"}}})}
    };
    nlohmann::json after = {
        {"results",
         nlohmann::json::array(
             {{{"po_id", "sha256:bbbb"}, {"category", "BUG"}, {"certificate_root", "sha256:dddd"}},
              {{"po_id", "sha256:aaaa"}, {"category", "SAFE"}}})}
    };

    auto changes = sappp::report::build_diff_changes(before, after, "SemanticsUpdated");
    ASSERT_TRUE(changes.has_value());
    ASSERT_TRUE(changes->is_array());
    ASSERT_EQ(changes->size(), 3U);

    EXPECT_EQ(changes->at(0).at("po_id"), "sha256:aaaa");
    EXPECT_EQ(changes->at(0).at("change_kind"), "New");
    EXPECT_EQ(changes->at(1).at("po_id"), "sha256:bbbb");
    EXPECT_EQ(changes->at(1).at("change_kind"), "Regressed");
    EXPECT_EQ(changes->at(2).at("po_id"), "sha256:cccc");
    EXPECT_EQ(changes->at(2).at("change_kind"), "Resolved");
}

TEST(ReportExplain, FiltersUnknowns)
{
    nlohmann::json unknown_ledger = {
        {"unknowns",
         nlohmann::json::array(
             {{{"unknown_stable_id", "sha256:1111"},
               {"po_id", "sha256:po1"},
               {"unknown_code", "BudgetExceeded"},
               {"missing_lemma",
                {{"expr", {{"op", "true"}}},
                 {"pretty", "true"},
                 {"symbols", nlohmann::json::array()}}},
               {"refinement_plan", {{"message", "none"}, {"actions", nlohmann::json::array()}}}},
              {{"unknown_stable_id", "sha256:2222"},
               {"po_id", "sha256:po2"},
               {"unknown_code", "Unsupported"},
               {"missing_lemma",
                {{"expr", {{"op", "true"}}},
                 {"pretty", "true"},
                 {"symbols", nlohmann::json::array()}}},
               {"refinement_plan",
                {{"message", "none"}, {"actions", nlohmann::json::array()}}}}})}
    };

    nlohmann::json validated = {
        {"results",
         nlohmann::json::array({{{"po_id", "sha256:po1"}, {"category", "UNKNOWN"}},
                                {{"po_id", "sha256:po2"}, {"category", "BUG"}}})}
    };

    auto filtered =
        sappp::report::filter_unknowns(unknown_ledger, validated, std::nullopt, std::nullopt);
    ASSERT_TRUE(filtered.has_value());
    ASSERT_TRUE(filtered->is_array());
    ASSERT_EQ(filtered->size(), 1U);
    EXPECT_EQ(filtered->at(0).at("po_id"), "sha256:po1");
}

}  // namespace
