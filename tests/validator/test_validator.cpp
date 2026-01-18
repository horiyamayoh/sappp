#include "sappp/canonical_json.hpp"
#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

/// RAII helper to create and clean up a temporary directory
class TempDir
{
public:
    explicit TempDir(const std::string& name)
        : m_path(fs::temp_directory_path() / name)
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

std::string object_path_for_hash(const fs::path& base_dir, const std::string& hash)
{
    constexpr std::string_view kPrefix = "sha256:";
    std::size_t digest_start = hash.starts_with(kPrefix) ? kPrefix.size() : 0;
    std::string shard = hash.substr(digest_start, 2);
    fs::path object_path = base_dir / "certstore" / "objects" / shard / (hash + ".json");
    return object_path.string();
}

[[nodiscard]] std::string put_cert_or_fail(sappp::certstore::CertStore& store,
                                           const nlohmann::json& cert,
                                           std::string_view label)
{
    auto result = store.put(cert);
    EXPECT_TRUE(result.has_value()) << "put(" << label << ") failed: " << result.error().message;
    if (!result) {
        return "";
    }
    return *result;
}

void bind_po_or_fail(sappp::certstore::CertStore& store,
                     const std::string& po_id,
                     const std::string& root_hash)
{
    auto bind_result = store.bind_po(po_id, root_hash);
    EXPECT_TRUE(bind_result.has_value()) << "bind_po() failed: " << bind_result.error().message;
}

[[nodiscard]] nlohmann::json make_po_cert(const std::string& po_id,
                                          const nlohmann::json& predicate_expr)
{
    return {
        {"schema_version",             "cert.v1"                          },
        {          "kind",                                         "PoDef"},
        {            "po",
         {{"po_id", po_id},
         {"po_kind", "div0"},
         {"profile_version", sappp::kProfileVersion},
         {"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"repo_identity",
         {{"path", "src/test.cpp"},
         {"content_sha256", sappp::common::sha256_prefixed("content")}}},
         {"function", {{"usr", "c:@F@test"}, {"mangled", "_Z4testv"}}},
         {"anchor", {{"block_id", "B1"}, {"inst_id", "I1"}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };
}

[[nodiscard]] nlohmann::json make_ir_cert(const std::string& tu_id)
{
    return {
        {"schema_version", "cert.v1"},
        {          "kind",   "IrRef"},
        {         "tu_id",     tu_id},
        {  "function_uid",   "func1"},
        {      "block_id",      "B1"},
        {       "inst_id",      "I1"}
    };
}

[[nodiscard]] nlohmann::json make_bug_trace(const std::string& po_id, const std::string& tu_id)
{
    return {
        {"schema_version",            "cert.v1"                          },
        {          "kind",                                     "BugTrace"},
        {    "trace_kind",                                   "ir_path.v1"},
        {         "steps",
         {{{"ir",
         {{"schema_version", "cert.v1"},
         {"kind", "IrRef"},
         {"tu_id", tu_id},
         {"function_uid", "func1"},
         {"block_id", "B1"},
         {"inst_id", "I1"}}}}}                                           },
        {     "violation", {{"po_id", po_id}, {"predicate_holds", false}}}
    };
}

[[nodiscard]] nlohmann::json make_safety_proof(const nlohmann::json& state)
{
    return {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "SafetyProof"},
        {        "domain", "interval+null+lifetime+init"},
        {        "points",
         {{{"ir", {{"function_uid", "func1"}, {"block_id", "B1"}, {"inst_id", "I1"}}},
         {"state", state}}}                             }
    };
}

[[nodiscard]] nlohmann::json make_proof_root(const std::string& po_hash,
                                             const std::string& ir_hash,
                                             const std::string& evidence_hash,
                                             std::string_view result)
{
    return {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "ProofRoot"},
        {            "po",          {{"ref", po_hash}}},
        {            "ir",          {{"ref", ir_hash}}},
        {      "evidence",    {{"ref", evidence_hash}}},
        {        "result",         std::string(result)},
        {       "depends",
         {{"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"profile_version", sappp::kProfileVersion}} },
        {    "hash_scope",             "hash_scope.v1"}
    };
}

struct CertBundle
{
    std::string po_id;
    std::string tu_id;
    std::string root_hash;
    std::string bug_trace_hash;
};

struct SafeCertBundle
{
    std::string po_id;
    std::string tu_id;
    std::string root_hash;
    std::string safety_proof_hash;
};

CertBundle build_cert_store(const fs::path& input_dir, const std::string& schema_dir)
{
    fs::path certstore_dir = input_dir / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-1");
    std::string tu_id = sappp::common::sha256_prefixed("tu-1");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };
    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);
    nlohmann::json bug_trace = make_bug_trace(po_id, tu_id);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string bug_hash = put_cert_or_fail(store, bug_trace, "bug_trace");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, bug_hash, "BUG");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    return {po_id, tu_id, root_hash, bug_hash};
}

SafeCertBundle build_safe_cert_store(const fs::path& input_dir,
                                     const std::string& schema_dir,
                                     bool include_predicate)
{
    fs::path certstore_dir = input_dir / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-safe-1");
    std::string tu_id = sappp::common::sha256_prefixed("tu-safe-1");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };
    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    nlohmann::json state = nlohmann::json::object();
    if (include_predicate) {
        state["predicates"] = nlohmann::json::array({predicate_expr});
    }
    nlohmann::json safety_proof = make_safety_proof(state);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string safety_hash = put_cert_or_fail(store, safety_proof, "safety_proof");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, safety_hash, "SAFE");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    return {po_id, tu_id, root_hash, safety_hash};
}

sappp::VoidResult write_json_file(const std::string& path, const nlohmann::json& payload)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open file for write: " + path));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical;
    if (!out) {
        return std::unexpected(sappp::Error::make("IOError", "Failed to write file: " + path));
    }
    return {};
}

}  // namespace

