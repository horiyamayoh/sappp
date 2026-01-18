/**
 * @file path.cpp
 * @brief Path normalization for deterministic output
 *
 * C++23 modernization:
 * - Using std::ranges::views and algorithms
 * - Using size_t literal suffix (uz)
 */

#include "sappp/common.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <vector>

namespace sappp::common {

namespace {

/**
 * @brief Split a path string into parts using ranges
 */
[[nodiscard]] std::vector<std::string> split_path(std::string_view path)
{
    std::vector<std::string> parts;

    // Use ranges to split by path separators
    for (auto part : path | std::views::split('/')) {
        std::string_view sv(part.begin(), part.end());
        // Handle backslash as well by splitting each part
        for (auto sub : sv | std::views::split('\\')) {
            std::string_view sub_sv(sub.begin(), sub.end());
            if (!sub_sv.empty()) {
                parts.emplace_back(sub_sv);
            }
        }
    }
    return parts;
}

struct PrefixInfo
{
    std::string prefix;
    std::size_t start;
};

[[nodiscard]] std::string normalize_separators(std::string_view input)
{
    std::string path(input);
    std::ranges::replace(path, '\\', '/');
    return path;
}

[[nodiscard]] PrefixInfo extract_prefix(std::string_view path)
{
    PrefixInfo info{.prefix = std::string{}, .start = 0};
    if (path.size() >= 2 && path[1] == ':') {
        info.prefix = std::string(1, static_cast<char>(std::tolower(path[0]))) + ":";
        info.start = 2;
        if (info.start < path.size() && path[info.start] == '/') {
            ++info.start;
        }
        return info;
    }
    if (!path.empty() && path.front() == '/') {
        info.start = 1;
    }
    return info;
}

[[nodiscard]] std::vector<std::string> resolve_parts(const std::vector<std::string>& parts,
                                                     bool absolute_input)
{
    std::vector<std::string> resolved;
    for (const auto& part : parts) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            if (!resolved.empty() && resolved.back() != "..") {
                resolved.pop_back();
                continue;
            }
            if (!absolute_input) {
                resolved.emplace_back("..");
            }
            continue;
        }
        resolved.push_back(part);
    }
    return resolved;
}

[[nodiscard]] std::string
apply_prefix(std::string normalized, std::string_view prefix, bool absolute_input)
{
    if (!prefix.empty()) {
        if (normalized.empty()) {
            return std::string(prefix) + "/";
        }
        return std::string(prefix) + "/" + normalized;
    }
    if (absolute_input) {
        return "/" + normalized;
    }
    return normalized;
}

/**
 * @brief Join path parts with '/' separator using ranges
 */
[[nodiscard]] std::string join_path(const std::vector<std::string>& parts)
{
    if (parts.empty()) {
        return "";
    }

    std::string result;
    // Pre-calculate size for efficiency
    std::size_t total_size = parts.size() - 1UZ;  // separators
    for (const auto& p : parts) {
        total_size += p.size();
    }
    result.reserve(total_size);

    bool first = true;
    for (const auto& p : parts) {
        if (!first) {
            result += '/';
        }
        first = false;
        result += p;
    }
    return result;
}

}  // namespace

bool is_absolute_path(std::string_view path)
{
    if (path.empty()) {
        return false;
    }

    // Unix absolute path
    if (path[0] == '/') {
        return true;
    }

    // Windows absolute path (C:\ or C:/)
    if (path.size() >= 3
        && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
        return true;
    }

    // UNC path
    return path.size() >= 2 && path[0] == '\\' && path[1] == '\\';
}

std::string normalize_path(std::string_view input, std::string_view repo_root)
{
    if (input.empty()) {
        return ".";
    }
    std::string path_str = normalize_separators(input);
    PrefixInfo prefix_info = extract_prefix(path_str);
    const bool absolute_input = is_absolute_path(input);

    auto parts = split_path(path_str.substr(prefix_info.start));
    std::vector<std::string> resolved = resolve_parts(parts, absolute_input);
    std::string normalized = apply_prefix(join_path(resolved), prefix_info.prefix, absolute_input);

    // Make relative to repo_root if provided
    if (!repo_root.empty()) {
        std::string norm_root = normalize_path(repo_root);
        if (normalized.starts_with(norm_root)) {
            normalized = normalized.substr(norm_root.size());
            if (!normalized.empty() && normalized[0] == '/') {
                normalized = normalized.substr(1);
            }
            if (normalized.empty()) {
                normalized = ".";
            }
        }
    }

    return normalized.empty() ? "." : normalized;
}

std::string make_relative(RelativePathSpec spec)
{
    std::string norm_path = normalize_path(spec.path);
    std::string norm_base = normalize_path(spec.base);

    auto path_parts = split_path(norm_path);
    auto base_parts = split_path(norm_base);

    // Find common prefix
    size_t common = 0;
    while (common < path_parts.size() && common < base_parts.size()
           && path_parts[common] == base_parts[common]) {
        ++common;
    }

    // Build relative path
    std::vector<std::string> result;
    const auto drop_count =
        static_cast<std::ranges::range_difference_t<decltype(base_parts)>>(common);
    for (const auto& _ : base_parts | std::views::drop(drop_count)) {
        (void)_;
        result.emplace_back("..");
    }
    for (const auto& part : path_parts | std::views::drop(drop_count)) {
        result.push_back(part);
    }

    return result.empty() ? "." : join_path(result);
}

}  // namespace sappp::common
