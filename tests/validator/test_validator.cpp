#include "sappp/canonical_json.hpp"
#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/validator.hpp"
#include "sappp/version.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;
constexpr std::string_view kTestFunctionUid = "func1";
constexpr std::string_view kTestCalleeUid = "func2";
constexpr std::string_view kTestBlockId = "B1";
constexpr std::string_view kTestMangledName = "_Z4testv";
constexpr std::string_view kTestCalleeMangledName = "_Z6calleev";
constexpr std::string_view kDefaultSafetyDomain = "interval+null+lifetime+init";

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
                                          const nlohmann::json& predicate_expr,
                                          std::string_view inst_id = "I1",
                                          std::string_view block_id = kTestBlockId)
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
         {"function", {{"usr", "c:@F@test"}, {"mangled", std::string(kTestMangledName)}}},
         {"anchor", {{"block_id", std::string(block_id)}, {"inst_id", std::string(inst_id)}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };
}

[[nodiscard]] nlohmann::json make_ir_cert(const std::string& tu_id,
                                          std::string_view inst_id = "I1",
                                          std::string_view block_id = kTestBlockId)
{
    return {
        {"schema_version",                     "cert.v1"},
        {          "kind",                       "IrRef"},
        {         "tu_id",                         tu_id},
        {  "function_uid", std::string(kTestFunctionUid)},
        {      "block_id",         std::string(block_id)},
        {       "inst_id",          std::string(inst_id)}
    };
}