TEST(ValidatorTest, ValidatesBugTrace)
{
    TempDir temp_dir("sappp_validator_bug");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, DowngradesOnHashMismatch)
{
    TempDir temp_dir("sappp_validator_hash_mismatch");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    std::string bug_path = object_path_for_hash(temp_dir.path(), bundle.bug_trace_hash);
    std::ifstream bug_stream(bug_path);
    nlohmann::json bug_trace = nlohmann::json::parse(bug_stream);
    bug_trace.at("steps").at(0).at("ir")["block_id"] = "B2";
    auto write_result = write_json_file(bug_path, bug_trace);
    ASSERT_TRUE(write_result);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "HashMismatch");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "HashMismatch");
}

TEST(ValidatorTest, DowngradesOnMissingDependency)
{
    TempDir temp_dir("sappp_validator_missing_dep");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);

    std::string bug_path = object_path_for_hash(temp_dir.path(), bundle.bug_trace_hash);
    fs::remove(bug_path);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "MissingDependency");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "MissingDependency");
}

TEST(ValidatorTest, ValidatesSafetyProof)
{
    TempDir temp_dir("sappp_validator_safe");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    SafeCertBundle bundle = build_safe_cert_store(temp_dir.path(), schema_dir, true);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "SAFE");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, DowngradesOnMissingSafetyPredicate)
{
    TempDir temp_dir("sappp_validator_safe_missing_predicate");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    build_safe_cert_store(temp_dir.path(), schema_dir, false);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "ProofCheckFailed");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "ProofCheckFailed");
}

