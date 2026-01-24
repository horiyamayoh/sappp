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

sappp::VoidResult write_json_file(const std::string& path, const nlohmann::json& payload) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(sappp::Error::make("IOError",
            "Failed to open file for write: " + path));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical;
    if (!out) {
        return std::unexpected(sappp::Error::make("IOError",
            "Failed to write file: " + path));
    }
    return {};
}

sappp::VoidResult write_nir_file(const fs::path& base_dir, const nlohmann::json& nir) {
    fs::path frontend_dir = base_dir / "frontend";
    std::error_code ec;
    fs::create_directories(frontend_dir, ec);
    if (ec) {
        return std::unexpected(sappp::Error::make("IOError",
            "Failed to create frontend dir: " + frontend_dir.string() + ": " + ec.message()));
    }
    fs::path nir_path = frontend_dir / "nir.json";
    return write_json_file(nir_path.string(), nir);
}

nlohmann::json build_basic_nir(const std::string& tu_id) {
    return nlohmann::json{
        {"schema_version", "nir.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::kVersion},
            {"build_id", sappp::kBuildId}
        }},
        {"generated_at", "2025-01-01T00:00:00Z"},
        {"tu_id", tu_id},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"profile_version", sappp::kProfileVersion},
        {"functions", {
            {
                {"function_uid", "func1"},
                {"mangled_name", "_Z4testv"},
                {"cfg", {
                    {"entry", "B1"},
                    {"blocks", {
                        {
                            {"id", "B1"},
                            {"insts", {
                                {{"id", "I1"}, {"op", "stmt"}}
                            }}
                        }
                    }},
                    {"edges", nlohmann::json::array()}
                }}
            }
        }}
    };
}

nlohmann::json build_exception_nir(const std::string& tu_id, bool include_exception_edge) {
    nlohmann::json edges = nlohmann::json::array();
    edges.push_back({{"from", "B0"}, {"to", "B2"}, {"kind", "normal"}});
    edges.push_back({{"from", "B0"}, {"to", "B1"}, {"kind", include_exception_edge ? "exception" : "normal"}});

    return nlohmann::json{
        {"schema_version", "nir.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::kVersion},
            {"build_id", sappp::kBuildId}
        }},
        {"generated_at", "2025-01-01T00:00:00Z"},
        {"tu_id", tu_id},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"profile_version", sappp::kProfileVersion},
        {"functions", {
            {
                {"function_uid", "caller"},
                {"mangled_name", "_Z6callerv"},
                {"cfg", {
                    {"entry", "B0"},
                    {"blocks", {
                        {
                            {"id", "B0"},
                            {"insts", {
                                {{"id", "I0"}, {"op", "invoke"}}
                            }}
                        },
                        {
                            {"id", "B1"},
                            {"insts", {
                                {{"id", "I0"}, {"op", "landingpad"}},
                                {{"id", "I1"}, {"op", "dtor"}},
                                {{"id", "I2"}, {"op", "resume"}}
                            }}
                        },
                        {
                            {"id", "B2"},
                            {"insts", {
                                {{"id", "I0"}, {"op", "stmt"}}
                            }}
                        }
                    }},
                    {"edges", edges}
                }}
            },
            {
                {"function_uid", "callee"},
                {"mangled_name", "_Z6calleev"},
                {"cfg", {
                    {"entry", "B0"},
                    {"blocks", {
                        {
                            {"id", "B0"},
                            {"insts", {
                                {{"id", "I0"}, {"op", "stmt"}},
                                {{"id", "I1"}, {"op", "throw"}}
                            }}
                        }
                    }},
                    {"edges", nlohmann::json::array()}
                }}
            }
        }}
    };
}

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
            {"profile_version", sappp::kProfileVersion},
            {"semantics_version", sappp::kSemanticsVersion},
            {"proof_system_version", sappp::kProofSystemVersion},
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
            {"semantics_version", sappp::kSemanticsVersion},
            {"proof_system_version", sappp::kProofSystemVersion},
            {"profile_version", sappp::kProfileVersion}
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

