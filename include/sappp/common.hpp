#pragma once

/**
 * @file common.hpp
 * @brief Common utilities: hash, path normalization, stable sort
 */

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace sappp::common {

// ============================================================================
// SHA-256 Hash
// ============================================================================

/**
 * Compute SHA-256 hash of data
 * @param data Input bytes
 * @return Hex-encoded hash string (64 characters)
 */
std::string sha256(std::string_view data);

/**
 * Compute SHA-256 hash of data with prefix
 * @param data Input bytes
 * @return "sha256:" + hex-encoded hash
 */
std::string sha256_prefixed(std::string_view data);

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
std::string normalize_path(std::string_view input, std::string_view repo_root = "");

/**
 * Check if path is absolute
 */
bool is_absolute_path(std::string_view path);

/**
 * Make path relative to base
 */
std::string make_relative(std::string_view path, std::string_view base);

// ============================================================================
// Stable Sort Comparators
// ============================================================================

/**
 * Compare two strings for stable sorting (lexicographic)
 */
inline bool stable_string_less(const std::string& a, const std::string& b) {
    return a < b;
}

} // namespace sappp::common
