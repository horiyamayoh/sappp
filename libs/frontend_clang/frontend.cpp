/**
 * @file frontend.cpp
 * @brief Clang frontend implementation for NIR and source map generation
 */

#include "frontend_clang/frontend.hpp"

#include "nir.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <iomanip>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Mangle.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/raw_ostream.h>

namespace sappp::frontend_clang {

namespace {

struct SourceMapEntryKey
{
    std::string function_uid;
    std::string block_id;
    std::string inst_id;
    nlohmann::json entry;
};

std::string current_time_utc()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

bool is_source_file(const std::string& path)
{
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return false;
    }
    std::string ext = path.substr(pos + 1);
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) noexcept {
        return static_cast<char>(std::tolower(c));
    });
    return ext == "c" || ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "c++" || ext == "cp";
}

struct CompileUnitCommand
{
    std::string file_path;
    std::vector<std::string> args;
};

sappp::Result<CompileUnitCommand> extract_compile_command(const nlohmann::json& compile_unit)
{
    CompileUnitCommand result;
    result.args = compile_unit.at("argv").get<std::vector<std::string>>();

    std::optional<std::size_t> source_index;
    for (auto [i, arg] : std::views::enumerate(result.args)) {
        const auto idx = static_cast<std::size_t>(i);
        if (arg == "-c" && i + 1 < std::ssize(result.args)
            && is_source_file(result.args[idx + 1])) {
            source_index = idx + 1;
            break;
        }
        if (!arg.empty() && arg.front() != '-' && is_source_file(arg)) {
            source_index = idx;
            break;
        }
    }

    if (!source_index.has_value()) {
        return std::unexpected(Error::make("CompileCommandMissingSource",
                                           "Unable to locate source file in compile unit argv"));
    }

    result.file_path = result.args[*source_index];
    result.args.erase(result.args.begin() + static_cast<std::ptrdiff_t>(*source_index));
    return result;
}

struct FilePathContext
{
    std::string_view cwd;
    std::string_view file_path;
};

struct LocationContext
{
    const clang::SourceManager* source_manager = nullptr;
    clang::SourceLocation loc;
};

std::string normalize_file_path(const FilePathContext& context)
{
    std::filesystem::path path(std::string(context.file_path));
    if (!path.is_absolute()) {
        std::filesystem::path base(std::string(context.cwd));
        path = base / path;
    }
    path = path.lexically_normal();
    return sappp::common::normalize_path(path.string());
}

std::optional<ir::Location> make_location(const LocationContext& context)
{
    if (context.source_manager == nullptr || context.loc.isInvalid()) {
        return std::nullopt;
    }
    if (context.source_manager->isInSystemHeader(context.loc)) {
        return std::nullopt;
    }

    clang::SourceLocation spelling = context.source_manager->getSpellingLoc(context.loc);
    clang::PresumedLoc presumed = context.source_manager->getPresumedLoc(spelling);
    if (!presumed.isValid()) {
        return std::nullopt;
    }

    ir::Location location;
    location.file = sappp::common::normalize_path(presumed.getFilename());
    location.line = static_cast<int>(presumed.getLine());
    location.col = static_cast<int>(presumed.getColumn());
    return location;
}

std::string classify_stmt(const clang::Stmt* stmt)
{
    if (clang::isa<clang::ReturnStmt>(stmt)) {
        return "ret";
    }
    if (clang::isa<clang::CallExpr>(stmt) || clang::isa<clang::CXXMemberCallExpr>(stmt)
        || clang::isa<clang::CXXConstructExpr>(stmt)
        || clang::isa<clang::CXXOperatorCallExpr>(stmt)) {
        return "call";
    }
    if (const auto* bin_op = clang::dyn_cast<clang::BinaryOperator>(stmt)) {
        if (bin_op->isAssignmentOp()) {
            return "store";
        }
    }
    if (const auto* decl_stmt = clang::dyn_cast<clang::DeclStmt>(stmt)) {
        for (const auto* decl : decl_stmt->decls()) {
            if (clang::isa<clang::VarDecl>(decl)) {
                return "assign";
            }
        }
    }
    if (clang::isa<clang::DeclRefExpr>(stmt) || clang::isa<clang::MemberExpr>(stmt)) {
        return "load";
    }
    return "stmt";
}

