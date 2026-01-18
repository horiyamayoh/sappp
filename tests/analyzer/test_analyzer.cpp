#include "sappp/analyzer.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/version.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

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

nlohmann::json make_po_entry(const std::string& po_id,
                             const std::string& po_kind,
                             const std::string& function_uid,
                             const std::string& mangled,
                             const std::string& block_id,
                             const std::string& inst_id) {
    return nlohmann::json{
        {"po_id", po_id},
        {"po_kind", po_kind},
        {"profile_version", sappp::kProfileVersion},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"repo_identity", {
            {"path", "src/test.cpp"},
            {"content_sha256", sappp::common::sha256_prefixed("content")}
        }},
        {"function", {
            {"usr", function_uid},
            {"mangled", mangled}
        }},
        {"anchor", {
            {"block_id", block_id},
            {"inst_id", inst_id}
        }},
        {"predicate", {
            {"expr", {
                {"op", "ub.check"},
                {"args", nlohmann::json::array({po_kind})}
            }},
            {"pretty", "ub.check"}
        }}
    };
}

nlohmann::json make_po_list(const std::vector<nlohmann::json>& pos) {
    return nlohmann::json{
        {"schema_version", "po.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::kVersion},
            {"build_id", sappp::kBuildId}
        }},
        {"generated_at", "2024-01-01T00:00:00Z"},
        {"tu_id", sappp::common::sha256_prefixed("tu-test")},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"profile_version", sappp::kProfileVersion},
        {"pos", pos}
    };
}

} // namespace

TEST(AnalyzerDeterminismTest, StableUnknownAndCertIndex) {
    TempDir temp_dir_a("sappp_analyzer_a");
    TempDir temp_dir_b("sappp_analyzer_b");

    const std::string po_id_a = sappp::common::sha256_prefixed("po-a");
    const std::string po_id_b = sappp::common::sha256_prefixed("po-b");

    nlohmann::json po_a = make_po_entry(po_id_a, "UB.DivZero", "c:@F@testA", "_Z5testAv", "B1", "I1");
    nlohmann::json po_b = make_po_entry(po_id_b, "UB.NullDeref", "c:@F@testB", "_Z5testBv", "B2", "I2");

    nlohmann::json po_list_order_a = make_po_list({po_a, po_b});
    nlohmann::json po_list_order_b = make_po_list({po_b, po_a});

    sappp::analyzer::Analyzer analyzer(SAPPP_SCHEMA_DIR);

    auto result_a = analyzer.analyze(po_list_order_a, temp_dir_a.path().string());
    ASSERT_TRUE(result_a.has_value()) << result_a.error().message;

    auto result_b = analyzer.analyze(po_list_order_b, temp_dir_b.path().string());
    ASSERT_TRUE(result_b.has_value()) << result_b.error().message;

    ASSERT_EQ(result_a->cert_index.size(), 2u);
    ASSERT_EQ(result_b->cert_index.size(), 2u);
    EXPECT_EQ(result_a->cert_index, result_b->cert_index);

    const auto& unknowns_a = result_a->unknown_ledger.at("unknowns");
    const auto& unknowns_b = result_b->unknown_ledger.at("unknowns");

    ASSERT_EQ(unknowns_a.size(), 1u);
    ASSERT_EQ(unknowns_b.size(), 1u);
    EXPECT_EQ(unknowns_a, unknowns_b);

    const std::string bug_po_id = std::min(po_id_a, po_id_b);
    const std::string unknown_po_id = (bug_po_id == po_id_a) ? po_id_b : po_id_a;

    const std::string unknown_code = "DomainTooWeak.Numeric";
    nlohmann::json id_input = {
        {"po_id", unknown_po_id},
        {"unknown_code", unknown_code},
        {"semantics_version", sappp::kSemanticsVersion},
        {"proof_system_version", sappp::kProofSystemVersion},
        {"profile_version", sappp::kProfileVersion}
    };
    auto expected_id = sappp::canonical::hash_canonical(id_input);
    ASSERT_TRUE(expected_id.has_value());

    EXPECT_EQ(unknowns_a.at(0).at("po_id").get<std::string>(), unknown_po_id);
    EXPECT_EQ(unknowns_a.at(0).at("unknown_code").get<std::string>(), unknown_code);
    EXPECT_EQ(unknowns_a.at(0).at("unknown_stable_id").get<std::string>(), *expected_id);
}
