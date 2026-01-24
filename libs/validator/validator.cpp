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
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sappp::validator {

namespace {

namespace fs = std::filesystem;

struct ValidationError {
    std::string status;
    std::string reason;
    std::string message;
};

struct InstInfo {
    std::size_t index = 0;
    std::string op{};
};

struct BlockInfo {
    std::unordered_map<std::string, InstInfo> insts{};
};

struct EdgeInfo {
    std::string to{};
    std::string kind{};
};

struct FunctionInfo {
    std::string entry{};
    std::unordered_map<std::string, BlockInfo> blocks{};
    std::unordered_map<std::string, std::vector<EdgeInfo>> edges{};
};

struct NirIndex {
    std::string tu_id{};
    std::unordered_map<std::string, FunctionInfo> functions{};
};

struct TraceStep {
    std::string function_uid{};
    std::string block_id{};
    std::string inst_id{};
    std::string op{};
    std::size_t inst_index = 0;
    std::optional<std::string> edge_kind{};
};

struct CallSite {
    std::string block_id{};
    std::string inst_id{};
    std::size_t inst_index = 0;
};

struct CallFrame {
    std::string function_uid{};
    std::optional<CallSite> call_site{};
};

[[nodiscard]] std::string current_time_rfc3339() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] bool is_hex_lower(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

[[nodiscard]] bool is_sha256_prefixed(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix)) {
        return false;
    }
    if (value.size() != prefix.size() + 64) {
        return false;
    }
    return std::ranges::all_of(value.substr(prefix.size()), is_hex_lower);
}

[[nodiscard]] std::string derive_po_id_from_path(const fs::path& path) {
    std::string stem = path.stem().string();
    if (is_sha256_prefixed(stem)) {
        return stem;
    }
    return sappp::common::sha256_prefixed(stem);
}

[[nodiscard]] std::string cert_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "cert.v1.schema.json").string();
}

[[nodiscard]] std::string cert_index_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "cert_index.v1.schema.json").string();
}

[[nodiscard]] std::string nir_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "nir.v1.schema.json").string();
}

[[nodiscard]] std::string validated_results_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "validated_results.v1.schema.json").string();
}

[[nodiscard]] sappp::Result<std::string> object_path_for_hash(const fs::path& base_dir, const std::string& hash) {
    constexpr std::string_view prefix = "sha256:";
    std::size_t digest_start = hash.starts_with(prefix) ? prefix.size() : 0;
    if (hash.size() < digest_start + 2) {
        return std::unexpected(Error::make("InvalidHash",
            "Hash is too short: " + hash));
    }
    std::string shard = hash.substr(digest_start, 2);
    fs::path object_path = base_dir / "certstore" / "objects" / shard / (hash + ".json");
    return object_path.string();
}

[[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(Error::make("IOError",
            "Failed to open file for read: " + path));
    }
    std::string content{std::istreambuf_iterator<char>{in},
                        std::istreambuf_iterator<char>{}};
    try {
        return nlohmann::json::parse(content);
    } catch (const std::exception& ex) {
        return std::unexpected(Error::make("ParseError",
            "Failed to parse JSON from " + path + ": " + ex.what()));
    }
}

[[nodiscard]] sappp::VoidResult write_json_file(const std::string& path, const nlohmann::json& payload) {
    fs::path out_path(path);
    fs::path parent = out_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(Error::make("IOError",
                "Failed to create directory: " + parent.string() + ": " + ec.message()));
        }
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return std::unexpected(Error::make("IOError",
            "Failed to open file for write: " + out_path.string()));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical;
    if (!out) {
        return std::unexpected(Error::make("IOError",
            "Failed to write file: " + out_path.string()));
    }
    return {};
}

[[nodiscard]] ValidationError make_error(const std::string& status, const std::string& message) {
    return {status, status, message};
}

[[nodiscard]] nlohmann::json make_unknown_result(const std::string& po_id,
                                  const ValidationError& error) {
    nlohmann::json result = {
        {"po_id", po_id},
        {"category", "UNKNOWN"},
        {"validator_status", error.status},
        {"downgrade_reason_code", error.reason}
    };
    if (!error.message.empty()) {
        result["notes"] = error.message;
    }
    return result;
}

