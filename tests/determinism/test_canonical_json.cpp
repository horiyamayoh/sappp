/**
 * @file test_canonical_json.cpp
 * @brief Canonical JSON determinism tests
 */

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace sappp::canonical;
using Json = nlohmann::json;

TEST(CanonicalJSON, KeyOrder)
{
    // Keys must be sorted lexicographically
    Json j = {
        {"z", 1},
        {"a", 2},
        {"m", 3}
    };
    auto canonical = canonicalize(j);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(*canonical, R"({"a":2,"m":3,"z":1})");
}

TEST(CanonicalJSON, NestedKeyOrder)
{
    Json j = {
        {  "outer", {{"z", 1}, {"a", 2}}},
        {"another",                    3}
    };
    auto canonical = canonicalize(j);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(*canonical, R"({"another":3,"outer":{"a":2,"z":1}})");
}

TEST(CanonicalJSON, NoWhitespace)
{
    Json j = {
        {"key",   "value"},
        {"arr", {1, 2, 3}}
    };
    auto canonical = canonicalize(j);
    ASSERT_TRUE(canonical);
    // No spaces, no newlines
    EXPECT_EQ(canonical->find(' '), std::string::npos);
    EXPECT_EQ(canonical->find('\n'), std::string::npos);
}

TEST(CanonicalJSON, FloatRejection)
{
    Json j = {
        {"value", 3.14}
    };
    auto canonical = canonicalize(j);
    EXPECT_FALSE(canonical);
}

TEST(CanonicalJSON, IntegerAllowed)
{
    Json j = {
        {"value", 42}
    };
    auto canonical = canonicalize(j);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(*canonical, R"({"value":42})");
}

TEST(CanonicalJSON, NegativeInteger)
{
    Json j = {
        {"value", -123}
    };
    auto canonical = canonicalize(j);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(*canonical, R"({"value":-123})");
}

TEST(CanonicalJSON, Determinism)
{
    Json j = {
        {    "id",           "test-123"},
        {"values",            {3, 1, 2}},
        {"nested", {{"b", 2}, {"a", 1}}}
    };

    auto c1 = canonicalize(j);
    auto c2 = canonicalize(j);
    auto c3 = canonicalize(j);
    ASSERT_TRUE(c1);
    ASSERT_TRUE(c2);
    ASSERT_TRUE(c3);

    EXPECT_EQ(*c1, *c2);
    EXPECT_EQ(*c2, *c3);
}

TEST(CanonicalJSON, HashDeterminism)
{
    Json j = {
        {"key", "value"}
    };

    auto h1 = hash_canonical(j);
    auto h2 = hash_canonical(j);
    ASSERT_TRUE(h1);
    ASSERT_TRUE(h2);

    EXPECT_EQ(*h1, *h2);
    EXPECT_TRUE(h1->starts_with("sha256:"));
}

TEST(CanonicalJSON, DifferentOrderSameHash)
{
    // Same content, different insertion order
    Json j1;
    j1["a"] = 1;
    j1["b"] = 2;

    Json j2;
    j2["b"] = 2;
    j2["a"] = 1;

    auto h1 = hash_canonical(j1);
    auto h2 = hash_canonical(j2);
    ASSERT_TRUE(h1);
    ASSERT_TRUE(h2);
    EXPECT_EQ(*h1, *h2);
}

TEST(CanonicalJSON, ValidateForCanonical)
{
    EXPECT_TRUE(validate_for_canonical(Json{
        {"int", 42}
    }));
    EXPECT_FALSE(validate_for_canonical(Json{
        {"float", 3.14}
    }));
    EXPECT_TRUE(validate_for_canonical(Json{
        {"str", "hello"}
    }));
    EXPECT_TRUE(validate_for_canonical(Json{
        {"arr", {1, 2, 3}}
    }));
}
