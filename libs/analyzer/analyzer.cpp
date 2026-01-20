/**
 * @file analyzer.cpp
 * @brief Analyzer v0 stub implementation
 */

#include "analyzer.hpp"

#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sappp::analyzer {

namespace {

constexpr std::string_view kSafetyDomain = "interval+null+lifetime+init";

[[nodiscard]] std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

struct JsonFieldContext
{
    const nlohmann::json* obj = nullptr;
    std::string_view key;
    std::string_view context;
};

[[nodiscard]] sappp::Result<std::string> require_string(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_string()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected string field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return obj.at(key).get<std::string>();
}

[[nodiscard]] sappp::Result<nlohmann::json> require_object(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_object()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected object field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return obj.at(key);
}

[[nodiscard]] sappp::Result<nlohmann::json> require_array(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_array()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected array field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return obj.at(key);
}

[[nodiscard]] std::unordered_map<std::string, std::string>
build_function_uid_map(const nlohmann::json& nir_json)
{
    std::unordered_map<std::string, std::string> mapping;
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return mapping;
    }
    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("mangled_name") || !func.contains("function_uid")) {
            continue;
        }
        if (!func.at("mangled_name").is_string() || !func.at("function_uid").is_string()) {
            continue;
        }
        mapping.emplace(func.at("mangled_name").get<std::string>(),
                        func.at("function_uid").get<std::string>());
    }
    return mapping;
}

[[nodiscard]] sappp::Result<std::string>
resolve_function_uid(const std::unordered_map<std::string, std::string>& mapping,
                     const nlohmann::json& po)
{
    auto function_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "function", .context = "po"});
    if (!function_obj) {
        return std::unexpected(function_obj.error());
    }
    auto mangled = require_string(
        JsonFieldContext{.obj = &(*function_obj), .key = "mangled", .context = "po.function"});
    if (!mangled) {
        return std::unexpected(mangled.error());
    }

    auto it = mapping.find(*mangled);
    if (it != mapping.end()) {
        return it->second;
    }

    if (function_obj->contains("usr") && function_obj->at("usr").is_string()) {
        return function_obj->at("usr").get<std::string>();
    }

    return *mangled;
}

struct IrAnchor
{
    std::string block_id;
    std::string inst_id;
};

[[nodiscard]] sappp::Result<IrAnchor> extract_anchor(const nlohmann::json& po)
{
    auto anchor_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "anchor", .context = "po"});
    if (!anchor_obj) {
        return std::unexpected(anchor_obj.error());
    }
    auto block_id = require_string(
        JsonFieldContext{.obj = &(*anchor_obj), .key = "block_id", .context = "po.anchor"});
    if (!block_id) {
        return std::unexpected(block_id.error());
    }
    auto inst_id = require_string(
        JsonFieldContext{.obj = &(*anchor_obj), .key = "inst_id", .context = "po.anchor"});
    if (!inst_id) {
        return std::unexpected(inst_id.error());
    }
    return IrAnchor{.block_id = std::move(*block_id), .inst_id = std::move(*inst_id)};
}

[[nodiscard]] sappp::Result<nlohmann::json> extract_predicate_expr(const nlohmann::json& po)
{
    auto predicate_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "predicate", .context = "po"});
    if (!predicate_obj) {
        return std::unexpected(predicate_obj.error());
    }
    if (!predicate_obj->contains("expr") || !predicate_obj->at("expr").is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected predicate.expr object in po"));
    }
    return predicate_obj->at("expr");
}

[[nodiscard]] sappp::Result<std::string> extract_predicate_pretty(const nlohmann::json& po)
{
    auto predicate_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "predicate", .context = "po"});
    if (!predicate_obj) {
        return std::unexpected(predicate_obj.error());
    }
    if (predicate_obj->contains("pretty") && predicate_obj->at("pretty").is_string()) {
        return predicate_obj->at("pretty").get<std::string>();
    }
    return std::string("predicate");
}

