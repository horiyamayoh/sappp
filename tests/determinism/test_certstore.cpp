/**
 * @file test_certstore.cpp
 * @brief CertStore determinism tests
 *
 * C++23 modernization:
 * - Using std::expected API for CertStore
 */

#include "sappp/certstore.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

using sappp::certstore::CertStore;
using json = nlohmann::json;

namespace {

json make_ir_ref_cert()
{
    return json{
        {"schema_version",                                                                 "cert.v1"},
        {          "kind",                                                                   "IrRef"},
        {         "tu_id", "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {  "function_uid",                                                           "test_function"},
        {      "block_id",                                                                      "B1"},
        {       "inst_id",                                                                      "I1"}
    };
}

/// RAII helper to create and clean up a temporary directory
class TempDir
{
public:
    explicit TempDir(const std::string& name)
        : m_path(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

}  // namespace

TEST(CertStore, PutGetDeterminism)
{
    TempDir temp_dir("sappp_certstore_test");

    CertStore store(temp_dir.path().string(), SAPPP_SCHEMA_DIR);
    json cert = make_ir_ref_cert();

    auto result1 = store.put(cert);
    ASSERT_TRUE(result1.has_value()) << "put() failed: " << result1.error().message;
    std::string hash1 = *result1;

    auto result2 = store.put(cert);
    ASSERT_TRUE(result2.has_value()) << "put() failed: " << result2.error().message;
    std::string hash2 = *result2;

    EXPECT_EQ(hash1, hash2);

    // Verify file is stored at correct sharded path: objects/<first2_hex>/<hash>.json
    // hash_canonical returns "sha256:<hex>", so the shard should be the first two
    // characters of the hex digest (after the "sha256:" prefix).
    constexpr std::string_view prefix = "sha256:";
    ASSERT_TRUE(hash1.starts_with(prefix)) << "Expected sha256: prefix in hash";
    std::string digest = hash1.substr(prefix.size());
    std::string shard = digest.substr(0, 2);
    std::filesystem::path expected_object_path =
        temp_dir.path() / "objects" / shard / (hash1 + ".json");
    ASSERT_TRUE(std::filesystem::exists(expected_object_path))
        << "Certificate not stored at expected path: " << expected_object_path;

    auto fetched = store.get(hash1);
    ASSERT_TRUE(fetched.has_value()) << "get() failed: " << fetched.error().message;
    EXPECT_EQ(*fetched, cert);

    std::string po_id = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    auto bind_result = store.bind_po(po_id, hash1);
    ASSERT_TRUE(bind_result.has_value()) << "bind_po() failed: " << bind_result.error().message;

    std::filesystem::path index_path = temp_dir.path() / "index" / (po_id + ".json");
    ASSERT_TRUE(std::filesystem::exists(index_path));

    std::ifstream in(index_path);
    ASSERT_TRUE(in.is_open());

    json index = json::parse(in);
    EXPECT_EQ(index.at("schema_version"), "cert_index.v1");
    EXPECT_EQ(index.at("po_id"), po_id);
    EXPECT_EQ(index.at("root"), hash1);
}
