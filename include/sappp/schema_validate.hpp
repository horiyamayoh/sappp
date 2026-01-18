#pragma once

/**
 * @file schema_validate.hpp
 * @brief JSON Schema validation utilities
 */

#include "sappp/common.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::common {

/**
 * Validate JSON against a JSON Schema file.
 *
 * @param j JSON document to validate
 * @param schema_path Path to JSON Schema file
 * @return Empty on success, error on failure
 */
[[nodiscard]] sappp::VoidResult validate_json(const nlohmann::json& j,
                                              const std::string& schema_path);

}  // namespace sappp::common
