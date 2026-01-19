// bench_canonical.cpp - Canonical JSON のベンチマーク
//
// 決定性に関わる重要なコンポーネントの性能を測定します。
// 性能回帰を検出するための基準値を提供します。

#include <sappp/canonical_json.hpp>
#include <sappp/common.hpp>

#include <random>
#include <sstream>
#include <string>

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>

namespace {

// ===========================================================================
// テストデータ生成
// ===========================================================================

// 小さなJSONオブジェクト（典型的なPO）
nlohmann::json create_small_json()
{
    return nlohmann::json{
        {          "po_id", "po_div0_func_block0_inst5_sem.v1_proof.v1_safety.core.v1"},
        {        "po_kind",                                                     "div0"},
        {   "function_uid",                                       "c:@F@test_function"},
        {       "block_id",                                                  "block_0"},
        {        "inst_id",                                                   "inst_5"},
        {"source_location",       {{"file", "test.cpp"}, {"line", 42}, {"column", 10}}},
    };
}

// 中規模JSONオブジェクト（典型的なNIR関数）
nlohmann::json create_medium_json()
{
    nlohmann::json blocks = nlohmann::json::array();
    for (int i = 0; i < 10; ++i) {
        nlohmann::json insts = nlohmann::json::array();
        for (int j = 0; j < 20; ++j) {
            insts.push_back({
                { "inst_id",      "inst_" + std::to_string(j)},
                {  "opcode",                            "add"},
                {"operands", nlohmann::json::array({1, 2, 3})},
            });
        }
        blocks.push_back({
            {    "block_id", "block_" + std::to_string(i)},
            {"instructions",                        insts},
        });
    }

    return nlohmann::json{
        {"function_uid", "c:@F@medium_function"},
        { "entry_block",              "block_0"},
        {      "blocks",                 blocks},
    };
}

// 大規模JSONオブジェクト（典型的なNIRファイル）
nlohmann::json create_large_json()
{
    nlohmann::json functions = nlohmann::json::array();
    for (int f = 0; f < 50; ++f) {
        nlohmann::json blocks = nlohmann::json::array();
        for (int i = 0; i < 10; ++i) {
            nlohmann::json insts = nlohmann::json::array();
            for (int j = 0; j < 20; ++j) {
                insts.push_back({
                    { "inst_id",      "inst_" + std::to_string(j)},
                    {  "opcode",                            "add"},
                    {"operands", nlohmann::json::array({1, 2, 3})},
                });
            }
            blocks.push_back({
                {    "block_id", "block_" + std::to_string(i)},
                {"instructions",                        insts},
            });
        }
        functions.push_back({
            {"function_uid", "c:@F@func_" + std::to_string(f)},
            { "entry_block",                        "block_0"},
            {      "blocks",                           blocks},
        });
    }

    return nlohmann::json{
        {"schema_version",  "nir.v1"},
        {     "functions", functions},
    };
}

// キー順序がランダムなJSON（カノニカル化の効果を測定）
nlohmann::json create_unordered_json()
{
    // nlohmann::json はデフォルトでキー順序を保持しないため、
    // 明示的に順序を変えて挿入
    nlohmann::json j;
    j["zebra"] = 1;
    j["apple"] = 2;
    j["mango"] = 3;
    j["banana"] = 4;
    j["nested"] = {
        {"zzz",                                1},
        {"aaa",                                2},
        {"mmm", {{"inner_z", 1}, {"inner_a", 2}}},
    };
    return j;
}

// ===========================================================================
// Canonical JSON ベンチマーク
// ===========================================================================

static void BM_Canonicalize_Small(benchmark::State& state)
{
    auto json = create_small_json();
    for (auto _ : state) {
        auto result = sappp::canonical::canonicalize(json);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Canonicalize_Small);

static void BM_Canonicalize_Medium(benchmark::State& state)
{
    auto json = create_medium_json();
    for (auto _ : state) {
        auto result = sappp::canonical::canonicalize(json);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Canonicalize_Medium);

static void BM_Canonicalize_Large(benchmark::State& state)
{
    auto json = create_large_json();
    for (auto _ : state) {
        auto result = sappp::canonical::canonicalize(json);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Canonicalize_Large);

static void BM_Canonicalize_Unordered(benchmark::State& state)
{
    auto json = create_unordered_json();
    for (auto _ : state) {
        auto result = sappp::canonical::canonicalize(json);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Canonicalize_Unordered);

// ===========================================================================
// SHA256 ベンチマーク
// ===========================================================================

static void BM_SHA256_Small(benchmark::State& state)
{
    std::string data = "Hello, World!";
    for (auto _ : state) {
        auto result = sappp::common::sha256(data);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
}
BENCHMARK(BM_SHA256_Small);

static void BM_SHA256_1KB(benchmark::State& state)
{
    std::string data(1'024, 'x');
    for (auto _ : state) {
        auto result = sappp::common::sha256(data);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
}
BENCHMARK(BM_SHA256_1KB);

static void BM_SHA256_1MB(benchmark::State& state)
{
    std::string data(1'024 * 1'024, 'x');
    for (auto _ : state) {
        auto result = sappp::common::sha256(data);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(data.size()));
}
BENCHMARK(BM_SHA256_1MB);

// ===========================================================================
// Canonical Hash（canonicalize + SHA256）ベンチマーク
// ===========================================================================

static void BM_CanonicalHash_Small(benchmark::State& state)
{
    auto json = create_small_json();
    for (auto _ : state) {
        auto canonical = sappp::canonical::canonicalize(json);
        if (canonical) {
            auto hash = sappp::common::sha256(*canonical);
            benchmark::DoNotOptimize(hash);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CanonicalHash_Small);

static void BM_CanonicalHash_Medium(benchmark::State& state)
{
    auto json = create_medium_json();
    for (auto _ : state) {
        auto canonical = sappp::canonical::canonicalize(json);
        if (canonical) {
            auto hash = sappp::common::sha256(*canonical);
            benchmark::DoNotOptimize(hash);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CanonicalHash_Medium);

static void BM_CanonicalHash_Large(benchmark::State& state)
{
    auto json = create_large_json();
    for (auto _ : state) {
        auto canonical = sappp::canonical::canonicalize(json);
        if (canonical) {
            auto hash = sappp::common::sha256(*canonical);
            benchmark::DoNotOptimize(hash);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CanonicalHash_Large);

}  // namespace
