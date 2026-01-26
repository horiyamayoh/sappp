#include "analyzer.hpp"
#include "sappp/certstore.hpp"

#include <filesystem>
#include <fstream>
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

ContractMatchContext make_match_context(std::vector<std::string> conditions = {})
{
    ContractMatchContext context;
    context.abi = "x86_64";
    context.library_version = "1.0.0";
    context.conditions = std::move(conditions);
    return context;
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

nlohmann::json make_nir_with_lifetime_dtor()
{
    nlohmann::json block = {
        {   "id",                                                    "B1"       },
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "lifetime.begin"},
         {"args", nlohmann::json::array({"use_after_lifetime"})}},
         nlohmann::json{{"id", "I1"},
         {"op", "dtor"},
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

nlohmann::json make_nir_with_lifetime_move()
{
    nlohmann::json block = {
        {   "id",                                                    "B1"        },
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "lifetime.begin"},
         {"args", nlohmann::json::array({"moved_from"})}},
         nlohmann::json{{"id", "I1"},
         {"op", "move"},
         {"args", nlohmann::json::array({"Widget::Widget", "moved_from"})}},
         nlohmann::json{
         {"id", "I2"},
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"use-after-lifetime", "moved_from"})}}})}
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

nlohmann::json make_nir_with_heap_double_free()
{
    nlohmann::json block = {
        {   "id",                                                 "B1"},
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "alloc"},
         {"args", nlohmann::json::array({"ptr"})}},
         nlohmann::json{{"id", "I1"},
         {"op", "free"},
         {"args", nlohmann::json::array({"ptr"})}},
         nlohmann::json{{"id", "I2"},
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"double-free", "ptr"})}}})   }
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

nlohmann::json make_nir_with_heap_invalid_free()
{
    nlohmann::json block = {
        {   "id",                                  "B1"},
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"invalid-free", "ptr"})}},
         nlohmann::json{{"id", "I1"},
         {"op", "free"},
         {"args", nlohmann::json::array({"ptr"})}}})   }
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

nlohmann::json make_nir_with_uninit_read_bug()
{
    nlohmann::json decl_ref = {
        {      "op",   "ref"},
        {    "name", "value"},
        {    "kind", "local"},
        {    "type",   "int"},
        {"has_init",   false}
    };
    nlohmann::json block = {
        {   "id",                                                   "B1"},
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "assign"},
         {"args", nlohmann::json::array({decl_ref})}},
         nlohmann::json{{"id", "I1"},
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"uninit_read", "value"})}}})   }
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

nlohmann::json make_nir_with_uninit_read_safe()
{
    nlohmann::json decl_ref = {
        {      "op",   "ref"},
        {    "name", "value"},
        {    "kind", "local"},
        {    "type",   "int"},
        {"has_init",   false}
    };
    nlohmann::json store_ref = {
        {  "op",   "ref"},
        {"name", "value"},
        {"kind", "local"},
        {"type",   "int"}
    };
    nlohmann::json block = {
        {   "id",                                                   "B1"},
        {"insts",
         nlohmann::json::array(
         {nlohmann::json{{"id", "I0"},
         {"op", "assign"},
         {"args", nlohmann::json::array({decl_ref})}},
         nlohmann::json{{"id", "I1"},
         {"op", "store"},
         {"args", nlohmann::json::array({store_ref})}},
         nlohmann::json{{"id", "I2"},
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"uninit_read", "value"})}}})   }
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

nlohmann::json make_double_free_po_list()
{
    nlohmann::json po = {
        {               "po_id",            make_sha256('b')                                },
        {             "po_kind",                                                "DoubleFree"},
        {     "profile_version",                                            "safety.core.v1"},
        {   "semantics_version",                                                    "sem.v1"},
        {"proof_system_version",                                                  "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}     },
        {            "function", nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I2"}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "sink.marker"},
         {"args", nlohmann::json::array({"DoubleFree", "ptr"})}}},
         {"pretty", "double_free"}}                                                         }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