[[nodiscard]] nlohmann::json
make_trace_step(const std::string& tu_id,
                std::string_view function_uid,
                std::string_view block_id,
                std::string_view inst_id,
                std::optional<std::string_view> edge_kind = std::nullopt)
{
    nlohmann::json step = {
        {"ir",
         {{"schema_version", "cert.v1"},
          {"kind", "IrRef"},
          {"tu_id", tu_id},
          {"function_uid", std::string(function_uid)},
          {"block_id", std::string(block_id)},
          {"inst_id", std::string(inst_id)}}}
    };
    if (edge_kind) {
        step["edge_kind"] = std::string(*edge_kind);
    }
    return step;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - Test helper signature is stable.
[[nodiscard]] nlohmann::json make_bug_trace(const std::string& po_id,
                                            [[maybe_unused]] const std::string& tu_id,
                                            const std::vector<nlohmann::json>& steps)
{
    return {
        {"schema_version",                                      "cert.v1"},
        {          "kind",                                     "BugTrace"},
        {    "trace_kind",                                   "ir_path.v1"},
        {         "steps",                                          steps},
        {     "violation", {{"po_id", po_id}, {"predicate_holds", false}}}
    };
}

[[nodiscard]] nlohmann::json make_bug_trace(const std::string& po_id, const std::string& tu_id)
{
    return make_bug_trace(po_id,
                          tu_id,
                          {make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I1")});
}

[[nodiscard]] nlohmann::json make_safety_proof(const nlohmann::json& state,
                                               std::string_view domain = kDefaultSafetyDomain)
{
    return {
        {"schema_version","cert.v1"                          },
        {          "kind",       "SafetyProof"},
        {        "domain", std::string(domain)},
        {        "points",
         {{{"ir",
         {{"function_uid", std::string(kTestFunctionUid)},
         {"block_id", std::string(kTestBlockId)},
         {"inst_id", "I1"}}},
         {"state", state}}}                   }
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

    return CertBundle{.po_id = po_id,
                      .tu_id = tu_id,
                      .root_hash = root_hash,
                      .bug_trace_hash = bug_hash};
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

    return SafeCertBundle{.po_id = po_id,
                          .tu_id = tu_id,
                          .root_hash = root_hash,
                          .safety_proof_hash = safety_hash};
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

struct NirInstSpec
{
    std::string id;
    std::string op;
};

sappp::VoidResult write_nir_file(const fs::path& input_dir,
                                 const std::string& tu_id,
                                 std::string_view function_uid,
                                 const std::vector<NirInstSpec>& insts)
{
    nlohmann::json inst_list = nlohmann::json::array();
    for (const auto& inst : insts) {
        inst_list.push_back({
            {"id", inst.id},
            {"op", inst.op}
        });
    }

    nlohmann::json cfg = {
        { "entry",                              std::string(kTestBlockId)                  },
        {"blocks",
         nlohmann::json::array({{{"id", std::string(kTestBlockId)}, {"insts", inst_list}}})},
        { "edges",                                                  nlohmann::json::array()}
    };
    nlohmann::json function_json = {
        {"function_uid",     std::string(function_uid)},
        {"mangled_name", std::string(kTestMangledName)},
        {         "cfg",                           cfg}
    };

    nlohmann::json nir = {
        {      "schema_version",                                                                         "nir.v1"},
        {                "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {        "generated_at",                                                           "1970-01-01T00:00:00Z"},
        {               "tu_id",                                                                            tu_id},
        {   "semantics_version",                                                         sappp::kSemanticsVersion},
        {"proof_system_version",                                                       sappp::kProofSystemVersion},
        {     "profile_version",                                                           sappp::kProfileVersion},
        {           "functions",                                           nlohmann::json::array({function_json})}
    };

    fs::path nir_path = input_dir / "frontend" / "nir.json";
    std::error_code ec;
    fs::create_directories(nir_path.parent_path(), ec);
    if (ec) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to create NIR directory: " + ec.message()));
    }
    return write_json_file(nir_path.string(), nir);
}

sappp::VoidResult write_nir_file_custom(const fs::path& input_dir,
                                        const std::string& tu_id,
                                        const nlohmann::json& functions)
{
    if (!functions.is_array()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected functions array for custom NIR"));
    }
    nlohmann::json nir = {
        {      "schema_version",                                                                         "nir.v1"},
        {                "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {        "generated_at",                                                           "1970-01-01T00:00:00Z"},
        {               "tu_id",                                                                            tu_id},
        {   "semantics_version",                                                         sappp::kSemanticsVersion},
        {"proof_system_version",                                                       sappp::kProofSystemVersion},
        {     "profile_version",                                                           sappp::kProfileVersion},
        {           "functions",                                                                        functions}
    };

    fs::path nir_path = input_dir / "frontend" / "nir.json";
    std::error_code ec;
    fs::create_directories(nir_path.parent_path(), ec);
    if (ec) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to create NIR directory: " + ec.message()));
    }
    return write_json_file(nir_path.string(), nir);
}

}  // namespace

TEST(ValidatorTest, ValidatesBugTrace)
{
    TempDir temp_dir("sappp_validator_bug");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);
    auto nir_result = write_nir_file(temp_dir.path(),
                                     bundle.tu_id,
                                     kTestFunctionUid,
                                     {
                                         NirInstSpec{.id = "I1", .op = "ub.check"}
    });
    ASSERT_TRUE(nir_result);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, ValidatesBugTraceWithLifetimeExceptionVcall)
{
    TempDir temp_dir("sappp_validator_bug_ops");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-bug-ops");
    std::string tu_id = sappp::common::sha256_prefixed("tu-bug-ops");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };
    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    std::vector<nlohmann::json> steps = {
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I1"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I2"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I3"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I4"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I5"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I6"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I7"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I8"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I9"),
        make_trace_step(tu_id, kTestFunctionUid, kTestBlockId, "I10"),
    };
    nlohmann::json bug_trace = make_bug_trace(po_id, tu_id, steps);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string bug_hash = put_cert_or_fail(store, bug_trace, "bug_trace");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, bug_hash, "BUG");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    auto nir_result = write_nir_file(temp_dir.path(),
                                     tu_id,
                                     kTestFunctionUid,
                                     {
                                         NirInstSpec{ .id = "I1", .op = "lifetime.begin"},
                                         NirInstSpec{ .id = "I2",           .op = "ctor"},
                                         NirInstSpec{ .id = "I3",           .op = "move"},
                                         NirInstSpec{ .id = "I4",           .op = "dtor"},
                                         NirInstSpec{ .id = "I5",         .op = "invoke"},
                                         NirInstSpec{ .id = "I6",          .op = "throw"},
                                         NirInstSpec{ .id = "I7",     .op = "landingpad"},
                                         NirInstSpec{ .id = "I8",         .op = "resume"},
                                         NirInstSpec{ .id = "I9",          .op = "vcall"},
                                         NirInstSpec{.id = "I10",   .op = "lifetime.end"}
    });
    ASSERT_TRUE(nir_result);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), root_hash);
}

