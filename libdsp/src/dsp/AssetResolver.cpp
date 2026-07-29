#include "dsp/AssetResolver.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>

namespace graphx::dsp::dashboard {

AssetResolver::AssetResolver(const Options& options) : options_(options) {
    if (!std::filesystem::exists(options.asset_root)) {
        throw std::runtime_error("Asset root does not exist: " +
                                 options.asset_root.string());
    }
    if (!std::filesystem::is_directory(options.asset_root)) {
        throw std::runtime_error("Asset root is not a directory: " +
                                 options.asset_root.string());
    }

    // Get canonical path
    try {
        canonical_root_ = std::filesystem::canonical(options.asset_root);
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Failed to canonicalize asset root: " +
                                 std::string(e.what()));
    }
}

std::string AssetResolver::NormalizePath(std::string_view path) {
    std::string normalized;
    std::istringstream iss{std::string(path)};
    std::string component;

    // Split by /
    std::vector<std::string> components;
    while (std::getline(iss, component, '/')) {
        if (component == "." || component.empty()) {
            // Skip . and empty (from //)
            continue;
        } else if (component == "..") {
            // Go up one level if possible
            if (!components.empty()) {
                components.pop_back();
            }
        } else {
            // Normal component
            components.push_back(component);
        }
    }

    // Reconstruct path
    for (const auto& comp : components) {
        normalized += "/" + comp;
    }

    return normalized.empty() ? "/" : normalized;
}

bool AssetResolver::ContainsForbiddenSequences(std::string_view path) {
    // Check for /../ or /.. patterns
    if (path.find("/../") != std::string::npos) {
        return true;
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == "/..") {
        return true;
    }

    // Check for .. as standalone component
    size_t pos = 0;
    while ((pos = path.find("..", pos)) != std::string::npos) {
        // Check if it's a standalone component
        bool start_ok = (pos == 0 || path[pos - 1] == '/');
        bool end_ok = (pos + 2 >= path.size() || path[pos + 2] == '/');
        if (start_ok && end_ok) {
            return true;
        }
        pos += 2;
    }

    return false;
}

std::optional<std::filesystem::path> AssetResolver::ResolveSafePath(
    std::string_view request_path) {
    // Check length
    if (request_path.size() > options_.max_path_length) {
        return std::nullopt;
    }

    // Check for forbidden sequences
    if (ContainsForbiddenSequences(request_path)) {
        return std::nullopt;
    }

    // Normalize the path
    std::string normalized = NormalizePath(request_path);

    // Remove leading slash for joining
    if (!normalized.empty() && normalized[0] == '/') {
        normalized = normalized.substr(1);
    }

    // Join with root
    std::filesystem::path full_path = canonical_root_ / normalized;

    // Check if file exists and is safe
    try {
        if (!std::filesystem::exists(full_path)) {
            return std::nullopt;
        }

        // Reject symlinks if not allowed
        if (!options_.allow_symlinks &&
            std::filesystem::is_symlink(full_path)) {
            return std::nullopt;
        }

        // Only serve regular files
        if (!std::filesystem::is_regular_file(full_path)) {
            return std::nullopt;
        }

        // Get canonical path to verify it's under root
        std::filesystem::path canonical_path =
            std::filesystem::canonical(full_path);

        // Verify it's under the asset root
        auto rel = std::filesystem::relative(canonical_path, canonical_root_);
        if (rel.string().find("..") == 0) {
            // Path escaped the root
            return std::nullopt;
        }

        return canonical_path;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

bool AssetResolver::IsSafeFile(const std::filesystem::path& path) {
    try {
        // Must be a regular file
        if (!std::filesystem::is_regular_file(path)) {
            return false;
        }

        // Reject symlinks
        if (std::filesystem::is_symlink(path)) {
            return false;
        }

        // Verify it's under the asset root
        std::filesystem::path canonical_path =
            std::filesystem::canonical(path);
        auto rel = std::filesystem::relative(canonical_path, canonical_root_);
        if (rel.string().find("..") == 0) {
            return false;
        }

        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

const std::filesystem::path& AssetResolver::GetAssetRoot() const {
    return canonical_root_;
}

std::vector<std::filesystem::path> AssetResolver::ListAssets() {
    std::vector<std::filesystem::path> assets;

    try {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(canonical_root_)) {
            if (entry.is_regular_file() && !entry.is_symlink()) {
                assets.push_back(
                    std::filesystem::relative(entry.path(), canonical_root_));
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error listing assets: " << e.what() << std::endl;
    }

    return assets;
}

bool AssetResolver::VerifyNoSourceFiles() {
    std::vector<std::string> forbidden_extensions = {
        ".ts", ".tsx", ".js", ".jsx", ".env", ".map", ".git", ".gitignore",
        ".env.local", ".env.development"};

    auto assets = ListAssets();
    for (const auto& asset : assets) {
        std::string filename = asset.filename().string();
        for (const auto& ext : forbidden_extensions) {
            if (filename.size() >= ext.size() &&
                filename.substr(filename.size() - ext.size()) == ext) {
                std::cerr << "Source file detected in assets: " << asset
                          << std::endl;
                return false;
            }
        }
    }

    return true;
}

}  // namespace graphx::dsp::dashboard
