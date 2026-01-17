/**
 * @file test_po_generator.cpp
 * @brief Tests for PO generator
 */

#include "po_generator.hpp"
#include "sappp/canonical_json.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace sappp::po::tests {

namespace {

std::filesystem::path write_temp_source(const std::string& contents = "int main() { return 0; }\n") {
    std::filesystem::path temp_root = std::filesystem::temp_directory_path() / "sappp_po_generator_test";
    std::filesystem::create_directories(temp_root);
    std::filesystem::path source_path = temp_root / "sample.cpp";
    std::ofstream out(source_path);
    out << contents;
    return source_path;
}

std::string read_file_contents(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

nlohmann::json build_minimal_nir(const std::filesystem::path& source_path,
                                 const std::string& op = "ub.check",
                                 const nlohmann::json& args = nlohmann::json::array({"div0", true})) {
    std::string tu_id = common::sha256_prefixed("test-tu");
    nlohmann::json inst = {
        {"id", "I0"},
        {"op", op},
        {"src", {
            {"file", source_path.string()},
            {"line", 1},
            {"col", 1}
        }}
    };
    if (args.is_array() && !args.empty()) {
        inst["args"] = args;
    }
    return nlohmann::json{
        {"schema_version", "nir.v1"},
        {"tool", {
            {"name", "sappp"},
            {"version", sappp::VERSION},
            {"build_id", sappp::BUILD_ID}
        }},
        {"generated_at", "2024-01-01T00:00:00Z"},
        {"tu_id", tu_id},
        {"semantics_version", sappp::SEMANTICS_VERSION},
        {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
        {"profile_version", sappp::PROFILE_VERSION},
        {"functions", nlohmann::json::array({
            {
                {"function_uid", "f1"},
                {"mangled_name", "_Z1fv"},
                {"cfg", {
                    {"entry", "B0"},
                    {"blocks", nlohmann::json::array({
                        {
                            {"id", "B0"},
                            {"insts", nlohmann::json::array({
                                inst
                            })}
                        }
                    })},
                    {"edges", nlohmann::json::array()}
                }}
            }
        })}
    };
}

} // namespace

TEST(PoGeneratorTest, GeneratesPoAndValidatesSchema) {
    std::filesystem::path source_path = write_temp_source();
    nlohmann::json nir = build_minimal_nir(source_path);

    PoGenerator generator;
    nlohmann::json po_list = generator.generate(nir);

    ASSERT_TRUE(po_list.contains("pos"));
    EXPECT_GE(po_list.at("pos").size(), 1U);
    const auto& first = po_list.at("pos").at(0);
    EXPECT_EQ(first.at("po_kind").get<std::string>(), "UB.DivZero");
    ASSERT_TRUE(first.contains("predicate"));
    const auto& pred = first.at("predicate");
    ASSERT_TRUE(pred.contains("expr"));
    const auto& expr = pred.at("expr");
    ASSERT_TRUE(expr.contains("args"));
    const auto& args = expr.at("args");
    ASSERT_TRUE(args.is_array());
    ASSERT_EQ(args.size(), 2U);
    EXPECT_EQ(args.at(0).get<std::string>(), "UB.DivZero");
    EXPECT_EQ(args.at(1).get<bool>(), true);

    std::string schema_error;
    EXPECT_TRUE(common::validate_json(po_list,
                                      std::string(SAPPP_SCHEMA_DIR) + "/po.v1.schema.json",
                                      schema_error))
        << schema_error;
}

TEST(PoGeneratorTest, PoIdIsDeterministic) {
    std::filesystem::path source_path = write_temp_source();
    nlohmann::json nir = build_minimal_nir(source_path);

    PoGenerator generator;
    nlohmann::json first = generator.generate(nir);
    nlohmann::json second = generator.generate(nir);

    ASSERT_FALSE(first.at("pos").empty());
    ASSERT_FALSE(second.at("pos").empty());

    std::string first_id = first.at("pos").at(0).at("po_id").get<std::string>();
    std::string second_id = second.at("pos").at(0).at("po_id").get<std::string>();

    EXPECT_EQ(first_id, second_id);
}

TEST(PoGeneratorTest, PoIdMatchesSpec) {
    const std::string contents = "int main() { return 0; }\n";
    std::filesystem::path source_path = write_temp_source(contents);
    nlohmann::json nir = build_minimal_nir(source_path);

    PoGenerator generator;
    nlohmann::json po_list = generator.generate(nir);

    const auto& po = po_list.at("pos").at(0);
    nlohmann::json repo_identity = {
        {"path", common::normalize_path(source_path.string())},
        {"content_sha256", common::sha256_prefixed(read_file_contents(source_path))}
    };
    nlohmann::json anchor = {
        {"block_id", "B0"},
        {"inst_id", "I0"}
    };
    nlohmann::json po_id_input = {
        {"repo_identity", repo_identity},
        {"function", {{"usr", "f1"}}},
        {"anchor", anchor},
        {"po_kind", "UB.DivZero"},
        {"semantics_version", sappp::SEMANTICS_VERSION},
        {"proof_system_version", sappp::PROOF_SYSTEM_VERSION},
        {"profile_version", sappp::PROFILE_VERSION}
    };
    std::string expected_id = sappp::canonical::hash_canonical(po_id_input);

    EXPECT_EQ(po.at("po_id").get<std::string>(), expected_id);
}

TEST(PoGeneratorTest, SinkMarkerGeneratesPo) {
    std::filesystem::path source_path = write_temp_source();
    nlohmann::json nir = build_minimal_nir(source_path, "sink.marker",
                                           nlohmann::json::array({"OOB"}));

    PoGenerator generator;
    nlohmann::json po_list = generator.generate(nir);

    ASSERT_FALSE(po_list.at("pos").empty());
    const auto& po = po_list.at("pos").at(0);
    EXPECT_EQ(po.at("po_kind").get<std::string>(), "UB.OutOfBounds");
    EXPECT_EQ(po.at("predicate").at("expr").at("op").get<std::string>(), "sink.marker");
}

} // namespace sappp::po::tests
