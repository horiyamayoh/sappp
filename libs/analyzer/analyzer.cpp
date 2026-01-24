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
#include <deque>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sappp::analyzer {

namespace {

constexpr std::string_view kSafetyDomain = "interval+null+lifetime+init";
constexpr std::string_view kDeterministicGeneratedAt = "1970-01-01T00:00:00Z";

struct ContractInfo
{
    std::string contract_id;
    std::string tier;
    std::string target_usr;
    nlohmann::json version_scope;
    bool has_pre = false;
    bool has_post = false;
    bool has_frame = false;
    bool has_ownership = false;
    bool has_concurrency = false;
};

using ContractIndex = std::map<std::string, std::vector<ContractInfo>>;

struct JsonFieldContext
{
    const nlohmann::json* obj = nullptr;
    std::string_view key;
    std::string_view context;
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

    nlohmann::json version_scope = nlohmann::json::object();
    if (contract.contains("version_scope") && contract.at("version_scope").is_object()) {
        version_scope = contract.at("version_scope");
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

    ContractMatchSummary()
        // NOLINTNEXTLINE(readability-redundant-member-init) - required for -Weffc++.
        : contracts()
    {}
};

[[nodiscard]] sappp::Result<ContractMatchSummary>
match_contracts_for_po(const nlohmann::json& po, const ContractIndex& contract_index)
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
    auto it = contract_index.find(*usr);
    if (it == contract_index.end()) {
        return summary;
    }
    summary.contracts.reserve(it->second.size());
    for (const auto& contract : it->second) {
        summary.contracts.push_back(&contract);
        summary.has_pre = summary.has_pre || contract.has_pre;
    }
    return summary;
}

[[nodiscard]] std::vector<std::string> collect_contract_ids(const ContractMatchSummary& summary)
{
    std::vector<std::string> ids;
    ids.reserve(summary.contracts.size());
    for (const auto* contract : summary.contracts) {
        ids.push_back(contract->contract_id);
    }
    std::ranges::stable_sort(ids);
    auto unique_ids = std::ranges::unique(ids);
    ids.erase(unique_ids.begin(), unique_ids.end());
    return ids;
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
    }
}

struct FunctionLifetimeAnalysis
{
    std::string function_uid;
    std::string entry_block;
    std::map<std::string, const nlohmann::json*> blocks;
    std::vector<std::string> block_order;
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

[[nodiscard]] LifetimeState apply_block_transfer(const LifetimeState& in_state,
                                                 const nlohmann::json& block)
{
    LifetimeState state = in_state;
    if (!block.contains("insts") || !block.at("insts").is_array()) {
        return state;
    }
    for (const auto& inst : block.at("insts")) {
        apply_lifetime_effect(inst, state);
    }
    return state;
}

void compute_lifetime_fixpoint(FunctionLifetimeAnalysis& analysis)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block_id : analysis.block_order) {
            LifetimeState normal_in =
                merge_predecessor_states(analysis, block_id, LifetimeFlow::kNormal);
            LifetimeState exception_in =
                merge_predecessor_states(analysis, block_id, LifetimeFlow::kException);

            if (auto landingpad_it = analysis.has_landingpad.find(block_id);
                landingpad_it != analysis.has_landingpad.end() && landingpad_it->second) {
                normal_in = merge_lifetime_states(normal_in, exception_in);
            }

            auto normal_in_it = analysis.normal_in_states.find(block_id);
            if (normal_in_it == analysis.normal_in_states.end()
                || normal_in_it->second.values != normal_in.values) {
                analysis.normal_in_states[block_id] = normal_in;
                changed = true;
            }

            auto exception_in_it = analysis.exception_in_states.find(block_id);
            if (exception_in_it == analysis.exception_in_states.end()
                || exception_in_it->second.values != exception_in.values) {
                analysis.exception_in_states[block_id] = exception_in;
                changed = true;
            }

            auto block_it = analysis.blocks.find(block_id);
            if (block_it == analysis.blocks.end()) {
                continue;
            }
            LifetimeState normal_out = apply_block_transfer(normal_in, *block_it->second);
            auto normal_out_it = analysis.normal_out_states.find(block_id);
            if (normal_out_it == analysis.normal_out_states.end()
                || normal_out_it->second.values != normal_out.values) {
                analysis.normal_out_states[block_id] = normal_out;
                changed = true;
            }

            LifetimeState exception_source = exception_in;
            if (auto exception_succ_it = analysis.has_exception_successor.find(block_id);
                exception_succ_it != analysis.has_exception_successor.end()
                && exception_succ_it->second) {
                exception_source = merge_lifetime_states(exception_source, normal_in);
            }

            LifetimeState exception_out = apply_block_transfer(exception_source, *block_it->second);
            auto exception_out_it = analysis.exception_out_states.find(block_id);
            if (exception_out_it == analysis.exception_out_states.end()
                || exception_out_it->second.values != exception_out.values) {
                analysis.exception_out_states[block_id] = exception_out;
                changed = true;
            }
        }
    }
}

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

