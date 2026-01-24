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
#include <filesystem>
#include <functional>
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
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
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

constexpr std::string_view kDeterministicGeneratedAt = "1970-01-01T00:00:00Z";

[[nodiscard]] std::string generated_at_from_snapshot(const nlohmann::json& build_snapshot)
{
    if (build_snapshot.contains("generated_at") && build_snapshot.at("generated_at").is_string()) {
        return build_snapshot.at("generated_at").get<std::string>();
    }
    return std::string(kDeterministicGeneratedAt);
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

    while (!result.args.empty() && !result.args.front().empty()
           && result.args.front().front() != '-') {
        result.args.erase(result.args.begin());
        if (result.args.empty() || result.args.front().empty()
            || result.args.front().front() == '-') {
            break;
        }
    }
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

std::optional<ir::Location> make_decl_location(const clang::Decl* decl,
                                               const clang::SourceManager* source_manager)
{
    if (decl == nullptr) {
        return std::nullopt;
    }
    return make_location({.source_manager = source_manager, .loc = decl->getLocation()});
}

std::string describe_var(const clang::VarDecl* var_decl)
{
    if (var_decl == nullptr) {
        return "var";
    }
    if (!var_decl->getName().empty()) {
        return var_decl->getName().str();
    }
    return var_decl->getType().getAsString();
}

std::string build_var_id(const clang::VarDecl* var_decl, const clang::SourceManager* source_manager)
{
    if (var_decl == nullptr) {
        return "var";
    }
    llvm::SmallString<128> usr_buffer;
    if (!clang::index::generateUSRForDecl(var_decl, usr_buffer)) {
        return std::string(usr_buffer.str());
    }

    std::string fallback = describe_var(var_decl);
    if (auto loc = make_decl_location(var_decl, source_manager)) {
        fallback += "@";
        fallback += loc->file;
        fallback += ":";
        fallback += std::to_string(loc->line);
        fallback += ":";
        fallback += std::to_string(loc->col);
    }
    return fallback;
}

bool is_local_auto_var(const clang::VarDecl* var_decl)
{
    if (var_decl == nullptr) {
        return false;
    }
    return var_decl->hasLocalStorage() && !var_decl->isStaticLocal();
}

std::optional<std::string> extract_var_id_from_expr(const clang::Expr* expr,
                                                    const clang::SourceManager* source_manager)
{
    if (expr == nullptr) {
        return std::nullopt;
    }
    const clang::Expr* node = expr->IgnoreParenImpCasts();
    if (const auto* decl_ref = clang::dyn_cast<clang::DeclRefExpr>(node)) {
        if (const auto* var_decl = clang::dyn_cast<clang::VarDecl>(decl_ref->getDecl())) {
            if (!is_local_auto_var(var_decl)) {
                return std::nullopt;
            }
            return build_var_id(var_decl, source_manager);
        }
    }
    if (const auto* member_expr = clang::dyn_cast<clang::MemberExpr>(node)) {
        if (const auto* field_decl =
                clang::dyn_cast<clang::FieldDecl>(member_expr->getMemberDecl())) {
            return field_decl->getQualifiedNameAsString();
        }
    }
    return std::nullopt;
}

std::string describe_temporary(const clang::Expr* expr)
{
    if (expr == nullptr) {
        return "temporary";
    }
    return std::string("temporary:") + expr->getType().getAsString();
}

std::string describe_ctor(const clang::CXXConstructorDecl* ctor_decl)
{
    if (ctor_decl == nullptr) {
        return "ctor";
    }
    return ctor_decl->getQualifiedNameAsString();
}

std::string describe_catch_type(const clang::CXXCatchStmt* catch_stmt)
{
    if (catch_stmt == nullptr || catch_stmt->getCaughtType().isNull()) {
        return "...";
    }
    return catch_stmt->getCaughtType().getAsString();
}

std::string describe_receiver_type(const clang::CXXMemberCallExpr* call_expr)
{
    if (call_expr == nullptr) {
        return "receiver";
    }
    const auto* receiver = call_expr->getImplicitObjectArgument();
    if (receiver == nullptr) {
        return "receiver";
    }
    return receiver->getType().getAsString();
}

std::string build_method_uid(const clang::CXXMethodDecl* method_decl)
{
    if (method_decl == nullptr) {
        return "method";
    }
    llvm::SmallString<128> usr_buffer;
    if (clang::index::generateUSRForDecl(method_decl, usr_buffer)) {
        return method_decl->getQualifiedNameAsString();
    }
    return std::string(usr_buffer.str());
}

std::string build_method_signature(const clang::CXXMethodDecl* method_decl)
{
    if (method_decl == nullptr) {
        return "signature";
    }
    return method_decl->getType().getAsString();
}

bool method_overrides(const clang::CXXMethodDecl* candidate,
                      const clang::CXXMethodDecl* base_method)
{
    if (candidate == nullptr || base_method == nullptr) {
        return false;
    }
    const auto* candidate_canonical = candidate->getCanonicalDecl();
    const auto* base_canonical = base_method->getCanonicalDecl();
    if (candidate_canonical == base_canonical) {
        return true;
    }
    return std::ranges::any_of(candidate->overridden_methods(),
                               [&](const clang::CXXMethodDecl* overridden_method) {
                                   return method_overrides(overridden_method, base_method);
                               });
}

std::vector<std::string>
collect_vcall_candidate_methods(const clang::CXXMethodDecl* base_method,
                                const std::vector<const clang::CXXMethodDecl*>& all_methods)
{
    std::vector<std::string> methods;
    if (base_method == nullptr) {
        return methods;
    }

    for (const auto* method_decl : all_methods) {
        if (method_overrides(method_decl, base_method)) {
            methods.push_back(build_method_uid(method_decl));
        }
    }
    if (methods.empty()) {
        methods.push_back(build_method_uid(base_method));
    }

    std::ranges::stable_sort(methods);
    auto unique_end = std::ranges::unique(methods);
    methods.erase(unique_end.begin(), methods.end());
    return methods;
}

void traverse_stmt_preorder(const clang::Stmt* stmt,
                            const std::function<void(const clang::Stmt*)>& visitor)
{
    if (stmt == nullptr) {
        return;
    }
    visitor(stmt);
    for (const auto* child : stmt->children()) {
        if (child != nullptr) {
            traverse_stmt_preorder(child, visitor);
        }
    }
}

bool is_nothrow_function_type(const clang::FunctionProtoType* proto)
{
    if (proto == nullptr) {
        return false;
    }
    return proto->canThrow() == clang::CT_Cannot;
}

bool call_can_throw(const clang::CallExpr* call_expr)
{
    if (call_expr == nullptr) {
        return false;
    }
    const auto* callee = call_expr->getDirectCallee();
    if (callee == nullptr) {
        return true;
    }
    const auto* proto = callee->getType()->getAs<clang::FunctionProtoType>();
    return !is_nothrow_function_type(proto);
}

bool ctor_can_throw(const clang::CXXConstructExpr* ctor_expr)
{
    if (ctor_expr == nullptr) {
        return false;
    }
    const auto* ctor_decl = ctor_expr->getConstructor();
    if (ctor_decl == nullptr) {
        return true;
    }
    const auto* proto = ctor_decl->getType()->getAs<clang::FunctionProtoType>();
    return !is_nothrow_function_type(proto);
}

const clang::Expr* strip_parens(const clang::Expr* expr)
{
    return expr != nullptr ? expr->IgnoreParenImpCasts() : nullptr;
}

std::optional<std::string> extract_string_literal(const clang::Expr* expr)
{
    const auto* stripped = strip_parens(expr);
    const auto* literal = clang::dyn_cast_or_null<clang::StringLiteral>(stripped);
    if (literal == nullptr) {
        return std::nullopt;
    }
    return literal->getString().str();
}

std::optional<bool> extract_bool_literal(const clang::Expr* expr)
{
    const auto* stripped = strip_parens(expr);
    if (const auto* bool_lit = clang::dyn_cast_or_null<clang::CXXBoolLiteralExpr>(stripped)) {
        return bool_lit->getValue();
    }
    if (const auto* int_lit = clang::dyn_cast_or_null<clang::IntegerLiteral>(stripped)) {
        return !int_lit->getValue().isZero();
    }
    return std::nullopt;
}

std::optional<std::vector<nlohmann::json>>
extract_sappp_check_args(const clang::CallExpr* call_expr)
{
    if (call_expr == nullptr || call_expr->getNumArgs() < 2) {
        return std::nullopt;
    }
    auto kind = extract_string_literal(call_expr->getArg(0));
    if (!kind) {
        return std::nullopt;
    }
    auto predicate = extract_bool_literal(call_expr->getArg(1));
    if (!predicate) {
        return std::nullopt;
    }
    std::vector<nlohmann::json> args;
    args.reserve(2);
    args.push_back(*kind);
    args.push_back(*predicate);
    return args;
}

struct ClassifiedStmt
{
    std::string op;
    std::vector<nlohmann::json> args;
};

std::optional<ClassifiedStmt> classify_return_stmt(const clang::Stmt* stmt)
{
    if (clang::isa<clang::ReturnStmt>(stmt)) {
        return ClassifiedStmt{.op = "ret", .args = {}};
    }
    return std::nullopt;
}

std::optional<ClassifiedStmt> classify_throw_stmt(const clang::Stmt* stmt)
{
    const auto* throw_expr = clang::dyn_cast<clang::CXXThrowExpr>(stmt);
    if (throw_expr == nullptr) {
        return std::nullopt;
    }
    if (throw_expr->getSubExpr() == nullptr) {
        return ClassifiedStmt{.op = "resume", .args = {}};
    }
    return ClassifiedStmt{
        .op = "throw",
        .args = {nlohmann::json(throw_expr->getSubExpr()->getType().getAsString())}};
}

std::optional<ClassifiedStmt> classify_catch_stmt(const clang::Stmt* stmt)
{
    const auto* catch_stmt = clang::dyn_cast<clang::CXXCatchStmt>(stmt);
    if (catch_stmt == nullptr) {
        return std::nullopt;
    }
    return ClassifiedStmt{.op = "landingpad",
                          .args = {nlohmann::json(describe_catch_type(catch_stmt))}};
}

std::optional<ClassifiedStmt> classify_call_stmt(const clang::Stmt* stmt)
{
    if (const auto* call_expr = clang::dyn_cast<clang::CallExpr>(stmt)) {
        const auto* callee = call_expr->getDirectCallee();
        if (callee != nullptr && callee->getNameAsString() == "sappp_check") {
            if (auto args = extract_sappp_check_args(call_expr)) {
                return ClassifiedStmt{.op = "ub.check", .args = std::move(*args)};
            }
        }
        return ClassifiedStmt{.op = call_can_throw(call_expr) ? "invoke" : "call", .args = {}};
    }
    if (const auto* ctor_expr = clang::dyn_cast<clang::CXXConstructExpr>(stmt)) {
        return ClassifiedStmt{.op = ctor_can_throw(ctor_expr) ? "invoke" : "call", .args = {}};
    }
    return std::nullopt;
}

std::optional<ClassifiedStmt> classify_store_stmt(const clang::Stmt* stmt,
                                                  const clang::SourceManager* source_manager)
{
    const auto* bin_op = clang::dyn_cast<clang::BinaryOperator>(stmt);
    if (bin_op != nullptr && bin_op->isAssignmentOp()) {
        std::vector<nlohmann::json> args;
        if (auto var_id = extract_var_id_from_expr(bin_op->getLHS(), source_manager)) {
            args.emplace_back(std::move(*var_id));
        }
        return ClassifiedStmt{.op = "store", .args = std::move(args)};
    }
    return std::nullopt;
}

std::optional<ClassifiedStmt> classify_assign_stmt(const clang::Stmt* stmt,
                                                   const clang::SourceManager* source_manager)
{
    const auto* decl_stmt = clang::dyn_cast<clang::DeclStmt>(stmt);
    if (decl_stmt == nullptr) {
        return std::nullopt;
    }
    for (const auto* decl : decl_stmt->decls()) {
        const auto* var_decl = clang::dyn_cast<clang::VarDecl>(decl);
        if (var_decl == nullptr || !is_local_auto_var(var_decl)) {
            continue;
        }
        std::vector<nlohmann::json> args;
        args.emplace_back(build_var_id(var_decl, source_manager));
        args.emplace_back(var_decl->hasInit() ? "init" : "uninit");
        return ClassifiedStmt{.op = "assign", .args = std::move(args)};
    }
    return std::nullopt;
}

std::optional<ClassifiedStmt> classify_load_stmt(const clang::Stmt* stmt,
                                                 const clang::SourceManager* source_manager)
{
    if (clang::isa<clang::DeclRefExpr>(stmt) || clang::isa<clang::MemberExpr>(stmt)) {
        std::vector<nlohmann::json> args;
        if (const auto* expr = clang::dyn_cast<clang::Expr>(stmt)) {
            if (auto var_id = extract_var_id_from_expr(expr, source_manager)) {
                args.emplace_back(std::move(*var_id));
            }
        }
        return ClassifiedStmt{.op = "load", .args = std::move(args)};
    }
    return std::nullopt;
}

ClassifiedStmt classify_stmt(const clang::Stmt* stmt, const clang::SourceManager* source_manager)
{
    if (stmt == nullptr) {
        return ClassifiedStmt{.op = "stmt", .args = {}};
    }
    if (auto classified = classify_return_stmt(stmt)) {
        return *classified;
    }
    if (auto classified = classify_throw_stmt(stmt)) {
        return *classified;
    }
    if (auto classified = classify_catch_stmt(stmt)) {
        return *classified;
    }
    if (auto classified = classify_call_stmt(stmt)) {
        return *classified;
    }
    if (auto classified = classify_store_stmt(stmt, source_manager)) {
        return *classified;
    }
    if (auto classified = classify_assign_stmt(stmt, source_manager)) {
        return *classified;
    }
    if (auto classified = classify_load_stmt(stmt, source_manager)) {
        return *classified;
    }
    return ClassifiedStmt{.op = "stmt", .args = {}};
}

struct SinkMarker
{
    std::string kind;
    std::vector<std::string> extra_args;
};

/**
 * Detects "sink" markers in the given Clang statement subtree.
 *
 * This function deliberately performs a conservative over-approximation:
 * it treats all potentially dangerous operations (such as division/modulo,
 * pointer dereferences, and array subscripts) as possible undefined
 * behavior (UB) sinks, regardless of whether they are actually problematic
 * in the concrete program. As a result, the returned marker set may contain
 * many false positives.
 *
 * Downstream analyses are expected to refine these results and filter out
 * false positives using additional semantic information (e.g., range
 * analysis, alias information, or domain-specific constraints). The role of
 * this function is to provide a simple, conservative starting point rather
 * than a precise classification of UB.
 */
std::vector<SinkMarker> detect_sink_markers(const clang::Stmt* stmt,
                                            const clang::SourceManager* source_manager)
{
    std::vector<SinkMarker> markers;
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
                markers.push_back(SinkMarker{.kind = "div0"});
            }
        } else if (const auto* call_expr = clang::dyn_cast<clang::CallExpr>(current)) {
            const auto* callee = call_expr->getDirectCallee();
            if (callee != nullptr && callee->getNameAsString() == "sappp_sink") {
                if (call_expr->getNumArgs() > 0) {
                    if (auto kind = extract_string_literal(call_expr->getArg(0))) {
                        SinkMarker marker{.kind = std::move(*kind), .extra_args = {}};
                        if (call_expr->getNumArgs() > 1) {
                            const auto* arg_expr =
                                clang::dyn_cast<clang::Expr>(call_expr->getArg(1));
                            if (auto var_id = extract_var_id_from_expr(arg_expr, source_manager)) {
                                marker.extra_args.push_back(std::move(*var_id));
                            }
                        }
                        markers.push_back(std::move(marker));
                    }
                }
            }
        } else if (const auto* unary_op = clang::dyn_cast<clang::UnaryOperator>(current)) {
            if (unary_op->getOpcode() == clang::UO_Deref) {
                markers.push_back(SinkMarker{.kind = "null"});
            }
        } else if (clang::isa<clang::ArraySubscriptExpr>(current)) {
            markers.push_back(SinkMarker{.kind = "oob"});
        }

        for (const auto* child : current->children()) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }

    std::ranges::stable_sort(markers, [](const SinkMarker& a, const SinkMarker& b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        return a.extra_args < b.extra_args;
    });
    auto unique_end = std::ranges::unique(markers, [](const SinkMarker& a, const SinkMarker& b) {
        return a.kind == b.kind && a.extra_args == b.extra_args;
    });
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

