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
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sappp::po {

namespace {

std::string current_time_utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &now_time);
#else
    gmtime_r(&now_time, &utc_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
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
    if (token.rfind("ub.", 0) == 0) {
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

std::string infer_po_kind(const nlohmann::json& inst) {
    if (inst.contains("kind") && inst.at("kind").is_string()) {
        std::string kind = map_po_kind(inst.at("kind").get<std::string>());
        if (!kind.empty()) {
            return kind;
        }
    }

    if (inst.contains("args") && inst.at("args").is_array()) {
        for (const auto& arg : inst.at("args")) {
            if (arg.is_string()) {
                std::string kind = map_po_kind(arg.get<std::string>());
                if (!kind.empty()) {
                    return kind;
                }
            }
        }
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

nlohmann::json build_anchor(const std::string& block_id,
                            const std::string& inst_id) {
    return nlohmann::json{
        {"block_id", block_id},
        {"inst_id", inst_id}
    };
}

nlohmann::json build_predicate(const std::string& po_kind) {
    nlohmann::json expr = {
        {"op", "ub.check"},
        {"args", nlohmann::json::array({po_kind})}
    };
    return nlohmann::json{
        {"expr", expr},
        {"pretty", "ub.check(" + po_kind + ")"}
    };
}

} // namespace

nlohmann::json PoGenerator::generate(const nlohmann::json& nir_json) const {
    nlohmann::json output = {
        {"schema_version", "po.v1"},
        {"tool", nir_json.at("tool")},
        {"generated_at", current_time_utc()},
        {"tu_id", nir_json.at("tu_id")},
        {"semantics_version", sappp::SEMANTICS_VERSION},
        {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
        {"profile_version", sappp::PROFILE_VERSION},
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
                if (!inst.contains("op") || inst.at("op").get<std::string>() != "ub.check") {
                    continue;
                }
                const std::string inst_id = inst.at("id").get<std::string>();
                const std::string po_kind = infer_po_kind(inst);

                nlohmann::json po_id_input = {
                    {"function_uid", function_uid},
                    {"block_id", block_id},
                    {"inst_id", inst_id},
                    {"po_kind", po_kind},
                    {"semantics_version", sappp::SEMANTICS_VERSION},
                    {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
                    {"profile_version", sappp::PROFILE_VERSION}
                };
                const std::string po_id = canonical::hash_canonical(po_id_input);

                nlohmann::json po_entry = {
                    {"po_id", po_id},
                    {"po_kind", po_kind},
                    {"semantics_version", sappp::SEMANTICS_VERSION},
                    {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
                    {"profile_version", sappp::PROFILE_VERSION},
                    {"repo_identity", build_repo_identity(inst, file_hashes)},
                    {"function", {
                        {"usr", function_uid},
                        {"mangled", mangled_name}
                    }},
                    {"anchor", build_anchor(block_id, inst_id)},
                    {"predicate", build_predicate(po_kind)}
                };
                pos.push_back(std::move(po_entry));
            }
        }
    }

    if (pos.empty()) {
        throw std::runtime_error("No ub.check instructions found for PO generation");
    }

    std::stable_sort(pos.begin(), pos.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
                     });

    output["pos"] = std::move(pos);
    return output;
}

} // namespace sappp::po
