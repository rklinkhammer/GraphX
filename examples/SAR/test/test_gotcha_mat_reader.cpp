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

class GotchaMatReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_mat_reader_" + std::to_string(now));
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
        ASSERT_TRUE(stream) << Path(relative);
        stream.write(reinterpret_cast<const char*>(signature.data()),
                     static_cast<std::streamsize>(signature.size()));
        stream << "mat stub";
    }

    void WriteSidecar(const std::string& mat_relative, const nlohmann::json& sidecar) const {
        const auto sidecar_path = Path(mat_relative + ".json");
        std::ofstream stream{sidecar_path};
        ASSERT_TRUE(stream) << sidecar_path;
        stream << sidecar.dump(2) << '\n';
    }

    void WriteManifest(const nlohmann::json& manifest) const {
        std::ofstream stream{Path("ordered_manifest.json")};
        ASSERT_TRUE(stream);
        stream << manifest.dump(2) << '\n';
    }

    [[nodiscard]] static nlohmann::json MakeSidecar(
        std::size_t base,
        std::optional<double> pulse_time = std::nullopt,
        std::optional<std::string> polarization = std::nullopt) {
        nlohmann::json sidecar{
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
                                       {"frequency_axis_hz", "freq"},
                                   }},
        };
        if (pulse_time.has_value()) {
            sidecar["pulse_time_seconds"] = *pulse_time;
        }
        if (polarization.has_value()) {
            sidecar["polarization"] = *polarization;
        }
        return sidecar;
    }

    std::filesystem::path root_{};
};

TEST_F(GotchaMatReaderTest, ReadsLexicalOrderedDirectoryIntoSingleChannelNormalizedProduct) {
    WriteMatStub("b_pulse.mat");
    WriteMatStub("a_pulse.mat");

    WriteSidecar("a_pulse.mat", MakeSidecar(0, 0.25, "HH"));
    WriteSidecar("b_pulse.mat", MakeSidecar(1, std::nullopt, std::nullopt));

    graphx::sar::GotchaMatReader reader;
    const auto result = reader.ReadDetailed(root_);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.message, "ok");

    const auto& product = result.product;
    EXPECT_EQ(product.collection.collection_id, "gotcha_collection");
    EXPECT_EQ(product.collection.provenance_label, "derived_from_gotcha_phase_history");
    EXPECT_EQ(product.collection.source_ordering, "lexical");
    ASSERT_EQ(product.collection.source_files.size(), 2u);
    EXPECT_EQ(std::filesystem::path{product.collection.source_files[0]}.filename(), "a_pulse.mat");
    EXPECT_EQ(std::filesystem::path{product.collection.source_files[1]}.filename(), "b_pulse.mat");

    const auto shape = product.Shape();
    EXPECT_EQ(shape.channel_count, 1u);
    EXPECT_EQ(shape.pulse_count, 2u);
    EXPECT_EQ(shape.max_sample_count, 3u);

    const auto& channel = product.Channel(0);
    EXPECT_EQ(channel.channel_id, "channel_0");
    EXPECT_EQ(channel.waveform.waveform_id, "gotcha_phase_history_channel_0");
    EXPECT_EQ(channel.waveform.sample_type, "complex_f32");
    EXPECT_EQ(channel.waveform.polarization, "HH");
    EXPECT_EQ(channel.waveform.frequency_axis_hz.size(), 3u);

    const auto& first_pulse = product.Pulse(0, 0);
    EXPECT_EQ(first_pulse.parameters.vector_index, 0u);
    EXPECT_DOUBLE_EQ(first_pulse.parameters.time_seconds, 0.25);
    EXPECT_EQ(first_pulse.parameters.platform.position_m[0], 1.0);
    EXPECT_EQ(first_pulse.samples.size(), 3u);
    EXPECT_FLOAT_EQ(first_pulse.samples[0].real, 10.0f);

    const auto& second_pulse = product.Pulse(1, 0);
    EXPECT_EQ(second_pulse.parameters.vector_index, 1u);
    EXPECT_DOUBLE_EQ(second_pulse.parameters.time_seconds, 1.0);
    EXPECT_EQ(second_pulse.parameters.platform.position_m[0], 2.0);

    ASSERT_FALSE(result.diagnostics.original_field_names.empty());
    EXPECT_EQ(result.diagnostics.original_field_names[0].normalized_field, "frequency_axis_hz");
    EXPECT_EQ(result.diagnostics.original_field_names[0].original_field_name, "freq");
}