struct VCallContext
{
    std::vector<ir::VCallCandidateSet>* candidate_sets = nullptr;
    const std::vector<const clang::CXXMethodDecl*>* all_methods = nullptr;
    int next_id = 0;
};

struct BlockInstructionContext
{
    const clang::CFGBlock* entry_block = nullptr;
    const clang::FunctionDecl* func_decl = nullptr;
    const clang::SourceManager* source_manager = nullptr;
    const std::string* function_uid = nullptr;
    const std::string* block_id = nullptr;
    std::vector<SourceMapEntryKey>* source_entries = nullptr;
    ir::BasicBlock* nir_block = nullptr;
    VCallContext* vcall_context = nullptr;
};

enum class ExceptionFlowKind { kNone, kInvoke, kThrow };

bool stmt_has_throw(const clang::Stmt* stmt)
{
    bool found = false;
    traverse_stmt_preorder(stmt, [&](const clang::Stmt* node) {
        if (!found && clang::isa<clang::CXXThrowExpr>(node)) {
            found = true;
        }
    });
    return found;
}

bool stmt_has_throwing_call(const clang::Stmt* stmt)
{
    bool found = false;
    traverse_stmt_preorder(stmt, [&](const clang::Stmt* node) {
        if (found) {
            return;
        }
        if (const auto* call_expr = clang::dyn_cast<clang::CallExpr>(node)) {
            found = call_can_throw(call_expr);
            return;
        }
        if (const auto* ctor_expr = clang::dyn_cast<clang::CXXConstructExpr>(node)) {
            found = ctor_can_throw(ctor_expr);
        }
    });
    return found;
}

