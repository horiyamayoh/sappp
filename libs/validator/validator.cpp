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
#include <vector>

namespace sappp::validator {

namespace {

namespace fs = std::filesystem;

struct ValidationError {
    std::string status;
    std::string reason;
    std::string message;
};

std::string current_time_rfc3339() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

bool is_hex_lower(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool is_sha256_prefixed(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix)) {
        return false;
    }
    if (value.size() != prefix.size() + 64) {
        return false;
    }
    return std::ranges::all_of(value.substr(prefix.size()), is_hex_lower);
}

std::string derive_po_id_from_path(const fs::path& path) {
    std::string stem = path.stem().string();
    if (is_sha256_prefixed(stem)) {
        return stem;
    }
    return sappp::common::sha256_prefixed(stem);
}

std::string cert_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "cert.v1.schema.json").string();
}

std::string cert_index_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "cert_index.v1.schema.json").string();
}

std::string validated_results_schema_path(const std::string& schema_dir) {
    return (fs::path(schema_dir) / "validated_results.v1.schema.json").string();
}

sappp::Result<std::string> object_path_for_hash(const fs::path& base_dir, const std::string& hash) {
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

sappp::Result<nlohmann::json> read_json_file(const std::string& path) {
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

sappp::VoidResult write_json_file(const std::string& path, const nlohmann::json& payload) {
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

ValidationError make_error(const std::string& status, const std::string& message) {
    return {status, status, message};
}

nlohmann::json make_unknown_result(const std::string& po_id,
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

nlohmann::json make_validated_result(const std::string& po_id,
                                     const std::string& category,
                                     const std::string& certificate_root) {
    return nlohmann::json{
        {"po_id", po_id},
        {"category", category},
        {"validator_status", "Validated"},
        {"certificate_root", certificate_root}
    };
}

ValidationError make_error_from_result(const sappp::Error& error) {
    return make_error(error.code, error.message);
}

sappp::Result<nlohmann::json> load_cert_object(const std::string& input_dir,
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
        if (sem_version != sappp::SEMANTICS_VERSION ||
            proof_version != sappp::PROOF_SYSTEM_VERSION ||
            profile_version != sappp::PROFILE_VERSION) {
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
            {"version", sappp::VERSION},
            {"build_id", sappp::BUILD_ID}
        }},
        {"generated_at", current_time_rfc3339()},
        {"tu_id", tu_id},
        {"results", results},
        {"semantics_version", sappp::SEMANTICS_VERSION},
        {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
        {"profile_version", sappp::PROFILE_VERSION}
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
