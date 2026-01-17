#include "sappp/certstore.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path make_temp_dir() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::filesystem::path base = std::filesystem::temp_directory_path() /
        ("sappp_certstore_test_" + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

} // namespace

TEST(CertStoreDeterminism, PutIsDeterministicForEquivalentJson) {
    std::filesystem::path base = make_temp_dir();
    sappp::CertStore store(base);

    nlohmann::json cert_a = {
        {"b", 2},
        {"a", 1}
    };
    nlohmann::json cert_b = {
        {"a", 1},
        {"b", 2}
    };

    std::string hash_a = store.put(cert_a);
    std::string hash_b = store.put(cert_b);

    EXPECT_EQ(hash_a, hash_b);

    auto loaded = store.get(hash_a);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded.value(), cert_a);

    const std::string po_id = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    store.bind_po(po_id, hash_a);

    std::filesystem::path index_path = base / "index" / (po_id + ".json");
    ASSERT_TRUE(std::filesystem::exists(index_path));

    nlohmann::json index = nlohmann::json::parse(std::ifstream(index_path));
    EXPECT_EQ(index.at("schema_version"), "cert_index.v1");
    EXPECT_EQ(index.at("po_id"), po_id);
    EXPECT_EQ(index.at("root"), hash_a);

    std::filesystem::remove_all(base);
}
