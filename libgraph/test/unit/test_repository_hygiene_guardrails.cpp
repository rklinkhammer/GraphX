// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path SourceRoot() {
    return fs::path(GRAPHX_SOURCE_ROOT);
}

bool ShouldSkipDirectory(const fs::path& directory) {
    const std::string name = directory.filename().string();
    return name == ".git" || name == "build" || name == "build-ninja" ||
           name == "cmake-build-debug" || name == "cmake-build-release" ||
           name == "out";
}

bool LooksLikeEditorArtifact(const fs::path& file_path) {
    const std::string name = file_path.filename().string();
    const std::string extension = file_path.extension().string();

    if (name == ".DS_Store") {
        return true;
    }
    if (extension == ".swp" || extension == ".swo" || extension == ".tmp" ||
        extension == ".orig" || extension == ".rej") {
        return true;
    }
    if (!name.empty() && name.back() == '~') {
        return true;
    }
    return false;
}

std::vector<fs::path> FindEditorArtifacts(const fs::path& root) {
    std::vector<fs::path> artifacts;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, ec);
    fs::recursive_directory_iterator end;

    while (it != end) {
        if (ec) {
            break;
        }

        const fs::path current = it->path();

        if (it->is_directory(ec)) {
            if (ShouldSkipDirectory(current)) {
                it.disable_recursion_pending();
            }
            ++it;
            continue;
        }

        if (it->is_regular_file(ec) && LooksLikeEditorArtifact(current)) {
            artifacts.push_back(fs::relative(current, root));
        }

        ++it;
    }

    std::sort(artifacts.begin(), artifacts.end());
    return artifacts;
}

}  // namespace

TEST(RepositoryHygieneGuardrailTest, SourceTreeContainsNoEditorArtifacts) {
    const fs::path root = SourceRoot();
    ASSERT_TRUE(fs::exists(root));

    const auto artifacts = FindEditorArtifacts(root);

    std::ostringstream artifact_list;
    for (const auto& artifact : artifacts) {
        artifact_list << "\n- " << artifact.string();
    }

    ASSERT_TRUE(artifacts.empty())
        << "Editor artifacts found in source tree. Remove these files:"
        << artifact_list.str();
}