TEST(ValidatorTest, ValidatesBugTraceWithExceptionEdge)
{
    TempDir temp_dir("sappp_validator_bug_exception_edge");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-bug-exception");
    std::string tu_id = sappp::common::sha256_prefixed("tu-bug-exception");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };
    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr, "I2");
    nlohmann::json ir_cert = make_ir_cert(tu_id, "I2");

    std::vector<nlohmann::json> steps = {
        make_trace_step(tu_id, kTestFunctionUid, "B0", "I1"),
        make_trace_step(tu_id, kTestFunctionUid, "B1", "I2", "exception"),
    };
    nlohmann::json bug_trace = make_bug_trace(po_id, tu_id, steps);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string bug_hash = put_cert_or_fail(store, bug_trace, "bug_trace");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, bug_hash, "BUG");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    nlohmann::json function_json = {
        {"function_uid",              std::string(kTestFunctionUid)                        },
        {"mangled_name",                                      std::string(kTestMangledName)},
        {         "cfg",
         {{"entry", "B0"},
         {"blocks",
         nlohmann::json::array({
         {{"id", "B0"}, {"insts", nlohmann::json::array({{{"id", "I1"}, {"op", "invoke"}}})}},
         {{"id", "B1"},
         {"insts", nlohmann::json::array({{{"id", "I2"}, {"op", "landingpad"}}})}},
         })},
         {"edges",
         nlohmann::json::array({{{"from", "B0"}, {"to", "B1"}, {"kind", "exception"}}})}}  }
    };
    auto nir_result =
        write_nir_file_custom(temp_dir.path(), tu_id, nlohmann::json::array({function_json}));
    ASSERT_TRUE(nir_result);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), root_hash);
}

TEST(ValidatorTest, ValidatesBugTraceWithCallStack)
{
    TempDir temp_dir("sappp_validator_bug_callstack");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-bug-callstack");
    std::string tu_id = sappp::common::sha256_prefixed("tu-bug-callstack");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };
    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr, "I2");
    nlohmann::json ir_cert = make_ir_cert(tu_id, "I2");

    std::vector<nlohmann::json> steps = {
        make_trace_step(tu_id, kTestFunctionUid, "B0", "I1"),
        make_trace_step(tu_id, kTestCalleeUid, "B0", "I1"),
        make_trace_step(tu_id, kTestFunctionUid, "B0", "I2"),
    };
    nlohmann::json bug_trace = make_bug_trace(po_id, tu_id, steps);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string bug_hash = put_cert_or_fail(store, bug_trace, "bug_trace");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, bug_hash, "BUG");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    nlohmann::json caller_function = {
        {"function_uid",std::string(kTestFunctionUid)                        },
        {"mangled_name", std::string(kTestMangledName)},
        {         "cfg",
         {{"entry", "B0"},
         {"blocks",
         nlohmann::json::array(
         {{{"id", "B0"},
         {"insts",
         nlohmann::json::array(
         {{{"id", "I1"}, {"op", "call"}}, {{"id", "I2"}, {"op", "ub.check"}}})}}})},
         {"edges", nlohmann::json::array()}}          }
    };
    nlohmann::json callee_function = {
        {"function_uid",std::string(kTestCalleeUid)                        },
        {"mangled_name", std::string(kTestCalleeMangledName)},
        {         "cfg",
         {{"entry", "B0"},
         {"blocks",
         nlohmann::json::array(
         {{{"id", "B0"},
         {"insts", nlohmann::json::array({{{"id", "I1"}, {"op", "ret"}}})}}})},
         {"edges", nlohmann::json::array()}}                }
    };
    auto nir_result =
        write_nir_file_custom(temp_dir.path(),
                              tu_id,
                              nlohmann::json::array({caller_function, callee_function}));
    ASSERT_TRUE(nir_result);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), root_hash);
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

