/**
 * @file analyzer.cpp
 * @brief Analyzer v0: generate certificate candidates and UNKNOWN ledger
 */

#include "sappp/analyzer.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sappp::analyzer {

namespace {

namespace fs = std::filesystem;

struct PoInfo {
    nlohmann::json po_json;
    std::string po_id;
    std::string po_kind;
    std::string semantics_version;
    std::string proof_system_version;
    std::string profile_version;
    std::string function_uid;
    std::string block_id;
    std::string inst_id;
};

[[nodiscard]] std::string current_time_utc() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] std::string unknown_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "unknown.v1.schema.json").string();
}

[[nodiscard]] sappp::Result<std::vector<PoInfo>> collect_po_info(const nlohmann::json& po_list) {
    if (!po_list.contains("pos") || !po_list.at("pos").is_array()) {
        return std::unexpected(Error::make("InvalidPoList", "po_list.pos must be an array"));
    }

    const auto& pos = po_list.at("pos");
    std::vector<PoInfo> result;
    result.reserve(pos.size());

    try {
        for (const auto& po : pos) {
            result.push_back(PoInfo{
                .po_json = po,
                .po_id = po.at("po_id").get<std::string>(),
                .po_kind = po.at("po_kind").get<std::string>(),
                .semantics_version = po.at("semantics_version").get<std::string>(),
                .proof_system_version = po.at("proof_system_version").get<std::string>(),
                .profile_version = po.at("profile_version").get<std::string>(),
                .function_uid = po.at("function").at("usr").get<std::string>(),
                .block_id = po.at("anchor").at("block_id").get<std::string>(),
                .inst_id = po.at("anchor").at("inst_id").get<std::string>()
            });
        }
    } catch (const std::exception& ex) {
        return std::unexpected(Error::make("InvalidPoList",
            std::string("Failed to parse po_list entries: ") + ex.what()));
    }

    return result;
}

[[nodiscard]] nlohmann::json build_ir_ref(const PoInfo& info, const std::string& tu_id) {
    return nlohmann::json{
        {"schema_version", "cert.v1"},
        {"kind", "IrRef"},
        {"tu_id", tu_id},
        {"function_uid", info.function_uid},
        {"block_id", info.block_id},
        {"inst_id", info.inst_id}
    };
}

[[nodiscard]] nlohmann::json build_bug_trace(const PoInfo& info,
                                             const nlohmann::json& ir_ref,
                                             bool predicate_holds) {
    return nlohmann::json{
        {"schema_version", "cert.v1"},
        {"kind", "BugTrace"},
        {"trace_kind", "ir_path.v1"},
        {"steps", nlohmann::json::array({
            {
                {"ir", ir_ref}
            }
        })},
        {"violation", {
            {"po_id", info.po_id},
            {"predicate_holds", predicate_holds}
        }}
    };
}

[[nodiscard]] nlohmann::json build_invariant(const PoInfo& info) {
    return nlohmann::json{
        {"schema_version", "cert.v1"},
        {"kind", "Invariant"},
        {"domain", "interval+null+init"},
        {"points", nlohmann::json::array({
            {
                {"ir", {
                    {"function_uid", info.function_uid},
                    {"block_id", info.block_id},
                    {"inst_id", info.inst_id}
                }},
                {"state", nlohmann::json::object()}
            }
        })}
    };
}

[[nodiscard]] sappp::Result<std::string> build_unknown_stable_id(const PoInfo& info,
                                                                 const std::string& unknown_code) {
    nlohmann::json id_input = {
        {"po_id", info.po_id},
        {"unknown_code", unknown_code},
        {"semantics_version", info.semantics_version},
        {"proof_system_version", info.proof_system_version},
        {"profile_version", info.profile_version}
    };
    return sappp::canonical::hash_canonical(id_input);
}

[[nodiscard]] nlohmann::json build_missing_lemma(const PoInfo& info) {
    nlohmann::json expr = {
        {"op", "needs_proof"},
        {"args", nlohmann::json::array({info.po_kind, info.po_id})}
    };
    return nlohmann::json{
        {"expr", expr},
        {"pretty", std::format("Need proof for {} at {}", info.po_kind, info.function_uid)},
        {"symbols", nlohmann::json::array({info.function_uid, info.po_id})}
    };
}

[[nodiscard]] nlohmann::json build_refinement_plan(const PoInfo& info) {
    return nlohmann::json{
        {"message", "Refine numeric domain or add invariants."},
        {"actions", nlohmann::json::array({
            {
                {"action", "refine.numeric-domain"},
                {"params", {
                    {"po_id", info.po_id}
                }}
            }
        })}
    };
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

} // namespace

Analyzer::Analyzer(std::string schema_dir)
    : m_schema_dir(std::move(schema_dir)) {}

