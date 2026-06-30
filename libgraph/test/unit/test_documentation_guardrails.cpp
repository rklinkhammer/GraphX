// GraphX Documentation Structure Guardrails
//
// These tests verify that:
// 1. Active documentation is discoverable from top-level README
// 2. No active docs reference archived documentation paths
// 3. Historical docs are clearly archived

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

class DocumentationGuardrailTest : public ::testing::Test {
protected:
  fs::path GetSourceRoot() {
    return fs::path(GRAPHX_SOURCE_ROOT);
  }

  std::string ReadFile(const fs::path& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  bool FileExists(const fs::path& path) {
    return fs::exists(path);
  }

  std::vector<std::string> FindFilesWithPattern(
      const fs::path& root,
      const std::string& pattern) {
    std::vector<std::string> results;
    std::regex regex_pattern(pattern);

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".md") {
        std::string content = ReadFile(entry.path());
        if (std::regex_search(content, regex_pattern)) {
          results.push_back(entry.path().relative_path().string());
        }
      }
    }
    return results;
  }
};

// AC1: Active docs are findable from top-level README
TEST_F(DocumentationGuardrailTest, ActiveDocsReferencedInReadme) {
  auto root = GetSourceRoot();
  std::string readme_content = ReadFile(root / "README.md");

  // Verify README mentions active documentation locations
  EXPECT_NE(readme_content.find("plan/BASELINE.md"), std::string::npos)
      << "README should reference plan/BASELINE.md";

  EXPECT_NE(readme_content.find("plan/roadmap/GRAPHX_PR_ROADMAP.md"),
            std::string::npos)
      << "README should reference active PR roadmap";

    EXPECT_NE(readme_content.find("plan/agents/GRAPHX_AGENT_ROLES.md"),
      std::string::npos)
    << "README should reference active agent role guidance";

  EXPECT_NE(readme_content.find("docs/archive"), std::string::npos)
      << "README should reference archived documentation location";
}

// AC2: No active docs claim archived paths are current
TEST_F(DocumentationGuardrailTest,
       ActiveDocsDoNotReferenceArchivedDocSubdirectories) {
  auto root = GetSourceRoot();
  // Verify plan/BASELINE.md doesn't reference old doc/ subdirectories
  std::string baseline_content = ReadFile(root / "plan/BASELINE.md");

  std::vector<std::regex> forbidden_patterns = {
      std::regex(R"(\bdoc/architecture\b)"),
      std::regex(R"(\bdoc/guides\b)"),
      std::regex(R"(\bdoc/phase-reports\b)"),
      std::regex(R"(\bdoc/tests\b)"),
  };

  for (const auto& pattern : forbidden_patterns) {
    EXPECT_FALSE(std::regex_search(baseline_content, pattern))
        << "plan/BASELINE.md should not reference archived doc subdirectories";
  }

  // Verify top-level README.md doesn't reference old doc/ subdirectories as active
  std::string readme_content = ReadFile(root / "README.md");
  for (const auto& pattern : forbidden_patterns) {
    // Allow mentions if they're in archive context
    std::string without_archive = std::regex_replace(
        readme_content, std::regex(R"(.*archive.*)", std::regex::ECMAScript),
        "");
    EXPECT_FALSE(std::regex_search(without_archive, pattern))
        << "README.md should not reference archived doc subdirectories as active";
  }
}

// AC3: Historical docs are clearly archived
TEST_F(DocumentationGuardrailTest, ArchivedDocStructureExists) {
  auto root = GetSourceRoot();
  // Verify archive location exists with moved docs
  EXPECT_TRUE(FileExists(root / "docs/archive/2026-06-baseline/doc/architecture"))
      << "Archived architecture docs should exist";

  EXPECT_TRUE(FileExists(root / "docs/archive/2026-06-baseline/doc/guides"))
      << "Archived guides should exist";

  EXPECT_TRUE(FileExists(root / "docs/archive/2026-06-baseline/doc/phase-reports"))
      << "Archived phase reports should exist";

  EXPECT_TRUE(FileExists(root / "docs/archive/2026-06-baseline/doc/tests"))
      << "Archived test docs should exist";

  // Verify old active doc locations no longer exist
  EXPECT_FALSE(FileExists(root / "doc/architecture"))
      << "Old doc/architecture should be archived (not in root doc/)";

  EXPECT_FALSE(FileExists(root / "doc/guides"))
      << "Old doc/guides should be archived (not in root doc/)";

  EXPECT_FALSE(FileExists(root / "doc/phase-reports"))
      << "Old doc/phase-reports should be archived (not in root doc/)";

  EXPECT_FALSE(FileExists(root / "doc/tests"))
      << "Old doc/tests should be archived (not in root doc/)";
}

