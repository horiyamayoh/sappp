/**
 * @file po.cpp
 * @brief Proof Obligation generation from NIR
 */

#include "sappp/po.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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

std::string read_file_or_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::optional<std::string> detect_po_kind(const std::string& op) {
    if (op == "ub.check") {
        return "div0";
    }
    if (op == "load") {
        return "null";
    }
    if (op == "store") {
        return "oob";
    }
    return std::nullopt;
}

nlohmann::json predicate_for_kind(const std::string& kind) {
    if (kind == "div0") {
        return {
            {"expr", {{"op", "nonzero"}, {"args", nlohmann::json::array({"divisor"})}}},
            {"pretty", "divisor != 0"}
        };
    }
    if (kind == "null") {
        return {
            {"expr", {{"op", "nonnull"}, {"args", nlohmann::json::array({"ptr"})}}},
            {"pretty", "ptr != null"}
        };
    }
    if (kind == "oob") {
        return {
            {"expr", {{"op", "in_bounds"}, {"args", nlohmann::json::array({"index"})}}},
            {"pretty", "index in bounds"}
        };
    }
    return {
        {"expr", {{"op", "true"}, {"args", nlohmann::json::array()}}},
        {"pretty", "true"}
    };
}

} // namespace

PoGenerator::PoGenerator(std::string schema_dir)
    : m_schema_dir(std::move(schema_dir)) {}

nlohmann::json PoGenerator::generate(const nlohmann::json& nir) const {
    const std::string tu_id = nir.at("tu_id").get<std::string>();
    const std::string semantics_version = nir.at("semantics_version").get<std::string>();
    const std::string proof_system_version = nir.value("proof_system_version", sappp::PROOF_SYSTEM_VERSION);
    const std::string profile_version = nir.value("profile_version", sappp::PROFILE_VERSION);

    std::vector<nlohmann::json> pos;
    std::map<std::string, std::string> file_hash_cache;

    const auto& functions = nir.at("functions");
    for (const auto& func : functions) {
        const std::string function_uid = func.at("function_uid").get<std::string>();
        const std::string mangled_name = func.at("mangled_name").get<std::string>();
        const auto& cfg = func.at("cfg");
        const auto& blocks = cfg.at("blocks");

        for (const auto& block : blocks) {
            const std::string block_id = block.at("id").get<std::string>();
            const auto& insts = block.at("insts");

            for (const auto& inst : insts) {
                const std::string inst_id = inst.at("id").get<std::string>();
                const std::string op = inst.at("op").get<std::string>();
                std::optional<std::string> kind = detect_po_kind(op);
                if (!kind.has_value()) {
                    continue;
                }

                std::string repo_path = "unknown";
                nlohmann::json anchor = {
                    {"block_id", block_id},
                    {"inst_id", inst_id}
                };
                if (inst.contains("src")) {
                    const auto& src = inst.at("src");
                    if (src.contains("file")) {
                        repo_path = src.at("file").get<std::string>();
                        repo_path = sappp::common::normalize_path(repo_path);
                    }
                    anchor["src"] = src;
                }

                std::string content_hash;
                auto cached = file_hash_cache.find(repo_path);
                if (cached != file_hash_cache.end()) {
                    content_hash = cached->second;
                } else {
                    std::string content = read_file_or_empty(repo_path);
                    content_hash = sappp::common::sha256_prefixed(content);
                    file_hash_cache.emplace(repo_path, content_hash);
                }

                nlohmann::json po = {
                    {"po_kind", *kind},
                    {"repo_identity", {
                        {"path", repo_path},
                        {"content_sha256", content_hash}
                    }},
                    {"function", {
                        {"usr", function_uid},
                        {"mangled", mangled_name}
                    }},
                    {"anchor", anchor},
                    {"predicate", predicate_for_kind(*kind)},
                    {"semantics_version", semantics_version},
                    {"proof_system_version", proof_system_version},
                    {"profile_version", profile_version}
                };

                nlohmann::json po_id_input = {
                    {"repo_identity", po.at("repo_identity")},
                    {"function_uid", function_uid},
                    {"anchor", {{"block_id", block_id}, {"inst_id", inst_id}}},
                    {"po_kind", *kind},
                    {"semantics_version", semantics_version},
                    {"proof_system_version", proof_system_version},
                    {"profile_version", profile_version}
                };
                po["po_id"] = sappp::canonical::hash_canonical(po_id_input);
                pos.push_back(std::move(po));
            }
        }
    }

    if (pos.empty()) {
        throw std::runtime_error("PO generation produced no items");
    }

    std::stable_sort(pos.begin(), pos.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.at("po_id").get<std::string>() < b.at("po_id").get<std::string>();
                     });

    nlohmann::json output = {
        {"schema_version", "po.v1"},
        {"tool", {{"name", "sappp"}, {"version", sappp::VERSION}, {"build_id", sappp::BUILD_ID}}},
        {"generated_at", current_time_utc()},
        {"tu_id", tu_id},
        {"pos", pos},
        {"semantics_version", semantics_version},
        {"proof_system_version", proof_system_version},
        {"profile_version", profile_version}
    };

    if (nir.contains("input_digest")) {
        output["input_digest"] = nir.at("input_digest");
    }

    std::string schema_error;
    std::string schema_path = m_schema_dir + "/po.v1.schema.json";
    if (!sappp::common::validate_json(output, schema_path, schema_error)) {
        throw std::runtime_error("po schema validation failed: " + schema_error);
    }

    return output;
}

} // namespace sappp::po