sappp::Result<AnalyzerOutput> Analyzer::analyze(const nlohmann::json& po_list,
                                                const std::string& output_dir) const {
    auto po_info_result = collect_po_info(po_list);
    if (!po_info_result) {
        return std::unexpected(po_info_result.error());
    }

    std::vector<PoInfo> pos = std::move(*po_info_result);
    if (pos.empty()) {
        return std::unexpected(Error::make("InvalidPoList", "po_list.pos must not be empty"));
    }

    std::string tu_id;
    try {
        tu_id = po_list.at("tu_id").get<std::string>();
    } catch (const std::exception& ex) {
        return std::unexpected(Error::make("InvalidPoList",
            std::string("Missing tu_id in po_list: ") + ex.what()));
    }

    std::ranges::stable_sort(pos, [](const PoInfo& a, const PoInfo& b) noexcept {
        return a.po_id < b.po_id;
    });

    const std::string bug_po_id = pos.front().po_id;
    const std::string default_unknown_code = "DomainTooWeak.Numeric";

    sappp::certstore::CertStore store((fs::path(output_dir) / "certstore").string(), m_schema_dir);

    std::vector<nlohmann::json> unknown_entries;
    std::vector<std::pair<std::string, std::string>> cert_index;
    cert_index.reserve(pos.size());

    for (const auto& info : pos) {
        nlohmann::json po_cert = {
            {"schema_version", "cert.v1"},
            {"kind", "PoDef"},
            {"po", info.po_json}
        };

        nlohmann::json ir_ref = build_ir_ref(info, tu_id);
        nlohmann::json ir_cert = ir_ref;

        bool is_bug = (info.po_id == bug_po_id);
        nlohmann::json evidence_cert = is_bug ? build_bug_trace(info, ir_ref, false)
                                              : build_invariant(info);
        std::string result_kind = is_bug ? "BUG" : "SAFE";

        auto po_hash = store.put(po_cert);
        if (!po_hash) {
            return std::unexpected(po_hash.error());
        }

        auto ir_hash = store.put(ir_cert);
        if (!ir_hash) {
            return std::unexpected(ir_hash.error());
        }

        auto evidence_hash = store.put(evidence_cert);
        if (!evidence_hash) {
            return std::unexpected(evidence_hash.error());
        }

        nlohmann::json proof_root = {
            {"schema_version", "cert.v1"},
            {"kind", "ProofRoot"},
            {"po", {{"ref", *po_hash}}},
            {"ir", {{"ref", *ir_hash}}},
            {"evidence", {{"ref", *evidence_hash}}},
            {"result", result_kind},
            {"depends", {
                {"semantics_version", info.semantics_version},
                {"proof_system_version", info.proof_system_version},
                {"profile_version", info.profile_version}
            }},
            {"hash_scope", "hash_scope.v1"}
        };

        auto root_hash = store.put(proof_root);
        if (!root_hash) {
            return std::unexpected(root_hash.error());
        }

        auto bind_result = store.bind_po(info.po_id, *root_hash);
        if (!bind_result) {
            return std::unexpected(bind_result.error());
        }
        cert_index.emplace_back(info.po_id, *root_hash);

        if (!is_bug) {
            auto stable_id = build_unknown_stable_id(info, default_unknown_code);
            if (!stable_id) {
                return std::unexpected(stable_id.error());
            }

            nlohmann::json unknown_entry = {
                {"unknown_stable_id", *stable_id},
                {"po_id", info.po_id},
                {"unknown_code", default_unknown_code},
                {"missing_lemma", build_missing_lemma(info)},
                {"refinement_plan", build_refinement_plan(info)}
            };
            unknown_entries.push_back(std::move(unknown_entry));
        }
    }

    std::ranges::stable_sort(unknown_entries,
                             [](const nlohmann::json& a, const nlohmann::json& b) noexcept {
        return a.at("unknown_stable_id").get<std::string>() <
               b.at("unknown_stable_id").get<std::string>();
    });

    std::string generated_at = current_time_utc();
    if (po_list.contains("generated_at") && po_list.at("generated_at").is_string()) {
        generated_at = po_list.at("generated_at").get<std::string>();
    }

    std::string root_semantics = pos.front().semantics_version;
    std::string root_proof_system = pos.front().proof_system_version;
    std::string root_profile = pos.front().profile_version;
    if (po_list.contains("semantics_version") && po_list.at("semantics_version").is_string()) {
        root_semantics = po_list.at("semantics_version").get<std::string>();
    }
    if (po_list.contains("proof_system_version") && po_list.at("proof_system_version").is_string()) {
        root_proof_system = po_list.at("proof_system_version").get<std::string>();
    }
    if (po_list.contains("profile_version") && po_list.at("profile_version").is_string()) {
        root_profile = po_list.at("profile_version").get<std::string>();
    }

    nlohmann::json unknown_ledger = {
        {"schema_version", "unknown.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::kVersion},
            {"build_id", sappp::kBuildId}
        }},
        {"generated_at", generated_at},
        {"tu_id", tu_id},
        {"unknowns", unknown_entries},
        {"semantics_version", root_semantics},
        {"proof_system_version", root_proof_system},
        {"profile_version", root_profile}
    };

    if (po_list.contains("input_digest") && po_list.at("input_digest").is_string()) {
        unknown_ledger["input_digest"] = po_list.at("input_digest").get<std::string>();
    }

    if (auto validation = sappp::common::validate_json(unknown_ledger, unknown_schema_path(m_schema_dir));
        !validation) {
        return std::unexpected(Error::make("SchemaInvalid",
            "Unknown ledger schema validation failed: " + validation.error().message));
    }

    fs::path unknown_path = fs::path(output_dir) / "analyzer" / "unknown_ledger.json";
    if (auto write = write_json_file(unknown_path.string(), unknown_ledger); !write) {
        return std::unexpected(write.error());
    }

    AnalyzerOutput output = {
        .unknown_ledger = std::move(unknown_ledger),
        .cert_index = std::move(cert_index)
    };
    return output;
}

} // namespace sappp::analyzer
