/**
 * @file analyzer.cpp
 * @brief Analyzer v0 stub implementation
 */

#include "analyzer.hpp"

#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sappp::analyzer {

namespace {

constexpr std::string_view kBaseSafetyDomain = "interval+null+lifetime+init";
constexpr std::string_view kPointsToDomainSimple = "interval+null+lifetime+init+points-to.simple";
constexpr std::string_view kPointsToDomainContext = "interval+null+lifetime+init+points-to.context";
constexpr std::string_view kPointsToNullTarget = "null";
constexpr std::string_view kPointsToInBoundsTarget = "inbounds";
constexpr std::string_view kPointsToOutOfBoundsTarget = "oob";
constexpr std::size_t kMaxPointsToTargets = 4;
constexpr std::string_view kDeterministicGeneratedAt = "1970-01-01T00:00:00Z";

// NOLINTBEGIN(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
// - Explicit init keeps -Weffc++ satisfied for POD-style holders.
struct VersionScopeInfo
{
    std::string abi;
    std::string library_version;
    std::vector<std::string> conditions;
    int priority;

    VersionScopeInfo()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : abi()
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , library_version()
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , conditions()
        , priority(0)
    {}
};
// NOLINTEND(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)

struct ContractInfo
{
    std::string contract_id;
    std::string tier;
    std::string target_usr;
    nlohmann::json version_scope;
    VersionScopeInfo scope;
    bool has_pre = false;
    bool has_post = false;
    bool has_frame = false;
    bool has_ownership = false;
    bool has_concurrency = false;
};

using ContractIndex = std::map<std::string, std::vector<ContractInfo>>;

struct VCallSummary
{
    bool has_vcall = false;
    bool missing_candidate_set = false;
    bool empty_candidate_set = false;
    std::vector<std::string> missing_candidate_ids;
    std::vector<std::string> candidate_methods;
    std::vector<const ContractInfo*> candidate_contracts;
    std::vector<std::string> missing_contract_targets;

    VCallSummary()
        // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
        : has_vcall(false)  // Weffc++ explicit init.
        // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
        , missing_candidate_set(false)  // Weffc++ explicit init.
        // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
        , empty_candidate_set(false)  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , missing_candidate_ids()  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , candidate_methods()  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , candidate_contracts()  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , missing_contract_targets()  // Weffc++ explicit init.
    {}
};

using VCallSummaryMap = std::map<std::string, VCallSummary>;

struct JsonFieldContext
{
    const nlohmann::json* obj = nullptr;
    std::string_view key;
    std::string_view context;
};

struct BudgetTracker
{
    AnalyzerConfig::AnalysisBudget budget;
    std::chrono::steady_clock::time_point start_time;
    std::uint64_t iterations = 0;
    std::uint64_t states = 0;
    std::uint64_t summary_nodes = 0;
    std::optional<std::string> exceeded_limit;
    std::set<std::string> summary_nodes_seen;

    explicit BudgetTracker(AnalyzerConfig::AnalysisBudget budget_in)
        : budget(budget_in)
        , start_time(std::chrono::steady_clock::now())
        , exceeded_limit()      // NOLINT(readability-redundant-member-init) - -Weffc++.
        , summary_nodes_seen()  // NOLINT(readability-redundant-member-init) - -Weffc++.
    {}

    [[nodiscard]] bool exceeded() const { return exceeded_limit.has_value(); }

    [[nodiscard]] std::optional<std::string> limit_reason() const { return exceeded_limit; }

    bool check_time()
    {
        if (exceeded()) {
            return false;
        }
        if (!budget.max_time_ms.has_value()) {
            return true;
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start_time)
                                    .count();
        if (elapsed_ms > static_cast<long long>(*budget.max_time_ms)) {
            exceeded_limit = "max_time_ms";
            return false;
        }
        return true;
    }

    bool consume_iteration()
    {
        if (!check_time()) {
            return false;
        }
        ++iterations;
        if (budget.max_iterations.has_value() && iterations > *budget.max_iterations) {
            exceeded_limit = "max_iterations";
            return false;
        }
        return true;
    }

    bool consume_state(std::size_t count)
    {
        if (!check_time()) {
            return false;
        }
        states += static_cast<std::uint64_t>(count);
        if (budget.max_states.has_value() && states > *budget.max_states) {
            exceeded_limit = "max_states";
            return false;
        }
        return true;
    }

    bool consume_summary_node(std::string_view function_uid)
    {
        if (!check_time()) {
            return false;
        }
        std::string key(function_uid);
        if (!summary_nodes_seen.insert(key).second) {
            return true;
        }
        ++summary_nodes;
        if (budget.max_summary_nodes.has_value() && summary_nodes > *budget.max_summary_nodes) {
            exceeded_limit = "max_summary_nodes";
            return false;
        }
        return true;
    }
};

[[nodiscard]] sappp::Result<std::string> require_string(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_string()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected string field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return obj.at(key).get<std::string>();
}

[[nodiscard]] sappp::Result<nlohmann::json> require_object(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_object()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected object field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return obj.at(key);
}

[[nodiscard]] sappp::Result<const nlohmann::json*> require_array(const JsonFieldContext& input)
{
    const nlohmann::json& obj = *input.obj;
    const std::string_view key = input.key;
    const std::string_view context = input.context;

    if (!obj.contains(key)) {
        return std::unexpected(sappp::Error::make("MissingField",
                                                  std::string("Missing required field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    if (!obj.at(key).is_array()) {
        return std::unexpected(sappp::Error::make("InvalidFieldType",
                                                  std::string("Expected array field '")
                                                      + std::string(key) + "' in "
                                                      + std::string(context)));
    }
    return &obj.at(key);
}

// NOLINTNEXTLINE(readability-function-size) - Keep scope validation explicit for diagnostics.
[[nodiscard]] sappp::Result<VersionScopeInfo> parse_version_scope(const nlohmann::json& contract,
                                                                  nlohmann::json& normalized_scope)
{
    VersionScopeInfo scope;
    if (!contract.contains("version_scope")) {
        normalized_scope = nlohmann::json::object();
        return scope;
    }
    if (!contract.at("version_scope").is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "version_scope must be an object in contract"));
    }
    normalized_scope = contract.at("version_scope");
    const auto& scope_obj = contract.at("version_scope");

    if (scope_obj.contains("abi")) {
        if (!scope_obj.at("abi").is_string()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "version_scope.abi must be a string"));
        }
        scope.abi = scope_obj.at("abi").get<std::string>();
    }
    if (scope_obj.contains("library_version")) {
        if (!scope_obj.at("library_version").is_string()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType",
                                   "version_scope.library_version must be a string"));
        }
        scope.library_version = scope_obj.at("library_version").get<std::string>();
    }
    if (scope_obj.contains("priority")) {
        if (!scope_obj.at("priority").is_number_integer()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "version_scope.priority must be integer"));
        }
        scope.priority = scope_obj.at("priority").get<int>();
        normalized_scope["priority"] = scope.priority;
    } else {
        normalized_scope["priority"] = 0;
    }
    if (scope_obj.contains("conditions")) {
        if (!scope_obj.at("conditions").is_array()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "version_scope.conditions must be array"));
        }
        for (const auto& entry : scope_obj.at("conditions")) {
            if (!entry.is_string()) {
                return std::unexpected(
                    sappp::Error::make("InvalidFieldType",
                                       "version_scope.conditions entries must be strings"));
            }
            scope.conditions.push_back(entry.get<std::string>());
        }
        std::ranges::stable_sort(scope.conditions);
        auto unique_end = std::ranges::unique(scope.conditions);
        scope.conditions.erase(unique_end.begin(), unique_end.end());
        normalized_scope["conditions"] = scope.conditions;
    } else {
        normalized_scope["conditions"] = nlohmann::json::array();
    }
    return scope;
}

[[nodiscard]] sappp::Result<ContractInfo> parse_contract_entry(const nlohmann::json& contract)
{
    if (!contract.is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Contract entry must be an object"));
    }
    auto contract_id = require_string(
        JsonFieldContext{.obj = &contract, .key = "contract_id", .context = "contract"});
    if (!contract_id) {
        return std::unexpected(contract_id.error());
    }
    auto tier =
        require_string(JsonFieldContext{.obj = &contract, .key = "tier", .context = "contract"});
    if (!tier) {
        return std::unexpected(tier.error());
    }
    auto target_obj =
        require_object(JsonFieldContext{.obj = &contract, .key = "target", .context = "contract"});
    if (!target_obj) {
        return std::unexpected(target_obj.error());
    }
    auto target_usr = require_string(
        JsonFieldContext{.obj = &(*target_obj), .key = "usr", .context = "contract.target"});
    if (!target_usr) {
        return std::unexpected(target_usr.error());
    }

    nlohmann::json version_scope;
    auto scope_info = parse_version_scope(contract, version_scope);
    if (!scope_info) {
        return std::unexpected(scope_info.error());
    }

    bool has_pre = false;
    bool has_post = false;
    bool has_frame = false;
    bool has_ownership = false;
    bool has_concurrency = false;
    if (contract.contains("contract") && contract.at("contract").is_object()) {
        const auto& body = contract.at("contract");
        has_pre = body.contains("pre");
        has_post = body.contains("post");
        has_frame = body.contains("frame");
        has_ownership = body.contains("ownership");
        has_concurrency = body.contains("concurrency");
    }

    return ContractInfo{
        .contract_id = std::move(*contract_id),
        .tier = std::move(*tier),
        .target_usr = std::move(*target_usr),
        .version_scope = std::move(version_scope),
        .scope = std::move(*scope_info),
        .has_pre = has_pre,
        .has_post = has_post,
        .has_frame = has_frame,
        .has_ownership = has_ownership,
        .has_concurrency = has_concurrency,
    };
}

[[nodiscard]] sappp::Result<ContractIndex>
build_contract_index(const nlohmann::json* specdb_snapshot)
{
    ContractIndex index;
    if (specdb_snapshot == nullptr) {
        return index;
    }
    auto contracts_array = require_array(
        JsonFieldContext{.obj = specdb_snapshot, .key = "contracts", .context = "specdb_snapshot"});
    if (!contracts_array) {
        return std::unexpected(contracts_array.error());
    }

    for (const auto& entry : **contracts_array) {
        auto contract_info = parse_contract_entry(entry);
        if (!contract_info) {
            return std::unexpected(contract_info.error());
        }
        index[contract_info->target_usr].push_back(std::move(*contract_info));
    }

    for (auto& [usr, contracts] : index) {
        (void)usr;
        std::ranges::stable_sort(contracts,
                                 [](const ContractInfo& a, const ContractInfo& b) noexcept {
                                     return a.contract_id < b.contract_id;
                                 });
    }

    return index;
}

using VCallCandidateSetMap = std::map<std::string, std::vector<std::string>>;

[[nodiscard]] std::optional<std::string> extract_vcall_candidate_id(const nlohmann::json& inst)
{
    if (!inst.contains("args") || !inst.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = inst.at("args");
    if (args.size() < 2U || !args.at(1).is_string()) {
        return std::nullopt;
    }
    return args.at(1).get<std::string>();
}

[[nodiscard]] std::vector<const ContractInfo*>
select_contracts_for_target(std::string_view usr,
                            const ContractIndex& contract_index,
                            const ContractMatchContext& context);

// NOLINTNEXTLINE(readability-function-size) - Parse vcall candidate tables.
[[nodiscard]] VCallCandidateSetMap collect_vcall_candidate_sets(const nlohmann::json& func)
{
    VCallCandidateSetMap sets;
    if (!func.contains("tables") || !func.at("tables").is_object()) {
        return sets;
    }
    const auto& tables = func.at("tables");
    if (!tables.contains("vcall_candidates") || !tables.at("vcall_candidates").is_array()) {
        return sets;
    }
    for (const auto& entry : tables.at("vcall_candidates")) {
        if (!entry.is_object() || !entry.contains("id") || !entry.at("id").is_string()) {
            continue;
        }
        std::vector<std::string> methods;
        if (entry.contains("methods") && entry.at("methods").is_array()) {
            for (const auto& method : entry.at("methods")) {
                if (method.is_string()) {
                    methods.emplace_back(method.get<std::string>());
                }
            }
        }
        std::ranges::stable_sort(methods);
        auto unique_end = std::ranges::unique(methods);
        methods.erase(unique_end.begin(), methods.end());
        sets.emplace(entry.at("id").get<std::string>(), std::move(methods));
    }
    return sets;
}

// NOLINTNEXTLINE(readability-function-size) - Summarize vcall candidates.
[[nodiscard]] VCallSummaryMap build_vcall_summary_map(const nlohmann::json& nir_json,
                                                      const ContractIndex& contract_index,
                                                      const ContractMatchContext& context)
{
    VCallSummaryMap summaries;
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return summaries;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        std::string function_uid = func.at("function_uid").get<std::string>();

        VCallSummary summary;
        const auto candidate_sets = collect_vcall_candidate_sets(func);

        if (!func.contains("cfg") || !func.at("cfg").is_object()) {
            continue;
        }
        const auto& cfg = func.at("cfg");
        if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
            continue;
        }

        for (const auto& block : cfg.at("blocks")) {
            if (!block.is_object() || !block.contains("insts") || !block.at("insts").is_array()) {
                continue;
            }
            for (const auto& inst : block.at("insts")) {
                if (!inst.is_object() || !inst.contains("op") || !inst.at("op").is_string()) {
                    continue;
                }
                if (inst.at("op").get<std::string>() != "vcall") {
                    continue;
                }
                summary.has_vcall = true;
                auto candidate_id = extract_vcall_candidate_id(inst);
                if (!candidate_id) {
                    summary.missing_candidate_set = true;
                    summary.missing_candidate_ids.emplace_back("unknown");
                    continue;
                }
                auto candidate_it = candidate_sets.find(*candidate_id);
                if (candidate_it == candidate_sets.end()) {
                    summary.missing_candidate_set = true;
                    summary.missing_candidate_ids.emplace_back(*candidate_id);
                    continue;
                }
                if (candidate_it->second.empty()) {
                    summary.empty_candidate_set = true;
                    continue;
                }
                summary.candidate_methods.insert(summary.candidate_methods.end(),
                                                 candidate_it->second.begin(),
                                                 candidate_it->second.end());
            }
        }

        if (!summary.has_vcall) {
            continue;
        }

        std::ranges::stable_sort(summary.missing_candidate_ids);
        auto missing_unique = std::ranges::unique(summary.missing_candidate_ids);
        summary.missing_candidate_ids.erase(missing_unique.begin(), missing_unique.end());

        std::ranges::stable_sort(summary.candidate_methods);
        auto method_unique = std::ranges::unique(summary.candidate_methods);
        summary.candidate_methods.erase(method_unique.begin(), method_unique.end());

        for (const auto& method : summary.candidate_methods) {
            bool has_pre = false;
            auto matched_contracts = select_contracts_for_target(method, contract_index, context);
            for (const auto* contract : matched_contracts) {
                summary.candidate_contracts.push_back(contract);
                has_pre = has_pre || contract->has_pre;
            }
            if (!has_pre) {
                summary.missing_contract_targets.push_back(method);
            }
        }

        std::ranges::stable_sort(summary.missing_contract_targets);
        auto missing_contract_unique = std::ranges::unique(summary.missing_contract_targets);
        summary.missing_contract_targets.erase(missing_contract_unique.begin(),
                                               missing_contract_unique.end());

        std::ranges::stable_sort(summary.candidate_contracts,
                                 [](const ContractInfo* a, const ContractInfo* b) noexcept {
                                     return a->contract_id < b->contract_id;
                                 });
        auto contract_unique =
            std::ranges::unique(summary.candidate_contracts,
                                [](const ContractInfo* a, const ContractInfo* b) noexcept {
                                    return a->contract_id == b->contract_id;
                                });
        summary.candidate_contracts.erase(contract_unique.begin(), contract_unique.end());

        summaries.emplace(std::move(function_uid), std::move(summary));
    }

    return summaries;
}

[[nodiscard]] std::unordered_map<std::string, std::string>
build_function_uid_map(const nlohmann::json& nir_json)
{
    std::unordered_map<std::string, std::string> mapping;
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return mapping;
    }
    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("mangled_name") || !func.contains("function_uid")) {
            continue;
        }
        if (!func.at("mangled_name").is_string() || !func.at("function_uid").is_string()) {
            continue;
        }
        mapping.emplace(func.at("mangled_name").get<std::string>(),
                        func.at("function_uid").get<std::string>());
    }
    return mapping;
}

[[nodiscard]] sappp::Result<std::string>
resolve_function_uid(const std::unordered_map<std::string, std::string>& mapping,
                     const nlohmann::json& po)
{
    auto function_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "function", .context = "po"});
    if (!function_obj) {
        return std::unexpected(function_obj.error());
    }
    auto mangled = require_string(
        JsonFieldContext{.obj = &(*function_obj), .key = "mangled", .context = "po.function"});
    if (!mangled) {
        return std::unexpected(mangled.error());
    }

    auto it = mapping.find(*mangled);
    if (it != mapping.end()) {
        return it->second;
    }

    if (function_obj->contains("usr") && function_obj->at("usr").is_string()) {
        return function_obj->at("usr").get<std::string>();
    }

    return *mangled;
}

struct ContractMatchSummary
{
    std::vector<const ContractInfo*> contracts;
    bool has_pre = false;
    bool has_concurrency = false;

    ContractMatchSummary()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : contracts()
    {}
};

[[nodiscard]] ContractMatchContext normalize_match_context(ContractMatchContext context)
{
    std::ranges::stable_sort(context.conditions);
    auto unique_end = std::ranges::unique(context.conditions);
    context.conditions.erase(unique_end.begin(), unique_end.end());
    return context;
}

[[nodiscard]] bool is_subset_sorted(const std::vector<std::string>& subset,
                                    const std::vector<std::string>& superset)
{
    return std::ranges::includes(superset, subset);
}

struct ContractMatchCandidate
{
    const ContractInfo* contract = nullptr;
    bool abi_specific = false;
    bool library_specific = false;
    std::size_t conditions_specificity = 0;
};

[[nodiscard]] std::optional<ContractMatchCandidate>
evaluate_contract_candidate(const ContractInfo& contract, const ContractMatchContext& context)
{
    ContractMatchCandidate candidate;
    candidate.contract = &contract;

    if (!contract.scope.abi.empty()) {
        if (context.abi.empty() || contract.scope.abi != context.abi) {
            return std::nullopt;
        }
        candidate.abi_specific = true;
    }
    if (!contract.scope.library_version.empty()) {
        if (context.library_version.empty()
            || contract.scope.library_version != context.library_version) {
            return std::nullopt;
        }
        candidate.library_specific = true;
    }
    if (!contract.scope.conditions.empty()) {
        if (context.conditions.empty()
            || !is_subset_sorted(contract.scope.conditions, context.conditions)) {
            return std::nullopt;
        }
        candidate.conditions_specificity = contract.scope.conditions.size();
    }
    return candidate;
}

