#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef SAR_PR18_LOCAL_GOTCHA_VALIDATION_SCRIPT_PATH
#define SAR_PR18_LOCAL_GOTCHA_VALIDATION_SCRIPT_PATH "examples/SAR/tools/local_gotcha_validation.sh"
#endif

#ifndef SAR_PR18_LOCAL_GOTCHA_VALIDATION_DOC_PATH
#define SAR_PR18_LOCAL_GOTCHA_VALIDATION_DOC_PATH "examples/SAR/tools/local_gotcha_validation.md"
#endif

namespace {

std::string ShellQuote(const std::filesystem::path& path) {
    std::string raw = path.string();
    std::string quoted{"'"};
    for (const char ch : raw) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

struct CommandResult {
    int exit_code{};
    std::string output{};
};

CommandResult RunCommand(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output{};
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return CommandResult{.exit_code = -1, .output = "popen failed"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    return CommandResult{.exit_code = pclose(pipe), .output = output};
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

} // namespace

TEST(Pr18LocalGotchaValidationTest, RunnerIsExplicitlyGatedAndDocumentsLocalOnlyBoundaries) {
    const auto script_path = std::filesystem::path{SAR_PR18_LOCAL_GOTCHA_VALIDATION_SCRIPT_PATH};
    const auto doc_path = std::filesystem::path{SAR_PR18_LOCAL_GOTCHA_VALIDATION_DOC_PATH};

    ASSERT_TRUE(std::filesystem::exists(script_path));
    ASSERT_TRUE(std::filesystem::exists(doc_path));

    const auto script = ReadText(script_path);
    const auto doc = ReadText(doc_path);

    EXPECT_NE(script.find("GRAPHX_SAR_GOTCHA_DATASET must be set"), std::string::npos);
    EXPECT_NE(script.find("scripts/verify_gotcha_dataset.sh"), std::string::npos);
    EXPECT_NE(script.find("--mode graphx-crsd-lite"), std::string::npos);
    EXPECT_NE(script.find("gotcha_crsd_index.json"), std::string::npos);
    EXPECT_EQ(script.find("curl "), std::string::npos);
    EXPECT_EQ(script.find("wget "), std::string::npos);
    EXPECT_EQ(script.find("git clone"), std::string::npos);
    EXPECT_EQ(script.find("pip install"), std::string::npos);

    EXPECT_NE(doc.find("No dataset download is performed"), std::string::npos);
    EXPECT_NE(doc.find("No GOTCHA data is checked into the repository"), std::string::npos);
    EXPECT_NE(doc.find("CI does not require this workflow"), std::string::npos);
    EXPECT_NE(doc.find("GRAPHX_SAR_GOTCHA_DATASET"), std::string::npos);
    EXPECT_NE(doc.find("graphx-crsd-lite"), std::string::npos);

    const auto no_env = RunCommand(
        "env -u GRAPHX_SAR_GOTCHA_DATASET -u GRAPHX_SAR_GOTCHA_MANIFEST "
        "-u GRAPHX_SAR_GOTCHA_CHECKSUMS bash " +
        ShellQuote(script_path) + " 2>&1");
    EXPECT_NE(no_env.exit_code, 0);
    EXPECT_NE(no_env.output.find("GRAPHX_SAR_GOTCHA_DATASET must be set"), std::string::npos)
        << no_env.output;
}

TEST(Pr18LocalGotchaValidationTest, OptionalSmokeRunsOnlyWhenRealDatasetEnvironmentIsSet) {
    const char* dataset = std::getenv("GRAPHX_SAR_GOTCHA_DATASET");
    if (dataset == nullptr || std::string{dataset}.empty()) {
        GTEST_SKIP() << "GRAPHX_SAR_GOTCHA_DATASET is not set; PR18 real GOTCHA validation is local-only";
    }

    const auto script_path = std::filesystem::path{SAR_PR18_LOCAL_GOTCHA_VALIDATION_SCRIPT_PATH};
    const auto result = RunCommand("bash " + ShellQuote(script_path) + " 2>&1");
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_NE(result.output.find("local_gotcha_validation_ok"), std::string::npos) << result.output;
}
