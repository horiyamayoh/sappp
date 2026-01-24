#pragma once

/**
 * @file nir.hpp
 * @brief Normalized IR (NIR) data structures
 */

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sappp::ir {

struct Location
{
    std::string file;
    int line = 0;
    int col = 0;
};

struct Instruction
{
    std::string id;
    std::string op;
    std::vector<nlohmann::json> args;
    std::optional<Location> src;
};

struct BasicBlock
{
    std::string id;
    std::vector<Instruction> insts;
};

struct Edge
{
    std::string from;
    std::string to;
    std::string kind;
};

struct Cfg
{
    std::string entry;
    std::vector<BasicBlock> blocks;
    std::vector<Edge> edges;
};

struct VCallCandidateSet
{
    std::string id;
    std::vector<std::string> methods;
};

struct FunctionTables
{
    std::vector<VCallCandidateSet> vcall_candidates;
};

struct FunctionParam
{
    std::string name;
    std::string type;
};

struct FunctionSignature
{
    std::string return_type;
    std::vector<FunctionParam> params;
    bool is_noexcept = false;
    bool variadic = false;
};

struct FunctionDef
{
    std::string function_uid;
    std::string mangled_name;
    FunctionSignature signature;
    Cfg cfg;
    std::optional<FunctionTables> tables;
};

struct Nir
{
    std::string schema_version;
    nlohmann::json tool;
    std::string generated_at;
    std::string tu_id;
    std::string semantics_version;
    std::string proof_system_version;
    std::string profile_version;
    std::optional<std::string> input_digest;
    std::vector<FunctionDef> functions;
};

inline void to_json(nlohmann::json& j, const Location& loc)
{
    j = nlohmann::json{
        {"file", loc.file},
        {"line", loc.line},
        { "col",  loc.col}
    };
}

inline void to_json(nlohmann::json& j, const Instruction& inst)
{
    j = nlohmann::json{
        {"id", inst.id},
        {"op", inst.op}
    };
    if (!inst.args.empty()) {
        j["args"] = inst.args;
    }
    if (inst.src.has_value()) {
        j["src"] = *inst.src;
    }
}

inline void to_json(nlohmann::json& j, const BasicBlock& block)
{
    j = nlohmann::json{
        {   "id",    block.id},
        {"insts", block.insts}
    };
}

inline void to_json(nlohmann::json& j, const Edge& edge)
{
    j = nlohmann::json{
        {"from", edge.from},
        {  "to",   edge.to},
        {"kind", edge.kind}
    };
}

inline void to_json(nlohmann::json& j, const Cfg& cfg)
{
    j = nlohmann::json{
        { "entry",  cfg.entry},
        {"blocks", cfg.blocks},
        { "edges",  cfg.edges}
    };
}

inline void to_json(nlohmann::json& j, const VCallCandidateSet& candidate_set)
{
    j = nlohmann::json{
        {     "id",      candidate_set.id},
        {"methods", candidate_set.methods}
    };
}

inline void to_json(nlohmann::json& j, const FunctionTables& tables)
{
    j = nlohmann::json{
        {"vcall_candidates", tables.vcall_candidates}
    };
}

inline void to_json(nlohmann::json& j, const FunctionParam& param)
{
    j = nlohmann::json{
        {"name", param.name},
        {"type", param.type}
    };
}

inline void to_json(nlohmann::json& j, const FunctionSignature& signature)
{
    j = nlohmann::json{
        {"return_type", signature.return_type},
        {     "params",      signature.params},
        {   "noexcept", signature.is_noexcept},
        {   "variadic",    signature.variadic}
    };
}

inline void to_json(nlohmann::json& j, const FunctionDef& func)
{
    j = nlohmann::json{
        {"function_uid", func.function_uid},
        {"mangled_name", func.mangled_name},
        {   "signature",    func.signature},
        {         "cfg",          func.cfg}
    };
    if (func.tables.has_value() && !func.tables->vcall_candidates.empty()) {
        j["tables"] = *func.tables;
    }
}

inline void to_json(nlohmann::json& j, const Nir& nir)
{
    j = nlohmann::json{
        {      "schema_version",       nir.schema_version},
        {                "tool",                 nir.tool},
        {        "generated_at",         nir.generated_at},
        {               "tu_id",                nir.tu_id},
        {   "semantics_version",    nir.semantics_version},
        {"proof_system_version", nir.proof_system_version},
        {     "profile_version",      nir.profile_version},
        {           "functions",            nir.functions}
    };
    if (nir.input_digest.has_value()) {
        j["input_digest"] = *nir.input_digest;
    }
}

}  // namespace sappp::ir
