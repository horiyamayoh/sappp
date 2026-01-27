#include "analyzer.hpp"
#include "sappp/certstore.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::analyzer::test {

namespace {

std::string make_sha256(char fill)
{
    return std::string("sha256:") + std::string(64, fill);
}

std::filesystem::path ensure_temp_dir(const std::string& name)
{
    auto temp_dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    return temp_dir;
}

ContractMatchContext make_match_context()
{
    ContractMatchContext context;
    context.abi = "x86_64";
    context.library_version = "1.0.0";
    return context;
}

nlohmann::json make_nir_with_points_to()
{
    nlohmann::json safe_inst = {
        {     "id",                                                    "I0"                   },
        {     "op",                                                                   "assign"},
        {"effects",
         nlohmann::json{{"points_to",
         nlohmann::json::array({nlohmann::json{
         {"ptr", "p"},
         {"targets",
         nlohmann::json::array({nlohmann::json{{"alloc_site", "alloc1"},
         {"field", "root"}}})}}})}}                                                           }
    };
    nlohmann::json safe_anchor_inst = {
        {"id",         "I1"},
        {"op", "custom.ptr"}
    };

    nlohmann::json safe_block = {
        {   "id",                                                 "B0"},
        {"insts", nlohmann::json::array({safe_inst, safe_anchor_inst})}
    };

    nlohmann::json safe_func = {
        {"function_uid","usr::safe"                        },
        {"mangled_name",                         "_Z4safev"},
        {         "cfg",
         nlohmann::json{{"entry", "B0"},
         {"blocks", nlohmann::json::array({safe_block})},
         {"edges", nlohmann::json::array()}}               }
    };

    nlohmann::json unknown_block = {
        {   "id",                                                                       "B0"},
        {"insts", nlohmann::json::array({nlohmann::json{{"id", "I0"}, {"op", "custom.op"}}})}
    };

    nlohmann::json unknown_func = {
        {"function_uid","usr::unknown"                        },
        {"mangled_name",                      "_Z7unknownv"},
        {         "cfg",
         nlohmann::json{{"entry", "B0"},
         {"blocks", nlohmann::json::array({unknown_block})},
         {"edges", nlohmann::json::array()}}               }
    };

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",        nlohmann::json::array({safe_func, unknown_func})}
    };
}

nlohmann::json make_po_list_with_points_to()
{
    nlohmann::json safe_po = {
        {               "po_id",              make_sha256('b')                                },
        {             "po_kind",                                                "UB.NullDeref"},
        {     "profile_version",                                              "safety.core.v1"},
        {   "semantics_version",                                                      "sem.v1"},
        {"proof_system_version",                                                    "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/safe.cpp"}, {"content_sha256", make_sha256('e')}}       },
        {            "function", nlohmann::json{{"usr", "usr::safe"}, {"mangled", "_Z4safev"}}},
        {              "anchor",         nlohmann::json{{"block_id", "B0"}, {"inst_id", "I1"}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "custom.ptr"},
         {"args", nlohmann::json::array({"UB.NullDeref", "p"})}}},
         {"pretty", "custom.ptr"}}                                                            }
    };

    nlohmann::json unknown_po = {
        {               "po_id",                    make_sha256('c')                                },
        {             "po_kind",                                                        "UB.DivZero"},
        {     "profile_version",                                                    "safety.core.v1"},
        {   "semantics_version",                                                            "sem.v1"},
        {"proof_system_version",                                                          "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/unknown.cpp"}, {"content_sha256", make_sha256('f')}}          },
        {            "function", nlohmann::json{{"usr", "usr::unknown"}, {"mangled", "_Z7unknownv"}}},
        {              "anchor",               nlohmann::json{{"block_id", "B0"}, {"inst_id", "I0"}}},
        {           "predicate",
         nlohmann::json{
         {"expr",
         nlohmann::json{{"op", "custom.op"}, {"args", nlohmann::json::array({"UB.DivZero"})}}},
         {"pretty", "custom.op"}}                                                                   }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",            nlohmann::json::array({safe_po, unknown_po})}
    };
}

nlohmann::json make_contract_snapshot_for_target(std::string_view target_usr)
{
    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(nlohmann::json{
        {"schema_version",                        "contract_ir.v1"                          },
        {   "contract_id",                                                  make_sha256('d')},
        {        "target",                  nlohmann::json{{"usr", std::string(target_usr)}}},
        {          "tier",                                                           "Tier1"},
        { "version_scope",
         nlohmann::json{{"abi", "x86_64"},
         {"library_version", "1.0.0"},
         {"conditions", nlohmann::json::array()},
         {"priority", 0}}                                                                   },
        {      "contract",
         nlohmann::json{
         {"pre",
         nlohmann::json{{"expr", nlohmann::json{{"op", "true"}}}, {"pretty", "true"}}}}     }
    });

    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };
}

nlohmann::json make_contract_snapshot_for_safe()
{
    return make_contract_snapshot_for_target("usr::safe");
}

