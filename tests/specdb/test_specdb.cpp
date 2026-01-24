#include "sappp/specdb.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::specdb::test {

namespace {

nlohmann::json
make_contract(std::string usr, std::string abi, std::vector<std::string> conditions, int priority)
{
    return nlohmann::json{
        {       "target",          {{"usr", std::move(usr)}}                         },
        {         "tier",                                                     "Tier1"},
        {"version_scope",
         {{"abi", std::move(abi)},
         {"library_version", "1.2.3"},
         {"conditions", std::move(conditions)},
         {"priority", priority}}                                                     },
        {     "contract", {{"pre", {{"expr", {{"op", "true"}}}, {"pretty", "true"}}}}}
    };
}

std::filesystem::path ensure_temp_dir(const std::string& name)
{
    auto temp_dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    return temp_dir;
}

void write_text_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open());
    out << content;
}

}  // namespace

TEST(SpecdbTest, NormalizeContractAssignsIdAndSortsConditions)
{
    auto contract = make_contract("usr::normalize", "x86_64", {"Z", "A"}, 0);
    auto normalized =
        sappp::specdb::normalize_contract_ir(contract, std::filesystem::path(SAPPP_SCHEMA_DIR));

    ASSERT_TRUE(normalized);
    EXPECT_EQ(normalized->at("schema_version"), "contract_ir.v1");
    EXPECT_TRUE(normalized->contains("contract_id"));
    EXPECT_TRUE(normalized->at("contract_id").get<std::string>().starts_with("sha256:"));
    auto conditions = normalized->at("version_scope").at("conditions");
    EXPECT_EQ(conditions, nlohmann::json::array({"A", "Z"}));
}

TEST(SpecdbTest, BuildSnapshotCollectsSidecarAndAnnotation)
{
    auto temp_dir = ensure_temp_dir("sappp_specdb_test");
    auto source_path = temp_dir / "sample.cpp";
    auto spec_dir = temp_dir / "specdb";
    std::filesystem::create_directories(spec_dir);

    auto annotation_contract = make_contract("usr::annotation", "arm64", {}, 0);
    auto sidecar_contract = make_contract("usr::sidecar", "x86_64", {"COND"}, 1);

    std::string annotation_line = "//@sappp contract " + annotation_contract.dump();
    write_text_file(source_path, annotation_line + "\n");

    {
        std::ofstream sidecar(spec_dir / "contract.json");
        ASSERT_TRUE(sidecar.is_open());
        sidecar << sidecar_contract.dump(2);
    }

    nlohmann::json build_snapshot = {
        {"schema_version",           "build_snapshot.v1"                          },
        {          "tool",               {{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        { "compile_units",
         nlohmann::json::array({nlohmann::json{
         {"cwd", temp_dir.string()},
         {"argv", nlohmann::json::array({"clang++", source_path.string()})}}})    }
    };

    sappp::specdb::BuildOptions options{
        .build_snapshot = build_snapshot,
        .spec_path = spec_dir,
        .schema_dir = std::filesystem::path(SAPPP_SCHEMA_DIR),
        .generated_at = "1970-01-01T00:00:00Z",
        .tool = nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}
    };

    auto snapshot = sappp::specdb::build_snapshot(options);
    ASSERT_TRUE(snapshot);
    ASSERT_TRUE(snapshot->contains("specdb_digest"));
    auto contracts = snapshot->at("contracts");
    ASSERT_TRUE(contracts.is_array());
    ASSERT_EQ(contracts.size(), 2U);
    EXPECT_EQ(contracts.at(0).at("target").at("usr"), "usr::annotation");
    EXPECT_EQ(contracts.at(1).at("target").at("usr"), "usr::sidecar");
}

TEST(SpecdbTest, NormalizeScopeContextSortsConditions)
{
    sappp::specdb::VersionScopeContext context{
        .abi = "x86_64",
        .library_version = "1.0.0",
        .conditions = {"Z", "A", "A"}
    };
    auto normalized = sappp::specdb::normalize_scope_context(std::move(context));
    EXPECT_EQ(normalized.conditions, (std::vector<std::string>{"A", "Z"}));
}

TEST(SpecdbTest, EvaluateVersionScopeMatchesAndRanks)
{
    nlohmann::json scope = {
        {            "abi",                        "x86_64"},
        {"library_version",                         "1.0.0"},
        {     "conditions", nlohmann::json::array({"COND"})},
        {       "priority",                               3}
    };
    sappp::specdb::VersionScopeContext context{
        .abi = "x86_64",
        .library_version = "1.0.0",
        .conditions = {"COND", "OTHER"}
    };
    auto match =
        sappp::specdb::evaluate_version_scope(scope,
                                              sappp::specdb::normalize_scope_context(context));
    ASSERT_TRUE(match);
    EXPECT_TRUE(match->matches);
    EXPECT_EQ(match->rank.abi_rank, 2);
    EXPECT_EQ(match->rank.library_version_rank, 2);
    EXPECT_EQ(match->rank.conditions_rank, 2);
    EXPECT_EQ(match->rank.priority, 3);

    sappp::specdb::VersionScopeContext mismatch_context{.abi = "arm64",
                                                        .library_version = "",
                                                        .conditions = {}};
    auto mismatch = sappp::specdb::evaluate_version_scope(
        scope,
        sappp::specdb::normalize_scope_context(std::move(mismatch_context)));
    ASSERT_TRUE(mismatch);
    EXPECT_FALSE(mismatch->matches);
}

}  // namespace sappp::specdb::test
