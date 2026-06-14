#include <gtest/gtest.h>

#include "sar/io/GotchaMatReader.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class GotchaFullPulseIngestionTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_full_pulse_ingestion_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    void WriteMatStub(const std::string& relative) const {
        const std::array<unsigned char, 8> signature{
            0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
        std::ofstream stream{Path(relative), std::ios::binary};
        ASSERT_TRUE(stream);
        stream.write(reinterpret_cast<const char*>(signature.data()),
                     static_cast<std::streamsize>(signature.size()));
        stream << "mat stub";
    }

    void WriteSidecar(const std::string& mat_relative, const nlohmann::json& sidecar) const {
        const auto sidecar_path = Path(mat_relative + ".json");
        std::ofstream stream{sidecar_path};
        ASSERT_TRUE(stream);
        stream << sidecar.dump(2) << '\n';
    }

    [[nodiscard]] static nlohmann::json MakeSidecarWithNp(
        std::uint64_t np,
        std::size_t base = 0) {
        nlohmann::json sidecar{
            {"Np", np},
            {"K", 512},
            {"deltaF", 1000000.0},
            {"minF", 9500000000.0},
            {"AntX", 0.0},
            {"AntY", 0.0},
            {"AntZ", 10.0},
            {"R0", 100000.0},
            {"carrier_hz", 9.6e9},
            {"bandwidth_hz", 640.0e6},
            {"sample_rate_hz", 1.0e9},
            {"frequency_axis_hz", nlohmann::json::array({9.599e9, 9.600e9, 9.601e9})},
            {"platform_position_m", nlohmann::json::array({
                                      static_cast<double>(base + 1),
                                      static_cast<double>(base + 2),
                                      static_cast<double>(base + 3)})},
            {"platform_velocity_mps", nlohmann::json::array({1.0, 2.0, 3.0})},
            {"range_sample_start", static_cast<std::uint64_t>(base)},
            {"iq_samples", nlohmann::json::array({
                               nlohmann::json{{"real", static_cast<float>(base + 10)}, {"imag", -1.0f}},
                               nlohmann::json{{"real", static_cast<float>(base + 11)}, {"imag", -2.0f}},
                               nlohmann::json{{"real", static_cast<float>(base + 12)}, {"imag", -3.0f}},
                           })},
            {"source_field_names", nlohmann::json{
                                       {"iq_samples", "DATA.IQ"},
                                       {"platform_position_m", "SC0"},
                                   }},
        };
        return sidecar;
    }

    std::filesystem::path root_{};
};

// Test 1: Single file with 3 pulses produces 3 PulseVectors with sequential indices
TEST_F(GotchaFullPulseIngestionTest, SingleFileWithMultiplePulsesProducesSequentialVectors) {
    WriteMatStub("file_001.mat");
    WriteSidecar("file_001.mat", MakeSidecarWithNp(3, 100));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.product.channels.empty());
    EXPECT_EQ(result.product.channels[0].pulses.size(), 3);
    EXPECT_EQ(result.product.channels[0].pulses[0].parameters.vector_index, 0);
    EXPECT_EQ(result.product.channels[0].pulses[1].parameters.vector_index, 1);
    EXPECT_EQ(result.product.channels[0].pulses[2].parameters.vector_index, 2);
}

// Test 2: Two files with multiple pulses produce combined total with sequential indices
TEST_F(GotchaFullPulseIngestionTest, TwoFilesWithMultiplePulsesProduceTotalCount) {
    WriteMatStub("file_a.mat");
    WriteMatStub("file_b.mat");
    WriteSidecar("file_a.mat", MakeSidecarWithNp(2, 100));
    WriteSidecar("file_b.mat", MakeSidecarWithNp(3, 200));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.product.channels.empty());
    EXPECT_EQ(result.product.channels[0].pulses.size(), 5);  // 2 + 3
    EXPECT_EQ(result.product.channels[0].pulses[0].parameters.vector_index, 0);  // First file, first pulse
    EXPECT_EQ(result.product.channels[0].pulses[1].parameters.vector_index, 1);  // First file, second pulse
    EXPECT_EQ(result.product.channels[0].pulses[2].parameters.vector_index, 2);  // Second file, first pulse
    EXPECT_EQ(result.product.channels[0].pulses[3].parameters.vector_index, 3);  // Second file, second pulse
    EXPECT_EQ(result.product.channels[0].pulses[4].parameters.vector_index, 4);  // Second file, third pulse
}

// Test 3: Backward compatibility: file with no Np field defaults to 1 pulse
TEST_F(GotchaFullPulseIngestionTest, FilesWithoutNpFieldDefaultToSinglePulse) {
    WriteMatStub("single_pulse.mat");
    nlohmann::json sidecar = MakeSidecarWithNp(1, 100);
    sidecar.erase("Np");  // Remove Np to test backward compatibility
    WriteSidecar("single_pulse.mat", sidecar);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.product.channels.empty());
    EXPECT_EQ(result.product.channels[0].pulses.size(), 1);
}

// Test 4: Pulses are in order within each file
TEST_F(GotchaFullPulseIngestionTest, PulseOrderingWithinFileIsPreserved) {
    WriteMatStub("multi_pulse_file.mat");
    WriteSidecar("multi_pulse_file.mat", MakeSidecarWithNp(5, 100));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.product.channels[0].pulses.size(), 5);
    for (std::size_t i = 0; i < result.product.channels[0].pulses.size(); ++i) {
        EXPECT_EQ(result.product.channels[0].pulses[i].parameters.vector_index, i);
    }
}

