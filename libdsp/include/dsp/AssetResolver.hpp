#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <optional>

namespace graphx::dsp::dashboard {

/**
 * @brief Safe asset path resolver with traversal prevention.
 *
 * Prevents path traversal attacks by:
 * - Resolving paths to absolute canonical form
 * - Verifying descendant of asset root
 * - Rejecting symlinks
 * - Rejecting /../ sequences and .. components
 * - Only serving regular files
 *
 * Test vectors that MUST be rejected:
 * - /assets/../../../etc/passwd → 404
 * - /assets/../../config.yaml → 404
 * - /assets/link-to-etc-passwd (symlink) → 404
 */
class AssetResolver {
public:
    /**
     * @brief Configuration options.
     */
    struct Options {
        /// Filesystem root directory for assets
        std::filesystem::path asset_root;
        
        /// Whether to follow symlinks (default: false for security)
        bool allow_symlinks = false;
        
        /// Max path length to prevent DoS (default: 4096)
        size_t max_path_length = 4096;
    };

    /**
     * @brief Construct asset resolver with options.
     *
     * @param options Resolver configuration
     * @throws std::runtime_error if asset_root doesn't exist or isn't a directory
     */
    explicit AssetResolver(const Options& options);

    /**
     * @brief Resolve a request path to a safe filesystem path.
     *
     * @param request_path URL path (e.g., "/assets/index.html" or "index.html")
     * @return Absolute filesystem path if valid, empty optional if invalid/unsafe
     *
     * Returns empty optional if:
     * - Path contains /../ or .. components
     * - Path resolves outside asset_root
     * - Path is a symlink (when allow_symlinks=false)
     * - Path is not a regular file
     * - Path exceeds max_path_length
     */
    std::optional<std::filesystem::path> ResolveSafePath(std::string_view request_path);

    /**
     * @brief Check if a file is safe to serve.
     *
     * @param path Absolute filesystem path
     * @return true if file can be safely served
     */
    bool IsSafeFile(const std::filesystem::path& path);

    /**
     * @brief Get the canonical asset root path.
     *
     * @return Absolute path to asset root
     */
    const std::filesystem::path& GetAssetRoot() const;

    /**
     * @brief List all assets in the root directory.
     *
     * @return Vector of relative paths to all regular files
     */
    std::vector<std::filesystem::path> ListAssets();

    /**
     * @brief Verify that no source files are exposed.
     *
     * Checks for and rejects common source file extensions:
     * - .ts, .tsx, .js, .jsx (TypeScript/JavaScript)
     * - .env, .env.* (environment files)
     * - .git, .gitignore (git metadata)
     * - .map (source maps)
     *
     * @return true if no source files found, false if any detected
     */
    bool VerifyNoSourceFiles();

private:
    Options options_;
    std::filesystem::path canonical_root_;

    /// Normalize path by removing . and .. components safely
    std::string NormalizePath(std::string_view path);

    /// Check if path contains forbidden sequences
    bool ContainsForbiddenSequences(std::string_view path);
};

}  // namespace graphx::dsp::dashboard