// NOLINTBEGIN(readability-function-size) - Matching rules are explicitly staged.
[[nodiscard]] std::vector<const ContractInfo*>
select_contracts_for_target(std::string_view usr,
                            const ContractIndex& contract_index,
                            const ContractMatchContext& context)
{
    auto it = contract_index.find(std::string(usr));
    if (it == contract_index.end()) {
        return {};
    }

    std::vector<ContractMatchCandidate> candidates;
    candidates.reserve(it->second.size());
    for (const auto& contract : it->second) {
        auto candidate = evaluate_contract_candidate(contract, context);
        if (!candidate) {
            continue;
        }
        candidates.push_back(*candidate);
    }
    if (candidates.empty()) {
        return {};
    }

    bool has_specific_abi = std::ranges::any_of(candidates, [](const auto& entry) noexcept {
        return entry.abi_specific;
    });
    if (has_specific_abi) {
        auto abi_end = std::ranges::remove_if(candidates, [](const auto& entry) noexcept {
            return !entry.abi_specific;
        });
        candidates.erase(abi_end.begin(), abi_end.end());
    }

    bool has_specific_library = std::ranges::any_of(candidates, [](const auto& entry) noexcept {
        return entry.library_specific;
    });
    if (has_specific_library) {
        auto lib_end = std::ranges::remove_if(candidates, [](const auto& entry) noexcept {
            return !entry.library_specific;
        });
        candidates.erase(lib_end.begin(), lib_end.end());
    }

    std::size_t max_conditions = 0;
    for (const auto& candidate : candidates) {
        max_conditions = std::max(max_conditions, candidate.conditions_specificity);
    }
    if (max_conditions > 0) {
        auto cond_end = std::ranges::remove_if(candidates, [&](const auto& entry) noexcept {
            return entry.conditions_specificity != max_conditions;
        });
        candidates.erase(cond_end.begin(), cond_end.end());
    }

    int max_priority = std::numeric_limits<int>::min();
    for (const auto& candidate : candidates) {
        max_priority = std::max(max_priority, candidate.contract->scope.priority);
    }
    auto priority_end = std::ranges::remove_if(candidates, [&](const auto& entry) noexcept {
        return entry.contract->scope.priority != max_priority;
    });
    candidates.erase(priority_end.begin(), priority_end.end());

    std::vector<const ContractInfo*> matched;
    matched.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        matched.push_back(candidate.contract);
    }
    std::ranges::stable_sort(matched, [](const ContractInfo* a, const ContractInfo* b) noexcept {
        return a->contract_id < b->contract_id;
    });
    auto unique_end =
        std::ranges::unique(matched, [](const ContractInfo* a, const ContractInfo* b) noexcept {
            return a->contract_id == b->contract_id;
        });
    matched.erase(unique_end.begin(), matched.end());
    return matched;
}
// NOLINTEND(readability-function-size)

[[nodiscard]] sappp::Result<ContractMatchSummary>
match_contracts_for_po(const nlohmann::json& po,
                       const ContractIndex& contract_index,
                       const ContractMatchContext& context)
{
    auto function_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "function", .context = "po"});
    if (!function_obj) {
        return std::unexpected(function_obj.error());
    }
    auto usr = require_string(
        JsonFieldContext{.obj = &(*function_obj), .key = "usr", .context = "po.function"});
    if (!usr) {
        return std::unexpected(usr.error());
    }

    ContractMatchSummary summary;
    auto matched = select_contracts_for_target(*usr, contract_index, context);
    if (matched.empty()) {
        return summary;
    }
    summary.contracts.reserve(matched.size());
    for (const auto* contract : matched) {
        summary.contracts.push_back(contract);
        summary.has_pre = summary.has_pre || contract->has_pre;
        summary.has_concurrency = summary.has_concurrency || contract->has_concurrency;
    }
    return summary;
}

[[nodiscard]] std::vector<std::string>
collect_contract_ids(const ContractMatchSummary& summary,
                     const std::vector<const ContractInfo*>& extra_contracts)
{
    std::vector<std::string> ids;
    ids.reserve(summary.contracts.size() + extra_contracts.size());
    for (const auto* contract : summary.contracts) {
        ids.push_back(contract->contract_id);
    }
    for (const auto* contract : extra_contracts) {
        ids.push_back(contract->contract_id);
    }
    std::ranges::stable_sort(ids);
    auto unique_ids = std::ranges::unique(ids);
    ids.erase(unique_ids.begin(), unique_ids.end());
    return ids;
}

[[nodiscard]] std::vector<std::string> collect_contract_ids(const ContractMatchSummary& summary)
{
    std::vector<const ContractInfo*> none;
    return collect_contract_ids(summary, none);
}

[[nodiscard]] std::vector<const ContractInfo*>
merge_contracts(const ContractMatchSummary& summary,
                const std::vector<const ContractInfo*>& extra_contracts)
{
    std::vector<const ContractInfo*> merged;
    merged.reserve(summary.contracts.size() + extra_contracts.size());
    for (const auto* contract : summary.contracts) {
        merged.push_back(contract);
    }
    for (const auto* contract : extra_contracts) {
        merged.push_back(contract);
    }
    std::ranges::stable_sort(merged, [](const ContractInfo* a, const ContractInfo* b) noexcept {
        return a->contract_id < b->contract_id;
    });
    auto unique_end =
        std::ranges::unique(merged, [](const ContractInfo* a, const ContractInfo* b) noexcept {
            return a->contract_id == b->contract_id;
        });
    merged.erase(unique_end.begin(), unique_end.end());
    return merged;
}

struct IrAnchor
{
    std::string block_id;
    std::string inst_id;
};

[[nodiscard]] sappp::Result<IrAnchor> extract_anchor(const nlohmann::json& po)
{
    auto anchor_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "anchor", .context = "po"});
    if (!anchor_obj) {
        return std::unexpected(anchor_obj.error());
    }
    auto block_id = require_string(
        JsonFieldContext{.obj = &(*anchor_obj), .key = "block_id", .context = "po.anchor"});
    if (!block_id) {
        return std::unexpected(block_id.error());
    }
    auto inst_id = require_string(
        JsonFieldContext{.obj = &(*anchor_obj), .key = "inst_id", .context = "po.anchor"});
    if (!inst_id) {
        return std::unexpected(inst_id.error());
    }
    return IrAnchor{.block_id = std::move(*block_id), .inst_id = std::move(*inst_id)};
}

[[nodiscard]] sappp::Result<nlohmann::json> extract_predicate_expr(const nlohmann::json& po)
{
    auto predicate_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "predicate", .context = "po"});
    if (!predicate_obj) {
        return std::unexpected(predicate_obj.error());
    }
    if (!predicate_obj->contains("expr") || !predicate_obj->at("expr").is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected predicate.expr object in po"));
    }
    return predicate_obj->at("expr");
}

[[nodiscard]] sappp::Result<std::string> extract_predicate_pretty(const nlohmann::json& po)
{
    auto predicate_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "predicate", .context = "po"});
    if (!predicate_obj) {
        return std::unexpected(predicate_obj.error());
    }
    if (predicate_obj->contains("pretty") && predicate_obj->at("pretty").is_string()) {
        return predicate_obj->at("pretty").get<std::string>();
    }
    return std::string("predicate");
}

enum class LifetimeValue {
    kAlive,
    kDead,
    kMaybe,
};

struct LifetimeState
{
    std::map<std::string, LifetimeValue> values;

    LifetimeState()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : values()
    {}
};

[[nodiscard]] LifetimeValue merge_lifetime_value(LifetimeValue a, LifetimeValue b)
{
    if (a == b) {
        return a;
    }
    return LifetimeValue::kMaybe;
}

[[nodiscard]] LifetimeState merge_lifetime_states(const LifetimeState& a, const LifetimeState& b)
{
    LifetimeState result;
    for (const auto& [key, value] : a.values) {
        LifetimeValue other = LifetimeValue::kMaybe;
        auto it = b.values.find(key);
        if (it != b.values.end()) {
            other = it->second;
        }
        result.values.emplace(key, merge_lifetime_value(value, other));
    }
    for (const auto& [key, value] : b.values) {
        if (result.values.contains(key)) {
            continue;
        }
        result.values.emplace(key, merge_lifetime_value(LifetimeValue::kMaybe, value));
    }
    return result;
}

enum class LifetimeFlow {
    kNormal,
    kException,
};

struct FlowPredecessors
{
    std::vector<std::string> normal;
    std::vector<std::string> exception;

    FlowPredecessors()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : normal()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        , exception()
    {}
};

[[nodiscard]] std::optional<std::string>
extract_first_string_arg(const nlohmann::json& inst)  // NOLINTNEXTLINE(readability-function-size)
{
    if (!inst.contains("args") || !inst.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = inst.at("args");
    if (args.empty()) {
        return std::nullopt;
    }
    const auto& first = args.at(0);
    if (!first.is_string()) {
        return std::nullopt;
    }
    return first.get<std::string>();
}

[[nodiscard]] std::optional<std::string>
extract_second_string_arg(const nlohmann::json& inst)  // NOLINTNEXTLINE(readability-function-size)
{
    if (!inst.contains("args") || !inst.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = inst.at("args");
    if (args.size() < 2) {
        return std::nullopt;
    }
    const auto& second = args.at(1);
    if (!second.is_string()) {
        return std::nullopt;
    }
    return second.get<std::string>();
}

[[nodiscard]] std::optional<std::string> extract_ref_name(const nlohmann::json& arg)
{
    if (!arg.is_object()) {
        return std::nullopt;
    }
    if (!arg.contains("name") || !arg.at("name").is_string()) {
        return std::nullopt;
    }
    return arg.at("name").get<std::string>();
}

[[nodiscard]] std::optional<bool> extract_ref_has_init(const nlohmann::json& arg)
{
    if (!arg.is_object()) {
        return std::nullopt;
    }
    if (!arg.contains("has_init") || !arg.at("has_init").is_boolean()) {
        return std::nullopt;
    }
    return arg.at("has_init").get<bool>();
}

void apply_lifetime_effect(const nlohmann::json& inst, LifetimeState& state)
{
    if (!inst.contains("op") || !inst.at("op").is_string()) {
        return;
    }
    const auto& op = inst.at("op").get_ref<const std::string&>();
    auto label = extract_first_string_arg(inst);
    if (!label.has_value()) {
        return;
    }
    if (op == "lifetime.begin") {
        state.values[*label] = LifetimeValue::kAlive;
    } else if (op == "lifetime.end" || op == "dtor") {
        state.values[*label] = LifetimeValue::kDead;
    } else if (op == "move") {
        auto source_label = extract_second_string_arg(inst);
        if (source_label.has_value()) {
            state.values[*source_label] = LifetimeValue::kMaybe;
        }
    }
}

struct FunctionLifetimeAnalysis
{
    std::string function_uid;
    std::string entry_block;
    std::map<std::string, const nlohmann::json*> blocks;
    std::vector<std::string> block_order;
    std::map<std::string, FlowPredecessors> predecessors;
    std::map<std::string, bool> has_exception_successor;
    std::map<std::string, bool> has_landingpad;
    std::map<std::string, LifetimeState> normal_in_states;
    std::map<std::string, LifetimeState> normal_out_states;
    std::map<std::string, LifetimeState> exception_in_states;
    std::map<std::string, LifetimeState> exception_out_states;

    // NOLINTBEGIN(readability-redundant-member-init) - required for -Weffc++.
    FunctionLifetimeAnalysis()
        : function_uid()
        , entry_block()
        , blocks()
        , block_order()
        , predecessors()
        , has_exception_successor()
        , has_landingpad()
        , normal_in_states()
        , normal_out_states()
        , exception_in_states()
        , exception_out_states()
    {}
    // NOLINTEND(readability-redundant-member-init)
};

struct LifetimeAnalysisCache
{
    std::map<std::string, FunctionLifetimeAnalysis> functions;

    LifetimeAnalysisCache()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : functions()
    {}
};

struct FunctionFeatureFlags
{
    bool has_exception_flow = false;
    bool has_unmodeled_exception_flow = false;
    bool has_vcall = false;
    bool has_atomic = false;
    bool has_thread = false;
    bool has_sync = false;
};

using FunctionFeatureCache = std::map<std::string, FunctionFeatureFlags>;

[[nodiscard]] LifetimeState merge_predecessor_states(const FunctionLifetimeAnalysis& analysis,
                                                     std::string_view block_id,
                                                     LifetimeFlow flow)
{
    auto pred_it = analysis.predecessors.find(std::string(block_id));
    if (pred_it == analysis.predecessors.end()) {
        return LifetimeState{};
    }

    const std::vector<std::string>* pred_list = nullptr;
    const std::map<std::string, LifetimeState>* out_states = nullptr;
    if (flow == LifetimeFlow::kNormal) {
        pred_list = &pred_it->second.normal;
        out_states = &analysis.normal_out_states;
    } else {
        pred_list = &pred_it->second.exception;
        out_states = &analysis.exception_out_states;
    }

    if (pred_list->empty()) {
        return LifetimeState{};
    }

    bool first = true;
    LifetimeState merged;
    for (const auto& pred : *pred_list) {
        auto out_it = out_states->find(pred);
        if (out_it == out_states->end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_lifetime_states(merged, out_it->second);
    }

    if (first) {
        return LifetimeState{};
    }
    return merged;
}

struct LifetimeTransferResult
{
    LifetimeState normal_out;
    std::optional<LifetimeState> exception_out;
    bool has_exception_op = false;
};

[[nodiscard]] LifetimeTransferResult
apply_lifetime_block_transfer_with_exception(const LifetimeState& in_state,
                                             const nlohmann::json& block)
{
    LifetimeState normal_state = in_state;
    std::optional<LifetimeState> exception_state;
    bool has_exception_op = false;

    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return LifetimeTransferResult{.normal_out = normal_state,
                                      .exception_out = std::move(exception_state),
                                      .has_exception_op = has_exception_op};
    }

    for (const auto& inst : block.at("insts")) {
        std::string_view op;
        bool has_op = false;
        if (inst.contains("op") && inst.at("op").is_string()) {
            op = inst.at("op").get_ref<const std::string&>();
            has_op = true;
        }
        const bool is_invoke = has_op && op == "invoke";
        const bool is_throw = has_op && (op == "throw" || op == "resume");
        if (is_invoke || is_throw) {
            apply_lifetime_effect(inst, normal_state);
            has_exception_op = true;
            if (exception_state.has_value()) {
                exception_state = merge_lifetime_states(*exception_state, normal_state);
            } else {
                exception_state = normal_state;
            }
            if (is_throw) {
                return LifetimeTransferResult{.normal_out = normal_state,
                                              .exception_out = std::move(exception_state),
                                              .has_exception_op = has_exception_op};
            }
            continue;
        }
        apply_lifetime_effect(inst, normal_state);
    }

    return LifetimeTransferResult{.normal_out = normal_state,
                                  .exception_out = std::move(exception_state),
                                  .has_exception_op = has_exception_op};
}

// NOLINTNEXTLINE(readability-function-size) - Fixpoint loop is clearer in one block.
void compute_lifetime_fixpoint(FunctionLifetimeAnalysis& analysis, BudgetTracker* budget)
{
    bool changed = true;
    while (changed) {
        if (budget != nullptr && budget->exceeded()) {
            return;
        }
        changed = false;
        for (const auto& block_id : analysis.block_order) {
            if (budget != nullptr && !budget->consume_iteration()) {
                return;
            }
            LifetimeState normal_in =
                merge_predecessor_states(analysis, block_id, LifetimeFlow::kNormal);
            LifetimeState exception_in =
                merge_predecessor_states(analysis, block_id, LifetimeFlow::kException);

            auto pred_it = analysis.predecessors.find(block_id);
            bool has_normal_preds = false;
            bool has_exception_preds = false;
            if (pred_it != analysis.predecessors.end()) {
                has_normal_preds = !pred_it->second.normal.empty();
                has_exception_preds = !pred_it->second.exception.empty();
            }

            LifetimeState normal_entry = normal_in;
            if (has_exception_preds && !has_normal_preds) {
                normal_entry = exception_in;
            } else if (has_exception_preds && has_normal_preds) {
                normal_entry = merge_lifetime_states(normal_entry, exception_in);
            }

            if (auto landingpad_it = analysis.has_landingpad.find(block_id);
                landingpad_it != analysis.has_landingpad.end() && landingpad_it->second) {
                normal_entry = merge_lifetime_states(normal_entry, exception_in);
            }

            auto normal_in_it = analysis.normal_in_states.find(block_id);
            if (normal_in_it == analysis.normal_in_states.end()
                || normal_in_it->second.values != normal_entry.values) {
                analysis.normal_in_states[block_id] = normal_entry;
                changed = true;
                if (budget != nullptr && !budget->consume_state(normal_in.values.size())) {
                    return;
                }
            }

            auto exception_in_it = analysis.exception_in_states.find(block_id);
            if (exception_in_it == analysis.exception_in_states.end()
                || exception_in_it->second.values != exception_in.values) {
                analysis.exception_in_states[block_id] = exception_in;
                changed = true;
                if (budget != nullptr && !budget->consume_state(exception_in.values.size())) {
                    return;
                }
            }

            auto block_it = analysis.blocks.find(block_id);
            if (block_it == analysis.blocks.end()) {
                continue;
            }
            auto normal_transfer =
                apply_lifetime_block_transfer_with_exception(normal_entry, *block_it->second);
            LifetimeState normal_out = std::move(normal_transfer.normal_out);
            auto normal_out_it = analysis.normal_out_states.find(block_id);
            if (normal_out_it == analysis.normal_out_states.end()
                || normal_out_it->second.values != normal_out.values) {
                analysis.normal_out_states[block_id] = normal_out;
                changed = true;
                if (budget != nullptr && !budget->consume_state(normal_out.values.size())) {
                    return;
                }
            }

            LifetimeState exception_source = merge_lifetime_states(normal_entry, exception_in);
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                exception_source = merge_lifetime_states(exception_source, normal_entry);
            }

            LifetimeState exception_out = exception_source;
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                auto exception_transfer =
                    apply_lifetime_block_transfer_with_exception(exception_source,
                                                                 *block_it->second);
                if (exception_transfer.exception_out.has_value()) {
                    exception_out = std::move(*exception_transfer.exception_out);
                } else {
                    exception_out =
                        merge_lifetime_states(exception_source, exception_transfer.normal_out);
                }
            }
            auto exception_out_it = analysis.exception_out_states.find(block_id);
            if (exception_out_it == analysis.exception_out_states.end()
                || exception_out_it->second.values != exception_out.values) {
                analysis.exception_out_states[block_id] = exception_out;
                changed = true;
                if (budget != nullptr && !budget->consume_state(exception_out.values.size())) {
                    return;
                }
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-size) - Lifetime anchor scan.
[[nodiscard]] std::optional<LifetimeState> state_at_anchor(const FunctionLifetimeAnalysis& analysis,
                                                           const IrAnchor& anchor)
{
    auto block_it = analysis.blocks.find(anchor.block_id);
    if (block_it == analysis.blocks.end()) {
        return std::nullopt;
    }
    LifetimeState state;
    auto pred_it = analysis.predecessors.find(anchor.block_id);
    bool has_normal_preds = false;
    bool has_exception_preds = false;
    if (pred_it != analysis.predecessors.end()) {
        has_normal_preds = !pred_it->second.normal.empty();
        has_exception_preds = !pred_it->second.exception.empty();
    }

    auto normal_it = analysis.normal_in_states.find(anchor.block_id);
    auto exception_it = analysis.exception_in_states.find(anchor.block_id);
    if (auto landingpad_it = analysis.has_landingpad.find(anchor.block_id);
        landingpad_it != analysis.has_landingpad.end() && landingpad_it->second) {
        if (normal_it != analysis.normal_in_states.end()) {
            state = normal_it->second;
        }
    } else if (has_exception_preds && !has_normal_preds) {
        if (exception_it != analysis.exception_in_states.end()) {
            state = exception_it->second;
        }
    } else if (has_normal_preds && has_exception_preds
               && normal_it != analysis.normal_in_states.end()
               && exception_it != analysis.exception_in_states.end()) {
        state = merge_lifetime_states(normal_it->second, exception_it->second);
    } else if (normal_it != analysis.normal_in_states.end()) {
        state = normal_it->second;
    } else if (exception_it != analysis.exception_in_states.end()) {
        state = exception_it->second;
    }
    const nlohmann::json& block = *block_it->second;
    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return std::nullopt;
    }
    for (const auto& inst : block.at("insts")) {
        if (inst.contains("id") && inst.at("id").is_string()
            && inst.at("id").get<std::string>() == anchor.inst_id) {
            return state;
        }
        apply_lifetime_effect(inst, state);
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-size) - Cache assembly reads structured JSON in one pass.
[[nodiscard]] LifetimeAnalysisCache build_lifetime_analysis_cache(const nlohmann::json& nir_json,
                                                                  BudgetTracker* budget)
{
    LifetimeAnalysisCache cache;
    if (budget != nullptr && budget->exceeded()) {
        return cache;
    }
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return cache;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        if (!func.contains("cfg") || !func.at("cfg").is_object()) {
            continue;
        }
        const auto& cfg = func.at("cfg");
        if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
            continue;
        }

        FunctionLifetimeAnalysis analysis;
        analysis.function_uid = func.at("function_uid").get<std::string>();
        if (cfg.contains("entry") && cfg.at("entry").is_string()) {
            analysis.entry_block = cfg.at("entry").get<std::string>();
        }

        for (const auto& block : cfg.at("blocks")) {
            if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()) {
                continue;
            }
            std::string block_id = block.at("id").get<std::string>();
            analysis.block_order.push_back(block_id);
            analysis.blocks.emplace(block_id, &block);
            analysis.normal_in_states.emplace(block_id, LifetimeState{});
            analysis.normal_out_states.emplace(block_id, LifetimeState{});
            analysis.exception_in_states.emplace(block_id, LifetimeState{});
            analysis.exception_out_states.emplace(block_id, LifetimeState{});
            analysis.has_exception_successor.emplace(block_id, false);

            bool block_has_landingpad = false;
            if (block.contains("insts") && block.at("insts").is_array()) {
                for (const auto& inst : block.at("insts")) {
                    if (!inst.is_object() || !inst.contains("op") || !inst.at("op").is_string()) {
                        continue;
                    }
                    if (inst.at("op").get<std::string>() == "landingpad") {
                        block_has_landingpad = true;
                        break;
                    }
                }
            }
            analysis.has_landingpad.emplace(block_id, block_has_landingpad);
        }

        if (cfg.contains("edges") && cfg.at("edges").is_array()) {
            for (const auto& edge : cfg.at("edges")) {
                if (!edge.is_object()) {
                    continue;
                }
                if (!edge.contains("from") || !edge.at("from").is_string()) {
                    continue;
                }
                if (!edge.contains("to") || !edge.at("to").is_string()) {
                    continue;
                }
                std::string from = edge.at("from").get<std::string>();
                std::string to = edge.at("to").get<std::string>();
                std::string kind;
                if (edge.contains("kind") && edge.at("kind").is_string()) {
                    kind = edge.at("kind").get<std::string>();
                }
                auto& preds = analysis.predecessors[to];
                if (kind == "exception") {
                    preds.exception.push_back(from);
                    analysis.has_exception_successor[from] = true;
                } else {
                    preds.normal.push_back(from);
                }
            }
        }

        for (auto& [block_id, preds] : analysis.predecessors) {
            (void)block_id;
            std::ranges::stable_sort(preds.normal);
            auto normal_unique = std::ranges::unique(preds.normal);
            preds.normal.erase(normal_unique.begin(), normal_unique.end());
            std::ranges::stable_sort(preds.exception);
            auto exception_unique = std::ranges::unique(preds.exception);
            preds.exception.erase(exception_unique.begin(), exception_unique.end());
        }

        if (!analysis.block_order.empty()) {
            if (analysis.entry_block.empty()) {
                analysis.entry_block = analysis.block_order.front();
            }
            if (budget != nullptr && !budget->consume_summary_node(analysis.function_uid)) {
                return cache;
            }
            compute_lifetime_fixpoint(analysis, budget);
            if (budget != nullptr && budget->exceeded()) {
                return cache;
            }
            cache.functions.emplace(analysis.function_uid, std::move(analysis));
        }
    }

    return cache;
}