// Guardrail: doc/README.md explains archived status
TEST_F(DocumentationGuardrailTest, DocReadmeIndicatesArchived) {
  auto root = GetSourceRoot();
  std::string doc_readme = ReadFile(root / "doc/README.md");

  EXPECT_NE(doc_readme.find("Archived"), std::string::npos)
      << "doc/README.md should indicate archived status";

  EXPECT_NE(doc_readme.find("Active Documentation"), std::string::npos)
      << "doc/README.md should reference active documentation";

  EXPECT_NE(doc_readme.find("docs/archive"), std::string::npos)
      << "doc/README.md should reference archive location";
}

// Guardrail: Active roadmaps and agents don't reference archived docs
TEST_F(DocumentationGuardrailTest, ActivePlanDocsNotReferenceArchive) {
  auto root = GetSourceRoot();
  std::vector<fs::path> active_plan_files = {
      root / "plan/roadmap/GRAPHX_PR_ROADMAP.md",
      root / "plan/agents/GRAPHX_AGENT_ROLES.md",
  };

  // These files should be able to reference archive for context
  // But should not claim archived items are current

  for (const auto& file : active_plan_files) {
    std::string content = ReadFile(file); // file is already a full path

    // Should not claim old doc/ dirs are the canonical architecture
    EXPECT_EQ(content.find("doc/architecture as the canonical"), std::string::npos)
        << file.string() << " should not reference doc/architecture as canonical";

    EXPECT_EQ(content.find("doc/guides as active"), std::string::npos)
        << file.string() << " should not reference doc/guides as active";
  }
}

// Guardrail: Documentation index consistency
TEST_F(DocumentationGuardrailTest, ActiveDocumentationSetIsConsistent) {
  auto root = GetSourceRoot();
  // Count active review and roadmap documents
  std::vector<fs::path> review_files;
  for (const auto& entry : fs::directory_iterator(root / "plan/reviews")) {
    if (entry.is_regular_file() && entry.path().extension() == ".md") {
      review_files.push_back(entry.path().filename());
    }
  }

  // Should have implementation and verification reports for PRs
  std::vector<std::string> expected_prefixes = {
      "GRAPHX_IMPL_", "GRAPHX_VERIFY_",
  };

  // At minimum, should have some reports
  EXPECT_GE(review_files.size(), 2)
      << "Should have multiple review/implementation reports in plan/reviews";
}

TEST_F(DocumentationGuardrailTest,
       ConsolidatedUserGuideHasNoStaleSarDomainReadme) {
  auto root = GetSourceRoot();
  EXPECT_FALSE(FileExists(root / "examples/SAR/README.md"));

  const auto readme = ReadFile(root / "README.md");
  EXPECT_EQ(readme.find("sar_gotcha_external_manual.json"), std::string::npos);
  EXPECT_EQ(readme.find("sar_stripmap_metal_window.json"), std::string::npos);
  EXPECT_NE(readme.find("sar_baseline_substitution_experiment.py"),
            std::string::npos);
}

// Guardrail: No stray references to old architecture docs in active code
TEST_F(DocumentationGuardrailTest, ActiveSourceDoesNotReferenceOldDocs) {
  auto root = GetSourceRoot();
  std::vector<fs::path> active_code_roots = {
      root / "libgraph/src",
      root / "libgraph/include",
      root / "libgpu/src",
      root / "libgpu/include",
  };

  std::vector<std::regex> forbidden_patterns = {
      std::regex(R"(doc/architecture)"),
      std::regex(R"(doc/guides)"),
      std::regex(R"(doc/phase-reports)"),
      std::regex(R"(doc/tests)"),
  };

  for (const auto& root : active_code_roots) {
    if (!fs::exists(root)) continue;

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (entry.is_regular_file()) {
        auto ext = entry.path().extension().string();
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
          std::string content = ReadFile(entry.path());

          for (const auto& pattern : forbidden_patterns) {
            EXPECT_FALSE(std::regex_search(content, pattern))
                << entry.path().filename().string()
                << " should not reference archived doc paths";
          }
        }
      }
    }
  }
}