ExceptionFlowKind classify_exception_flow(const clang::CFGBlock* block)
{
    bool has_throw = false;
    bool has_invoke = false;

    for (const auto& element : *block) {
        if (const auto stmt_elem = element.getAs<clang::CFGStmt>()) {
            const clang::Stmt* stmt = stmt_elem->getStmt();
            if (!has_throw && stmt_has_throw(stmt)) {
                has_throw = true;
            }
            if (!has_invoke && stmt_has_throwing_call(stmt)) {
                has_invoke = true;
            }
        }
    }

    if (has_throw) {
        return ExceptionFlowKind::kThrow;
    }
    if (has_invoke) {
        return ExceptionFlowKind::kInvoke;
    }
    return ExceptionFlowKind::kNone;
}

void append_simple_instruction(const BlockInstructionContext& context,
                               int& inst_index,
                               std::string_view op,
                               std::vector<nlohmann::json> args,
                               std::optional<ir::Location> src)
{
    ir::Instruction inst;
    inst.id = "I" + std::to_string(inst_index++);
    inst.op = std::string(op);
    inst.args = std::move(args);
    inst.src = std::move(src);
    append_instruction(*context.nir_block,
                       *context.function_uid,
                       *context.block_id,
                       std::move(inst),
                       *context.source_entries);
}

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

