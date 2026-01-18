#pragma once

/**
 * @file frontend.hpp
 * @brief Clang frontend integration for NIR and source map generation
 */

#include "sappp/common.hpp"

#include <string>

#include <nlohmann/json.hpp>

namespace sappp::frontend_clang {

struct FrontendResult
{
    nlohmann::json nir;
    nlohmann::json source_map;
};

class FrontendClang
{
public:
    explicit FrontendClang(std::string schema_dir = "schemas");

    [[nodiscard]] sappp::Result<FrontendResult> analyze(const nlohmann::json& build_snapshot) const;

private:
    std::string m_schema_dir;
};

}  // namespace sappp::frontend_clang