// Test: Unsupported domain causes downgrade to UNKNOWN
TEST(ValidatorTest, DowngradesOnUnsupportedDomain)
{
    TempDir temp_dir("sappp_validator_unsupported_domain");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-domain-test");
    std::string tu_id = sappp::common::sha256_prefixed("tu-domain-test");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = {
        {"schema_version",             "cert.v1"                          },
        {          "kind",                                         "PoDef"},
        {            "po",
         {{"po_id", po_id},
         {"po_kind", "div0"},
         {"profile_version", sappp::kProfileVersion},
         {"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"repo_identity",
         {{"path", "src/test.cpp"},
         {"content_sha256", sappp::common::sha256_prefixed("content")}}},
         {"function", {{"usr", "c:@F@test"}, {"mangled", "_Z4testv"}}},
         {"anchor", {{"block_id", "B1"}, {"inst_id", "I1"}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };

    nlohmann::json ir_cert = {
        {"schema_version", "cert.v1"},
        {          "kind",   "IrRef"},
        {         "tu_id",     tu_id},
        {  "function_uid",   "func1"},
        {      "block_id",      "B1"},
        {       "inst_id",      "I1"}
    };

    // Use an unsupported domain
    nlohmann::json safety_proof = {
        {"schema_version","cert.v1"                          },
        {          "kind",                   "SafetyProof"},
        {        "domain",            "unsupported-domain"},
        {        "points",
         {{{"ir", {{"function_uid", "func1"}, {"block_id", "B1"}, {"inst_id", "I1"}}},
         {"state", {{"predicates", {predicate_expr}}}}}}  }
    };

    auto po_result = store.put(po_cert);
    ASSERT_TRUE(po_result.has_value());
    auto ir_result = store.put(ir_cert);
    ASSERT_TRUE(ir_result.has_value());
    auto safety_result = store.put(safety_proof);
    ASSERT_TRUE(safety_result.has_value());

    nlohmann::json proof_root = {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "ProofRoot"},
        {            "po",       {{"ref", *po_result}}},
        {            "ir",       {{"ref", *ir_result}}},
        {      "evidence",   {{"ref", *safety_result}}},
        {        "result",                      "SAFE"},
        {       "depends",
         {{"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"profile_version", sappp::kProfileVersion}} },
        {    "hash_scope",             "hash_scope.v1"}
    };

    auto root_result = store.put(proof_root);
    ASSERT_TRUE(root_result.has_value());
    auto bind_result = store.bind_po(po_id, *root_result);
    ASSERT_TRUE(bind_result.has_value());

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "UnsupportedProofFeature");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "UnsupportedProofFeature");
}

// Test: Missing anchor (inst_id mismatch) causes downgrade to UNKNOWN
TEST(ValidatorTest, DowngradesOnMissingAnchor)
{
    TempDir temp_dir("sappp_validator_missing_anchor");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-anchor-test");
    std::string tu_id = sappp::common::sha256_prefixed("tu-anchor-test");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = {
        {"schema_version",             "cert.v1"                          },
        {          "kind",                                         "PoDef"},
        {            "po",
         {{"po_id", po_id},
         {"po_kind", "div0"},
         {"profile_version", sappp::kProfileVersion},
         {"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"repo_identity",
         {{"path", "src/test.cpp"},
         {"content_sha256", sappp::common::sha256_prefixed("content")}}},
         {"function", {{"usr", "c:@F@test"}, {"mangled", "_Z4testv"}}},
         {"anchor", {{"block_id", "B1"}, {"inst_id", "I1"}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };

    nlohmann::json ir_cert = {
        {"schema_version", "cert.v1"},
        {          "kind",   "IrRef"},
        {         "tu_id",     tu_id},
        {  "function_uid",   "func1"},
        {      "block_id",      "B1"},
        {       "inst_id",      "I1"}
    };

    // Safety proof with different block_id - should not match anchor
    nlohmann::json safety_proof = {
        {"schema_version","cert.v1"                          },
        {          "kind",                   "SafetyProof"},
        {        "domain",   "interval+null+lifetime+init"},
        {        "points",
         {{{"ir", {{"function_uid", "func1"}, {"block_id", "B2"}, {"inst_id", "I1"}}},
         {"state", {{"predicates", {predicate_expr}}}}}}  }
    };

    auto po_result = store.put(po_cert);
    ASSERT_TRUE(po_result.has_value());
    auto ir_result = store.put(ir_cert);
    ASSERT_TRUE(ir_result.has_value());
    auto safety_result = store.put(safety_proof);
    ASSERT_TRUE(safety_result.has_value());

    nlohmann::json proof_root = {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "ProofRoot"},
        {            "po",       {{"ref", *po_result}}},
        {            "ir",       {{"ref", *ir_result}}},
        {      "evidence",   {{"ref", *safety_result}}},
        {        "result",                      "SAFE"},
        {       "depends",
         {{"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"profile_version", sappp::kProfileVersion}} },
        {    "hash_scope",             "hash_scope.v1"}
    };

    auto root_result = store.put(proof_root);
    ASSERT_TRUE(root_result.has_value());
    auto bind_result = store.bind_po(po_id, *root_result);
    ASSERT_TRUE(bind_result.has_value());

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "ProofCheckFailed");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "ProofCheckFailed");
}

// Test: Block-level invariant (no inst_id) cannot validate instruction-level PO
TEST(ValidatorTest, DowngradesOnBlockLevelInvariantForInstructionPO)
{
    TempDir temp_dir("sappp_validator_block_level");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-block-level-test");
    std::string tu_id = sappp::common::sha256_prefixed("tu-block-level-test");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    // PO with instruction-level anchor
    nlohmann::json po_cert = {
        {"schema_version",             "cert.v1"                          },
        {          "kind",                                         "PoDef"},
        {            "po",
         {{"po_id", po_id},
         {"po_kind", "div0"},
         {"profile_version", sappp::kProfileVersion},
         {"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"repo_identity",
         {{"path", "src/test.cpp"},
         {"content_sha256", sappp::common::sha256_prefixed("content")}}},
         {"function", {{"usr", "c:@F@test"}, {"mangled", "_Z4testv"}}},
         {"anchor", {{"block_id", "B1"}, {"inst_id", "I5"}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };

    nlohmann::json ir_cert = {
        {"schema_version", "cert.v1"},
        {          "kind",   "IrRef"},
        {         "tu_id",     tu_id},
        {  "function_uid",   "func1"},
        {      "block_id",      "B1"},
        {       "inst_id",      "I5"}
    };

    // Block-level invariant without inst_id - should NOT match instruction-level PO
    nlohmann::json safety_proof = {
        {"schema_version","cert.v1"                          },
        {          "kind",                   "SafetyProof"},
        {        "domain",   "interval+null+lifetime+init"},
        {        "points",
         {{{"ir", {{"function_uid", "func1"}, {"block_id", "B1"}}},
         {"state", {{"predicates", {predicate_expr}}}}}}  }
    };

    auto po_result = store.put(po_cert);
    ASSERT_TRUE(po_result.has_value());
    auto ir_result = store.put(ir_cert);
    ASSERT_TRUE(ir_result.has_value());
    auto safety_result = store.put(safety_proof);
    ASSERT_TRUE(safety_result.has_value());

    nlohmann::json proof_root = {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "ProofRoot"},
        {            "po",       {{"ref", *po_result}}},
        {            "ir",       {{"ref", *ir_result}}},
        {      "evidence",   {{"ref", *safety_result}}},
        {        "result",                      "SAFE"},
        {       "depends",
         {{"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"profile_version", sappp::kProfileVersion}} },
        {    "hash_scope",             "hash_scope.v1"}
    };

    auto root_result = store.put(proof_root);
    ASSERT_TRUE(root_result.has_value());
    auto bind_result = store.bind_po(po_id, *root_result);
    ASSERT_TRUE(bind_result.has_value());

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "ProofCheckFailed");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "ProofCheckFailed");
}
