#pragma once

/**
 * @file certstore.hpp
 * @brief Content-addressed cert storage and PO index
 */

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sappp {

class CertStore {
public:
    explicit CertStore(std::filesystem::path base_path);

    std::string put(const nlohmann::json& cert);
    std::optional<nlohmann::json> get(const std::string& hash) const;
    void bind_po(const std::string& po_id, const std::string& cert_hash) const;

private:
    std::filesystem::path base_path_;

    std::filesystem::path object_path_for_hash(const std::string& hash) const;
    std::filesystem::path index_path_for_po(const std::string& po_id) const;
};

} // namespace sappp