std::vector<std::string> detect_sink_markers(const clang::Stmt* stmt)
{
    std::vector<std::string> markers;
    if (stmt == nullptr) {
        return markers;
    }

    std::vector<const clang::Stmt*> stack;
    stack.push_back(stmt);

    while (!stack.empty()) {
        const clang::Stmt* current = stack.back();
        stack.pop_back();
        if (const auto* bin_op = clang::dyn_cast<clang::BinaryOperator>(current)) {
            const auto opcode = bin_op->getOpcode();
            if (opcode == clang::BO_Div || opcode == clang::BO_Rem || opcode == clang::BO_DivAssign
                || opcode == clang::BO_RemAssign) {
                markers.emplace_back("div_or_mod_zero");
            }
        } else if (const auto* unary_op = clang::dyn_cast<clang::UnaryOperator>(current)) {
            if (unary_op->getOpcode() == clang::UO_Deref) {
                markers.emplace_back("deref");
            }
        } else if (clang::isa<clang::ArraySubscriptExpr>(current)) {
            markers.emplace_back("oob");
        }

        for (const auto* child : current->children()) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }

    std::ranges::sort(markers);
    auto unique_end = std::ranges::unique(markers);
    markers.erase(unique_end.begin(), markers.end());
    return markers;
}

[[nodiscard]] std::string build_function_uid(const clang::FunctionDecl* func_decl)
{
    std::string function_uid;
    llvm::SmallString<128> usr_buffer;
    if (clang::index::generateUSRForDecl(func_decl, usr_buffer)) {
        function_uid = func_decl->getQualifiedNameAsString();
    } else {
        function_uid = std::string(usr_buffer.str());
    }
    return function_uid;
}

[[nodiscard]] std::string build_mangled_name(const clang::FunctionDecl* func_decl,
                                             clang::MangleContext& mangle_context)
{
    std::string mangled_name;
    if (mangle_context.shouldMangleDeclName(func_decl)) {
        llvm::raw_string_ostream os(mangled_name);
        mangle_context.mangleName(func_decl, os);
    } else {
        mangled_name = func_decl->getNameAsString();
    }
    return mangled_name;
}

[[nodiscard]] std::vector<const clang::CFGBlock*> collect_blocks(const clang::CFG& cfg)
{
    std::vector<const clang::CFGBlock*> blocks;
    blocks.reserve(cfg.getNumBlockIDs());
    for (const auto* block : cfg) {
        if (block != nullptr) {
            blocks.push_back(block);
        }
    }

    std::ranges::stable_sort(blocks, [](const clang::CFGBlock* a, const clang::CFGBlock* b) {
        return a->getBlockID() < b->getBlockID();
    });

    return blocks;
}

[[nodiscard]] std::unordered_map<const clang::CFGBlock*, std::string>
assign_block_ids(const std::vector<const clang::CFGBlock*>& blocks)
{
    std::unordered_map<const clang::CFGBlock*, std::string> block_ids;
    block_ids.reserve(blocks.size());
    for (auto [i, block] : std::views::enumerate(blocks)) {
        block_ids[block] = "B" + std::to_string(i);
    }
    return block_ids;
}

void append_source_entry(std::vector<SourceMapEntryKey>& entries,
                         std::string_view function_uid,
                         std::string_view block_id,
                         std::string_view inst_id,
                         const ir::Location& loc)
{
    nlohmann::json entry = {
        {       "ir_ref",
         {{"function_uid", std::string(function_uid)},
         {"block_id", std::string(block_id)},
         {"inst_id", std::string(inst_id)}}      },
        { "spelling_loc",                     loc},
        {"expansion_loc",                     loc},
        {  "macro_stack", nlohmann::json::array()}
    };
    entries.push_back(
        {std::string(function_uid), std::string(block_id), std::string(inst_id), entry});
}

void append_instruction(ir::BasicBlock& nir_block,
                        std::string_view function_uid,
                        std::string_view block_id,
                        ir::Instruction inst,
                        std::vector<SourceMapEntryKey>& source_entries)
{
    if (inst.src.has_value()) {
        append_source_entry(source_entries, function_uid, block_id, inst.id, *inst.src);
    }
    nir_block.insts.push_back(std::move(inst));
}