void append_lifetime_begin_for_stmt(const clang::Stmt* stmt,
                                    const BlockInstructionContext& context,
                                    int& inst_index)
{
    if (stmt == nullptr) {
        return;
    }
    if (const auto* decl_stmt = clang::dyn_cast<clang::DeclStmt>(stmt)) {
        for (const auto* decl : decl_stmt->decls()) {
            const auto* var_decl = clang::dyn_cast<clang::VarDecl>(decl);
            if (var_decl == nullptr || !var_decl->hasLocalStorage() || var_decl->isStaticLocal()) {
                continue;
            }
            append_simple_instruction(context,
                                      inst_index,
                                      "lifetime.begin",
                                      {nlohmann::json(describe_var(var_decl))},
                                      make_decl_location(var_decl, context.source_manager));
        }
    }

    traverse_stmt_preorder(stmt, [&](const clang::Stmt* node) {
        const auto* temp_expr = clang::dyn_cast<clang::CXXBindTemporaryExpr>(node);
        if (temp_expr == nullptr) {
            return;
        }
        append_simple_instruction(context,
                                  inst_index,
                                  "lifetime.begin",
                                  {nlohmann::json(describe_temporary(temp_expr->getSubExpr()))},
                                  make_location({.source_manager = context.source_manager,
                                                 .loc = temp_expr->getBeginLoc()}));
    });
}

