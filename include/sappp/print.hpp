#pragma once

#if __has_include(<print>)
    #include <print>
#else
    #include <cstdio>
    #include <format>
    #include <string>
    #include <tuple>
    #include <utility>

namespace std {

template <typename... Args>
[[nodiscard]] std::string format_text(std::format_string<Args...> fmt, Args&&... args)
{
    auto packed = std::make_tuple(std::forward<Args>(args)...);
    return std::apply(
        [&fmt](auto&... unpacked) {
            return std::vformat(fmt.get(), std::make_format_args(unpacked...));
        },
        packed);
}

template <typename... Args>
void print(std::format_string<Args...> fmt, Args&&... args)
{
    auto text = format_text(fmt, std::forward<Args>(args)...);
    std::fwrite(text.data(), 1, text.size(), stdout);
}

template <typename... Args>
void print(FILE* stream, std::format_string<Args...> fmt, Args&&... args)
{
    auto text = format_text(fmt, std::forward<Args>(args)...);
    std::fwrite(text.data(), 1, text.size(), stream);
}

template <typename... Args>
void println(std::format_string<Args...> fmt, Args&&... args)
{
    auto text = format_text(fmt, std::forward<Args>(args)...);
    text.push_back('\n');
    std::fwrite(text.data(), 1, text.size(), stdout);
}

template <typename... Args>
void println(FILE* stream, std::format_string<Args...> fmt, Args&&... args)
{
    auto text = format_text(fmt, std::forward<Args>(args)...);
    text.push_back('\n');
    std::fwrite(text.data(), 1, text.size(), stream);
}

}  // namespace std
#endif