enum class InitValue {
    kInit,
    kUninit,
    kMaybe,
};

struct InitState
{
    std::map<std::string, InitValue> values;

    InitState()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : values()
    {}
};

[[nodiscard]] InitValue merge_init_value(InitValue a, InitValue b)
{
    if (a == b) {
        return a;
    }
    return InitValue::kMaybe;
}

[[nodiscard]] InitState merge_init_states(const InitState& a, const InitState& b)
{
    InitState result;
    for (const auto& [key, value] : a.values) {
        InitValue other = InitValue::kMaybe;
        auto it = b.values.find(key);
        if (it != b.values.end()) {
            other = it->second;
        }
        result.values.emplace(key, merge_init_value(value, other));
    }
    for (const auto& [key, value] : b.values) {
        if (result.values.contains(key)) {
            continue;
        }
        result.values.emplace(key, merge_init_value(InitValue::kMaybe, value));
    }
    return result;
}

// NOLINTNEXTLINE(readability-function-size) - Op-driven init state updates.
void apply_init_effect(const nlohmann::json& inst, InitState& state)
{
    if (!inst.contains("op") || !inst.at("op").is_string()) {
        return;
    }
    const auto& op = inst.at("op").get_ref<const std::string&>();
    if (!inst.contains("args") || !inst.at("args").is_array()) {
        return;
    }
    const auto& args = inst.at("args");
    if (op == "assign") {
        for (const auto& arg : args) {
            auto label = extract_ref_name(arg);
            if (!label.has_value()) {
                continue;
            }
            if (auto has_init = extract_ref_has_init(arg); has_init.has_value()) {
                state.values[*label] = *has_init ? InitValue::kInit : InitValue::kUninit;
            } else {
                state.values[*label] = InitValue::kMaybe;
            }
        }
        return;
    }
    if (op == "store") {
        if (!args.empty()) {
            if (auto label = extract_ref_name(args.at(0)); label.has_value()) {
                state.values[*label] = InitValue::kInit;
            }
        }
        return;
    }
    if (op == "move") {
        if (args.size() >= 2U && args.at(1).is_string()) {
            state.values[args.at(1).get<std::string>()] = InitValue::kMaybe;
        }
        return;
    }
}

struct FunctionInitAnalysis
{
    std::string function_uid;
    std::string entry_block;
    std::map<std::string, const nlohmann::json*> blocks;
    std::vector<std::string> block_order;
    std::map<std::string, FlowPredecessors> predecessors;
    std::map<std::string, bool> has_exception_successor;
    std::map<std::string, InitState> in_states;
    std::map<std::string, InitState> out_states;
    std::map<std::string, InitState> exception_out_states;

    // NOLINTBEGIN(readability-redundant-member-init) - required for -Weffc++.
    FunctionInitAnalysis()
        : function_uid()
        , entry_block()
        , blocks()
        , block_order()
        , predecessors()
        , has_exception_successor()
        , in_states()
        , out_states()
        , exception_out_states()
    {}
    // NOLINTEND(readability-redundant-member-init)
};

struct InitAnalysisCache
{
    std::map<std::string, FunctionInitAnalysis> functions;

    InitAnalysisCache()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : functions()
    {}
};

[[nodiscard]] InitState merge_init_predecessor_states(const FunctionInitAnalysis& analysis,
                                                      std::string_view block_id)
{
    auto pred_it = analysis.predecessors.find(std::string(block_id));
    if (pred_it == analysis.predecessors.end()) {
        return InitState{};
    }

    bool first = true;
    InitState merged;
    for (const auto& pred : pred_it->second.normal) {
        auto out_it = analysis.out_states.find(pred);
        if (out_it == analysis.out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_init_states(merged, out_it->second);
    }

    for (const auto& pred : pred_it->second.exception) {
        auto out_it = analysis.exception_out_states.find(pred);
        if (out_it == analysis.exception_out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_init_states(merged, out_it->second);
    }

    if (first) {
        return InitState{};
    }
    return merged;
}

struct InitTransferResult
{
    InitState normal_out;
    std::optional<InitState> exception_out;
    bool has_exception_op = false;
};

[[nodiscard]] InitTransferResult
apply_init_block_transfer_with_exception(const InitState& in_state, const nlohmann::json& block)
{
    InitState normal_state = in_state;
    std::optional<InitState> exception_state;
    bool has_exception_op = false;

    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return InitTransferResult{.normal_out = normal_state,
                                  .exception_out = std::move(exception_state),
                                  .has_exception_op = has_exception_op};
    }

    for (const auto& inst : block.at("insts")) {
        std::string_view op;
        bool has_op = false;
        if (inst.contains("op") && inst.at("op").is_string()) {
            op = inst.at("op").get_ref<const std::string&>();
            has_op = true;
        }
        const bool is_invoke = has_op && op == "invoke";
        const bool is_throw = has_op && (op == "throw" || op == "resume");
        if (is_invoke || is_throw) {
            apply_init_effect(inst, normal_state);
            has_exception_op = true;
            if (exception_state.has_value()) {
                exception_state = merge_init_states(*exception_state, normal_state);
            } else {
                exception_state = normal_state;
            }
            if (is_throw) {
                return InitTransferResult{.normal_out = normal_state,
                                          .exception_out = std::move(exception_state),
                                          .has_exception_op = has_exception_op};
            }
            continue;
        }
        apply_init_effect(inst, normal_state);
    }

    return InitTransferResult{.normal_out = normal_state,
                              .exception_out = std::move(exception_state),
                              .has_exception_op = has_exception_op};
}

// NOLINTNEXTLINE(readability-function-size) - Fixpoint loop is clearer in one block.
void compute_init_fixpoint(FunctionInitAnalysis& analysis, BudgetTracker* budget)
{
    bool changed = true;
    while (changed) {
        if (budget != nullptr && budget->exceeded()) {
            return;
        }
        changed = false;
        for (const auto& block_id : analysis.block_order) {
            if (budget != nullptr && !budget->consume_iteration()) {
                return;
            }
            InitState in_state = merge_init_predecessor_states(analysis, block_id);
            auto in_it = analysis.in_states.find(block_id);
            if (in_it == analysis.in_states.end() || in_it->second.values != in_state.values) {
                analysis.in_states[block_id] = in_state;
                changed = true;
                if (budget != nullptr && !budget->consume_state(in_state.values.size())) {
                    return;
                }
            }

            auto block_it = analysis.blocks.find(block_id);
            if (block_it == analysis.blocks.end()) {
                continue;
            }
            auto transfer = apply_init_block_transfer_with_exception(in_state, *block_it->second);
            InitState out_state = std::move(transfer.normal_out);
            auto out_it = analysis.out_states.find(block_id);
            if (out_it == analysis.out_states.end() || out_it->second.values != out_state.values) {
                analysis.out_states[block_id] = out_state;
                changed = true;
                if (budget != nullptr && !budget->consume_state(out_state.values.size())) {
                    return;
                }
            }

            InitState exception_out = in_state;
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                if (transfer.exception_out.has_value()) {
                    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded above.
                    exception_out = std::move(*transfer.exception_out);
                } else {
                    exception_out = merge_init_states(in_state, out_state);
                }
            }
            auto exception_out_it = analysis.exception_out_states.find(block_id);
            if (exception_out_it == analysis.exception_out_states.end()
                || exception_out_it->second.values != exception_out.values) {
                analysis.exception_out_states[block_id] = std::move(exception_out);
                changed = true;
                if (budget != nullptr
                    && !budget->consume_state(
                        analysis.exception_out_states.at(block_id).values.size())) {
                    return;
                }
            }
        }
    }
}

[[nodiscard]] std::optional<InitState> init_state_at_anchor(const FunctionInitAnalysis& analysis,
                                                            const IrAnchor& anchor)
{
    auto block_it = analysis.blocks.find(anchor.block_id);
    if (block_it == analysis.blocks.end()) {
        return std::nullopt;
    }
    InitState state;
    auto in_it = analysis.in_states.find(anchor.block_id);
    if (in_it != analysis.in_states.end()) {
        state = in_it->second;
    }
    const nlohmann::json& block = *block_it->second;
    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return std::nullopt;
    }
    for (const auto& inst : block.at("insts")) {
        if (inst.contains("id") && inst.at("id").is_string()
            && inst.at("id").get<std::string>() == anchor.inst_id) {
            return state;
        }
        apply_init_effect(inst, state);
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-size) - Cache assembly reads structured JSON in one pass.
[[nodiscard]] InitAnalysisCache build_init_analysis_cache(const nlohmann::json& nir_json,
                                                          BudgetTracker* budget)
{
    InitAnalysisCache cache;
    if (budget != nullptr && budget->exceeded()) {
        return cache;
    }
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return cache;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        if (!func.contains("cfg") || !func.at("cfg").is_object()) {
            continue;
        }
        const auto& cfg = func.at("cfg");
        if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
            continue;
        }

        FunctionInitAnalysis analysis;
        analysis.function_uid = func.at("function_uid").get<std::string>();
        if (cfg.contains("entry") && cfg.at("entry").is_string()) {
            analysis.entry_block = cfg.at("entry").get<std::string>();
        }

        for (const auto& block : cfg.at("blocks")) {
            if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()) {
                continue;
            }
            std::string block_id = block.at("id").get<std::string>();
            analysis.block_order.push_back(block_id);
            analysis.blocks.emplace(block_id, &block);
            analysis.in_states.emplace(block_id, InitState{});
            analysis.out_states.emplace(block_id, InitState{});
            analysis.exception_out_states.emplace(block_id, InitState{});
            analysis.has_exception_successor.emplace(block_id, false);
        }

        if (cfg.contains("edges") && cfg.at("edges").is_array()) {
            for (const auto& edge : cfg.at("edges")) {
                if (!edge.is_object()) {
                    continue;
                }
                if (!edge.contains("from") || !edge.at("from").is_string()) {
                    continue;
                }
                if (!edge.contains("to") || !edge.at("to").is_string()) {
                    continue;
                }
                std::string from = edge.at("from").get<std::string>();
                std::string to = edge.at("to").get<std::string>();
                std::string kind;
                if (edge.contains("kind") && edge.at("kind").is_string()) {
                    kind = edge.at("kind").get<std::string>();
                }
                auto& preds = analysis.predecessors[to];
                if (kind == "exception") {
                    preds.exception.push_back(from);
                    analysis.has_exception_successor[from] = true;
                } else {
                    preds.normal.push_back(from);
                }
            }
        }

        for (auto& [block_id, preds] : analysis.predecessors) {
            (void)block_id;
            std::ranges::stable_sort(preds.normal);
            auto normal_unique = std::ranges::unique(preds.normal);
            preds.normal.erase(normal_unique.begin(), normal_unique.end());
            std::ranges::stable_sort(preds.exception);
            auto exception_unique = std::ranges::unique(preds.exception);
            preds.exception.erase(exception_unique.begin(), exception_unique.end());
        }

        if (!analysis.block_order.empty()) {
            if (analysis.entry_block.empty()) {
                analysis.entry_block = analysis.block_order.front();
            }
            if (budget != nullptr && !budget->consume_summary_node(analysis.function_uid)) {
                return cache;
            }
            compute_init_fixpoint(analysis, budget);
            if (budget != nullptr && budget->exceeded()) {
                return cache;
            }
            cache.functions.emplace(analysis.function_uid, std::move(analysis));
        }
    }

    return cache;
}

struct PointsToSet
{
    bool is_unknown = false;
    std::vector<std::string> targets;

    PointsToSet()
        // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init,modernize-use-default-member-init)
        : is_unknown(false)  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , targets()  // Weffc++ explicit init.
    {}

    PointsToSet(bool unknown, std::vector<std::string> targets_in)
        : is_unknown(unknown)
        , targets(std::move(targets_in))
    {}

    bool operator==(const PointsToSet&) const = default;
};

struct PointsToState
{
    std::map<std::string, PointsToSet> values;

    PointsToState()
        // NOLINTNEXTLINE(readability-redundant-member-init)
        : values()  // Weffc++ explicit init.
    {}

    bool operator==(const PointsToState&) const = default;
};

struct PointsToEffect
{
    std::string ptr;
    std::vector<std::string> targets;

    PointsToEffect()
        // NOLINTNEXTLINE(readability-redundant-member-init)
        : ptr()  // Weffc++ explicit init.
        // NOLINTNEXTLINE(readability-redundant-member-init)
        , targets()  // Weffc++ explicit init.
    {}
};

[[nodiscard]] PointsToSet make_points_to_set(std::vector<std::string> targets)
{
    std::ranges::stable_sort(targets);
    auto unique_end = std::ranges::unique(targets);
    targets.erase(unique_end.begin(), targets.end());

    if (targets.size() > kMaxPointsToTargets) {
        return {true, {}};
    }

    return {false, std::move(targets)};
}