struct BlockInstructionContext
{
    const clang::CFGBlock* entry_block = nullptr;
    const clang::FunctionDecl* func_decl = nullptr;
    const clang::SourceManager* source_manager = nullptr;
    const std::string* function_uid = nullptr;
    const std::string* block_id = nullptr;
    std::vector<SourceMapEntryKey>* source_entries = nullptr;
    ir::BasicBlock* nir_block = nullptr;
};

void append_entry_block_check(const clang::CFGBlock* block,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    if (block != context.entry_block) {
        return;
    }

    ir::Instruction ub_check;
    ub_check.id = "I" + std::to_string(inst_index++);
    ub_check.op = "ub.check";
    ub_check.args = {nlohmann::json("UB.DivZero"), nlohmann::json(true)};
    ub_check.src = make_location(
        {.source_manager = context.source_manager, .loc = context.func_decl->getBeginLoc()});
    append_instruction(*context.nir_block,
                       *context.function_uid,
                       *context.block_id,
                       std::move(ub_check),
                       *context.source_entries);
}

void append_stmt_instructions(const clang::CFGBlock* block,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    for (const auto& element : *block) {
        if (const auto stmt_elem = element.getAs<clang::CFGStmt>()) {
            const clang::Stmt* stmt = stmt_elem->getStmt();
            ir::Instruction inst;
            inst.id = "I" + std::to_string(inst_index++);
            inst.op = classify_stmt(stmt);
            inst.src = make_location(
                {.source_manager = context.source_manager, .loc = stmt->getBeginLoc()});
            append_instruction(*context.nir_block,
                               *context.function_uid,
                               *context.block_id,
                               std::move(inst),
                               *context.source_entries);
            for (const auto& sink_kind : detect_sink_markers(stmt)) {
                ir::Instruction sink_inst;
                sink_inst.id = "I" + std::to_string(inst_index++);
                sink_inst.op = "sink.marker";
                sink_inst.args = {nlohmann::json(sink_kind)};
                sink_inst.src = make_location(
                    {.source_manager = context.source_manager, .loc = stmt->getBeginLoc()});
                append_instruction(*context.nir_block,
                                   *context.function_uid,
                                   *context.block_id,
                                   std::move(sink_inst),
                                   *context.source_entries);
            }
        }
    }
}

void append_terminator_instruction(const clang::CFGBlock* block,
                                   const BlockInstructionContext& context,
                                   int& inst_index)
{
    const clang::Stmt* terminator = block->getTerminatorStmt();
    if (terminator == nullptr || clang::isa<clang::ReturnStmt>(terminator)) {
        return;
    }

    ir::Instruction inst;
    inst.id = "I" + std::to_string(inst_index++);
    inst.op = "branch";
    inst.src =
        make_location({.source_manager = context.source_manager, .loc = terminator->getBeginLoc()});
    append_instruction(*context.nir_block,
                       *context.function_uid,
                       *context.block_id,
                       std::move(inst),
                       *context.source_entries);
}

void append_block_instructions(const clang::CFGBlock* block, const BlockInstructionContext& context)
{
    int inst_index = 0;
    append_entry_block_check(block, context, inst_index);
    append_stmt_instructions(block, context, inst_index);
    append_terminator_instruction(block, context, inst_index);
}

void append_block_edges(const clang::CFGBlock* block,
                        const std::unordered_map<const clang::CFGBlock*, std::string>& block_ids,
                        std::vector<ir::Edge>& edges)
{
    int succ_index = 0;
    for (const auto& succ : block->succs()) {
        const clang::CFGBlock* succ_block = succ.getReachableBlock();
        if (succ_block == nullptr) {
            ++succ_index;
            continue;
        }
        ir::Edge edge;
        edge.from = block_ids.at(block);
        edge.to = block_ids.at(succ_block);
        edge.kind = "succ" + std::to_string(succ_index++);
        edges.push_back(std::move(edge));
    }
}

[[nodiscard]] bool edge_less(const ir::Edge& a, const ir::Edge& b);

