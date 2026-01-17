/**
 * @file schema_validate.cpp
 * @brief JSON Schema validation using valijson
 */

#include "sappp/schema_validate.hpp"

#include <fstream>
#include <sstream>

#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>

namespace sappp::common {

namespace {

std::string format_validation_errors(valijson::ValidationResults& results) {
    std::ostringstream oss;
    valijson::ValidationResults::Error error;
    bool first = true;

    while (results.popError(error)) {
        std::string context;
        for (const auto& part : error.context) {
            context += "/" + part;
        }
        if (context.empty()) {
            context = "/";
        }

        if (!first) {
            oss << "\n";
        }
        first = false;
        oss << context << ": " << error.description;
    }

    return oss.str();
}

} // namespace

bool validate_json(const nlohmann::json& j, const std::string& schema_path, std::string& error_out) {
    error_out.clear();

    std::ifstream schema_stream(schema_path);
    if (!schema_stream) {
        error_out = "Failed to open schema file: " + schema_path;
        return false;
    }

    nlohmann::json schema_json;
    try {
        schema_stream >> schema_json;
    } catch (const std::exception& ex) {
        error_out = std::string("Failed to parse schema JSON: ") + ex.what();
        return false;
    }

    valijson::Schema schema;
    valijson::SchemaParser parser;

    try {
        valijson::adapters::NlohmannJsonAdapter schema_adapter(schema_json);
        parser.populateSchema(schema_adapter, schema);
    } catch (const std::exception& ex) {
        error_out = std::string("Failed to build schema: ") + ex.what();
        return false;
    }

    valijson::Validator validator;
    valijson::ValidationResults results;
    valijson::adapters::NlohmannJsonAdapter target_adapter(j);

    if (!validator.validate(schema, target_adapter, &results)) {
        error_out = format_validation_errors(results);
        if (error_out.empty()) {
            error_out = "Schema validation failed.";
        }
        return false;
    }

    return true;
}

} // namespace sappp::common