[[nodiscard]] PointsToSet merge_points_to_sets(const PointsToSet& a, const PointsToSet& b)
{
    if (a.is_unknown || b.is_unknown) {
        return {true, {}};
    }

    std::vector<std::string> merged = a.targets;
    merged.insert(merged.end(), b.targets.begin(), b.targets.end());
    return make_points_to_set(std::move(merged));
}

[[nodiscard]] PointsToState merge_points_to_states(const PointsToState& a, const PointsToState& b)
{
    PointsToState result;

    for (const auto& [ptr, set] : a.values) {
        PointsToSet other{true, {}};
        auto it = b.values.find(ptr);
        if (it != b.values.end()) {
            other = it->second;
        }
        result.values.emplace(ptr, merge_points_to_sets(set, other));
    }

    for (const auto& [ptr, set] : b.values) {
        if (result.values.contains(ptr)) {
            continue;
        }
        PointsToSet other{true, {}};
        auto it = a.values.find(ptr);
        if (it != a.values.end()) {
            other = it->second;
        }
        result.values.emplace(ptr, merge_points_to_sets(other, set));
    }

    return result;
}

[[nodiscard]] sappp::Result<std::vector<PointsToEffect>>
extract_points_to_effects(const nlohmann::json& inst)
{
    if (!inst.contains("effects")) {
        return std::vector<PointsToEffect>{};
    }
    if (!inst.at("effects").is_object()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected effects object in nir instruction"));
    }

    const auto& effects = inst.at("effects");
    if (!effects.contains("points_to")) {
        return std::vector<PointsToEffect>{};
    }
    if (!effects.at("points_to").is_array()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected effects.points_to array in nir"));
    }

    std::vector<PointsToEffect> output;
    for (const auto& entry : effects.at("points_to")) {
        if (!entry.is_object()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "Expected points_to entry object in nir"));
        }
        if (!entry.contains("ptr") || !entry.contains("targets")) {
            return std::unexpected(
                sappp::Error::make("MissingField", "points_to entry missing ptr or targets"));
        }
        if (!entry.at("ptr").is_string() || !entry.at("targets").is_array()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "points_to entry has invalid field types"));
        }
        PointsToEffect effect;
        effect.ptr = entry.at("ptr").get<std::string>();
        for (const auto& target : entry.at("targets")) {
            if (!target.is_string()) {
                return std::unexpected(
                    sappp::Error::make("InvalidFieldType",
                                       "points_to targets must be strings in nir"));
            }
            effect.targets.push_back(target.get<std::string>());
        }
        output.push_back(std::move(effect));
    }

    return output;
}

[[nodiscard]] sappp::VoidResult apply_points_to_effects(const nlohmann::json& inst,
                                                        PointsToState& state)
{
    auto effects = extract_points_to_effects(inst);
    if (!effects) {
        return std::unexpected(effects.error());
    }
    for (const auto& effect : *effects) {
        state.values[effect.ptr] = make_points_to_set(effect.targets);
    }
    return {};
}

struct FunctionPointsToAnalysis
{
    std::string function_uid;
    std::string entry_block;
    std::map<std::string, const nlohmann::json*> blocks;
    std::vector<std::string> block_order;
    std::map<std::string, FlowPredecessors> predecessors;
    std::map<std::string, bool> has_exception_successor;
    std::map<std::string, PointsToState> in_states;
    std::map<std::string, PointsToState> out_states;
    std::map<std::string, PointsToState> exception_out_states;

    // NOLINTBEGIN(readability-redundant-member-init) - required for -Weffc++.
    FunctionPointsToAnalysis()
        : function_uid()
        , entry_block()
        , blocks()
        , block_order()
        , predecessors()
        , has_exception_successor()
        , in_states()
        , out_states()
        , exception_out_states()
    {}
    // NOLINTEND(readability-redundant-member-init)
};

struct PointsToAnalysisCache
{
    std::map<std::string, FunctionPointsToAnalysis> functions;

    PointsToAnalysisCache()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : functions()
    {}
};

[[nodiscard]] PointsToState
merge_predecessor_points_to_states(const FunctionPointsToAnalysis& analysis,
                                   std::string_view block_id)
{
    auto pred_it = analysis.predecessors.find(std::string(block_id));
    if (pred_it == analysis.predecessors.end()) {
        return PointsToState{};
    }

    bool first = true;
    PointsToState merged;
    for (const auto& pred : pred_it->second.normal) {
        auto out_it = analysis.out_states.find(pred);
        if (out_it == analysis.out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_points_to_states(merged, out_it->second);
    }

    for (const auto& pred : pred_it->second.exception) {
        auto out_it = analysis.exception_out_states.find(pred);
        if (out_it == analysis.exception_out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_points_to_states(merged, out_it->second);
    }

    if (first) {
        return PointsToState{};
    }
    return merged;
}

struct PointsToTransferResult
{
    PointsToState normal_out;
    std::optional<PointsToState> exception_out;
    bool has_exception_op = false;
};

[[nodiscard]] sappp::Result<PointsToTransferResult>
apply_points_to_block_transfer_with_exception(const PointsToState& in_state,
                                              const nlohmann::json& block)
{
    PointsToState normal_state = in_state;
    std::optional<PointsToState> exception_state;
    bool has_exception_op = false;

    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return PointsToTransferResult{.normal_out = normal_state,
                                      .exception_out = std::move(exception_state),
                                      .has_exception_op = has_exception_op};
    }

    for (const auto& inst : block.at("insts")) {
        std::string_view op;
        bool has_op = false;
        if (inst.contains("op") && inst.at("op").is_string()) {
            op = inst.at("op").get_ref<const std::string&>();
            has_op = true;
        }
        const bool is_invoke = has_op && op == "invoke";
        const bool is_throw = has_op && (op == "throw" || op == "resume");
        if (is_invoke || is_throw) {
            if (auto applied = apply_points_to_effects(inst, normal_state); !applied) {
                return std::unexpected(applied.error());
            }
            has_exception_op = true;
            if (exception_state.has_value()) {
                exception_state = merge_points_to_states(*exception_state, normal_state);
            } else {
                exception_state = normal_state;
            }
            if (is_throw) {
                return PointsToTransferResult{.normal_out = normal_state,
                                              .exception_out = std::move(exception_state),
                                              .has_exception_op = has_exception_op};
            }
            continue;
        }
        if (auto applied = apply_points_to_effects(inst, normal_state); !applied) {
            return std::unexpected(applied.error());
        }
    }

    return PointsToTransferResult{.normal_out = normal_state,
                                  .exception_out = std::move(exception_state),
                                  .has_exception_op = has_exception_op};
}

// NOLINTNEXTLINE(readability-function-size) - Fixpoint loop is clearer in one block.
[[nodiscard]] sappp::VoidResult compute_points_to_fixpoint(FunctionPointsToAnalysis& analysis,
                                                           BudgetTracker* budget)
{
    bool changed = true;
    while (changed) {
        if (budget != nullptr && budget->exceeded()) {
            return {};
        }
        changed = false;
        for (const auto& block_id : analysis.block_order) {
            if (budget != nullptr && !budget->consume_iteration()) {
                return {};
            }
            PointsToState in_state = merge_predecessor_points_to_states(analysis, block_id);
            auto in_it = analysis.in_states.find(block_id);
            if (in_it == analysis.in_states.end() || in_it->second != in_state) {
                analysis.in_states[block_id] = in_state;
                changed = true;
                if (budget != nullptr && !budget->consume_state(in_state.values.size())) {
                    return {};
                }
            }

            auto block_it = analysis.blocks.find(block_id);
            if (block_it == analysis.blocks.end()) {
                continue;
            }
            auto transfer =
                apply_points_to_block_transfer_with_exception(in_state, *block_it->second);
            if (!transfer) {
                return std::unexpected(transfer.error());
            }
            auto out_it = analysis.out_states.find(block_id);
            if (out_it == analysis.out_states.end() || out_it->second != transfer->normal_out) {
                analysis.out_states[block_id] = transfer->normal_out;
                changed = true;
                if (budget != nullptr
                    && !budget->consume_state(transfer->normal_out.values.size())) {
                    return {};
                }
            }

            PointsToState exception_out = in_state;
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                if (transfer->exception_out.has_value()) {
                    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded above.
                    exception_out = std::move(*transfer->exception_out);
                } else {
                    exception_out = merge_points_to_states(in_state, transfer->normal_out);
                }
            }
            auto exception_out_it = analysis.exception_out_states.find(block_id);
            if (exception_out_it == analysis.exception_out_states.end()
                || exception_out_it->second != exception_out) {
                analysis.exception_out_states[block_id] = std::move(exception_out);
                changed = true;
                if (budget != nullptr
                    && !budget->consume_state(
                        analysis.exception_out_states.at(block_id).values.size())) {
                    return {};
                }
            }
        }
    }
    return {};
}

[[nodiscard]] sappp::Result<std::optional<PointsToState>>
points_to_state_at_anchor(const FunctionPointsToAnalysis& analysis, const IrAnchor& anchor)
{
    auto block_it = analysis.blocks.find(anchor.block_id);
    if (block_it == analysis.blocks.end()) {
        return std::optional<PointsToState>();
    }
    auto in_it = analysis.in_states.find(anchor.block_id);
    PointsToState state;
    if (in_it != analysis.in_states.end()) {
        state = in_it->second;
    }
    const nlohmann::json& block = *block_it->second;
    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return std::optional<PointsToState>();
    }
    for (const auto& inst : block.at("insts")) {
        if (inst.contains("id") && inst.at("id").is_string()
            && inst.at("id").get<std::string>() == anchor.inst_id) {
            return std::optional<PointsToState>(state);
        }
        if (auto applied = apply_points_to_effects(inst, state); !applied) {
            return std::unexpected(applied.error());
        }
    }
    return std::optional<PointsToState>();
}

[[nodiscard]] sappp::Result<PointsToAnalysisCache>
build_points_to_analysis_cache(const nlohmann::json& nir_json,  // NOLINT(readability-function-size)
                                                                // - cache assembly reads JSON.
                               BudgetTracker* budget)
{
    PointsToAnalysisCache cache;
    if (budget != nullptr && budget->exceeded()) {
        return cache;
    }
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return cache;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        if (!func.contains("cfg") || !func.at("cfg").is_object()) {
            continue;
        }
        const auto& cfg = func.at("cfg");
        if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
            continue;
        }

        FunctionPointsToAnalysis analysis;
        analysis.function_uid = func.at("function_uid").get<std::string>();
        if (cfg.contains("entry") && cfg.at("entry").is_string()) {
            analysis.entry_block = cfg.at("entry").get<std::string>();
        }

        for (const auto& block : cfg.at("blocks")) {
            if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()) {
                continue;
            }
            std::string block_id = block.at("id").get<std::string>();
            analysis.block_order.push_back(block_id);
            analysis.blocks.emplace(block_id, &block);
            analysis.in_states.emplace(block_id, PointsToState{});
            analysis.out_states.emplace(block_id, PointsToState{});
            analysis.exception_out_states.emplace(block_id, PointsToState{});
            analysis.has_exception_successor.emplace(block_id, false);
        }

        if (cfg.contains("edges") && cfg.at("edges").is_array()) {
            for (const auto& edge : cfg.at("edges")) {
                if (!edge.is_object()) {
                    continue;
                }
                if (!edge.contains("from") || !edge.at("from").is_string()) {
                    continue;
                }
                if (!edge.contains("to") || !edge.at("to").is_string()) {
                    continue;
                }
                std::string from = edge.at("from").get<std::string>();
                std::string to = edge.at("to").get<std::string>();
                std::string kind;
                if (edge.contains("kind") && edge.at("kind").is_string()) {
                    kind = edge.at("kind").get<std::string>();
                }
                auto& preds = analysis.predecessors[to];
                if (kind == "exception") {
                    preds.exception.push_back(from);
                    analysis.has_exception_successor[from] = true;
                } else {
                    preds.normal.push_back(from);
                }
            }
        }

        for (auto& [block_id, preds] : analysis.predecessors) {
            (void)block_id;
            std::ranges::stable_sort(preds.normal);
            auto normal_unique = std::ranges::unique(preds.normal);
            preds.normal.erase(normal_unique.begin(), normal_unique.end());
            std::ranges::stable_sort(preds.exception);
            auto exception_unique = std::ranges::unique(preds.exception);
            preds.exception.erase(exception_unique.begin(), exception_unique.end());
        }

        if (!analysis.block_order.empty()) {
            if (analysis.entry_block.empty()) {
                analysis.entry_block = analysis.block_order.front();
            }
            if (budget != nullptr && !budget->consume_summary_node(analysis.function_uid)) {
                return cache;
            }
            if (auto fixpoint = compute_points_to_fixpoint(analysis, budget); !fixpoint) {
                return std::unexpected(fixpoint.error());
            }
            if (budget != nullptr && budget->exceeded()) {
                return cache;
            }
            cache.functions.emplace(analysis.function_uid, std::move(analysis));
        }
    }

    return cache;
}

enum class HeapLifetimeValue {
    kUnallocated,
    kAllocated,
    kFreed,
    kMaybe,
};

struct HeapLifetimeState
{
    std::map<std::string, HeapLifetimeValue> values;

    HeapLifetimeState()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : values()
    {}
};

[[nodiscard]] HeapLifetimeValue merge_heap_value(HeapLifetimeValue a, HeapLifetimeValue b)
{
    if (a == b) {
        return a;
    }
    return HeapLifetimeValue::kMaybe;
}

[[nodiscard]] HeapLifetimeState merge_heap_states(const HeapLifetimeState& a,
                                                  const HeapLifetimeState& b)
{
    HeapLifetimeState result;
    for (const auto& [key, value] : a.values) {
        HeapLifetimeValue other = HeapLifetimeValue::kMaybe;
        auto it = b.values.find(key);
        if (it != b.values.end()) {
            other = it->second;
        }
        result.values.emplace(key, merge_heap_value(value, other));
    }
    for (const auto& [key, value] : b.values) {
        if (result.values.contains(key)) {
            continue;
        }
        result.values.emplace(key, merge_heap_value(HeapLifetimeValue::kMaybe, value));
    }
    return result;
}

[[nodiscard]] HeapLifetimeState make_heap_state(const std::vector<std::string>& labels,
                                                HeapLifetimeValue initial)
{
    HeapLifetimeState state;
    for (const auto& label : labels) {
        state.values.emplace(label, initial);
    }
    return state;
}

void apply_heap_lifetime_effect(const nlohmann::json& inst, HeapLifetimeState& state)
{
    if (!inst.contains("op") || !inst.at("op").is_string()) {
        return;
    }
    const auto& op = inst.at("op").get_ref<const std::string&>();
    auto label = extract_first_string_arg(inst);
    if (!label.has_value()) {
        return;
    }
    if (op == "alloc") {
        state.values[*label] = HeapLifetimeValue::kAllocated;
    } else if (op == "free") {
        state.values[*label] = HeapLifetimeValue::kFreed;
    }
}

struct FunctionHeapLifetimeAnalysis
{
    std::string function_uid;
    std::string entry_block;
    std::map<std::string, const nlohmann::json*> blocks;
    std::vector<std::string> block_order;
    std::map<std::string, FlowPredecessors> predecessors;
    std::map<std::string, bool> has_exception_successor;
    std::map<std::string, HeapLifetimeState> in_states;
    std::map<std::string, HeapLifetimeState> out_states;
    std::map<std::string, HeapLifetimeState> exception_out_states;
    HeapLifetimeState initial_state;

    // NOLINTBEGIN(readability-redundant-member-init) - required for -Weffc++.
    FunctionHeapLifetimeAnalysis()
        : function_uid()
        , entry_block()
        , blocks()
        , block_order()
        , predecessors()
        , has_exception_successor()
        , in_states()
        , out_states()
        , exception_out_states()
        , initial_state()
    {}
    // NOLINTEND(readability-redundant-member-init)
};

struct HeapLifetimeAnalysisCache
{
    std::map<std::string, FunctionHeapLifetimeAnalysis> functions;

    HeapLifetimeAnalysisCache()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : functions()
    {}
};

[[nodiscard]] std::vector<std::string> collect_heap_labels(const nlohmann::json& cfg)
{
    std::vector<std::string> labels;
    if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
        return labels;
    }
    for (const auto& block : cfg.at("blocks")) {
        if (!block.is_object() || !block.contains("insts") || !block.at("insts").is_array()) {
            continue;
        }
        for (const auto& inst : block.at("insts")) {
            if (!inst.contains("op") || !inst.at("op").is_string()) {
                continue;
            }
            const auto& op = inst.at("op").get_ref<const std::string&>();
            if (op != "alloc" && op != "free") {
                continue;
            }
            auto label = extract_first_string_arg(inst);
            if (label.has_value()) {
                labels.push_back(*label);
            }
        }
    }
    std::ranges::stable_sort(labels);
    auto unique_end = std::ranges::unique(labels);
    labels.erase(unique_end.begin(), unique_end.end());
    return labels;
}

[[nodiscard]] HeapLifetimeState
merge_heap_predecessor_states(const FunctionHeapLifetimeAnalysis& analysis,
                              std::string_view block_id)
{
    auto pred_it = analysis.predecessors.find(std::string(block_id));
    if (pred_it == analysis.predecessors.end()) {
        return analysis.initial_state;
    }

    bool first = true;
    HeapLifetimeState merged;
    for (const auto& pred : pred_it->second.normal) {
        auto out_it = analysis.out_states.find(pred);
        if (out_it == analysis.out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_heap_states(merged, out_it->second);
    }

    for (const auto& pred : pred_it->second.exception) {
        auto out_it = analysis.exception_out_states.find(pred);
        if (out_it == analysis.exception_out_states.end()) {
            continue;
        }
        if (first) {
            merged = out_it->second;
            first = false;
            continue;
        }
        merged = merge_heap_states(merged, out_it->second);
    }

    if (first) {
        return analysis.initial_state;
    }
    return merged;
}

struct HeapLifetimeTransferResult
{
    HeapLifetimeState normal_out;
    std::optional<HeapLifetimeState> exception_out;
    bool has_exception_op = false;
};

[[nodiscard]] HeapLifetimeTransferResult
apply_heap_block_transfer_with_exception(const HeapLifetimeState& in_state,
                                         const nlohmann::json& block)
{
    HeapLifetimeState normal_state = in_state;
    std::optional<HeapLifetimeState> exception_state;
    bool has_exception_op = false;

    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return HeapLifetimeTransferResult{.normal_out = normal_state,
                                          .exception_out = std::move(exception_state),
                                          .has_exception_op = has_exception_op};
    }

    for (const auto& inst : block.at("insts")) {
        std::string_view op;
        bool has_op = false;
        if (inst.contains("op") && inst.at("op").is_string()) {
            op = inst.at("op").get_ref<const std::string&>();
            has_op = true;
        }
        const bool is_invoke = has_op && op == "invoke";
        const bool is_throw = has_op && (op == "throw" || op == "resume");
        if (is_invoke || is_throw) {
            apply_heap_lifetime_effect(inst, normal_state);
            has_exception_op = true;
            if (exception_state.has_value()) {
                exception_state = merge_heap_states(*exception_state, normal_state);
            } else {
                exception_state = normal_state;
            }
            if (is_throw) {
                return HeapLifetimeTransferResult{.normal_out = normal_state,
                                                  .exception_out = std::move(exception_state),
                                                  .has_exception_op = has_exception_op};
            }
            continue;
        }
        apply_heap_lifetime_effect(inst, normal_state);
    }

    return HeapLifetimeTransferResult{.normal_out = normal_state,
                                      .exception_out = std::move(exception_state),
                                      .has_exception_op = has_exception_op};
}