[[nodiscard]] nlohmann::json make_validated_result(const std::string& po_id,
                                     const std::string& category,
                                     const std::string& certificate_root) {
    return nlohmann::json{
        {"po_id", po_id},
        {"category", category},
        {"validator_status", "Validated"},
        {"certificate_root", certificate_root}
    };
}

[[nodiscard]] ValidationError make_error_from_result(const sappp::Error& error) {
    return make_error(error.code, error.message);
}

[[nodiscard]] sappp::Result<nlohmann::json> load_cert_object(const std::string& input_dir,
                                               const std::string& schema_dir,
                                               const std::string& hash) {
    auto path = object_path_for_hash(input_dir, hash);
    if (!path) {
        return std::unexpected(path.error());
    }

    std::error_code ec;
    if (!fs::exists(*path, ec)) {
        if (ec) {
            return std::unexpected(Error::make("IOError",
                "Failed to stat certificate: " + *path + ": " + ec.message()));
        }
        return std::unexpected(Error::make("MissingDependency",
            "Missing certificate: " + hash));
    }

    auto cert_result = read_json_file(*path);
    if (!cert_result) {
        return std::unexpected(cert_result.error());
    }

    if (auto result = sappp::common::validate_json(*cert_result, cert_schema_path(schema_dir)); !result) {
        return std::unexpected(Error::make("SchemaInvalid",
            "Certificate schema invalid: " + result.error().message));
    }

    auto computed_hash = sappp::canonical::hash_canonical(*cert_result);
    if (!computed_hash) {
        return std::unexpected(computed_hash.error());
    }
    if (*computed_hash != hash) {
        return std::unexpected(Error::make("HashMismatch",
            "Certificate hash mismatch: expected " + hash + ", got " + *computed_hash));
    }

    return *cert_result;
}

ValidationError version_mismatch_error(const std::string& message) {
    return {"VersionMismatch", "VersionMismatch", message};
}

ValidationError unsupported_error(const std::string& message) {
    return {"UnsupportedProofFeature", "UnsupportedProofFeature", message};
}

ValidationError proof_failed_error(const std::string& message) {
    return {"ProofCheckFailed", "ProofCheckFailed", message};
}

ValidationError rule_violation_error(const std::string& message) {
    return {"RuleViolation", "RuleViolation", message};
}

[[nodiscard]] bool edge_kind_matches(std::string_view expected, std::string_view actual) {
    if (expected == actual) {
        return true;
    }
    if (expected == "exception") {
        return actual.starts_with("exception");
    }
    return false;
}

[[nodiscard]] bool is_exception_edge_kind(std::string_view kind) {
    return kind.starts_with("exception");
}

[[nodiscard]] bool is_call_op(std::string_view op) {
    return op == "call" || op == "invoke" || op == "vcall";
}

[[nodiscard]] bool is_interprocedural_kind(std::string_view kind) {
    return kind == "call" || kind == "return" || kind == "unwind";
}

[[nodiscard]] sappp::Result<nlohmann::json> load_nir_json(const std::string& input_dir,
                                                          const std::string& schema_dir) {
    fs::path nir_path = fs::path(input_dir) / "frontend" / "nir.json";
    std::error_code ec;
    if (!fs::exists(nir_path, ec)) {
        return std::unexpected(Error::make("MissingDependency",
            "NIR file not found: " + nir_path.string()));
    }
    if (ec) {
        return std::unexpected(Error::make("MissingDependency",
            "Failed to stat NIR file: " + nir_path.string() + ": " + ec.message()));
    }
    auto nir_result = read_json_file(nir_path.string());
    if (!nir_result) {
        if (nir_result.error().code == "ParseError") {
            return std::unexpected(Error::make("SchemaInvalid",
                "Failed to parse NIR JSON: " + nir_result.error().message));
        }
        return std::unexpected(Error::make("MissingDependency",
            "Failed to read NIR file: " + nir_result.error().message));
    }

    if (auto result = sappp::common::validate_json(*nir_result, nir_schema_path(schema_dir)); !result) {
        return std::unexpected(Error::make("SchemaInvalid",
            "NIR schema invalid: " + result.error().message));
    }

    const auto& nir_json = *nir_result;
    if (nir_json.at("semantics_version").get<std::string>() != sappp::kSemanticsVersion) {
        return std::unexpected(Error::make("VersionMismatch",
            "NIR semantics_version mismatch"));
    }
    if (nir_json.contains("proof_system_version") &&
        nir_json.at("proof_system_version").get<std::string>() != sappp::kProofSystemVersion) {
        return std::unexpected(Error::make("VersionMismatch",
            "NIR proof_system_version mismatch"));
    }
    if (nir_json.contains("profile_version") &&
        nir_json.at("profile_version").get<std::string>() != sappp::kProfileVersion) {
        return std::unexpected(Error::make("VersionMismatch",
            "NIR profile_version mismatch"));
    }

    return nir_json;
}