[[nodiscard]] LifetimeAnalysisCache
build_lifetime_analysis_cache(const nlohmann::json& nir_json)  // NOLINT(readability-function-size)
{
    LifetimeAnalysisCache cache;
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
            compute_lifetime_fixpoint(analysis);
            cache.functions.emplace(analysis.function_uid, std::move(analysis));
        }
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

    queue.push_back(std::string(entry_block));
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
            queue.push_back(edge.to);
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
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        path.push_back(*it);
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
                                               bool predicate_holds)
{
    nlohmann::json state = nlohmann::json::object();
    if (predicate_holds) {
        state["predicates"] = nlohmann::json::array({predicate_expr});
    } else {
        state["predicates"] = nlohmann::json::array();
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
        {        "domain",     std::string(kSafetyDomain)},
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

[[nodiscard]] UnknownDetails build_use_after_lifetime_unknown_details(std::string_view notes)
{
    return UnknownDetails{.code = "LifetimeStateUnknown",
                          .missing_notes = std::string(notes),
                          .refinement_message =
                              "Provide lifetime target context or refine lifetime tracking.",
                          .refinement_action = "refine-lifetime",
                          .refinement_domain = "lifetime"};
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
    const ContractIndex* contract_index = nullptr;
    std::unordered_map<std::string, std::string>* contract_ref_cache = nullptr;
    const LifetimeAnalysisCache* lifetime_cache = nullptr;
    const nlohmann::json* nir_json = nullptr;
    std::string_view tu_id;
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
                                                        input.is_safe),
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
                                               const std::vector<std::string>& contract_hashes)
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
                                 .is_safe = base.is_safe};
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
        decision.unknown_details = build_use_after_lifetime_unknown_details(
            "Lifetime target is missing from the PO predicate.");
        return decision;
    }
    if (context.lifetime_cache == nullptr || context.function_uid_map == nullptr) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_use_after_lifetime_unknown_details("Lifetime analysis context unavailable.");
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
            build_use_after_lifetime_unknown_details("Lifetime analysis missing for function.");
        return decision;
    }

    auto state = state_at_anchor(analysis_it->second, *anchor);
    if (!state) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_use_after_lifetime_unknown_details("Lifetime analysis missing at anchor.");
        return decision;
    }

    auto state_it = state->values.find(*target);
    if (state_it == state->values.end()) {
        decision.is_unknown = true;
        decision.unknown_details =
            build_use_after_lifetime_unknown_details("Lifetime target is not tracked at anchor.");
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

[[nodiscard]] sappp::Result<PoDecision> decide_po(const nlohmann::json& po,
                                                  const PoProcessingContext& context)
{
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
    return match_contracts_for_po(po, *context.contract_index);
}

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
    std::vector<std::string> contract_hashes;
    if (context.contract_ref_cache != nullptr && context.contract_index != nullptr) {
        contract_hashes.reserve(contract_match->contracts.size());
        for (const auto* contract : contract_match->contracts) {
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

    if (auto stored = store_po_proof(po, *base, context, contract_hashes); !stored) {
        return std::unexpected(stored.error());
    }

    PoProcessingOutput output{.po_id = base->po_id};
    if (decision->is_unknown || (!base->is_bug && !base->is_safe)) {
        UnknownDetails details = decision->unknown_details;
        if (contract_match->contracts.empty() || !contract_match->has_pre) {
            details = build_missing_contract_details("Pre");
        }
        auto contract_ids = collect_contract_ids(*contract_match);
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

sappp::Result<AnalyzeOutput> Analyzer::analyze(const nlohmann::json& nir_json,
                                               const nlohmann::json& po_list_json,
                                               const nlohmann::json* specdb_snapshot) const
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
    const auto function_uid_map = build_function_uid_map(nir_json);
    auto contract_index = build_contract_index(specdb_snapshot);
    if (!contract_index) {
        return std::unexpected(contract_index.error());
    }
    const auto lifetime_cache = build_lifetime_analysis_cache(nir_json);
    std::unordered_map<std::string, std::string> contract_ref_cache;

    std::vector<nlohmann::json> unknowns;
    unknowns.reserve(ordered_pos_value.size());

    PoProcessingContext context{.cert_store = &cert_store,
                                .function_uid_map = &function_uid_map,
                                .contract_index = &(*contract_index),
                                .contract_ref_cache = &contract_ref_cache,
                                .lifetime_cache = &lifetime_cache,
                                .nir_json = &nir_json,
                                .tu_id = *tu_id,
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
