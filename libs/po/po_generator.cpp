/**
 * @file po_generator.cpp
 * @brief Proof Obligation (PO) generator from NIR
 */

#include "po_generator.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sappp::po {

namespace {

std::string current_time_utc() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

std::string read_file_contents(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open source file: " + path);
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string normalize_kind_token(std::string token) {
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (token.starts_with("ub.")) {
        token = token.substr(3);
    }
    return token;
}

std::string map_po_kind(const std::string& token) {
    const std::string normalized = normalize_kind_token(token);
    if (normalized == "div0" || normalized == "divzero" || normalized == "div_zero" ||
        normalized == "div-by-zero") {
        return "UB.DivZero";
    }
    if (normalized == "null" || normalized == "null_deref" || normalized == "null-deref" ||
        normalized == "nullderef") {
        return "UB.NullDeref";
    }
    if (normalized == "oob" || normalized == "out_of_bounds" || normalized == "out-of-bounds" ||
        normalized == "outofbounds") {
        return "UB.OutOfBounds";
    }
    return "";
}

std::string extract_kind_token(const nlohmann::json& inst) {
    if (inst.contains("kind") && inst.at("kind").is_string()) {
        return inst.at("kind").get<std::string>();
    }

    if (inst.contains("args") && inst.at("args").is_array()) {
        for (const auto& arg : inst.at("args")) {
            if (arg.is_string()) {
                return arg.get<std::string>();
            }
        }
    }

    return "";
}

std::string infer_po_kind(const nlohmann::json& inst) {
    std::string token = extract_kind_token(inst);
    std::string mapped = map_po_kind(token);
    if (!mapped.empty()) {
        return mapped;
    }
    if (!token.empty()) {
        return token;
    }
    return "UB.Unknown";
}

nlohmann::json build_repo_identity(const nlohmann::json& inst,
                                  std::unordered_map<std::string, std::string>& file_hashes) {
    std::string path = "unknown";
    std::string content_hash = common::sha256_prefixed("");

    if (inst.contains("src") && inst.at("src").is_object()) {
        const auto& src = inst.at("src");
        if (src.contains("file") && src.at("file").is_string()) {
            std::string file_path = src.at("file").get<std::string>();
            path = common::normalize_path(file_path);
            auto it = file_hashes.find(file_path);
            if (it == file_hashes.end()) {
                std::string contents = read_file_contents(file_path);
                std::string digest = common::sha256_prefixed(contents);
                it = file_hashes.emplace(file_path, std::move(digest)).first;
            }
            content_hash = it->second;
        }
    }

    return nlohmann::json{
        {"path", path},
        {"content_sha256", content_hash}
    };
}

nlohmann::json build_anchor_id(const std::string& block_id,
                               const std::string& inst_id) {
    return nlohmann::json{
        {"block_id", block_id},
        {"inst_id", inst_id}
    };
}

nlohmann::json build_predicate_args(const nlohmann::json& inst,
                                    const std::string& po_kind) {
    nlohmann::json args = nlohmann::json::array();
    if (inst.contains("args") && inst.at("args").is_array()) {
        const auto& inst_args = inst.at("args");
        if (!inst_args.empty() && inst_args.at(0).is_string()) {
            args.push_back(po_kind);
            for (size_t i = 1; i < inst_args.size(); ++i) {
                args.push_back(inst_args.at(i));
            }
        } else {
            args.push_back(po_kind);
            for (const auto& arg : inst_args) {
                args.push_back(arg);
            }
        }
    } else {
        args.push_back(po_kind);
    }
    return args;
}

std::string pretty_arg(const nlohmann::json& arg) {
    if (arg.is_string()) {
        return arg.get<std::string>();
    }
    if (arg.is_boolean()) {
        return arg.get<bool>() ? "true" : "false";
    }
    if (arg.is_number_integer()) {
        return std::to_string(arg.get<long long>());
    }
    if (arg.is_number_unsigned()) {
        return std::to_string(arg.get<unsigned long long>());
    }
    if (arg.is_null()) {
        return "null";
    }
    return canonical::canonicalize(arg);
}

std::string format_pretty(const std::string& op,
                          const nlohmann::json& args) {
    std::string result = op + "(";
    if (args.is_array()) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += pretty_arg(args.at(i));
        }
    }
    result += ")";
    return result;
}

