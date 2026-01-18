/**
 * @file test_po_determinism.cpp
 * @brief PO generation determinism tests (AGENTS.md Section 6.3)
 *
 * Tests that PO generation produces identical results regardless of
 * parallelization level (--jobs=1 vs --jobs=N).
 */

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace sappp::determinism {
namespace {

/**
 * @brief Simulates PO generation from multiple translation units
 *
 * This test verifies that when POs are generated from multiple sources
 * and merged, the result is deterministic regardless of processing order.
 */
class PoMergeDeterminismTest : public ::testing::Test
{
protected:
    // Simulate POs from different translation units (could arrive in any order)
    std::vector<json> m_po_batches = {
        json::array(
            {{{"po_id", "sha256:aaaa"}, {"po_kind", "UB.DivZero"}, {"function_uid", "func_c"}},
                     {{"po_id", "sha256:bbbb"}, {"po_kind", "UB.NullDeref"}, {"function_uid", "func_a"}}}
            ),
        json::array(
            {{{"po_id", "sha256:cccc"}, {"po_kind", "UB.OutOfBounds"}, {"function_uid", "func_b"}},
                     {{"po_id", "sha256:dddd"}, {"po_kind", "UB.DivZero"}, {"function_uid", "func_d"}}}
            ),
        json::array(
            {{{"po_id", "sha256:eeee"}, {"po_kind", "UB.NullDeref"}, {"function_uid", "func_a"}}}
            )
    };

    [[nodiscard]] json merge_and_sort(const std::vector<json>& batches) const
    {
        std::vector<json> all_pos;
        for (const auto& batch : batches) {
            for (const auto& po : batch) {
                all_pos.push_back(po);
            }
        }

        // Stable sort by po_id (as per AGENTS.md Section 4.1)
        std::ranges::stable_sort(all_pos, [](const json& a, const json& b) {
            return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
        });

        return json{
            {"pos", all_pos}
        };
    }
};

TEST_F(PoMergeDeterminismTest, MergeOrderDoesNotAffectResult)
{
    // Simulate different arrival orders (as would happen with different --jobs)
    std::vector<json> order1 = {m_po_batches[0], m_po_batches[1], m_po_batches[2]};
    std::vector<json> order2 = {m_po_batches[2], m_po_batches[0], m_po_batches[1]};
    std::vector<json> order3 = {m_po_batches[1], m_po_batches[2], m_po_batches[0]};

    auto result1 = merge_and_sort(order1);
    auto result2 = merge_and_sort(order2);
    auto result3 = merge_and_sort(order3);

    // All results must be identical
    EXPECT_EQ(result1, result2);
    EXPECT_EQ(result2, result3);

    // Verify canonical JSON produces identical hashes
    auto hash1 = canonical::hash_canonical(result1);
    auto hash2 = canonical::hash_canonical(result2);
    auto hash3 = canonical::hash_canonical(result3);

    ASSERT_TRUE(hash1.has_value());
    ASSERT_TRUE(hash2.has_value());
    ASSERT_TRUE(hash3.has_value());

    EXPECT_EQ(*hash1, *hash2);
    EXPECT_EQ(*hash2, *hash3);
}

TEST_F(PoMergeDeterminismTest, PoIdOrderIsStable)
{
    auto result = merge_and_sort(m_po_batches);
    const auto& pos = result.at("pos");

    // Verify po_ids are in ascending order
    std::string prev_id;
    for (const auto& po : pos) {
        std::string current_id = po.at("po_id").get<std::string>();
        EXPECT_LT(prev_id, current_id) << "POs must be sorted by po_id";
        prev_id = current_id;
    }
}

/**
 * @brief Tests UNKNOWN ledger determinism
 */
class UnknownLedgerDeterminismTest : public ::testing::Test
{
protected:
    std::vector<json> m_unknown_batches = {
        json::array({{{"unknown_stable_id", "unk:0003"}, {"unknown_code", "UnsupportedFeature"}},
                     {{"unknown_stable_id", "unk:0001"}, {"unknown_code", "LoopBound"}}}
        ),
        json::array({{{"unknown_stable_id", "unk:0002"}, {"unknown_code", "ExternalCall"}}}
        )
    };

    [[nodiscard]] json merge_and_sort(const std::vector<json>& batches) const
    {
        std::vector<json> all_unknowns;
        for (const auto& batch : batches) {
            for (const auto& unk : batch) {
                all_unknowns.push_back(unk);
            }
        }

        // Stable sort by unknown_stable_id (as per AGENTS.md Section 4.1)
        std::ranges::stable_sort(all_unknowns, [](const json& a, const json& b) {
            return a.at("unknown_stable_id").get<std::string>()
                   < b.at("unknown_stable_id").get<std::string>();
        });

        return json{
            {"unknowns", all_unknowns}
        };
    }
};

TEST_F(UnknownLedgerDeterminismTest, MergeOrderDoesNotAffectResult)
{
    std::vector<json> order1 = {m_unknown_batches[0], m_unknown_batches[1]};
    std::vector<json> order2 = {m_unknown_batches[1], m_unknown_batches[0]};

    auto result1 = merge_and_sort(order1);
    auto result2 = merge_and_sort(order2);

    EXPECT_EQ(result1, result2);

    auto hash1 = canonical::hash_canonical(result1);
    auto hash2 = canonical::hash_canonical(result2);

    ASSERT_TRUE(hash1.has_value());
    ASSERT_TRUE(hash2.has_value());
    EXPECT_EQ(*hash1, *hash2);
}

/**
 * @brief Tests validated_results determinism
 */
class ValidatedResultsDeterminismTest : public ::testing::Test
{
protected:
    std::vector<json> m_result_batches = {
        json::array({{{"po_id", "sha256:zzzz"}, {"category", "BUG"}},
                     {{"po_id", "sha256:aaaa"}, {"category", "SAFE"}}}
        ),
        json::array({{{"po_id", "sha256:mmmm"}, {"category", "UNKNOWN"}}}
        )
    };

    [[nodiscard]] json merge_and_sort(const std::vector<json>& batches) const
    {
        std::vector<json> all_results;
        for (const auto& batch : batches) {
            for (const auto& r : batch) {
                all_results.push_back(r);
            }
        }

        // Stable sort by po_id (as per AGENTS.md Section 4.1)
        std::ranges::stable_sort(all_results, [](const json& a, const json& b) {
            return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
        });

        return json{
            {"results", all_results}
        };
    }
};

TEST_F(ValidatedResultsDeterminismTest, MergeOrderDoesNotAffectResult)
{
    std::vector<json> order1 = {m_result_batches[0], m_result_batches[1]};
    std::vector<json> order2 = {m_result_batches[1], m_result_batches[0]};

    auto result1 = merge_and_sort(order1);
    auto result2 = merge_and_sort(order2);

    EXPECT_EQ(result1, result2);

    // Verify the order is by po_id
    const auto& results = result1.at("results");
    EXPECT_EQ(results[0].at("po_id"), "sha256:aaaa");
    EXPECT_EQ(results[1].at("po_id"), "sha256:mmmm");
    EXPECT_EQ(results[2].at("po_id"), "sha256:zzzz");
}

/**
 * @brief Tests that repeated operations produce identical results
 */
TEST(DeterminismRepeatTest, RepeatedCanonicalHashIsIdentical)
{
    json complex_data = {
        { "version",                 "1.0.0"                    },
        {     "pos",
         json::array({{{"id", "c"}, {"value", 3}},
         {{"id", "a"}, {"value", 1}},
         {{"id", "b"}, {"value", 2}}})                          },
        {"metadata", {{"z_field", "last"}, {"a_field", "first"}}}
    };

    // Run 100 times to catch any non-determinism
    std::string first_hash;
    for (int i = 0; i < 100; ++i) {
        auto hash = canonical::hash_canonical(complex_data);
        ASSERT_TRUE(hash.has_value()) << "Iteration " << i;

        if (i == 0) {
            first_hash = *hash;
        } else {
            EXPECT_EQ(*hash, first_hash) << "Hash differs at iteration " << i;
        }
    }
}

}  // namespace
}  // namespace sappp::determinism
