#include "sappp/schema_validate.hpp"

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::common::test {

namespace {

std::string schema_path(const std::string& name)
{
    return std::string(SAPPP_SCHEMA_DIR) + "/" + name;
}

std::string sha256_hex()
{
    return "sha256:" + std::string(64, 'a');
}

nlohmann::json make_valid_unknown_json()
{
    return nlohmann::json{
        {      "schema_version",                    "unknown.v1"                                },
        {                "tool",                       {{"name", "sappp"}, {"version", "0.1.0"}}},
        {        "generated_at",                                          "2024-01-01T00:00:00Z"},
        {               "tu_id",                                                    sha256_hex()},
        {            "unknowns",
         nlohmann::json::array({nlohmann::json{
         {"unknown_stable_id", sha256_hex()},
         {"po_id", sha256_hex()},
         {"unknown_code", "U-TEST"},
         {"missing_lemma",
         {{"expr", {{"op", "and"}, {"args", nlohmann::json::array()}}},
         {"pretty", "x"},
         {"symbols", nlohmann::json::array({"x"})}}},
         {"refinement_plan", {{"message", "noop"}, {"actions", nlohmann::json::array()}}}}})    },
        {   "semantics_version",                                                            "v1"},
        {"proof_system_version",                                                            "v1"},
        {     "profile_version",                                                            "v1"}
    };
}

nlohmann::json make_valid_specdb_snapshot()
{
    return nlohmann::json{
        {"schema_version",                      "specdb_snapshot.v1"},
        {          "tool", {{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                    "2024-01-01T00:00:00Z"},
        {     "contracts",                   nlohmann::json::array()}
    };
}

}  // namespace

TEST(SchemaValidateTest, ValidUnknownSchemaPasses)
{
    nlohmann::json valid = make_valid_unknown_json();
    auto result = sappp::common::validate_json(valid, schema_path("unknown.v1.schema.json"));

    EXPECT_TRUE(result);
}

TEST(SchemaValidateTest, InvalidUnknownSchemaFails)
{
    nlohmann::json invalid = make_valid_unknown_json();
    invalid["schema_version"] = "unknown.v2";

    auto result = sappp::common::validate_json(invalid, schema_path("unknown.v1.schema.json"));

    EXPECT_FALSE(result);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().message.empty());
}

TEST(SchemaValidateTest, ValidSpecdbSnapshotPasses)
{
    nlohmann::json valid = make_valid_specdb_snapshot();
    auto result =
        sappp::common::validate_json(valid, schema_path("specdb_snapshot.v1.schema.json"));

    EXPECT_TRUE(result);
}

TEST(SchemaValidateTest, InvalidSpecdbSnapshotFails)
{
    nlohmann::json invalid = make_valid_specdb_snapshot();
    invalid.erase("contracts");

    auto result =
        sappp::common::validate_json(invalid, schema_path("specdb_snapshot.v1.schema.json"));

    EXPECT_FALSE(result);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().message.empty());
}

}  // namespace sappp::common::test