void append_ctor_instructions_for_stmt(const clang::Stmt* stmt,
                                       const BlockInstructionContext& context,
                                       int& inst_index)
{
    if (stmt == nullptr) {
        return;
    }
    traverse_stmt_preorder(stmt, [&](const clang::Stmt* node) {
        const auto* ctor_expr = clang::dyn_cast<clang::CXXConstructExpr>(node);
        if (ctor_expr == nullptr) {
            return;
        }
        const auto* ctor_decl = ctor_expr->getConstructor();
        const bool is_move = ctor_decl != nullptr && ctor_decl->isMoveConstructor();
        std::vector<nlohmann::json> args;
        args.emplace_back(describe_ctor(ctor_decl));
        if (is_move && ctor_expr->getNumArgs() > 0) {
            const auto* arg_expr = clang::dyn_cast<clang::Expr>(ctor_expr->getArg(0));
            if (auto var_id = extract_var_id_from_expr(arg_expr, context.source_manager)) {
                args.emplace_back(std::move(*var_id));
            }
        }
        append_simple_instruction(context,
                                  inst_index,
                                  is_move ? "move" : "ctor",
                                  std::move(args),
                                  make_location({.source_manager = context.source_manager,
                                                 .loc = ctor_expr->getBeginLoc()}));
    });
}

std::optional<ir::Instruction> build_vcall_instruction(const clang::Stmt* stmt,
                                                       const BlockInstructionContext& context,
                                                       int& inst_index)
{
    if (context.vcall_context == nullptr || context.vcall_context->candidate_sets == nullptr
        || context.vcall_context->all_methods == nullptr) {
        return std::nullopt;
    }

    const auto* call_expr = clang::dyn_cast<clang::CXXMemberCallExpr>(stmt);
    if (call_expr == nullptr) {
        return std::nullopt;
    }
    const auto* method_decl = call_expr->getMethodDecl();
    if (method_decl == nullptr || !method_decl->isVirtual()) {
        return std::nullopt;
    }

    const std::string candidate_id = "CS" + std::to_string(context.vcall_context->next_id++);
    ir::VCallCandidateSet candidate_set;
    candidate_set.id = candidate_id;
    candidate_set.methods =
        collect_vcall_candidate_methods(method_decl, *context.vcall_context->all_methods);
    context.vcall_context->candidate_sets->push_back(candidate_set);

    ir::Instruction inst;
    inst.id = "I" + std::to_string(inst_index++);
    inst.op = "vcall";
    inst.args = {nlohmann::json(describe_receiver_type(call_expr)),
                 nlohmann::json(candidate_id),
                 nlohmann::json(build_method_signature(method_decl))};
    inst.src =
        make_location({.source_manager = context.source_manager, .loc = call_expr->getBeginLoc()});
    return inst;
}

