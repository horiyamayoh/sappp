#pragma once

/**
 * @file po_generator.hpp
 * @brief Proof Obligation (PO) generator from NIR
 */

#include "sappp/common.hpp"

#include <nlohmann/json.hpp>

namespace sappp::po {

class PoGenerator {
public:
    PoGenerator() = default;

    [[nodiscard]] sappp::Result<nlohmann::json> generate(const nlohmann::json& nir_json) const;
};

} // namespace sappp::po
