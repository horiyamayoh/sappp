#pragma once

/**
 * @file validator.hpp
 * @brief Certificate validator for confirming SAFE/BUG results
 */

#include "sappp/common.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::validator {

class Validator
{
public:
    explicit Validator(std::string input_dir, std::string schema_dir = "schemas");

    [[nodiscard]] sappp::Result<nlohmann::json> validate(bool strict);
    [[nodiscard]] sappp::VoidResult write_results(const nlohmann::json& results,
                                                  const std::string& output_path) const;

private:
    std::string m_input_dir;
    std::string m_schema_dir;
};

}  // namespace sappp::validator