nlohmann::json make_invalid_free_po_list()
{
    nlohmann::json po = {
        {               "po_id",            make_sha256('b')                                },
        {             "po_kind",                                               "InvalidFree"},
        {     "profile_version",                                            "safety.core.v1"},
        {   "semantics_version",                                                    "sem.v1"},
        {"proof_system_version",                                                  "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}     },
        {            "function", nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor",       nlohmann::json{{"block_id", "B1"}, {"inst_id", "I0"}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "sink.marker"},
         {"args", nlohmann::json::array({"InvalidFree", "ptr"})}}},
         {"pretty", "invalid_free"}}                                                        }
    };

    return nlohmann::json{
        {"schema_version",                                                 "po.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {         "tu_id",                                        make_sha256('a')},
        {           "pos",                             nlohmann::json::array({po})}
    };
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Test helper keeps call sites compact.
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Test helper keeps call sites compact.
nlohmann::json make_use_after_lifetime_po_list_for_target(std::string_view target,
                                                          std::string_view inst_id)
{
    nlohmann::json po = {
        {               "po_id",                      make_sha256('b')                                },
        {             "po_kind",                                                    "UseAfterLifetime"},
        {     "profile_version",                                                      "safety.core.v1"},
        {   "semantics_version",                                                              "sem.v1"},
        {"proof_system_version",                                                            "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}               },
        {            "function",           nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor", nlohmann::json{{"block_id", "B1"}, {"inst_id", std::string(inst_id)}}},
        {           "predicate",
         nlohmann::json{
         {"expr",
         nlohmann::json{
         {"op", "sink.marker"},
         {"args", nlohmann::json::array({"UseAfterLifetime", std::string(target)})}}},
         {"pretty", "use_after_lifetime"}}                                                            }
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
    return make_use_after_lifetime_po_list_for_target("use_after_lifetime", "I2");
}

nlohmann::json make_uninit_read_po_list(std::string_view inst_id)
{
    nlohmann::json po = {
        {               "po_id",                      make_sha256('b')                                },
        {             "po_kind",                                                          "UninitRead"},
        {     "profile_version",                                                      "safety.core.v1"},
        {   "semantics_version",                                                              "sem.v1"},
        {"proof_system_version",                                                            "proof.v1"},
        {       "repo_identity",
         nlohmann::json{{"path", "src/main.cpp"}, {"content_sha256", make_sha256('c')}}               },
        {            "function",           nlohmann::json{{"usr", "usr::foo"}, {"mangled", "_Z3foov"}}},
        {              "anchor", nlohmann::json{{"block_id", "B1"}, {"inst_id", std::string(inst_id)}}},
        {           "predicate",
         nlohmann::json{{"expr",
         nlohmann::json{{"op", "sink.marker"},
         {"args", nlohmann::json::array({"UninitRead", "value"})}}},
         {"pretty", "uninit_read"}}                                                                   }
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

nlohmann::json make_contract_entry(std::string contract_id,
                                   std::string usr,
                                   std::string abi,
                                   std::string library_version,
                                   std::vector<std::string> conditions,
                                   int priority)
{
    return nlohmann::json{
        {"schema_version",                        "contract_ir.v1"                          },
        {   "contract_id",                                            std::move(contract_id)},
        {        "target",                           nlohmann::json{{"usr", std::move(usr)}}},
        {          "tier",                                                           "Tier1"},
        { "version_scope",
         nlohmann::json{{"abi", std::move(abi)},
         {"library_version", std::move(library_version)},
         {"conditions", std::move(conditions)},
         {"priority", priority}}                                                            },
        {      "contract",
         nlohmann::json{
         {"pre",
         nlohmann::json{{"expr", nlohmann::json{{"op", "true"}}}, {"pretty", "true"}}}}     }
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
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto specdb_snapshot = make_contract_snapshot(false);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir_with_lifetime();
    auto po_list = make_use_after_lifetime_po_list();
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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

TEST(AnalyzerContractTest, UseAfterLifetimeWithDtorProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_lifetime_dtor_bug");
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

    auto nir = make_nir_with_lifetime_dtor();
    auto po_list = make_use_after_lifetime_po_list();
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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

TEST(AnalyzerContractTest, UseAfterLifetimeAfterMoveIsUnknown)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_lifetime_move_unknown");
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

    auto nir = make_nir_with_lifetime_move();
    auto po_list = make_use_after_lifetime_po_list_for_target("moved_from", "I2");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "LifetimeStateUnknown");
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
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir_with_heap_double_free();
    auto po_list = make_double_free_po_list();
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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

TEST(AnalyzerContractTest, InvalidFreePoProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_invalid_free_bug");
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

    auto nir = make_nir_with_heap_invalid_free();
    auto po_list = make_invalid_free_po_list();
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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

TEST(AnalyzerContractTest, UninitReadPoProducesBug)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_uninit_bug");
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

    auto nir = make_nir_with_uninit_read_bug();
    auto po_list = make_uninit_read_po_list("I1");
    auto specdb_snapshot = make_contract_snapshot(true);

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
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

TEST(AnalyzerContractTest, UninitReadPoProducesSafe)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_uninit_safe");
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

    auto nir = make_nir_with_uninit_read_safe();
    auto po_list = make_uninit_read_po_list("I2");
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
    EXPECT_EQ(root_cert->at("result"), "SAFE");
}

TEST(AnalyzerContractTest, MatchContractsRespectsConditionsSpecificity)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contract_match_conditions");
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

    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(make_contract_entry(make_sha256('d'),
                                            "usr::foo",
                                            "x86_64",
                                            "1.0.0",
                                            {"COND_A", "COND_B"},
                                            1));
    contracts.push_back(
        make_contract_entry(make_sha256('e'), "usr::foo", "x86_64", "1.0.0", {"COND_A"}, 5));
    contracts.push_back(
        make_contract_entry(make_sha256('f'), "usr::foo", "x86_64", "1.0.0", {}, 10));
    contracts.push_back(make_contract_entry(make_sha256('g'),
                                            "usr::foo",
                                            "arm64",
                                            "1.0.0",
                                            {"COND_A", "COND_B"},
                                            0));

    nlohmann::json specdb_snapshot = {
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto output =
        analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context({"COND_A", "COND_B"}));
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    const auto& depends = unknowns.at(0).at("depends_on");
    const auto& matched_contracts = depends.at("contracts");
    ASSERT_EQ(matched_contracts.size(), 1U);
    EXPECT_EQ(matched_contracts.at(0).get<std::string>(), make_sha256('d'));
}

TEST(AnalyzerContractTest, MatchContractsPrefersLibraryVersionAfterAbi)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contract_match_library");
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

    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(
        make_contract_entry(make_sha256('j'), "usr::foo", "x86_64", "1.0.0", {}, 0));
    contracts.push_back(make_contract_entry(make_sha256('k'), "usr::foo", "x86_64", "", {}, 0));

    nlohmann::json specdb_snapshot = {
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    const auto& depends = unknowns.at(0).at("depends_on");
    const auto& matched_contracts = depends.at("contracts");
    ASSERT_EQ(matched_contracts.size(), 1U);
    EXPECT_EQ(matched_contracts.at(0).get<std::string>(), make_sha256('j'));
}

TEST(AnalyzerContractTest, MatchContractsUsesPriorityAfterScope)
{
    auto temp_dir = ensure_temp_dir("sappp_analyzer_contract_match_priority");
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

    nlohmann::json contracts = nlohmann::json::array();
    contracts.push_back(
        make_contract_entry(make_sha256('h'), "usr::foo", "x86_64", "1.0.0", {"COND_X"}, 1));
    contracts.push_back(
        make_contract_entry(make_sha256('i'), "usr::foo", "x86_64", "1.0.0", {"COND_X"}, 7));

    nlohmann::json specdb_snapshot = {
        {"schema_version",                                    "specdb_snapshot.v1"},
        {          "tool", nlohmann::json{{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                                  "1970-01-01T00:00:00Z"},
        {     "contracts",                                               contracts}
    };

    auto nir = make_nir();
    auto po_list = make_po_list("UB.DivZero");
    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context({"COND_X"}));
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    const auto& depends = unknowns.at(0).at("depends_on");
    const auto& matched_contracts = depends.at("contracts");
    ASSERT_EQ(matched_contracts.size(), 1U);
    EXPECT_EQ(matched_contracts.at(0).get<std::string>(), make_sha256('i'));
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
                     .profile = "safety.core.v1"},
        .budget = AnalyzerConfig::AnalysisBudget{},
        .memory_domain = ""
    });

    auto nir = make_nir_with_vcall("usr::vcall_target");
    auto po_list = make_po_list_for_function("usr::caller", "_Z6callerv", "B1", "I0");
    auto specdb_snapshot = make_contract_snapshot_for_target("usr::caller");

    auto output = analyzer.analyze(nir, po_list, &specdb_snapshot, make_match_context());
    ASSERT_TRUE(output);

    const auto& unknowns = output->unknown_ledger.at("unknowns");
    ASSERT_EQ(unknowns.size(), 1U);
    EXPECT_EQ(unknowns.at(0).at("unknown_code"), "VirtualCall.MissingContract.Pre");
}

}  // namespace sappp::analyzer::test
