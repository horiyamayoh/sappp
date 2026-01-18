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

namespace sappp::canonical {

namespace {

sappp::VoidResult validate_no_float(const nlohmann::json& j, std::string_view path)
{
    if (j.is_number_float()) {
        return std::unexpected(Error::make(
            "FloatingPointNotAllowed",
            std::format("Floating point numbers not allowed in canonical JSON at: {}", path)));
    }
    if (j.is_object()) {
        for (const auto& [key, val] : j.items()) {
            std::string next_path = std::string(path) + "." + key;
            if (auto result = validate_no_float(val, next_path); !result) {
                return result;
            }
        }
    } else if (j.is_array()) {
        // Use views::enumerate for cleaner indexed iteration
        for (auto [i, elem] : std::views::enumerate(j)) {
            if (auto result = validate_no_float(elem, std::format("{}[{}]", path, i)); !result) {
                return result;
            }
        }
    }
    return {};
}

/**
 * @brief Recursively create a sorted copy of JSON (keys in lexicographic order)
 */
[[nodiscard]] nlohmann::json make_sorted_copy(const nlohmann::json& j)
{
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

}  // namespace

sappp::Result<std::string> canonicalize(const nlohmann::json& j)
{
    // Validate: no floating point
    if (auto result = validate_no_float(j, "$"); !result) {
        return std::unexpected(result.error());
    }

    // Sort keys recursively
    nlohmann::json sorted = make_sorted_copy(j);

    // Serialize without whitespace
    return sorted.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
}

sappp::Result<std::string> hash_canonical(const nlohmann::json& j)
{
    auto canonical = canonicalize(j);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    return common::sha256_prefixed(*canonical);
}

void sort_keys_recursive(nlohmann::json& j)
{
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

sappp::VoidResult validate_for_canonical(const nlohmann::json& j)
{
    return validate_no_float(j, "$");
}

}  // namespace sappp::canonical
