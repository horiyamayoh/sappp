/**
 * @file certstore.cpp
 * @brief Content-addressed cert storage and PO index implementation
 */

#include "sappp/certstore.hpp"

#include "sappp/canonical_json.hpp"

#include <fstream>
#include <stdexcept>

namespace sappp {

namespace {

std::string hash_prefix_dir(const std::string& hash) {
    constexpr std::string_view prefix = "sha256:";
    if (hash.rfind(prefix, 0) == 0 && hash.size() >= prefix.size() + 2) {
        return hash.substr(prefix.size(), 2);
    }
    if (hash.size() >= 2) {
        return hash.substr(0, 2);
    }
    return hash;
}

nlohmann::json load_json_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    return nlohmann::json::parse(input);
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }
    output << content;
    if (!output.good()) {
        throw std::runtime_error("Failed to write file: " + path.string());
    }
}

} // namespace

CertStore::CertStore(std::filesystem::path base_path)
    : base_path_(std::move(base_path)) {}

std::string CertStore::put(const nlohmann::json& cert) {
    const std::string hash = canonical::hash_canonical(cert);
    const std::filesystem::path object_path = object_path_for_hash(hash);
    std::filesystem::create_directories(object_path.parent_path());
    const std::string canonical = canonical::canonicalize(cert);
    write_text_file(object_path, canonical);
    return hash;
}

std::optional<nlohmann::json> CertStore::get(const std::string& hash) const {
    const std::filesystem::path object_path = object_path_for_hash(hash);
    if (!std::filesystem::exists(object_path)) {
        return std::nullopt;
    }
    return load_json_file(object_path);
}

void CertStore::bind_po(const std::string& po_id, const std::string& cert_hash) const {
    const std::filesystem::path index_path = index_path_for_po(po_id);
    std::filesystem::create_directories(index_path.parent_path());

    nlohmann::json index = {
        {"schema_version", "cert_index.v1"},
        {"po_id", po_id},
        {"root", cert_hash}
    };

    const std::string canonical = canonical::canonicalize(index);
    write_text_file(index_path, canonical);
}

std::filesystem::path CertStore::object_path_for_hash(const std::string& hash) const {
    const std::string prefix = hash_prefix_dir(hash);
    return base_path_ / "objects" / prefix / (hash + ".json");
}

std::filesystem::path CertStore::index_path_for_po(const std::string& po_id) const {
    return base_path_ / "index" / (po_id + ".json");
}

} // namespace sappp
