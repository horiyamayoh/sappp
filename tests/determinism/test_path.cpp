/**
 * @file test_path.cpp
 * @brief Path normalization determinism tests
 */

#include "sappp/common.hpp"

#include <gtest/gtest.h>

using namespace sappp::common;

TEST(PathNormalization, UnixPaths)
{
    EXPECT_EQ(normalize_path("/home/user/project"), "/home/user/project");
    EXPECT_EQ(normalize_path("/home/user/project/"), "/home/user/project");
    EXPECT_EQ(normalize_path("/home/user/../user/project"), "/home/user/project");
    EXPECT_EQ(normalize_path("/home/user/./project"), "/home/user/project");
}

TEST(PathNormalization, WindowsToUnix)
{
    // Backslashes converted to forward slashes
    EXPECT_EQ(normalize_path("C:\\Users\\dev\\project"), "c:/Users/dev/project");
    EXPECT_EQ(normalize_path("src\\main.cpp"), "src/main.cpp");
}

TEST(PathNormalization, DotDot)
{
    EXPECT_EQ(normalize_path("a/b/../c"), "a/c");
    EXPECT_EQ(normalize_path("a/b/c/../../d"), "a/d");
    EXPECT_EQ(normalize_path("../a/b"), "../a/b");
}

TEST(PathNormalization, Dot)
{
    EXPECT_EQ(normalize_path("./a/b"), "a/b");
    EXPECT_EQ(normalize_path("a/./b"), "a/b");
    EXPECT_EQ(normalize_path("a/b/."), "a/b");
}

TEST(PathNormalization, Empty)
{
    EXPECT_EQ(normalize_path(""), ".");
}

TEST(PathNormalization, RelativeToRoot)
{
    EXPECT_EQ(normalize_path("/home/user/project/src/main.cpp", "/home/user/project"),
              "src/main.cpp");
    EXPECT_EQ(normalize_path("/home/user/project", "/home/user/project"), ".");
}

TEST(PathNormalization, IsAbsolute)
{
    EXPECT_TRUE(is_absolute_path("/home/user"));
    EXPECT_TRUE(is_absolute_path("C:/Users"));
    EXPECT_TRUE(is_absolute_path("C:\\Users"));
    EXPECT_FALSE(is_absolute_path("relative/path"));
    EXPECT_FALSE(is_absolute_path("./relative"));
    EXPECT_FALSE(is_absolute_path(""));
}

TEST(PathNormalization, MakeRelative)
{
    EXPECT_EQ(make_relative("/a/b/c", "/a/b"), "c");
    EXPECT_EQ(make_relative("/a/b/c", "/a/d"), "../b/c");
    EXPECT_EQ(make_relative("/a/b", "/a/b"), ".");
}
