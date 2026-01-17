/**
 * @file test_certstore.cpp
 * @brief CertStore determinism tests
 */

#include "sappp/certstore.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using sappp::certstore::CertStore;
using json = nlohmann::json;

namespace {

json make_ir_ref_cert() {
    return json{
        {"schema_version", "cert.v1"},
        {"kind", "IrRef"},
        {"tu_id", "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {"function_uid", "test_function"},
        {"block_id", "B1"},
        {"inst_id", "I1"}
    };
}

} // namespace

TEST(CertStore, PutGetDeterminism) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "sappp_certstore_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    CertStore store(temp_dir.string(), SAPPP_SCHEMA_DIR);
    json cert = make_ir_ref_cert();

    std::string hash1 = store.put(cert);
    std::string hash2 = store.put(cert);

    EXPECT_EQ(hash1, hash2);

    auto fetched = store.get(hash1);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(*fetched, cert);

    std::string po_id = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    store.bind_po(po_id, hash1);

    std::filesystem::path index_path = temp_dir / "index" / (po_id + ".json");
    ASSERT_TRUE(std::filesystem::exists(index_path));

    std::ifstream in(index_path);
    ASSERT_TRUE(in.is_open());

    json index = json::parse(in);
    EXPECT_EQ(index.at("schema_version"), "cert_index.v1");
    EXPECT_EQ(index.at("po_id"), po_id);
    EXPECT_EQ(index.at("root"), hash1);
}