[[nodiscard]] nlohmann::json
make_ir_ref_obj(const std::string& tu_id, const std::string& function_uid, const IrAnchor& anchor)
{
    return nlohmann::json{
        {"schema_version",       "cert.v1"},
        {          "kind",         "IrRef"},
        {         "tu_id",           tu_id},
        {  "function_uid",    function_uid},
        {      "block_id", anchor.block_id},
        {       "inst_id",  anchor.inst_id}
    };
}

[[nodiscard]] nlohmann::json make_bug_trace(const std::string& po_id,
                                            const nlohmann::json& ir_ref_obj)
{
    nlohmann::json step = nlohmann::json{
        {"ir", ir_ref_obj}
    };
    return nlohmann::json{
        {"schema_version",                                                    "cert.v1"},
        {          "kind",                                                   "BugTrace"},
        {    "trace_kind",                                                 "ir_path.v1"},
        {         "steps",                                nlohmann::json::array({step})},
        {     "violation", nlohmann::json{{"po_id", po_id}, {"predicate_holds", false}}}
    };
}

[[nodiscard]] nlohmann::json make_safety_proof(const std::string& function_uid,
                                               const IrAnchor& anchor,
                                               const nlohmann::json& predicate_expr,
                                               bool predicate_holds)
{
    nlohmann::json state = nlohmann::json::object();
    if (predicate_holds) {
        state["predicates"] = nlohmann::json::array({predicate_expr});
    } else {
        state["predicates"] = nlohmann::json::array();
    }

    nlohmann::json point = nlohmann::json{
        {   "ir",
         {{"function_uid", function_uid},
         {"block_id", anchor.block_id},
         {"inst_id", anchor.inst_id}} },
        {"state",                state}
    };

    return nlohmann::json{
        {"schema_version",                      "cert.v1"},
        {          "kind",                  "SafetyProof"},
        {        "domain",     std::string(kSafetyDomain)},
        {        "points", nlohmann::json::array({point})},
        {        "pretty",                   "stub proof"}
    };
}

[[nodiscard]] nlohmann::json make_proof_root(const std::string& po_hash,
                                             const std::string& ir_hash,
                                             const std::string& evidence_hash,
                                             const std::string& result_kind,
                                             const sappp::VersionTriple& versions)
{
    return nlohmann::json{
        {"schema_version",    "cert.v1"                          },
        {          "kind",                            "ProofRoot"},
        {            "po",       nlohmann::json{{"ref", po_hash}}},
        {            "ir",       nlohmann::json{{"ref", ir_hash}}},
        {        "result",                            result_kind},
        {      "evidence", nlohmann::json{{"ref", evidence_hash}}},
        {       "depends",
         nlohmann::json{{"semantics_version", versions.semantics},
         {"proof_system_version", versions.proof_system},
         {"profile_version", versions.profile}}                  },
        {    "hash_scope",                        "hash_scope.v1"}
    };
}

struct UnknownEntryInput
{
    std::string_view po_id;
    std::string_view predicate_pretty;
    const nlohmann::json* predicate_expr = nullptr;
    std::string_view function_hint;
};

[[nodiscard]] nlohmann::json make_unknown_entry(const UnknownEntryInput& input)
{
    nlohmann::json missing_lemma = {
        {   "expr",                                     *input.predicate_expr},
        { "pretty",                       std::string(input.predicate_pretty)},
        {"symbols", nlohmann::json::array({std::string(input.function_hint)})}
    };

    nlohmann::json refinement_plan = {
        {"message","Provide invariant or solver support to discharge the PO."},
        {"actions",
         nlohmann::json::array(
         {nlohmann::json{{"action", "synthesize-invariant"},
         {"params", nlohmann::json{{"po_id", input.po_id}}}}}) }
    };

    return nlohmann::json{
        {"unknown_stable_id", sappp::common::sha256_prefixed(input.po_id)},
        {            "po_id",                    std::string(input.po_id)},
        {     "unknown_code",                      "Unknown.AnalysisStub"},
        {    "missing_lemma",                               missing_lemma},
        {  "refinement_plan",                             refinement_plan}
    };
}

