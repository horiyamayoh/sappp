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

#include <clang/Analysis/CFG.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Mangle.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/raw_ostream.h>

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

namespace sappp::frontend_clang {

namespace {

struct SourceMapEntryKey {
    std::string function_uid;
    std::string block_id;
    std::string inst_id;
    nlohmann::json entry;
};

std::string current_time_utc() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

bool is_source_file(const std::string& path) {
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

struct CompileUnitCommand {
    std::string file_path;
    std::vector<std::string> args;
};

sappp::Result<CompileUnitCommand> extract_compile_command(const nlohmann::json& compile_unit) {
    CompileUnitCommand result;
    result.args = compile_unit.at("argv").get<std::vector<std::string>>();

    std::optional<std::size_t> source_index;
    for (auto [i, arg] : std::views::enumerate(result.args)) {
        const auto idx = static_cast<std::size_t>(i);
        if (arg == "-c" && idx + 1 < result.args.size() && is_source_file(result.args[idx + 1])) {
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

std::string normalize_file_path(const std::string& cwd, const std::string& file_path) {
    std::filesystem::path path(file_path);
    if (!path.is_absolute()) {
        std::filesystem::path base(cwd);
        path = base / path;
    }
    path = path.lexically_normal();
    return sappp::common::normalize_path(path.string());
}

std::optional<ir::Location> make_location(const clang::SourceManager& source_manager,
                                          clang::SourceLocation loc) {
    if (loc.isInvalid()) {
        return std::nullopt;
    }
    if (source_manager.isInSystemHeader(loc)) {
        return std::nullopt;
    }

    clang::SourceLocation spelling = source_manager.getSpellingLoc(loc);
    clang::PresumedLoc presumed = source_manager.getPresumedLoc(spelling);
    if (!presumed.isValid()) {
        return std::nullopt;
    }

    ir::Location location;
    location.file = sappp::common::normalize_path(presumed.getFilename());
    location.line = static_cast<int>(presumed.getLine());
    location.col = static_cast<int>(presumed.getColumn());
    return location;
}

std::string classify_stmt(const clang::Stmt* stmt) {
    if (clang::isa<clang::ReturnStmt>(stmt)) {
        return "ret";
    }
    if (clang::isa<clang::CallExpr>(stmt) || clang::isa<clang::CXXMemberCallExpr>(stmt) ||
        clang::isa<clang::CXXConstructExpr>(stmt) || clang::isa<clang::CXXOperatorCallExpr>(stmt)) {
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

class NirBuilder {
public:
    NirBuilder() = default;

    void build(clang::ASTContext& context) {
        auto mangle_context = std::unique_ptr<clang::MangleContext>(context.createMangleContext());
        const auto& source_manager = context.getSourceManager();

        for (const auto* decl : context.getTranslationUnitDecl()->decls()) {
            auto* func_decl = clang::dyn_cast<clang::FunctionDecl>(decl);
            if (!func_decl || !func_decl->hasBody()) {
                continue;
            }
            if (!source_manager.isWrittenInMainFile(func_decl->getLocation())) {
                continue;
            }

            std::string function_uid;
            llvm::SmallString<128> usr_buffer;
            if (clang::index::generateUSRForDecl(func_decl, usr_buffer)) {
                function_uid = func_decl->getQualifiedNameAsString();
            } else {
                function_uid = std::string(usr_buffer.str());
            }

            std::string mangled_name;
            if (mangle_context->shouldMangleDeclName(func_decl)) {
                llvm::raw_string_ostream os(mangled_name);
                mangle_context->mangleName(func_decl, os);
            } else {
                mangled_name = func_decl->getNameAsString();
            }

            std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
                func_decl,
                func_decl->getBody(),
                &context,
                clang::CFG::BuildOptions());

            if (!cfg) {
                continue;
            }

            std::vector<const clang::CFGBlock*> blocks;
            blocks.reserve(cfg->getNumBlockIDs());
            for (const auto* block : *cfg) {
                if (block) {
                    blocks.push_back(block);
                }
            }

            std::ranges::stable_sort(blocks,
                                      [](const clang::CFGBlock* a, const clang::CFGBlock* b) {
                                          return a->getBlockID() < b->getBlockID();
                                      });

            std::unordered_map<const clang::CFGBlock*, std::string> block_ids;
            block_ids.reserve(blocks.size());
            for (auto [i, block] : std::views::enumerate(blocks)) {
                block_ids[block] = "B" + std::to_string(i);
            }

            const clang::CFGBlock* entry_block = &cfg->getEntry();
            ir::Cfg nir_cfg;
            nir_cfg.entry = block_ids.at(entry_block);

            std::vector<ir::Edge> edges;
            edges.reserve(blocks.size());

            for (const auto* block : blocks) {
                ir::BasicBlock nir_block;
                nir_block.id = block_ids.at(block);

                int inst_index = 0;
                if (block == entry_block) {
                    ir::Instruction ub_check;
                    ub_check.id = "I" + std::to_string(inst_index++);
                    ub_check.op = "ub.check";
                    ub_check.args = {nlohmann::json("UB.DivZero"), nlohmann::json(true)};
                    ub_check.src = make_location(source_manager, func_decl->getBeginLoc());
                    nir_block.insts.push_back(std::move(ub_check));
                }

                for (auto elem_it = block->begin(); elem_it != block->end(); ++elem_it) {
                    if (const auto stmt_elem = elem_it->getAs<clang::CFGStmt>()) {
                        const clang::Stmt* stmt = stmt_elem->getStmt();
                        ir::Instruction inst;
                        inst.id = "I" + std::to_string(inst_index++);
                        inst.op = classify_stmt(stmt);
                        inst.src = make_location(source_manager, stmt->getBeginLoc());
                        nir_block.insts.push_back(inst);

                        if (inst.src.has_value()) {
                            nlohmann::json loc = *inst.src;
                            nlohmann::json entry = {
                                {"ir_ref", {
                                    {"function_uid", function_uid},
                                    {"block_id", nir_block.id},
                                    {"inst_id", inst.id}
                                }},
                                {"spelling_loc", loc},
                                {"expansion_loc", loc},
                                {"macro_stack", nlohmann::json::array()}
                            };
                            m_source_entries.push_back({function_uid, nir_block.id, inst.id, entry});
                        }
                    }
                }

                const clang::Stmt* terminator = block->getTerminatorStmt();
                if (terminator && !clang::isa<clang::ReturnStmt>(terminator)) {
                    ir::Instruction inst;
                    inst.id = "I" + std::to_string(inst_index++);
                    inst.op = "branch";
                    inst.src = make_location(source_manager, terminator->getBeginLoc());
                    nir_block.insts.push_back(inst);

                    if (inst.src.has_value()) {
                        nlohmann::json loc = *inst.src;
                        nlohmann::json entry = {
                            {"ir_ref", {
                                {"function_uid", function_uid},
                                {"block_id", nir_block.id},
                                {"inst_id", inst.id}
                            }},
                            {"spelling_loc", loc},
                            {"expansion_loc", loc},
                            {"macro_stack", nlohmann::json::array()}
                        };
                        m_source_entries.push_back({function_uid, nir_block.id, inst.id, entry});
                    }
                }

                nir_cfg.blocks.push_back(std::move(nir_block));

                int succ_index = 0;
                for (auto succ_it = block->succ_begin(); succ_it != block->succ_end(); ++succ_it) {
                    const clang::CFGBlock* succ_block = succ_it->getReachableBlock();
                    if (!succ_block) {
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

            std::ranges::stable_sort(nir_cfg.blocks,
                                      [](const ir::BasicBlock& a, const ir::BasicBlock& b) {
                                          return a.id < b.id;
                                      });

            std::ranges::stable_sort(edges,
                                      [](const ir::Edge& a, const ir::Edge& b) {
                                          if (a.from != b.from) {
                                              return a.from < b.from;
                                          }
                                          if (a.to != b.to) {
                                              return a.to < b.to;
                                          }
                                          return a.kind < b.kind;
                                      });
            nir_cfg.edges = std::move(edges);

            ir::FunctionDef nir_func;
            nir_func.function_uid = std::move(function_uid);
            nir_func.mangled_name = std::move(mangled_name);
            nir_func.cfg = std::move(nir_cfg);

            m_functions.push_back(std::move(nir_func));
        }
    }

    std::vector<ir::FunctionDef> take_functions() { return std::move(m_functions); }
    std::vector<SourceMapEntryKey> take_source_entries() { return std::move(m_source_entries); }

private:
    std::vector<ir::FunctionDef> m_functions;
    std::vector<SourceMapEntryKey> m_source_entries;
};

class NirASTConsumer final : public clang::ASTConsumer {
public:
    explicit NirASTConsumer(NirBuilder& builder)
        : m_builder(builder) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        m_builder.build(context);
    }

private:
    NirBuilder& m_builder;
};

class NirFrontendAction final : public clang::ASTFrontendAction {
public:
    explicit NirFrontendAction(NirBuilder& builder)
        : m_builder(builder) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&,
                                                          llvm::StringRef) override {
        return std::make_unique<NirASTConsumer>(m_builder);
    }

private:
    NirBuilder& m_builder;
};

class NirFrontendActionFactory final : public clang::tooling::FrontendActionFactory {
public:
    explicit NirFrontendActionFactory(NirBuilder& builder)
        : m_builder(builder) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<NirFrontendAction>(m_builder);
    }

private:
    NirBuilder& m_builder;
};

} // namespace

FrontendClang::FrontendClang(std::string schema_dir)
    : m_schema_dir(std::move(schema_dir)) {}

sappp::Result<FrontendResult> FrontendClang::analyze(const nlohmann::json& build_snapshot) const {
    std::filesystem::path schema_dir(m_schema_dir);
    if (auto result = sappp::common::validate_json(build_snapshot,
                                                   (schema_dir / "build_snapshot.v1.schema.json").string());
        !result) {
        return std::unexpected(result.error());
    }

    const auto& compile_units = build_snapshot.at("compile_units");
    std::vector<ir::FunctionDef> functions;
    std::vector<SourceMapEntryKey> source_entries;
    std::vector<std::string> tu_ids;

    for (const auto& unit : compile_units) {
        std::string tu_id = unit.at("tu_id").get<std::string>();
        tu_ids.push_back(tu_id);

        auto command_result = extract_compile_command(unit);
        if (!command_result) {
            return std::unexpected(command_result.error());
        }
        CompileUnitCommand command = *command_result;
        std::string cwd = unit.at("cwd").get<std::string>();
        std::string file_path = normalize_file_path(cwd, command.file_path);

        std::filesystem::path file_fs(file_path);
        if (!std::filesystem::exists(file_fs)) {
            return std::unexpected(Error::make("SourceFileNotFound",
                "Source file not found: " + file_path));
        }

        clang::tooling::FixedCompilationDatabase comp_db(cwd, command.args);
        clang::tooling::ClangTool tool(comp_db, {file_path});

        NirBuilder builder;
        NirFrontendActionFactory factory(builder);

        if (tool.run(&factory) != 0) {
            return std::unexpected(Error::make("ClangToolFailed",
                "ClangTool failed for source file: " + file_path));
        }

        auto unit_functions = builder.take_functions();
        functions.insert(functions.end(),
                         std::make_move_iterator(unit_functions.begin()),
                         std::make_move_iterator(unit_functions.end()));

        auto unit_entries = builder.take_source_entries();
        source_entries.insert(source_entries.end(),
                               std::make_move_iterator(unit_entries.begin()),
                               std::make_move_iterator(unit_entries.end()));
    }

    if (functions.empty()) {
        return std::unexpected(Error::make("NirEmpty",
            "No functions found to emit NIR"));
    }

    std::ranges::stable_sort(functions,
                              [](const ir::FunctionDef& a, const ir::FunctionDef& b) {
                                  return a.function_uid < b.function_uid;
                              });

    for (auto& func : functions) {
        std::ranges::stable_sort(func.cfg.blocks,
                                 [](const ir::BasicBlock& a, const ir::BasicBlock& b) {
                                     return a.id < b.id;
                                 });
        for (auto& block : func.cfg.blocks) {
            std::ranges::stable_sort(block.insts,
                                     [](const ir::Instruction& a, const ir::Instruction& b) {
                                         return a.id < b.id;
                                     });
        }
        std::ranges::stable_sort(func.cfg.edges,
                                  [](const ir::Edge& a, const ir::Edge& b) {
                                      if (a.from != b.from) {
                                          return a.from < b.from;
                                      }
                                      if (a.to != b.to) {
                                          return a.to < b.to;
                                      }
                                      return a.kind < b.kind;
                                  });
    }

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

    std::string tu_id;
    if (tu_ids.size() == 1) {
        tu_id = tu_ids.front();
    } else {
        std::ranges::stable_sort(tu_ids);
        nlohmann::json tu_array = tu_ids;
        auto tu_id_result = sappp::canonical::hash_canonical(tu_array);
        if (!tu_id_result) {
            return std::unexpected(tu_id_result.error());
        }
        tu_id = *tu_id_result;
    }

    ir::Nir nir;
    nir.schema_version = "nir.v1";
    nir.tool = {
        {"name", "sappp"},
        {"version", sappp::VERSION},
        {"build_id", sappp::BUILD_ID}
    };
    nir.generated_at = current_time_utc();
    nir.tu_id = tu_id;
    nir.semantics_version = sappp::SEMANTICS_VERSION;
    nir.proof_system_version = sappp::PROOF_SYSTEM_VERSION;
    nir.profile_version = sappp::PROFILE_VERSION;
    if (build_snapshot.contains("input_digest")) {
        nir.input_digest = build_snapshot.at("input_digest").get<std::string>();
    }
    nir.functions = std::move(functions);

    nlohmann::json nir_json = nir;

    nlohmann::json source_map_json = {
        {"schema_version", "source_map.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::VERSION},
            {"build_id", sappp::BUILD_ID}
        }},
        {"generated_at", current_time_utc()},
        {"tu_id", tu_id},
        {"entries", nlohmann::json::array()}
    };

    auto& entries_array = source_map_json["entries"];
    for (const auto& entry : source_entries) {
        entries_array.push_back(entry.entry);
    }

    if (build_snapshot.contains("input_digest")) {
        source_map_json["input_digest"] = build_snapshot.at("input_digest").get<std::string>();
    }

    if (auto result = sappp::common::validate_json(nir_json,
                                                   (schema_dir / "nir.v1.schema.json").string());
        !result) {
        return std::unexpected(result.error());
    }

    if (auto result = sappp::common::validate_json(source_map_json,
                                                   (schema_dir / "source_map.v1.schema.json").string());
        !result) {
        return std::unexpected(result.error());
    }

    return FrontendResult{nir_json, source_map_json};
}

} // namespace sappp::frontend_clang
