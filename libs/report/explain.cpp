#include "sappp/report/explain.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace sappp::report {

namespace {

[[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open JSON file: " + path.string()));
    }
    nlohmann::json payload;
    try {
        in >> payload;
    } catch (const std::exception& ex) {
        return std::unexpected(
            sappp::Error::make("ParseError",
                               "Failed to parse JSON file: " + path.string() + ": " + ex.what()));
    }
    return payload;
}

[[nodiscard]] sappp::Result<nlohmann::json> load_and_validate_unknown(const ExplainOptions& options)
{
    auto unknown_payload = read_json_file(options.unknown_path);
    if (!unknown_payload) {
        return std::unexpected(unknown_payload.error());
    }
    const std::filesystem::path schema_path =
        std::filesystem::path(options.schema_dir) / "unknown.v1.schema.json";
    if (auto validation = sappp::common::validate_json(*unknown_payload, schema_path.string());
        !validation) {
        return std::unexpected(sappp::Error::make("SchemaInvalid",
                                                  std::string("unknown schema validation failed: ")
                                                      + validation.error().message));
    }
    return *unknown_payload;
}

[[nodiscard]] sappp::Result<std::optional<nlohmann::json>>
load_validated_results(const ExplainOptions& options)
{
    if (!options.validated_path) {
        return std::optional<nlohmann::json>{};
    }
    auto validated_payload = read_json_file(*options.validated_path);
    if (!validated_payload) {
        return std::unexpected(validated_payload.error());
    }
    const std::filesystem::path schema_path =
        std::filesystem::path(options.schema_dir) / "validated_results.v1.schema.json";
    if (auto validation = sappp::common::validate_json(*validated_payload, schema_path.string());
        !validation) {
        return std::unexpected(
            sappp::Error::make("SchemaInvalid",
                               std::string("validated_results schema validation failed: ")
                                   + validation.error().message));
    }
    return std::optional<nlohmann::json>{*validated_payload};
}

[[nodiscard]] std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

[[nodiscard]] std::optional<nlohmann::json>
find_result_for_po(const nlohmann::json& validated_results, std::string_view po_id)
{
    if (!validated_results.contains("results")) {
        return std::nullopt;
    }
    for (const auto& result : validated_results.at("results")) {
        if (result.value("po_id", "") == po_id) {
            return result;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool matches_filters(const nlohmann::json& unknown,
                                   const ExplainOptions& options,
                                   const std::optional<nlohmann::json>& validated_results)
{
    const std::string po_id = unknown.value("po_id", "");
    if (options.po_id && *options.po_id != po_id) {
        return false;
    }
    if (options.unknown_id && *options.unknown_id != unknown.value("unknown_stable_id", "")) {
        return false;
    }
    if (validated_results) {
        auto result = find_result_for_po(*validated_results, po_id);
        if (!result || result->value("category", "") != "UNKNOWN") {
            return false;
        }
    }
    return true;
}

void append_missing_lemma(std::vector<std::string>& lines, const nlohmann::json& unknown)
{
    if (const auto missing_lemma = unknown.find("missing_lemma"); missing_lemma != unknown.end()) {
        lines.emplace_back(std::string("  missing_lemma: ") + missing_lemma->value("pretty", ""));
        if (missing_lemma->contains("notes")) {
            lines.emplace_back(std::string("  notes: ") + missing_lemma->value("notes", ""));
        }
        if (missing_lemma->contains("symbols")) {
            std::string symbols = "  symbols: ";
            const auto& list = missing_lemma->at("symbols");
            for (auto [i, item] : std::views::enumerate(list)) {
                if (i != 0) {
                    symbols += ", ";
                }
                symbols += item.get<std::string>();
            }
            lines.emplace_back(std::move(symbols));
        }
    }
}

void append_refinement_plan(std::vector<std::string>& lines, const nlohmann::json& unknown)
{
    if (const auto refinement_plan = unknown.find("refinement_plan");
        refinement_plan != unknown.end()) {
        lines.emplace_back(std::string("  refinement: ") + refinement_plan->value("message", ""));
        if (refinement_plan->contains("actions")) {
            for (const auto& action : refinement_plan->at("actions")) {
                lines.emplace_back(std::string("    - ") + action.value("action", ""));
            }
        }
    }
}

void append_depends_on(std::vector<std::string>& lines, const nlohmann::json& unknown)
{
    if (const auto depends_on = unknown.find("depends_on"); depends_on != unknown.end()) {
        if (depends_on->contains("contracts")) {
            std::string contracts = "  contracts: ";
            const auto& list = depends_on->at("contracts");
            for (auto [i, item] : std::views::enumerate(list)) {
                if (i != 0) {
                    contracts += ", ";
                }
                contracts += item.get<std::string>();
            }
            lines.emplace_back(std::move(contracts));
        }
        if (depends_on->contains("semantics_deviations")) {
            std::string deviations = "  semantics_deviations: ";
            const auto& list = depends_on->at("semantics_deviations");
            for (auto [i, item] : std::views::enumerate(list)) {
                if (i != 0) {
                    deviations += ", ";
                }
                deviations += item.get<std::string>();
            }
            lines.emplace_back(std::move(deviations));
        }
    }
}

void append_validator_status(std::vector<std::string>& lines,
                             const nlohmann::json& unknown,
                             const std::optional<nlohmann::json>& validated_results)
{
    if (validated_results) {
        if (auto result = find_result_for_po(*validated_results, unknown.value("po_id", ""))) {
            lines.emplace_back(std::string("  validator_status: ")
                               + result->value("validator_status", ""));
            if (result->contains("downgrade_reason_code")) {
                lines.emplace_back(std::string("  downgrade_reason: ")
                                   + result->value("downgrade_reason_code", ""));
            }
        }
    }
}

void append_text_block(std::vector<std::string>& lines,
                       const nlohmann::json& unknown,
                       const std::optional<nlohmann::json>& validated_results)
{
    lines.emplace_back(std::string("UNKNOWN: ") + unknown.value("unknown_stable_id", ""));
    lines.emplace_back(std::string("  po_id: ") + unknown.value("po_id", ""));
    lines.emplace_back(std::string("  code: ") + unknown.value("unknown_code", ""));

    append_missing_lemma(lines, unknown);
    append_refinement_plan(lines, unknown);
    append_depends_on(lines, unknown);
    append_validator_status(lines, unknown, validated_results);
}

[[nodiscard]] sappp::VoidResult write_json_output(const std::filesystem::path& path,
                                                  const nlohmann::json& payload)
{
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open output file: " + path.string()));
    }
    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical << "\n";
    if (!out) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to write output file: " + path.string()));
    }
    return {};
}

}  // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage): Public API defined in header.
[[nodiscard]] sappp::Result<ExplainOutput> explain_unknowns(const ExplainOptions& options)
{
    auto unknown_payload = load_and_validate_unknown(options);
    if (!unknown_payload) {
        return std::unexpected(unknown_payload.error());
    }

    auto validated_payload = load_validated_results(options);
    if (!validated_payload) {
        return std::unexpected(validated_payload.error());
    }

    if (!unknown_payload->contains("unknowns")) {
        return std::unexpected(
            sappp::Error::make("SchemaInvalid", "unknown ledger missing unknowns array"));
    }

    std::vector<nlohmann::json> filtered_unknowns;
    for (const auto& unknown : unknown_payload->at("unknowns")) {
        if (matches_filters(unknown, options, *validated_payload)) {
            filtered_unknowns.push_back(unknown);
        }
    }

    std::stable_sort(filtered_unknowns.begin(),
                     filtered_unknowns.end(),
                     [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
                         return lhs.value("unknown_stable_id", "")
                                < rhs.value("unknown_stable_id", "");
                     });

    ExplainOutput output;
    output.format = options.format;
    output.unknown_count = filtered_unknowns.size();
    output.summary = std::format("UNKNOWN entries: {}", filtered_unknowns.size());

    if (options.format == ExplainFormat::kJson) {
        nlohmann::json payload = {
            {"schema_version",                     "explain.v1"                              },
            {          "tool",
             {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
            {  "generated_at",                                             current_time_utc()},
            {      "unknowns",                                              filtered_unknowns}
        };
        if (validated_payload && *validated_payload) {
            payload["validated_results"] = {
                {"path", options.validated_path->string()}
            };
        }
        output.json = std::move(payload);
    } else {
        std::vector<std::string> lines;
        lines.emplace_back(output.summary);
        for (const auto& unknown : filtered_unknowns) {
            append_text_block(lines, unknown, *validated_payload);
        }
        output.text = std::move(lines);
    }

    return output;
}

[[nodiscard]] sappp::VoidResult write_explain_output(const ExplainOptions& options,
                                                     const ExplainOutput& output)
{
    if (options.format == ExplainFormat::kJson) {
        if (!options.output_path) {
            return std::unexpected(
                sappp::Error::make("MissingArgument", "--out is required for json output"));
        }
        return write_json_output(*options.output_path, output.json);
    }

    if (options.output_path) {
        std::ofstream out(*options.output_path);
        if (!out) {
            return std::unexpected(
                sappp::Error::make("IOError",
                                   "Failed to open output file: " + options.output_path->string()));
        }
        for (const auto& line : output.text) {
            out << line << "\n";
        }
        if (!out) {
            return std::unexpected(sappp::Error::make("IOError",
                                                      "Failed to write output file: "
                                                          + options.output_path->string()));
        }
        return {};
    }

    for (const auto& line : output.text) {
        std::println("{}", line);
    }
    return {};
}

}  // namespace sappp::report
