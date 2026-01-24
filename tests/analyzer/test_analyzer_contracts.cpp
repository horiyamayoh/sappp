#include "analyzer.hpp"
#include "sappp/certstore.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] nlohmann::json make_simple_cfg(const nlohmann::json& inst,
                                             const nlohmann::json& edges)
{
    nlohmann::json block = {
        {   "id",                          "B1"},
        {"insts", nlohmann::json::array({inst})}
    };
    return nlohmann::json{
        { "entry",                           "B1"},
        {"blocks", nlohmann::json::array({block})},
        { "edges",                          edges}
    };
}

[[nodiscard]] nlohmann::json make_nir_with_function(const nlohmann::json& func)
{
    nlohmann::json nir = make_nir();
    nir["functions"] = nlohmann::json::array({func});
    return nir;
}

nlohmann::json make_nir_with_lifetime()
{
    nlohmann::json block = {
        {   "id",                                                    "B1"       },
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "lifetime.begin"},
         {"args", nlohmann::json::array({"use_after_lifetime"})}},
         nlohmann::json{{"id", "I1"},
         {"op", "lifetime.end"},
         {"args", nlohmann::json::array({"use_after_lifetime"})}},
         nlohmann::json{
         {"id", "I2"},
         {"op", "sink.marker"},
         {"args",
         nlohmann::json::array({"use-after-lifetime", "use_after_lifetime"})}}})}
    };

    nlohmann::json func = {
        {"function_uid","usr::foo"                        },
        {"mangled_name",            "_Z3foov"},
        {         "cfg",
         {{"entry", "B1"},
         {"blocks", nlohmann::json::array({block})},
         {"edges", nlohmann::json::array()}} }
    };

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                           nlohmann::json::array({func})}
    };
}

[[nodiscard]] nlohmann::json make_nir_with_exception_flow()
{
    nlohmann::json inst = {
        {  "id",                    "I1"},
        {  "op",                "invoke"},
        {"args", nlohmann::json::array()}
    };
    nlohmann::json edges = nlohmann::json::array({
        {{"from", "B1"}, {"to", "B1"}, {"kind", "exception"}}
    });
    nlohmann::json cfg = make_simple_cfg(inst, edges);
    nlohmann::json func = {
        {"function_uid",     "usr::foo"},
        {"mangled_name",      "_Z3foov"},
        {         "cfg", std::move(cfg)}
    };
    return make_nir_with_function(func);
}

nlohmann::json make_nir_with_vcall(std::string_view candidate_method)
{
    nlohmann::json block = {
        {   "id",                                                           "B1"},
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "vcall"},
         {"args", nlohmann::json::array({"receiver", "CS0", "signature"})}}})   }
    };

    nlohmann::json func = {
        {"function_uid",                          "usr::caller"                        },
        {"mangled_name",                                                   "_Z6callerv"},
        {         "cfg",
         {{"entry", "B1"},
         {"blocks", nlohmann::json::array({block})},
         {"edges", nlohmann::json::array()}}                                           },
        {      "tables",
         {{"vcall_candidates",
         nlohmann::json::array({nlohmann::json{
         {"id", "CS0"},
         {"methods", nlohmann::json::array({std::string(candidate_method)})}}})}}      }
    };

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                           nlohmann::json::array({func})}
    };
}

[[nodiscard]] nlohmann::json make_nir_with_concurrency()
{
    nlohmann::json inst = {
        {  "id",                    "I1"},
        {  "op",          "thread.spawn"},
        {"args", nlohmann::json::array()}
    };
    nlohmann::json cfg = make_simple_cfg(inst, nlohmann::json::array());
    nlohmann::json func = {
        {"function_uid",     "usr::foo"},
        {"mangled_name",      "_Z3foov"},
        {         "cfg", std::move(cfg)}
    };
    return make_nir_with_function(func);
}
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