[[nodiscard]] sappp::Result<nlohmann::json> build_unknown_entry(const nlohmann::json& po,
                                                                std::string_view po_id)
{
    auto predicate_expr = extract_predicate_expr(po);
    if (!predicate_expr) {
        return std::unexpected(predicate_expr.error());
    }
    auto predicate_pretty = extract_predicate_pretty(po);
    if (!predicate_pretty) {
        return std::unexpected(predicate_pretty.error());
    }
    auto function_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "function", .context = "po"});
    if (!function_obj) {
        return std::unexpected(function_obj.error());
    }
    auto function_hint = require_string(
        JsonFieldContext{.obj = &(*function_obj), .key = "mangled", .context = "po.function"});
    if (!function_hint) {
        return std::unexpected(function_hint.error());
    }

    UnknownEntryInput input = {.po_id = po_id,
                               .predicate_pretty = *predicate_pretty,
                               .predicate_expr = &(*predicate_expr),
                               .function_hint = *function_hint};
    return make_unknown_entry(input);
}

[[nodiscard]] sappp::Result<std::vector<const nlohmann::json*>>
collect_ordered_pos(const nlohmann::json& po_list)
{
    auto pos_array =
        require_array(JsonFieldContext{.obj = &po_list, .key = "pos", .context = "po_list"});
    if (!pos_array) {
        return std::unexpected(pos_array.error());
    }

    std::vector<const nlohmann::json*> result;
    result.reserve(pos_array->size());
    for (const auto& po : *pos_array) {
        if (!po.is_object()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "Expected PO entry to be an object"));
        }
        result.push_back(&po);
    }

    std::ranges::stable_sort(result, [](const nlohmann::json* a, const nlohmann::json* b) {
        return a->at("po_id").get<std::string>() < b->at("po_id").get<std::string>();
    });

    return result;
}

[[nodiscard]] nlohmann::json build_unknown_ledger_base(const nlohmann::json& nir_json,
                                                       const nlohmann::json& po_list_json,
                                                       const sappp::VersionTriple& versions,
                                                       const nlohmann::json& tool_obj,
                                                       std::string_view tu_id)
{
    std::string generated_at = current_time_utc();
    if (nir_json.contains("generated_at") && nir_json.at("generated_at").is_string()) {
        generated_at = nir_json.at("generated_at").get<std::string>();
    } else if (po_list_json.contains("generated_at")
               && po_list_json.at("generated_at").is_string()) {
        generated_at = po_list_json.at("generated_at").get<std::string>();
    }

    nlohmann::json unknown_ledger = {
        {      "schema_version",            "unknown.v1"},
        {                "tool",                tool_obj},
        {        "generated_at",            generated_at},
        {               "tu_id",      std::string(tu_id)},
        {            "unknowns", nlohmann::json::array()},
        {   "semantics_version",      versions.semantics},
        {"proof_system_version",   versions.proof_system},
        {     "profile_version",        versions.profile}
    };

    if (nir_json.contains("input_digest")) {
        unknown_ledger["input_digest"] = nir_json.at("input_digest");
    } else if (po_list_json.contains("input_digest")) {
        unknown_ledger["input_digest"] = po_list_json.at("input_digest");
    }

    return unknown_ledger;
}

struct PoProcessingContext
{
    sappp::certstore::CertStore* cert_store = nullptr;
    const std::unordered_map<std::string, std::string>* function_uid_map = nullptr;
    std::string_view tu_id;
    const sappp::VersionTriple* versions = nullptr;
};

struct PoProcessingOutput
{
    std::string po_id;
    bool has_unknown = false;
    nlohmann::json unknown_entry = nlohmann::json::object();
};

struct EvidenceResult
{
    nlohmann::json evidence;
    std::string result_kind;
};

