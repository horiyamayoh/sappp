#pragma once

#include "sappp/common.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sappp::report {

enum class ExplainFormat { kText, kJson };

struct ExplainOptions
{
    std::filesystem::path unknown_path;
    std::optional<std::filesystem::path> validated_path;
    std::optional<std::string> po_id;
    std::optional<std::string> unknown_id;
    std::optional<std::filesystem::path> output_path;
    std::string schema_dir;
    ExplainFormat format;
};

struct ExplainOutput
{
    ExplainFormat format;
    std::size_t unknown_count;
    std::string summary;
    nlohmann::json json;
    std::vector<std::string> text;
};

[[nodiscard]] sappp::Result<ExplainOutput> explain_unknowns(const ExplainOptions& options);

[[nodiscard]] sappp::VoidResult write_explain_output(const ExplainOptions& options,
                                                     const ExplainOutput& output);

}  // namespace sappp::report
