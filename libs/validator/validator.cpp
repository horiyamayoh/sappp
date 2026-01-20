/**
 * @file validator.cpp
 * @brief Certificate validator implementation
 */

#include "sappp/validator.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

namespace sappp::validator {

namespace {

namespace fs = std::filesystem;

struct ValidationError
{
    std::string status;
    std::string reason;
    std::string message;
};

struct ValidationContext
{
    const fs::path* input_dir;
    const std::string* schema_dir;
    bool strict;
};

[[nodiscard]] std::string current_time_rfc3339()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] bool is_hex_lower(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

[[nodiscard]] bool is_sha256_prefixed(std::string_view value)
{
    constexpr std::string_view kPrefix = "sha256:";
    if (!value.starts_with(kPrefix)) {
        return false;
    }
    if (value.size() != kPrefix.size() + 64) {
        return false;
    }
    return std::ranges::all_of(value.substr(kPrefix.size()), is_hex_lower);
}

[[nodiscard]] std::string derive_po_id_from_path(const fs::path& path)
{
    std::string stem = path.stem().string();
    if (is_sha256_prefixed(stem)) {
        return stem;
    }
    return sappp::common::sha256_prefixed(stem);
}

[[nodiscard]] std::string cert_schema_path(std::string_view schema_dir)
{
    return (fs::path(schema_dir) / "cert.v1.schema.json").string();
}

[[nodiscard]] std::string cert_index_schema_path(std::string_view schema_dir)
{
    return (fs::path(schema_dir) / "cert_index.v1.schema.json").string();
}

[[nodiscard]] std::string validated_results_schema_path(std::string_view schema_dir)
{
    return (fs::path(schema_dir) / "validated_results.v1.schema.json").string();
}

[[nodiscard]] sappp::Result<std::string> object_path_for_hash(const fs::path& base_dir,
                                                              const std::string& hash)
{
    constexpr std::string_view kPrefix = "sha256:";
    std::size_t digest_start = hash.starts_with(kPrefix) ? kPrefix.size() : 0;
    if (hash.size() < digest_start + 2) {
        return std::unexpected(Error::make("InvalidHash", "Hash is too short: " + hash));
    }
    std::string shard = hash.substr(digest_start, 2);
    fs::path object_path = base_dir / "certstore" / "objects" / shard / (hash + ".json");
    return object_path.string();
}

[[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(Error::make("IOError", "Failed to open file for read: " + path));
    }
    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    try {
        return nlohmann::json::parse(content);
    } catch (const std::exception& ex) {
        return std::unexpected(
            Error::make("ParseError", "Failed to parse JSON from " + path + ": " + ex.what()));
    }
}

[[nodiscard]] sappp::VoidResult write_json_file(const std::string& path,
                                                const nlohmann::json& payload)
{
    fs::path out_path(path);
    fs::path parent = out_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(Error::make("IOError",
                                               "Failed to create directory: " + parent.string()
                                                   + ": " + ec.message()));
        }
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return std::unexpected(
            Error::make("IOError", "Failed to open file for write: " + out_path.string()));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical;
    if (!out) {
        return std::unexpected(
            Error::make("IOError", "Failed to write file: " + out_path.string()));
    }
    return {};
}

[[nodiscard]] ValidationError make_error(const std::string& status, const std::string& message)
{
    return {.status = status, .reason = status, .message = message};
}

[[nodiscard]] nlohmann::json make_unknown_result(const std::string& po_id,
                                                 const ValidationError& error)
{
    nlohmann::json result = {
        {                "po_id",        po_id},
        {             "category",    "UNKNOWN"},
        {     "validator_status", error.status},
        {"downgrade_reason_code", error.reason}
    };
    if (!error.message.empty()) {
        result["notes"] = error.message;
    }
    return result;
}

[[nodiscard]] nlohmann::json make_validated_result(const std::string& po_id,
                                                   const std::string& category,
                                                   const std::string& certificate_root)
{
    return nlohmann::json{
        {           "po_id",            po_id},
        {        "category",         category},
        {"validator_status",      "Validated"},
        {"certificate_root", certificate_root}
    };
}

[[nodiscard]] ValidationError make_error_from_result(const sappp::Error& error)
{
    return make_error(error.code, error.message);
}

[[nodiscard]] ValidationError version_mismatch_error(const std::string& message);
[[nodiscard]] ValidationError unsupported_error(const std::string& message);
[[nodiscard]] ValidationError proof_failed_error(const std::string& message);
[[nodiscard]] ValidationError rule_violation_error(const std::string& message);
[[nodiscard]] bool is_supported_safety_domain(std::string_view domain);

[[nodiscard]] sappp::Result<nlohmann::json>
load_cert_object(const fs::path& input_dir, std::string_view schema_dir, const std::string& hash)
{
    auto path = object_path_for_hash(input_dir, hash);
    if (!path) {
        return std::unexpected(path.error());
    }

    std::error_code ec;
    if (!fs::exists(*path, ec)) {
        if (ec) {
            return std::unexpected(
                Error::make("IOError",
                            "Failed to stat certificate: " + *path + ": " + ec.message()));
        }
        return std::unexpected(Error::make("MissingDependency", "Missing certificate: " + hash));
    }

    auto cert_result = read_json_file(*path);
    if (!cert_result) {
        return std::unexpected(cert_result.error());
    }

    if (auto result = sappp::common::validate_json(*cert_result, cert_schema_path(schema_dir));
        !result) {
        return std::unexpected(
            Error::make("SchemaInvalid", "Certificate schema invalid: " + result.error().message));
    }

    auto computed_hash = sappp::canonical::hash_canonical(*cert_result);
    if (!computed_hash) {
        return std::unexpected(computed_hash.error());
    }
    if (*computed_hash != hash) {
        return std::unexpected(
            Error::make("HashMismatch",
                        "Certificate hash mismatch: expected " + hash + ", got " + *computed_hash));
    }

    return *cert_result;
}

[[nodiscard]] sappp::Result<nlohmann::json> finish_or_unknown(const std::string& po_id,
                                                              const ValidationError& error,
                                                              const ValidationContext& context)
{
    if (context.strict) {
        return std::unexpected(Error::make(error.reason, error.message));
    }
    return make_unknown_result(po_id, error);
}

[[nodiscard]] sappp::Result<nlohmann::json> load_index_json(const fs::path& index_path,
                                                            std::string_view schema_dir)
{
    auto index_json_result = read_json_file(index_path.string());
    if (!index_json_result) {
        return std::unexpected(index_json_result.error());
    }

    if (auto result =
            sappp::common::validate_json(*index_json_result, cert_index_schema_path(schema_dir));
        !result) {
        return std::unexpected(
            Error::make("SchemaInvalid", "Cert index schema invalid: " + result.error().message));
    }

    return *index_json_result;
}

[[nodiscard]] std::optional<ValidationError> validate_root_header(const nlohmann::json& root)
{
    if (!root.contains("kind") || root.at("kind").get<std::string>() != "ProofRoot") {
        return unsupported_error("Root certificate is not ProofRoot");
    }

    const nlohmann::json& depends = root.at("depends");
    std::string sem_version = depends.at("semantics_version").get<std::string>();
    std::string proof_version = depends.at("proof_system_version").get<std::string>();
    std::string profile_version = depends.at("profile_version").get<std::string>();
    if (sem_version != sappp::kSemanticsVersion || proof_version != sappp::kProofSystemVersion
        || profile_version != sappp::kProfileVersion) {
        return version_mismatch_error("ProofRoot version triple mismatch");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError> validate_po_header(const nlohmann::json& po_cert,
                                                                const std::string& po_id)
{
    if (po_cert.at("kind").get<std::string>() != "PoDef") {
        return rule_violation_error("Po reference is not PoDef");
    }

    std::string po_cert_id = po_cert.at("po").at("po_id").get<std::string>();
    if (po_cert_id != po_id) {
        return rule_violation_error("PoDef po_id mismatch");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError> validate_ir_header(const nlohmann::json& ir_cert)
{
    if (ir_cert.at("kind").get<std::string>() != "IrRef") {
        return rule_violation_error("IR reference is not IrRef");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError> validate_bug_evidence(const std::string& po_id,
                                                                   const nlohmann::json& evidence)
{
    if (evidence.at("kind").get<std::string>() != "BugTrace") {
        return unsupported_error("BUG evidence is not BugTrace");
    }

    const nlohmann::json& violation = evidence.at("violation");
    std::string violation_po_id = violation.at("po_id").get<std::string>();
    bool predicate_holds = violation.at("predicate_holds").get<bool>();
    if (violation_po_id != po_id) {
        return rule_violation_error("BugTrace po_id mismatch");
    }
    if (predicate_holds) {
        return proof_failed_error("BugTrace predicate holds at violation state");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError> validate_contracts(const ValidationContext& context,
                                                                const nlohmann::json& depends)
{
    if (!depends.contains("contracts")) {
        return std::nullopt;
    }

    const auto& contracts = depends.at("contracts");
    for (const auto& contract_ref : contracts) {
        std::string contract_hash = contract_ref.at("ref").get<std::string>();
        auto contract_cert =
            load_cert_object(*context.input_dir, *context.schema_dir, contract_hash);
        if (!contract_cert) {
            return make_error_from_result(contract_cert.error());
        }
        if (contract_cert->at("kind").get<std::string>() != "ContractRef") {
            return rule_violation_error("Contract reference is not ContractRef");
        }

        std::string tier = contract_cert->at("tier").get<std::string>();
        if (tier == "Tier2" || tier == "Disabled") {
            return proof_failed_error("Contract tier not allowed for SAFE: " + tier);
        }
    }

    return std::nullopt;
}

struct PredicateCheck
{
    bool anchor_found = false;
    bool predicate_implied = false;
};

struct PredicateInput
{
    const nlohmann::json* state = nullptr;
    const nlohmann::json* predicate_expr = nullptr;
};

[[nodiscard]] bool json_matches_expected(const nlohmann::json& expected,
                                         const nlohmann::json& actual);

[[nodiscard]] bool predicate_in_state(const PredicateInput& input)
{
    const nlohmann::json& state = *input.state;
    if (!state.contains("predicates") || !state.at("predicates").is_array()) {
        return false;
    }
    const auto& predicates = state.at("predicates");
    return std::ranges::any_of(predicates, [&](const auto& predicate) {
        return json_matches_expected(*input.predicate_expr, predicate);
    });
}

struct PointPredicateInput
{
    const nlohmann::json* point = nullptr;
    const nlohmann::json* predicate_expr = nullptr;
};

[[nodiscard]] bool point_implies_predicate(const PointPredicateInput& input)
{
    const nlohmann::json& point = *input.point;
    if (!point.contains("state")) {
        return false;
    }
    return predicate_in_state(
        {.state = &point.at("state"), .predicate_expr = input.predicate_expr});
}

[[nodiscard]] bool json_matches_expected(const nlohmann::json& expected,
                                         const nlohmann::json& actual)
{
    if (expected.type() != actual.type()) {
        return false;
    }
    if (expected.is_object()) {
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const auto& key = it.key();
            if (!actual.contains(key)) {
                return false;
            }
            if (!json_matches_expected(it.value(), actual.at(key))) {
                return false;
            }
        }
        return true;
    }
    if (expected.is_array()) {
        if (!actual.is_array() || expected.size() != actual.size()) {
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!json_matches_expected(expected.at(i), actual.at(i))) {
                return false;
            }
        }
        return true;
    }
    return expected == actual;
}

struct AbstractStateCheckResult
{
    bool ok = true;
    std::string reason;
};

[[nodiscard]] std::optional<int> infinity_int_value(const nlohmann::json& value)
{
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    return std::nullopt;
}

[[nodiscard]] bool is_inf_value(const nlohmann::json& value, std::string_view expected)
{
    return value.is_string() && value.get<std::string>() == expected;
}

[[nodiscard]] bool interval_is_valid(const nlohmann::json& lo, const nlohmann::json& hi)
{
    if (is_inf_value(lo, "inf") || is_inf_value(hi, "-inf")) {
        return false;
    }
    auto lo_value = infinity_int_value(lo);
    auto hi_value = infinity_int_value(hi);
    if (lo_value && hi_value) {
        return *lo_value <= *hi_value;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> check_duplicate_entries(const nlohmann::json& state,
                                                                 std::string_view field,
                                                                 std::string_view key_field,
                                                                 std::string_view value_field)
{
    if (!state.contains(field)) {
        return std::nullopt;
    }
    if (!state.at(field).is_array()) {
        return std::string(field) + " must be an array";
    }
    std::unordered_map<std::string, std::string> seen;
    for (const auto& entry : state.at(field)) {
        if (!entry.contains(key_field) || !entry.contains(value_field)) {
            return std::string(field) + " entry missing required fields";
        }
        std::string key = entry.at(key_field).get<std::string>();
        std::string value = entry.at(value_field).get<std::string>();
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen.emplace(key, value);
            continue;
        }
        if (it->second != value) {
            return std::string(field) + " has conflicting entries for " + key;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> check_numeric_intervals(const nlohmann::json& state)
{
    if (!state.contains("numeric")) {
        return std::nullopt;
    }
    if (!state.at("numeric").is_array()) {
        return std::string("numeric must be an array");
    }
    std::unordered_map<std::string, std::pair<nlohmann::json, nlohmann::json>> seen;
    for (const auto& entry : state.at("numeric")) {
        if (!entry.contains("var") || !entry.contains("lo") || !entry.contains("hi")) {
            return std::string("numeric entry missing required fields");
        }
        std::string var = entry.at("var").get<std::string>();
        const auto& lo = entry.at("lo");
        const auto& hi = entry.at("hi");
        if (!interval_is_valid(lo, hi)) {
            return std::string("numeric interval is invalid for ") + var;
        }
        auto it = seen.find(var);
        if (it == seen.end()) {
            seen.emplace(var, std::make_pair(lo, hi));
            continue;
        }
        if (it->second.first != lo || it->second.second != hi) {
            return std::string("numeric has conflicting intervals for ") + var;
        }
    }
    return std::nullopt;
}

[[nodiscard]] AbstractStateCheckResult validate_state_consistency(const nlohmann::json& state)
{
    AbstractStateCheckResult result;
    if (!state.is_object()) {
        return {.ok = false, .reason = "State is not an object"};
    }

    if (auto conflict = check_duplicate_entries(state, "nullness", "var", "value")) {
        return {.ok = false, .reason = *conflict};
    }
    if (auto conflict = check_duplicate_entries(state, "lifetime", "obj", "value")) {
        return {.ok = false, .reason = *conflict};
    }
    if (auto conflict = check_duplicate_entries(state, "init", "var", "value")) {
        return {.ok = false, .reason = *conflict};
    }

    if (auto conflict = check_numeric_intervals(state)) {
        return {.ok = false, .reason = *conflict};
    }

    return result;
}

[[nodiscard]] PredicateCheck check_predicate_implied(const nlohmann::json& evidence,
                                                     const std::string& function_uid,
                                                     const std::string& block_id,
                                                     const std::string& inst_id,
                                                     const nlohmann::json& predicate_expr)
{
    PredicateCheck result;
    for (const auto& point : evidence.at("points")) {
        const auto& point_ir = point.at("ir");
        if (point_ir.at("function_uid").get<std::string>() != function_uid) {
            continue;
        }
        if (point_ir.at("block_id").get<std::string>() != block_id) {
            continue;
        }
        if (!point_ir.contains("inst_id") || point_ir.at("inst_id").get<std::string>() != inst_id) {
            continue;
        }
        result.anchor_found = true;
        result.predicate_implied =
            point_implies_predicate({.point = &point, .predicate_expr = &predicate_expr});

        if (result.predicate_implied) {
            break;
        }
    }
    return result;
}

struct SafeEvidenceInputs
{
    const nlohmann::json* depends = nullptr;
    const nlohmann::json* po_cert = nullptr;
    const nlohmann::json* ir_cert = nullptr;
    const nlohmann::json* evidence = nullptr;
};

[[nodiscard]] std::optional<ValidationError> validate_safety_points(const nlohmann::json& evidence)
{
    if (!evidence.contains("points") || !evidence.at("points").is_array()) {
        return proof_failed_error("SafetyProof points missing");
    }
    for (const auto& point : evidence.at("points")) {
        if (!point.contains("state")) {
            return proof_failed_error("SafetyProof point missing state");
        }
        auto check = validate_state_consistency(point.at("state"));
        if (!check.ok) {
            return proof_failed_error("SafetyProof state invalid: " + check.reason);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError>
validate_safe_evidence(const ValidationContext& context, const SafeEvidenceInputs& inputs)
{
    const nlohmann::json& depends = *inputs.depends;
    const nlohmann::json& po_cert = *inputs.po_cert;
    const nlohmann::json& ir_cert = *inputs.ir_cert;
    const nlohmann::json& evidence = *inputs.evidence;

    std::string evidence_kind = evidence.at("kind").get<std::string>();
    if (evidence_kind != "Invariant" && evidence_kind != "SafetyProof") {
        return unsupported_error("SAFE evidence is not SafetyProof/Invariant");
    }

    std::string domain = evidence.at("domain").get<std::string>();
    if (!is_supported_safety_domain(domain)) {
        return unsupported_error("Unsupported SafetyProof domain: " + domain);
    }

    if (auto error = validate_contracts(context, depends)) {
        return error;
    }

    if (auto error = validate_safety_points(evidence)) {
        return error;
    }

    const nlohmann::json& po = po_cert.at("po");
    const nlohmann::json& anchor = po.at("anchor");
    const std::string block_id = anchor.at("block_id").get<std::string>();
    const std::string inst_id = anchor.at("inst_id").get<std::string>();
    const std::string function_uid = ir_cert.at("function_uid").get<std::string>();
    const nlohmann::json& predicate_expr = po.at("predicate").at("expr");

    auto predicate_check =
        check_predicate_implied(evidence, function_uid, block_id, inst_id, predicate_expr);
    if (!predicate_check.anchor_found) {
        return proof_failed_error("SafetyProof missing anchor invariant");
    }
    if (!predicate_check.predicate_implied) {
        return proof_failed_error("SafetyProof does not imply PO predicate");
    }

    return std::nullopt;
}

struct RootRefs
{
    const nlohmann::json* depends = nullptr;
    std::string po_ref;
    std::string ir_ref;
    std::string evidence_ref;
    std::string result_kind;
};

[[nodiscard]] RootRefs extract_root_refs(const nlohmann::json& root)
{
    return RootRefs{.depends = &root.at("depends"),
                    .po_ref = root.at("po").at("ref").get<std::string>(),
                    .ir_ref = root.at("ir").at("ref").get<std::string>(),
                    .evidence_ref = root.at("evidence").at("ref").get<std::string>(),
                    .result_kind = root.at("result").get<std::string>()};
}

struct ResultValidationInputs
{
    const ValidationContext* context = nullptr;
    const std::string* po_id = nullptr;
    const std::string* root_hash = nullptr;
    const std::string* result_kind = nullptr;
    const nlohmann::json* depends = nullptr;
    const nlohmann::json* po_cert = nullptr;
    const nlohmann::json* ir_cert = nullptr;
    const nlohmann::json* evidence_cert = nullptr;
};

[[nodiscard]] sappp::Result<nlohmann::json>
validate_result_kind(const ResultValidationInputs& inputs)
{
    if (*inputs.result_kind == "BUG") {
        if (auto error = validate_bug_evidence(*inputs.po_id, *inputs.evidence_cert)) {
            return finish_or_unknown(*inputs.po_id, *error, *inputs.context);
        }
        return make_validated_result(*inputs.po_id, "BUG", *inputs.root_hash);
    }
    if (*inputs.result_kind == "SAFE") {
        auto safe_inputs = SafeEvidenceInputs{.depends = inputs.depends,
                                              .po_cert = inputs.po_cert,
                                              .ir_cert = inputs.ir_cert,
                                              .evidence = inputs.evidence_cert};
        if (auto error = validate_safe_evidence(*inputs.context, safe_inputs)) {
            return finish_or_unknown(*inputs.po_id, *error, *inputs.context);
        }
        return make_validated_result(*inputs.po_id, "SAFE", *inputs.root_hash);
    }
    return finish_or_unknown(*inputs.po_id,
                             rule_violation_error("ProofRoot result is invalid"),
                             *inputs.context);
}

[[nodiscard]] sappp::Result<std::vector<fs::path>> collect_index_files(const fs::path& index_dir)
{
    std::error_code ec;
    if (!fs::exists(index_dir, ec)) {
        if (ec) {
            return std::unexpected(Error::make("IOError",
                                               "Failed to stat certstore index directory: "
                                                   + index_dir.string() + ": " + ec.message()));
        }
        return std::unexpected(
            Error::make("MissingDependency",
                        "certstore index directory not found: " + index_dir.string()));
    }

    std::vector<fs::path> index_files;
    for (fs::directory_iterator it(index_dir, ec); it != fs::directory_iterator();
         it.increment(ec)) {
        if (ec) {
            return std::unexpected(
                Error::make("IOError",
                            "Failed to read certstore index directory: " + ec.message()));
        }
        const auto& entry = *it;
        std::error_code entry_ec;
        bool is_regular = entry.is_regular_file(entry_ec);
        if (entry_ec) {
            return std::unexpected(
                Error::make("IOError",
                            "Failed to stat index entry: " + entry.path().string() + ": "
                                + entry_ec.message()));
        }
        if (!is_regular) {
            continue;
        }
        if (entry.path().extension() == ".json") {
            index_files.push_back(entry.path());
        }
    }
    if (ec) {
        return std::unexpected(
            Error::make("IOError", "Failed to read certstore index directory: " + ec.message()));
    }

    std::ranges::sort(index_files);
    return index_files;
}

[[nodiscard]] sappp::Result<nlohmann::json> validate_index_entry(const ValidationContext& context,
                                                                 const fs::path& index_path,
                                                                 std::string& tu_id)
{
    auto index_json = load_index_json(index_path, *context.schema_dir);
    if (!index_json) {
        std::string fallback_po_id = derive_po_id_from_path(index_path);
        return finish_or_unknown(fallback_po_id,
                                 make_error_from_result(index_json.error()),
                                 context);
    }

    std::string po_id = index_json->at("po_id").get<std::string>();
    std::string root_hash = index_json->at("root").get<std::string>();

    auto root_cert = load_cert_object(*context.input_dir, *context.schema_dir, root_hash);
    if (!root_cert) {
        return finish_or_unknown(po_id, make_error_from_result(root_cert.error()), context);
    }

    const nlohmann::json& root = *root_cert;
    if (auto error = validate_root_header(root)) {
        return finish_or_unknown(po_id, *error, context);
    }

    auto root_refs = extract_root_refs(root);

    auto po_cert = load_cert_object(*context.input_dir, *context.schema_dir, root_refs.po_ref);
    if (!po_cert) {
        return finish_or_unknown(po_id, make_error_from_result(po_cert.error()), context);
    }
    if (auto error = validate_po_header(*po_cert, po_id)) {
        return finish_or_unknown(po_id, *error, context);
    }

    auto ir_cert = load_cert_object(*context.input_dir, *context.schema_dir, root_refs.ir_ref);
    if (!ir_cert) {
        return finish_or_unknown(po_id, make_error_from_result(ir_cert.error()), context);
    }
    if (auto error = validate_ir_header(*ir_cert)) {
        return finish_or_unknown(po_id, *error, context);
    }
    if (tu_id.empty()) {
        tu_id = ir_cert->at("tu_id").get<std::string>();
    }

    auto evidence_cert =
        load_cert_object(*context.input_dir, *context.schema_dir, root_refs.evidence_ref);
    if (!evidence_cert) {
        return finish_or_unknown(po_id, make_error_from_result(evidence_cert.error()), context);
    }

    auto result_inputs = ResultValidationInputs{.context = &context,
                                                .po_id = &po_id,
                                                .root_hash = &root_hash,
                                                .result_kind = &root_refs.result_kind,
                                                .depends = root_refs.depends,
                                                .po_cert = &(*po_cert),
                                                .ir_cert = &(*ir_cert),
                                                .evidence_cert = &(*evidence_cert)};
    return validate_result_kind(result_inputs);
}

ValidationError version_mismatch_error(const std::string& message)
{
    return {.status = "VersionMismatch", .reason = "VersionMismatch", .message = message};
}

ValidationError unsupported_error(const std::string& message)
{
    return {.status = "UnsupportedProofFeature",
            .reason = "UnsupportedProofFeature",
            .message = message};
}

ValidationError proof_failed_error(const std::string& message)
{
    return {.status = "ProofCheckFailed", .reason = "ProofCheckFailed", .message = message};
}

ValidationError rule_violation_error(const std::string& message)
{
    return {.status = "RuleViolation", .reason = "RuleViolation", .message = message};
}

[[nodiscard]] bool is_supported_safety_domain(std::string_view domain)
{
    return domain == "interval+null+lifetime+init";
}

}  // namespace

Validator::Validator(std::string input_dir, std::string schema_dir, sappp::VersionTriple versions)
    : m_input_dir(std::move(input_dir))
    , m_schema_dir(std::move(schema_dir))
    , m_versions(std::move(versions))
{}

sappp::Result<nlohmann::json> Validator::validate(bool strict)
{
    fs::path index_dir = fs::path(m_input_dir) / "certstore" / "index";
    auto index_files = collect_index_files(index_dir);
    if (!index_files) {
        return std::unexpected(index_files.error());
    }

    fs::path input_dir(m_input_dir);
    ValidationContext context{.input_dir = &input_dir,
                              .schema_dir = &m_schema_dir,
                              .strict = strict};
    std::vector<nlohmann::json> results;
    results.reserve(index_files->size());
    std::string tu_id;

    for (const auto& index_path : *index_files) {
        auto result = validate_index_entry(context, index_path, tu_id);
        if (!result) {
            return std::unexpected(result.error());
        }
        results.push_back(std::move(*result));
    }

    if (results.empty()) {
        return std::unexpected(
            Error::make("MissingDependency", "No certificate index entries found"));
    }

    std::ranges::stable_sort(results, [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
    });

    if (tu_id.empty()) {
        return std::unexpected(
            Error::make("RuleViolation", "Failed to determine tu_id from IR references"));
    }

    nlohmann::json output = {
        {      "schema_version",                                                           "validated_results.v1"},
        {                "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {        "generated_at",                                                           current_time_rfc3339()},
        {               "tu_id",                                                                            tu_id},
        {             "results",                                                                          results},
        {   "semantics_version",                                                             m_versions.semantics},
        {"proof_system_version",                                                          m_versions.proof_system},
        {     "profile_version",                                                               m_versions.profile}
    };

    if (auto result =
            sappp::common::validate_json(output, validated_results_schema_path(m_schema_dir));
        !result) {
        return std::unexpected(
            Error::make("SchemaInvalid",
                        "Validated results schema invalid: " + result.error().message));
    }

    return output;
}

sappp::VoidResult Validator::write_results(const nlohmann::json& results,
                                           const std::string& output_path) const
{
    if (auto result =
            sappp::common::validate_json(results, validated_results_schema_path(m_schema_dir));
        !result) {
        return std::unexpected(
            Error::make("SchemaInvalid",
                        "Validated results schema invalid: " + result.error().message));
    }
    return write_json_file(output_path, results);
}

}  // namespace sappp::validator
