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

#include <nlohmann/json.hpp>
#include <string>

namespace sappp::canonical {

/**
 * Hash scope for canonical serialization
 * - core: Include in hash computation (required for verification)
 * - ui: Exclude from hash (pretty text, notes, etc.)
 */
enum class HashScope {
    Core,  // Included in hash
    UI     // Excluded from hash
};

/**
 * Serialize JSON to canonical form
 * @param j JSON value
 * @return Canonical byte string
 */
std::string canonicalize(const nlohmann::json& j);

/**
 * Compute SHA-256 hash of canonical JSON
 * @param j JSON value
 * @return "sha256:" + hex hash
 */
std::string hash_canonical(const nlohmann::json& j);

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
 * @return true if valid for canonicalization
 */
bool validate_for_canonical(const nlohmann::json& j);

} // namespace sappp::canonical
