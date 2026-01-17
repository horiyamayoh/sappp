#pragma once

/**
 * @file certstore.hpp
 * @brief Content-addressed certificate store (CAS + index)
 *
 * C++23 modernization:
 * - Using std::expected for error handling
 */

#include "sappp/common.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace sappp::certstore {

class CertStore {
public:
    explicit CertStore(std::string base_dir, std::string schema_dir = "schemas");

    /**
     * @brief Store a certificate and return its hash
     * @return Hash of the stored certificate or error
     */
    [[nodiscard]] sappp::Result<std::string> put(const nlohmann::json& cert);

    /**
     * @brief Retrieve a certificate by hash
     * @return Certificate JSON or error if not found/invalid
     */
    [[nodiscard]] sappp::Result<nlohmann::json> get(const std::string& hash) const;

    /**
     * @brief Bind a PO ID to a certificate hash
     * @return Success or error
     */
    [[nodiscard]] sappp::VoidResult bind_po(const std::string& po_id, const std::string& cert_hash);

private:
    std::string m_base_dir;
    std::string m_schema_dir;

    [[nodiscard]] std::string cert_schema_path() const;
    [[nodiscard]] std::string index_schema_path() const;

    [[nodiscard]] std::string canonical_hash(const nlohmann::json& cert) const;

    [[nodiscard]] std::string object_path_for_hash(const std::string& hash) const;
    [[nodiscard]] std::string index_path_for_po(const std::string& po_id) const;

    [[nodiscard]] sappp::VoidResult write_json_file(const std::string& path, const nlohmann::json& payload) const;
    [[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::string& path) const;
};

} // namespace sappp::certstore
