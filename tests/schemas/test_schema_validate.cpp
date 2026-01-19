#include "sappp/schema_validate.hpp"

#include <string>
#include <string_view>
#include <vector>

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

nlohmann::json make_tool_json()
{
    return nlohmann::json{
        {   "name", "sappp"},
        {"version", "0.1.0"}
    };
}

nlohmann::json make_location_json()
{
    return nlohmann::json{
        {"file", "main.c"},
        {"line",        1},
        { "col",        1}
    };
}

nlohmann::json make_expr_json()
{
    return nlohmann::json{
        {  "op",                   "and"},
        {"args", nlohmann::json::array()}
    };
}

nlohmann::json make_valid_unknown_json()
{
    return nlohmann::json{
        {      "schema_version",                    "unknown.v1"                                },
        {                "tool",                                                make_tool_json()},
        {        "generated_at",                                          "2024-01-01T00:00:00Z"},
        {               "tu_id",                                                    sha256_hex()},
        {            "unknowns",
         nlohmann::json::array({nlohmann::json{
         {"unknown_stable_id", sha256_hex()},
         {"po_id", sha256_hex()},
         {"unknown_code", "U-TEST"},
         {"missing_lemma",
         {{"expr", make_expr_json()},
         {"pretty", "x"},
         {"symbols", nlohmann::json::array({"x"})}}},
         {"refinement_plan", {{"message", "noop"}, {"actions", nlohmann::json::array()}}}}})    },
        {   "semantics_version",                                                            "v1"},
        {"proof_system_version",                                                            "v1"},
        {     "profile_version",                                                            "v1"}
    };
}

struct SchemaCase
{
    std::string schema_file;
    nlohmann::json valid_json;
};

constexpr std::string_view kGeneratedAt = "2024-01-01T00:00:00Z";

std::string generated_at_string()
{
    return std::string(kGeneratedAt);
}

nlohmann::json make_analysis_config_json()
{
    return nlohmann::json{
        {      "schema_version",                   "analysis_config.v1"},
        {                "tool",                       make_tool_json()},
        {        "generated_at",                  generated_at_string()},
        {   "semantics_version",                                   "v1"},
        {"proof_system_version",                                   "v1"},
        {     "profile_version",                                   "v1"},
        {            "analysis", {{"budget", nlohmann::json::object()}}}
    };
}

nlohmann::json make_build_snapshot_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {"schema_version",   "build_snapshot.v1"                          },
        {          "tool",                                make_tool_json()},
        {  "generated_at",                           generated_at_string()},
        {          "host",           {{"os", "linux"}, {"arch", "x86_64"}}},
        { "compile_units",
         nlohmann::json::array(
         {{{"tu_id", sha},
         {"cwd", "/repo"},
         {"argv", nlohmann::json::array({"cc"})},
         {"lang", "c"},
         {"std", "c11"},
         {"target",
         {{"triple", "x86_64-unknown-linux-gnu"},
         {"abi", "sysv"},
         {"data_layout", {{"ptr_bits", 64}, {"long_bits", 64}, {"align", {{"max", 8}}}}}}},
         {"frontend", {{"kind", "clang"}, {"version", "17.0.0"}}}}})      }
    };
}

nlohmann::json make_cert_po_def_json()
{
    const auto sha = sha256_hex();
    const auto expr = make_expr_json();
    return nlohmann::json{
        {"schema_version","cert.v1"                          },
        {          "kind",                          "PoDef"},
        {            "po",
         {{"po_id", sha},
         {"po_kind", "div0"},
         {"profile_version", "v1"},
         {"semantics_version", "v1"},
         {"proof_system_version", "v1"},
         {"repo_identity", {{"path", "main.c"}, {"content_sha256", sha}}},
         {"function", {{"usr", "usr:main"}, {"mangled", "_Z4mainv"}}},
         {"anchor", {{"block_id", "B0"}, {"inst_id", "I0"}}},
         {"predicate", {{"expr", expr}, {"pretty", "x"}}}} }
    };
}

nlohmann::json make_cert_index_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {"schema_version", "cert_index.v1"},
        {         "po_id",             sha},
        {          "root",             sha}
    };
}

nlohmann::json make_contract_ir_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {"schema_version",         "contract_ir.v1"},
        {   "contract_id",                      sha},
        {        "target",    {{"usr", "usr:main"}}},
        {          "tier",                  "Tier0"},
        { "version_scope", nlohmann::json::object()},
        {      "contract", nlohmann::json::object()}
    };
}

nlohmann::json make_diff_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {"schema_version",   "diff.v1"                          },
        {          "tool",                      make_tool_json()},
        {  "generated_at",                 generated_at_string()},
        {        "before",
         {{"input_digest", sha},
         {"semantics_version", "v1"},
         {"proof_system_version", "v1"},
         {"profile_version", "v1"},
         {"results_digest", sha}}                               },
        {         "after",
         {{"input_digest", sha},
         {"semantics_version", "v1"},
         {"proof_system_version", "v1"},
         {"profile_version", "v1"},
         {"results_digest", sha}}                               },
        {       "changes",
         nlohmann::json::array({{{"po_id", sha},
         {"from", {{"category", "UNKNOWN"}}},
         {"to", {{"category", "UNKNOWN"}}},
         {"change_kind", "Unchanged"}}})                        }
    };
}

nlohmann::json make_nir_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {   "schema_version","nir.v1"                             },
        {             "tool",           make_tool_json()},
        {     "generated_at",      generated_at_string()},
        {            "tu_id",                        sha},
        {"semantics_version",                       "v1"},
        {        "functions",
         nlohmann::json::array(
         {{{"function_uid", "func:main"},
         {"mangled_name", "_Z4mainv"},
         {"cfg",
         {{"entry", "B0"},
         {"blocks",
         nlohmann::json::array(
         {{{"id", "B0"},
         {"insts", nlohmann::json::array({{{"id", "I0"}, {"op", "ret"}}})}}})},
         {"edges", nlohmann::json::array()}}}}})        }
    };
}