TEST(ValidatorTest, AcceptsLargeNumericIntervals)
{
    TempDir temp_dir("sappp_validator_large_numeric");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-safe-large");
    std::string tu_id = sappp::common::sha256_prefixed("tu-safe-large");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    nlohmann::json state = {
        {"predicates",nlohmann::json::array({predicate_expr})                  },
        {   "numeric",
         nlohmann::json::array(
         {{{"var", "x"}, {"lo", 3'000'000'000LL}, {"hi", 4'000'000'000LL}}})}
    };
    nlohmann::json safety_proof = make_safety_proof(state);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string safety_hash = put_cert_or_fail(store, safety_proof, "safety_proof");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, safety_hash, "SAFE");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "SAFE");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), root_hash);
}

TEST(ValidatorTest, AcceptsLifetimeAndInitState)
{
    TempDir temp_dir("sappp_validator_lifetime_init");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-safe-lifetime");
    std::string tu_id = sappp::common::sha256_prefixed("tu-safe-lifetime");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    nlohmann::json state = {
        {"predicates",        nlohmann::json::array({predicate_expr})                  },
        {  "lifetime",
         nlohmann::json::array(
         {{{"obj", "obj1"}, {"value", "alive"}}, {{"obj", "obj2"}, {"value", "dead"}}})},
        {      "init",
         nlohmann::json::array(
         {{{"var", "x"}, {"value", "init"}}, {{"var", "y"}, {"value", "uninit"}}})     }
    };
    nlohmann::json safety_proof = make_safety_proof(state);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string safety_hash = put_cert_or_fail(store, safety_proof, "safety_proof");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, safety_hash, "SAFE");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1U);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "SAFE");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), root_hash);
}

TEST(ValidatorTest, DowngradesOnInconsistentAbstractState)
{
    TempDir temp_dir("sappp_validator_safe_inconsistent_state");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-safe-abs");
    std::string tu_id = sappp::common::sha256_prefixed("tu-safe-abs");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    nlohmann::json state = {
        {"predicates",     nlohmann::json::array({predicate_expr})                  },
        {  "nullness",
         nlohmann::json::array(
         {{{"var", "p"}, {"value", "null"}}, {{"var", "p"}, {"value", "non-null"}}})}
    };
    nlohmann::json safety_proof = make_safety_proof(state);

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string safety_hash = put_cert_or_fail(store, safety_proof, "safety_proof");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, safety_hash, "SAFE");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "ProofCheckFailed");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "ProofCheckFailed");
}

