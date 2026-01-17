#pragma once

/**
 * @file schema_validate.hpp
 * @brief JSON Schema validation utilities
 */

#include <nlohmann/json.hpp>
#include <string>

namespace sappp::common {

/**
 * Validate JSON against a JSON Schema file.
 *
 * @param j JSON document to validate
 * @param schema_path Path to JSON Schema file
 * @param error_out Error message output on failure
 * @return true if valid, false otherwise
 */
bool validate_json(const nlohmann::json& j, const std::string& schema_path, std::string& error_out);

} // namespace sappp::common
