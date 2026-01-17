#pragma once

/**
 * @file validator.hpp
 * @brief Certificate validator for confirming SAFE/BUG results
 */

#include <nlohmann/json.hpp>
#include <string>

namespace sappp::validator {

class Validator {
public:
    explicit Validator(std::string input_dir, std::string schema_dir = "schemas");

    nlohmann::json validate(bool strict);
    void write_results(const nlohmann::json& results, const std::string& output_path) const;

private:
    std::string m_input_dir;
    std::string m_schema_dir;
};

} // namespace sappp::validator
