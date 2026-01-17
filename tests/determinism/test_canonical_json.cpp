/**
 * @file test_canonical_json.cpp
 * @brief Canonical JSON determinism tests
 */

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace sappp::canonical;
using json = nlohmann::json;

TEST(CanonicalJSON, KeyOrder) {
    // Keys must be sorted lexicographically
    json j = {{"z", 1}, {"a", 2}, {"m", 3}};
    std::string canonical = canonicalize(j);
    EXPECT_EQ(canonical, R"({"a":2,"m":3,"z":1})");
}

TEST(CanonicalJSON, NestedKeyOrder) {
    json j = {
        {"outer", {{"z", 1}, {"a", 2}}},
        {"another", 3}
    };
    std::string canonical = canonicalize(j);
    EXPECT_EQ(canonical, R"({"another":3,"outer":{"a":2,"z":1}})");
}

TEST(CanonicalJSON, NoWhitespace) {
    json j = {{"key", "value"}, {"arr", {1, 2, 3}}};
    std::string canonical = canonicalize(j);
    // No spaces, no newlines
    EXPECT_EQ(canonical.find(' '), std::string::npos);
    EXPECT_EQ(canonical.find('\n'), std::string::npos);
}

TEST(CanonicalJSON, FloatRejection) {
    json j = {{"value", 3.14}};
    EXPECT_THROW(canonicalize(j), std::runtime_error);
}

TEST(CanonicalJSON, IntegerAllowed) {
    json j = {{"value", 42}};
    EXPECT_NO_THROW(canonicalize(j));
    EXPECT_EQ(canonicalize(j), R"({"value":42})");
}

TEST(CanonicalJSON, NegativeInteger) {
    json j = {{"value", -123}};
    EXPECT_EQ(canonicalize(j), R"({"value":-123})");
}

TEST(CanonicalJSON, Determinism) {
    json j = {
        {"id", "test-123"},
        {"values", {3, 1, 2}},
        {"nested", {{"b", 2}, {"a", 1}}}
    };
    
    std::string c1 = canonicalize(j);
    std::string c2 = canonicalize(j);
    std::string c3 = canonicalize(j);
    
    EXPECT_EQ(c1, c2);
    EXPECT_EQ(c2, c3);
}

TEST(CanonicalJSON, HashDeterminism) {
    json j = {{"key", "value"}};
    
    std::string h1 = hash_canonical(j);
    std::string h2 = hash_canonical(j);
    
    EXPECT_EQ(h1, h2);
    EXPECT_TRUE(h1.rfind("sha256:", 0) == 0);
}

TEST(CanonicalJSON, DifferentOrderSameHash) {
    // Same content, different insertion order
    json j1;
    j1["a"] = 1;
    j1["b"] = 2;
    
    json j2;
    j2["b"] = 2;
    j2["a"] = 1;
    
    EXPECT_EQ(hash_canonical(j1), hash_canonical(j2));
}

TEST(CanonicalJSON, ValidateForCanonical) {
    EXPECT_TRUE(validate_for_canonical(json{{"int", 42}}));
    EXPECT_FALSE(validate_for_canonical(json{{"float", 3.14}}));
    EXPECT_TRUE(validate_for_canonical(json{{"str", "hello"}}));
    EXPECT_TRUE(validate_for_canonical(json{{"arr", {1, 2, 3}}}));
}