[[nodiscard]] ir::Cfg build_nir_cfg(const clang::CFG& cfg,
                                    const clang::FunctionDecl* func_decl,
                                    const clang::SourceManager& source_manager,
                                    const std::string& function_uid,
                                    std::vector<SourceMapEntryKey>& source_entries)
{
    auto blocks = collect_blocks(cfg);
    auto block_ids = assign_block_ids(blocks);

    const clang::CFGBlock* entry_block = &cfg.getEntry();
    ir::Cfg nir_cfg;
    nir_cfg.entry = block_ids.at(entry_block);

    std::vector<ir::Edge> edges;
    edges.reserve(blocks.size());

    for (const auto* block : blocks) {
        ir::BasicBlock nir_block;
        nir_block.id = block_ids.at(block);
        BlockInstructionContext context{.entry_block = entry_block,
                                        .func_decl = func_decl,
                                        .source_manager = &source_manager,
                                        .function_uid = &function_uid,
                                        .block_id = &nir_block.id,
                                        .source_entries = &source_entries,
                                        .nir_block = &nir_block};
        append_block_instructions(block, context);
        nir_cfg.blocks.push_back(std::move(nir_block));
        append_block_edges(block, block_ids, edges);
    }

    std::ranges::stable_sort(nir_cfg.blocks, [](const ir::BasicBlock& a, const ir::BasicBlock& b) {
        return a.id < b.id;
    });

    std::ranges::stable_sort(edges, edge_less);
    nir_cfg.edges = std::move(edges);
    return nir_cfg;
}

[[nodiscard]] std::optional<ir::FunctionDef>
build_function_def(const clang::FunctionDecl* func_decl,
                   clang::ASTContext& context,
                   clang::MangleContext& mangle_context,
                   std::vector<SourceMapEntryKey>& source_entries)
{
    if (func_decl == nullptr || !func_decl->hasBody()) {
        return std::nullopt;
    }

    const auto& source_manager = context.getSourceManager();
    if (!source_manager.isWrittenInMainFile(func_decl->getLocation())) {
        return std::nullopt;
    }

    std::string function_uid = build_function_uid(func_decl);
    std::string mangled_name = build_mangled_name(func_decl, mangle_context);

    std::unique_ptr<clang::CFG> cfg =
        clang::CFG::buildCFG(func_decl, func_decl->getBody(), &context, clang::CFG::BuildOptions());
    if (!cfg) {
        return std::nullopt;
    }

    ir::Cfg nir_cfg = build_nir_cfg(*cfg, func_decl, source_manager, function_uid, source_entries);

    ir::FunctionDef nir_func;
    nir_func.function_uid = std::move(function_uid);
    nir_func.mangled_name = std::move(mangled_name);
    nir_func.cfg = std::move(nir_cfg);
    return nir_func;
}

class NirBuilder
{
public:
    NirBuilder() = default;

    void build(clang::ASTContext& context)
    {
        auto mangle_context = std::unique_ptr<clang::MangleContext>(context.createMangleContext());
        for (const auto* decl : context.getTranslationUnitDecl()->decls()) {
            const auto* func_decl = clang::dyn_cast<clang::FunctionDecl>(decl);
            if (func_decl == nullptr) {
                continue;
            }
            auto nir_func =
                build_function_def(func_decl, context, *mangle_context, m_source_entries);
            if (nir_func) {
                m_functions.push_back(std::move(*nir_func));
            }
        }
    }

    std::vector<ir::FunctionDef> take_functions() { return std::move(m_functions); }
    std::vector<SourceMapEntryKey> take_source_entries() { return std::move(m_source_entries); }

private:
    std::vector<ir::FunctionDef> m_functions;
    std::vector<SourceMapEntryKey> m_source_entries;
};

class NirASTConsumer final : public clang::ASTConsumer
{
public:
    explicit NirASTConsumer(NirBuilder& builder)
        : m_builder(&builder)
    {}

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        if (m_builder != nullptr) {
            m_builder->build(context);
        }
    }

private:
    NirBuilder* m_builder = nullptr;
};

class NirFrontendAction final : public clang::ASTFrontendAction
{
public:
    explicit NirFrontendAction(NirBuilder& builder)
        : m_builder(&builder)
    {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                          llvm::StringRef input_file) override
    {
        (void)compiler;
        (void)input_file;
        return std::make_unique<NirASTConsumer>(*m_builder);
    }

private:
    NirBuilder* m_builder = nullptr;
};

class NirFrontendActionFactory final : public clang::tooling::FrontendActionFactory
{
public:
    explicit NirFrontendActionFactory(NirBuilder& builder)
        : m_builder(&builder)
    {}

    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<NirFrontendAction>(*m_builder);
    }

