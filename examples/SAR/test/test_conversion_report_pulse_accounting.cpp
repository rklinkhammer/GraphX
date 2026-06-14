#include <gtest/gtest.h>

#include "sar/io/GotchaMatReader.hpp"
#include "sar/io/GraphxSarNormalizedIO.hpp"
#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarIoUtilities.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

// ──────────────────────────────────────────────────────────────────────
// Helper: build a minimal NormalizedSarProduct with per-pulse provenance
// ──────────────────────────────────────────────────────────────────────
static graphx::sar::NormalizedSarProduct MakeProductWithProvenance(
    const std::vector<std::string>& source_files,
    const std::vector<std::size_t>& pulses_per_file) {
    graphx::sar::NormalizedSarProduct product{};
    product.collection.product_id = "test_product";
    product.collection.collection_id = "test_collection";
    product.collection.coordinate_frame = "gotcha_local_cartesian";
    product.collection.time_basis = "seconds";
    product.collection.source_files = source_files;
    product.collection.source_ordering = "lexical";
    product.collection.provenance_label = "derived_from_gotcha_phase_history";

    graphx::sar::ChannelSignal channel{};
    channel.channel_id = "ch0";
    channel.waveform.waveform_id = "wf0";
    channel.waveform.carrier_hz = 9.593e9;
    channel.waveform.bandwidth_hz = 8.0e6;
    channel.waveform.sample_rate_hz = 1.0e6;
    channel.waveform.frequency_axis_hz = {9.59e9, 9.592e9, 9.594e9, 9.596e9};

    std::uint64_t global_idx = 0;
    for (std::size_t file_idx = 0; file_idx < pulses_per_file.size(); ++file_idx) {
        for (std::size_t pi = 0; pi < pulses_per_file[file_idx]; ++pi) {
            graphx::sar::PulseVector pulse{};
            pulse.parameters.vector_index = global_idx;
            pulse.parameters.source_file_index = static_cast<std::uint64_t>(file_idx);
            pulse.parameters.source_pulse_index = static_cast<std::uint64_t>(pi);
            pulse.parameters.time_seconds = static_cast<double>(global_idx);
            pulse.parameters.platform.position_m = {static_cast<double>(file_idx) * 10.0, 0.0, 100.0};
            pulse.samples.push_back({1.0f, -1.0f});
            channel.pulses.push_back(std::move(pulse));
            ++global_idx;
        }
    }
    product.channels.push_back(std::move(channel));
    return product;
}

// ──────────────────────────────────────────────────────────────────────
// Helper: materialize a fixture into a temp directory
// ──────────────────────────────────────────────────────────────────────
class ConversionReportPulseAccountingTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_pr7_report_test_" + std::to_string(ticks));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code err{};
        std::filesystem::remove_all(root_, err);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& rel) const {
        return root_ / rel;
    }

    [[nodiscard]] static nlohmann::json ReadJson(const std::filesystem::path& path) {
        std::ifstream stream{path};
        EXPECT_TRUE(stream.good()) << path;
        nlohmann::json value{};
        stream >> value;
        return value;
    }

    std::filesystem::path root_{};
};

// ──────────────────────────────────────────────────────────────────────
// Test 1: BuildConversionReportJson emits aperture_accounting section
// ──────────────────────────────────────────────────────────────────────
TEST(ConversionReportSchemaTest, EmitsApertureAccountingSection) {
    const auto json = graphx::sar::SarIoUtilities::BuildConversionReportJson(
        graphx::sar::ConversionReportBuildInput{
            .format = "graphx-sar-normalized",
            .label = "NON-STANDARD",
            .selected_mode = "graphx-sar-normalized",
            .validation_status = "ok",
            .provenance = "derived_from_gotcha_phase_history",
            .source_ordering = "lexical",
            .total_files_read = 2,
            .total_pulses_read = 20,
            .pulses_per_file = {
                graphx::sar::SarPulseFileCount{.filename = "subData01.mat", .pulse_count = 10},
                graphx::sar::SarPulseFileCount{.filename = "subData02.mat", .pulse_count = 10},
            },
            .aperture_mode = "full_aperture",
        });

    ASSERT_TRUE(json.contains("aperture_accounting"));
    const auto& acct = json.at("aperture_accounting");
    EXPECT_EQ(acct.at("total_files_read"), 2);
    EXPECT_EQ(acct.at("total_pulses_read"), 20);
    EXPECT_EQ(acct.at("aperture_mode"), "full_aperture");
    ASSERT_EQ(acct.at("pulses_per_file").size(), 2u);
    EXPECT_EQ(acct.at("pulses_per_file").at(0).at("filename"), "subData01.mat");
    EXPECT_EQ(acct.at("pulses_per_file").at(0).at("pulse_count"), 10);
    EXPECT_EQ(acct.at("pulses_per_file").at(1).at("filename"), "subData02.mat");
    EXPECT_EQ(acct.at("pulses_per_file").at(1).at("pulse_count"), 10);
    // pulse_selection_method absent when aperture_mode is full_aperture
    EXPECT_FALSE(acct.contains("pulse_selection_method"));
}

