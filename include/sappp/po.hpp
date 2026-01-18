#pragma once

/**
 * @file po.hpp
 * @brief Proof Obligation (PO) generation from NIR.
 */

#include <nlohmann/json.hpp>
#include <string>

namespace sappp::po {

class PoGenerator {
public:
    explicit PoGenerator(std::string schema_dir = "schemas");

    nlohmann::json generate(const nlohmann::json& nir) const;

private:
    std::string m_schema_dir;
};

} // namespace sappp::po
