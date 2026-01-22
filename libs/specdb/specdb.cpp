/**
 * @file specdb.cpp
 * @brief SpecDB normalization and snapshot builder
 */

#include "sappp/specdb.hpp"

#include "sappp/canonical_json.hpp"
#include "sappp/schema_validate.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sappp::specdb {

namespace {

constexpr std::string_view kAnnotationPrefix = "//@sappp";
constexpr std::string_view kAnnotationContract = "contract";

struct AnnotationPayload
{
    std::string_view text;
    std::string_view source;
};

struct AnnotationFileSpec
{
    std::filesystem::path path;
    std::filesystem::path schema_dir;
};

[[nodiscard]] std::string trim_left(std::string_view input)
{
    std::size_t pos = 0;
    while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])) != 0) {
        ++pos;
    }
    return std::string(input.substr(pos));
}

[[nodiscard]] std::string trim(std::string_view input)
{
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }
    return std::string(input.substr(start, end - start));
}

[[nodiscard]] bool has_source_extension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) noexcept {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++";
}

[[nodiscard]] sappp::Result<nlohmann::json> read_json_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open JSON file: " + path.string()));
    }
    nlohmann::json payload;
    try {
        in >> payload;
    } catch (const std::exception& ex) {
        return std::unexpected(
            sappp::Error::make("ParseError",
                               "Failed to parse JSON file: " + path.string() + ": " + ex.what()));
    }
    return payload;
}

[[nodiscard]] sappp::Result<nlohmann::json> parse_inline_contract(AnnotationPayload payload_info)
{
    nlohmann::json parsed_payload;
    try {
        parsed_payload = nlohmann::json::parse(payload_info.text);
    } catch (const std::exception& ex) {
        return std::unexpected(sappp::Error::make("ParseError",
                                                  "Failed to parse contract annotation in "
                                                      + std::string(payload_info.source) + ": "
                                                      + ex.what()));
    }
    if (!parsed_payload.is_object()) {
        return std::unexpected(sappp::Error::make("InvalidContract",
                                                  "Contract annotation must be a JSON object in "
                                                      + std::string(payload_info.source)));
    }
    return parsed_payload;
}

[[nodiscard]] sappp::Result<nlohmann::json> make_contract_id(const nlohmann::json& contract)
{
    nlohmann::json hash_input = {
        {"schema_version",             "contract_ir.v1"},
        {        "target",        contract.at("target")},
        {          "tier",          contract.at("tier")},
        { "version_scope", contract.at("version_scope")},
        {      "contract",      contract.at("contract")}
    };
    auto hash = sappp::canonical::hash_canonical(hash_input);
    if (!hash) {
        return std::unexpected(hash.error());
    }
    return nlohmann::json(*hash);
}

