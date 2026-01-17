#pragma once

/**
 * @file version.hpp
 * @brief SAP++ version information
 */

namespace sappp {

constexpr const char* VERSION = "0.1.0";
constexpr const char* BUILD_ID = "dev";

// Semantic versions (embedded in all outputs)
constexpr const char* SEMANTICS_VERSION = "sem.v1";
constexpr const char* PROOF_SYSTEM_VERSION = "proof.v1";
constexpr const char* PROFILE_VERSION = "safety.core.v1";

} // namespace sappp
