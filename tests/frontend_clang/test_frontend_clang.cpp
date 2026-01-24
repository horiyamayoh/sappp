#include "frontend_clang/frontend.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace sappp::frontend_clang::test {

namespace {

std::string schema_path(const std::string& name)
{
    return std::string(SAPPP_SCHEMA_DIR) + "/" + name;
}

struct BuildSnapshotInput
{
    std::string_view cwd;
    std::string_view source_path;
};

nlohmann::json make_build_snapshot(const BuildSnapshotInput& input)
{
    nlohmann::json compile_unit = {
        {         "tu_id",   sappp::common::sha256_prefixed("tu")                          },
        {           "cwd",                         sappp::common::normalize_path(input.cwd)},
        {          "argv",
         nlohmann::json::array(
         {SAPPP_CXX_COMPILER, "-std=c++23", "-c", std::string(input.source_path)})         },
        {     "env_delta",                                         nlohmann::json::object()},
        {"response_files",                                          nlohmann::json::array()},
        {          "lang",                                                            "c++"},
        {           "std",                                                          "c++23"},
        {        "target",
         {{"triple", "x86_64-unknown-linux-gnu"},
         {"abi", "sysv"},
         {"data_layout", {{"ptr_bits", 64}, {"long_bits", 64}, {"align", {{"max", 16}}}}}} },
        {      "frontend",                         {{"kind", "clang"}, {"version", "test"}}}
    };

    return nlohmann::json{
        {"schema_version",                       "build_snapshot.v1"},
        {          "tool", {{"name", "sappp"}, {"version", "0.1.0"}}},
        {  "generated_at",                    "2024-01-01T00:00:00Z"},
        {          "host",     {{"os", "linux"}, {"arch", "x86_64"}}},
        { "compile_units",     nlohmann::json::array({compile_unit})}
    };
}

std::string sink_marker_source()
{
    return "int main() {\n"
           "  int a = 1;\n"
           "  int b = 0;\n"
           "  int c = a / b;\n"
           "  int shift = a << b;\n"
           "  int overflow = a + 1;\n"
           "  int* p = nullptr;\n"
           "  int d = *p;\n"
           "  int arr[1] = {0};\n"
           "  int e = arr[1];\n"
           "  int uninit;\n"
           "  int f = uninit;\n"
           "  int* heap = new int(42);\n"
           "  delete heap;\n"
           "  alignas(1) char buf[sizeof(int) + 1] = {};\n"
           "  int* misaligned = reinterpret_cast<int*>(buf + 1);\n"
           "  int g = *misaligned;\n"
           "  return c + d + e + f + g + shift + overflow;\n"
           "}\n";
}

std::string lifetime_source()
{
    return "#include <utility>\n"
           "struct Widget {\n"
           "  Widget() {}\n"
           "  Widget(Widget&&) {}\n"
           "  ~Widget() {}\n"
           "};\n"
           "int main() {\n"
           "  Widget w;\n"
           "  Widget w2;\n"
           "  Widget w3(std::move(w2));\n"
           "  const Widget& w4 = Widget{};\n"
           "  (void)w4;\n"
           "  return 0;\n"
           "}\n";
}

std::string exception_flow_source()
{
    return "struct Widget {\n"
           "  ~Widget() {}\n"
           "};\n"
           "void may_throw();\n"
           "int main() {\n"
           "  try {\n"
           "    Widget w;\n"
           "    may_throw();\n"
           "    throw 1;\n"
           "  } catch (...) {\n"
           "    throw;\n"
           "  }\n"
           "}\n";
}

std::string vcall_source()
{
    return "struct Base {\n"
           "  virtual int value() { return 1; }\n"
           "  virtual ~Base() = default;\n"
           "};\n"
           "struct Derived : Base {\n"
           "  int value() override { return 2; }\n"
           "};\n"
           "int main() {\n"
           "  Base* ptr = new Derived();\n"
           "  int out = ptr->value();\n"
           "  delete ptr;\n"
           "  return out;\n"
           "}\n";
}

std::string heap_source()
{
    return "int main() {\n"
           "  int* ptr = new int(1);\n"
           "  delete ptr;\n"
           "  return 0;\n"
           "}\n";
}

std::string manual_marker_source()
{
    return "void sappp_sink(const char* kind);\n"
           "void sappp_check(const char* kind, bool predicate);\n"
           "int main() {\n"
           "  sappp_sink(\"use-after-lifetime\");\n"
           "  sappp_check(\"shift\", false);\n"
           "  return 0;\n"
           "}\n";
}

std::string operand_source()
{
    return "void may_throw();\n"
           "void no_throw() noexcept;\n"
           "int add(int a, int b) { return a + b; }\n"
           "int main() {\n"
           "  int x = 1;\n"
           "  int y = x;\n"
           "  x;\n"
           "  x = y;\n"
           "  x = add(y, 2);\n"
           "  no_throw();\n"
           "  may_throw();\n"
           "  return x;\n"
           "}\n";
}

void write_source_file(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream source_file(path);
    ASSERT_TRUE(source_file.good());
    source_file << contents;
}

std::optional<std::string> sink_kind_from_inst(const nlohmann::json& inst)
{
    if (inst.at("op").get<std::string>() != "sink.marker") {
        return std::nullopt;
    }
    if (!inst.contains("args") || !inst.at("args").is_array() || inst.at("args").empty()
        || !inst.at("args").at(0).is_string()) {
        return std::nullopt;
    }
    return inst.at("args").at(0).get<std::string>();
}

void collect_sink_kinds_from_block(const nlohmann::json& block,
                                   std::unordered_set<std::string>& sink_kinds)
{
    for (const auto& inst : block.at("insts")) {
        auto sink_kind = sink_kind_from_inst(inst);
        if (sink_kind.has_value()) {
            sink_kinds.insert(*sink_kind);
        }
    }
}

void collect_sink_kinds_from_function(const nlohmann::json& func,
                                      std::unordered_set<std::string>& sink_kinds)
{
    for (const auto& block : func.at("cfg").at("blocks")) {
        collect_sink_kinds_from_block(block, sink_kinds);
    }
}

std::unordered_set<std::string> collect_sink_kinds(const nlohmann::json& nir)
{
    std::unordered_set<std::string> sink_kinds;
    for (const auto& func : nir.at("functions")) {
        collect_sink_kinds_from_function(func, sink_kinds);
    }
    return sink_kinds;
}

std::unordered_set<std::string> collect_ops(const nlohmann::json& nir)
{
    std::unordered_set<std::string> ops;
    for (const auto& func : nir.at("functions")) {
        for (const auto& block : func.at("cfg").at("blocks")) {
            for (const auto& inst : block.at("insts")) {
                ops.insert(inst.at("op").get<std::string>());
            }
        }
    }
    return ops;
}

std::unordered_set<std::string> collect_edge_kinds(const nlohmann::json& nir)
{
    std::unordered_set<std::string> kinds;
    for (const auto& func : nir.at("functions")) {
        for (const auto& edge : func.at("cfg").at("edges")) {
            kinds.insert(edge.at("kind").get<std::string>());
        }
    }
    return kinds;
}

bool block_has_vcall(const nlohmann::json& block)
{
    return std::ranges::any_of(block.at("insts"), [](const auto& inst) {
        return inst.at("op").template get<std::string>() == "vcall";
    });
}

const nlohmann::json* find_function_with_vcall(const nlohmann::json& nir)
{
    for (const auto& func : nir.at("functions")) {
        const auto& blocks = func.at("cfg").at("blocks");
        if (std::ranges::any_of(blocks, block_has_vcall)) {
            return &func;
        }
    }
    return nullptr;
}

std::vector<nlohmann::json> collect_vcall_insts(const nlohmann::json& func)
{
    std::vector<nlohmann::json> insts;
    for (const auto& block : func.at("cfg").at("blocks")) {
        for (const auto& inst : block.at("insts")) {
            if (inst.at("op").get<std::string>() == "vcall") {
                insts.push_back(inst);
            }
        }
    }
    return insts;
}

bool is_sorted_strings(const std::vector<std::string>& values)
{
    return std::ranges::is_sorted(values);
}

bool has_ub_check_with_kind(const nlohmann::json& nir, std::string_view kind, bool predicate_value)
{
    for (const auto& func : nir.at("functions")) {
        for (const auto& block : func.at("cfg").at("blocks")) {
            for (const auto& inst : block.at("insts")) {
                if (inst.at("op").get<std::string>() != "ub.check") {
                    continue;
                }
                if (!inst.contains("args") || !inst.at("args").is_array()) {
                    continue;
                }
                const auto& args = inst.at("args");
                if (args.size() < 2U || !args.at(0).is_string() || !args.at(1).is_boolean()) {
                    continue;
                }
                if (args.at(0).get<std::string>() == kind
                    && args.at(1).get<bool>() == predicate_value) {
                    return true;
                }
            }
        }
    }
    return false;
}

void expect_signature_shape(const nlohmann::json& func)
{
    ASSERT_TRUE(func.contains("signature")) << "missing signature in function";
    const auto& signature = func.at("signature");
    ASSERT_TRUE(signature.is_object());
    ASSERT_TRUE(signature.contains("return_type"));
    EXPECT_TRUE(signature.at("return_type").is_string());
    ASSERT_TRUE(signature.contains("params"));
    ASSERT_TRUE(signature.at("params").is_array());
    ASSERT_TRUE(signature.contains("noexcept"));
    EXPECT_TRUE(signature.at("noexcept").is_boolean());
    ASSERT_TRUE(signature.contains("variadic"));
    EXPECT_TRUE(signature.at("variadic").is_boolean());

    for (const auto& param : signature.at("params")) {
        ASSERT_TRUE(param.is_object());
        ASSERT_TRUE(param.contains("name"));
        EXPECT_TRUE(param.at("name").is_string());
        ASSERT_TRUE(param.contains("type"));
        EXPECT_TRUE(param.at("type").is_string());
    }
}

}  // namespace

