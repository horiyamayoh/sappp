/**
 * @file test_po_generator.cpp
 * @brief Tests for PO generator
 */

#include "po_generator.hpp"
#include "sappp/common.hpp"
#include "sappp/schema_validate.hpp"
#include "sappp/version.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace sappp::po::tests {

namespace {

std::filesystem::path write_temp_source() {
    std::filesystem::path temp_root = std::filesystem::temp_directory_path() / "sappp_po_generator_test";
    std::filesystem::create_directories(temp_root);
    std::filesystem::path source_path = temp_root / "sample.cpp";
    std::ofstream out(source_path);
    out << "int main() { return 0; }\n";
    return source_path;
}

nlohmann::json build_minimal_nir(const std::filesystem::path& source_path) {
    std::string tu_id = common::sha256_prefixed("test-tu");
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
                                {
                                    {"id", "I0"},
                                    {"op", "ub.check"},
                                    {"args", nlohmann::json::array({"div0"})},
                                    {"src", {
                                        {"file", source_path.string()},
                                        {"line", 1},
                                        {"col", 1}
                                    }}
                                }
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

} // namespace sappp::po::tests