// ──────────────────────────────────────────────────────────────────────
// Test 2: subset mode emits pulse_selection_method
// ──────────────────────────────────────────────────────────────────────
TEST(ConversionReportSchemaTest, SubsetModeEmitsPulseSelectionMethod) {
    const auto json = graphx::sar::SarIoUtilities::BuildConversionReportJson(
        graphx::sar::ConversionReportBuildInput{
            .format = "graphx-sar-normalized",
            .label = "NON-STANDARD",
            .selected_mode = "graphx-sar-normalized",
            .validation_status = "ok",
            .provenance = "derived_from_gotcha_phase_history",
            .source_ordering = "lexical",
            .total_files_read = 1,
            .total_pulses_read = 1,
            .pulses_per_file = {
                graphx::sar::SarPulseFileCount{.filename = "subData01.mat", .pulse_count = 1},
            },
            .aperture_mode = "subset",
            .pulse_selection_method = "single_index",
        });

    const auto& acct = json.at("aperture_accounting");
    EXPECT_EQ(acct.at("aperture_mode"), "subset");
    EXPECT_TRUE(acct.contains("pulse_selection_method"));
    EXPECT_EQ(acct.at("pulse_selection_method"), "single_index");
}

// ──────────────────────────────────────────────────────────────────────
// Test 3: ComputePulsesPerFile — 2-file product with provenance
// ──────────────────────────────────────────────────────────────────────
TEST(ComputePulsesPerFileTest, TwoFileProductReturnsCorrectCounts) {
    const auto product = MakeProductWithProvenance(
        {"subData01.mat", "subData02.mat"},
        {10, 10});

    const auto per_file = graphx::sar::SarIoUtilities::ComputePulsesPerFile(product);
    ASSERT_EQ(per_file.size(), 2u);
    EXPECT_EQ(per_file[0].filename, "subData01.mat");
    EXPECT_EQ(per_file[0].pulse_count, 10u);
    EXPECT_EQ(per_file[1].filename, "subData02.mat");
    EXPECT_EQ(per_file[1].pulse_count, 10u);
}

// ──────────────────────────────────────────────────────────────────────
// Test 4: ComputePulsesPerFile — 10-file product with provenance
// ──────────────────────────────────────────────────────────────────────
TEST(ComputePulsesPerFileTest, TenFileProductReturnsCorrectCounts) {
    const std::vector<std::string> source_files = {
        "subData01.mat", "subData02.mat", "subData03.mat", "subData04.mat", "subData05.mat",
        "subData06.mat", "subData07.mat", "subData08.mat", "subData09.mat", "subData10.mat"};
    const std::vector<std::size_t> counts_per_file(10, 5u);
    const auto product = MakeProductWithProvenance(source_files, counts_per_file);

    const auto per_file = graphx::sar::SarIoUtilities::ComputePulsesPerFile(product);
    ASSERT_EQ(per_file.size(), 10u);
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(per_file[i].pulse_count, 5u) << "file index " << i;
    }
    std::size_t total = 0;
    for (const auto& entry : per_file) {
        total += entry.pulse_count;
    }
    EXPECT_EQ(total, 50u);
}