TEST(FrontendClangTest, GeneratesValidNirAndSourceMap)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_test_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path,
                      "int add(int a, int b) { return a + b; }\n"
                      "int main() { return add(1, 2); }\n");

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    auto nir_validation =
        sappp::common::validate_json(result->nir, schema_path("nir.v1.schema.json"));
    EXPECT_TRUE(nir_validation) << (nir_validation ? "" : nir_validation.error().message);

    auto source_validation =
        sappp::common::validate_json(result->source_map, schema_path("source_map.v1.schema.json"));
    EXPECT_TRUE(source_validation) << (source_validation ? "" : source_validation.error().message);

    EXPECT_TRUE(result->nir.contains("functions"));
    EXPECT_FALSE(result->nir.at("functions").empty());

    EXPECT_TRUE(result->source_map.contains("entries"));
    EXPECT_FALSE(result->source_map.at("entries").empty());

    for (const auto& func : result->nir.at("functions")) {
        expect_signature_shape(func);
    }

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsSinkMarkersForPotentialUb)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_sink_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, sink_marker_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> sink_kinds = collect_sink_kinds(result->nir);

    EXPECT_NE(sink_kinds.find("div0"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("shift"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("signed_overflow"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("null"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("oob"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("uninit_read"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("double_free"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("invalid_free"), sink_kinds.end());
    EXPECT_NE(sink_kinds.find("misaligned"), sink_kinds.end());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsExceptionFlowLowering)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path()
        / ("sappp_frontend_exception_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, exception_flow_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> ops = collect_ops(result->nir);
    std::unordered_set<std::string> edge_kinds = collect_edge_kinds(result->nir);

    EXPECT_NE(ops.find("invoke"), ops.end());
    EXPECT_NE(ops.find("throw"), ops.end());
    EXPECT_NE(ops.find("landingpad"), ops.end());
    EXPECT_NE(ops.find("resume"), ops.end());
    EXPECT_NE(edge_kinds.find("exception"), edge_kinds.end());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsLifetimeAndCtorEvents)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_lifetime_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, lifetime_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> ops = collect_ops(result->nir);

    EXPECT_NE(ops.find("lifetime.begin"), ops.end());
    EXPECT_NE(ops.find("lifetime.end"), ops.end());
    EXPECT_NE(ops.find("ctor"), ops.end());
    EXPECT_NE(ops.find("dtor"), ops.end());
    EXPECT_NE(ops.find("move"), ops.end());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsHeapAllocFreeEvents)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_heap_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, heap_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> ops = collect_ops(result->nir);

    EXPECT_NE(ops.find("alloc"), ops.end());
    EXPECT_NE(ops.find("free"), ops.end());

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsVirtualCallCandidates)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_vcall_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, vcall_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    const nlohmann::json* vcall_func = find_function_with_vcall(result->nir);
    ASSERT_NE(vcall_func, nullptr);

    auto vcall_insts = collect_vcall_insts(*vcall_func);
    ASSERT_FALSE(vcall_insts.empty());

    ASSERT_TRUE(vcall_func->contains("tables"));
    ASSERT_TRUE(vcall_func->at("tables").contains("vcall_candidates"));
    const auto& candidate_sets = vcall_func->at("tables").at("vcall_candidates");
    ASSERT_TRUE(candidate_sets.is_array());
    ASSERT_FALSE(candidate_sets.empty());

    const auto& vcall_inst = vcall_insts.front();
    ASSERT_TRUE(vcall_inst.contains("args"));
    const auto& args = vcall_inst.at("args");
    ASSERT_GE(args.size(), 2U);
    const std::string candidate_id = args.at(1).get<std::string>();

    auto candidate_it = std::ranges::find_if(candidate_sets, [&](const auto& candidate_set) {
        return candidate_set.at("id").template get<std::string>() == candidate_id;
    });
    ASSERT_NE(candidate_it, candidate_sets.end());
    ASSERT_TRUE(candidate_it->contains("methods"));
    const auto& methods_json = candidate_it->at("methods");
    ASSERT_TRUE(methods_json.is_array());
    ASSERT_FALSE(methods_json.empty());

    std::vector<std::string> methods = methods_json.get<std::vector<std::string>>();
    EXPECT_TRUE(is_sorted_strings(methods));

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsManualMarkers)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_manual_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, manual_marker_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    std::unordered_set<std::string> sink_kinds = collect_sink_kinds(result->nir);
    EXPECT_NE(sink_kinds.find("use-after-lifetime"), sink_kinds.end());
    EXPECT_TRUE(has_ub_check_with_kind(result->nir, "shift", false));

    std::filesystem::remove_all(temp_dir);
}

TEST(FrontendClangTest, EmitsOperandsForLoadStoreAssignAndCalls)
{
    auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path()
                                     / ("sappp_frontend_operands_" + std::to_string(unique_suffix));
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path source_path = temp_dir / "sample.cpp";
    write_source_file(source_path, operand_source());

    nlohmann::json build_snapshot =
        make_build_snapshot({.cwd = temp_dir.string(), .source_path = source_path.string()});

    FrontendClang frontend(SAPPP_SCHEMA_DIR);
    auto result = frontend.analyze(build_snapshot);
    ASSERT_TRUE(result);

    const std::unordered_set<std::string> target_ops = {
        "load",
        "store",
        "assign",
        "call",
        "invoke",
    };

    std::unordered_set<std::string> seen_ops;
    for (const auto& func : result->nir.at("functions")) {
        for (const auto& block : func.at("cfg").at("blocks")) {
            for (const auto& inst : block.at("insts")) {
                const auto op = inst.at("op").get<std::string>();
                if (!target_ops.contains(op)) {
                    continue;
                }
                seen_ops.insert(op);
                ASSERT_TRUE(inst.contains("args"));
                ASSERT_TRUE(inst.at("args").is_array());
                ASSERT_FALSE(inst.at("args").empty());
                if (op == "call" || op == "invoke") {
                    EXPECT_GE(inst.at("args").size(), 2U);
                    EXPECT_TRUE(inst.at("args").at(0).is_object());
                }
            }
        }
    }

    for (const auto& op : target_ops) {
        EXPECT_NE(seen_ops.find(op), seen_ops.end()) << "missing op: " << op;
    }

    std::filesystem::remove_all(temp_dir);
}

}  // namespace sappp::frontend_clang::test
