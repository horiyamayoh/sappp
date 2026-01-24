#include "analyzer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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

struct VCallTestConfig
{
    std::optional<std::string_view> candidate_id;
    bool include_candidates = false;
    std::vector<std::string_view> candidate_methods;
};

nlohmann::json make_nir_with_ops(const std::vector<std::string_view>& ops,
                                 const VCallTestConfig& vcall_config = {},
                                 const std::vector<nlohmann::json>& edges = {})
{
    nlohmann::json insts = nlohmann::json::array();
    int index = 0;
    for (const auto& op : ops) {
        nlohmann::json inst = nlohmann::json{
            {"id", "I" + std::to_string(index++)},
            {"op",               std::string(op)}
        };
        if (op == "vcall" && vcall_config.candidate_id.has_value()) {
            inst["args"] =
                nlohmann::json::array({"receiver", std::string(*vcall_config.candidate_id)});
        }
        insts.push_back(std::move(inst));
    }

    nlohmann::json block = {
        {   "id",  "B1"},
        {"insts", insts}
    };

    nlohmann::json edge_list = nlohmann::json::array();
    for (const auto& edge : edges) {
        edge_list.push_back(edge);
    }

    nlohmann::json func = {
        {"function_uid","usr::foo"                        },
        {"mangled_name",         "_Z3foov"},
        {         "cfg",
         {{"entry", "B1"},
         {"blocks", nlohmann::json::array({block})},
         {"edges", std::move(edge_list)}} }
    };

    if (vcall_config.include_candidates) {
        nlohmann::json methods = nlohmann::json::array();
        for (const auto& method : vcall_config.candidate_methods) {
            methods.push_back(std::string(method));
        }
        func["tables"] = nlohmann::json{
            {"vcall_candidates", nlohmann::json::array({{{"id", "CS0"}, {"methods", methods}}})}
        };
    }

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                           nlohmann::json::array({func})}
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
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I0"}}},
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

nlohmann::json make_contract_snapshot(bool include_concurrency = false,
                                      const std::vector<std::string_view>& extra_targets = {})
{
    nlohmann::json contract_body = nlohmann::json{
        {"pre", nlohmann::json{{"expr", nlohmann::json{{"op", "true"}}}, {"pretty", "true"}}}
    };
    if (include_concurrency) {
        contract_body["concurrency"] = nlohmann::json::object();
    }

    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(nlohmann::json{
        {"schema_version","contract_ir.v1"                          },
        {   "contract_id",                    make_sha256('d')},
        {        "target", nlohmann::json{{"usr", "usr::foo"}}},
        {          "tier",                             "Tier1"},
        { "version_scope",
         nlohmann::json{{"abi", "x86_64"},
         {"library_version", "1.0.0"},
         {"conditions", nlohmann::json::array()},
         {"priority", 0}}                                     },
        {      "contract",                       contract_body}
    });
    char contract_id_fill = 'e';
    for (const auto& target : extra_targets) {
        contracts.push_back(nlohmann::json{
            {"schema_version","contract_ir.v1"                              },
            {   "contract_id",              make_sha256(contract_id_fill++)},
            {        "target", nlohmann::json{{"usr", std::string(target)}}},
            {          "tier",                                      "Tier1"},
            { "version_scope",
             nlohmann::json{{"abi", "x86_64"},
             {"library_version", "1.0.0"},
             {"conditions", nlohmann::json::array()},
             {"priority", 0}}                                              },
            {      "contract",                                contract_body}
        });
    }

    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };
}

Analyzer make_analyzer(const std::filesystem::path& cert_dir)
{
    return Analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"}
    });
}

void expect_unknown_code(const nlohmann::json& unknowns,
                         std::string_view expected_code,
                         std::string_view expected_action)
{
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), expected_code);
    const auto& action = unknowns.at(0).at("refinement_plan").at("actions").at(0).at("action");
    EXPECT_EQ(action, expected_action);
}

}  // namespace

TEST(AnalyzerUnknownCodeTest, ExceptionFlowConservativeForUnmodeledExceptionEdge)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_exception_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    auto nir = make_nir_with_ops(
        {
            "call"
    },
        {},
        {nlohmann::json{{"from", "B1"}, {"to", "B1"}, {"kind", "exception"}}});
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "ExceptionFlowConservative", "refine-exception");
}

TEST(AnalyzerUnknownCodeTest, VirtualDispatchUnknownForVcall)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_vcall_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    auto nir = make_nir_with_ops({"vcall"});
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "VirtualCall.CandidateSetMissing", "refine-vcall");
}

TEST(AnalyzerUnknownCodeTest, VirtualDispatchUnknownWithVcallCandidates)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_vcall_dispatch_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    VCallTestConfig vcall_config{
        .candidate_id = "CS0",
        .include_candidates = true,
        .candidate_methods = {"_Z3barv"},
    };
    auto nir = make_nir_with_ops({"vcall"}, vcall_config);
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(false, {"_Z3barv"});

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "VirtualDispatchUnknown", "resolve-vcall");
}

TEST(AnalyzerUnknownCodeTest, AtomicOrderUnknownForAtomicRead)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_atomic_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    auto nir = make_nir_with_ops({"atomic.r"});
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "AtomicOrderUnknown", "refine-atomic-order");
}

TEST(AnalyzerUnknownCodeTest, ConcurrencyUnsupportedForThreadSpawn)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_thread_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    auto nir = make_nir_with_ops({"thread.spawn"});
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "ConcurrencyUnsupported", "refine-concurrency");
}

TEST(AnalyzerUnknownCodeTest, SyncContractMissingForSyncEvent)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_sync_unknown");
    auto cert_dir = temp_dir / "certstore";

    auto analyzer = make_analyzer(cert_dir);
    auto nir = make_nir_with_ops({"sync.event"});
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(false);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    expect_unknown_code(unknowns, "SyncContractMissing", "add-contract");
}

}  // namespace sappp::analyzer::test
