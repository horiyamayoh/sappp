/**
 * @file test_explain.cpp
 * @brief Tests for explain UNKNOWN report generation
 */

#include "sappp/report/explain.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace sappp::report::tests {

namespace {

constexpr std::string_view kShaPrefix = "sha256:";

std::string make_sha256(char fill)
{
    return std::string(kShaPrefix) + std::string(64, fill);
}

std::filesystem::path write_json(const std::filesystem::path& path, const nlohmann::json& payload)
{
    std::ofstream out(path);
    out << payload.dump(2);
    return path;
}

nlohmann::json make_unknown_ledger()
{
    nlohmann::json unknown = {
        {      "schema_version",                    "unknown.v1"                                },
        {                "tool", {{"name", "sappp"}, {"version", "test"}, {"build_id", "local"}}},
        {        "generated_at",                                          "2024-01-01T00:00:00Z"},
        {               "tu_id",                                                make_sha256('a')},
        {   "semantics_version",                                                        "sem.v1"},
        {"proof_system_version",                                                      "proof.v1"},
        {     "profile_version",                                                    "profile.v1"},
        {            "unknowns",
         nlohmann::json::array({nlohmann::json{
         {"unknown_stable_id", make_sha256('b')},
         {"po_id", make_sha256('c')},
         {"unknown_code", "MissingLemma"},
         {"missing_lemma",
         {{"expr", {{"op", ">"}, {"args", nlohmann::json::array()}}},
         {"pretty", "x > 0"},
         {"symbols", nlohmann::json::array({"x"})}}},
         {"refinement_plan",
         {{"message", "Add precondition"},
         {"actions", nlohmann::json::array({{{"action", "add_precondition"}}})}}}}})            }
    };
    return unknown;
}

nlohmann::json make_validated_results(std::string_view category)
{
    nlohmann::json results = {
        {      "schema_version",          "validated_results.v1"                                },
        {                "tool", {{"name", "sappp"}, {"version", "test"}, {"build_id", "local"}}},
        {        "generated_at",                                          "2024-01-01T00:00:00Z"},
        {               "tu_id",                                                make_sha256('a')},
        {   "semantics_version",                                                        "sem.v1"},
        {"proof_system_version",                                                      "proof.v1"},
        {     "profile_version",                                                    "profile.v1"},
        {             "results",
         nlohmann::json::array({{{"po_id", make_sha256('c')},
         {"category", std::string(category)},
         {"validator_status", "Validated"}}})                                                   }
    };
    return results;
}

}  // namespace

TEST(ExplainReportTest, GeneratesTextSummary)
{
    std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "sappp_explain_test_text";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    auto unknown_path = write_json(temp_root / "unknown.json", make_unknown_ledger());

    ExplainOptions options{.unknown_path = unknown_path,
                           .validated_path = std::nullopt,
                           .po_id = std::nullopt,
                           .unknown_id = std::nullopt,
                           .output_path = std::nullopt,
                           .schema_dir = SAPPP_SCHEMA_DIR,
                           .format = ExplainFormat::kText};

    auto output = explain_unknowns(options);
    ASSERT_TRUE(output);
    EXPECT_EQ(output->unknown_count, 1U);
    ASSERT_FALSE(output->text.empty());
    EXPECT_EQ(output->text.front(), "UNKNOWN entries: 1");
}

TEST(ExplainReportTest, FiltersUsingValidatedResults)
{
    std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "sappp_explain_test_json";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    auto unknown_path = write_json(temp_root / "unknown.json", make_unknown_ledger());
    auto validated_path = write_json(temp_root / "validated.json", make_validated_results("SAFE"));

    ExplainOptions options{.unknown_path = unknown_path,
                           .validated_path = validated_path,
                           .po_id = std::nullopt,
                           .unknown_id = std::nullopt,
                           .output_path = temp_root / "explain.json",
                           .schema_dir = SAPPP_SCHEMA_DIR,
                           .format = ExplainFormat::kJson};

    auto output = explain_unknowns(options);
    ASSERT_TRUE(output);
    EXPECT_EQ(output->unknown_count, 0U);

    auto write = write_explain_output(options, *output);
    ASSERT_TRUE(write);

    std::ifstream in(*options.output_path);
    ASSERT_TRUE(in.is_open());
    nlohmann::json payload;
    in >> payload;
    EXPECT_EQ(payload.at("schema_version"), "explain.v1");
    EXPECT_EQ(payload.at("unknowns").size(), 0U);
}

}  // namespace sappp::report::tests