// ──────────────────────────────────────────────────────────────────────
// Test 5: ComputePulsesPerFile — single-file fallback (no provenance)
// ──────────────────────────────────────────────────────────────────────
TEST(ComputePulsesPerFileTest, SingleFileNoProvenanceFallback) {
    graphx::sar::NormalizedSarProduct product{};
    product.collection.source_files = {"subData01.mat"};
    product.collection.coordinate_frame = "gotcha_local_cartesian";
    product.collection.time_basis = "seconds";

    graphx::sar::ChannelSignal channel{};
    channel.channel_id = "ch0";
    channel.waveform.sample_rate_hz = 1.0e6;
    channel.waveform.waveform_id = "wf0";

    for (std::size_t i = 0; i < 7; ++i) {
        graphx::sar::PulseVector pulse{};
        pulse.parameters.vector_index = static_cast<std::uint64_t>(i);
        // source_file_index intentionally NOT set (simulates legacy product)
        pulse.samples.push_back({1.0f, 0.0f});
        channel.pulses.push_back(std::move(pulse));
    }
    product.channels.push_back(std::move(channel));

    const auto per_file = graphx::sar::SarIoUtilities::ComputePulsesPerFile(product);
    ASSERT_EQ(per_file.size(), 1u);
    EXPECT_EQ(per_file[0].filename, "subData01.mat");
    EXPECT_EQ(per_file[0].pulse_count, 7u);
}

// ──────────────────────────────────────────────────────────────────────
// Test 6: Writer emits aperture_accounting in conversion_report.json
// ──────────────────────────────────────────────────────────────────────
TEST_F(ConversionReportPulseAccountingTest, NormalizedWriterEmitsApertureAccountingForTwoFileProduct) {
    const auto product = MakeProductWithProvenance(
        {"subData01.mat", "subData02.mat"},
        {10, 10});

    const auto out_dir = Path("two_file_out");
    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto result = writer.Write(out_dir, product);
    ASSERT_TRUE(result.success) << result.message;

    const auto report = ReadJson(out_dir / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile);
    ASSERT_TRUE(report.contains("aperture_accounting"));
    const auto& acct = report.at("aperture_accounting");
    EXPECT_EQ(acct.at("total_files_read"), 2);
    EXPECT_EQ(acct.at("total_pulses_read"), 20);
    EXPECT_EQ(acct.at("aperture_mode"), "full_aperture");
    ASSERT_EQ(acct.at("pulses_per_file").size(), 2u);
    EXPECT_EQ(acct.at("pulses_per_file").at(0).at("pulse_count"), 10);
    EXPECT_EQ(acct.at("pulses_per_file").at(1).at("pulse_count"), 10);
}

// ──────────────────────────────────────────────────────────────────────
// Test 7: Writer emits correct accounting for 10-file / 50-pulse product
// ──────────────────────────────────────────────────────────────────────
TEST_F(ConversionReportPulseAccountingTest, NormalizedWriterEmitsApertureAccountingForTenFileProduct) {
    const std::vector<std::string> source_files = {
        "subData01.mat", "subData02.mat", "subData03.mat", "subData04.mat", "subData05.mat",
        "subData06.mat", "subData07.mat", "subData08.mat", "subData09.mat", "subData10.mat"};
    const std::vector<std::size_t> counts_per_file(10, 5u);
    const auto product = MakeProductWithProvenance(source_files, counts_per_file);

    const auto out_dir = Path("ten_file_out");
    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto result = writer.Write(out_dir, product);
    ASSERT_TRUE(result.success) << result.message;

    const auto report = ReadJson(out_dir / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile);
    const auto& acct = report.at("aperture_accounting");
    EXPECT_EQ(acct.at("total_files_read"), 10);
    EXPECT_EQ(acct.at("total_pulses_read"), 50);
    EXPECT_EQ(acct.at("aperture_mode"), "full_aperture");
    ASSERT_EQ(acct.at("pulses_per_file").size(), 10u);
    std::size_t total = 0;
    for (const auto& entry : acct.at("pulses_per_file")) {
        total += entry.at("pulse_count").get<std::size_t>();
    }
    EXPECT_EQ(total, 50u);
}