// Test 5: Three files with varying pulse counts
TEST_F(GotchaFullPulseIngestionTest, MultiFileScenarioWithVaryingPulseCounts) {
    WriteMatStub("file_1.mat");
    WriteMatStub("file_2.mat");
    WriteMatStub("file_3.mat");
    WriteSidecar("file_1.mat", MakeSidecarWithNp(2, 100));
    WriteSidecar("file_2.mat", MakeSidecarWithNp(4, 200));
    WriteSidecar("file_3.mat", MakeSidecarWithNp(3, 300));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.product.channels[0].pulses.size(), 9);  // 2 + 4 + 3
    
    // Verify sequential indexing across all files
    for (std::size_t i = 0; i < result.product.channels[0].pulses.size(); ++i) {
        EXPECT_EQ(result.product.channels[0].pulses[i].parameters.vector_index, i);
    }
}

// Test 6: Total pulse count equals sum of Np across all files
TEST_F(GotchaFullPulseIngestionTest, TotalPulseCountEqualsSum) {
    WriteMatStub("f1.mat");
    WriteMatStub("f2.mat");
    WriteMatStub("f3.mat");
    WriteMatStub("f4.mat");
    WriteMatStub("f5.mat");
    
    WriteSidecar("f1.mat", MakeSidecarWithNp(10, 100));
    WriteSidecar("f2.mat", MakeSidecarWithNp(15, 200));
    WriteSidecar("f3.mat", MakeSidecarWithNp(8, 300));
    WriteSidecar("f4.mat", MakeSidecarWithNp(12, 400));
    WriteSidecar("f5.mat", MakeSidecarWithNp(5, 500));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    const std::uint64_t expected_total = 10 + 15 + 8 + 12 + 5;  // 50
    EXPECT_EQ(result.product.channels[0].pulses.size(), expected_total);
}

// Test 7: Channel metadata is set correctly for multi-pulse files
TEST_F(GotchaFullPulseIngestionTest, ChannelMetadataPreservedWithMultiplePulses) {
    WriteMatStub("metadata_test.mat");
    nlohmann::json sidecar = MakeSidecarWithNp(3, 100);
    sidecar.erase("K");
    sidecar.erase("deltaF");
    sidecar.erase("minF");
    sidecar.erase("AntX");
    sidecar.erase("AntY");
    sidecar.erase("AntZ");
    sidecar.erase("R0");
    sidecar["carrier_hz"] = 9.65e9;
    sidecar["bandwidth_hz"] = 750.0e6;
    sidecar["sample_rate_hz"] = 1.5e9;
    sidecar["polarization"] = "VV";
    WriteSidecar("metadata_test.mat", sidecar);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.product.channels[0].pulses.size(), 3);
    EXPECT_EQ(result.product.channels[0].waveform.carrier_hz, 9.65e9);
    EXPECT_EQ(result.product.channels[0].waveform.bandwidth_hz, 750.0e6);
    EXPECT_EQ(result.product.channels[0].waveform.sample_rate_hz, 1.5e9);
    EXPECT_EQ(result.product.channels[0].waveform.polarization, "VV");
}

// Test 8: Platform position and velocity are accessible per pulse
TEST_F(GotchaFullPulseIngestionTest, PlatformParametersAccessibleForAllPulses) {
    WriteMatStub("platform_test.mat");
    auto sidecar = MakeSidecarWithNp(3, 100);
    sidecar.erase("K");
    sidecar.erase("deltaF");
    sidecar.erase("minF");
    sidecar.erase("AntX");
    sidecar.erase("AntY");
    sidecar.erase("AntZ");
    sidecar.erase("R0");
    WriteSidecar("platform_test.mat", sidecar);

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.product.channels[0].pulses.size(), 3);
    
    for (const auto& pulse : result.product.channels[0].pulses) {
        EXPECT_EQ(pulse.parameters.platform.position_m[0], 101.0);  // base + 1
        EXPECT_EQ(pulse.parameters.platform.position_m[1], 102.0);  // base + 2
        EXPECT_EQ(pulse.parameters.platform.position_m[2], 103.0);  // base + 3
        EXPECT_EQ(pulse.parameters.platform.velocity_mps[0], 1.0);
        EXPECT_EQ(pulse.parameters.platform.velocity_mps[1], 2.0);
        EXPECT_EQ(pulse.parameters.platform.velocity_mps[2], 3.0);
    }
}

// Test 9: Deterministic results with repeated reads
TEST_F(GotchaFullPulseIngestionTest, RepeatedReadsAreIdentical) {
    WriteMatStub("deterministic.mat");
    WriteSidecar("deterministic.mat", MakeSidecarWithNp(5, 100));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result1 = reader.ReadDetailed(root_);
    const auto result2 = reader.ReadDetailed(root_);

    EXPECT_EQ(result1.product.channels[0].pulses.size(), result2.product.channels[0].pulses.size());
    EXPECT_EQ(result1.product.channels[0].pulses.size(), 5);
    
    for (std::size_t i = 0; i < result1.product.channels[0].pulses.size(); ++i) {
        EXPECT_EQ(result1.product.channels[0].pulses[i].parameters.vector_index,
                  result2.product.channels[0].pulses[i].parameters.vector_index);
    }
}

// Test 10: Large Np value produces proportional number of pulses
TEST_F(GotchaFullPulseIngestionTest, LargeNpValueProducesExpectedPulseCount) {
    WriteMatStub("large_np.mat");
    WriteSidecar("large_np.mat", MakeSidecarWithNp(1000, 100));

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "test_collection",
            .product_id = "test_product",
        }};

    const auto result = reader.ReadDetailed(root_);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.product.channels[0].pulses.size(), 1000);
}

}  // namespace
