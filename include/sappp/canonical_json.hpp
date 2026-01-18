#pragma once

/**
 * @file canonical_json.hpp
 * @brief Canonical JSON serialization for deterministic hashing
 *
 * Rules (ADR-0101):
 * - UTF-8 encoding
 * - Object keys in lexicographic order
 * - No whitespace (minimal representation)
 * - Integers only (no floating point in hash scope)
 * - Arrays sorted where semantically appropriate
 */

#include "sappp/common.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::canonical {

/**
 * Hash scope for canonical serialization
 * - kCore: Include in hash computation (required for verification)
 * - kUi: Exclude from hash (pretty text, notes, etc.)
 *
 * Naming convention: kPascalCase for enum constants (Google C++ Style Guide)
 */
enum class HashScope {
    kCore,  ///< Included in hash
    kUi     ///< Excluded from hash
};

/**
 * Serialize JSON to canonical form
 * @param j JSON value
 * @return Canonical byte string or error
 */
[[nodiscard]] sappp::Result<std::string> canonicalize(const nlohmann::json& j);

/**
 * Compute SHA-256 hash of canonical JSON
 * @param j JSON value
 * @return "sha256:" + hex hash or error
 */
[[nodiscard]] sappp::Result<std::string> hash_canonical(const nlohmann::json& j);

/**
 * Sort JSON object keys recursively
 * @param j JSON value (modified in place)
 */
void sort_keys_recursive(nlohmann::json& j);

/**
 * Validate JSON for canonical form requirements
 * - No floating point numbers
 * - No duplicate keys
 * @param j JSON value
 * @return Empty on success, error on failure
 */
[[nodiscard]] sappp::VoidResult validate_for_canonical(const nlohmann::json& j);

}  // namespace sappp::canonical
