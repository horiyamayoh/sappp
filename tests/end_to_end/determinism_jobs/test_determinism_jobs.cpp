#include "sappp/certstore.hpp"
#include "sappp/common.hpp"
#include "sappp/version.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

class TempDir
{
public:
    explicit TempDir(std::string_view base_name)
    {
        const auto unique_suffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        m_path = fs::temp_directory_path() / std::format("{}_{}", base_name, unique_suffix);
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

struct Options
{
    std::string sappp_path;
    std::string schema_dir;
    std::string repo_root;
    std::string compiler;
};

struct CommandSpec
{
    std::string label;
    std::string command;
};

struct AnalyzeJobSpec
{
    fs::path snapshot_path;
    fs::path output_dir;
    int jobs = 0;
};

struct ValidateJobSpec
{
    fs::path input_dir;
    fs::path output_file;
    int jobs = 0;
};

struct CaptureSpec
{
    fs::path compile_commands;
    fs::path capture_dir;
};

struct JobResults
{
    fs::path output_dir;
    fs::path validated_path;
};

[[nodiscard]] std::string shell_quote(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

[[nodiscard]] bool run_command(const CommandSpec& spec)
{
    std::println("[determinism_jobs] {}: {}", spec.label, spec.command);
    int result = std::system(spec.command.c_str());
    if (result != 0) {
        std::println(stderr, "[determinism_jobs] {} failed with code {}", spec.label, result);
    }
    return result == 0;
}

[[nodiscard]] Json read_json_file(const fs::path& path)
{
    std::ifstream in{};
    in.open(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open JSON file: " + path.string());
    }
    std::string content{};
    content.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    return Json::parse(content);
}

[[nodiscard]] bool write_json_file(const fs::path& path, const Json& payload)
{
    std::ofstream out{};
    out.open(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::println(stderr, "[determinism_jobs] Failed to open {} for write", path.string());
        return false;
    }
    out << payload.dump();
    return static_cast<bool>(out);
}

[[nodiscard]] Json make_po_cert(const std::string& po_id)
{
    Json predicate_expr = {
        {"op", "neq"}
    };

    return {
        {"schema_version",             "cert.v1"                          },
        {          "kind",                                         "PoDef"},
        {            "po",
         {{"po_id", po_id},
         {"po_kind", "div0"},
         {"profile_version", sappp::kProfileVersion},
         {"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"repo_identity",
         {{"path", "tests/end_to_end/determinism_jobs"},
         {"content_sha256", sappp::common::sha256_prefixed("content")}}},
         {"function", {{"usr", "c:@F@test"}, {"mangled", "_Z4testv"}}},
         {"anchor", {{"block_id", "B1"}, {"inst_id", "I1"}}},
         {"predicate", {{"expr", predicate_expr}, {"pretty", "x != 0"}}}} }
    };
}

[[nodiscard]] Json make_ir_cert(const std::string& tu_id)
{
    return {
        {"schema_version", "cert.v1"},
        {          "kind",   "IrRef"},
        {         "tu_id",     tu_id},
        {  "function_uid",   "func1"},
        {      "block_id",      "B1"},
        {       "inst_id",      "I1"}
    };
}

[[nodiscard]] Json make_bug_trace(const std::string& po_id, const std::string& tu_id)
{
    return {
        {"schema_version",            "cert.v1"                          },
        {          "kind",                                     "BugTrace"},
        {    "trace_kind",                                   "ir_path.v1"},
        {         "steps",
         {{{"ir",
         {{"schema_version", "cert.v1"},
         {"kind", "IrRef"},
         {"tu_id", tu_id},
         {"function_uid", "func1"},
         {"block_id", "B1"},
         {"inst_id", "I1"}}}}}                                           },
        {     "violation", {{"po_id", po_id}, {"predicate_holds", false}}}
    };
}

[[nodiscard]] Json make_proof_root(const std::string& po_hash,
                                   const std::string& ir_hash,
                                   const std::string& evidence_hash)
{
    return {
        {"schema_version","cert.v1"                          },
        {          "kind",                 "ProofRoot"},
        {            "po",          {{"ref", po_hash}}},
        {            "ir",          {{"ref", ir_hash}}},
        {      "evidence",    {{"ref", evidence_hash}}},
        {        "result",                       "BUG"},
        {       "depends",
         {{"semantics_version", sappp::kSemanticsVersion},
         {"proof_system_version", sappp::kProofSystemVersion},
         {"profile_version", sappp::kProfileVersion}} },
        {    "hash_scope",             "hash_scope.v1"}
    };
}

template <typename T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - label/result pairing is explicit.
[[nodiscard]] bool ensure_result(const sappp::Result<T>& result, std::string_view label)
{
    if (!result) {
        std::println(stderr, "[determinism_jobs] Failed to {}: {}", label, result.error().message);
    }
    return result.has_value();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - label/result pairing is explicit.
[[nodiscard]] bool ensure_result(const sappp::VoidResult& result, std::string_view label)
{
    if (!result) {
        std::println(stderr, "[determinism_jobs] Failed to {}: {}", label, result.error().message);
    }
    return result.has_value();
}

[[nodiscard]] bool populate_certstore(const fs::path& output_dir, const std::string& schema_dir)
{
    const fs::path po_path = output_dir / "po" / "po_list.json";
    if (!fs::exists(po_path)) {
        std::println(stderr, "[determinism_jobs] Missing po_list.json at {}", po_path.string());
        return false;
    }

    Json po_list = read_json_file(po_path);
    const auto& pos = po_list.at("pos");
    if (pos.empty()) {
        std::println(stderr, "[determinism_jobs] po_list.json is empty");
        return false;
    }

    const fs::path certstore_dir = output_dir / "certstore";
    sappp::certstore::CertStore store(certstore_dir.string(), schema_dir);

    const std::string tu_id = sappp::common::sha256_prefixed("tu-determinism");
    const Json ir_cert = make_ir_cert(tu_id);
    auto ir_hash_result = store.put(ir_cert);
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized via helper.
    const bool ir_ok = ensure_result(ir_hash_result, "store IR cert");
    if (!ir_ok) {
        return ir_ok;
    }

    for (const auto& po : pos) {
        const std::string po_id = po.at("po_id").get<std::string>();

        const Json po_cert = make_po_cert(po_id);
        const Json bug_trace = make_bug_trace(po_id, tu_id);

        auto po_hash_result = store.put(po_cert);
        // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized via helper.
        const bool po_ok = ensure_result(po_hash_result, "store PO cert");
        if (!po_ok) {
            return po_ok;
        }

        auto bug_hash_result = store.put(bug_trace);
        // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized via helper.
        const bool bug_ok = ensure_result(bug_hash_result, "store BugTrace cert");
        if (!bug_ok) {
            return bug_ok;
        }

        const Json proof_root = make_proof_root(*po_hash_result, *ir_hash_result, *bug_hash_result);
        auto root_hash_result = store.put(proof_root);
        // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized via helper.
        const bool root_ok = ensure_result(root_hash_result, "store ProofRoot cert");
        if (!root_ok) {
            return root_ok;
        }

        auto bind_result = store.bind_po(po_id, *root_hash_result);
        // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized via helper.
        const bool bind_ok = ensure_result(bind_result, std::format("bind PO {}", po_id));
        if (!bind_ok) {
            return bind_ok;
        }
    }

    return true;
}

[[nodiscard]] std::set<std::string> collect_po_ids(const fs::path& po_list_path)
{
    Json po_list = read_json_file(po_list_path);
    std::set<std::string> ids;
    for (const auto& po : po_list.at("pos")) {
        ids.insert(po.at("po_id").get<std::string>());
    }
    return ids;
}

[[nodiscard]] std::set<std::string> collect_unknown_ids(const fs::path& unknown_path)
{
    if (!fs::exists(unknown_path)) {
        std::println(stderr,
                     "[determinism_jobs] Missing unknown ledger at {} (treating as empty)",
                     unknown_path.string());
        return {};
    }
    Json unknowns = read_json_file(unknown_path);
    std::set<std::string> ids;
    for (const auto& unk : unknowns.at("unknowns")) {
        ids.insert(unk.at("unknown_stable_id").get<std::string>());
    }
    return ids;
}

[[nodiscard]] std::set<std::string> collect_validated_keys(const fs::path& results_path)
{
    Json results = read_json_file(results_path);
    std::set<std::string> ids;
    for (const auto& result : results.at("results")) {
        const std::string po_id = result.at("po_id").get<std::string>();
        const std::string category = result.at("category").get<std::string>();
        const std::string status = result.at("validator_status").get<std::string>();
        const std::string root = result.contains("certificate_root")
                                     ? result.at("certificate_root").get<std::string>()
                                     : "";
        ids.insert(std::format("{}|{}|{}|{}", po_id, category, status, root));
    }
    return ids;
}

[[nodiscard]] bool log_set_mismatch(std::string_view label,
                                    const std::set<std::string>& lhs,
                                    const std::set<std::string>& rhs)
{
    std::println(stderr, "[determinism_jobs] {} mismatch", label);
    for (const auto& item : lhs) {
        if (!rhs.contains(item)) {
            std::println(stderr, "  only in jobs=1: {}", item);
        }
    }
    for (const auto& item : rhs) {
        if (!lhs.contains(item)) {
            std::println(stderr, "  only in jobs=8: {}", item);
        }
    }
    return false;
}

[[nodiscard]] bool compare_sets(std::string_view label,
                                const std::set<std::string>& lhs,
                                const std::set<std::string>& rhs)
{
    if (lhs == rhs) {
        return true;
    }
    return log_set_mismatch(label, lhs, rhs);
}

[[nodiscard]] Options parse_args(int argc, char** argv)
{
    Options options{};
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        auto next_value = [&](std::string& out) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument: " + std::string(arg));
            }
            out = argv[++i];
        };
        if (arg == "--sappp") {
            next_value(options.sappp_path);
        } else if (arg == "--schema-dir") {
            next_value(options.schema_dir);
        } else if (arg == "--repo-root") {
            next_value(options.repo_root);
        } else if (arg == "--compiler") {
            next_value(options.compiler);
        } else {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (options.sappp_path.empty() || options.schema_dir.empty() || options.repo_root.empty()
        || options.compiler.empty()) {
        throw std::runtime_error("Missing required arguments");
    }
    return options;
}

[[nodiscard]] bool write_compile_commands(const fs::path& path,
                                          const Options& options,
                                          const std::vector<fs::path>& inputs)
{
    Json compile_db = Json::array();
    for (const auto& input : inputs) {
        const std::string command =
            std::format("{} -std=c++23 -c {}", options.compiler, input.string());
        compile_db.push_back({
            {"directory", options.repo_root},
            {  "command",           command},
            {     "file",    input.string()}
        });
    }

    return write_json_file(path, compile_db);
}

[[nodiscard]] std::vector<fs::path> collect_litmus_inputs(const Options& options)
{
    std::vector<fs::path> inputs = {
        fs::path(options.repo_root) / "tests" / "end_to_end" / "litmus_div0" / "input.cpp",
        fs::path(options.repo_root) / "tests" / "end_to_end" / "litmus_null" / "input.cpp",
        fs::path(options.repo_root) / "tests" / "end_to_end" / "litmus_oob" / "input.cpp"};

    for (const auto& input : inputs) {
        if (!fs::exists(input)) {
            throw std::runtime_error("Missing litmus input: " + input.string());
        }
    }
    return inputs;
}

[[nodiscard]] CommandSpec make_capture_command(const Options& options, const CaptureSpec& spec)
{
    return CommandSpec{
        .label = "capture",
        .command = std::format("{} capture --compile-commands {} --output {} --repo-root {}",
                               shell_quote(options.sappp_path),
                               shell_quote(spec.compile_commands.string()),
                               shell_quote(spec.capture_dir.string()),
                               shell_quote(options.repo_root))};
}

[[nodiscard]] CommandSpec make_analyze_command(const Options& options, const AnalyzeJobSpec& spec)
{
    return CommandSpec{
        .label = std::format("analyze_jobs={}", spec.jobs),
        .command = std::format("{} analyze --snapshot {} --schema-dir {} --output {} --jobs {}",
                               shell_quote(options.sappp_path),
                               shell_quote(spec.snapshot_path.string()),
                               shell_quote(options.schema_dir),
                               shell_quote(spec.output_dir.string()),
                               spec.jobs)};
}

[[nodiscard]] CommandSpec make_validate_command(const Options& options, const ValidateJobSpec& spec)
{
    return CommandSpec{.label = std::format("validate_jobs={}", spec.jobs),
                       .command = std::format("{} validate --input {} --output {} --schema-dir {}",
                                              shell_quote(options.sappp_path),
                                              shell_quote(spec.input_dir.string()),
                                              shell_quote(spec.output_file.string()),
                                              shell_quote(options.schema_dir))};
}

[[nodiscard]] bool run_analysis_pipeline(const Options& options,
                                         const fs::path& snapshot_path,
                                         int jobs,
                                         const fs::path& output_dir,
                                         const fs::path& validated_path)
{
    const AnalyzeJobSpec analyze_spec{
        .snapshot_path = snapshot_path,
        .output_dir = output_dir,
        .jobs = jobs,
    };
    if (!run_command(make_analyze_command(options, analyze_spec))) {
        return false;
    }

    if (!populate_certstore(output_dir, options.schema_dir)) {
        return false;
    }

    const ValidateJobSpec validate_spec{
        .input_dir = output_dir,
        .output_file = validated_path,
        .jobs = jobs,
    };
    return run_command(make_validate_command(options, validate_spec));
}

[[nodiscard]] bool compare_outputs(const JobResults& job_j1, const JobResults& job_j8)
{
    const auto po_ids_j1 = collect_po_ids(job_j1.output_dir / "po" / "po_list.json");
    const auto po_ids_j8 = collect_po_ids(job_j8.output_dir / "po" / "po_list.json");
    if (!compare_sets("po_id", po_ids_j1, po_ids_j8)) {
        return false;
    }

    const auto unknown_ids_j1 =
        collect_unknown_ids(job_j1.output_dir / "analyzer" / "unknown_ledger.json");
    const auto unknown_ids_j8 =
        collect_unknown_ids(job_j8.output_dir / "analyzer" / "unknown_ledger.json");
    if (!compare_sets("unknown_stable_id", unknown_ids_j1, unknown_ids_j8)) {
        return false;
    }

    const auto results_j1 = collect_validated_keys(job_j1.validated_path);
    const auto results_j8 = collect_validated_keys(job_j8.validated_path);
    return compare_sets("validated_results", results_j1, results_j8);
}

[[nodiscard]] bool run_determinism_jobs(const Options& options)
{
    TempDir temp_dir("sappp_e2e_determinism_jobs");
    fs::path capture_dir = temp_dir.path() / "capture";
    fs::path out_j1 = temp_dir.path() / "out_j1";
    fs::path out_j8 = temp_dir.path() / "out_j8";
    fs::create_directories(capture_dir);
    fs::create_directories(out_j1);
    fs::create_directories(out_j8);

    const auto inputs = collect_litmus_inputs(options);
    fs::path compile_commands = temp_dir.path() / "compile_commands.json";
    if (!write_compile_commands(compile_commands, options, inputs)) {
        return false;
    }

    const CaptureSpec capture_spec{
        .compile_commands = compile_commands,
        .capture_dir = capture_dir,
    };
    if (!run_command(make_capture_command(options, capture_spec))) {
        return false;
    }

    const fs::path snapshot_path = capture_dir / "build_snapshot.json";
    const fs::path validated_j1 = out_j1 / "validated_results.json";
    const fs::path validated_j8 = out_j8 / "validated_results.json";

    if (!run_analysis_pipeline(options, snapshot_path, 1, out_j1, validated_j1)) {
        return false;
    }
    if (!run_analysis_pipeline(options, snapshot_path, 8, out_j8, validated_j8)) {
        return false;
    }

    const JobResults job_j1{
        .output_dir = out_j1,
        .validated_path = validated_j1,
    };
    const JobResults job_j8{
        .output_dir = out_j8,
        .validated_path = validated_j8,
    };
    return compare_outputs(job_j1, job_j8);
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        Options options = parse_args(argc, argv);
        if (!run_determinism_jobs(options)) {
            return 1;
        }

        std::println("[determinism_jobs] Determinism checks passed");
        return 0;
    } catch (const std::exception& ex) {
        std::println(stderr, "[determinism_jobs] Error: {}", ex.what());
        return 1;
    }
}
