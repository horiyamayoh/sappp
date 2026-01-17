/**
 * @file test_sha256.cpp
 * @brief SHA-256 determinism tests
 */

#include "sappp/common.hpp"
#include <gtest/gtest.h>

using namespace sappp::common;

TEST(SHA256, EmptyString) {
    // SHA-256 of empty string is well-known
    EXPECT_EQ(sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SHA256, HelloWorld) {
    EXPECT_EQ(sha256("Hello, World!"), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f");
}

TEST(SHA256, Determinism) {
    // Same input must always produce same output
    std::string input = "test input for determinism";
    std::string hash1 = sha256(input);
    std::string hash2 = sha256(input);
    std::string hash3 = sha256(input);
    
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash2, hash3);
}

TEST(SHA256, Prefixed) {
    std::string hash = sha256_prefixed("test");
    EXPECT_TRUE(hash.rfind("sha256:", 0) == 0);
    EXPECT_EQ(hash.length(), 7 + 64); // "sha256:" + 64 hex chars
}

TEST(SHA256, DifferentInputs) {
    EXPECT_NE(sha256("a"), sha256("b"));
    EXPECT_NE(sha256("abc"), sha256("ABC"));
}
