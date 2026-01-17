/**
 * @file certstore.cpp
 * @brief Content-addressed certificate store implementation
 */

#include "sappp/certstore.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sappp::certstore {

namespace {

namespace fs = std::filesystem;

void ensure_parent_dir(const fs::path& path) {
    fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

} // namespace

CertStore::CertStore(std::string base_dir, std::string schema_dir)
    : m_base_dir(std::move(base_dir)),
      m_schema_dir(std::move(schema_dir)) {}

std::string CertStore::put(const nlohmann::json& cert) {
    std::string error;
    if (!sappp::common::validate_json(cert, cert_schema_path(), error)) {
        throw std::runtime_error("Certificate schema validation failed: " + error);
    }

    std::string hash = canonical_hash(cert);
    write_json_file(object_path_for_hash(hash), cert);
    return hash;
}

std::optional<nlohmann::json> CertStore::get(const std::string& hash) const {
    std::string path = object_path_for_hash(hash);
    if (!fs::exists(path)) {
        return std::nullopt;
    }

    nlohmann::json cert = read_json_file(path);

    std::string error;
    if (!sappp::common::validate_json(cert, cert_schema_path(), error)) {
        throw std::runtime_error("Stored certificate schema validation failed: " + error);
    }

    std::string computed_hash = canonical_hash(cert);
    if (computed_hash != hash) {
        throw std::runtime_error("Certificate hash mismatch: expected " + hash + ", got " + computed_hash);
    }

    return cert;
}

void CertStore::bind_po(const std::string& po_id, const std::string& cert_hash) {
    std::string object_path = object_path_for_hash(cert_hash);
    if (!fs::exists(object_path)) {
        throw std::runtime_error("Certificate hash not found: " + cert_hash);
    }

    nlohmann::json index = {
        {"schema_version", "cert_index.v1"},
        {"po_id", po_id},
        {"root", cert_hash}
    };

    std::string error;
    if (!sappp::common::validate_json(index, index_schema_path(), error)) {
        throw std::runtime_error("Certificate index schema validation failed: " + error);
    }

    write_json_file(index_path_for_po(po_id), index);
}

std::string CertStore::cert_schema_path() const {
    return (fs::path(m_schema_dir) / "cert.v1.schema.json").string();
}

std::string CertStore::index_schema_path() const {
    return (fs::path(m_schema_dir) / "cert_index.v1.schema.json").string();
}

std::string CertStore::canonical_hash(const nlohmann::json& cert) const {
    return sappp::canonical::hash_canonical(cert);
}

std::string CertStore::object_path_for_hash(const std::string& hash) const {
    // Allow hashes with or without a "sha256:" prefix, but always shard based on
    // the first two hex characters of the digest portion.
    constexpr std::string_view prefix = "sha256:";
    std::size_t digest_start = hash.starts_with(prefix) ? prefix.size() : 0;

    if (hash.size() < digest_start + 2) {
        throw std::invalid_argument("Hash is too short: " + hash);
    }

    std::string shard = hash.substr(digest_start, 2);

    fs::path base(m_base_dir);
    fs::path object_dir = base / "objects" / shard;
    fs::path object_path = object_dir / (hash + ".json");
    return object_path.string();
}

std::string CertStore::index_path_for_po(const std::string& po_id) const {
    fs::path base(m_base_dir);
    fs::path index_path = base / "index" / (po_id + ".json");
    return index_path.string();
}

void CertStore::write_json_file(const std::string& path, const nlohmann::json& payload) const {
    fs::path out_path(path);
    ensure_parent_dir(out_path);

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open file for write: " + out_path.string());
    }

    out << sappp::canonical::canonicalize(payload);
    if (!out) {
        throw std::runtime_error("Failed to write file: " + out_path.string());
    }
}

nlohmann::json CertStore::read_json_file(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open file for read: " + path);
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    try {
        return nlohmann::json::parse(buffer.str());
    } catch (const std::exception& ex) {
        throw std::runtime_error("Failed to parse JSON from " + path + ": " + ex.what());
    }
}

} // namespace sappp::certstore
