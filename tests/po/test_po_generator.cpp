#include "sappp/po.hpp"

#include "sappp/schema_validate.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace sappp::po::test {

namespace {

std::string schema_path(const std::string& name) {
    return std::string(SAPPP_SCHEMA_DIR) + "/" + name;
}

std::string sha256_hex() {
    return "sha256:" + std::string(64, 'b');
}

nlohmann::json make_min_nir() {
    return nlohmann::json{
        {"schema_version", "nir.v1"},
        {"tool", {{"name", "sappp"}, {"version", "0.1.0"}}},
        {"generated_at", "2024-01-01T00:00:00Z"},
        {"tu_id", sha256_hex()},
        {"semantics_version", "sem.v1"},
        {"proof_system_version", "proof.v1"},
        {"profile_version", "profile.v1"},
        {"functions", nlohmann::json::array({
            nlohmann::json{
                {"function_uid", "usr::f"},
                {"mangled_name", "_Z1fv"},
                {"cfg", {
                    {"entry", "B0"},
                    {"blocks", nlohmann::json::array({
                        nlohmann::json{
                            {"id", "B0"},
                            {"insts", nlohmann::json::array({
                                nlohmann::json{
                                    {"id", "I0"},
                                    {"op", "ub.check"},
                                    {"src", {{"file", "src/main.cpp"}, {"line", 1}, {"col", 1}}}
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

TEST(PoGeneratorTest, GeneratesValidPoList) {
    PoGenerator generator(SAPPP_SCHEMA_DIR);
    nlohmann::json nir = make_min_nir();
    nlohmann::json po_list = generator.generate(nir);

    std::string error;
    bool ok = sappp::common::validate_json(
        po_list,
        schema_path("po.v1.schema.json"),
        error);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(po_list.at("pos").size(), 1);
    EXPECT_EQ(po_list.at("pos").at(0).at("po_kind").get<std::string>(), "div0");
}

} // namespace sappp::po::test
