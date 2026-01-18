/**
 * @file sha256.cpp
 * @brief SHA-256 implementation (standalone, no external dependency)
 *
 * C++23 modernization:
 * - All helper functions are constexpr
 * - Using std::rotl/rotr from <bit>
 * - Using std::byteswap for endian conversion
 * - Using std::views::enumerate for indexed iteration
 * - Using size_t literal suffix (uz)
 */

#include "sappp/common.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <format>
#include <ranges>
#include <span>

namespace sappp::common {

namespace {

// SHA-256 constants (constexpr)
constexpr std::array<uint32_t, 64> kRoundConstants = {
    {0x42'8a'2f'98, 0x71'37'44'91, 0xb5'c0'fb'cf, 0xe9'b5'db'a5, 0x39'56'c2'5b, 0x59'f1'11'f1,
     0x92'3f'82'a4, 0xab'1c'5e'd5, 0xd8'07'aa'98, 0x12'83'5b'01, 0x24'31'85'be, 0x55'0c'7d'c3,
     0x72'be'5d'74, 0x80'de'b1'fe, 0x9b'dc'06'a7, 0xc1'9b'f1'74, 0xe4'9b'69'c1, 0xef'be'47'86,
     0x0f'c1'9d'c6, 0x24'0c'a1'cc, 0x2d'e9'2c'6f, 0x4a'74'84'aa, 0x5c'b0'a9'dc, 0x76'f9'88'da,
     0x98'3e'51'52, 0xa8'31'c6'6d, 0xb0'03'27'c8, 0xbf'59'7f'c7, 0xc6'e0'0b'f3, 0xd5'a7'91'47,
     0x06'ca'63'51, 0x14'29'29'67, 0x27'b7'0a'85, 0x2e'1b'21'38, 0x4d'2c'6d'fc, 0x53'38'0d'13,
     0x65'0a'73'54, 0x76'6a'0a'bb, 0x81'c2'c9'2e, 0x92'72'2c'85, 0xa2'bf'e8'a1, 0xa8'1a'66'4b,
     0xc2'4b'8b'70, 0xc7'6c'51'a3, 0xd1'92'e8'19, 0xd6'99'06'24, 0xf4'0e'35'85, 0x10'6a'a0'70,
     0x19'a4'c1'16, 0x1e'37'6c'08, 0x27'48'77'4c, 0x34'b0'bc'b5, 0x39'1c'0c'b3, 0x4e'd8'aa'4a,
     0x5b'9c'ca'4f, 0x68'2e'6f'f3, 0x74'8f'82'ee, 0x78'a5'63'6f, 0x84'c8'78'14, 0x8c'c7'02'08,
     0x90'be'ff'fa, 0xa4'50'6c'eb, 0xbe'f9'a3'f7, 0xc6'71'78'f2}
};

// SHA-256 helper functions (all constexpr, using C++23 std::rotr)
[[nodiscard]] constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) noexcept
{
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr uint32_t sigma0(uint32_t x) noexcept
{
    return std::rotr(x, 2U) ^ std::rotr(x, 13U) ^ std::rotr(x, 22U);
}

[[nodiscard]] constexpr uint32_t sigma1(uint32_t x) noexcept
{
    return std::rotr(x, 6U) ^ std::rotr(x, 11U) ^ std::rotr(x, 25U);
}

[[nodiscard]] constexpr uint32_t gamma0(uint32_t x) noexcept
{
    return std::rotr(x, 7U) ^ std::rotr(x, 18U) ^ (x >> 3U);
}

[[nodiscard]] constexpr uint32_t gamma1(uint32_t x) noexcept
{
    return std::rotr(x, 17U) ^ std::rotr(x, 19U) ^ (x >> 10U);
}

class SHA256
{
public:
    SHA256() { reset(); }

    void reset()
    {
        m_state = std::array<uint32_t, 8>{
            {0x6a'09'e6'67,
             0xbb'67'ae'85, 0x3c'6e'f3'72,
             0xa5'4f'f5'3a, 0x51'0e'52'7f,
             0x9b'05'68'8c, 0x1f'83'd9'ab,
             0x5b'e0'cd'19}
        };
        m_count = 0;
        m_buffer_len = 0;
    }

    void update(std::span<const std::byte> data)
    {
        for (auto byte : data) {
            m_buffer.at(m_buffer_len) = std::to_integer<uint8_t>(byte);
            ++m_buffer_len;
            if (m_buffer_len == m_buffer.size()) {
                transform();
                m_count += 512;
                m_buffer_len = 0;
            }
        }
    }

    void update(std::span<const uint8_t> data) { update(std::as_bytes(data)); }

    void update(std::string_view data) { update(std::as_bytes(std::span(data))); }

    std::array<uint8_t, 32> finalize()
    {
        uint64_t total_bits = m_count + (m_buffer_len * 8);

        // Padding
        m_buffer.at(m_buffer_len) = 0x80;
        ++m_buffer_len;
        if (m_buffer_len > 56) {
            while (m_buffer_len < 64) {
                m_buffer.at(m_buffer_len) = 0;
                ++m_buffer_len;
            }
            transform();
            m_buffer_len = 0;
        }
        while (m_buffer_len < 56) {
            m_buffer.at(m_buffer_len) = 0;
            ++m_buffer_len;
        }

        // Length (big-endian)
        for (auto i : std::views::iota(0, 8) | std::views::reverse) {
            const auto shift = static_cast<uint64_t>(i) * 8U;
            m_buffer.at(m_buffer_len) = static_cast<uint8_t>(total_bits >> shift);
            ++m_buffer_len;
        }
        transform();

        // Output (big-endian) using views::enumerate
        std::array<uint8_t, 32> hash{};
        for (auto [i, state] : std::views::enumerate(m_state)) {
            const std::size_t idx = static_cast<std::size_t>(i) * std::size_t{4};
            hash.at(idx) = static_cast<uint8_t>(state >> 24);
            hash.at(idx + std::size_t{1}) = static_cast<uint8_t>(state >> 16);
            hash.at(idx + std::size_t{2}) = static_cast<uint8_t>(state >> 8);
            hash.at(idx + std::size_t{3}) = static_cast<uint8_t>(state);
        }
        return hash;
    }

private:
    void transform()
    {
        std::array<uint32_t, 64> schedule{};

        // Prepare message schedule using std::byteswap for big-endian conversion
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t buffer_index = i * std::size_t{4};
            uint32_t val{};
            std::memcpy(&val, &m_buffer.at(buffer_index), sizeof(val));
            if constexpr (std::endian::native == std::endian::little) {
                schedule.at(i) = std::byteswap(val);
            } else {
                schedule.at(i) = val;
            }
        }
        for (std::size_t i = 16; i < schedule.size(); ++i) {
            schedule.at(i) = gamma1(schedule.at(i - 2)) + schedule.at(i - 7)
                             + gamma0(schedule.at(i - 15)) + schedule.at(i - 16);
        }

        uint32_t a = m_state.at(0);
        uint32_t b = m_state.at(1);
        uint32_t c = m_state.at(2);
        uint32_t d = m_state.at(3);
        uint32_t e = m_state.at(4);
        uint32_t f = m_state.at(5);
        uint32_t g = m_state.at(6);
        uint32_t h = m_state.at(7);

        for (auto [i, w_val] : std::views::enumerate(schedule)) {
            const auto idx = static_cast<std::size_t>(i);
            uint32_t t1 = h + sigma1(e) + ch(e, f, g) + kRoundConstants.at(idx) + w_val;
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        m_state.at(0) += a;
        m_state.at(1) += b;
        m_state.at(2) += c;
        m_state.at(3) += d;
        m_state.at(4) += e;
        m_state.at(5) += f;
        m_state.at(6) += g;
        m_state.at(7) += h;
    }

    std::array<uint32_t, 8> m_state{};
    std::array<uint8_t, 64> m_buffer{};
    std::size_t m_buffer_len = 0;
    uint64_t m_count = 0;
};

[[nodiscard]] std::string to_hex(const std::array<uint8_t, 32>& hash)
{
    std::string result;
    result.reserve(64);
    for (uint8_t b : hash) {
        result += std::format("{:02x}", b);
    }
    return result;
}

}  // namespace

std::string sha256(std::string_view data)
{
    SHA256 hasher;
    hasher.update(data);
    return to_hex(hasher.finalize());
}

std::string sha256_prefixed(std::string_view data)
{
    return "sha256:" + sha256(data);
}

}  // namespace sappp::common