nlohmann::json make_nir_with_exception_points_to()
{
    nlohmann::json invoke_inst = {
        {     "id",                                                    "I0"                   },
        {     "op",                                                                   "invoke"},
        {"effects",
         nlohmann::json{{"points_to",
         nlohmann::json::array({nlohmann::json{
         {"ptr", "p"},
         {"targets",
         nlohmann::json::array({nlohmann::json{{"alloc_site", "inbounds"},
         {"field", "root"}}})}}})}}                                                           }
    };

    nlohmann::json exception_block = {
        {   "id",                                 "B0"},
        {"insts", nlohmann::json::array({invoke_inst})}
    };

    nlohmann::json anchor_block = {
        {   "id",                                                                        "B1"},
        {"insts", nlohmann::json::array({nlohmann::json{{"id", "I0"}, {"op", "custom.ptr"}}})}
    };

    nlohmann::json func = {
        {"function_uid",                          "usr::exception"                        },
        {"mangled_name",                                                   "_Z9exceptionv"},
        {         "cfg",
         nlohmann::json{
         {"entry", "B0"},
         {"blocks", nlohmann::json::array({exception_block, anchor_block})},
         {"edges",
         nlohmann::json::array(
         {nlohmann::json{{"from", "B0"}, {"to", "B1"}, {"kind", "exception"}}})}}         }
    };

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                           nlohmann::json::array({func})}
    };
}

nlohmann::json make_po_list_for_exception_points_to()
{
    nlohmann::json po = {
        {               "po_id",                        make_sha256('f')                                },
        {             "po_kind",                                                          "UB.NullDeref"},
        {     "profile_version",                                                        "safety.core.v1"},
        {   "semantics_version",                                                                "sem.v1"},
        {"proof_system_version",                                                              "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/exception.cpp"}, {"content_sha256", make_sha256('e')}}            },
        {            "function", nlohmann::json{{"usr", "usr::exception"}, {"mangled", "_Z9exceptionv"}}},
        {              "anchor",                   nlohmann::json{{"block_id", "B1"}, {"inst_id", "I0"}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "custom.ptr"},
         {"args", nlohmann::json::array({"UB.NullDeref", "p"})}}},
         {"pretty", "custom.ptr"}}                                                                      }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

}  // namespace

TEST(AnalyzerPointsToTest, PointsToSimpleResolvesNullDeref)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_points_to");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir_with_points_to();
    auto po_list = make_po_list_with_points_to();
    auto specdb_snapshot = make_contract_snapshot_for_safe();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "MissingContract.Pre");
    EXPECT_EQ(unknowns.at(0).at("po_id").get<std::string>(), make_sha256('c'));

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "SAFE");

    std::string evidence_hash = root_cert->at("evidence").at("ref").get<std::string>();
    auto evidence_cert = cert_store.get(evidence_hash);
    ASSERT_TRUE(evidence_cert);
    EXPECT_EQ(evidence_cert->at("kind"), "SafetyProof");
    EXPECT_EQ(evidence_cert->at("domain"), "interval+null+lifetime+init+points-to.simple");

    const auto& points = evidence_cert->at("points");
    ASSERT_EQ(points.size(), 1U);
    const auto& state = points.at(0).at("state");
    ASSERT_TRUE(state.contains("points_to"));
    const auto& points_to = state.at("points_to");
    ASSERT_TRUE(points_to.is_array());
    ASSERT_EQ(points_to.size(), 1U);
    EXPECT_EQ(points_to.at(0).at("ptr"), "p");
    const auto& targets = points_to.at(0).at("targets");
    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets.at(0).at("alloc_site"), "alloc1");
    EXPECT_EQ(targets.at(0).at("field"), "root");
}

TEST(AnalyzerPointsToTest, PointsToExceptionPathPropagatesState)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_points_to_exception");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir_with_exception_points_to();
    auto po_list = make_po_list_for_exception_points_to();
    auto specdb_snapshot = make_contract_snapshot_for_target("usr::exception");

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
    ASSERT_TRUE(output) << output.error().code << ": " << output.error().message;

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U) << unknowns.dump(2);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "DomainTooWeak.Numeric");

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('f') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "SAFE");

    std::string evidence_hash = root_cert->at("evidence").at("ref").get<std::string>();
    auto evidence_cert = cert_store.get(evidence_hash);
    ASSERT_TRUE(evidence_cert);
    EXPECT_EQ(evidence_cert->at("kind"), "SafetyProof");
    const auto& points = evidence_cert->at("points");
    ASSERT_EQ(points.size(), 1U);
    const auto& state = points.at(0).at("state");
    ASSERT_TRUE(state.contains("points_to"));
    const auto& points_to = state.at("points_to");
    ASSERT_EQ(points_to.size(), 1U);
    EXPECT_EQ(points_to.at(0).at("ptr"), "p");
    const auto& targets = points_to.at(0).at("targets");
    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets.at(0).at("alloc_site"), "inbounds");
    EXPECT_EQ(targets.at(0).at("field"), "root");
}

}  // namespace sappp::analyzer::test