// ──────────────────────────────────────────────────────────────────────
// Test 8: aperture_accounting is deterministic across repeated writes
// ──────────────────────────────────────────────────────────────────────
TEST_F(ConversionReportPulseAccountingTest, ApertureAccountingIsDeterministicAcrossRepeatedWrites) {
    const auto product = MakeProductWithProvenance(
        {"subData01.mat", "subData02.mat", "subData03.mat"},
        {4, 4, 4});

    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto out_a = Path("run_a");
    const auto out_b = Path("run_b");
    ASSERT_TRUE(writer.Write(out_a, product).success);
    ASSERT_TRUE(writer.Write(out_b, product).success);

    const auto report_a = ReadJson(out_a / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile);
    const auto report_b = ReadJson(out_b / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile);
    EXPECT_EQ(report_a, report_b);
    EXPECT_EQ(report_a.at("aperture_accounting"), report_b.at("aperture_accounting"));
}

// ──────────────────────────────────────────────────────────────────────
// Test 9: Integration — full-aperture read of 2-file fixture
//          produces correct report accounting
// ──────────────────────────────────────────────────────────────────────
TEST_F(ConversionReportPulseAccountingTest, FullApertureReadFromFixtureProducesCorrectReportCounts) {
    const auto fixture_spec =
        std::filesystem::path{__FILE__}.parent_path() /
        "fixtures" / "gotcha_full_aperture_synthetic" / "2file_10pulse_each.json";

    std::ifstream spec_stream{fixture_spec};
    ASSERT_TRUE(spec_stream.good()) << fixture_spec;
    nlohmann::json spec{};
    spec_stream >> spec;

    const auto input_dir = Path("input");
    std::filesystem::create_directories(input_dir);

    for (const auto& file_spec : spec.at("files")) {
        const auto mat_path = input_dir / file_spec.at("path").get<std::string>();
        {
            std::ofstream stub{mat_path, std::ios::binary};
            const std::array<unsigned char, 8> sig{
                0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
            stub.write(reinterpret_cast<const char*>(sig.data()),
                       static_cast<std::streamsize>(sig.size()));
            stub << " synthetic";
        }
        const double ax = file_spec.at("AntX").get<double>();
        const double ay = file_spec.at("AntY").get<double>();
        const double az = file_spec.at("AntZ").get<double>();
        nlohmann::json sidecar{
            {"Np", file_spec.at("Np")},
            {"K", file_spec.at("K")},
            {"deltaF", file_spec.at("deltaF")},
            {"minF", file_spec.at("minF")},
            {"AntX", ax}, {"AntY", ay}, {"AntZ", az},
            {"R0", file_spec.at("R0")},
            {"phdata", "ph"},
            {"sample_rate_hz", 1.0e6},
            {"platform_position_m", nlohmann::json::array({ax, ay, az})},
            {"platform_velocity_mps", nlohmann::json::array({0.1, 0.2, 0.3})},
            {"pulse_time_seconds", 0.0},
            {"range_sample_start", 0u},
            {"polarization", "HH"},
            {"iq_samples", file_spec.at("iq_samples")},
            {"source_field_names", nlohmann::json{{"iq_samples", "DATA.IQ"}}},
        };
        const auto sidecar_path = std::filesystem::path{mat_path.string() + ".json"};
        std::ofstream sf{sidecar_path};
        sf << sidecar.dump(2);
    }

    graphx::sar::GotchaMatReader reader{
        graphx::sar::GotchaMatReaderOptions{
            .ordering_mode = graphx::sar::GotchaMatReaderOrderingMode::Lexical,
            .collection_id = "pr7_test",
            .product_id = "pr7_test_product",
        }};
    const auto read = reader.ReadDetailed(input_dir);
    ASSERT_TRUE(read.success) << read.message;

    const auto out_dir = Path("output");
    graphx::sar::GraphxSarNormalizedWriter writer;
    const auto write = writer.Write(out_dir, read.product);
    ASSERT_TRUE(write.success) << write.message;

    const auto report = ReadJson(out_dir / graphx::sar::GraphxSarNormalizedWriter::kConversionReportFile);
    ASSERT_TRUE(report.contains("aperture_accounting"));
    const auto& acct = report.at("aperture_accounting");
    EXPECT_EQ(acct.at("total_files_read"), 2);
    EXPECT_EQ(acct.at("total_pulses_read"), 20);
    EXPECT_EQ(acct.at("aperture_mode"), "full_aperture");
    ASSERT_EQ(acct.at("pulses_per_file").size(), 2u);
    EXPECT_EQ(acct.at("pulses_per_file").at(0).at("pulse_count"), 10);
    EXPECT_EQ(acct.at("pulses_per_file").at(1).at("pulse_count"), 10);
}

} // namespace