// NOLINTNEXTLINE(readability-function-size) - Fixpoint loop is clearer in one block.
void compute_heap_lifetime_fixpoint(FunctionHeapLifetimeAnalysis& analysis, BudgetTracker* budget)
{
    bool changed = true;
    while (changed) {
        if (budget != nullptr && budget->exceeded()) {
            return;
        }
        changed = false;
        for (const auto& block_id : analysis.block_order) {
            if (budget != nullptr && !budget->consume_iteration()) {
                return;
            }
            HeapLifetimeState in_state = merge_heap_predecessor_states(analysis, block_id);
            auto in_it = analysis.in_states.find(block_id);
            if (in_it == analysis.in_states.end() || in_it->second.values != in_state.values) {
                analysis.in_states[block_id] = in_state;
                changed = true;
                if (budget != nullptr && !budget->consume_state(in_state.values.size())) {
                    return;
                }
            }

            auto block_it = analysis.blocks.find(block_id);
            if (block_it == analysis.blocks.end()) {
                continue;
            }
            auto transfer = apply_heap_block_transfer_with_exception(in_state, *block_it->second);
            HeapLifetimeState out_state = std::move(transfer.normal_out);
            auto out_it = analysis.out_states.find(block_id);
            if (out_it == analysis.out_states.end() || out_it->second.values != out_state.values) {
                analysis.out_states[block_id] = out_state;
                changed = true;
                if (budget != nullptr && !budget->consume_state(out_state.values.size())) {
                    return;
                }
            }

            HeapLifetimeState exception_out = in_state;
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                if (transfer.exception_out.has_value()) {
                    exception_out = std::move(*transfer.exception_out);
                } else {
                    exception_out = merge_heap_states(in_state, out_state);
                }
            }
            auto exception_out_it = analysis.exception_out_states.find(block_id);
            if (exception_out_it == analysis.exception_out_states.end()
                || exception_out_it->second.values != exception_out.values) {
                analysis.exception_out_states[block_id] = std::move(exception_out);
                changed = true;
            }
        }
    }
}

[[nodiscard]] std::optional<HeapLifetimeState>
heap_state_at_anchor(const FunctionHeapLifetimeAnalysis& analysis, const IrAnchor& anchor)
{
    auto block_it = analysis.blocks.find(anchor.block_id);
    if (block_it == analysis.blocks.end()) {
        return std::nullopt;
    }
    auto in_it = analysis.in_states.find(anchor.block_id);
    HeapLifetimeState state = analysis.initial_state;
    if (in_it != analysis.in_states.end()) {
        state = in_it->second;
    }
    const nlohmann::json& block = *block_it->second;
    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return std::nullopt;
    }
    for (const auto& inst : block.at("insts")) {
        if (inst.contains("id") && inst.at("id").is_string()
            && inst.at("id").get<std::string>() == anchor.inst_id) {
            return state;
        }
        apply_heap_lifetime_effect(inst, state);
    }
    return std::nullopt;
}

[[nodiscard]] HeapLifetimeAnalysisCache
// NOLINTNEXTLINE(readability-function-size) - Cache heap lifetimes.
build_heap_lifetime_analysis_cache(const nlohmann::json& nir_json, BudgetTracker* budget)
{
    HeapLifetimeAnalysisCache cache;
    if (budget != nullptr && budget->exceeded()) {
        return cache;
    }
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return cache;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        if (!func.contains("cfg") || !func.at("cfg").is_object()) {
            continue;
        }
        const auto& cfg = func.at("cfg");
        if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
            continue;
        }

        FunctionHeapLifetimeAnalysis analysis;
        analysis.function_uid = func.at("function_uid").get<std::string>();
        if (cfg.contains("entry") && cfg.at("entry").is_string()) {
            analysis.entry_block = cfg.at("entry").get<std::string>();
        }
        auto labels = collect_heap_labels(cfg);
        analysis.initial_state = make_heap_state(labels, HeapLifetimeValue::kUnallocated);

        for (const auto& block : cfg.at("blocks")) {
            if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()) {
                continue;
            }
            std::string block_id = block.at("id").get<std::string>();
            analysis.block_order.push_back(block_id);
            analysis.blocks.emplace(block_id, &block);
            analysis.in_states.emplace(block_id, analysis.initial_state);
            analysis.out_states.emplace(block_id, analysis.initial_state);
            analysis.exception_out_states.emplace(block_id, analysis.initial_state);
            analysis.has_exception_successor.emplace(block_id, false);
        }

        if (cfg.contains("edges") && cfg.at("edges").is_array()) {
            for (const auto& edge : cfg.at("edges")) {
                if (!edge.is_object()) {
                    continue;
                }
                if (!edge.contains("from") || !edge.at("from").is_string()) {
                    continue;
                }
                if (!edge.contains("to") || !edge.at("to").is_string()) {
                    continue;
                }
                std::string from = edge.at("from").get<std::string>();
                std::string to = edge.at("to").get<std::string>();
                std::string kind;
                if (edge.contains("kind") && edge.at("kind").is_string()) {
                    kind = edge.at("kind").get<std::string>();
                }
                auto& preds = analysis.predecessors[to];
                if (kind == "exception") {
                    preds.exception.push_back(from);
                    analysis.has_exception_successor[from] = true;
                } else {
                    preds.normal.push_back(from);
                }
            }
        }

        for (auto& [block_id, preds] : analysis.predecessors) {
            (void)block_id;
            std::ranges::stable_sort(preds.normal);
            auto normal_unique = std::ranges::unique(preds.normal);
            preds.normal.erase(normal_unique.begin(), normal_unique.end());
            std::ranges::stable_sort(preds.exception);
            auto exception_unique = std::ranges::unique(preds.exception);
            preds.exception.erase(exception_unique.begin(), exception_unique.end());
        }

        if (!analysis.block_order.empty()) {
            if (analysis.entry_block.empty()) {
                analysis.entry_block = analysis.block_order.front();
            }
            if (budget != nullptr && !budget->consume_summary_node(analysis.function_uid)) {
                return cache;
            }
            compute_heap_lifetime_fixpoint(analysis, budget);
            if (budget != nullptr && budget->exceeded()) {
                return cache;
            }
            cache.functions.emplace(analysis.function_uid, std::move(analysis));
        }
    }

    return cache;
}

[[nodiscard]] bool is_exception_op(std::string_view op)
{
    constexpr std::array<std::string_view, 4> kExceptionOps{
        {"invoke", "throw", "landingpad", "resume"}
    };
    return std::ranges::find(kExceptionOps, op) != kExceptionOps.end();
}

[[nodiscard]] bool is_exception_boundary_op(std::string_view op)
{
    constexpr std::array<std::string_view, 3> kExceptionBoundaryOps{
        {"invoke", "throw", "resume"}
    };
    return std::ranges::find(kExceptionBoundaryOps, op) != kExceptionBoundaryOps.end();
}

[[nodiscard]] bool is_thread_op(std::string_view op)
{
    constexpr std::array<std::string_view, 2> kThreadOps{
        {"thread.spawn", "thread.join"}
    };
    return std::ranges::find(kThreadOps, op) != kThreadOps.end();
}

[[nodiscard]] bool is_atomic_op(std::string_view op)
{
    return op == "fence" || op.starts_with("atomic.");
}

void update_feature_flags(std::string_view op, FunctionFeatureFlags& flags)
{
    if (is_exception_op(op)) {
        flags.has_exception_flow = true;
    }
    if (op == "vcall") {
        flags.has_vcall = true;
    }
    if (is_atomic_op(op)) {
        flags.has_atomic = true;
    }
    if (is_thread_op(op)) {
        flags.has_thread = true;
    }
    if (op == "sync.event") {
        flags.has_sync = true;
    }
}

// NOLINTNEXTLINE(readability-function-size) - Collect feature flags.
[[nodiscard]] FunctionFeatureCache build_function_feature_cache(const nlohmann::json& nir_json)
{
    FunctionFeatureCache cache;
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return cache;
    }

    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object()) {
            continue;
        }
        if (!func.contains("function_uid") || !func.at("function_uid").is_string()) {
            continue;
        }
        const std::string function_uid = func.at("function_uid").get<std::string>();
        FunctionFeatureFlags flags;
        std::map<std::string, bool> block_has_exception_boundary;

        if (func.contains("tables") && func.at("tables").is_object()) {
            const auto& tables = func.at("tables");
            if (tables.contains("vcall_candidates") && tables.at("vcall_candidates").is_array()
                && !tables.at("vcall_candidates").empty()) {
                flags.has_vcall = true;
            }
        }

        if (func.contains("cfg") && func.at("cfg").is_object()) {
            const auto& cfg = func.at("cfg");
            if (cfg.contains("blocks") && cfg.at("blocks").is_array()) {
                for (const auto& block : cfg.at("blocks")) {
                    if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()
                        || !block.contains("insts") || !block.at("insts").is_array()) {
                        continue;
                    }
                    const std::string block_id = block.at("id").get<std::string>();
                    bool has_exception_boundary = false;
                    for (const auto& inst : block.at("insts")) {
                        if (!inst.is_object() || !inst.contains("op")
                            || !inst.at("op").is_string()) {
                            continue;
                        }
                        const auto& op = inst.at("op").get_ref<const std::string&>();
                        update_feature_flags(op, flags);
                        if (is_exception_boundary_op(op)) {
                            has_exception_boundary = true;
                        }
                    }
                    block_has_exception_boundary.emplace(block_id, has_exception_boundary);
                }
            }
            if (cfg.contains("edges") && cfg.at("edges").is_array()) {
                for (const auto& edge : cfg.at("edges")) {
                    if (!edge.is_object() || !edge.contains("kind")
                        || !edge.at("kind").is_string()) {
                        continue;
                    }
                    const auto& kind = edge.at("kind").get_ref<const std::string&>();
                    if (kind != "exception") {
                        continue;
                    }
                    flags.has_exception_flow = true;
                    if (edge.contains("from") && edge.at("from").is_string()) {
                        const std::string from = edge.at("from").get<std::string>();
                        auto boundary_it = block_has_exception_boundary.find(from);
                        if (boundary_it == block_has_exception_boundary.end()
                            || !boundary_it->second) {
                            flags.has_unmodeled_exception_flow = true;
                        }
                    } else {
                        flags.has_unmodeled_exception_flow = true;
                    }
                }
            }
        }

        cache.emplace(function_uid, flags);
    }

    return cache;
}

[[nodiscard]] nlohmann::json
make_ir_ref_obj(const std::string& tu_id, const std::string& function_uid, const IrAnchor& anchor)
{
    return nlohmann::json{
        {"schema_version",       "cert.v1"},
        {          "kind",         "IrRef"},
        {         "tu_id",           tu_id},
        {  "function_uid",    function_uid},
        {      "block_id", anchor.block_id},
        {       "inst_id",  anchor.inst_id}
    };
}

struct TraceBlockInst
{
    std::string inst_id;
    std::string op;
};

struct TraceEdge
{
    std::string to;
    std::string kind;
};

struct TracePathNode
{
    std::string block_id;
    std::optional<std::string> edge_kind;
};