nlohmann::json build_predicate(const std::string& op,
                               const std::string& po_kind,
                               const nlohmann::json& inst) {
    nlohmann::json args = build_predicate_args(inst, po_kind);
    nlohmann::json expr = {
        {"op", op},
        {"args", args}
    };
    return nlohmann::json{
        {"expr", expr},
        {"pretty", format_pretty(op, args)}
    };
}

} // namespace

nlohmann::json PoGenerator::generate(const nlohmann::json& nir_json) const {
    const std::string semantics_version = nir_json.at("semantics_version").get<std::string>();
    const std::string proof_system_version = nir_json.at("proof_system_version").get<std::string>();
    const std::string profile_version = nir_json.at("profile_version").get<std::string>();

    nlohmann::json output = {
        {"schema_version", "po.v1"},
        {"tool", nir_json.at("tool")},
        {"generated_at", nir_json.value("generated_at", current_time_utc())},
        {"tu_id", nir_json.at("tu_id")},
        {"semantics_version", semantics_version},
        {"proof_system_version", proof_system_version},
        {"profile_version", profile_version},
        {"pos", nlohmann::json::array()}
    };

    if (nir_json.contains("input_digest")) {
        output["input_digest"] = nir_json.at("input_digest");
    }

    std::vector<nlohmann::json> pos;
    std::unordered_map<std::string, std::string> file_hashes;

    const auto& functions = nir_json.at("functions");
    for (const auto& func : functions) {
        const std::string function_uid = func.at("function_uid").get<std::string>();
        const std::string mangled_name = func.at("mangled_name").get<std::string>();
        const auto& blocks = func.at("cfg").at("blocks");

        for (const auto& block : blocks) {
            const std::string block_id = block.at("id").get<std::string>();
            const auto& insts = block.at("insts");

            for (const auto& inst : insts) {
                if (!inst.contains("op") || !inst.at("op").is_string()) {
                    continue;
                }
                const std::string op = inst.at("op").get<std::string>();
                if (op != "ub.check" && op != "sink.marker") {
                    continue;
                }
                const std::string inst_id = inst.at("id").get<std::string>();
                const std::string po_kind = infer_po_kind(inst);
                const nlohmann::json repo_identity = build_repo_identity(inst, file_hashes);
                const nlohmann::json anchor_id = build_anchor_id(block_id, inst_id);

                nlohmann::json po_id_input = {
                    {"repo_identity", repo_identity},
                    {"function", {{"usr", function_uid}}},
                    {"anchor", anchor_id},
                    {"po_kind", po_kind},
                    {"semantics_version", semantics_version},
                    {"proof_system_version", proof_system_version},
                    {"profile_version", profile_version}
                };
                const std::string po_id = canonical::hash_canonical(po_id_input);

                nlohmann::json po_entry = {
                    {"po_id", po_id},
                    {"po_kind", po_kind},
                    {"semantics_version", semantics_version},
                    {"proof_system_version", proof_system_version},
                    {"profile_version", profile_version},
                    {"repo_identity", repo_identity},
                    {"function", {
                        {"usr", function_uid},
                        {"mangled", mangled_name}
                    }},
                    {"anchor", anchor_id},
                    {"predicate", build_predicate(op, po_kind, inst)}
                };
                pos.push_back(std::move(po_entry));
            }
        }
    }

    if (pos.empty()) {
        throw std::runtime_error("No sink instructions found for PO generation");
    }

    std::ranges::stable_sort(pos,
                              [](const nlohmann::json& a, const nlohmann::json& b) {
                                  return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
                              });

    output["pos"] = std::move(pos);
    return output;
}

} // namespace sappp::po