nlohmann::json make_pack_manifest_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {      "schema_version",              "pack_manifest.v1"                                },
        {                "tool",                                                make_tool_json()},
        {        "generated_at",                                           generated_at_string()},
        {   "semantics_version",                                                            "v1"},
        {"proof_system_version",                                                            "v1"},
        {     "profile_version",                                                            "v1"},
        {        "input_digest",                                                             sha},
        {         "repro_level",                                                            "L0"},
        {               "files",
         nlohmann::json::array({{{"path", "out/nir.json"}, {"sha256", sha}, {"size_bytes", 0}}})}
    };
}

nlohmann::json make_po_json()
{
    const auto sha = sha256_hex();
    const auto expr = make_expr_json();
    return nlohmann::json{
        {"schema_version",                         "po.v1"                          },
        {          "tool",                                          make_tool_json()},
        {  "generated_at",                                     generated_at_string()},
        {         "tu_id",                                                       sha},
        {           "pos",
         nlohmann::json::array({{{"po_id", sha},
         {"po_kind", "div0"},
         {"profile_version", "v1"},
         {"semantics_version", "v1"},
         {"proof_system_version", "v1"},
         {"repo_identity", {{"path", "main.c"}, {"content_sha256", sha}}},
         {"function", {{"usr", "usr:main"}, {"mangled", "_Z4mainv"}}},
         {"anchor", {{"block_id", "B0"}, {"inst_id", "I0"}}},
         {"predicate", {{"expr", expr}, {"pretty", "x"}}}}})                        }
    };
}

nlohmann::json make_source_map_json()
{
    const auto sha = sha256_hex();
    const auto location = make_location_json();
    return nlohmann::json{
        {"schema_version","source_map.v1"                          },
        {          "tool",      make_tool_json()},
        {  "generated_at", generated_at_string()},
        {         "tu_id",                   sha},
        {       "entries",
         nlohmann::json::array(
         {{{"ir_ref", {{"function_uid", "func:main"}, {"block_id", "B0"}, {"inst_id", "I0"}}},
         {"spelling_loc", location},
         {"expansion_loc", location}}})         }
    };
}

nlohmann::json make_specdb_snapshot_json()
{
    return nlohmann::json{
        {"schema_version",    "specdb_snapshot.v1"},
        {          "tool",        make_tool_json()},
        {  "generated_at",   generated_at_string()},
        {     "contracts", nlohmann::json::array()}
    };
}

nlohmann::json make_validated_results_json()
{
    const auto sha = sha256_hex();
    return nlohmann::json{
        {      "schema_version",       "validated_results.v1"                                },
        {                "tool",                                             make_tool_json()},
        {        "generated_at",                                        generated_at_string()},
        {               "tu_id",                                                          sha},
        {   "semantics_version",                                                         "v1"},
        {"proof_system_version",                                                         "v1"},
        {     "profile_version",                                                         "v1"},
        {             "results",
         nlohmann::json::array(
         {{{"po_id", sha}, {"category", "UNKNOWN"}, {"validator_status", "Downgraded"}}})    }
    };
}

std::vector<SchemaCase> make_schema_cases()
{
    return {
        {  .schema_file = "analysis_config.v1.schema.json",
         .valid_json = make_analysis_config_json()                                                  },
        {   .schema_file = "build_snapshot.v1.schema.json", .valid_json = make_build_snapshot_json()},
        {             .schema_file = "cert.v1.schema.json",    .valid_json = make_cert_po_def_json()},
        {       .schema_file = "cert_index.v1.schema.json",     .valid_json = make_cert_index_json()},
        {      .schema_file = "contract_ir.v1.schema.json",    .valid_json = make_contract_ir_json()},
        {             .schema_file = "diff.v1.schema.json",           .valid_json = make_diff_json()},
        {              .schema_file = "nir.v1.schema.json",            .valid_json = make_nir_json()},
        {    .schema_file = "pack_manifest.v1.schema.json",  .valid_json = make_pack_manifest_json()},
        {               .schema_file = "po.v1.schema.json",             .valid_json = make_po_json()},
        {       .schema_file = "source_map.v1.schema.json",     .valid_json = make_source_map_json()},
        {  .schema_file = "specdb_snapshot.v1.schema.json",
         .valid_json = make_specdb_snapshot_json()                                                  },
        {          .schema_file = "unknown.v1.schema.json",  .valid_json = make_valid_unknown_json()},
        {.schema_file = "validated_results.v1.schema.json",
         .valid_json = make_validated_results_json()                                                }
    };
}

}  // namespace

TEST(SchemaValidateTest, ValidSchemaSamplesPass)
{
    for (const auto& schema_case : make_schema_cases()) {
        SCOPED_TRACE(schema_case.schema_file);
        auto result = sappp::common::validate_json(schema_case.valid_json,
                                                   schema_path(schema_case.schema_file));

        EXPECT_TRUE(result);
    }
}

TEST(SchemaValidateTest, InvalidSchemaSamplesFail)
{
    for (const auto& schema_case : make_schema_cases()) {
        SCOPED_TRACE(schema_case.schema_file);
        nlohmann::json invalid = schema_case.valid_json;
        invalid["schema_version"] = "invalid.v0";

        auto result = sappp::common::validate_json(invalid, schema_path(schema_case.schema_file));

        EXPECT_FALSE(result);
        ASSERT_FALSE(result);
        EXPECT_FALSE(result.error().message.empty());
    }
}

}  // namespace sappp::common::test