[[nodiscard]] sappp::Result<NirIndex> build_nir_index(const nlohmann::json& nir_json) {
    NirIndex index;
    index.tu_id = nir_json.at("tu_id").get<std::string>();

    for (const auto& func_json : nir_json.at("functions")) {
        std::string function_uid = func_json.at("function_uid").get<std::string>();
        FunctionInfo info;
        const auto& cfg = func_json.at("cfg");
        info.entry = cfg.at("entry").get<std::string>();

        for (const auto& block_json : cfg.at("blocks")) {
            std::string block_id = block_json.at("id").get<std::string>();
            BlockInfo block;
            const auto& insts = block_json.at("insts");
            for (auto [i, inst_json] : std::views::enumerate(insts)) {
                std::string inst_id = inst_json.at("id").get<std::string>();
                std::string op = inst_json.at("op").get<std::string>();
                InstInfo inst_info{static_cast<std::size_t>(i), std::move(op)};
                if (!block.insts.emplace(inst_id, std::move(inst_info)).second) {
                    return std::unexpected(Error::make("RuleViolation",
                        "Duplicate inst_id in NIR: " + inst_id));
                }
            }
            if (!info.blocks.emplace(block_id, std::move(block)).second) {
                return std::unexpected(Error::make("RuleViolation",
                    "Duplicate block_id in NIR: " + block_id));
            }
        }

        for (const auto& edge_json : cfg.at("edges")) {
            std::string from = edge_json.at("from").get<std::string>();
            std::string to = edge_json.at("to").get<std::string>();
            std::string kind = edge_json.at("kind").get<std::string>();
            if (!info.blocks.contains(from) || !info.blocks.contains(to)) {
                return std::unexpected(Error::make("RuleViolation",
                    "CFG edge references unknown block"));
            }
            info.edges[from].push_back({std::move(to), std::move(kind)});
        }

        if (!index.functions.emplace(function_uid, std::move(info)).second) {
            return std::unexpected(Error::make("RuleViolation",
                "Duplicate function_uid in NIR: " + function_uid));
        }
    }

    return index;
}