private:
    NirBuilder* m_builder = nullptr;
};

[[nodiscard]] bool edge_less(const ir::Edge& a, const ir::Edge& b)
{
    if (a.from != b.from) {
        return a.from < b.from;
    }
    if (a.to != b.to) {
        return a.to < b.to;
    }
    return a.kind < b.kind;
}

[[nodiscard]] sappp::VoidResult
validate_build_snapshot_schema(const nlohmann::json& build_snapshot,
                               const std::filesystem::path& schema_dir)
{
    return sappp::common::validate_json(build_snapshot,
                                        (schema_dir / "build_snapshot.v1.schema.json").string());
}

struct UnitAnalysisResult
{
    std::string tu_id;
    std::vector<ir::FunctionDef> functions;
    std::vector<SourceMapEntryKey> source_entries;
};

[[nodiscard]] sappp::Result<UnitAnalysisResult> analyze_compile_unit(const nlohmann::json& unit)
{
    std::string tu_id = unit.at("tu_id").get<std::string>();
    auto command_result = extract_compile_command(unit);
    if (!command_result) {
        return std::unexpected(command_result.error());
    }

    auto command = *command_result;
    std::string cwd = unit.at("cwd").get<std::string>();
    std::string file_path = normalize_file_path({.cwd = cwd, .file_path = command.file_path});

    std::filesystem::path file_fs(file_path);
    if (!std::filesystem::exists(file_fs)) {
        return std::unexpected(
            Error::make("SourceFileNotFound", "Source file not found: " + file_path));
    }

    clang::tooling::FixedCompilationDatabase comp_db(cwd, command.args);
    clang::tooling::ClangTool tool(comp_db, {file_path});

    NirBuilder builder;
    NirFrontendActionFactory factory(builder);

    if (tool.run(&factory) != 0) {
        return std::unexpected(
            Error::make("ClangToolFailed", "ClangTool failed for source file: " + file_path));
    }

    UnitAnalysisResult result;
    result.tu_id = std::move(tu_id);
    result.functions = builder.take_functions();
    result.source_entries = builder.take_source_entries();
    return result;
}

void sort_function_contents(ir::FunctionDef& func)
{
    std::ranges::stable_sort(func.cfg.blocks, [](const ir::BasicBlock& a, const ir::BasicBlock& b) {
        return a.id < b.id;
    });
    for (auto& block : func.cfg.blocks) {
        std::ranges::stable_sort(
            block.insts,
            [](const ir::Instruction& a, const ir::Instruction& b) { return a.id < b.id; });
    }
    std::ranges::stable_sort(func.cfg.edges, edge_less);
}

void sort_functions(std::vector<ir::FunctionDef>& functions)
{
    std::ranges::stable_sort(functions, [](const ir::FunctionDef& a, const ir::FunctionDef& b) {
        return a.function_uid < b.function_uid;
    });
    for (auto& func : functions) {
        sort_function_contents(func);
    }
}

void sort_source_entries(std::vector<SourceMapEntryKey>& source_entries)
{
    std::ranges::stable_sort(source_entries,
                             [](const SourceMapEntryKey& a, const SourceMapEntryKey& b) {
                                 if (a.function_uid != b.function_uid) {
                                     return a.function_uid < b.function_uid;
                                 }
                                 if (a.block_id != b.block_id) {
                                     return a.block_id < b.block_id;
                                 }
                                 return a.inst_id < b.inst_id;
                             });
}

[[nodiscard]] sappp::Result<std::string> compute_tu_id(std::vector<std::string> tu_ids)
{
    if (tu_ids.size() == 1) {
        return tu_ids.front();
    }

    std::ranges::stable_sort(tu_ids);
    nlohmann::json tu_array = tu_ids;
    auto tu_id_result = sappp::canonical::hash_canonical(tu_array);
    if (!tu_id_result) {
        return std::unexpected(tu_id_result.error());
    }
    return *tu_id_result;
}

