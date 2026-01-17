/**
 * @file path.cpp
 * @brief Path normalization for deterministic output
 */

#include "sappp/common.hpp"
#include <algorithm>
#include <sstream>
#include <vector>

namespace sappp::common {

namespace {

std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    std::string current;
    
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }
    return parts;
}

std::string join_path(const std::vector<std::string>& parts) {
    if (parts.empty()) return "";
    
    std::ostringstream oss;
    bool first = true;
    for (const auto& p : parts) {
        if (!first) oss << '/';
        first = false;
        oss << p;
    }
    return oss.str();
}

} // namespace

bool is_absolute_path(std::string_view path) {
    if (path.empty()) return false;
    
    // Unix absolute path
    if (path[0] == '/') return true;
    
    // Windows absolute path (C:\ or C:/)
    if (path.size() >= 3 && 
        ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\')) {
        return true;
    }
    
    // UNC path
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        return true;
    }
    
    return false;
}

std::string normalize_path(std::string_view input, std::string_view repo_root) {
    if (input.empty()) return "";
    
    std::string path_str(input);
    
    // Replace backslashes with forward slashes
    std::replace(path_str.begin(), path_str.end(), '\\', '/');
    
    // Handle Windows drive letters - strip for normalization
    std::string prefix;
    size_t start = 0;
    if (path_str.size() >= 2 && path_str[1] == ':') {
        // Convert drive letter to lowercase for consistency
        prefix = std::string(1, static_cast<char>(std::tolower(path_str[0]))) + ":";
        start = 2;
        if (start < path_str.size() && path_str[start] == '/') {
            start++;
        }
    } else if (!path_str.empty() && path_str[0] == '/') {
        start = 1;
    }
    
    // Split and resolve . and ..
    auto parts = split_path(path_str.substr(start));
    std::vector<std::string> resolved;
    
    for (const auto& part : parts) {
        if (part == ".") {
            continue;
        } else if (part == "..") {
            if (!resolved.empty() && resolved.back() != "..") {
                resolved.pop_back();
            } else if (!is_absolute_path(input)) {
                resolved.push_back("..");
            }
        } else {
            resolved.push_back(part);
        }
    }
    
    std::string normalized = join_path(resolved);
    
    // Add leading slash for absolute paths (Unix style)
    if (is_absolute_path(input) && prefix.empty()) {
        normalized = "/" + normalized;
    }
    
    // Make relative to repo_root if provided
    if (!repo_root.empty()) {
        std::string norm_root = normalize_path(repo_root);
        if (normalized.find(norm_root) == 0) {
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

std::string make_relative(std::string_view path, std::string_view base) {
    std::string norm_path = normalize_path(path);
    std::string norm_base = normalize_path(base);
    
    auto path_parts = split_path(norm_path);
    auto base_parts = split_path(norm_base);
    
    // Find common prefix
    size_t common = 0;
    while (common < path_parts.size() && 
           common < base_parts.size() && 
           path_parts[common] == base_parts[common]) {
        ++common;
    }
    
    // Build relative path
    std::vector<std::string> result;
    for (size_t i = common; i < base_parts.size(); ++i) {
        result.push_back("..");
    }
    for (size_t i = common; i < path_parts.size(); ++i) {
        result.push_back(path_parts[i]);
    }
    
    return result.empty() ? "." : join_path(result);
}

} // namespace sappp::common
