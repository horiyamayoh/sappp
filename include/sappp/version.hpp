#pragma once

/**
 * @file version.hpp
 * @brief SAP++ version information
 *
 * Naming convention: kPascalCase for constants (Google C++ Style Guide)
 */

#include <string>

namespace sappp {

/// SAP++ version string
constexpr const char* kVersion = "0.1.0";

/// Build identifier
constexpr const char* kBuildId = "dev";

/// Semantic versions (embedded in all outputs)
constexpr const char* kSemanticsVersion = "sem.v1";
constexpr const char* kProofSystemVersion = "proof.v1";
constexpr const char* kProfileVersion = "safety.core.v1";

struct VersionTriple
{
    std::string semantics;
    std::string proof_system;
    std::string profile;
};

[[nodiscard]] inline VersionTriple default_version_triple()
{
    return VersionTriple{.semantics = kSemanticsVersion,
                         .proof_system = kProofSystemVersion,
                         .profile = kProfileVersion};
}

}  // namespace sappp