struct EvidenceInput
{
    const nlohmann::json* po = nullptr;
    const nlohmann::json* ir_ref = nullptr;
    std::string_view po_id;
    std::string_view function_uid;
    const IrAnchor* anchor = nullptr;
    bool is_bug = false;
    bool is_safe = false;
};

[[nodiscard]] sappp::Result<EvidenceResult> build_evidence(const EvidenceInput& input)
{
    if (input.is_bug) {
        return EvidenceResult{.evidence = make_bug_trace(std::string(input.po_id), *input.ir_ref),
                              .result_kind = "BUG"};
    }

    auto predicate_expr = extract_predicate_expr(*input.po);
    if (!predicate_expr) {
        return std::unexpected(predicate_expr.error());
    }

    EvidenceResult output{.evidence = make_safety_proof(std::string(input.function_uid),
                                                        *input.anchor,
                                                        *predicate_expr,
                                                        input.is_safe),
                          .result_kind = "SAFE"};
    return output;
}

[[nodiscard]] sappp::Result<std::string> put_cert(sappp::certstore::CertStore& cert_store,
                                                  const nlohmann::json& cert)
{
    auto cert_hash = cert_store.put(cert);
    if (!cert_hash) {
        return std::unexpected(cert_hash.error());
    }
    return *cert_hash;
}

struct PoBaseData
{
    std::string po_id;
    std::string function_uid;
    IrAnchor anchor;
    nlohmann::json po_def;
    nlohmann::json ir_ref;
    bool is_bug = false;
    bool is_safe = false;
};

[[nodiscard]] sappp::Result<PoBaseData>
build_po_base(const nlohmann::json& po, std::size_t index, const PoProcessingContext& context)
{
    auto po_id = require_string(JsonFieldContext{.obj = &po, .key = "po_id", .context = "po"});
    if (!po_id) {
        return std::unexpected(po_id.error());
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }

    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    nlohmann::json po_def = {
        {"schema_version", "cert.v1"},
        {          "kind",   "PoDef"},
        {            "po",        po}
    };

    nlohmann::json ir_ref = make_ir_ref_obj(std::string(context.tu_id), *function_uid, *anchor);

    return PoBaseData{.po_id = *po_id,
                      .function_uid = *function_uid,
                      .anchor = std::move(*anchor),
                      .po_def = std::move(po_def),
                      .ir_ref = std::move(ir_ref),
                      .is_bug = index == 0,
                      .is_safe = index == 1};
}

[[nodiscard]] sappp::Result<PoProcessingOutput>
process_po(const nlohmann::json& po, std::size_t index, const PoProcessingContext& context)
{
    auto base = build_po_base(po, index, context);
    if (!base) {
        return std::unexpected(base.error());
    }

    auto po_hash = put_cert(*context.cert_store, base->po_def);
    if (!po_hash) {
        return std::unexpected(po_hash.error());
    }

    auto ir_hash = put_cert(*context.cert_store, base->ir_ref);
    if (!ir_hash) {
        return std::unexpected(ir_hash.error());
    }

    EvidenceInput evidence_input{.po = &po,
                                 .ir_ref = &base->ir_ref,
                                 .po_id = base->po_id,
                                 .function_uid = base->function_uid,
                                 .anchor = &base->anchor,
                                 .is_bug = base->is_bug,
                                 .is_safe = base->is_safe};
    auto evidence_result = build_evidence(evidence_input);
    if (!evidence_result) {
        return std::unexpected(evidence_result.error());
    }

    auto evidence_hash = put_cert(*context.cert_store, evidence_result->evidence);
    if (!evidence_hash) {
        return std::unexpected(evidence_hash.error());
    }

    nlohmann::json root = make_proof_root(*po_hash,
                                          *ir_hash,
                                          *evidence_hash,
                                          evidence_result->result_kind,
                                          *context.versions);
    auto root_hash = put_cert(*context.cert_store, root);
    if (!root_hash) {
        return std::unexpected(root_hash.error());
    }
    if (auto bind = context.cert_store->bind_po(base->po_id, *root_hash); !bind) {
        return std::unexpected(bind.error());
    }

    PoProcessingOutput output{.po_id = base->po_id};
    if (!base->is_bug && !base->is_safe) {
        auto unknown_entry = build_unknown_entry(po, base->po_id);
        if (!unknown_entry) {
            return std::unexpected(unknown_entry.error());
        }
        output.has_unknown = true;
        output.unknown_entry = std::move(*unknown_entry);
    }

    return output;
}

