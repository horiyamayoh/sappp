/**
 * @file schema_validate.cpp
 * @brief JSON Schema validation using valijson
 */

#include "sappp/schema_validate.hpp"

#include <format>
#include <fstream>

#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>

namespace sappp::common {

namespace {

void normalize_ref(nlohmann::json& value)
{
    if (!value.is_string()) {
        return;
    }
    std::string ref = value.get<std::string>();
    constexpr std::string_view kPrefix = "#/$defs/";
    if (ref.starts_with(kPrefix)) {
        value = "#/definitions/" + ref.substr(kPrefix.size());
    }
}

void normalize_schema_defs(nlohmann::json& schema)
{
    if (schema.is_object()) {
        if (schema.contains("$defs") && !schema.contains("definitions")) {
            schema["definitions"] = schema["$defs"];
        }

        std::vector<std::string> keys;
        keys.reserve(schema.size());
        for (const auto& item : schema.items()) {
            keys.push_back(item.key());
        }
        for (const auto& key : keys) {
            nlohmann::json& value = schema.at(key);
            if (key == "$ref") {
                normalize_ref(value);
                continue;
            }
            normalize_schema_defs(value);
        }
        return;
    }
    if (schema.is_array()) {
        for (auto& value : schema) {
            normalize_schema_defs(value);
        }
    }
}

std::string format_validation_errors(valijson::ValidationResults& results)
{
    std::string result;
    valijson::ValidationResults::Error error;

    while (results.popError(error)) {
        std::string context;
        for (const auto& part : error.context) {
            context += "/" + part;
        }
        if (context.empty()) {
            context = "/";
        }

        if (!result.empty()) {
            result += '\n';
        }
        result += std::format("{}: {}", context, error.description);
    }

    return result;
}

}  // namespace

sappp::VoidResult validate_json(const nlohmann::json& j, const std::string& schema_path)
{
    std::ifstream schema_stream(schema_path);
    if (!schema_stream) {
        return std::unexpected(
            Error::make("SchemaFileOpenFailed", "Failed to open schema file: " + schema_path));
    }

    nlohmann::json schema_json;
    try {
        schema_stream >> schema_json;
    } catch (const std::exception& ex) {
        return std::unexpected(
            Error::make("SchemaParseFailed",
                        std::string("Failed to parse schema JSON: ") + ex.what()));
    }

    normalize_schema_defs(schema_json);

    valijson::Schema schema;
    valijson::SchemaParser parser;

    try {
        valijson::adapters::NlohmannJsonAdapter schema_adapter(schema_json);
        parser.populateSchema(schema_adapter, schema);
    } catch (const std::exception& ex) {
        return std::unexpected(
            Error::make("SchemaBuildFailed", std::string("Failed to build schema: ") + ex.what()));
    }

    valijson::Validator validator;
    valijson::ValidationResults results;
    valijson::adapters::NlohmannJsonAdapter target_adapter(j);

    if (!validator.validate(schema, target_adapter, &results)) {
        std::string error = format_validation_errors(results);
        if (error.empty()) {
            error = "Schema validation failed.";
        }
        return std::unexpected(Error::make("SchemaValidationFailed", std::move(error)));
    }

    return {};
}

}  // namespace sappp::common
