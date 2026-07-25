#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>

#include "dsp/AssetResolver.hpp"

using namespace graphx::dsp::dashboard;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

// ============================================================================
// Category 2: Static Asset Containment - Path Traversal Tests
// ============================================================================

class TemporaryAssetDirectory {
public:
    TemporaryAssetDirectory() {
        // Create a temporary directory for testing
        char tmp_template[] = "/tmp/graphx-assets-test-XXXXXX";
        char* tmp_dir = mkdtemp(tmp_template);
        if (tmp_dir) {
            temp_path_ = tmp_dir;
            // Create some test files
            CreateTestFiles();
        }
    }

    ~TemporaryAssetDirectory() {
        if (!temp_path_.empty() && fs::exists(temp_path_)) {
            fs::remove_all(temp_path_);
        }
    }

    const std::string& Path() const { return temp_path_; }

private:
    std::string temp_path_;

    void CreateTestFiles() {
        // Create index.html
        std::ofstream(fs::path(temp_path_) / "index.html") << "<html></html>";
        
        // Create js subdirectory and file
        fs::create_directories(fs::path(temp_path_) / "js");
        std::ofstream(fs::path(temp_path_) / "js" / "app.js") << "console.log('test');";
        
        // Create css subdirectory and file
        fs::create_directories(fs::path(temp_path_) / "css");
        std::ofstream(fs::path(temp_path_) / "css" / "style.css") << "body {}";
    }
};

TEST_CASE("AssetResolver: Normal File Access") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Normal access should succeed
    auto path = resolver.ResolveSafePath("/index.html");
    REQUIRE(path.has_value());
    REQUIRE(fs::exists(path.value()));
}

TEST_CASE("AssetResolver: Subdirectory File Access") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Subdirectory access should succeed
    auto path = resolver.ResolveSafePath("/js/app.js");
    REQUIRE(path.has_value());
    REQUIRE(fs::exists(path.value()));
}

TEST_CASE("AssetResolver: Path Traversal Rejection") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Path traversal attempts should be rejected
    REQUIRE_FALSE(resolver.ResolveSafePath("/../etc/passwd").has_value());
    REQUIRE_FALSE(resolver.ResolveSafePath("/../../etc/shadow").has_value());
    REQUIRE_FALSE(resolver.ResolveSafePath("/js/../../../etc/passwd").has_value());
}

TEST_CASE("AssetResolver: Double-Dot Component Rejection") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Explicit .. should be rejected
    REQUIRE_FALSE(resolver.ResolveSafePath("/..").has_value());
    REQUIRE_FALSE(resolver.ResolveSafePath("/js/..").has_value());
}

TEST_CASE("AssetResolver: Dot-Slash Normalization") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Paths with ./ should be normalized correctly
    auto path = resolver.ResolveSafePath("/./index.html");
    REQUIRE(path.has_value());
    REQUIRE(fs::exists(path.value()));
}

TEST_CASE("AssetResolver: Double-Slash Handling") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Double slashes should be normalized
    auto path = resolver.ResolveSafePath("//index.html");
    REQUIRE(path.has_value());
    REQUIRE(fs::exists(path.value()));
}

TEST_CASE("AssetResolver: Nonexistent File") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Nonexistent files should return empty
    REQUIRE_FALSE(resolver.ResolveSafePath("/missing.html").has_value());
}

TEST_CASE("AssetResolver: Directory Rejection") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Directories should not be served
    REQUIRE_FALSE(resolver.ResolveSafePath("/js").has_value());
    REQUIRE_FALSE(resolver.ResolveSafePath("/css/").has_value());
}

TEST_CASE("AssetResolver: Symlink Rejection") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    opts.allow_symlinks = false;
    
    AssetResolver resolver(opts);
    
    // Create a symlink to a file outside the asset root
    fs::path asset_path(temp.Path());
    fs::path symlink_path = asset_path / "link_to_etc_passwd";
    
    // Try to create symlink (may fail on some systems, so wrap in try-catch)
    try {
        fs::create_symlink("/etc/passwd", symlink_path);
        
        // Symlink should be rejected when allow_symlinks=false
        REQUIRE_FALSE(resolver.ResolveSafePath("/link_to_etc_passwd").has_value());
        
        fs::remove(symlink_path);
    } catch (const fs::filesystem_error&) {
        // Skip on systems that don't support symlinks
        SKIP("Symlinks not supported on this system");
    }
}

TEST_CASE("AssetResolver: Safe File Check") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Valid file should pass safety check
    fs::path valid_file = fs::path(temp.Path()) / "index.html";
    REQUIRE(resolver.IsSafeFile(valid_file));
}

TEST_CASE("AssetResolver: Asset Inventory") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // List assets should find files
    auto assets = resolver.ListAssets();
    REQUIRE(assets.size() >= 3);  // index.html, app.js, style.css
}

TEST_CASE("AssetResolver: Source File Detection") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    
    AssetResolver resolver(opts);
    
    // Create some source files
    std::ofstream(fs::path(temp.Path()) / "app.tsx") << "export default {}";
    std::ofstream(fs::path(temp.Path()) / ".env") << "SECRET=key";
    
    // Verify that source files are detected
    REQUIRE_FALSE(resolver.VerifyNoSourceFiles());
}

TEST_CASE("AssetResolver: Max Path Length") {
    TemporaryAssetDirectory temp;
    AssetResolver::Options opts;
    opts.asset_root = temp.Path();
    opts.max_path_length = 50;  // Very short limit for testing
    
    AssetResolver resolver(opts);
    
    // Very long path should be rejected
    std::string long_path(100, 'a');
    REQUIRE_FALSE(resolver.ResolveSafePath(long_path).has_value());
}
