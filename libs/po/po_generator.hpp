#pragma once

/**
 * @file po_generator.hpp
 * @brief Proof Obligation (PO) generator from NIR
 */

#include <nlohmann/json.hpp>

namespace sappp::po {

class PoGenerator {
public:
    PoGenerator() = default;

    nlohmann::json generate(const nlohmann::json& nir_json) const;
};

} // namespace sappp::po