void append_cfg_stmt_element(const clang::CFGStmt& stmt_elem,
                             const BlockInstructionContext& context,
                             int& inst_index)
{
    const clang::Stmt* stmt = stmt_elem.getStmt();
    append_lifetime_begin_for_stmt(stmt, context, inst_index);
    if (auto vcall_inst = build_vcall_instruction(stmt, context, inst_index)) {
        append_instruction(*context.nir_block,
                           *context.function_uid,
                           *context.block_id,
                           std::move(*vcall_inst),
                           *context.source_entries);
        append_ctor_instructions_for_stmt(stmt, context, inst_index);
        for (const auto& marker : detect_sink_markers(stmt, context.source_manager)) {
            ir::Instruction sink_inst;
            sink_inst.id = "I" + std::to_string(inst_index++);
            sink_inst.op = "sink.marker";
            sink_inst.args = {nlohmann::json(marker.kind)};
            for (const auto& extra_arg : marker.extra_args) {
                sink_inst.args.push_back(nlohmann::json(extra_arg));
            }
            sink_inst.src = make_location(
                {.source_manager = context.source_manager,
                 .loc = stmt != nullptr ? stmt->getBeginLoc() : clang::SourceLocation()});
            append_instruction(*context.nir_block,
                               *context.function_uid,
                               *context.block_id,
                               std::move(sink_inst),
                               *context.source_entries);
        }
        return;
    }
    auto classified = classify_stmt(stmt, context.source_manager);
    ir::Instruction inst;
    inst.id = "I" + std::to_string(inst_index++);
    inst.op = std::move(classified.op);
    inst.args = std::move(classified.args);
    inst.src =
        make_location({.source_manager = context.source_manager, .loc = stmt->getBeginLoc()});
    append_instruction(*context.nir_block,
                       *context.function_uid,
                       *context.block_id,
                       std::move(inst),
                       *context.source_entries);
    append_ctor_instructions_for_stmt(stmt, context, inst_index);
    for (const auto& marker : detect_sink_markers(stmt, context.source_manager)) {
        ir::Instruction sink_inst;
        sink_inst.id = "I" + std::to_string(inst_index++);
        sink_inst.op = "sink.marker";
        sink_inst.args = {nlohmann::json(marker.kind)};
        for (const auto& extra_arg : marker.extra_args) {
            sink_inst.args.push_back(nlohmann::json(extra_arg));
        }
        sink_inst.src =
            make_location({.source_manager = context.source_manager, .loc = stmt->getBeginLoc()});
        append_instruction(*context.nir_block,
                           *context.function_uid,
                           *context.block_id,
                           std::move(sink_inst),
                           *context.source_entries);
    }
}

void append_lifetime_end_element(const clang::CFGLifetimeEnds& lifetime_end,
                                 const BlockInstructionContext& context,
                                 int& inst_index)
{
    const auto* var_decl = lifetime_end.getVarDecl();
    append_simple_instruction(context,
                              inst_index,
                              "lifetime.end",
                              {nlohmann::json(describe_var(var_decl))},
                              make_location({.source_manager = context.source_manager,
                                             .loc = lifetime_end.getTriggerStmt() != nullptr
                                                        ? lifetime_end.getTriggerStmt()->getEndLoc()
                                                        : clang::SourceLocation()}));
}

void append_auto_dtor_element(const clang::CFGAutomaticObjDtor& auto_dtor,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    const auto* var_decl = auto_dtor.getVarDecl();
    append_simple_instruction(context,
                              inst_index,
                              "dtor",
                              {nlohmann::json(describe_var(var_decl))},
                              make_location({.source_manager = context.source_manager,
                                             .loc = auto_dtor.getTriggerStmt() != nullptr
                                                        ? auto_dtor.getTriggerStmt()->getEndLoc()
                                                        : clang::SourceLocation()}));
}

void append_temp_dtor_element(const clang::CFGTemporaryDtor& temp_dtor,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    const auto* temp_expr = temp_dtor.getBindTemporaryExpr();
    append_simple_instruction(
        context,
        inst_index,
        "dtor",
        {nlohmann::json(
            describe_temporary(temp_expr != nullptr ? temp_expr->getSubExpr() : nullptr))},
        make_location(
            {.source_manager = context.source_manager,
             .loc = temp_expr != nullptr ? temp_expr->getBeginLoc() : clang::SourceLocation()}));
    append_simple_instruction(
        context,
        inst_index,
        "lifetime.end",
        {nlohmann::json(
            describe_temporary(temp_expr != nullptr ? temp_expr->getSubExpr() : nullptr))},
        make_location(
            {.source_manager = context.source_manager,
             .loc = temp_expr != nullptr ? temp_expr->getEndLoc() : clang::SourceLocation()}));
}

