#pragma once

/**
 * @file common.hpp
 * @brief Common utilities: hash, path normalization, stable sort
 */

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sappp {

/**
 * @brief Error information for Result types
 */
struct Error
{
    std::string code;     ///< Machine-readable error code
    std::string message;  ///< Human-readable error message

    [[nodiscard]] static Error make(std::string code, std::string message)
    {
        return Error{.code = std::move(code), .message = std::move(message)};
    }
};

/**
 * @brief Result type using std::expected (C++23)
 * @tparam T Success value type
 */
template <typename T>
using Result = std::expected<T, Error>;

/**
 * @brief Result type for void success using std::expected (C++23)
 */
using VoidResult = std::expected<void, Error>;

}  // namespace sappp

namespace sappp::common {

// ============================================================================
// SHA-256 Hash
// ============================================================================

/**
 * Compute SHA-256 hash of data
 * @param data Input bytes
 * @return Hex-encoded hash string (64 characters)
 */
[[nodiscard]] std::string sha256(std::string_view data);

/**
 * Compute SHA-256 hash of data with prefix
 * @param data Input bytes
 * @return "sha256:" + hex-encoded hash
 */
[[nodiscard]] std::string sha256_prefixed(std::string_view data);

// ============================================================================
// Path Normalization
// ============================================================================

/**
 * Normalize a path for deterministic output
 * - Use '/' as separator
 * - Remove trailing slashes
 * - Resolve '..' and '.'
 * - Optionally make relative to repo_root
 *
 * @param input Input path
 * @param repo_root Optional repository root for relative paths
 * @return Normalized path
 */
[[nodiscard]] std::string normalize_path(std::string_view input, std::string_view repo_root = "");

/**
 * Check if path is absolute
 */
[[nodiscard]] bool is_absolute_path(std::string_view path);

/**
 * Make path relative to base
 */
[[nodiscard]] std::string make_relative(std::string_view path, std::string_view base);

// ============================================================================
// Stable Sort Comparators
// ============================================================================

/**
 * Compare two strings for stable sorting (lexicographic)
 */
[[nodiscard]] inline bool stable_string_less(const std::string& a, const std::string& b)
{
    return a < b;
}

}  // namespace sappp::common