[[nodiscard]] bool has_edge(const FunctionInfo& func,
                            const std::string& from,
                            const std::string& to,
                            const std::optional<std::string>& expected_kind) {
    auto it = func.edges.find(from);
    if (it == func.edges.end()) {
        return false;
    }
    for (const auto& edge : it->second) {
        if (edge.to != to) {
            continue;
        }
        if (!expected_kind.has_value()) {
            return true;
        }
        if (edge_kind_matches(*expected_kind, edge.kind)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_exception_edge(const FunctionInfo& func,
                                      const std::string& from,
                                      const std::string& to) {
    auto it = func.edges.find(from);
    if (it == func.edges.end()) {
        return false;
    }
    for (const auto& edge : it->second) {
        if (edge.to == to && is_exception_edge_kind(edge.kind)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] sappp::VoidResult validate_bug_trace_path(const nlohmann::json& bug_trace,
                                                        const NirIndex& nir_index) {
    if (bug_trace.at("trace_kind").get<std::string>() != "ir_path.v1") {
        return std::unexpected(Error::make("UnsupportedProofFeature",
            "Unsupported BugTrace trace_kind"));
    }

    const auto& steps_json = bug_trace.at("steps");
    if (steps_json.empty()) {
        return std::unexpected(Error::make("ProofCheckFailed",
            "BugTrace steps are empty"));
    }

    std::vector<TraceStep> steps;
    steps.reserve(steps_json.size());
    for (const auto& step_json : steps_json) {
        const auto& ir = step_json.at("ir");
        std::string tu_id = ir.at("tu_id").get<std::string>();
        if (tu_id != nir_index.tu_id) {
            return std::unexpected(Error::make("RuleViolation",
                "BugTrace tu_id mismatch with NIR"));
        }
        TraceStep step;
        step.function_uid = ir.at("function_uid").get<std::string>();
        step.block_id = ir.at("block_id").get<std::string>();
        step.inst_id = ir.at("inst_id").get<std::string>();
        if (step_json.contains("edge_kind")) {
            step.edge_kind = step_json.at("edge_kind").get<std::string>();
            if (step.edge_kind->empty()) {
                return std::unexpected(Error::make("RuleViolation",
                    "BugTrace edge_kind must be non-empty"));
            }
        }

        auto func_it = nir_index.functions.find(step.function_uid);
        if (func_it == nir_index.functions.end()) {
            return std::unexpected(Error::make("ProofCheckFailed",
                "BugTrace refers to unknown function_uid"));
        }
        const FunctionInfo& func = func_it->second;
        auto block_it = func.blocks.find(step.block_id);
        if (block_it == func.blocks.end()) {
            return std::unexpected(Error::make("ProofCheckFailed",
                "BugTrace refers to unknown block_id"));
        }
        const BlockInfo& block = block_it->second;
        auto inst_it = block.insts.find(step.inst_id);
        if (inst_it == block.insts.end()) {
            return std::unexpected(Error::make("ProofCheckFailed",
                "BugTrace refers to unknown inst_id"));
        }
        step.inst_index = inst_it->second.index;
        step.op = inst_it->second.op;
        steps.push_back(std::move(step));
    }

    std::vector<CallFrame> call_stack;
    call_stack.push_back({steps.front().function_uid, std::nullopt});

    for (std::size_t i = 1; i < steps.size(); ++i) {
        const TraceStep& prev = steps[i - 1];
        const TraceStep& curr = steps[i];

        if (prev.function_uid == curr.function_uid) {
            if (curr.edge_kind.has_value() && is_interprocedural_kind(*curr.edge_kind)) {
                return std::unexpected(Error::make("RuleViolation",
                    "Interprocedural edge_kind used within a single function"));
            }

            auto func_it = nir_index.functions.find(prev.function_uid);
            if (func_it == nir_index.functions.end()) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace refers to unknown function_uid"));
            }
            const FunctionInfo& func = func_it->second;
            if (prev.block_id == curr.block_id) {
                if (curr.inst_index < prev.inst_index) {
                    return std::unexpected(Error::make("ProofCheckFailed",
                        "BugTrace steps regress within block"));
                }
                continue;
            }

            if (!has_edge(func, prev.block_id, curr.block_id, curr.edge_kind)) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace CFG edge mismatch"));
            }
            continue;
        }

        if (!curr.edge_kind.has_value()) {
            return std::unexpected(Error::make("RuleViolation",
                "Interprocedural transition missing edge_kind"));
        }

        if (call_stack.empty() || call_stack.back().function_uid != prev.function_uid) {
            return std::unexpected(Error::make("ProofCheckFailed",
                "BugTrace call stack mismatch"));
        }

        const std::string& kind = *curr.edge_kind;
        if (kind == "call") {
            if (!is_call_op(prev.op)) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace call edge without call-like op"));
            }
            auto func_it = nir_index.functions.find(curr.function_uid);
            if (func_it == nir_index.functions.end()) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace refers to unknown callee function_uid"));
            }
            if (func_it->second.entry != curr.block_id) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace call does not enter callee entry block"));
            }
            CallSite site{prev.block_id, prev.inst_id, prev.inst_index};
            call_stack.push_back({curr.function_uid, site});
            continue;
        }

        if (kind == "return" || kind == "unwind") {
            if (call_stack.size() < 2) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace return/unwind without caller frame"));
            }
            CallFrame callee_frame = call_stack.back();
            call_stack.pop_back();
            const std::string& caller_uid = call_stack.back().function_uid;
            if (caller_uid != curr.function_uid) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace return/unwind target mismatch"));
            }
            if (!callee_frame.call_site.has_value()) {
                return std::unexpected(Error::make("ProofCheckFailed",
                    "BugTrace missing call site for return/unwind"));
            }
            const CallSite& site = *callee_frame.call_site;
            const FunctionInfo& caller_info = nir_index.functions.at(caller_uid);
            if (kind == "unwind") {
                if (!has_exception_edge(caller_info, site.block_id, curr.block_id)) {
                    return std::unexpected(Error::make("ProofCheckFailed",
                        "BugTrace unwind does not follow exception edge"));
                }
            } else {
                if (curr.block_id == site.block_id) {
                    if (curr.inst_index < site.inst_index) {
                        return std::unexpected(Error::make("ProofCheckFailed",
                            "BugTrace return regresses within call-site block"));
                    }
                } else if (!has_edge(caller_info, site.block_id, curr.block_id, std::nullopt)) {
                    return std::unexpected(Error::make("ProofCheckFailed",
                        "BugTrace return target not reachable from call-site block"));
                }
            }
            continue;
        }

        return std::unexpected(Error::make("UnsupportedProofFeature",
            "Unsupported interprocedural edge_kind: " + kind));
    }

    return {};
}

} // namespace