CertBundle build_exception_cert_store(const fs::path& input_dir, const std::string& schema_dir) {
    fs::path certstore_dir = input_dir / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    std::string po_id = sappp::common::sha256_prefixed("po-exc");
    std::string tu_id = sappp::common::sha256_prefixed("tu-exc");

    nlohmann::json po_cert = {
        {"schema_version", "cert.v1"},
        {"kind", "PoDef"},
        {"po", {
            {"po_id", po_id},
            {"po_kind", "div0"},
            {"profile_version", sappp::kProfileVersion},
            {"semantics_version", sappp::kSemanticsVersion},
            {"proof_system_version", sappp::kProofSystemVersion},
            {"repo_identity", {
                {"path", "src/exception.cpp"},
                {"content_sha256", sappp::common::sha256_prefixed("content-exc")}
            }},
            {"function", {
                {"usr", "c:@F@caller"},
                {"mangled", "_Z6callerv"}
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
        {"function_uid", "caller"},
        {"block_id", "B0"},
        {"inst_id", "I0"}
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
                    {"function_uid", "caller"},
                    {"block_id", "B0"},
                    {"inst_id", "I0"}
                }}
            },
            {
                {"edge_kind", "call"},
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "callee"},
                    {"block_id", "B0"},
                    {"inst_id", "I0"}
                }}
            },
            {
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "callee"},
                    {"block_id", "B0"},
                    {"inst_id", "I1"}
                }}
            },
            {
                {"edge_kind", "unwind"},
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "caller"},
                    {"block_id", "B1"},
                    {"inst_id", "I0"}
                }}
            },
            {
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "caller"},
                    {"block_id", "B1"},
                    {"inst_id", "I1"}
                }}
            },
            {
                {"ir", {
                    {"schema_version", "cert.v1"},
                    {"kind", "IrRef"},
                    {"tu_id", tu_id},
                    {"function_uid", "caller"},
                    {"block_id", "B1"},
                    {"inst_id", "I2"}
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
            {"semantics_version", sappp::kSemanticsVersion},
            {"proof_system_version", sappp::kProofSystemVersion},
            {"profile_version", sappp::kProfileVersion}
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

} // namespace

TEST(ValidatorTest, ValidatesBugTrace) {
    TempDir temp_dir("sappp_validator_bug");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);
    auto nir_write = write_nir_file(temp_dir.path(), build_basic_nir(bundle.tu_id));
    ASSERT_TRUE(nir_write);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1u);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, ValidatesBugTraceWithExceptionFlow) {
    TempDir temp_dir("sappp_validator_exception");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_exception_cert_store(temp_dir.path(), schema_dir);
    auto nir_write = write_nir_file(temp_dir.path(), build_exception_nir(bundle.tu_id, true));
    ASSERT_TRUE(nir_write);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    ASSERT_EQ(results->at("results").size(), 1u);
    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "BUG");
    EXPECT_EQ(entry.at("validator_status"), "Validated");
    EXPECT_EQ(entry.at("certificate_root"), bundle.root_hash);
}

TEST(ValidatorTest, DowngradesOnInvalidExceptionTrace) {
    TempDir temp_dir("sappp_validator_exception_invalid");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_exception_cert_store(temp_dir.path(), schema_dir);
    auto nir_write = write_nir_file(temp_dir.path(), build_exception_nir(bundle.tu_id, false));
    ASSERT_TRUE(nir_write);

    sappp::validator::Validator validator(temp_dir.path().string(), schema_dir);
    auto results = validator.validate(false);
    ASSERT_TRUE(results);

    const nlohmann::json& entry = results->at("results").at(0);
    EXPECT_EQ(entry.at("category"), "UNKNOWN");
    EXPECT_EQ(entry.at("validator_status"), "ProofCheckFailed");
    EXPECT_EQ(entry.at("downgrade_reason_code"), "ProofCheckFailed");
}

TEST(ValidatorTest, DowngradesOnHashMismatch) {
    TempDir temp_dir("sappp_validator_hash_mismatch");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);
    auto nir_write = write_nir_file(temp_dir.path(), build_basic_nir(bundle.tu_id));
    ASSERT_TRUE(nir_write);

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

TEST(ValidatorTest, DowngradesOnMissingDependency) {
    TempDir temp_dir("sappp_validator_missing_dep");
    std::string schema_dir = SAPPP_SCHEMA_DIR;

    CertBundle bundle = build_cert_store(temp_dir.path(), schema_dir);
    auto nir_write = write_nir_file(temp_dir.path(), build_basic_nir(bundle.tu_id));
    ASSERT_TRUE(nir_write);

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