TEST(ValidatorTest, DowngradesOnTuIdMismatchAcrossEntries)
{
    TempDir temp_dir("sappp_validator_tu_id_mismatch");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    std::string po_id_a = sappp::common::sha256_prefixed("po-safe-a");
    std::string tu_id_a = sappp::common::sha256_prefixed("tu-safe-a");

    nlohmann::json po_cert_a = make_po_cert(po_id_a, predicate_expr);
    nlohmann::json ir_cert_a = make_ir_cert(tu_id_a);
    nlohmann::json state_a = {
        {"predicates", nlohmann::json::array({predicate_expr})}
    };
    nlohmann::json safety_proof_a = make_safety_proof(state_a);

    std::string po_hash_a = put_cert_or_fail(store, po_cert_a, "po_cert_a");
    std::string ir_hash_a = put_cert_or_fail(store, ir_cert_a, "ir_cert_a");
    std::string safety_hash_a = put_cert_or_fail(store, safety_proof_a, "safety_proof_a");
    nlohmann::json proof_root_a = make_proof_root(po_hash_a, ir_hash_a, safety_hash_a, "SAFE");
    std::string root_hash_a = put_cert_or_fail(store, proof_root_a, "proof_root_a");
    bind_po_or_fail(store, po_id_a, root_hash_a);

    std::string po_id_b = sappp::common::sha256_prefixed("po-safe-b");
    std::string tu_id_b = sappp::common::sha256_prefixed("tu-safe-b");

    nlohmann::json po_cert_b = make_po_cert(po_id_b, predicate_expr);
    nlohmann::json ir_cert_b = make_ir_cert(tu_id_b);
    nlohmann::json state_b = {
        {"predicates", nlohmann::json::array({predicate_expr})}
    };
    nlohmann::json safety_proof_b = make_safety_proof(state_b);

    std::string po_hash_b = put_cert_or_fail(store, po_cert_b, "po_cert_b");
    std::string ir_hash_b = put_cert_or_fail(store, ir_cert_b, "ir_cert_b");
    std::string safety_hash_b = put_cert_or_fail(store, safety_proof_b, "safety_proof_b");
    nlohmann::json proof_root_b = make_proof_root(po_hash_b, ir_hash_b, safety_hash_b, "SAFE");
    std::string root_hash_b = put_cert_or_fail(store, proof_root_b, "proof_root_b");
    bind_po_or_fail(store, po_id_b, root_hash_b);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    std::size_t safe_count = 0;
    std::size_t unknown_count = 0;
    for (const auto& entry : results->at("results")) {
        if (entry.at("category") == "SAFE") {
            ++safe_count;
        } else if (entry.at("category") == "UNKNOWN") {
            ++unknown_count;
            EXPECT_EQ(entry.at("validator_status"), "RuleViolation");
            EXPECT_EQ(entry.at("downgrade_reason_code"), "RuleViolation");
        }
    }

    EXPECT_EQ(safe_count, 1U);
    EXPECT_EQ(unknown_count, 1U);
    EXPECT_TRUE(results->at("tu_id") == tu_id_a || results->at("tu_id") == tu_id_b);
}

TEST(ValidatorTest, DowngradesOnConflictingPointsToState)
{
    TempDir temp_dir("sappp_validator_safe_points_to_conflict");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    fs::path certstore_dir = temp_dir.path() / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-safe-points");
    std::string tu_id = sappp::common::sha256_prefixed("tu-safe-points");

    nlohmann::json predicate_expr = {
        {"op", "neq"}
    };

    nlohmann::json po_cert = make_po_cert(po_id, predicate_expr);
    nlohmann::json ir_cert = make_ir_cert(tu_id);

    nlohmann::json state = {
        {"predicates",             nlohmann::json::array({predicate_expr})},
        { "points_to",
         nlohmann::json::array({{{"ptr", "p"},
         {"targets",
         nlohmann::json::array({nlohmann::json{{"alloc_site", "alloc1"},
         {"field", "root"}}})}},
         {{"ptr", "p"},
         {"targets",
         nlohmann::json::array({nlohmann::json{{"alloc_site", "alloc2"},
         {"field", "root"}}})}}})                                         }
    };
    nlohmann::json safety_proof =
        make_safety_proof(state, "interval+null+lifetime+init+points-to.simple");

    std::string po_hash = put_cert_or_fail(store, po_cert, "po_cert");
    std::string ir_hash = put_cert_or_fail(store, ir_cert, "ir_cert");
    std::string safety_hash = put_cert_or_fail(store, safety_proof, "safety_proof");

    nlohmann::json proof_root = make_proof_root(po_hash, ir_hash, safety_hash, "SAFE");
    std::string root_hash = put_cert_or_fail(store, proof_root, "proof_root");

    bind_po_or_fail(store, po_id, root_hash);

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