[[nodiscard]] sappp::Result<nlohmann::json> normalize_contract_scope(const nlohmann::json& input,
                                                                     std::string_view source)
{
    if (!input.is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidContract",
                               "version_scope must be an object in " + std::string(source)));
    }
    nlohmann::json scope = input;
    if (!scope.contains("priority")) {
        scope["priority"] = 0;
    }
    if (!scope.at("priority").is_number_integer()) {
        return std::unexpected(sappp::Error::make("InvalidContract",
                                                  "version_scope.priority must be an integer in "
                                                      + std::string(source)));
    }
    if (!scope.contains("conditions")) {
        scope["conditions"] = nlohmann::json::array();
    }
    if (!scope.at("conditions").is_array()) {
        return std::unexpected(sappp::Error::make("InvalidContract",
                                                  "version_scope.conditions must be an array in "
                                                      + std::string(source)));
    }
    std::vector<std::string> conditions;
    for (const auto& item : scope.at("conditions")) {
        if (!item.is_string()) {
            return std::unexpected(sappp::Error::make(
                "InvalidContract",
                "version_scope.conditions entries must be strings in " + std::string(source)));
        }
        conditions.push_back(item.get<std::string>());
    }
    std::ranges::stable_sort(conditions);
    scope["conditions"] = conditions;
    return scope;
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
normalize_contracts_array(const nlohmann::json& input,
                          const std::filesystem::path& schema_dir,
                          std::string_view source)
{
    if (!input.is_array()) {
        return std::unexpected(
            sappp::Error::make("InvalidContract",
                               "contracts must be an array in " + std::string(source)));
    }
    std::vector<nlohmann::json> contracts;
    contracts.reserve(input.size());
    for (const auto& entry : input) {
        auto normalized = normalize_contract_ir(entry, schema_dir);
        if (!normalized) {
            return std::unexpected(normalized.error());
        }
        contracts.push_back(std::move(*normalized));
    }
    return contracts;
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
normalize_contract_document(const nlohmann::json& input,
                            const std::filesystem::path& schema_dir,
                            std::string_view source)
{
    if (input.is_array()) {
        return normalize_contracts_array(input, schema_dir, source);
    }
    if (!input.is_object()) {
        return std::unexpected(sappp::Error::make("InvalidContract",
                                                  "SpecDB entry must be an object or array in "
                                                      + std::string(source)));
    }
    if (input.contains("schema_version") && input.at("schema_version").is_string()
        && input.at("schema_version").get<std::string>() == "specdb_snapshot.v1") {
        if (!input.contains("contracts")) {
            return std::unexpected(sappp::Error::make("InvalidContract",
                                                      "specdb_snapshot.v1 is missing contracts in "
                                                          + std::string(source)));
        }
        return normalize_contracts_array(input.at("contracts"), schema_dir, source);
    }
    if (input.contains("contracts")) {
        return normalize_contracts_array(input.at("contracts"), schema_dir, source);
    }
    auto normalized = normalize_contract_ir(input, schema_dir);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    return std::vector<nlohmann::json>{std::move(*normalized)};
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
parse_annotations_in_file(const AnnotationFileSpec& spec)
{
    std::ifstream in(spec.path);
    if (!in) {
        return std::unexpected(
            sappp::Error::make("IOError", "Failed to open source file: " + spec.path.string()));
    }
    std::vector<nlohmann::json> contracts;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        auto pos = line.find(kAnnotationPrefix);
        if (pos == std::string::npos) {
            continue;
        }
        std::string_view tail(line);
        tail.remove_prefix(pos + kAnnotationPrefix.size());
        std::string trimmed = trim_left(tail);
        if (!trimmed.starts_with(kAnnotationContract)) {
            continue;
        }
        trimmed = trim_left(std::string_view(trimmed).substr(kAnnotationContract.size()));
        std::string payload_text = trim(trimmed);
        if (payload_text.empty()) {
            return std::unexpected(sappp::Error::make("InvalidContract",
                                                      "Empty contract annotation in "
                                                          + spec.path.string() + ":"
                                                          + std::to_string(line_no)));
        }
        auto parsed = parse_inline_contract({.text = payload_text, .source = spec.path.string()});
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        auto normalized = normalize_contract_ir(*parsed, spec.schema_dir);
        if (!normalized) {
            return std::unexpected(normalized.error());
        }
        contracts.push_back(std::move(*normalized));
    }
    return contracts;
}

[[nodiscard]] std::vector<std::filesystem::path>
collect_sources_from_unit(const nlohmann::json& unit)
{
    std::vector<std::filesystem::path> sources;
    if (!unit.contains("argv") || !unit.at("argv").is_array()) {
        return sources;
    }
    std::filesystem::path cwd = unit.value("cwd", std::string{});
    for (const auto& arg : unit.at("argv")) {
        if (!arg.is_string()) {
            continue;
        }
        std::filesystem::path candidate(arg.get<std::string>());
        if (!has_source_extension(candidate)) {
            continue;
        }
        if (!candidate.is_absolute()) {
            std::filesystem::path base = cwd.empty() ? std::filesystem::current_path() : cwd;
            if (!base.is_absolute()) {
                base = std::filesystem::current_path() / base;
            }
            candidate = base / candidate;
        }
        sources.push_back(candidate.lexically_normal());
    }
    return sources;
}

[[nodiscard]] sappp::Result<std::vector<std::filesystem::path>>
collect_annotation_sources(const nlohmann::json& build_snapshot)
{
    if (!build_snapshot.contains("compile_units")
        || !build_snapshot.at("compile_units").is_array()) {
        return std::unexpected(
            sappp::Error::make("InvalidSnapshot",
                               "build_snapshot.compile_units is missing or invalid"));
    }
    std::unordered_set<std::string> seen;
    std::vector<std::filesystem::path> sources;
    for (const auto& unit : build_snapshot.at("compile_units")) {
        auto unit_sources = collect_sources_from_unit(unit);
        for (auto& path : unit_sources) {
            std::string key = path.generic_string();
            if (seen.insert(key).second) {
                sources.push_back(std::move(path));
            }
        }
    }
    std::ranges::stable_sort(sources, [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    return sources;
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
load_contracts_from_path(const std::filesystem::path& path, const std::filesystem::path& schema_dir)
{
    auto payload = read_json_file(path);
    if (!payload) {
        return std::unexpected(payload.error());
    }
    auto normalized = normalize_contract_document(*payload, schema_dir, path.string());
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    return *normalized;
}

[[nodiscard]] sappp::Result<std::vector<std::filesystem::path>>
list_sidecar_files(const std::filesystem::path& spec_path)
{
    if (!std::filesystem::is_directory(spec_path)) {
        return std::unexpected(
            sappp::Error::make("InvalidSpecPath",
                               "Spec path is not a directory: " + spec_path.string()));
    }
    std::vector<std::filesystem::path> json_files;
    for (const auto& entry : std::filesystem::directory_iterator(spec_path)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".json") {
            continue;
        }
        if (path.filename() == "snapshot.json") {
            continue;
        }
        json_files.push_back(path);
    }
    std::ranges::stable_sort(json_files, [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    return json_files;
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
load_sidecar_contracts(const std::filesystem::path& spec_path,
                       const std::filesystem::path& schema_dir)
{
    std::vector<nlohmann::json> contracts;
    if (spec_path.empty()) {
        return contracts;
    }
    if (std::filesystem::is_regular_file(spec_path)) {
        return load_contracts_from_path(spec_path, schema_dir);
    }
    auto json_files = list_sidecar_files(spec_path);
    if (!json_files) {
        return std::unexpected(json_files.error());
    }
    for (const auto& path : *json_files) {
        auto loaded = load_contracts_from_path(path, schema_dir);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        contracts.insert(contracts.end(),
                         std::make_move_iterator(loaded->begin()),
                         std::make_move_iterator(loaded->end()));
    }
    return contracts;
}

struct ContractSortKey
{
    std::string target_usr;
    std::string abi;
    std::string library_version;
    std::string conditions_key;
    int priority = 0;
    std::string contract_id;
};

[[nodiscard]] ContractSortKey build_sort_key(const nlohmann::json& contract)
{
    const auto& target = contract.at("target");
    const auto& scope = contract.at("version_scope");
    std::string target_usr = target.value("usr", std::string{});
    std::string abi = scope.value("abi", std::string{});
    std::string library_version = scope.value("library_version", std::string{});
    int priority = scope.value("priority", 0);
    std::string conditions_key;
    if (scope.contains("conditions") && scope.at("conditions").is_array()) {
        std::vector<std::string> conditions;
        for (const auto& entry : scope.at("conditions")) {
            if (entry.is_string()) {
                conditions.push_back(entry.get<std::string>());
            }
        }
        std::ranges::stable_sort(conditions);
        for (const auto& entry : conditions) {
            if (!conditions_key.empty()) {
                conditions_key += "|";
            }
            conditions_key += entry;
        }
    }
    std::string contract_id = contract.value("contract_id", std::string{});
    return ContractSortKey{.target_usr = std::move(target_usr),
                           .abi = std::move(abi),
                           .library_version = std::move(library_version),
                           .conditions_key = std::move(conditions_key),
                           .priority = priority,
                           .contract_id = std::move(contract_id)};
}

[[nodiscard]] sappp::Result<std::vector<nlohmann::json>>
collect_annotation_contracts(const nlohmann::json& build_snapshot,
                             const std::filesystem::path& schema_dir)
{
    auto sources = collect_annotation_sources(build_snapshot);
    if (!sources) {
        return std::unexpected(sources.error());
    }
    std::vector<nlohmann::json> contracts;
    for (const auto& path : *sources) {
        auto annotations = parse_annotations_in_file({.path = path, .schema_dir = schema_dir});
        if (!annotations) {
            return std::unexpected(annotations.error());
        }
        contracts.insert(contracts.end(),
                         std::make_move_iterator(annotations->begin()),
                         std::make_move_iterator(annotations->end()));
    }
    return contracts;
}

[[nodiscard]] std::vector<nlohmann::json> dedupe_contracts(std::vector<nlohmann::json> contracts)
{
    std::unordered_set<std::string> seen_contracts;
    std::vector<nlohmann::json> unique_contracts;
    unique_contracts.reserve(contracts.size());
    for (auto& contract : contracts) {
        std::string contract_id = contract.value("contract_id", std::string{});
        if (contract_id.empty()) {
            continue;
        }
        if (seen_contracts.insert(contract_id).second) {
            unique_contracts.push_back(std::move(contract));
        }
    }
    return unique_contracts;
}

void sort_contracts(std::vector<nlohmann::json>& contracts)
{
    std::ranges::stable_sort(contracts, [](const nlohmann::json& a, const nlohmann::json& b) {
        const auto key_a = build_sort_key(a);
        const auto key_b = build_sort_key(b);
        if (key_a.target_usr != key_b.target_usr) {
            return key_a.target_usr < key_b.target_usr;
        }
        if (key_a.abi != key_b.abi) {
            return key_a.abi < key_b.abi;
        }
        if (key_a.library_version != key_b.library_version) {
            return key_a.library_version < key_b.library_version;
        }
        if (key_a.conditions_key != key_b.conditions_key) {
            return key_a.conditions_key < key_b.conditions_key;
        }
        if (key_a.priority != key_b.priority) {
            return key_a.priority > key_b.priority;
        }
        return key_a.contract_id < key_b.contract_id;
    });
}

}  // namespace

sappp::Result<nlohmann::json> normalize_contract_ir(const nlohmann::json& input,
                                                    const std::filesystem::path& schema_dir)
{
    if (!input.is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidContract", "contract_ir entry must be an object"));
    }
    nlohmann::json contract = input;
    if (!contract.contains("schema_version")) {
        contract["schema_version"] = "contract_ir.v1";
    }
    if (!contract.contains("version_scope")) {
        contract["version_scope"] = nlohmann::json::object();
    }
    auto normalized_scope = normalize_contract_scope(contract.at("version_scope"), "contract_ir");
    if (!normalized_scope) {
        return std::unexpected(normalized_scope.error());
    }
    contract["version_scope"] = std::move(*normalized_scope);
    if (!contract.contains("contract_id")) {
        if (!contract.contains("target") || !contract.contains("tier")
            || !contract.contains("contract") || !contract.contains("version_scope")) {
            return std::unexpected(
                sappp::Error::make("InvalidContract",
                                   "Missing fields required to compute contract_id"));
        }
        auto contract_id = make_contract_id(contract);
        if (!contract_id) {
            return std::unexpected(contract_id.error());
        }
        contract["contract_id"] = *contract_id;
    }
    const auto schema_path = (schema_dir / "contract_ir.v1.schema.json").string();
    if (auto validation = sappp::common::validate_json(contract, schema_path); !validation) {
        return std::unexpected(validation.error());
    }
    return contract;
}

sappp::Result<nlohmann::json> build_snapshot(const BuildOptions& options)
{
    std::vector<nlohmann::json> contracts;
    auto sidecar_contracts = load_sidecar_contracts(options.spec_path, options.schema_dir);
    if (!sidecar_contracts) {
        return std::unexpected(sidecar_contracts.error());
    }
    contracts.insert(contracts.end(),
                     std::make_move_iterator(sidecar_contracts->begin()),
                     std::make_move_iterator(sidecar_contracts->end()));

    auto annotation_contracts =
        collect_annotation_contracts(options.build_snapshot, options.schema_dir);
    if (!annotation_contracts) {
        return std::unexpected(annotation_contracts.error());
    }
    contracts.insert(contracts.end(),
                     std::make_move_iterator(annotation_contracts->begin()),
                     std::make_move_iterator(annotation_contracts->end()));

    auto unique_contracts = dedupe_contracts(std::move(contracts));
    sort_contracts(unique_contracts);

    nlohmann::json snapshot = {
        {"schema_version", "specdb_snapshot.v1"},
        {          "tool",         options.tool},
        {  "generated_at", options.generated_at},
        {     "contracts",     unique_contracts}
    };

    auto digest = sappp::canonical::hash_canonical(snapshot.at("contracts"));
    if (!digest) {
        return std::unexpected(digest.error());
    }
    snapshot["specdb_digest"] = *digest;

    const auto schema_path = (options.schema_dir / "specdb_snapshot.v1.schema.json").string();
    if (auto validation = sappp::common::validate_json(snapshot, schema_path); !validation) {
        return std::unexpected(validation.error());
    }

    return snapshot;
}

}  // namespace sappp::specdb
