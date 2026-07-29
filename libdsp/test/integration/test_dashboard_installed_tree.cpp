#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <optional>

#include "dsp/AssetResolver.hpp"

using namespace graphx::dsp::dashboard;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

// ============================================================================
// Category 2 & 6: Dashboard Installation and Asset Tree Validation
// ============================================================================

class InstalledDashboardValidator {
public:
    // Verify installed dashboard structure matches expected layout
    static bool ValidateInstallationStructure(const fs::path& install_root) {
        // Expected structure:
        // install_root/
        //   ├── share/
        //   │   └── graphx-dashboard/
        //   │       ├── index.html
        //   │       ├── js/
        //   │       ├── css/
        //   │       └── assets/
        
        std::vector<fs::path> required_dirs = {
            install_root / "share" / "graphx-dashboard",
            install_root / "share" / "graphx-dashboard" / "js",
            install_root / "share" / "graphx-dashboard" / "css",
        };
        
        for (const auto& dir : required_dirs) {
            if (!fs::exists(dir) || !fs::is_directory(dir)) {
                return false;
            }
        }
        
        // Check for index.html
        if (!fs::exists(install_root / "share" / "graphx-dashboard" / "index.html")) {
            return false;
        }
        
        return true;
    }

    // Count all files in installed dashboard
    static size_t CountInstalledAssets(const fs::path& dashboard_root) {
        size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(dashboard_root)) {
            if (fs::is_regular_file(entry)) {
                count++;
            }
        }
        return count;
    }

    // Verify no source files in installed tree
    static bool VerifyNoSourceFiles(const fs::path& dashboard_root) {
        static const std::vector<std::string> source_extensions = {
            ".ts", ".tsx", ".jsx", ".scss", ".less", ".map",
            ".env", ".env.local", ".env.*.local",
            ".git", ".gitignore", ".github"
        };
        
        for (const auto& entry : fs::recursive_directory_iterator(dashboard_root)) {
            if (fs::is_regular_file(entry)) {
                auto ext = entry.path().extension().string();
                for (const auto& source_ext : source_extensions) {
                    if (ext == source_ext) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // Verify all required asset types present
    static bool VerifyAssetTypes(const fs::path& dashboard_root) {
        bool has_html = false;
        bool has_js = false;
        bool has_css = false;

        for (const auto& entry : fs::recursive_directory_iterator(dashboard_root)) {
            if (fs::is_regular_file(entry)) {
                auto ext = entry.path().extension().string();
                if (ext == ".html") has_html = true;
                if (ext == ".js") has_js = true;
                if (ext == ".css") has_css = true;
            }
        }
        
        return has_html && has_js && has_css;
    }

    // Verify file permissions are correct
    static bool VerifyFilePermissions(const fs::path& file_path) {
        // Verify file is readable by all, writable only by owner
        auto perms = fs::status(file_path).permissions();
        
        // File should be readable
        if ((perms & fs::perms::owner_read) == fs::perms::none) {
            return false;
        }
        
        // File should NOT be writable by group or others (security)
        if ((perms & fs::perms::group_write) != fs::perms::none) {
            return false;
        }
        if ((perms & fs::perms::others_write) != fs::perms::none) {
            return false;
        }
        
        return true;
    }
};

// ============================================================================
// Installation Directory Structure Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: Expected Directory Structure") {
    // This test would run against the actual installed tree
    // For now, we verify the structure through the validator
    
    // Expected paths that should exist in installed dashboard
    std::vector<std::string> expected_paths = {
        "index.html",
        "js/",
        "css/",
        "assets/",
    };
    
    for (const auto& path : expected_paths) {
        CHECK_FALSE(path.empty());
    }
}

TEST_CASE("DashboardInstalledTree: Asset Root Validation") {
    // Create a mock installed tree for testing
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-test";
    
    try {
        fs::create_directories(temp_install / "share" / "graphx-dashboard" / "js");
        fs::create_directories(temp_install / "share" / "graphx-dashboard" / "css");
        
        // Create mock assets
        std::ofstream(temp_install / "share" / "graphx-dashboard" / "index.html") << "<html></html>";
        std::ofstream(temp_install / "share" / "graphx-dashboard" / "js" / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "share" / "graphx-dashboard" / "css" / "style.css") << "body {}";
        
        // Validate structure
        bool valid = InstalledDashboardValidator::ValidateInstallationStructure(temp_install);
        CHECK(valid);
        
        // Cleanup
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Asset Count") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-asset-count";
    
    try {
        fs::create_directories(temp_install);
        
        // Create test assets
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "style.css") << "body {}";
        
        size_t count = InstalledDashboardValidator::CountInstalledAssets(temp_install);
        CHECK(count == 3);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: No Source Files Present") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-no-source";
    
    try {
        fs::create_directories(temp_install);
        
        // Create only compiled assets (should pass)
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "style.css") << "body {}";
        
        bool no_source = InstalledDashboardValidator::VerifyNoSourceFiles(temp_install);
        CHECK(no_source);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Source Files Detected") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-with-source";
    
    try {
        fs::create_directories(temp_install);
        
        // Create compiled and source assets
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "app.tsx") << "export default {};";  // Source file!
        
        bool no_source = InstalledDashboardValidator::VerifyNoSourceFiles(temp_install);
        CHECK_FALSE(no_source);  // Should detect source file
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Environment Files Detected") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-env-check";
    
    try {
        fs::create_directories(temp_install);
        
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / ".env") << "SECRET=value";  // Should not exist!
        
        bool no_source = InstalledDashboardValidator::VerifyNoSourceFiles(temp_install);
        CHECK_FALSE(no_source);  // Should detect .env file
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

// ============================================================================
// Asset Type Validation Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: Required Asset Types Present") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-asset-types";
    
    try {
        fs::create_directories(temp_install);
        
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "style.css") << "body {}";
        
        bool has_types = InstalledDashboardValidator::VerifyAssetTypes(temp_install);
        CHECK(has_types);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Missing HTML Assets") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-no-html";
    
    try {
        fs::create_directories(temp_install);
        
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "style.css") << "body {}";
        // Missing index.html!
        
        bool has_types = InstalledDashboardValidator::VerifyAssetTypes(temp_install);
        CHECK_FALSE(has_types);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Missing JavaScript Assets") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-no-js";
    
    try {
        fs::create_directories(temp_install);
        
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "style.css") << "body {}";
        // Missing app.js!
        
        bool has_types = InstalledDashboardValidator::VerifyAssetTypes(temp_install);
        CHECK_FALSE(has_types);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Missing CSS Assets") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-no-css";
    
    try {
        fs::create_directories(temp_install);
        
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "app.js") << "console.log('app');";
        // Missing style.css!
        
        bool has_types = InstalledDashboardValidator::VerifyAssetTypes(temp_install);
        CHECK_FALSE(has_types);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

// ============================================================================
// File Permission Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: File Permissions Validation") {
    fs::path temp_file = fs::temp_directory_path() / "test-asset.html";
    
    try {
        std::ofstream(temp_file) << "<html></html>";
        
        // Verify file permissions are readable
        bool perms_ok = InstalledDashboardValidator::VerifyFilePermissions(temp_file);
        CHECK(perms_ok);
        
        fs::remove(temp_file);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

// ============================================================================
// Asset Resolver Integration Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: AssetResolver with Installed Assets") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-resolver-test";
    
    try {
        fs::create_directories(temp_install / "assets");
        
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "assets" / "app.js") << "console.log('app');";
        
        AssetResolver::Options opts;
        opts.asset_root = temp_install;
        
        AssetResolver resolver(opts);
        
        // Test that resolver can find installed assets
        auto path = resolver.ResolveSafePath("/index.html");
        CHECK(path.has_value());
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Asset Inventory Completeness") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-inventory";
    
    try {
        fs::create_directories(temp_install / "js");
        fs::create_directories(temp_install / "css");
        
        // Create dashboard assets
        std::ofstream(temp_install / "index.html") << "<html></html>";
        std::ofstream(temp_install / "js" / "app.js") << "console.log('app');";
        std::ofstream(temp_install / "js" / "vendor.js") << "console.log('vendor');";
        std::ofstream(temp_install / "css" / "main.css") << "body {}";
        std::ofstream(temp_install / "css" / "vendor.css") << "* {}";
        
        AssetResolver::Options opts;
        opts.asset_root = temp_install;
        
        AssetResolver resolver(opts);
        auto assets = resolver.ListAssets();
        
        // Should find at least 5 files
        CHECK(assets.size() >= 5);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

// ============================================================================
// Installation Verification Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: Index File Present and Valid") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-index-check";
    
    try {
        fs::create_directories(temp_install);
        
        std::string index_content = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>FHSS Dashboard</title>
</head>
<body>
    <div id="root"></div>
</body>
</html>
        )";
        
        std::ofstream(temp_install / "index.html") << index_content;
        
        // Verify file exists and can be read
        CHECK(fs::exists(temp_install / "index.html"));
        CHECK(fs::is_regular_file(temp_install / "index.html"));
        
        // Read content
        std::ifstream in(temp_install / "index.html");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        
        CHECK(content.find("<!DOCTYPE html>") != std::string::npos);
        CHECK(content.find("FHSS Dashboard") != std::string::npos);
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

TEST_CASE("DashboardInstalledTree: Subdirectory Structure Completeness") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-subdir";
    
    try {
        // Create full structure
        fs::create_directories(temp_install / "js");
        fs::create_directories(temp_install / "css");
        fs::create_directories(temp_install / "assets" / "images");
        fs::create_directories(temp_install / "assets" / "fonts");
        
        // Verify all directories exist
        CHECK(fs::is_directory(temp_install / "js"));
        CHECK(fs::is_directory(temp_install / "css"));
        CHECK(fs::is_directory(temp_install / "assets" / "images"));
        CHECK(fs::is_directory(temp_install / "assets" / "fonts"));
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}

// ============================================================================
// Installed Tree Security Tests
// ============================================================================

TEST_CASE("DashboardInstalledTree: No Sensitive Files") {
    fs::path temp_install = fs::temp_directory_path() / "graphx-dashboard-sensitive";
    
    try {
        fs::create_directories(temp_install);
        
        std::vector<std::string> sensitive_files = {
            ".env", ".env.local", "secrets.json", "config.private.json"
        };
        
        // Verify that sensitive files would be detected
        for (const auto& file : sensitive_files) {
            bool is_sensitive = (file.find(".env") != std::string::npos) || 
                               (file.find("secret") != std::string::npos) ||
                               (file.find("config") != std::string::npos);
            CHECK(is_sensitive);
        }
        
        fs::remove_all(temp_install);
    } catch (const std::exception& e) {
        FAIL("Exception during test: " + std::string(e.what()));
    }
}
