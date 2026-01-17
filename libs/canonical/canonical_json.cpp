/**
 * @file canonical_json.cpp
 * @brief Canonical JSON serialization (ADR-0101)
 *
 * C++23 modernization:
 * - Using std::ranges algorithms and views
 * - Using std::views::enumerate for indexed iteration
 * - Using size_t literal suffix (uz)
 * - Using [[nodiscard]] for pure functions
 */

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>

namespace sappp::canonical {

namespace {

void validate_no_float(const nlohmann::json& j, const std::string& path) {
    if (j.is_number_float()) {
        throw std::runtime_error("Floating point numbers not allowed in canonical JSON at: " + path);
    }
    if (j.is_object()) {
        for (const auto& [key, val] : j.items()) {
            validate_no_float(val, path + "." + key);
        }
    } else if (j.is_array()) {
        // Use views::enumerate for cleaner indexed iteration
        for (auto [i, elem] : std::views::enumerate(j)) {
            validate_no_float(elem, std::format("{}[{}]", path, i));
        }
    }
}

/**
 * @brief Recursively create a sorted copy of JSON (keys in lexicographic order)
 */
[[nodiscard]] nlohmann::json make_sorted_copy(const nlohmann::json& j) {
    if (j.is_object()) {
        // Get keys and sort them using ranges
        std::vector<std::string> keys;
        keys.reserve(j.size());
        for (const auto& [key, _] : j.items()) {
            keys.push_back(key);
        }
        std::ranges::sort(keys);
        
        // Create ordered object
        nlohmann::json result = nlohmann::json::object();
        for (const auto& key : keys) {
            result[key] = make_sorted_copy(j[key]);
        }
        return result;
    } else if (j.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        result.get_ref<nlohmann::json::array_t&>().reserve(j.size());
        for (const auto& elem : j) {
            result.push_back(make_sorted_copy(elem));
        }
        return result;
    }
    return j;
}

} // namespace

std::string canonicalize(const nlohmann::json& j) {
    // Validate: no floating point
    validate_no_float(j, "$");
    
    // Sort keys recursively
    nlohmann::json sorted = make_sorted_copy(j);
    
    // Serialize without whitespace
    return sorted.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
}

std::string hash_canonical(const nlohmann::json& j) {
    std::string canonical = canonicalize(j);
    return common::sha256_prefixed(canonical);
}

void sort_keys_recursive(nlohmann::json& j) {
    if (j.is_object()) {
        // Get keys and sort
        std::vector<std::string> keys;
        for (auto& [key, _] : j.items()) {
            keys.push_back(key);
        }
        std::ranges::sort(keys);
        
        // Rebuild object in sorted order
        nlohmann::json sorted = nlohmann::json::object();
        for (const auto& key : keys) {
            sort_keys_recursive(j[key]);
            sorted[key] = std::move(j[key]);
        }
        j = std::move(sorted);
    } else if (j.is_array()) {
        for (auto& elem : j) {
            sort_keys_recursive(elem);
        }
    }
}

bool validate_for_canonical(const nlohmann::json& j) {
    try {
        validate_no_float(j, "$");
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sappp::canonical
