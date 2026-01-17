#pragma once

/**
 * @file certstore.hpp
 * @brief Content-addressed certificate store (CAS + index)
 */

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace sappp::certstore {

class CertStore {
public:
    explicit CertStore(std::string base_dir, std::string schema_dir = "schemas");

    std::string put(const nlohmann::json& cert);
    std::optional<nlohmann::json> get(const std::string& hash) const;
    void bind_po(const std::string& po_id, const std::string& cert_hash);

private:
    std::string m_base_dir;
    std::string m_schema_dir;

    std::string cert_schema_path() const;
    std::string index_schema_path() const;

    std::string canonical_hash(const nlohmann::json& cert) const;

    std::string object_path_for_hash(const std::string& hash) const;
    std::string index_path_for_po(const std::string& po_id) const;

    void write_json_file(const std::string& path, const nlohmann::json& payload) const;
    nlohmann::json read_json_file(const std::string& path) const;
};

} // namespace sappp::certstore