TEST_F(GotchaMatReaderTest, ManifestOrderingIsPreservedInPulseVectorOrder) {
    WriteMatStub("pulse_00.mat");
    WriteMatStub("pulse_01.mat");
    WriteSidecar("pulse_00.mat", MakeSidecar(0, 0.5, "VV"));
    WriteSidecar("pulse_01.mat", MakeSidecar(5, 0.1, "VV"));

    WriteManifest(nlohmann::json{
        {"schema", graphx::sar::GotchaInputOrdering::kSchemaName},
        {"files", nlohmann::json::array({
                      nlohmann::json{{"path", "pulse_01.mat"}},
                      nlohmann::json{{"path", "pulse_00.mat"}},
                  })},
    });

    graphx::sar::GotchaMatReader reader(graphx::sar::GotchaMatReaderOptions{
        .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Manifest,
        .manifest_path = Path("ordered_manifest.json"),
        .collection_id = "collection-manifest",
        .product_id = "product-manifest",
        .collector_name = "collector-manifest",
        .coordinate_frame = "enu",
        .time_basis = "seconds",
    });

    const auto result = reader.ReadDetailed(root_);

    ASSERT_TRUE(result.success) << result.message;
    const auto& product = result.product;
    EXPECT_EQ(product.collection.collection_id, "collection-manifest");
    EXPECT_EQ(product.collection.product_id, "product-manifest");
    EXPECT_EQ(product.collection.collector_name, "collector-manifest");
    EXPECT_EQ(product.collection.coordinate_frame, "enu");
    EXPECT_EQ(product.collection.source_ordering, "manifest");

    ASSERT_EQ(product.collection.source_files.size(), 2u);
    EXPECT_EQ(std::filesystem::path{product.collection.source_files[0]}.filename(), "pulse_01.mat");
    EXPECT_EQ(std::filesystem::path{product.collection.source_files[1]}.filename(), "pulse_00.mat");

    EXPECT_FLOAT_EQ(product.Pulse(0, 0).samples[0].real, 15.0f);
    EXPECT_FLOAT_EQ(product.Pulse(1, 0).samples[0].real, 10.0f);
}

TEST_F(GotchaMatReaderTest, RawGotchaMetadataMapsToWaveformAndLocalGeometry) {
    WriteMatStub("raw_gotcha.mat");
    auto sidecar = MakeSidecar(0, 0.5, "HH");
    sidecar.erase("carrier_hz");
    sidecar.erase("bandwidth_hz");
    sidecar.erase("sample_rate_hz");
    sidecar.erase("frequency_axis_hz");
    sidecar.erase("platform_position_m");
    sidecar["K"] = 4;
    sidecar["deltaF"] = 2.0e6;
    sidecar["minF"] = 9.590e9;
    sidecar["AntX"] = -10.0;
    sidecar["AntY"] = 20.0;
    sidecar["AntZ"] = 300.0;
    sidecar["R0"] = 765.25;
    WriteSidecar("raw_gotcha.mat", sidecar);

    graphx::sar::GotchaMatReader reader;
    const auto result = reader.ReadDetailed(root_);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.product.collection.coordinate_frame, "gotcha_local_cartesian");
    ASSERT_EQ(result.product.channels.size(), 1u);
    const auto& channel = result.product.Channel(0);
    EXPECT_DOUBLE_EQ(channel.waveform.carrier_hz, 9.593e9);
    EXPECT_DOUBLE_EQ(channel.waveform.bandwidth_hz, 8.0e6);
    EXPECT_DOUBLE_EQ(channel.waveform.sample_rate_hz, 8.0e6);
    ASSERT_EQ(channel.waveform.frequency_axis_hz.size(), 4u);
    EXPECT_DOUBLE_EQ(channel.waveform.frequency_axis_hz[3], 9.596e9);

    const auto& pulse = result.product.Pulse(0, 0);
    EXPECT_DOUBLE_EQ(pulse.parameters.platform.position_m[0], -10.0);
    EXPECT_DOUBLE_EQ(pulse.parameters.platform.position_m[1], 20.0);
    EXPECT_DOUBLE_EQ(pulse.parameters.platform.position_m[2], 300.0);
    ASSERT_TRUE(pulse.parameters.reference_range_m.has_value());
    EXPECT_DOUBLE_EQ(*pulse.parameters.reference_range_m, 765.25);
}

TEST_F(GotchaMatReaderTest, MissingSidecarProducesDeterministicError) {
    WriteMatStub("only.mat");

    graphx::sar::GotchaMatReader reader;
    const auto result = reader.ReadDetailed(root_);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "mat sidecar parsing failed: sidecar_missing");
    ASSERT_FALSE(result.diagnostics.issues.empty());
    EXPECT_EQ(result.diagnostics.issues.front().code, "sidecar_missing");
}

TEST_F(GotchaMatReaderTest, ReaderInterfaceBridgesToSarReadResult) {
    WriteMatStub("single.mat");
    WriteSidecar("single.mat", MakeSidecar(3, 7.0, "HV"));

    const graphx::sar::ISarReader& reader = graphx::sar::GotchaMatReader{};
    const auto result = reader.Read(root_);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.product.Shape().channel_count, 1u);
    EXPECT_EQ(result.product.Shape().pulse_count, 1u);
    EXPECT_EQ(result.product.collection.provenance_label, "derived_from_gotcha_phase_history");
}

} // namespace
