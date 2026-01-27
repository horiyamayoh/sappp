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
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
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

struct NirInstruction
{
    std::string op;
    std::size_t index;
};

struct NirBlock
{
    std::unordered_map<std::string, NirInstruction> insts;

    NirBlock()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : insts()
    {}
};

struct NirEdge
{
    std::string to;
    std::string kind;
};

struct NirFunction
{
    std::unordered_map<std::string, NirBlock> blocks;
    std::unordered_map<std::string, std::vector<NirEdge>> edges;
    std::string entry_block;

    NirFunction()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : blocks()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , edges()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , entry_block()
    {}
};

struct NirIndex
{
    std::unordered_map<std::string, NirFunction> functions;
    std::string tu_id;

    NirIndex()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : functions()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , tu_id()
    {}
};

struct NirContext
{
    std::optional<NirIndex> index;
    std::optional<ValidationError> error;

    NirContext()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : index()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , error()
    {}
};

struct ValidationContext
{
    const fs::path* input_dir;
    const std::string* schema_dir;
    const NirContext* nir_context;
    bool strict;
};

constexpr std::string_view kDeterministicGeneratedAt = "1970-01-01T00:00:00Z";

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

[[nodiscard]] std::string nir_schema_path(std::string_view schema_dir)
{
    return (fs::path(schema_dir) / "nir.v1.schema.json").string();
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

[[nodiscard]] std::optional<std::string> read_generated_at_from(const fs::path& path)
{
    auto json = read_json_file(path.string());
    if (!json) {
        return std::nullopt;
    }
    if (!json->contains("generated_at") || !(*json)["generated_at"].is_string()) {
        return std::nullopt;
    }
    return (*json)["generated_at"].get<std::string>();
}

[[nodiscard]] std::string pick_generated_at(const fs::path& input_dir)
{
    const std::vector<fs::path> candidates = {
        input_dir / "config" / "analysis_config.json",
        input_dir / "frontend" / "nir.json",
        input_dir / "po" / "po_list.json",
        input_dir / "build_snapshot.json",
    };
    for (const auto& path : candidates) {
        if (auto generated_at = read_generated_at_from(path); generated_at) {
            return *generated_at;
        }
    }
    return std::string(kDeterministicGeneratedAt);
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
[[nodiscard]] bool is_supported_bug_trace_op(std::string_view op);
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

[[nodiscard]] sappp::Result<std::pair<std::string, NirBlock>>
build_nir_block_entry(const nlohmann::json& block_json)
{
    std::string block_id = block_json.at("id").get<std::string>();
    NirBlock block_index;
    std::size_t inst_index = 0;
    for (const auto& inst_json : block_json.at("insts")) {
        std::string inst_id = inst_json.at("id").get<std::string>();
        std::string op = inst_json.at("op").get<std::string>();
        if (block_index.insts.contains(inst_id)) {
            return std::unexpected(
                Error::make("NirInvalid", "Duplicate inst_id in NIR: " + inst_id));
        }
        block_index.insts.emplace(std::move(inst_id),
                                  NirInstruction{.op = std::move(op), .index = inst_index});
        ++inst_index;
    }
    return std::make_pair(std::move(block_id), std::move(block_index));
}

[[nodiscard]] sappp::VoidResult add_nir_edges(const nlohmann::json& cfg,
                                              NirFunction& function_index)
{
    for (const auto& edge_json : cfg.at("edges")) {
        std::string from = edge_json.at("from").get<std::string>();
        std::string to = edge_json.at("to").get<std::string>();
        std::string kind = edge_json.at("kind").get<std::string>();
        if (!function_index.blocks.contains(from) || !function_index.blocks.contains(to)) {
            std::string message = "NIR edge references missing block: ";
            message += from;
            message += " -> ";
            message += to;
            return std::unexpected(Error::make("NirInvalid", message));
        }
        function_index.edges[from].push_back(NirEdge{.to = std::move(to), .kind = std::move(kind)});
    }
    return {};
}

[[nodiscard]] sappp::Result<NirFunction> build_nir_function(const nlohmann::json& function_json)
{
    NirFunction function_index;
    const auto& cfg = function_json.at("cfg");
    if (!cfg.contains("entry") || !cfg.at("entry").is_string()) {
        return std::unexpected(Error::make("NirInvalid", "Missing cfg.entry in NIR"));
    }
    function_index.entry_block = cfg.at("entry").get<std::string>();
    const auto& blocks = cfg.at("blocks");
    for (const auto& block_json : blocks) {
        auto block_entry = build_nir_block_entry(block_json);
        if (!block_entry) {
            return std::unexpected(block_entry.error());
        }
        auto [block_id, block_index] = std::move(*block_entry);
        if (function_index.blocks.contains(block_id)) {
            return std::unexpected(
                Error::make("NirInvalid", "Duplicate block_id in NIR: " + block_id));
        }
        function_index.blocks.emplace(std::move(block_id), std::move(block_index));
    }
    if (!function_index.blocks.contains(function_index.entry_block)) {
        return std::unexpected(
            Error::make("NirInvalid", "cfg.entry does not match any block in NIR"));
    }

    if (auto edge_result = add_nir_edges(cfg, function_index); !edge_result) {
        return std::unexpected(edge_result.error());
    }

    return function_index;
}

[[nodiscard]] sappp::Result<NirIndex> build_nir_index(const nlohmann::json& nir_json)
{
    NirIndex index;
    index.tu_id = nir_json.at("tu_id").get<std::string>();

    const auto& functions = nir_json.at("functions");
    if (!functions.is_array()) {
        return std::unexpected(Error::make("NirInvalid", "NIR functions field missing or invalid"));
    }

    for (const auto& function_json : functions) {
        std::string function_uid = function_json.at("function_uid").get<std::string>();
        if (index.functions.contains(function_uid)) {
            return std::unexpected(
                Error::make("NirInvalid", "Duplicate function_uid in NIR: " + function_uid));
        }
        auto function_index = build_nir_function(function_json);
        if (!function_index) {
            return std::unexpected(function_index.error());
        }
        index.functions.emplace(std::move(function_uid), std::move(*function_index));
    }

    return index;
}

[[nodiscard]] sappp::Result<NirIndex> load_nir_index(const fs::path& input_dir,
                                                     std::string_view schema_dir)
{
    fs::path nir_path = input_dir / "frontend" / "nir.json";
    std::error_code ec;
    if (!fs::exists(nir_path, ec)) {
        if (ec) {
            return std::unexpected(
                Error::make("IOError",
                            "Failed to stat NIR file: " + nir_path.string() + ": " + ec.message()));
        }
        return std::unexpected(
            Error::make("MissingDependency", "NIR file not found: " + nir_path.string()));
    }

    auto nir_json_result = read_json_file(nir_path.string());
    if (!nir_json_result) {
        return std::unexpected(nir_json_result.error());
    }

    if (auto result = sappp::common::validate_json(*nir_json_result, nir_schema_path(schema_dir));
        !result) {
        return std::unexpected(
            Error::make("SchemaInvalid", "NIR schema invalid: " + result.error().message));
    }

    return build_nir_index(*nir_json_result);
}

[[nodiscard]] const NirFunction* find_function(const NirIndex& index,
                                               const std::string& function_uid)
{
    auto it = index.functions.find(function_uid);
    if (it == index.functions.end()) {
        return nullptr;
    }
    return &it->second;
}

[[nodiscard]] const NirBlock* find_block(const NirFunction& function, const std::string& block_id)
{
    auto it = function.blocks.find(block_id);
    if (it == function.blocks.end()) {
        return nullptr;
    }
    return &it->second;
}

[[nodiscard]] const NirInstruction* find_instruction(const NirBlock& block,
                                                     const std::string& inst_id)
{
    auto it = block.insts.find(inst_id);
    if (it == block.insts.end()) {
        return nullptr;
    }
    return &it->second;
}

struct EdgeLookup
{
    std::string from;
    std::string to;
};

[[nodiscard]] bool has_cfg_edge(const NirFunction& function,
                                const EdgeLookup& lookup,
                                const std::optional<std::string>& edge_kind)
{
    auto it = function.edges.find(lookup.from);
    if (it == function.edges.end()) {
        return false;
    }
    return std::ranges::any_of(it->second, [&](const NirEdge& edge) noexcept {
        if (edge.to != lookup.to) {
            return false;
        }
        return !edge_kind || edge.kind == *edge_kind;
    });
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

struct TraceStepInfo
{
    const NirFunction*
        function;  // NOLINT(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
                   // - required for -Weffc++.
    std::string function_uid;
    std::string block_id;
    std::string inst_id;
    std::string inst_op;
    std::size_t
        inst_index;  // NOLINT(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
                     // - required for -Weffc++.
    std::optional<std::string> edge_kind;
    bool is_entry_block = false;

    // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
    TraceStepInfo()
        : function(nullptr)
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , function_uid()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , block_id()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , inst_id()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , inst_op()
        , inst_index(0)
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , edge_kind()
    {}

    TraceStepInfo(const TraceStepInfo&) = default;
    TraceStepInfo& operator=(const TraceStepInfo&) = default;
    TraceStepInfo(TraceStepInfo&&) = default;
    TraceStepInfo& operator=(TraceStepInfo&&) = default;
    ~TraceStepInfo() = default;
};

struct TraceExpectations
{
    std::string_view tu_id;
};

struct CallFrame
{
    std::string function_uid;
    std::string block_id;
    std::size_t inst_index;
};

[[nodiscard]] bool is_call_transition_op(std::string_view op)
{
    return op == "call" || op == "invoke" || op == "vcall" || op == "ctor";
}

[[nodiscard]] bool is_return_transition_op(std::string_view op)
{
    return op == "ret";
}

[[nodiscard]] bool is_unwind_transition_op(std::string_view op)
{
    return op == "throw" || op == "resume";
}

[[nodiscard]] std::optional<ValidationError> resolve_nir_index(const ValidationContext& context,
                                                               const NirIndex*& nir_index)
{
    if (context.nir_context == nullptr) {
        return unsupported_error("BugTrace validation missing NIR context");
    }
    if (!context.nir_context->index) {
        if (context.nir_context->error) {
            return context.nir_context->error;
        }
        return unsupported_error("BugTrace validation missing NIR index");
    }
    nir_index = &*context.nir_context->index;
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError>
build_trace_step_info(const NirIndex& nir_index,
                      const TraceExpectations& expected,
                      const nlohmann::json& step,
                      TraceStepInfo& info)
{
    const nlohmann::json& ir = step.at("ir");
    std::string tu_id = ir.at("tu_id").get<std::string>();
    if (tu_id != expected.tu_id) {
        return rule_violation_error("BugTrace tu_id mismatch");
    }
    std::string function_uid = ir.at("function_uid").get<std::string>();

    std::string block_id = ir.at("block_id").get<std::string>();
    std::string inst_id = ir.at("inst_id").get<std::string>();

    const NirFunction* function = find_function(nir_index, function_uid);
    if (function == nullptr) {
        return rule_violation_error("BugTrace function not found in NIR");
    }
    const NirBlock* block = find_block(*function, block_id);
    if (block == nullptr) {
        return rule_violation_error("BugTrace block not found in NIR");
    }
    const NirInstruction* inst = find_instruction(*block, inst_id);
    if (inst == nullptr) {
        return rule_violation_error("BugTrace instruction not found in NIR");
    }
    if (!is_supported_bug_trace_op(inst->op)) {
        return unsupported_error("BugTrace op not supported: " + inst->op);
    }

    std::optional<std::string> edge_kind;
    if (step.contains("edge_kind")) {
        edge_kind = step.at("edge_kind").get<std::string>();
    }

    info.function = function;
    info.function_uid = std::move(function_uid);
    info.block_id = std::move(block_id);
    info.inst_id = std::move(inst_id);
    info.inst_op = inst->op;
    info.inst_index = inst->index;
    info.edge_kind = std::move(edge_kind);
    info.is_entry_block = info.block_id == function->entry_block;
    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError>
// NOLINTNEXTLINE(readability-function-size) - Trace transition validation.
validate_trace_transition(const TraceStepInfo& previous,
                          const TraceStepInfo& current,
                          std::vector<CallFrame>& call_stack)
{
    if (current.function_uid == previous.function_uid) {
        if (current.block_id == previous.block_id) {
            if (current.inst_index < previous.inst_index) {
                return proof_failed_error("BugTrace instruction order is not monotonic");
            }
            return std::nullopt;
        }
        EdgeLookup lookup{.from = previous.block_id, .to = current.block_id};
        if (!has_cfg_edge(*current.function, lookup, current.edge_kind)) {
            return proof_failed_error("BugTrace path is not connected in CFG");
        }
        return std::nullopt;
    }

    if (is_call_transition_op(previous.inst_op)) {
        if (!current.is_entry_block) {
            return proof_failed_error("BugTrace call enters non-entry block");
        }
        call_stack.push_back(CallFrame{.function_uid = previous.function_uid,
                                       .block_id = previous.block_id,
                                       .inst_index = previous.inst_index});
        return std::nullopt;
    }

    if (is_return_transition_op(previous.inst_op)) {
        if (call_stack.empty()) {
            return proof_failed_error("BugTrace return without call frame");
        }
        const CallFrame& frame = call_stack.back();
        if (frame.function_uid != current.function_uid) {
            return proof_failed_error("BugTrace return target mismatch");
        }
        if (current.block_id == frame.block_id && current.inst_index < frame.inst_index) {
            return proof_failed_error("BugTrace return goes backwards in caller");
        }
        call_stack.pop_back();
        return std::nullopt;
    }

    if (is_unwind_transition_op(previous.inst_op)) {
        if (call_stack.empty()) {
            return proof_failed_error("BugTrace unwind without call frame");
        }
        auto reversed_call_stack = call_stack | std::views::reverse;
        auto match_it = std::ranges::find_if(reversed_call_stack, [&](const CallFrame& frame) {
            return frame.function_uid == current.function_uid;
        });
        if (match_it == std::ranges::end(reversed_call_stack)) {
            return proof_failed_error("BugTrace unwind target mismatch");
        }
        while (!call_stack.empty() && call_stack.back().function_uid != current.function_uid) {
            call_stack.pop_back();
        }
        if (call_stack.empty()) {
            return proof_failed_error("BugTrace unwind target missing");
        }
        if (current.block_id == call_stack.back().block_id
            && current.inst_index < call_stack.back().inst_index) {
            return proof_failed_error("BugTrace unwind goes backwards in caller");
        }
        call_stack.pop_back();
        return std::nullopt;
    }

    return unsupported_error("BugTrace function transition op not supported");
}

[[nodiscard]] std::optional<ValidationError>
validate_bug_trace_path(const ValidationContext& context,
                        const nlohmann::json& ir_cert,
                        const nlohmann::json& evidence)
{
    const NirIndex* nir_index = nullptr;
    if (auto error = resolve_nir_index(context, nir_index)) {
        return error;
    }

    std::string expected_tu_id = ir_cert.at("tu_id").get<std::string>();

    if (nir_index->tu_id != expected_tu_id) {
        return rule_violation_error("NIR tu_id does not match IR reference");
    }

    if (!evidence.contains("steps") || !evidence.at("steps").is_array()) {
        return rule_violation_error("BugTrace steps missing");
    }

    TraceExpectations expected{.tu_id = expected_tu_id};
    std::vector<CallFrame> call_stack;
    std::optional<TraceStepInfo> previous;
    for (const auto& step : evidence.at("steps")) {
        TraceStepInfo current;
        if (auto error = build_trace_step_info(*nir_index, expected, step, current)) {
            return error;
        }
        if (previous) {
            if (auto error = validate_trace_transition(*previous, current, call_stack)) {
                return error;
            }
        }
        previous = std::move(current);
    }

    if (!previous) {
        return rule_violation_error("BugTrace steps missing");
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ValidationError> validate_bug_evidence(const ValidationContext& context,
                                                                   const nlohmann::json& ir_cert,
                                                                   const std::string& po_id,
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

    if (auto error = validate_bug_trace_path(context, ir_cert, evidence)) {
        return error;
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
    bool ok;
    std::string reason;
};

[[nodiscard]] std::optional<std::int64_t> infinity_int_value(const nlohmann::json& value)
{
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_unsigned()) {
        auto unsigned_value = value.get<std::uint64_t>();
        if (unsigned_value
            <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(unsigned_value);
        }
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
    if ((lo.is_number_integer() || lo.is_number_unsigned()) && !lo_value) {
        return false;
    }
    if ((hi.is_number_integer() || hi.is_number_unsigned()) && !hi_value) {
        return false;
    }
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

// NOLINTNEXTLINE(readability-function-size) - Validation branches are explicit for clarity.
[[nodiscard]] std::optional<std::string> check_points_to_entries(const nlohmann::json& state)
{
    if (!state.contains("points_to")) {
        return std::nullopt;
    }
    if (!state.at("points_to").is_array()) {
        return std::string("points_to must be an array");
    }
    struct PointsToTarget
    {
        std::string alloc_site;
        std::string field;

        bool operator==(const PointsToTarget&) const = default;
    };

    auto target_less = [](const PointsToTarget& a, const PointsToTarget& b) {
        if (a.alloc_site != b.alloc_site) {
            return a.alloc_site < b.alloc_site;
        }
        return a.field < b.field;
    };

    std::unordered_map<std::string, std::vector<PointsToTarget>> seen;
    for (const auto& entry : state.at("points_to")) {
        if (!entry.contains("ptr") || !entry.contains("targets")) {
            return std::string("points_to entry missing required fields");
        }
        if (!entry.at("ptr").is_string() || !entry.at("targets").is_array()) {
            return std::string("points_to entry has invalid field types");
        }
        std::string ptr = entry.at("ptr").get<std::string>();
        std::vector<PointsToTarget> targets;
        for (const auto& target : entry.at("targets")) {
            if (!target.is_object() || !target.contains("alloc_site")
                || !target.contains("field")) {
                return std::string("points_to targets must include alloc_site and field");
            }
            if (!target.at("alloc_site").is_string() || !target.at("field").is_string()) {
                return std::string("points_to targets must include string alloc_site/field");
            }
            targets.push_back(PointsToTarget{
                .alloc_site = target.at("alloc_site").get<std::string>(),
                .field = target.at("field").get<std::string>(),
            });
        }
        std::ranges::sort(targets, target_less);
        auto unique_end = std::ranges::unique(targets);
        targets.erase(unique_end.begin(), targets.end());

        auto it = seen.find(ptr);
        if (it == seen.end()) {
            seen.emplace(std::move(ptr), std::move(targets));
            continue;
        }
        if (it->second != targets) {
            return std::string("points_to has conflicting targets for ") + ptr;
        }
    }
    return std::nullopt;
}

[[nodiscard]] AbstractStateCheckResult validate_state_consistency(const nlohmann::json& state)
{
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
    if (auto conflict = check_points_to_entries(state)) {
        return {.ok = false, .reason = *conflict};
    }

    return {.ok = true, .reason = ""};
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
        if (auto error = validate_bug_evidence(*inputs.context,
                                               *inputs.ir_cert,
                                               *inputs.po_id,
                                               *inputs.evidence_cert)) {
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

[[nodiscard]] std::optional<ValidationError>
check_tu_id_consistency(std::string_view entry_tu_id,
                        std::optional<std::string>& tu_id,
                        const std::optional<std::string>& expected_tu_id)
{
    if (expected_tu_id && entry_tu_id != *expected_tu_id) {
        return rule_violation_error("IR tu_id mismatch: expected " + *expected_tu_id + ", got "
                                    + std::string(entry_tu_id));
    }
    if (!tu_id) {
        tu_id = std::string(entry_tu_id);
        return std::nullopt;
    }
    if (*tu_id != entry_tu_id) {
        return rule_violation_error("IR tu_id mismatch across certs: expected " + *tu_id + ", got "
                                    + std::string(entry_tu_id));
    }
    return std::nullopt;
}

[[nodiscard]] sappp::Result<nlohmann::json>
validate_index_entry(const ValidationContext& context,
                     const fs::path& index_path,
                     std::optional<std::string>& tu_id,
                     const std::optional<std::string>& expected_tu_id)
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
    std::string entry_tu_id = ir_cert->at("tu_id").get<std::string>();
    if (auto error = check_tu_id_consistency(entry_tu_id, tu_id, expected_tu_id)) {
        return finish_or_unknown(po_id, *error, context);
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

[[nodiscard]] bool is_supported_bug_trace_op(std::string_view op)
{
    constexpr std::array<std::string_view, 21> kSupportedOps{
        {
         "alloc",  "assign",      "branch",         "call",         "ctor",  "dtor",     "free",
         "invoke", "landingpad",  "lifetime.begin", "lifetime.end", "load",  "move",     "ret",
         "resume", "sink.marker", "stmt",           "store",        "throw", "ub.check", "vcall",
         }
    };
    return std::ranges::find(kSupportedOps, op) != kSupportedOps.end();
}

[[nodiscard]] bool is_supported_safety_domain(std::string_view domain)
{
    constexpr std::string_view kBaseDomain = "interval+null+lifetime+init";
    return domain == kBaseDomain || domain == "interval+null+lifetime+init+points-to.simple"
           || domain == "interval+null+lifetime+init+points-to.context";
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
    NirContext nir_context;
    auto nir_index = load_nir_index(input_dir, m_schema_dir);
    if (nir_index) {
        nir_context.index = std::move(*nir_index);
    } else {
        nir_context.error = make_error_from_result(nir_index.error());
    }

    ValidationContext context{.input_dir = &input_dir,
                              .schema_dir = &m_schema_dir,
                              .nir_context = &nir_context,
                              .strict = strict};
    std::vector<nlohmann::json> results;
    results.reserve(index_files->size());
    std::optional<std::string> tu_id;
    std::optional<std::string> expected_tu_id;
    if (nir_context.index) {
        expected_tu_id = nir_context.index->tu_id;
        tu_id = expected_tu_id;
    }

    for (const auto& index_path : *index_files) {
        auto result = validate_index_entry(context, index_path, tu_id, expected_tu_id);
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

    if (!tu_id) {
        return std::unexpected(
            Error::make("RuleViolation", "Failed to determine tu_id from IR references"));
    }

    const std::string generated_at = pick_generated_at(input_dir);
    nlohmann::json output = {
        {      "schema_version",                                                           "validated_results.v1"},
        {                "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {        "generated_at",                                                                     generated_at},
        {               "tu_id",                                                                           *tu_id},
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
