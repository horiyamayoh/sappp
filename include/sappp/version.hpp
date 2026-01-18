#pragma once

/**
 * @file version.hpp
 * @brief SAP++ version information
 *
 * Naming convention: kPascalCase for constants (Google C++ Style Guide)
 */

namespace sappp {

/// SAP++ version string
constexpr const char* kVersion = "0.1.0";

/// Build identifier
constexpr const char* kBuildId = "dev";

/// Semantic versions (embedded in all outputs)
constexpr const char* kSemanticsVersion = "sem.v1";
constexpr const char* kProofSystemVersion = "proof.v1";
constexpr const char* kProfileVersion = "safety.core.v1";

}  // namespace sappp
