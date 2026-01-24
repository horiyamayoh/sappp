#include "analyzer.hpp"
#include "sappp/certstore.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

nlohmann::json make_nir()
{
    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                                 nlohmann::json::array()}
    };
}

nlohmann::json make_nir_with_insts(const nlohmann::json& insts)
{
    return nlohmann::json{
        {"schema_version",                      "nir.v1"                          },
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",
         nlohmann::json::array({nlohmann::json{
         {"function_uid", "usr::foo"},
         {"mangled_name", "_Z3foov"},
         {"cfg",
         {{"entry", "B1"},
         {"blocks", nlohmann::json::array({{{"id", "B1"}, {"insts", insts}}})},
         {"edges", nlohmann::json::array()}}}}})                                  }
    };
}

nlohmann::json make_po_list(std::string_view po_kind)
{
    nlohmann::json po = {
        {               "po_id",            make_sha256('b')                                },
        {             "po_kind",                                        std::string(po_kind)},
        {     "profile_version",                                            "safety.core.v1"},
        {   "semantics_version",                                                    "sem.v1"},
        {"proof_system_version",                                                  "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}     },
        {            "function", nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I1"}}},
        {           "predicate",
         nlohmann::json{
         {"expr", nlohmann::json{{"op", "custom.op"}, {"args", nlohmann::json::array({true})}}},
         {"pretty", "custom"}}                                                              }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

nlohmann::json
make_sink_po_list(std::string_view po_kind, std::string_view block_id, std::string_view inst_id)
{
    nlohmann::json predicate_expr = {
        {  "op",                                 "sink.marker"},
        {"args", nlohmann::json::array({std::string(po_kind)})}
    };
    nlohmann::json po = {
        {               "po_id",               make_sha256('b')                                },
        {             "po_kind",                                           std::string(po_kind)},
        {     "profile_version",                                               "safety.core.v1"},
        {   "semantics_version",                                                       "sem.v1"},
        {"proof_system_version",                                                     "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}        },
        {            "function",    nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",
         nlohmann::json{{"block_id", std::string(block_id)}, {"inst_id", std::string(inst_id)}}},
        {           "predicate",   nlohmann::json{{"expr", predicate_expr}, {"pretty", "sink"}}}
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

nlohmann::json make_contract_snapshot(bool include_contract)
{
    nlohmann::json contracts = nlohmann::json::array();
    if (include_contract) {
        contracts.push_back(nlohmann::json{
            {"schema_version",                    "contract_ir.v1"                              },
            {   "contract_id",                                                  make_sha256('d')},
            {        "target",                               nlohmann::json{{"usr", "usr::foo"}}},
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
    }

    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };
}

}  // namespace

TEST(AnalyzerContractTest, AddsContractRefsAndKeepsUnknownDetails)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contracts");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "DomainTooWeak.Numeric");
    ASSERT_TRUE(unknowns.at(0).contains("depends_on"));
    const auto& contracts = unknowns.at(0).at("depends_on").at("contracts");
    ASSERT_EQ(contracts.size(), 1U);
    EXPECT_EQ(contracts.at(0).get<std::string>(), make_sha256('d'));

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    ASSERT_TRUE(root_cert->at("depends").contains("contracts"));
    std::string contract_ref_hash =
        root_cert->at("depends").at("contracts").at(0).at("ref").get<std::string>();

    auto contract_cert = cert_store.get(contract_ref_hash);
    ASSERT_TRUE(contract_cert);
    EXPECT_EQ(contract_cert->at("kind"), "ContractRef");
    EXPECT_EQ(contract_cert->at("contract_id"), make_sha256('d'));
}

TEST(AnalyzerContractTest, MissingContractProducesUnknownCode)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_missing_contracts");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(false);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "MissingContract.Pre");
    EXPECT_FALSE(unknowns.at(0).contains("depends_on"));
}

TEST(AnalyzerContractTest, LifetimePoProducesLifetimeUnknown)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_lifetime_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UseAfterLifetime");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "LifetimeUnmodeled");
}

TEST(AnalyzerContractTest, DoubleFreePoProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_double_free_bug");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    nlohmann::json insts = nlohmann::json::array({
        {{"id", "I1"},       {"op", "alloc"},    {"args", nlohmann::json::array({"ptr", "new"})}},
        {{"id", "I2"},        {"op", "free"}, {"args", nlohmann::json::array({"ptr", "delete"})}},
        {{"id", "I3"}, {"op", "sink.marker"},   {"args", nlohmann::json::array({"double-free"})}},
        {{"id", "I4"},        {"op", "free"}, {"args", nlohmann::json::array({"ptr", "delete"})}}
    });
    auto nir = make_nir_with_insts(insts);
    auto po_list = make_sink_po_list("DoubleFree", "B1", "I3");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    EXPECT_TRUE(unknowns.empty());

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "BUG");
}

TEST(AnalyzerContractTest, InvalidFreePoProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_invalid_free_bug");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    nlohmann::json insts = nlohmann::json::array({
        {{"id", "I1"},        {"op", "free"}, {"args", nlohmann::json::array({"ptr", "delete"})}},
        {{"id", "I2"}, {"op", "sink.marker"},  {"args", nlohmann::json::array({"invalid-free"})}}
    });
    auto nir = make_nir_with_insts(insts);
    auto po_list = make_sink_po_list("InvalidFree", "B1", "I2");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    EXPECT_TRUE(unknowns.empty());

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "BUG");
}

TEST(AnalyzerContractTest, DoubleFreePoProducesSafe)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_double_free_safe");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    nlohmann::json insts = nlohmann::json::array({
        {{"id", "I1"},       {"op", "alloc"},    {"args", nlohmann::json::array({"ptr", "new"})}},
        {{"id", "I2"},        {"op", "free"}, {"args", nlohmann::json::array({"ptr", "delete"})}},
        {{"id", "I3"}, {"op", "sink.marker"},   {"args", nlohmann::json::array({"double-free"})}}
    });
    auto nir = make_nir_with_insts(insts);
    auto po_list = make_sink_po_list("DoubleFree", "B1", "I3");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    EXPECT_TRUE(unknowns.empty());

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "SAFE");
}

TEST(AnalyzerContractTest, UninitReadPoProducesInitUnknown)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_uninit_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UninitRead");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "DomainTooWeak.Memory");
}

}  // namespace sappp::analyzer::test