Validator::Validator(std::string input_dir, std::string schema_dir)
    : m_input_dir(std::move(input_dir)),
      m_schema_dir(std::move(schema_dir)) {}

sappp::Result<nlohmann::json> Validator::validate(bool strict) {
    fs::path index_dir = fs::path(m_input_dir) / "certstore" / "index";
    std::error_code ec;
    if (!fs::exists(index_dir, ec)) {
        if (ec) {
            return std::unexpected(Error::make("IOError",
                "Failed to stat certstore index directory: " + index_dir.string() + ": " + ec.message()));
        }
        return std::unexpected(Error::make("MissingDependency",
            "certstore index directory not found: " + index_dir.string()));
    }

    std::vector<fs::path> index_files;
    for (fs::directory_iterator it(index_dir, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) {
            return std::unexpected(Error::make("IOError",
                "Failed to read certstore index directory: " + ec.message()));
        }
        const auto& entry = *it;
        std::error_code entry_ec;
        bool is_regular = entry.is_regular_file(entry_ec);
        if (entry_ec) {
            return std::unexpected(Error::make("IOError",
                "Failed to stat index entry: " + entry.path().string() + ": " + entry_ec.message()));
        }
        if (!is_regular) {
            continue;
        }
        if (entry.path().extension() == ".json") {
            index_files.push_back(entry.path());
        }
    }
    if (ec) {
        return std::unexpected(Error::make("IOError",
            "Failed to read certstore index directory: " + ec.message()));
    }

    std::ranges::sort(index_files);

    std::vector<nlohmann::json> results;
    std::string tu_id;

    std::optional<NirIndex> nir_index;
    std::optional<ValidationError> nir_load_error;

    auto get_nir_index = [&]() -> std::expected<const NirIndex*, ValidationError> {
        if (nir_index.has_value()) {
            return &*nir_index;
        }
        if (nir_load_error.has_value()) {
            return std::unexpected(*nir_load_error);
        }
        auto nir_json = load_nir_json(m_input_dir, m_schema_dir);
        if (!nir_json) {
            nir_load_error = make_error_from_result(nir_json.error());
            return std::unexpected(*nir_load_error);
        }
        auto index_result = build_nir_index(*nir_json);
        if (!index_result) {
            nir_load_error = make_error_from_result(index_result.error());
            return std::unexpected(*nir_load_error);
        }
        nir_index = std::move(*index_result);
        return &*nir_index;
    };

    for (const auto& index_path : index_files) {
        std::string fallback_po_id = derive_po_id_from_path(index_path);
        auto index_json_result = read_json_file(index_path.string());
        if (!index_json_result) {
            if (strict) {
                return std::unexpected(index_json_result.error());
            }
            results.push_back(make_unknown_result(fallback_po_id,
                                                  make_error_from_result(index_json_result.error())));
            continue;
        }

        if (auto result = sappp::common::validate_json(*index_json_result, cert_index_schema_path(m_schema_dir));
            !result) {
            if (strict) {
                return std::unexpected(Error::make("SchemaInvalid",
                    "Cert index schema invalid: " + result.error().message));
            }
            results.push_back(make_unknown_result(fallback_po_id,
                                                  make_error("SchemaInvalid",
                                                             "Cert index schema invalid: " + result.error().message)));
            continue;
        }

        std::string po_id = index_json_result->at("po_id").get<std::string>();
        std::string root_hash = index_json_result->at("root").get<std::string>();

        auto root_cert = load_cert_object(m_input_dir, m_schema_dir, root_hash);
        if (!root_cert) {
            if (strict) {
                return std::unexpected(root_cert.error());
            }
            results.push_back(make_unknown_result(po_id, make_error_from_result(root_cert.error())));
            continue;
        }

        const nlohmann::json& root = *root_cert;
        if (!root.contains("kind") || root.at("kind").get<std::string>() != "ProofRoot") {
            ValidationError unsupported = unsupported_error("Root certificate is not ProofRoot");
            if (strict) {
                return std::unexpected(Error::make(unsupported.reason, unsupported.message));
            }
            results.push_back(make_unknown_result(po_id, unsupported));
            continue;
        }

        const nlohmann::json& depends = root.at("depends");
        std::string sem_version = depends.at("semantics_version").get<std::string>();
        std::string proof_version = depends.at("proof_system_version").get<std::string>();
        std::string profile_version = depends.at("profile_version").get<std::string>();
        if (sem_version != sappp::kSemanticsVersion ||
            proof_version != sappp::kProofSystemVersion ||
            profile_version != sappp::kProfileVersion) {
            ValidationError mismatch = version_mismatch_error("ProofRoot version triple mismatch");
            if (strict) {
                return std::unexpected(Error::make(mismatch.reason, mismatch.message));
            }
            results.push_back(make_unknown_result(po_id, mismatch));
            continue;
        }

        std::string po_ref = root.at("po").at("ref").get<std::string>();
        std::string ir_ref = root.at("ir").at("ref").get<std::string>();
        std::string evidence_ref = root.at("evidence").at("ref").get<std::string>();

        auto po_cert = load_cert_object(m_input_dir, m_schema_dir, po_ref);
        if (!po_cert) {
            if (strict) {
                return std::unexpected(po_cert.error());
            }
            results.push_back(make_unknown_result(po_id, make_error_from_result(po_cert.error())));
            continue;
        }

        if (po_cert->at("kind").get<std::string>() != "PoDef") {
            ValidationError violation = rule_violation_error("Po reference is not PoDef");
            if (strict) {
                return std::unexpected(Error::make(violation.reason, violation.message));
            }
            results.push_back(make_unknown_result(po_id, violation));
            continue;
        }

        std::string po_cert_id = po_cert->at("po").at("po_id").get<std::string>();
        if (po_cert_id != po_id) {
            ValidationError violation = rule_violation_error("PoDef po_id mismatch");
            if (strict) {
                return std::unexpected(Error::make(violation.reason, violation.message));
            }
            results.push_back(make_unknown_result(po_id, violation));
            continue;
        }

        auto ir_cert = load_cert_object(m_input_dir, m_schema_dir, ir_ref);
        if (!ir_cert) {
            if (strict) {
                return std::unexpected(ir_cert.error());
            }
            results.push_back(make_unknown_result(po_id, make_error_from_result(ir_cert.error())));
            continue;
        }

        if (ir_cert->at("kind").get<std::string>() != "IrRef") {
            ValidationError violation = rule_violation_error("IR reference is not IrRef");
            if (strict) {
                return std::unexpected(Error::make(violation.reason, violation.message));
            }
            results.push_back(make_unknown_result(po_id, violation));
            continue;
        }

        if (tu_id.empty()) {
            tu_id = ir_cert->at("tu_id").get<std::string>();
        }

        auto evidence_cert = load_cert_object(m_input_dir, m_schema_dir, evidence_ref);
        if (!evidence_cert) {
            if (strict) {
                return std::unexpected(evidence_cert.error());
            }
            results.push_back(make_unknown_result(po_id, make_error_from_result(evidence_cert.error())));
            continue;
        }

        std::string result_kind = root.at("result").get<std::string>();
        if (result_kind == "BUG") {
            if (evidence_cert->at("kind").get<std::string>() != "BugTrace") {
                ValidationError unsupported = unsupported_error("BUG evidence is not BugTrace");
                if (strict) {
                    return std::unexpected(Error::make(unsupported.reason, unsupported.message));
                }
                results.push_back(make_unknown_result(po_id, unsupported));
                continue;
            }

            const nlohmann::json& violation = evidence_cert->at("violation");
            std::string violation_po_id = violation.at("po_id").get<std::string>();
            bool predicate_holds = violation.at("predicate_holds").get<bool>();
            if (violation_po_id != po_id) {
                ValidationError violation_error = rule_violation_error("BugTrace po_id mismatch");
                if (strict) {
                    return std::unexpected(Error::make(violation_error.reason, violation_error.message));
                }
                results.push_back(make_unknown_result(po_id, violation_error));
                continue;
            }
            if (predicate_holds) {
                ValidationError proof_error = proof_failed_error("BugTrace predicate holds at violation state");
                if (strict) {
                    return std::unexpected(Error::make(proof_error.reason, proof_error.message));
                }
                results.push_back(make_unknown_result(po_id, proof_error));
                continue;
            }

            auto nir_index_result = get_nir_index();
            if (!nir_index_result) {
                if (strict) {
                    return std::unexpected(Error::make(nir_index_result.error().reason,
                        nir_index_result.error().message));
                }
                results.push_back(make_unknown_result(po_id, nir_index_result.error()));
                continue;
            }

            auto trace_check = validate_bug_trace_path(*evidence_cert, **nir_index_result);
            if (!trace_check) {
                ValidationError proof_error = make_error_from_result(trace_check.error());
                if (strict) {
                    return std::unexpected(Error::make(proof_error.reason, proof_error.message));
                }
                results.push_back(make_unknown_result(po_id, proof_error));
                continue;
            }

            results.push_back(make_validated_result(po_id, "BUG", root_hash));
        } else if (result_kind == "SAFE") {
            ValidationError unsupported = unsupported_error("SAFE validation not yet supported");
            if (strict) {
                return std::unexpected(Error::make(unsupported.reason, unsupported.message));
            }
            results.push_back(make_unknown_result(po_id, unsupported));
        } else {
            ValidationError violation = rule_violation_error("ProofRoot result is invalid");
            if (strict) {
                return std::unexpected(Error::make(violation.reason, violation.message));
            }
            results.push_back(make_unknown_result(po_id, violation));
        }
    }

    if (results.empty()) {
        return std::unexpected(Error::make("MissingDependency",
            "No certificate index entries found"));
    }

    std::ranges::stable_sort(results,
                              [](const nlohmann::json& a, const nlohmann::json& b) {
                                  return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
                              });

    if (tu_id.empty()) {
        return std::unexpected(Error::make("RuleViolation",
            "Failed to determine tu_id from IR references"));
    }

    nlohmann::json output = {
        {"schema_version", "validated_results.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::kVersion},
            {"build_id", sappp::kBuildId}
        }},
        {"generated_at", current_time_rfc3339()},
        {"tu_id", tu_id},
        {"results", results},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"profile_version", sappp::kProfileVersion}
    };

    if (auto result = sappp::common::validate_json(output, validated_results_schema_path(m_schema_dir)); !result) {
        return std::unexpected(Error::make("SchemaInvalid",
            "Validated results schema invalid: " + result.error().message));
    }

    return output;
}

sappp::VoidResult Validator::write_results(const nlohmann::json& results, const std::string& output_path) const {
    if (auto result = sappp::common::validate_json(results, validated_results_schema_path(m_schema_dir)); !result) {
        return std::unexpected(Error::make("SchemaInvalid",
            "Validated results schema invalid: " + result.error().message));
    }
    return write_json_file(output_path, results);
}

} // namespace sappp::validator