[[nodiscard]] sappp::VoidResult
ensure_unknowns(std::vector<nlohmann::json>& unknowns,
                const std::vector<const nlohmann::json*>& ordered_pos)
{
    if (!unknowns.empty() || ordered_pos.empty()) {
        return {};
    }

    const nlohmann::json& po = *ordered_pos.front();
    auto po_id = require_string(JsonFieldContext{.obj = &po, .key = "po_id", .context = "po"});
    if (!po_id) {
        return std::unexpected(po_id.error());
    }
    auto unknown_entry = build_unknown_entry(po, *po_id);
    if (!unknown_entry) {
        return std::unexpected(unknown_entry.error());
    }
    unknowns.push_back(std::move(*unknown_entry));
    return {};
}

}  // namespace

Analyzer::Analyzer(AnalyzerConfig config)
    : m_config(std::move(config))
{}

sappp::Result<AnalyzeOutput> Analyzer::analyze(const nlohmann::json& nir_json,
                                               const nlohmann::json& po_list_json) const
{
    auto tu_id =
        require_string(JsonFieldContext{.obj = &nir_json, .key = "tu_id", .context = "nir"});
    if (!tu_id) {
        return std::unexpected(tu_id.error());
    }

    auto tool_obj =
        require_object(JsonFieldContext{.obj = &nir_json, .key = "tool", .context = "nir"});
    if (!tool_obj) {
        return std::unexpected(tool_obj.error());
    }

    auto ordered_pos = collect_ordered_pos(po_list_json);
    if (!ordered_pos) {
        return std::unexpected(ordered_pos.error());
    }
    const auto& ordered_pos_value = ordered_pos.value();

    nlohmann::json unknown_ledger =
        build_unknown_ledger_base(nir_json, po_list_json, m_config.versions, *tool_obj, *tu_id);

    sappp::certstore::CertStore cert_store(m_config.certstore_dir, m_config.schema_dir);
    const auto function_uid_map = build_function_uid_map(nir_json);

    std::vector<nlohmann::json> unknowns;
    unknowns.reserve(ordered_pos_value.size());

    PoProcessingContext context{.cert_store = &cert_store,
                                .function_uid_map = &function_uid_map,
                                .tu_id = *tu_id,
                                .versions = &m_config.versions};

    for (std::size_t index = 0; index < ordered_pos_value.size(); ++index) {
        const nlohmann::json& po = *ordered_pos_value.at(index);
        auto processed = process_po(po, index, context);
        if (!processed) {
            return std::unexpected(processed.error());
        }
        if (processed->has_unknown) {
            unknowns.push_back(std::move(processed->unknown_entry));
        }
    }

    if (auto ensure_result = ensure_unknowns(unknowns, ordered_pos_value); !ensure_result) {
        return std::unexpected(ensure_result.error());
    }

    std::ranges::stable_sort(unknowns, [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("unknown_stable_id").get<std::string>()
               < b.at("unknown_stable_id").get<std::string>();
    });

    unknown_ledger["unknowns"] = unknowns;

    const std::string schema_path = m_config.schema_dir + "/unknown.v1.schema.json";
    if (auto validation = sappp::common::validate_json(unknown_ledger, schema_path); !validation) {
        return std::unexpected(validation.error());
    }

    return AnalyzeOutput{.unknown_ledger = std::move(unknown_ledger)};
}

}  // namespace sappp::analyzer