[[nodiscard]] sappp::Result<nlohmann::json> build_nir_json(const nlohmann::json& build_snapshot,
                                                           std::string_view tu_id,
                                                           std::vector<ir::FunctionDef> functions,
                                                           const std::filesystem::path& schema_dir)
{
    ir::Nir nir;
    nir.schema_version = "nir.v1";
    nir.tool = {
        {    "name",         "sappp"},
        { "version", sappp::kVersion},
        {"build_id", sappp::kBuildId}
    };
    nir.generated_at = current_time_utc();
    nir.tu_id = std::string(tu_id);
    nir.semantics_version = sappp::kSemanticsVersion;
    nir.proof_system_version = sappp::kProofSystemVersion;
    nir.profile_version = sappp::kProfileVersion;
    if (build_snapshot.contains("input_digest")) {
        nir.input_digest = build_snapshot.at("input_digest").get<std::string>();
    }
    nir.functions = std::move(functions);

    nlohmann::json nir_json = nir;
    if (auto result =
            sappp::common::validate_json(nir_json, (schema_dir / "nir.v1.schema.json").string());
        !result) {
        return std::unexpected(result.error());
    }
    return nir_json;
}

[[nodiscard]] sappp::Result<nlohmann::json>
build_source_map_json(const nlohmann::json& build_snapshot,
                      std::string_view tu_id,
                      const std::vector<SourceMapEntryKey>& source_entries,
                      const std::filesystem::path& schema_dir)
{
    nlohmann::json source_map_json = {
        {"schema_version",                                                                  "source_map.v1"},
        {          "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {  "generated_at",                                                               current_time_utc()},
        {         "tu_id",                                                               std::string(tu_id)},
        {       "entries",                                                          nlohmann::json::array()}
    };

    auto& entries_array = source_map_json["entries"];
    for (const auto& entry : source_entries) {
        entries_array.push_back(entry.entry);
    }

    if (build_snapshot.contains("input_digest")) {
        source_map_json["input_digest"] = build_snapshot.at("input_digest").get<std::string>();
    }

    if (auto result =
            sappp::common::validate_json(source_map_json,
                                         (schema_dir / "source_map.v1.schema.json").string());
        !result) {
        return std::unexpected(result.error());
    }
    return source_map_json;
}

}  // namespace

FrontendClang::FrontendClang(std::string schema_dir)
    : m_schema_dir(std::move(schema_dir))
{}

sappp::Result<FrontendResult> FrontendClang::analyze(const nlohmann::json& build_snapshot) const
{
    std::filesystem::path schema_dir(m_schema_dir);
    if (auto result = validate_build_snapshot_schema(build_snapshot, schema_dir); !result) {
        return std::unexpected(result.error());
    }

    const auto& compile_units = build_snapshot.at("compile_units");
    std::vector<ir::FunctionDef> functions;
    std::vector<SourceMapEntryKey> source_entries;
    std::vector<std::string> tu_ids;

    for (const auto& unit : compile_units) {
        auto unit_result = analyze_compile_unit(unit);
        if (!unit_result) {
            return std::unexpected(unit_result.error());
        }
        tu_ids.push_back(unit_result->tu_id);

        auto unit_functions = std::move(unit_result->functions);
        functions.insert(functions.end(),
                         std::make_move_iterator(unit_functions.begin()),
                         std::make_move_iterator(unit_functions.end()));

        auto unit_entries = std::move(unit_result->source_entries);
        source_entries.insert(source_entries.end(),
                              std::make_move_iterator(unit_entries.begin()),
                              std::make_move_iterator(unit_entries.end()));
    }

    if (functions.empty()) {
        return std::unexpected(Error::make("NirEmpty", "No functions found to emit NIR"));
    }

    sort_functions(functions);
    sort_source_entries(source_entries);

    auto tu_id_result = compute_tu_id(std::move(tu_ids));
    if (!tu_id_result) {
        return std::unexpected(tu_id_result.error());
    }
    const auto& tu_id = *tu_id_result;

    auto nir_json = build_nir_json(build_snapshot, tu_id, std::move(functions), schema_dir);
    if (!nir_json) {
        return std::unexpected(nir_json.error());
    }

    auto source_map_json = build_source_map_json(build_snapshot, tu_id, source_entries, schema_dir);
    if (!source_map_json) {
        return std::unexpected(source_map_json.error());
    }

    return FrontendResult{.nir = std::move(*nir_json), .source_map = std::move(*source_map_json)};
}

}  // namespace sappp::frontend_clang