void append_base_dtor_element(const clang::CFGBaseDtor& base_dtor,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    const auto* base_specifier = base_dtor.getBaseSpecifier();
    std::string label =
        base_specifier != nullptr ? base_specifier->getType().getAsString() : "base";
    append_simple_instruction(context, inst_index, "dtor", {nlohmann::json(label)}, std::nullopt);
}

void append_member_dtor_element(const clang::CFGMemberDtor& member_dtor,
                                const BlockInstructionContext& context,
                                int& inst_index)
{
    const auto* field_decl = member_dtor.getFieldDecl();
    std::string label = field_decl != nullptr ? field_decl->getNameAsString() : "member";
    append_simple_instruction(context,
                              inst_index,
                              "dtor",
                              {nlohmann::json(label)},
                              make_decl_location(field_decl, context.source_manager));
}

void append_delete_dtor_element(const clang::CFGDeleteDtor& delete_dtor,
                                const BlockInstructionContext& context,
                                int& inst_index)
{
    const auto* delete_expr = delete_dtor.getDeleteExpr();
    std::string label = delete_dtor.getCXXRecordDecl() != nullptr
                            ? delete_dtor.getCXXRecordDecl()->getQualifiedNameAsString()
                            : "delete";
    append_simple_instruction(
        context,
        inst_index,
        "dtor",
        {nlohmann::json(label)},
        make_location({.source_manager = context.source_manager,
                       .loc = delete_expr != nullptr ? delete_expr->getBeginLoc()
                                                     : clang::SourceLocation()}));
}