[[nodiscard]] const nlohmann::json* find_function_json(const nlohmann::json& nir_json,
                                                       std::string_view function_uid)
{
    if (!nir_json.contains("functions") || !nir_json.at("functions").is_array()) {
        return nullptr;
    }
    for (const auto& func : nir_json.at("functions")) {
        if (!func.is_object() || !func.contains("function_uid")
            || !func.at("function_uid").is_string()) {
            continue;
        }
        if (func.at("function_uid").get<std::string>() == function_uid) {
            return &func;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<std::vector<TracePathNode>>
// NOLINTNEXTLINE(readability-function-size, bugprone-easily-swappable-parameters) - BFS path.
build_block_path(const nlohmann::json& cfg, std::string_view entry_block, std::string_view target)
{
    std::map<std::string, std::vector<TraceEdge>> edges;
    if (cfg.contains("edges") && cfg.at("edges").is_array()) {
        for (const auto& edge : cfg.at("edges")) {
            if (!edge.is_object() || !edge.contains("from") || !edge.contains("to")
                || !edge.contains("kind")) {
                continue;
            }
            if (!edge.at("from").is_string() || !edge.at("to").is_string()
                || !edge.at("kind").is_string()) {
                continue;
            }
            std::string from = edge.at("from").get<std::string>();
            edges[from].push_back(TraceEdge{.to = edge.at("to").get<std::string>(),
                                            .kind = edge.at("kind").get<std::string>()});
        }
    }

    for (auto& [block_id, block_edges] : edges) {
        (void)block_id;
        std::ranges::stable_sort(block_edges, [](const TraceEdge& a, const TraceEdge& b) noexcept {
            if (a.to == b.to) {
                return a.kind < b.kind;
            }
            return a.to < b.to;
        });
    }

    std::deque<std::string> queue;
    std::unordered_map<std::string, bool> visited;
    struct PrevEntry
    {
        std::string from;
        std::string edge_kind;
    };
    std::unordered_map<std::string, PrevEntry> prev;

    queue.emplace_back(entry_block);
    visited.emplace(std::string(entry_block), true);

    while (!queue.empty()) {
        std::string current = std::move(queue.front());
        queue.pop_front();
        if (current == target) {
            break;
        }
        auto edge_it = edges.find(current);
        if (edge_it == edges.end()) {
            continue;
        }
        for (const auto& edge : edge_it->second) {
            if (visited.contains(edge.to)) {
                continue;
            }
            visited.emplace(edge.to, true);
            prev.emplace(edge.to, PrevEntry{.from = current, .edge_kind = edge.kind});
            queue.emplace_back(edge.to);
        }
    }

    if (!visited.contains(std::string(target))) {
        return std::nullopt;
    }

    std::vector<TracePathNode> reversed;
    std::string current = std::string(target);
    while (current != entry_block) {
        auto prev_it = prev.find(current);
        if (prev_it == prev.end()) {
            return std::nullopt;
        }
        reversed.push_back(
            TracePathNode{.block_id = current, .edge_kind = prev_it->second.edge_kind});
        current = prev_it->second.from;
    }
    reversed.push_back(
        TracePathNode{.block_id = std::string(entry_block), .edge_kind = std::nullopt});
    std::vector<TracePathNode> path;
    path.reserve(reversed.size());
    for (const auto& node : std::views::reverse(reversed)) {
        path.push_back(node);
    }
    return path;
}

[[nodiscard]] std::optional<std::string> select_trace_inst(const std::vector<TraceBlockInst>& insts,
                                                           std::string_view anchor_inst_id,
                                                           bool is_anchor_block)
{
    if (is_anchor_block) {
        return std::string(anchor_inst_id);
    }
    for (const auto& inst : insts) {
        if (inst.op == "dtor") {
            return inst.inst_id;
        }
    }
    if (!insts.empty()) {
        return insts.front().inst_id;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<nlohmann::json>>
// NOLINTNEXTLINE(readability-function-size) - Trace steps.
build_bug_trace_steps(const nlohmann::json& nir_json,
                      std::string_view tu_id,
                      std::string_view function_uid,
                      const IrAnchor& anchor)
{
    const nlohmann::json* function_json = find_function_json(nir_json, function_uid);
    if (function_json == nullptr || !function_json->contains("cfg")) {
        return std::nullopt;
    }
    const auto& cfg = function_json->at("cfg");
    if (!cfg.contains("entry") || !cfg.at("entry").is_string()) {
        return std::nullopt;
    }
    std::string entry_block = cfg.at("entry").get<std::string>();
    if (!cfg.contains("blocks") || !cfg.at("blocks").is_array()) {
        return std::nullopt;
    }

    std::map<std::string, std::vector<TraceBlockInst>> block_insts;
    for (const auto& block : cfg.at("blocks")) {
        if (!block.is_object() || !block.contains("id") || !block.at("id").is_string()
            || !block.contains("insts") || !block.at("insts").is_array()) {
            continue;
        }
        std::string block_id = block.at("id").get<std::string>();
        std::vector<TraceBlockInst> insts;
        for (const auto& inst : block.at("insts")) {
            if (!inst.is_object() || !inst.contains("id") || !inst.at("id").is_string()
                || !inst.contains("op") || !inst.at("op").is_string()) {
                continue;
            }
            insts.push_back(TraceBlockInst{.inst_id = inst.at("id").get<std::string>(),
                                           .op = inst.at("op").get<std::string>()});
        }
        block_insts.emplace(std::move(block_id), std::move(insts));
    }

    auto anchor_block_it = block_insts.find(anchor.block_id);
    if (anchor_block_it == block_insts.end()) {
        return std::nullopt;
    }
    bool anchor_inst_found = false;
    for (const auto& inst : anchor_block_it->second) {
        if (inst.inst_id == anchor.inst_id) {
            anchor_inst_found = true;
            break;
        }
    }
    if (!anchor_inst_found) {
        return std::nullopt;
    }

    auto path = build_block_path(cfg, entry_block, anchor.block_id);
    if (!path) {
        return std::nullopt;
    }

    std::vector<nlohmann::json> steps;
    steps.reserve(path->size());
    for (const auto& node : *path) {
        auto inst_it = block_insts.find(node.block_id);
        if (inst_it == block_insts.end()) {
            return std::nullopt;
        }
        bool is_anchor_block = node.block_id == anchor.block_id;
        auto inst_id = select_trace_inst(inst_it->second, anchor.inst_id, is_anchor_block);
        if (!inst_id) {
            return std::nullopt;
        }
        IrAnchor inst_anchor{.block_id = node.block_id, .inst_id = *inst_id};
        nlohmann::json step = nlohmann::json{
            {"ir", make_ir_ref_obj(std::string(tu_id), std::string(function_uid), inst_anchor)}
        };
        if (node.edge_kind) {
            step["edge_kind"] = *node.edge_kind;
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

[[nodiscard]] nlohmann::json make_bug_trace(const std::string& po_id,
                                            const std::vector<nlohmann::json>& steps)
{
    return nlohmann::json{
        {"schema_version",                                                    "cert.v1"},
        {          "kind",                                                   "BugTrace"},
        {    "trace_kind",                                                 "ir_path.v1"},
        {         "steps",                                                        steps},
        {     "violation", nlohmann::json{{"po_id", po_id}, {"predicate_holds", false}}}
    };
}

[[nodiscard]] nlohmann::json make_safety_proof(const std::string& function_uid,
                                               const IrAnchor& anchor,
                                               const nlohmann::json& predicate_expr,
                                               bool predicate_holds,
                                               const std::optional<nlohmann::json>& points_to,
                                               std::string_view domain)
{
    nlohmann::json state = nlohmann::json::object();
    if (predicate_holds) {
        state["predicates"] = nlohmann::json::array({predicate_expr});
    } else {
        state["predicates"] = nlohmann::json::array();
    }
    if (points_to) {
        state["points_to"] = *points_to;
    }

    nlohmann::json point = nlohmann::json{
        {   "ir",
         {{"function_uid", function_uid},
         {"block_id", anchor.block_id},
         {"inst_id", anchor.inst_id}} },
        {"state",                state}
    };

    return nlohmann::json{
        {"schema_version",                      "cert.v1"},
        {          "kind",                  "SafetyProof"},
        {        "domain",            std::string(domain)},
        {        "points", nlohmann::json::array({point})},
        {        "pretty",                   "stub proof"}
    };
}

[[nodiscard]] nlohmann::json make_contract_ref(const ContractInfo& contract)
{
    nlohmann::json contract_ref = {
        {"schema_version",                                    "cert.v1"},
        {          "kind",                                "ContractRef"},
        {   "contract_id",                         contract.contract_id},
        {          "tier",                                contract.tier},
        {        "target", nlohmann::json{{"usr", contract.target_usr}}}
    };

    if (!contract.version_scope.empty()) {
        contract_ref["version_scope"] = contract.version_scope;
    }

    return contract_ref;
}

// NOLINTNEXTLINE(readability-function-size) - Proof root assembly lists explicit inputs.
[[nodiscard]] nlohmann::json make_proof_root(const std::string& po_hash,
                                             const std::string& ir_hash,
                                             const std::string& evidence_hash,
                                             const std::optional<std::string>& depgraph_hash,
                                             const std::vector<std::string>& contract_hashes,
                                             const std::string& result_kind,
                                             const sappp::VersionTriple& versions)
{
    nlohmann::json depends = {
        {   "semantics_version",    versions.semantics},
        {"proof_system_version", versions.proof_system},
        {     "profile_version",      versions.profile}
    };
    if (!contract_hashes.empty()) {
        nlohmann::json contracts = nlohmann::json::array();
        for (const auto& contract_hash : contract_hashes) {
            contracts.push_back(nlohmann::json{
                {"ref", contract_hash}
            });
        }
        depends["contracts"] = std::move(contracts);
    }
    if (depgraph_hash) {
        depends["assumptions"] =
            nlohmann::json::array({std::string("depgraph_ref=") + *depgraph_hash});
    }
    return nlohmann::json{
        {"schema_version",                              "cert.v1"},
        {          "kind",                            "ProofRoot"},
        {            "po",       nlohmann::json{{"ref", po_hash}}},
        {            "ir",       nlohmann::json{{"ref", ir_hash}}},
        {        "result",                            result_kind},
        {      "evidence", nlohmann::json{{"ref", evidence_hash}}},
        {       "depends",                     std::move(depends)},
        {    "hash_scope",                        "hash_scope.v1"}
    };
}

[[nodiscard]] nlohmann::json make_dependency_graph(const std::string& po_hash,
                                                   const std::string& ir_hash,
                                                   const std::string& evidence_hash,
                                                   const std::vector<std::string>& contract_hashes)
{
    std::vector<std::string> nodes = {po_hash, ir_hash, evidence_hash};
    for (const auto& contract_hash : contract_hashes) {
        nodes.push_back(contract_hash);
    }
    std::ranges::sort(nodes);
    auto unique_nodes = std::ranges::unique(nodes);
    nodes.erase(unique_nodes.begin(), unique_nodes.end());

    std::vector<nlohmann::json> edges;
    edges.push_back(nlohmann::json{
        {"from",  po_hash},
        {  "to",  ir_hash},
        {"role", "anchor"}
    });
    edges.push_back(nlohmann::json{
        {"from",       po_hash},
        {  "to", evidence_hash},
        {"role",    "evidence"}
    });
    for (const auto& contract_hash : contract_hashes) {
        edges.push_back(nlohmann::json{
            {"from",       po_hash},
            {  "to", contract_hash},
            {"role",    "contract"}
        });
    }

    std::ranges::stable_sort(edges, [](const nlohmann::json& a, const nlohmann::json& b) {
        const auto& a_from = a.at("from").get_ref<const std::string&>();
        const auto& b_from = b.at("from").get_ref<const std::string&>();
        if (a_from != b_from) {
            return a_from < b_from;
        }
        const auto& a_to = a.at("to").get_ref<const std::string&>();
        const auto& b_to = b.at("to").get_ref<const std::string&>();
        if (a_to != b_to) {
            return a_to < b_to;
        }
        return a.at("role").get<std::string>() < b.at("role").get<std::string>();
    });

    return nlohmann::json{
        {"schema_version",         "cert.v1"},
        {          "kind", "DependencyGraph"},
        {         "nodes",  std::move(nodes)},
        {         "edges",  std::move(edges)}
    };
}

struct UnknownEntryInput
{
    std::string_view po_id;
    std::string_view predicate_pretty;
    const nlohmann::json* predicate_expr = nullptr;
    std::string_view function_hint;
    std::string_view po_kind;
    std::string_view unknown_code;
    std::string_view missing_notes;
    std::string_view refinement_message;
    std::string_view refinement_action;
    std::string_view refinement_domain;
    const std::vector<std::string>* contract_ids = nullptr;
};

[[nodiscard]] nlohmann::json make_unknown_entry(const UnknownEntryInput& input)
{
    nlohmann::json missing_lemma = {
        {   "expr",                                     *input.predicate_expr},
        { "pretty",                       std::string(input.predicate_pretty)},
        {"symbols", nlohmann::json::array({std::string(input.function_hint)})}
    };
    if (!input.missing_notes.empty()) {
        missing_lemma["notes"] = std::string(input.missing_notes);
    }

    nlohmann::json refinement_plan = {
        {"message",          std::string(input.refinement_message)},
        {"actions",
         nlohmann::json::array(
         {nlohmann::json{{"action", std::string(input.refinement_action)},
         {"params",
         nlohmann::json{{"po_id", input.po_id},
         {"po_kind", input.po_kind},
         {"domain", input.refinement_domain}}}}})                 }
    };

    nlohmann::json entry = {
        {"unknown_stable_id", sappp::common::sha256_prefixed(input.po_id)},
        {            "po_id",                    std::string(input.po_id)},
        {     "unknown_code",             std::string(input.unknown_code)},
        {    "missing_lemma",                               missing_lemma},
        {  "refinement_plan",                             refinement_plan}
    };

    if (input.contract_ids != nullptr && !input.contract_ids->empty()) {
        entry["depends_on"] = nlohmann::json{
            {"contracts", *input.contract_ids}
        };
    }

    return entry;
}

struct UnknownDetails
{
    std::string code;
    std::string missing_notes;
    std::string refinement_message;
    std::string refinement_action;
    std::string refinement_domain;
};

[[nodiscard]] std::string join_strings(const std::vector<std::string>& items,
                                       std::string_view separator)
{
    std::string joined;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            joined += separator;
        }
        joined += items[i];
    }
    return joined;
}

[[nodiscard]] UnknownDetails make_unknown_details(std::string_view code,
                                                  std::string_view missing_notes,
                                                  std::string_view refinement_message,
                                                  std::string_view refinement_action,
                                                  std::string_view refinement_domain)
{
    return UnknownDetails{.code = std::string(code),
                          .missing_notes = std::string(missing_notes),
                          .refinement_message = std::string(refinement_message),
                          .refinement_action = std::string(refinement_action),
                          .refinement_domain = std::string(refinement_domain)};
}

[[nodiscard]] UnknownDetails build_missing_contract_details(std::string_view clause)
{
    std::string code = std::string("MissingContract.") + std::string(clause);
    std::string notes =
        std::string("Missing contract ") + std::string(clause) + " clause for this function.";
    std::string message =
        std::string("Provide contract ") + std::string(clause) + " clause to discharge this PO.";
    return UnknownDetails{.code = std::move(code),
                          .missing_notes = std::move(notes),
                          .refinement_message = std::move(message),
                          .refinement_action = "add-contract",
                          .refinement_domain = "contract"};
}

[[nodiscard]] UnknownDetails build_exception_flow_unknown_details()
{
    return make_unknown_details("ExceptionFlowConservative",
                                "Exception flow detected; analysis does not model exceptions.",
                                "Model exception flow to discharge this PO.",
                                "refine-exception",
                                "exception");
}

[[nodiscard]] UnknownDetails build_virtual_dispatch_unknown_details()
{
    return make_unknown_details("VirtualDispatchUnknown",
                                "Virtual call requires dispatch resolution.",
                                "Resolve virtual dispatch targets for this PO.",
                                "resolve-vcall",
                                "dispatch");
}

[[nodiscard]] UnknownDetails build_atomic_order_unknown_details()
{
    return make_unknown_details("AtomicOrderUnknown",
                                "Atomic ordering is not modeled.",
                                "Model atomic order and happens-before relations.",
                                "refine-atomic-order",
                                "concurrency");
}

[[nodiscard]] UnknownDetails build_sync_contract_missing_unknown_details()
{
    return make_unknown_details("SyncContractMissing",
                                "Synchronization event lacks a concurrency contract.",
                                "Add concurrency contract for the synchronization primitive.",
                                "add-contract",
                                "concurrency");
}

[[nodiscard]] UnknownDetails build_concurrency_unsupported_unknown_details()
{
    return make_unknown_details("ConcurrencyUnsupported",
                                "Concurrency events detected; analysis is not implemented.",
                                "Implement concurrency analysis for this PO.",
                                "refine-concurrency",
                                "concurrency");
}

[[nodiscard]] UnknownDetails build_budget_exceeded_unknown_details(std::string_view limit)
{
    std::string notes = "Analysis budget exceeded";
    if (!limit.empty()) {
        notes += " (" + std::string(limit) + ")";
    }
    return UnknownDetails{.code = "BudgetExceeded",
                          .missing_notes = std::move(notes),
                          .refinement_message =
                              "Increase analysis budget or narrow analysis scope.",
                          .refinement_action = "increase-budget",
                          .refinement_domain = "analysis-budget"};
}

[[nodiscard]] UnknownDetails
build_vcall_missing_candidates_details(const std::vector<std::string>& candidate_ids)
{
    std::string notes = "Virtual call candidate set is missing in NIR.";
    if (!candidate_ids.empty()) {
        notes = "Virtual call candidate set missing: " + join_strings(candidate_ids, ", ");
    }
    return UnknownDetails{.code = "VirtualDispatchUnknown",
                          .missing_notes = std::move(notes),
                          .refinement_message = "Resolve virtual dispatch targets for this PO.",
                          .refinement_action = "resolve-vcall",
                          .refinement_domain = "dispatch"};
}

[[nodiscard]] UnknownDetails build_vcall_empty_candidates_details()
{
    return UnknownDetails{.code = "VirtualDispatchUnknown",
                          .missing_notes = "Virtual call candidate set has no methods.",
                          .refinement_message = "Resolve virtual dispatch targets for this PO.",
                          .refinement_action = "resolve-vcall",
                          .refinement_domain = "dispatch"};
}

[[nodiscard]] UnknownDetails
build_vcall_missing_contract_details(const std::vector<std::string>& missing_methods)
{
    std::string notes = "Missing contract precondition for vcall candidates.";
    if (!missing_methods.empty()) {
        notes += " Candidates: " + join_strings(missing_methods, ", ");
    }
    return UnknownDetails{.code = "VirtualCall.MissingContract.Pre",
                          .missing_notes = std::move(notes),
                          .refinement_message =
                              "Provide preconditions for vcall candidate methods.",
                          .refinement_action = "add-contract",
                          .refinement_domain = "contract"};
}

[[nodiscard]] bool is_kind_in(std::string_view kind, const std::array<std::string_view, 3>& set)
{
    return std::ranges::find(set, kind) != set.end();
}

[[nodiscard]] bool is_kind_in(std::string_view kind, const std::array<std::string_view, 1>& set)
{
    return std::ranges::find(set, kind) != set.end();
}

[[nodiscard]] bool is_kind_in(std::string_view kind, const std::array<std::string_view, 2>& set)
{
    return std::ranges::find(set, kind) != set.end();
}

[[nodiscard]] UnknownDetails build_unknown_details(std::string_view po_kind)
{
    constexpr std::array<std::string_view, 3> kLifetimeKinds{
        {
         "UseAfterLifetime", "DoubleFree",
         "InvalidFree", }
    };
    constexpr std::array<std::string_view, 1> kInitKinds{{"UninitRead"}};
    constexpr std::array<std::string_view, 2> kPointsToKinds{
        {
         "UB.NullDeref", "UB.OutOfBounds",
         }
    };

    if (is_kind_in(po_kind, kLifetimeKinds)) {
        return UnknownDetails{.code = "LifetimeUnmodeled",
                              .missing_notes = "Lifetime state is not modeled yet.",
                              .refinement_message =
                                  "Model lifetime states to prove or refute this PO.",
                              .refinement_action = "refine-lifetime",
                              .refinement_domain = "lifetime"};
    }
    if (is_kind_in(po_kind, kInitKinds)) {
        return UnknownDetails{.code = "DomainTooWeak.Memory",
                              .missing_notes = "Initialization state is unknown at this access.",
                              .refinement_message =
                                  "Track initialization states to discharge this PO.",
                              .refinement_action = "refine-init",
                              .refinement_domain = "init"};
    }
    if (is_kind_in(po_kind, kPointsToKinds)) {
        return UnknownDetails{.code = "PointsToUnknown",
                              .missing_notes = "Points-to set is unknown or too wide.",
                              .refinement_message = "Refine points-to analysis for this access.",
                              .refinement_action = "refine-points-to",
                              .refinement_domain = "points-to"};
    }
    if (po_kind.starts_with("UB.")) {
        return UnknownDetails{.code = "DomainTooWeak.Numeric",
                              .missing_notes = "Numeric domain is too weak to decide.",
                              .refinement_message =
                                  "Strengthen numeric reasoning for this UB check.",
                              .refinement_action = "refine-numeric",
                              .refinement_domain = "interval"};
    }
    return UnknownDetails{.code = "UnsupportedFeature",
                          .missing_notes = "Unsupported PO kind in analyzer.",
                          .refinement_message = "Extend analyzer support for this PO kind.",
                          .refinement_action = "extend-analyzer",
                          .refinement_domain = "unknown"};
}

[[nodiscard]] bool vcall_dispatch_resolved(const VCallSummary* summary)
{
    if (summary == nullptr) {
        return false;
    }
    if (!summary->has_vcall) {
        return false;
    }
    if (summary->missing_candidate_set || summary->empty_candidate_set) {
        return false;
    }
    if (!summary->missing_contract_targets.empty()) {
        return false;
    }
    return !summary->candidate_methods.empty();
}

[[nodiscard]] std::optional<UnknownDetails>
build_feature_unknown_details(const FunctionFeatureFlags& features,
                              const ContractMatchSummary& contract_match,
                              const VCallSummary* vcall_summary)
{
    if (features.has_sync && !contract_match.has_concurrency) {
        return build_sync_contract_missing_unknown_details();
    }
    if (features.has_atomic) {
        return build_atomic_order_unknown_details();
    }
    if (features.has_thread || features.has_sync) {
        return build_concurrency_unsupported_unknown_details();
    }
    if (features.has_unmodeled_exception_flow) {
        return build_exception_flow_unknown_details();
    }
    if (features.has_vcall) {
        if (vcall_dispatch_resolved(vcall_summary)) {
            return std::nullopt;
        }
        return build_virtual_dispatch_unknown_details();
    }
    return std::nullopt;
}

[[nodiscard]] bool allow_feature_override(std::string_view unknown_code)
{
    return !unknown_code.starts_with("Lifetime") && unknown_code != "BudgetExceeded"
           && !unknown_code.starts_with("MissingContract.")
           && !unknown_code.starts_with("VirtualCall.");
}

[[nodiscard]] UnknownDetails build_use_after_lifetime_unknown_details(std::string_view notes)
{
    return UnknownDetails{.code = "LifetimeStateUnknown",
                          .missing_notes = std::string(notes),
                          .refinement_message =
                              "Provide lifetime target context or refine lifetime tracking.",
                          .refinement_action = "refine-lifetime",
                          .refinement_domain = "lifetime"};
}

[[nodiscard]] UnknownDetails build_lifetime_unmodeled_details(std::string_view notes)
{
    return UnknownDetails{.code = "LifetimeUnmodeled",
                          .missing_notes = std::string(notes),
                          .refinement_message = "Model lifetime events to prove or refute this PO.",
                          .refinement_action = "refine-lifetime",
                          .refinement_domain = "lifetime"};
}

[[nodiscard]] UnknownDetails build_heap_lifetime_unknown_details(std::string_view notes)
{
    return UnknownDetails{.code = "LifetimeStateUnknown",
                          .missing_notes = std::string(notes),
                          .refinement_message =
                              "Provide heap lifetime target context or refine heap tracking.",
                          .refinement_action = "refine-lifetime",
                          .refinement_domain = "lifetime"};
}

[[nodiscard]] UnknownDetails build_heap_lifetime_unmodeled_details(std::string_view notes)
{
    return UnknownDetails{.code = "LifetimeUnmodeled",
                          .missing_notes = std::string(notes),
                          .refinement_message =
                              "Model heap lifetime events to prove or refute this PO.",
                          .refinement_action = "refine-lifetime",
                          .refinement_domain = "lifetime"};
}

[[nodiscard]] UnknownDetails build_init_unknown_details(std::string_view notes)
{
    return UnknownDetails{.code = "DomainTooWeak.Memory",
                          .missing_notes = std::string(notes),
                          .refinement_message = "Track initialization states to discharge this PO.",
                          .refinement_action = "refine-init",
                          .refinement_domain = "init"};
}

[[nodiscard]] sappp::Result<nlohmann::json>
build_unknown_entry(const nlohmann::json& po,
                    std::string_view po_id,
                    const UnknownDetails& details,
                    const std::vector<std::string>& contracts)
{
    auto predicate_expr = extract_predicate_expr(po);
    if (!predicate_expr) {
        return std::unexpected(predicate_expr.error());
    }
    auto predicate_pretty = extract_predicate_pretty(po);
    if (!predicate_pretty) {
        return std::unexpected(predicate_pretty.error());
    }
    auto function_obj =
        require_object(JsonFieldContext{.obj = &po, .key = "function", .context = "po"});
    if (!function_obj) {
        return std::unexpected(function_obj.error());
    }
    auto function_hint = require_string(
        JsonFieldContext{.obj = &(*function_obj), .key = "mangled", .context = "po.function"});
    if (!function_hint) {
        return std::unexpected(function_hint.error());
    }
    auto po_kind = require_string(JsonFieldContext{.obj = &po, .key = "po_kind", .context = "po"});
    if (!po_kind) {
        return std::unexpected(po_kind.error());
    }

    UnknownEntryInput input = {
        .po_id = po_id,
        .predicate_pretty = *predicate_pretty,
        .predicate_expr = &(*predicate_expr),
        .function_hint = *function_hint,
        .po_kind = *po_kind,
        .unknown_code = details.code,
        .missing_notes = details.missing_notes,
        .refinement_message = details.refinement_message,
        .refinement_action = details.refinement_action,
        .refinement_domain = details.refinement_domain,
        .contract_ids = &contracts,
    };
    return make_unknown_entry(input);
}

[[nodiscard]] sappp::Result<std::vector<const nlohmann::json*>>
collect_ordered_pos(const nlohmann::json& po_list)
{
    auto pos_array =
        require_array(JsonFieldContext{.obj = &po_list, .key = "pos", .context = "po_list"});
    if (!pos_array) {
        return std::unexpected(pos_array.error());
    }

    std::vector<const nlohmann::json*> result;
    result.reserve((*pos_array)->size());
    for (const auto& po : **pos_array) {
        if (!po.is_object()) {
            return std::unexpected(
                sappp::Error::make("InvalidFieldType", "Expected PO entry to be an object"));
        }
        result.push_back(&po);
    }

    std::ranges::stable_sort(result, [](const nlohmann::json* a, const nlohmann::json* b) {
        return a->at("po_id").get<std::string>() < b->at("po_id").get<std::string>();
    });

    return result;
}

[[nodiscard]] nlohmann::json build_unknown_ledger_base(const nlohmann::json& nir_json,
                                                       const nlohmann::json& po_list_json,
                                                       const sappp::VersionTriple& versions,
                                                       const nlohmann::json& tool_obj,
                                                       std::string_view tu_id)
{
    std::string generated_at = std::string(kDeterministicGeneratedAt);
    if (nir_json.contains("generated_at") && nir_json.at("generated_at").is_string()) {
        generated_at = nir_json.at("generated_at").get<std::string>();
    } else if (po_list_json.contains("generated_at")
               && po_list_json.at("generated_at").is_string()) {
        generated_at = po_list_json.at("generated_at").get<std::string>();
    }

    nlohmann::json unknown_ledger = {
        {      "schema_version",            "unknown.v1"},
        {                "tool",                tool_obj},
        {        "generated_at",            generated_at},
        {               "tu_id",      std::string(tu_id)},
        {            "unknowns", nlohmann::json::array()},
        {   "semantics_version",      versions.semantics},
        {"proof_system_version",   versions.proof_system},
        {     "profile_version",        versions.profile}
    };

    if (nir_json.contains("input_digest")) {
        unknown_ledger["input_digest"] = nir_json.at("input_digest");
    } else if (po_list_json.contains("input_digest")) {
        unknown_ledger["input_digest"] = po_list_json.at("input_digest");
    }

    return unknown_ledger;
}

struct PoProcessingContext
{
    sappp::certstore::CertStore* cert_store = nullptr;
    const std::unordered_map<std::string, std::string>* function_uid_map = nullptr;
    const FunctionFeatureCache* feature_cache = nullptr;
    const ContractIndex* contract_index = nullptr;
    const ContractMatchContext* match_context = nullptr;
    const VCallSummaryMap* vcall_summaries = nullptr;
    std::unordered_map<std::string, std::string>* contract_ref_cache = nullptr;
    const LifetimeAnalysisCache* lifetime_cache = nullptr;
    const HeapLifetimeAnalysisCache* heap_lifetime_cache = nullptr;
    const InitAnalysisCache* init_cache = nullptr;
    const nlohmann::json* nir_json = nullptr;
    const PointsToAnalysisCache* points_to_cache = nullptr;
    std::string_view tu_id;
    std::optional<std::string> budget_exceeded_limit;
    std::string points_to_domain;
    const sappp::VersionTriple* versions = nullptr;
};

struct PoProcessingOutput
{
    std::string po_id;
    bool has_unknown = false;
    nlohmann::json unknown_entry = nlohmann::json::object();
};

struct EvidenceResult
{
    nlohmann::json evidence;
    std::string result_kind;
};

struct EvidenceInput
{
    const nlohmann::json* po = nullptr;
    const nlohmann::json* ir_ref = nullptr;
    const nlohmann::json* nir_json = nullptr;
    std::string_view po_id;
    std::string_view function_uid;
    const IrAnchor* anchor = nullptr;
    bool is_bug = false;
    bool is_safe = false;
    const std::optional<nlohmann::json>* points_to = nullptr;
    std::string_view safety_domain = kBaseSafetyDomain;
};

[[nodiscard]] sappp::Result<EvidenceResult> build_evidence(const EvidenceInput& input)
{
    if (input.is_bug) {
        std::vector<nlohmann::json> steps;
        if (input.nir_json != nullptr && input.anchor != nullptr) {
            auto trace_steps = build_bug_trace_steps(*input.nir_json,
                                                     input.ir_ref->at("tu_id").get<std::string>(),
                                                     input.function_uid,
                                                     *input.anchor);
            if (trace_steps) {
                steps = std::move(*trace_steps);
            }
        }
        if (steps.empty()) {
            steps.push_back(nlohmann::json{
                {"ir", *input.ir_ref}
            });
        }
        return EvidenceResult{.evidence = make_bug_trace(std::string(input.po_id), steps),
                              .result_kind = "BUG"};
    }

    auto predicate_expr = extract_predicate_expr(*input.po);
    if (!predicate_expr) {
        return std::unexpected(predicate_expr.error());
    }

    EvidenceResult output{.evidence = make_safety_proof(std::string(input.function_uid),
                                                        *input.anchor,
                                                        *predicate_expr,
                                                        input.is_safe,
                                                        input.points_to != nullptr
                                                            ? *input.points_to
                                                            : std::optional<nlohmann::json>(),
                                                        input.safety_domain),
                          .result_kind = "SAFE"};
    return output;
}

[[nodiscard]] sappp::Result<std::string> put_cert(sappp::certstore::CertStore& cert_store,
                                                  const nlohmann::json& cert)
{
    auto cert_hash = cert_store.put(cert);
    if (!cert_hash) {
        return std::unexpected(cert_hash.error());
    }
    return *cert_hash;
}

[[nodiscard]] sappp::Result<std::string> ensure_contract_ref(const ContractInfo& contract,
                                                             PoProcessingContext& context)
{
    if (context.contract_ref_cache == nullptr) {
        return std::unexpected(
            sappp::Error::make("MissingContext", "Contract reference cache unavailable"));
    }
    auto it = context.contract_ref_cache->find(contract.contract_id);
    if (it != context.contract_ref_cache->end()) {
        return it->second;
    }
    nlohmann::json contract_ref = make_contract_ref(contract);
    auto contract_hash = put_cert(*context.cert_store, contract_ref);
    if (!contract_hash) {
        return std::unexpected(contract_hash.error());
    }
    context.contract_ref_cache->emplace(contract.contract_id, *contract_hash);
    return *contract_hash;
}

struct PoBaseData
{
    std::string po_id;
    std::string function_uid;
    IrAnchor anchor;
    nlohmann::json po_def;
    nlohmann::json ir_ref;
    bool is_bug = false;
    bool is_safe = false;
};

[[nodiscard]] sappp::VoidResult store_po_proof(const nlohmann::json& po,
                                               const PoBaseData& base,
                                               const PoProcessingContext& context,
                                               const std::vector<std::string>& contract_hashes,
                                               const std::optional<nlohmann::json>& points_to,
                                               std::string_view safety_domain)
{
    auto po_hash = put_cert(*context.cert_store, base.po_def);
    if (!po_hash) {
        return std::unexpected(po_hash.error());
    }

    auto ir_hash = put_cert(*context.cert_store, base.ir_ref);
    if (!ir_hash) {
        return std::unexpected(ir_hash.error());
    }

    EvidenceInput evidence_input{.po = &po,
                                 .ir_ref = &base.ir_ref,
                                 .nir_json = context.nir_json,
                                 .po_id = base.po_id,
                                 .function_uid = base.function_uid,
                                 .anchor = &base.anchor,
                                 .is_bug = base.is_bug,
                                 .is_safe = base.is_safe,
                                 .points_to = &points_to,
                                 .safety_domain = safety_domain};
    auto evidence_result = build_evidence(evidence_input);
    if (!evidence_result) {
        return std::unexpected(evidence_result.error());
    }

    auto evidence_hash = put_cert(*context.cert_store, evidence_result->evidence);
    if (!evidence_hash) {
        return std::unexpected(evidence_hash.error());
    }

    nlohmann::json depgraph =
        make_dependency_graph(*po_hash, *ir_hash, *evidence_hash, contract_hashes);
    auto depgraph_hash = put_cert(*context.cert_store, depgraph);
    if (!depgraph_hash) {
        return std::unexpected(depgraph_hash.error());
    }

    nlohmann::json root = make_proof_root(*po_hash,
                                          *ir_hash,
                                          *evidence_hash,
                                          std::optional<std::string>(*depgraph_hash),
                                          contract_hashes,
                                          evidence_result->result_kind,
                                          *context.versions);
    auto root_hash = put_cert(*context.cert_store, root);
    if (!root_hash) {
        return std::unexpected(root_hash.error());
    }
    if (auto bind = context.cert_store->bind_po(base.po_id, *root_hash); !bind) {
        return std::unexpected(bind.error());
    }
    return {};
}

[[nodiscard]] sappp::Result<PoBaseData> build_po_base(const nlohmann::json& po,
                                                      const PoProcessingContext& context,
                                                      bool is_bug,
                                                      bool is_safe)
{
    auto po_id = require_string(JsonFieldContext{.obj = &po, .key = "po_id", .context = "po"});
    if (!po_id) {
        return std::unexpected(po_id.error());
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }

    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    nlohmann::json po_def = {
        {"schema_version", "cert.v1"},
        {          "kind",   "PoDef"},
        {            "po",        po}
    };

    nlohmann::json ir_ref = make_ir_ref_obj(std::string(context.tu_id), *function_uid, *anchor);

    return PoBaseData{.po_id = *po_id,
                      .function_uid = *function_uid,
                      .anchor = std::move(*anchor),
                      .po_def = std::move(po_def),
                      .ir_ref = std::move(ir_ref),
                      .is_bug = is_bug,
                      .is_safe = is_safe};
}

struct PoDecision
{
    bool is_bug = false;
    bool is_safe = false;
    bool is_unknown = false;
    UnknownDetails unknown_details{};
    std::optional<nlohmann::json> points_to = std::nullopt;
    std::string safety_domain = std::string(kBaseSafetyDomain);
};

[[nodiscard]] sappp::Result<std::optional<bool>>
extract_predicate_boolean(const nlohmann::json& predicate_expr)
{
    if (!predicate_expr.contains("args") || !predicate_expr.at("args").is_array()) {
        return std::optional<bool>();
    }

    const auto& args = predicate_expr.at("args");
    for (const auto& arg : args | std::views::reverse) {
        if (arg.is_boolean()) {
            return std::optional<bool>(arg.get<bool>());
        }
    }
    return std::optional<bool>();
}

[[nodiscard]] std::optional<std::string>
extract_lifetime_target(const nlohmann::json& predicate_expr)
{
    if (!predicate_expr.contains("op") || !predicate_expr.at("op").is_string()) {
        return std::nullopt;
    }
    if (!predicate_expr.contains("args") || !predicate_expr.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = predicate_expr.at("args");
    const auto& op = predicate_expr.at("op").get_ref<const std::string&>();
    if (op == "sink.marker") {
        if (args.size() >= 2 && args.at(1).is_string()) {
            return args.at(1).get<std::string>();
        }
        return std::nullopt;
    }
    if ((op == "lifetime.begin" || op == "lifetime.end") && args.size() == 1
        && args.at(0).is_string()) {
        return args.at(0).get<std::string>();
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extract_init_target(const nlohmann::json& predicate_expr)
{
    if (!predicate_expr.contains("args") || !predicate_expr.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = predicate_expr.at("args");
    if (args.size() < 2) {
        return std::nullopt;
    }
    const auto& candidate = args.at(1);
    if (candidate.is_string()) {
        return candidate.get<std::string>();
    }
    return extract_ref_name(candidate);
}

[[nodiscard]] std::optional<std::string>
extract_points_to_pointer(const nlohmann::json& predicate_expr)
{
    if (!predicate_expr.contains("args") || !predicate_expr.at("args").is_array()) {
        return std::nullopt;
    }
    const auto& args = predicate_expr.at("args");
    if (args.size() < 2) {
        return std::nullopt;
    }
    if (!args.at(1).is_string()) {
        return std::nullopt;
    }
    return args.at(1).get<std::string>();
}

[[nodiscard]] bool points_to_contains(const PointsToSet& set, std::string_view target)
{
    return std::ranges::find(set.targets, target) != set.targets.end();
}

[[nodiscard]] nlohmann::json build_points_to_entries(std::string_view ptr, const PointsToSet& set)
{
    return nlohmann::json::array({
        nlohmann::json{{"ptr", std::string(ptr)}, {"targets", set.targets}}
    });
}

[[nodiscard]] sappp::Result<std::optional<PoDecision>>
// NOLINTNEXTLINE(readability-function-size, bugprone-easily-swappable-parameters) - PO+predicate.
decide_points_to(const nlohmann::json& po,
                 const nlohmann::json& predicate_expr,
                 std::string_view po_kind,
                 const PoProcessingContext& context)
{
    if (context.points_to_cache == nullptr || context.function_uid_map == nullptr) {
        return std::optional<PoDecision>();
    }

    auto pointer = extract_points_to_pointer(predicate_expr);
    if (!pointer) {
        return std::optional<PoDecision>();
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }
    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    auto analysis_it = context.points_to_cache->functions.find(*function_uid);
    if (analysis_it == context.points_to_cache->functions.end()) {
        return std::optional<PoDecision>();
    }

    auto state = points_to_state_at_anchor(analysis_it->second, *anchor);
    if (!state) {
        return std::unexpected(state.error());
    }
    const auto& state_opt = *state;
    if (!state_opt.has_value()) {
        return std::optional<PoDecision>();
    }

    const auto& points_state = *state_opt;
    auto set_it = points_state.values.find(*pointer);
    if (set_it == points_state.values.end()) {
        return std::optional<PoDecision>();
    }

    const PointsToSet& points_to_set = set_it->second;
    if (points_to_set.is_unknown || points_to_set.targets.empty()) {
        PoDecision decision;
        decision.is_unknown = true;
        decision.unknown_details = build_unknown_details(po_kind);
        return std::optional<PoDecision>(decision);
    }

    if (po_kind == "UB.NullDeref") {
        const bool has_null = points_to_contains(points_to_set, kPointsToNullTarget);
        if (has_null) {
            if (points_to_set.targets.size() == 1U) {
                PoDecision decision;
                decision.is_bug = true;
                return std::optional<PoDecision>(decision);
            }
            PoDecision decision;
            decision.is_unknown = true;
            decision.unknown_details = build_unknown_details(po_kind);
            return std::optional<PoDecision>(decision);
        }

        PoDecision decision;
        decision.is_safe = true;
        decision.points_to = build_points_to_entries(*pointer, points_to_set);
        decision.safety_domain = context.points_to_domain;
        return std::optional<PoDecision>(decision);
    }

    if (po_kind == "UB.OutOfBounds") {
        const bool has_oob = points_to_contains(points_to_set, kPointsToOutOfBoundsTarget);
        const bool has_inbounds = points_to_contains(points_to_set, kPointsToInBoundsTarget);
        if (has_oob) {
            if (points_to_set.targets.size() == 1U) {
                PoDecision decision;
                decision.is_bug = true;
                return std::optional<PoDecision>(decision);
            }
            PoDecision decision;
            decision.is_unknown = true;
            decision.unknown_details = build_unknown_details(po_kind);
            return std::optional<PoDecision>(decision);
        }
        if (has_inbounds && points_to_set.targets.size() == 1U) {
            PoDecision decision;
            decision.is_safe = true;
            decision.points_to = build_points_to_entries(*pointer, points_to_set);
            decision.safety_domain = context.points_to_domain;
            return std::optional<PoDecision>(decision);
        }
        PoDecision decision;
        decision.is_unknown = true;
        decision.unknown_details = build_unknown_details(po_kind);
        return std::optional<PoDecision>(decision);
    }

    return std::optional<PoDecision>();
}

[[nodiscard]] sappp::Result<PoDecision>
decide_use_after_lifetime(  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const nlohmann::json& po,
    const nlohmann::json& predicate_expr,
    const PoProcessingContext& context)
{
    PoDecision decision;
    auto target = extract_lifetime_target(predicate_expr);
    if (!target) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_lifetime_unmodeled_details("Lifetime target is missing from the PO predicate.");
        return decision;
    }
    if (context.lifetime_cache == nullptr || context.function_uid_map == nullptr) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_lifetime_unmodeled_details("Lifetime analysis context unavailable.");
        return decision;
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }
    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    auto analysis_it = context.lifetime_cache->functions.find(*function_uid);
    if (analysis_it == context.lifetime_cache->functions.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_lifetime_unmodeled_details("Lifetime analysis missing for function.");
        return decision;
    }

    auto state = state_at_anchor(analysis_it->second, *anchor);
    if (!state) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_lifetime_unmodeled_details("Lifetime analysis missing at anchor.");
        return decision;
    }

    auto state_it = state->values.find(*target);
    if (state_it == state->values.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_lifetime_unmodeled_details("Lifetime target is not tracked at anchor.");
        return decision;
    }

    switch (state_it->second) {
        case LifetimeValue::kDead:
            decision.is_bug = true;
            return decision;
        case LifetimeValue::kAlive:
            decision.is_safe = true;
            return decision;
        case LifetimeValue::kMaybe:
            decision.is_unknown = true;
            decision.unknown_details = build_use_after_lifetime_unknown_details(
                "Lifetime state is indeterminate at this point.");
            return decision;
        default:
            decision.is_unknown = true;
            decision.unknown_details =
                build_use_after_lifetime_unknown_details("Lifetime state is indeterminate.");
            return decision;
    }
}

[[nodiscard]] sappp::Result<PoDecision>
decide_uninit_read(  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const nlohmann::json& po,
    const nlohmann::json& predicate_expr,
    const PoProcessingContext& context)
{
    PoDecision decision;
    auto target = extract_init_target(predicate_expr);
    if (!target) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_init_unknown_details("Init target is missing from the PO predicate.");
        return decision;
    }
    if (context.init_cache == nullptr || context.function_uid_map == nullptr) {
        decision.is_unknown = true;
        decision.unknown_details = build_init_unknown_details("Init analysis context unavailable.");
        return decision;
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }
    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    auto analysis_it = context.init_cache->functions.find(*function_uid);
    if (analysis_it == context.init_cache->functions.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_init_unknown_details("Init analysis missing for function.");
        return decision;
    }

    auto state = init_state_at_anchor(analysis_it->second, *anchor);
    if (!state) {
        decision.is_unknown = true;
        decision.unknown_details = build_init_unknown_details("Init analysis missing at anchor.");
        return decision;
    }

    auto state_it = state->values.find(*target);
    if (state_it == state->values.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_init_unknown_details("Init target is not tracked at anchor.");
        return decision;
    }

    switch (state_it->second) {
        case InitValue::kInit:
            decision.is_safe = true;
            return decision;
        case InitValue::kUninit:
            decision.is_bug = true;
            return decision;
        case InitValue::kMaybe:
            decision.is_unknown = true;
            decision.unknown_details =
                build_init_unknown_details("Init state is indeterminate at this point.");
            return decision;
        default:
            decision.is_unknown = true;
            decision.unknown_details = build_init_unknown_details("Init state is indeterminate.");
            return decision;
    }
}

[[nodiscard]] sappp::Result<PoDecision>
// NOLINTNEXTLINE(readability-function-size) - Mirror lifetime cases.
decide_heap_free(  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const nlohmann::json& po,
    const nlohmann::json& predicate_expr,
    const PoProcessingContext& context,
    std::string_view po_kind)
{
    PoDecision decision;
    auto target = extract_lifetime_target(predicate_expr);
    if (!target) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_heap_lifetime_unmodeled_details("Heap target is missing from the PO predicate.");
        return decision;
    }
    if (context.heap_lifetime_cache == nullptr || context.function_uid_map == nullptr) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_heap_lifetime_unmodeled_details("Heap lifetime analysis context unavailable.");
        return decision;
    }

    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }
    auto anchor = extract_anchor(po);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }

    auto analysis_it = context.heap_lifetime_cache->functions.find(*function_uid);
    if (analysis_it == context.heap_lifetime_cache->functions.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_heap_lifetime_unmodeled_details("Heap lifetime analysis missing for function.");
        return decision;
    }

    auto state = heap_state_at_anchor(analysis_it->second, *anchor);
    if (!state) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_heap_lifetime_unmodeled_details("Heap lifetime analysis missing at anchor.");
        return decision;
    }

    auto state_it = state->values.find(*target);
    if (state_it == state->values.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_heap_lifetime_unmodeled_details("Heap target is not tracked at anchor.");
        return decision;
    }

    switch (state_it->second) {
        case HeapLifetimeValue::kAllocated:
            decision.is_safe = true;
            return decision;
        case HeapLifetimeValue::kFreed:
            decision.is_bug = true;
            return decision;
        case HeapLifetimeValue::kUnallocated:
            if (po_kind == "InvalidFree") {
                decision.is_bug = true;
                return decision;
            }
            decision.is_unknown = true;
            decision.unknown_details =
                build_heap_lifetime_unknown_details("Heap target is unallocated at anchor.");
            return decision;
        case HeapLifetimeValue::kMaybe:
            decision.is_unknown = true;
            decision.unknown_details = build_heap_lifetime_unknown_details(
                "Heap lifetime state is indeterminate at this point.");
            return decision;
        default:
            decision.is_unknown = true;
            decision.unknown_details =
                build_heap_lifetime_unknown_details("Heap lifetime state is indeterminate.");
            return decision;
    }
}

[[nodiscard]] sappp::Result<const VCallSummary*>
find_vcall_summary(const nlohmann::json& po, const PoProcessingContext& context)
{
    if (context.vcall_summaries == nullptr || context.function_uid_map == nullptr) {
        return static_cast<const VCallSummary*>(nullptr);
    }
    auto function_uid = resolve_function_uid(*context.function_uid_map, po);
    if (!function_uid) {
        return std::unexpected(function_uid.error());
    }
    auto it = context.vcall_summaries->find(*function_uid);
    if (it == context.vcall_summaries->end()) {
        return static_cast<const VCallSummary*>(nullptr);
    }
    return &it->second;
}

[[nodiscard]] sappp::Result<std::optional<UnknownDetails>>
resolve_vcall_unknown_details(const nlohmann::json& po, const PoProcessingContext& context)
{
    auto summary = find_vcall_summary(po, context);
    if (!summary) {
        return std::unexpected(summary.error());
    }
    if (*summary == nullptr || !(*summary)->has_vcall) {
        return std::optional<UnknownDetails>();
    }
    const VCallSummary& detail = *(*summary);
    if (detail.missing_candidate_set) {
        return std::optional<UnknownDetails>(
            build_vcall_missing_candidates_details(detail.missing_candidate_ids));
    }
    if (detail.empty_candidate_set) {
        return std::optional<UnknownDetails>(build_vcall_empty_candidates_details());
    }
    if (!detail.missing_contract_targets.empty()) {
        return std::optional<UnknownDetails>(
            build_vcall_missing_contract_details(detail.missing_contract_targets));
    }
    return std::optional<UnknownDetails>();
}

// NOLINTNEXTLINE(readability-function-size) - Central PO dispatch.
[[nodiscard]] sappp::Result<PoDecision> decide_po(const nlohmann::json& po,
                                                  const PoProcessingContext& context)
{
    if (context.budget_exceeded_limit.has_value()) {
        PoDecision decision;
        decision.is_unknown = true;
        decision.unknown_details =
            build_budget_exceeded_unknown_details(*context.budget_exceeded_limit);
        return decision;
    }

    auto vcall_unknown = resolve_vcall_unknown_details(po, context);
    if (!vcall_unknown) {
        return std::unexpected(vcall_unknown.error());
    }
    auto vcall_details_opt = std::move(*vcall_unknown);
    if (vcall_details_opt.has_value()) {
        PoDecision decision;
        decision.is_unknown = true;
        decision.unknown_details = std::move(*vcall_details_opt);
        return decision;
    }

    auto po_kind = require_string(JsonFieldContext{.obj = &po, .key = "po_kind", .context = "po"});
    if (!po_kind) {
        return std::unexpected(po_kind.error());
    }
    auto predicate_expr = extract_predicate_expr(po);
    if (!predicate_expr) {
        return std::unexpected(predicate_expr.error());
    }
    if (!predicate_expr->contains("op") || !predicate_expr->at("op").is_string()) {
        return std::unexpected(
            sappp::Error::make("InvalidFieldType", "Expected predicate.expr.op string in po"));
    }
    std::string op = predicate_expr->at("op").get<std::string>();
    auto predicate_bool = extract_predicate_boolean(*predicate_expr);
    if (!predicate_bool) {
        return std::unexpected(predicate_bool.error());
    }

    if (op == "ub.check") {
        const std::optional<bool>& predicate_value = *predicate_bool;
        if (predicate_value.has_value()) {
            bool holds = *predicate_value;
            PoDecision decision;
            decision.is_bug = holds;
            decision.is_safe = !holds;
            return decision;
        }
        PoDecision decision;
        decision.is_bug = true;
        return decision;
    }

    if (*po_kind == "UseAfterLifetime") {
        auto decision = decide_use_after_lifetime(po, *predicate_expr, context);
        if (!decision) {
            return std::unexpected(decision.error());
        }
        return *decision;
    }

    if (*po_kind == "UninitRead") {
        auto decision = decide_uninit_read(po, *predicate_expr, context);
        if (!decision) {
            return std::unexpected(decision.error());
        }
        return *decision;
    }

    if (*po_kind == "DoubleFree" || *po_kind == "InvalidFree") {
        auto decision = decide_heap_free(po, *predicate_expr, context, *po_kind);
        if (!decision) {
            return std::unexpected(decision.error());
        }
        return *decision;
    }

    if (*po_kind == "UB.NullDeref" || *po_kind == "UB.OutOfBounds") {
        auto points_to_decision = decide_points_to(po, *predicate_expr, *po_kind, context);
        if (!points_to_decision) {
            return std::unexpected(points_to_decision.error());
        }
        const auto& points_opt = *points_to_decision;
        if (points_opt.has_value()) {
            return *points_opt;
        }
    }

    if (op == "sink.marker") {
        if (*po_kind == "UB.OutOfBounds" || *po_kind == "UB.NullDeref") {
            PoDecision decision;
            decision.is_bug = true;
            return decision;
        }
    }

    auto details = build_unknown_details(*po_kind);
    PoDecision decision;
    decision.is_unknown = true;
    decision.unknown_details = std::move(details);
    return decision;
}

[[nodiscard]] sappp::Result<ContractMatchSummary>
resolve_contracts(const nlohmann::json& po, const PoProcessingContext& context)
{
    if (context.contract_index == nullptr) {
        return ContractMatchSummary{};
    }
    ContractMatchContext fallback;
    const ContractMatchContext& match_context =
        context.match_context == nullptr ? fallback : *context.match_context;
    return match_contracts_for_po(po, *context.contract_index, match_context);
}

// NOLINTNEXTLINE(readability-function-size) - Aggregates PO artifacts.
[[nodiscard]] sappp::Result<PoProcessingOutput> process_po(const nlohmann::json& po,
                                                           PoProcessingContext& context)
{
    auto decision = decide_po(po, context);
    if (!decision) {
        return std::unexpected(decision.error());
    }
    auto contract_match = resolve_contracts(po, context);
    if (!contract_match) {
        return std::unexpected(contract_match.error());
    }

    std::vector<const ContractInfo*> vcall_contracts;
    const VCallSummary* vcall_summary_ptr = nullptr;
    auto vcall_summary = find_vcall_summary(po, context);
    if (!vcall_summary) {
        return std::unexpected(vcall_summary.error());
    }
    if (*vcall_summary != nullptr) {
        vcall_summary_ptr = *vcall_summary;
        vcall_contracts = vcall_summary_ptr->candidate_contracts;
    }

    auto merged_contracts = merge_contracts(*contract_match, vcall_contracts);
    std::vector<std::string> contract_hashes;
    if (context.contract_ref_cache != nullptr && context.contract_index != nullptr) {
        contract_hashes.reserve(merged_contracts.size());
        for (const auto* contract : merged_contracts) {
            auto hash = ensure_contract_ref(*contract, context);
            if (!hash) {
                return std::unexpected(hash.error());
            }
            contract_hashes.push_back(std::move(*hash));
        }
        std::ranges::stable_sort(contract_hashes);
        auto unique_hashes = std::ranges::unique(contract_hashes);
        contract_hashes.erase(unique_hashes.begin(), unique_hashes.end());
    }

    auto base = build_po_base(po, context, decision->is_bug, decision->is_safe);
    if (!base) {
        return std::unexpected(base.error());
    }

    if (auto stored = store_po_proof(po,
                                     *base,
                                     context,
                                     contract_hashes,
                                     decision->points_to,
                                     decision->safety_domain);
        !stored) {
        return std::unexpected(stored.error());
    }

    PoProcessingOutput output{.po_id = base->po_id};
    if (decision->is_unknown || (!base->is_bug && !base->is_safe)) {
        UnknownDetails details = decision->unknown_details;
        if (context.feature_cache != nullptr && allow_feature_override(details.code)) {
            auto it = context.feature_cache->find(base->function_uid);
            if (it != context.feature_cache->end()) {
                if (auto feature_details = build_feature_unknown_details(it->second,
                                                                         *contract_match,
                                                                         vcall_summary_ptr)) {
                    details = std::move(*feature_details);
                }
            }
        }
        if (details.code != "BudgetExceeded"
            && (contract_match->contracts.empty() || !contract_match->has_pre)
            && !details.code.starts_with("VirtualCall.")) {
            details = build_missing_contract_details("Pre");
        }
        auto contract_ids = collect_contract_ids(*contract_match, vcall_contracts);
        auto unknown_entry = build_unknown_entry(po, base->po_id, details, contract_ids);
        if (!unknown_entry) {
            return std::unexpected(unknown_entry.error());
        }
        output.has_unknown = true;
        output.unknown_entry = std::move(*unknown_entry);
    }

    return output;
}

[[nodiscard]] sappp::VoidResult
ensure_unknowns(std::vector<nlohmann::json>& unknowns,
                const std::vector<const nlohmann::json*>& ordered_pos,
                const PoProcessingContext& context)
{
    if (!unknowns.empty() || ordered_pos.empty()) {
        return {};
    }

    const nlohmann::json& po = *ordered_pos.front();
    auto po_id = require_string(JsonFieldContext{.obj = &po, .key = "po_id", .context = "po"});
    if (!po_id) {
        return std::unexpected(po_id.error());
    }
    auto contract_match = resolve_contracts(po, context);
    if (!contract_match) {
        return std::unexpected(contract_match.error());
    }
    UnknownDetails details = build_unknown_details("UB.Unknown");
    if (contract_match->contracts.empty() || !contract_match->has_pre) {
        details = build_missing_contract_details("Pre");
    }
    auto contract_ids = collect_contract_ids(*contract_match);
    auto unknown_entry = build_unknown_entry(po, *po_id, details, contract_ids);
    if (!unknown_entry) {
        return std::unexpected(unknown_entry.error());
    }
    unknowns.push_back(std::move(*unknown_entry));
    return {};
}

}  // namespace