nlohmann::json make_po_list_for_function(std::string_view function_usr,
                                         std::string_view mangled_name,
                                         std::string_view block_id,
                                         std::string_view inst_id)
{
    nlohmann::json po = {
        {               "po_id",               make_sha256('b')                                },
        {             "po_kind",                                                   "UB.DivZero"},
        {     "profile_version",                                               "safety.core.v1"},
        {   "semantics_version",                                                       "sem.v1"},
        {"proof_system_version",                                                     "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}        },
        {            "function",
         nlohmann::json{{"usr", std::string(function_usr)},
         {"mangled", std::string(mangled_name)}}                                               },
        {              "anchor",
         nlohmann::json{{"block_id", std::string(block_id)}, {"inst_id", std::string(inst_id)}}},
        {           "predicate",
         nlohmann::json{
         {"expr", nlohmann::json{{"op", "ub.check"}, {"args", nlohmann::json::array({true})}}},
         {"pretty", "ub.check(true)"}}                                                         }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

nlohmann::json make_use_after_lifetime_po_list()
{
    nlohmann::json po = {
        {               "po_id",            make_sha256('b')                                },
        {             "po_kind",                                          "UseAfterLifetime"},
        {     "profile_version",                                            "safety.core.v1"},
        {   "semantics_version",                                                    "sem.v1"},
        {"proof_system_version",                                                  "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}     },
        {            "function", nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I2"}}},
        {           "predicate",
         nlohmann::json{
         {"expr",
         nlohmann::json{
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"UseAfterLifetime", "use_after_lifetime"})}}},
         {"pretty", "use_after_lifetime"}}                                                  }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
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

nlohmann::json make_contract_snapshot(bool include_contract,
                                      bool include_concurrency_clause = false)
{
    nlohmann::json contracts = nlohmann::json::array();
    if (include_contract) {
        nlohmann::json contract_body = {
            {"pre", nlohmann::json{{"expr", nlohmann::json{{"op", "true"}}}, {"pretty", "true"}}}
        };
        if (include_concurrency_clause) {
            contract_body["concurrency"] = nlohmann::json::object();
        }
        contracts.push_back(nlohmann::json{
            {"schema_version","contract_ir.v1"                              },
            {   "contract_id",                    make_sha256('d')},
            {        "target", nlohmann::json{{"usr", "usr::foo"}}},
            {          "tier",                             "Tier1"},
            { "version_scope",
             nlohmann::json{{"abi", "x86_64"},
             {"library_version", "1.0.0"},
             {"conditions", nlohmann::json::array()},
             {"priority", 0}}                                     },
            {      "contract",            std::move(contract_body)}
        });
    }

    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };
}

[[nodiscard]] nlohmann::json make_contract_entry(std::string contract_id,
                                                 std::string abi,
                                                 std::string library_version,
                                                 std::vector<std::string> conditions,
                                                 int priority)
{
    nlohmann::json scope = {
        {  "priority",                priority},
        {"conditions", nlohmann::json::array()}
    };
    if (!abi.empty()) {
        scope["abi"] = std::move(abi);
    }
    if (!library_version.empty()) {
        scope["library_version"] = std::move(library_version);
    }
    for (auto& condition : conditions) {
        scope["conditions"].push_back(std::move(condition));
    }

    nlohmann::json contract_body = {
        {"pre", nlohmann::json{{"expr", nlohmann::json{{"op", "true"}}}, {"pretty", "true"}}}
    };

    return nlohmann::json{
        {"schema_version",                    "contract_ir.v1"},
        {   "contract_id",              std::move(contract_id)},
        {        "target", nlohmann::json{{"usr", "usr::foo"}}},
        {          "tier",                             "Tier1"},
        { "version_scope",                    std::move(scope)},
        {      "contract",            std::move(contract_body)}
    };
}

[[nodiscard]] nlohmann::json make_contract_snapshot_from_contracts(nlohmann::json contracts)
{
    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                    std::move(contracts)}
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
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
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
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
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

TEST(AnalyzerContractTest, UseAfterLifetimeProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_lifetime_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir_with_lifetime();
    auto po_list = make_use_after_lifetime_po_list();
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    sappp::certstore::CertStore cert_store(cert_dir.string(), SAPPP_SCHEMA_DIR);
    std::ifstream index_file(cert_dir / "index" / (make_sha256('b') + ".json"));
    ASSERT_TRUE(index_file.is_open());
    nlohmann::json index_json = nlohmann::json::parse(index_file);
    std::string root_hash = index_json.at("root").get<std::string>();

    auto root_cert = cert_store.get(root_hash);
    ASSERT_TRUE(root_cert);
    EXPECT_EQ(root_cert->at("result"), "BUG");
}

TEST(AnalyzerContractTest, DoubleFreePoProducesLifetimeUnknown)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_double_free_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("DoubleFree");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "LifetimeUnmodeled");
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
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
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

TEST(AnalyzerContractTest, ExceptionFlowProducesUnknownCode)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_exception_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir_with_exception_flow();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "ExceptionFlowConservative");
}

TEST(AnalyzerContractTest, VCallMissingContractProducesUnknownCode)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_vcall_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir_with_vcall("usr::vcall_target");
    auto po_list = make_po_list_for_function("usr::caller", "_Z6callerv", "B1", "I0");
    auto specdb_snapshot = make_contract_snapshot_for_target("usr::caller");

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "VirtualCall.MissingContract.Pre");
}

TEST(AnalyzerContractTest, ConcurrencyUnsupportedProducesUnknownCode)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_concurrency_unknown");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {            .abi = "", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir_with_concurrency();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(true, true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "ConcurrencyUnsupported");
}

TEST(AnalyzerContractTest, ResolvesContractsByScopeAndPriority)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contract_resolution");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"       },
        .contract_scope = {      .abi = "x86_64",
                     .library_version = "1.0.0",
                     .conditions = {"COND_A", "COND_B"}}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");

    nlohmann::json contracts = nlohmann::json::array(
        {make_contract_entry(make_sha256('d'), "x86_64", "1.0.0", {"COND_A"}, 0),
         make_contract_entry(make_sha256('e'), "x86_64", "1.0.0", {}, 9),
         make_contract_entry(make_sha256('f'), "", "", {}, 10)});
    auto specdb_snapshot = make_contract_snapshot_from_contracts(std::move(contracts));

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    ASSERT_TRUE(unknowns.at(0).contains("depends_on"));
    const auto& matched = unknowns.at(0).at("depends_on").at("contracts");
    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(matched.at(0), make_sha256('d'));
}

TEST(AnalyzerContractTest, ResolvesContractsByPriorityWhenScopeEqual)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contract_priority");
    auto cert_dir = temp_dir / "certstore";

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"                                   },
        .contract_scope = {      .abi = "x86_64", .library_version = "", .conditions = {}}
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");

    nlohmann::json contracts =
        nlohmann::json::array({make_contract_entry(make_sha256('g'), "x86_64", "", {}, 1),
                               make_contract_entry(make_sha256('h'), "x86_64", "", {}, 7)});
    auto specdb_snapshot = make_contract_snapshot_from_contracts(std::move(contracts));

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    ASSERT_TRUE(unknowns.at(0).contains("depends_on"));
    const auto& matched = unknowns.at(0).at("depends_on").at("contracts");
    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(matched.at(0), make_sha256('h'));
}

}  // namespace sappp::analyzer::test