void append_stmt_instructions(const clang::CFGBlock* block,
                              const BlockInstructionContext& context,
                              int& inst_index)
{
    for (const auto& element : *block) {
        if (const auto stmt_elem = element.getAs<clang::CFGStmt>()) {
            append_cfg_stmt_element(*stmt_elem, context, inst_index);
            continue;
        }
        if (const auto lifetime_end = element.getAs<clang::CFGLifetimeEnds>()) {
            append_lifetime_end_element(*lifetime_end, context, inst_index);
            continue;
        }
        if (const auto auto_dtor = element.getAs<clang::CFGAutomaticObjDtor>()) {
            append_auto_dtor_element(*auto_dtor, context, inst_index);
            continue;
        }
        if (const auto temp_dtor = element.getAs<clang::CFGTemporaryDtor>()) {
            append_temp_dtor_element(*temp_dtor, context, inst_index);
            continue;
        }
        if (const auto base_dtor = element.getAs<clang::CFGBaseDtor>()) {
            append_base_dtor_element(*base_dtor, context, inst_index);
            continue;
        }
        if (const auto member_dtor = element.getAs<clang::CFGMemberDtor>()) {
            append_member_dtor_element(*member_dtor, context, inst_index);
            continue;
        }
        if (const auto delete_dtor = element.getAs<clang::CFGDeleteDtor>()) {
            append_delete_dtor_element(*delete_dtor, context, inst_index);
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

void append_block_edges(
    const clang::CFGBlock* block,
    const std::unordered_map<const clang::CFGBlock*, std::string>& block_ids,
    const std::unordered_map<const clang::CFGBlock*, ExceptionFlowKind>& exception_flows,
    std::vector<ir::Edge>& edges)
{
    int succ_index = 0;
    int normal_index = 0;
    const auto total_succs = static_cast<int>(block->succ_size());
    const auto exception_flow = exception_flows.at(block);
    int exception_start_index = -1;
    if (exception_flow == ExceptionFlowKind::kThrow) {
        exception_start_index = 0;
    } else if (exception_flow == ExceptionFlowKind::kInvoke && total_succs > 1) {
        exception_start_index = total_succs - 1;
    }
    for (const auto& succ : block->succs()) {
        const clang::CFGBlock* succ_block = succ.getReachableBlock();
        if (succ_block == nullptr) {
            ++succ_index;
            continue;
        }
        ir::Edge edge;
        edge.from = block_ids.at(block);
        edge.to = block_ids.at(succ_block);
        if (exception_start_index >= 0 && succ_index >= exception_start_index) {
            edge.kind = "exception";
        } else {
            edge.kind = "succ" + std::to_string(normal_index++);
        }
        ++succ_index;
        edges.push_back(std::move(edge));
    }
}

[[nodiscard]] bool edge_less(const ir::Edge& a, const ir::Edge& b);

[[nodiscard]] ir::Cfg build_nir_cfg(const clang::CFG& cfg,
                                    const clang::FunctionDecl* func_decl,
                                    const clang::SourceManager& source_manager,
                                    const std::string& function_uid,
                                    std::vector<SourceMapEntryKey>& source_entries,
                                    VCallContext& vcall_context)
{
    auto blocks = collect_blocks(cfg);
    auto block_ids = assign_block_ids(blocks);
    std::unordered_map<const clang::CFGBlock*, ExceptionFlowKind> exception_flows;
    exception_flows.reserve(blocks.size());
    for (const auto* block : blocks) {
        exception_flows[block] = classify_exception_flow(block);
    }

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
                                        .nir_block = &nir_block,
                                        .vcall_context = &vcall_context};
        append_block_instructions(block, context);
        nir_cfg.blocks.push_back(std::move(nir_block));
        append_block_edges(block, block_ids, exception_flows, edges);
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
                   const std::vector<const clang::CXXMethodDecl*>& method_decls,
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

    clang::CFG::BuildOptions cfg_options;
    cfg_options.AddImplicitDtors = true;
    cfg_options.AddEHEdges = true;
    cfg_options.AddLifetime = true;
    cfg_options.AddScopes = true;
    cfg_options.AddTemporaryDtors = true;
    cfg_options.AddRichCXXConstructors = true;
    std::unique_ptr<clang::CFG> cfg =
        clang::CFG::buildCFG(func_decl, func_decl->getBody(), &context, cfg_options);
    if (!cfg) {
        return std::nullopt;
    }

    std::vector<ir::VCallCandidateSet> vcall_candidates;
    VCallContext vcall_context{.candidate_sets = &vcall_candidates,
                               .all_methods = &method_decls,
                               .next_id = 0};
    ir::Cfg nir_cfg =
        build_nir_cfg(*cfg, func_decl, source_manager, function_uid, source_entries, vcall_context);

    ir::FunctionDef nir_func;
    nir_func.function_uid = std::move(function_uid);
    nir_func.mangled_name = std::move(mangled_name);
    nir_func.cfg = std::move(nir_cfg);
    if (!vcall_candidates.empty()) {
        nir_func.tables = ir::FunctionTables{.vcall_candidates = std::move(vcall_candidates)};
    }
    return nir_func;
}

class MethodCollector final : public clang::RecursiveASTVisitor<MethodCollector>
{
public:
    bool VisitCXXMethodDecl(clang::CXXMethodDecl* decl)
    {
        if (decl == nullptr || !decl->isVirtual()) {
            return true;
        }

        const auto& source_manager = decl->getASTContext().getSourceManager();
        if (source_manager.isWrittenInMainFile(decl->getLocation())) {
            m_methods.push_back(decl);
        }
        return true;
    }

    std::vector<const clang::CXXMethodDecl*> take_methods() { return std::move(m_methods); }

private:
    std::vector<const clang::CXXMethodDecl*> m_methods;
};

class NirBuilder
{
public:
    NirBuilder() = default;

    void build(clang::ASTContext& context)
    {
        MethodCollector collector;
        collector.TraverseDecl(context.getTranslationUnitDecl());
        m_methods = collector.take_methods();

        auto mangle_context = std::unique_ptr<clang::MangleContext>(context.createMangleContext());
        for (const auto* decl : context.getTranslationUnitDecl()->decls()) {
            const auto* func_decl = clang::dyn_cast<clang::FunctionDecl>(decl);
            if (func_decl == nullptr) {
                continue;
            }
            auto nir_func = build_function_def(func_decl,
                                               context,
                                               *mangle_context,
                                               m_methods,
                                               m_source_entries);
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
    std::vector<const clang::CXXMethodDecl*> m_methods;
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
    if (func.tables.has_value()) {
        auto& candidates = func.tables->vcall_candidates;
        for (auto& candidate_set : candidates) {
            std::ranges::stable_sort(candidate_set.methods);
        }
        std::ranges::stable_sort(candidates,
                                 [](const ir::VCallCandidateSet& a,
                                    const ir::VCallCandidateSet& b) { return a.id < b.id; });
    }
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
                                                           const std::filesystem::path& schema_dir,
                                                           const sappp::VersionTriple& versions)
{
    ir::Nir nir;
    nir.schema_version = "nir.v1";
    nir.tool = {
        {    "name",         "sappp"},
        { "version", sappp::kVersion},
        {"build_id", sappp::kBuildId}
    };
    nir.generated_at = generated_at_from_snapshot(build_snapshot);
    nir.tu_id = std::string(tu_id);
    nir.semantics_version = versions.semantics;
    nir.proof_system_version = versions.proof_system;
    nir.profile_version = versions.profile;
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
    const std::string generated_at = generated_at_from_snapshot(build_snapshot);
    nlohmann::json source_map_json = {
        {"schema_version",                                                                  "source_map.v1"},
        {          "tool", {{"name", "sappp"}, {"version", sappp::kVersion}, {"build_id", sappp::kBuildId}}},
        {  "generated_at",                                                                     generated_at},
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

sappp::Result<FrontendResult> FrontendClang::analyze(const nlohmann::json& build_snapshot,
                                                     const sappp::VersionTriple& versions) const
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

    auto nir_json =
        build_nir_json(build_snapshot, tu_id, std::move(functions), schema_dir, versions);
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
