#include "analyzer.hpp"

#include <filesystem>
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

nlohmann::json make_nir_with_two_blocks()
{
    nlohmann::json block0 = {
        {   "id",                                                                    "B0"},
        {"insts", nlohmann::json::array({nlohmann::json{{"id", "I0"}, {"op", "assign"}}})}
    };

    nlohmann::json block1 = {
        {   "id",                                                                         "B1"},
        {"insts", nlohmann::json::array({nlohmann::json{{"id", "I1"}, {"op", "sink.marker"}}})}
    };

    nlohmann::json func = {
        {"function_uid",                   "usr::foo"                        },
        {"mangled_name",                                            "_Z3foov"},
        {         "cfg",
         {{"entry", "B0"},
         {"blocks", nlohmann::json::array({block0, block1})},
         {"edges", nlohmann::json::array({{{"from", "B0"}, {"to", "B1"}}})}} }
    };

    return nlohmann::json{
        {"schema_version",                                                "nir.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {     "functions",                           nlohmann::json::array({func})}
    };
}

nlohmann::json make_po_list()
{
    nlohmann::json po = {
        {               "po_id",            make_sha256('b')                                },
        {             "po_kind",                                                "UB.DivZero"},
        {     "profile_version",                                            "safety.core.v1"},
        {   "semantics_version",                                                    "sem.v1"},
        {"proof_system_version",                                                  "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}     },
        {            "function", nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I1"}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "sink.marker"},
         {"args", nlohmann::json::array({"UB.DivZero"})}}},
         {"pretty", "sink.marker"}}                                                         }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

nlohmann::json make_contract_snapshot()
{
    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(nlohmann::json{
        {"schema_version",                        "contract_ir.v1"                          },
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

    return nlohmann::json{
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };
}

}  // namespace

TEST(AnalyzerBudgetTest, BudgetExceededProducesUnknown)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_budget_exceeded");
    auto cert_dir = temp_dir / "certstore";

    AnalyzerConfig::AnalysisBudget budget{};
    budget.max_iterations = 1;

    Analyzer analyzer({
        .schema_dir = SAPPP_SCHEMA_DIR,
        .certstore_dir = cert_dir.string(),
        .versions = {.semantics = "sem.v1",
                     .proof_system = "proof.v1",
                     .profile = "safety.core.v1"},
        .budget = budget,
        .memory_domain = ""
    });

    auto nir = make_nir_with_two_blocks();
    auto po_list = make_po_list();
    auto specdb_snapshot = make_contract_snapshot();

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot);
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "BudgetExceeded");
    const auto& action = unknowns.at(0).at("refinement_plan").at("actions").at(0).at("action");
    EXPECT_EQ(action, "increase-budget");
}

}  // namespace sappp::analyzer::test
