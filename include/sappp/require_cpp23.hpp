#pragma once

/**
 * @file require_cpp23.hpp
 * @brief C++23 feature-test macros for SAP++ (CODING_STYLE_CPP23.md Section 2.4)
 *
 * This header verifies at compile time that the standard library provides
 * all C++23 features required by SAP++. Include this header early in your
 * translation unit (e.g., in a precompiled header or main.cpp) to get
 * clear error messages if the toolchain is insufficient.
 *
 * Required compiler versions:
 *   - GCC 14.0+
 *   - Clang 19.0+
 */

#include <expected>
#include <version>

// =============================================================================
// C++23 Language Standard Check
// =============================================================================

#if !defined(__cplusplus) || __cplusplus < 202'302L
    #error "SAP++ requires C++23 or later (__cplusplus >= 202302L)."
#endif

// =============================================================================
// std::print / std::println (__cpp_lib_print)
// =============================================================================
// Required for: Console output (replaces std::cout)
// Minimum value: 202207L

#if !defined(__cpp_lib_print) || __cpp_lib_print < 202'207L
    #error "SAP++ requires std::print/std::println (__cpp_lib_print >= 202207L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::expected (__cpp_lib_expected)
// =============================================================================
// Required for: Error handling (replaces exceptions)
// Minimum value: 202202L

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202'202L
    #error "SAP++ requires std::expected (__cpp_lib_expected >= 202202L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::views::enumerate (__cpp_lib_ranges_enumerate)
// =============================================================================
// Required for: Indexed iteration (replaces manual index loops)
// Minimum value: 202302L

#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202'302L
    #error "SAP++ requires std::views::enumerate (__cpp_lib_ranges_enumerate >= 202302L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::rotl / std::rotr (__cpp_lib_bitops)
// =============================================================================
// Required for: Bit rotation operations
// Minimum value: 201907L

#if !defined(__cpp_lib_bitops) || __cpp_lib_bitops < 201'907L
    #error "SAP++ requires <bit> bit operations (__cpp_lib_bitops >= 201907L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::byteswap (__cpp_lib_byteswap)
// =============================================================================
// Required for: Endian conversion
// Minimum value: 202110L

#if !defined(__cpp_lib_byteswap) || __cpp_lib_byteswap < 202'110L
    #error "SAP++ requires std::byteswap (__cpp_lib_byteswap >= 202110L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::to_underlying (__cpp_lib_to_underlying)
// =============================================================================
// Required for: enum to underlying type conversion (replaces static_cast)
// Minimum value: 202102L

#if !defined(__cpp_lib_to_underlying) || __cpp_lib_to_underlying < 202'102L
    #error "SAP++ requires std::to_underlying (__cpp_lib_to_underlying >= 202102L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::format (__cpp_lib_format)
// =============================================================================
// Required for: String formatting (used extensively with std::print)
// Minimum value: 202110L

#if !defined(__cpp_lib_format) || __cpp_lib_format < 202'110L
    #error "SAP++ requires std::format (__cpp_lib_format >= 202110L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// std::ranges (__cpp_lib_ranges)
// =============================================================================
// Required for: Range algorithms and views
// Minimum value: 202110L

#if !defined(__cpp_lib_ranges) || __cpp_lib_ranges < 202'110L
    #error "SAP++ requires std::ranges (__cpp_lib_ranges >= 202110L). " \
       "Please use GCC 14+ or Clang 19+ with a compatible standard library."
#endif

// =============================================================================
// Validation Summary
// =============================================================================
// If we reach here, all required C++23 features are available.
// This macro can be used for conditional compilation if needed.

#define SAPPP_CPP23_FEATURES_VERIFIED 1

namespace sappp::compat {

/**
 * @brief Runtime verification that all required C++23 features are available.
 *
 * This function is constexpr and always returns true if compilation succeeds.
 * It can be used in static_assert or runtime checks for defensive programming.
 *
 * @return true if all C++23 features are verified (always true at runtime)
 */
[[nodiscard]] constexpr bool verify_cpp23_features() noexcept
{
    return true;
}

}  // namespace sappp::compat