Analyzer::Analyzer(AnalyzerConfig config)
    : m_config(std::move(config))
{}

// NOLINTNEXTLINE(readability-function-size) - Top-level analysis.
sappp::Result<AnalyzeOutput> Analyzer::analyze(const nlohmann::json& nir_json,
                                               const nlohmann::json& po_list_json,
                                               const nlohmann::json* specdb_snapshot,
                                               const ContractMatchContext& match_context) const
{
    auto tu_id =
        require_string(JsonFieldContext{.obj = &nir_json, .key = "tu_id", .context = "nir"});
    if (!tu_id) {
        return std::unexpected(tu_id.error());
    }

    auto tool_obj =
        require_object(JsonFieldContext{.obj = &nir_json, .key = "tool", .context = "nir"});
    if (!tool_obj) {
        return std::unexpected(tool_obj.error());
    }

    auto ordered_pos = collect_ordered_pos(po_list_json);
    if (!ordered_pos) {
        return std::unexpected(ordered_pos.error());
    }
    const auto& ordered_pos_value = ordered_pos.value();

    nlohmann::json unknown_ledger =
        build_unknown_ledger_base(nir_json, po_list_json, m_config.versions, *tool_obj, *tu_id);

    sappp::certstore::CertStore cert_store(m_config.certstore_dir, m_config.schema_dir);
    BudgetTracker budget_tracker(m_config.budget);
    const auto function_uid_map = build_function_uid_map(nir_json);
    auto contract_index = build_contract_index(specdb_snapshot);
    if (!contract_index) {
        return std::unexpected(contract_index.error());
    }
    ContractMatchContext normalized_context = normalize_match_context(match_context);
    const auto vcall_summaries =
        build_vcall_summary_map(nir_json, *contract_index, normalized_context);
    const auto lifetime_cache = build_lifetime_analysis_cache(nir_json, &budget_tracker);
    const auto heap_lifetime_cache = build_heap_lifetime_analysis_cache(nir_json, &budget_tracker);
    const auto init_cache = build_init_analysis_cache(nir_json, &budget_tracker);
    auto points_to_cache = build_points_to_analysis_cache(nir_json, &budget_tracker);
    if (!points_to_cache) {
        return std::unexpected(points_to_cache.error());
    }
    const auto feature_cache = build_function_feature_cache(nir_json);
    std::unordered_map<std::string, std::string> contract_ref_cache;

    std::string points_to_domain = std::string(kPointsToDomainSimple);
    if (m_config.memory_domain.has_value()) {
        if (*m_config.memory_domain == "points-to.context") {
            points_to_domain = std::string(kPointsToDomainContext);
        } else if (*m_config.memory_domain == "points-to.simple") {
            points_to_domain = std::string(kPointsToDomainSimple);
        }
    }

    std::vector<nlohmann::json> unknowns;
    unknowns.reserve(ordered_pos_value.size());

    PoProcessingContext context{.cert_store = &cert_store,
                                .function_uid_map = &function_uid_map,
                                .feature_cache = &feature_cache,
                                .contract_index = &(*contract_index),
                                .match_context = &normalized_context,
                                .vcall_summaries = &vcall_summaries,
                                .contract_ref_cache = &contract_ref_cache,
                                .lifetime_cache = &lifetime_cache,
                                .heap_lifetime_cache = &heap_lifetime_cache,
                                .init_cache = &init_cache,
                                .nir_json = &nir_json,
                                .points_to_cache = &(*points_to_cache),
                                .tu_id = *tu_id,
                                .budget_exceeded_limit = budget_tracker.limit_reason(),
                                .points_to_domain = std::move(points_to_domain),
                                .versions = &m_config.versions};

    for (const nlohmann::json* po_entry : ordered_pos_value) {
        const nlohmann::json& po = *po_entry;
        auto processed = process_po(po, context);
        if (!processed) {
            return std::unexpected(processed.error());
        }
        if (processed->has_unknown) {
            unknowns.push_back(std::move(processed->unknown_entry));
        }
    }

    if (auto ensure_result = ensure_unknowns(unknowns, ordered_pos_value, context);
        !ensure_result) {
        return std::unexpected(ensure_result.error());
    }

    std::ranges::stable_sort(unknowns, [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("unknown_stable_id").get<std::string>()
               < b.at("unknown_stable_id").get<std::string>();
    });

    unknown_ledger["unknowns"] = unknowns;

    const std::string schema_path = m_config.schema_dir + "/unknown.v1.schema.json";
    if (auto validation = sappp::common::validate_json(unknown_ledger, schema_path); !validation) {
        return std::unexpected(validation.error());
    }

    return AnalyzeOutput{.unknown_ledger = std::move(unknown_ledger)};
}

}  // namespace sappp::analyzer
