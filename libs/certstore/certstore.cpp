/**
 * @file certstore.cpp
 * @brief Content-addressed certificate store implementation
 *
 * C++23 modernization:
 * - Using std::expected for error handling
 * - Using [[nodiscard]] consistently
 */

#include "sappp/certstore.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace sappp::certstore {

namespace {

namespace fs = std::filesystem;

void ensure_parent_dir(const fs::path& path)
{
    fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

}  // namespace

CertStore::CertStore(std::string base_dir, std::string schema_dir)
    : m_base_dir(std::move(base_dir))
    , m_schema_dir(std::move(schema_dir))
{}

sappp::Result<std::string> CertStore::put(const nlohmann::json& cert)
{
    if (auto result = sappp::common::validate_json(cert, cert_schema_path()); !result) {
        return std::unexpected(
            Error::make(result.error().code,
                        "Certificate schema validation failed: " + result.error().message));
    }

    auto hash = canonical_hash(cert);
    if (!hash) {
        return std::unexpected(hash.error());
    }
    auto object_path = object_path_for_hash(*hash);
    if (!object_path) {
        return std::unexpected(object_path.error());
    }
    if (auto result = write_json_file(*object_path, cert); !result) {
        return std::unexpected(result.error());
    }
    return *hash;
}

sappp::Result<nlohmann::json> CertStore::get(const std::string& hash) const
{
    auto object_path = object_path_for_hash(hash);
    if (!object_path) {
        return std::unexpected(object_path.error());
    }
    if (!fs::exists(*object_path)) {
        return std::unexpected(Error::make("NotFound", "Certificate not found: " + hash));
    }

    auto cert_result = read_json_file(*object_path);
    if (!cert_result) {
        return std::unexpected(cert_result.error());
    }

    const nlohmann::json& cert = *cert_result;

    if (auto result = sappp::common::validate_json(cert, cert_schema_path()); !result) {
        return std::unexpected(
            Error::make(result.error().code,
                        "Stored certificate schema validation failed: " + result.error().message));
    }

    auto computed_hash = canonical_hash(cert);
    if (!computed_hash) {
        return std::unexpected(computed_hash.error());
    }
    if (*computed_hash != hash) {
        return std::unexpected(
            Error::make("HashMismatch",
                        "Certificate hash mismatch: expected " + hash + ", got " + *computed_hash));
    }

    return cert;
}

sappp::VoidResult CertStore::bind_po(const std::string& po_id, const std::string& cert_hash)
{
    auto object_path = object_path_for_hash(cert_hash);
    if (!object_path) {
        return std::unexpected(object_path.error());
    }
    if (!fs::exists(*object_path)) {
        return std::unexpected(Error::make("NotFound", "Certificate hash not found: " + cert_hash));
    }

    nlohmann::json index = {
        {"schema_version", "cert_index.v1"},
        {         "po_id",           po_id},
        {          "root",       cert_hash}
    };

    if (auto result = sappp::common::validate_json(index, index_schema_path()); !result) {
        return std::unexpected(
            Error::make(result.error().code,
                        "Certificate index schema validation failed: " + result.error().message));
    }

    return write_json_file(index_path_for_po(po_id), index);
}

std::string CertStore::cert_schema_path() const
{
    return (fs::path(m_schema_dir) / "cert.v1.schema.json").string();
}

std::string CertStore::index_schema_path() const
{
    return (fs::path(m_schema_dir) / "cert_index.v1.schema.json").string();
}

sappp::Result<std::string> CertStore::canonical_hash(const nlohmann::json& cert)
{
    return sappp::canonical::hash_canonical(cert);
}

sappp::Result<std::string> CertStore::object_path_for_hash(const std::string& hash) const
{
    // Allow hashes with or without a "sha256:" prefix, but always shard based on
    // the first two hex characters of the digest portion.
    constexpr std::string_view kPrefix = "sha256:";
    std::size_t digest_start = hash.starts_with(kPrefix) ? kPrefix.size() : 0UZ;

    if (hash.size() < digest_start + 2UZ) {
        return std::unexpected(Error::make("InvalidHash", "Hash is too short: " + hash));
    }

    std::string shard = hash.substr(digest_start, 2);

    fs::path base(m_base_dir);
    fs::path object_dir = base / "objects" / shard;
    fs::path object_path = object_dir / (hash + ".json");
    return object_path.string();
}

std::string CertStore::index_path_for_po(const std::string& po_id) const
{
    fs::path base(m_base_dir);
    fs::path index_path = base / "index" / (po_id + ".json");
    return index_path.string();
}

sappp::VoidResult CertStore::write_json_file(const std::string& path, const nlohmann::json& payload)
{
    fs::path out_path(path);
    ensure_parent_dir(out_path);

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return std::unexpected(
            Error::make("IOError", "Failed to open file for write: " + out_path.string()));
    }

    auto canonical = sappp::canonical::canonicalize(payload);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    out << *canonical;
    if (!out) {
        return std::unexpected(
            Error::make("IOError", "Failed to write file: " + out_path.string()));
    }

    return {};
}

sappp::Result<nlohmann::json> CertStore::read_json_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::unexpected(Error::make("IOError", "Failed to open file for read: " + path));
    }

    std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

    try {
        return nlohmann::json::parse(content);
    } catch (const std::exception& ex) {
        return std::unexpected(
            Error::make("ParseError", "Failed to parse JSON from " + path + ": " + ex.what()));
    }
}

}  // namespace sappp::certstore
