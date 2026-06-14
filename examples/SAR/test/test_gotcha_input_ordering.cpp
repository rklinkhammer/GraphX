#include <gtest/gtest.h>

#include "sar/io/GotchaInputOrdering.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class GotchaInputOrderingTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_input_ordering_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    void Touch(const std::string& relative) const {
        const auto path = Path(relative);
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream stream{path};
        ASSERT_TRUE(stream) << path;
        stream << "opaque test input\n";
    }

    void WriteManifest(const std::string& relative, const nlohmann::json& value) const {
        const auto path = Path(relative);
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream stream{path};
        ASSERT_TRUE(stream) << path;
        stream << value.dump(2);
    }

    [[nodiscard]] std::vector<std::string> Filenames(
        const std::vector<std::filesystem::path>& paths) const {
        std::vector<std::string> filenames{};
        for (const auto& path : paths) {
            filenames.push_back(path.filename().string());
        }
        return filenames;
    }

    std::filesystem::path root_{};
};

std::vector<std::string> ErrorCodes(const graphx::sar::GotchaInputOrderingResult& result) {
    std::vector<std::string> codes{};
    for (const auto& error : result.errors) {
        codes.push_back(error.code);
    }
    return codes;
}

} // namespace

TEST_F(GotchaInputOrderingTest, LexicalOrderingSortsOpaqueMatFilesByFilename) {
    Touch("zeta.mat");
    Touch("alpha.mat");
    Touch("middle.mat");
    Touch("ignored.txt");

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverLexical(root_);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(Filenames(result.files), std::vector<std::string>({
                                           "alpha.mat",
                                           "middle.mat",
                                           "zeta.mat",
                                       }));
}

TEST_F(GotchaInputOrderingTest, LexicalOrderingPreservesContiguousGotchaApertureSequence) {
    Touch("subData04.mat");
    Touch("subData02.mat");
    Touch("subData01.mat");
    Touch("subData03.mat");

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverLexical(root_);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(Filenames(result.files), std::vector<std::string>({
                                           "subData01.mat",
                                           "subData02.mat",
                                           "subData03.mat",
                                           "subData04.mat",
                                       }));
}

TEST_F(GotchaInputOrderingTest, ManifestOrderingUsesManifestOrder) {
    Touch("first_on_disk.mat");
    Touch("second_on_disk.mat");
    Touch("third_on_disk.mat");
    WriteManifest("gotcha_input_manifest.json", nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "third_on_disk.mat"}},
                      nlohmann::json{{"path", "first_on_disk.mat"}},
                      nlohmann::json{{"path", "second_on_disk.mat"}},
                  })},
    });

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("gotcha_input_manifest.json"));

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(Filenames(result.files), std::vector<std::string>({
                                           "third_on_disk.mat",
                                           "first_on_disk.mat",
                                           "second_on_disk.mat",
                                       }));
}

TEST_F(GotchaInputOrderingTest, LexicalOrderingReportsGapInGotchaApertureSequence) {
    Touch("subData01.mat");
    Touch("subData03.mat");

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverLexical(root_);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"aperture_sequence_gap"});
}

TEST_F(GotchaInputOrderingTest, ManifestOrderingReportsOutOfOrderGotchaApertureSequence) {
    Touch("subData01.mat");
    Touch("subData02.mat");
    Touch("subData03.mat");
    WriteManifest("gotcha_input_manifest.json", nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "subData02.mat"}},
                      nlohmann::json{{"path", "subData01.mat"}},
                      nlohmann::json{{"path", "subData03.mat"}},
                  })},
    });

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("gotcha_input_manifest.json"));

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"aperture_sequence_out_of_order"});
}

TEST_F(GotchaInputOrderingTest, ManifestOrderingReportsDuplicateGotchaApertureSequence) {
    Touch("a/subData01.mat");
    Touch("b/subData01.mat");
    WriteManifest("gotcha_input_manifest.json", nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "a/subData01.mat"}},
                      nlohmann::json{{"path", "b/subData01.mat"}},
                  })},
    });

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("gotcha_input_manifest.json"));

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"duplicate_aperture_sequence"});
}

TEST_F(GotchaInputOrderingTest, MissingManifestFileReportsDeterministicError) {
    Touch("pulse.mat");

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("missing_manifest.json"));

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"manifest_not_found"});
}

TEST_F(GotchaInputOrderingTest, DuplicateManifestEntryReportsDeterministicError) {
    Touch("pulse_a.mat");
    WriteManifest("gotcha_input_manifest.json", nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "pulse_a.mat"}},
                      nlohmann::json{{"path", "pulse_a.mat"}},
                  })},
    });

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("gotcha_input_manifest.json"));

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"duplicate_manifest_entry"});
}

TEST_F(GotchaInputOrderingTest, MissingManifestEntryReportsDeterministicError) {
    Touch("present.mat");
    WriteManifest("gotcha_input_manifest.json", nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "present.mat"}},
                      nlohmann::json{{"path", "absent.mat"}},
                  })},
    });

    const auto result = graphx::sar::GotchaInputOrdering::DiscoverManifest(
        root_, Path("gotcha_input_manifest.json"));

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"manifest_entry_not_found"});
}

TEST_F(GotchaInputOrderingTest, EmptyInputDirectoryReportsDeterministicError) {
    const auto result = graphx::sar::GotchaInputOrdering::DiscoverLexical(root_);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.files.empty());
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"empty_input_directory"});
}

TEST_F(GotchaInputOrderingTest, ManifestSchemaDocumentNamesOrderingOnlyContract) {
    const auto schema_doc = std::filesystem::path{SAR_GOTCHA_INPUT_MANIFEST_SCHEMA_PATH};
    std::ifstream stream{schema_doc};
    ASSERT_TRUE(stream) << schema_doc;
    const std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};

    EXPECT_NE(text.find(graphx::sar::GotchaInputOrdering::kSchemaName), std::string::npos);
    EXPECT_NE(text.find("Manifest order is authoritative"), std::string::npos);
    EXPECT_NE(text.find("does not use MATLAB"), std::string::npos);
    EXPECT_NE(text.find("does not parse MAT contents"), std::string::npos);
}
