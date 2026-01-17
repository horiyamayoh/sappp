#include "sappp/certstore.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

/// RAII helper to create and clean up a temporary directory
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : m_path(fs::temp_directory_path() / name) {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

std::string object_path_for_hash(const fs::path& base_dir, const std::string& hash) {
    constexpr std::string_view prefix = "sha256:";
    std::size_t digest_start = hash.starts_with(prefix) ? prefix.size() : 0;
    std::string shard = hash.substr(digest_start, 2);
    fs::path object_path = base_dir / "certstore" / "objects" / shard / (hash + ".json");
    return object_path.string();
}

struct CertBundle {
    std::string po_id;
    std::string tu_id;
    std::string root_hash;
    std::string bug_trace_hash;
};

CertBundle build_cert_store(const fs::path& input_dir, const std::string& schema_dir) {
    fs::path certstore_dir = input_dir / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-1");
    std::string tu_id = sappp::common::sha256_prefixed("tu-1");

    nlohmann::json po_cert = {
        {"schema_version", "cert.v1"},
        {"kind", "PoDef"},
        {"po", {
            {"po_id", po_id},
            {"po_kind", "div0"},
            {"profile_version", sappp::PROFILE_VERSION},
            {"semantics_version", sappp::SEMANTICS_VERSION},
            {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
            {"repo_identity", {
                {"path", "src/test.cpp"},
                {"content_sha256", sappp::common::sha256_prefixed("content")}
            }},
            {"function", {
                {"usr", "c:@F@test"},
                {"mangled", "_Z4testv"}
            }},
            {"anchor", {
                {"block_id", "B1"},
                {"inst_id", "I1"}
            }},
            {"predicate", {
                {"expr", {
                    {"op", "neq"}
                }},
                {"pretty", "x != 0"}
            }}
        }}
    };

    nlohmann::json ir_cert = {
        {"schema_version", "cert.v1"},
        {"kind", "IrRef"},
        {"tu_id", tu_id},
        {"function_uid", "func1"},
        {"block_id", "B1"},
        {"inst_id", "I1"}
    };

    nlohmann::json bug_trace = {
        {"schema_version", "cert.v1"},
        {"kind", "BugTrace"},
        {"trace_kind", "ir_path.v1"},
        {"steps", {
            {
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "func1"},
                    {"block_id", "B1"},
                    {"inst_id", "I1"}
                }}
            }
        }},
        {"violation", {
            {"po_id", po_id},
            {"predicate_holds", false}
        }}
    };

    auto po_result = store.put(po_cert);
    EXPECT_TRUE(po_result.has_value()) << "put(po_cert) failed: " << po_result.error().message;
    std::string po_hash = *po_result;

    auto ir_result = store.put(ir_cert);
    EXPECT_TRUE(ir_result.has_value()) << "put(ir_cert) failed: " << ir_result.error().message;
    std::string ir_hash = *ir_result;

    auto bug_result = store.put(bug_trace);
    EXPECT_TRUE(bug_result.has_value()) << "put(bug_trace) failed: " << bug_result.error().message;
    std::string bug_hash = *bug_result;

    nlohmann::json proof_root = {
        {"schema_version", "cert.v1"},
        {"kind", "ProofRoot"},
        {"po", { {"ref", po_hash} }},
        {"ir", { {"ref", ir_hash} }},
        {"evidence", { {"ref", bug_hash} }},
        {"result", "BUG"},
        {"depends", {
            {"semantics_version", sappp::SEMANTICS_VERSION},
            {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
            {"profile_version", sappp::PROFILE_VERSION}
        }},
        {"hash_scope", "hash_scope.v1"}
    };

    auto root_result = store.put(proof_root);
    EXPECT_TRUE(root_result.has_value()) << "put(proof_root) failed: " << root_result.error().message;
    std::string root_hash = *root_result;

    auto bind_result = store.bind_po(po_id, root_hash);
    EXPECT_TRUE(bind_result.has_value()) << "bind_po() failed: " << bind_result.error().message;

    return {po_id, tu_id, root_hash, bug_hash};
}

void write_json_file(const std::string& path, const nlohmann::json& payload) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << sappp::canonical::canonicalize(payload);
}

} // namespace

TEST(ValidatorTest, ValidatesBugTrace) {
    TempDir temp_dir("sappp_validator_bug");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    nlohmann::json results = validator.validate(false);

    ASSERT_EQ(results.at("results").size(), 1u);
    const nlohmann::json& entry = results.at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, DowngradesOnHashMismatch) {
    TempDir temp_dir("sappp_validator_hash_mismatch");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    std::string bug_path = object_path_for_hash(temp_dir.path(), bundle.bug_trace_hash);
    std::ifstream bug_stream(bug_path);
    nlohmann::json bug_trace = nlohmann::json::parse(bug_stream);
    bug_trace.at("steps").at(0).at("ir")["block_id"] = "B2";
    write_json_file(bug_path, bug_trace);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    nlohmann::json results = validator.validate(false);

    const nlohmann::json& entry = results.at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "HashMismatch");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "HashMismatch");
}

TEST(ValidatorTest, DowngradesOnMissingDependency) {
    TempDir temp_dir("sappp_validator_missing_dep");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    std::string bug_path = object_path_for_hash(temp_dir.path(), bundle.bug_trace_hash);
    fs::remove(bug_path);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    nlohmann::json results = validator.validate(false);

    const nlohmann::json& entry = results.at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "MissingDependency");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "MissingDependency");
}
