// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_aperture_assembly_adapter_node.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/CrsdApertureAssemblyAdapterNode.hpp"
#include "sar/OrderedCrsdSetInputSourceNode.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_CRSD_TINY_FIXTURE_BASE_DIR
#define SAR_CRSD_TINY_FIXTURE_BASE_DIR "examples/SAR/test/fixtures/crsd_binary_tiny_multisegment"
#endif

std::filesystem::path FixtureBaseDir() {
    return std::filesystem::path{SAR_CRSD_TINY_FIXTURE_BASE_DIR};
}

std::vector<std::string> FixturePaths() {
    return {
        (FixtureBaseDir() / "segment_000" / "product.crsd").string(),
        (FixtureBaseDir() / "segment_001" / "product.crsd").string(),
        (FixtureBaseDir() / "segment_002" / "product.crsd").string(),
    };
}

sar::SarAccelControlToken MakeDataToken(std::uint64_t segment_index) {
    sar::SarAccelControlToken token{};
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.sequence_id = segment_index;
    token.sidecar.pulse_range_start = segment_index * 2u;
    token.sidecar.pulse_range_count = 2u;
    token.sidecar.payload_byte_count = 64u;
    token.has_host_view = false;
    return token;
}

sar::SarAccelControlToken MakeEosToken() {
    sar::SarAccelControlToken token{};
    token.sidecar.marker = sar::SarFrameMarker::EndOfStream;
    token.sidecar.sequence_id = 999u;
    return token;
}

class FakeReader final : public graphx::sar::ICrsdReader {
public:
    explicit FakeReader(graphx::sar::CrsdReadResult result)
        : result_(std::move(result)) {}

    [[nodiscard]] graphx::sar::CrsdReadResult ReadOrderedSet(const graphx::sar::CrsdReadOptions&) const override {
        return result_;
    }

private:
    graphx::sar::CrsdReadResult result_{};
};

std::string AdapterPluginFilename() {
    return std::string("libcrsd_aperture_assembly_adapter_node") + kSharedLibraryExtension;
}

// Helper: build a valid 3-segment CrsdReadResult for injection via FakeReader.
graphx::sar::CrsdReadResult MakeValidThreeSegmentResult(
    std::uint64_t vectors_per_segment = 2u,
    std::uint64_t samples_per_vector = 4u,
    double carrier_hz = 9.6e9,
    double sample_rate_hz = 1.0e9,
    std::uint64_t channel_id = 7u) {
    graphx::sar::CrsdReadResult result{};
    result.success = true;
    std::uint64_t global_start = 0u;
    for (std::uint64_t seg_idx = 0u; seg_idx < 3u; ++seg_idx) {
        graphx::sar::CrsdSegmentRecord seg{};
        seg.segment_index = seg_idx;
        seg.channel_id = channel_id;
        seg.global_vector_start = global_start;
        seg.vector_count = vectors_per_segment;
        seg.samples_per_vector = samples_per_vector;
        seg.carrier_hz = carrier_hz;
        seg.sample_rate_hz = sample_rate_hz;
        for (std::uint64_t v = 0u; v < vectors_per_segment; ++v) {
            graphx::sar::CrsdVectorRecord vec{};
            vec.vector_index = global_start + v;
            vec.channel_id = channel_id;
            vec.rcv_time_s = static_cast<double>(global_start + v) * 1e-6;
            vec.platform_position_m = {1000.0, 2000.0, 3000.0};
            vec.platform_velocity_mps = {100.0, 0.0, 0.0};
            vec.signal.assign(samples_per_vector, std::complex<float>{1.0f, 0.5f});
            seg.vectors.push_back(vec);
        }
        if (!seg.vectors.empty()) {
            seg.first_vector = seg.vectors.front();
            seg.last_vector = seg.vectors.back();
        }
        seg.payload_hash = static_cast<std::uint64_t>(seg_idx + 1u) * 0xDEADBEEFu;
        result.value.segments.push_back(seg);
        global_start += vectors_per_segment;
    }
    result.value.total_vector_count = global_start;
    result.value.ordered_set_payload_hash = 0xCAFEBABEu;
    return result;
}

} // namespace

TEST(CrsdApertureAssemblyAdapterNodeTest, AssemblesFullApertureFrameOnEndOfStream) {
    sar::OrderedCrsdSetInputSourceNode source;
    sar::CrsdApertureAssemblyAdapterNode adapter;

    const nlohmann::json source_cfg{
        {"crsd_paths", FixturePaths()},
        {"stream_id", 4},
        {"backend_id", 0},
        {"backend", 0},
    };
    const nlohmann::json adapter_cfg{{"crsd_paths", FixturePaths()}};

    ASSERT_NO_THROW(source.Configure(graph::JsonView(source_cfg)));
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(adapter_cfg)));

    std::optional<sar::SarPhaseHistoryControlMessage> assembled{};
    while (true) {
        auto token = source.Produce(std::integral_constant<std::size_t, 0>{});
        if (!token.has_value()) {
            break;
        }
        assembled = adapter.Transfer(
            *token,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
    }

    ASSERT_TRUE(assembled.has_value());
    EXPECT_EQ(assembled->control.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(assembled->frame.control_marker, sar::SarPhaseHistoryControlMarker::EndOfStream);
    EXPECT_EQ(assembled->frame.segments.size(), 3u);
    EXPECT_EQ(assembled->frame.total_vector_count, 6u);
    EXPECT_EQ(assembled->frame.samples_per_vector, 4u);
    EXPECT_EQ(assembled->frame.layout.rank, 2u);
    EXPECT_EQ(assembled->frame.layout.shape[0], 6u);
    EXPECT_EQ(assembled->frame.layout.shape[1], 8u);
    EXPECT_EQ(assembled->frame.split_boundary_input_hash, assembled->frame.ordered_set_payload_hash);
    EXPECT_EQ(adapter.GetLastDiagnostic(), "ok:aperture_assembled");
}

TEST(CrsdApertureAssemblyAdapterNodeTest, DetectsOutOfOrderAndMissingSegmentDiagnostics) {
    sar::CrsdApertureAssemblyAdapterNode adapter;
    const nlohmann::json adapter_cfg{{"crsd_paths", FixturePaths()}};
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(adapter_cfg)));

    auto out_of_order = adapter.Transfer(
        MakeDataToken(1u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(out_of_order.has_value());
    EXPECT_EQ(adapter.GetLastDiagnostic(), "out_of_order_segment_index:1");

    adapter.Reset();

    auto seg0 = adapter.Transfer(
        MakeDataToken(0u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(seg0.has_value());

    auto eos = adapter.Transfer(
        MakeEosToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(eos.has_value());
    EXPECT_EQ(adapter.GetLastDiagnostic(), "missing_segment_index:1");
}

TEST(CrsdApertureAssemblyAdapterNodeTest, DetectsDuplicateAndUnexpectedSegments) {
    sar::CrsdApertureAssemblyAdapterNode adapter;
    const nlohmann::json adapter_cfg{{"crsd_paths", FixturePaths()}};
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(adapter_cfg)));

    EXPECT_FALSE(adapter.Transfer(
        MakeDataToken(0u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}).has_value());

    EXPECT_FALSE(adapter.Transfer(
        MakeDataToken(0u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}).has_value());
    EXPECT_EQ(adapter.GetLastDiagnostic(), "duplicate_segment_index:0");

    adapter.Reset();
    EXPECT_FALSE(adapter.Transfer(
        MakeDataToken(42u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}).has_value());
    EXPECT_EQ(adapter.GetLastDiagnostic(), "unexpected_segment_index:42");
}

TEST(CrsdApertureAssemblyAdapterNodeTest, OptionalSidecarPulseRangeCrossCheckIsConfigurable) {
    sar::CrsdApertureAssemblyAdapterNode relaxed;
    const nlohmann::json relaxed_cfg{
        {"crsd_paths", FixturePaths()},
        {"enable_sidecar_pulse_range_cross_check", false},
    };
    ASSERT_NO_THROW(relaxed.Configure(graph::JsonView(relaxed_cfg)));

    auto bad_range = MakeDataToken(0u);
    bad_range.sidecar.pulse_range_start = 999u;
    bad_range.sidecar.pulse_range_count = 999u;

    EXPECT_FALSE(relaxed.Transfer(
        bad_range,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}).has_value());

    sar::CrsdApertureAssemblyAdapterNode strict;
    const nlohmann::json strict_cfg{
        {"crsd_paths", FixturePaths()},
        {"enable_sidecar_pulse_range_cross_check", true},
    };
    ASSERT_NO_THROW(strict.Configure(graph::JsonView(strict_cfg)));

    EXPECT_FALSE(strict.Transfer(
        bad_range,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}).has_value());
    EXPECT_EQ(strict.GetLastDiagnostic(), "pulse_range_mismatch:0");
}

TEST(CrsdApertureAssemblyAdapterNodeTest, EnforcesSampleAndFrequencyConsistencyAtConfigure) {
    graphx::sar::CrsdSegmentRecord segment0{};
    segment0.segment_index = 0u;
    segment0.vector_count = 1u;
    segment0.samples_per_vector = 4u;
    segment0.carrier_hz = 9.6e9;
    segment0.sample_rate_hz = 1.0e9;
    segment0.vectors.push_back(graphx::sar::CrsdVectorRecord{});

    graphx::sar::CrsdSegmentRecord segment1 = segment0;
    segment1.segment_index = 1u;
    segment1.samples_per_vector = 8u;

    graphx::sar::CrsdReadResult result{};
    result.success = true;
    result.value.segments = {segment0, segment1};
    result.value.total_vector_count = 2u;

    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(
        sar::CrsdApertureAssemblyAdapterConfig{},
        fake_reader);

    const nlohmann::json cfg{{"crsd_paths", nlohmann::json::array({"x/product.crsd", "y/product.crsd"})}};
    EXPECT_THROW(adapter.Configure(graph::JsonView(cfg)), graph::ConfigError);
}

TEST(CrsdApertureAssemblyAdapterNodeTest, DynamicPluginLoadAndInstantiationSmoke) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(AdapterPluginFilename()));

    auto created = registry->CreateNodeExpected("CrsdApertureAssemblyAdapterNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::CrsdApertureAssemblyAdapterNode>();
    ASSERT_TRUE(node);
}

// --- PR3b: Metadata/PVP mapping tests (field-by-field) ---

TEST(CrsdApertureAssemblyAdapterNodeTest, MetadataPvpMappingPreservesAllFieldsFieldByField) {
    auto result = MakeValidThreeSegmentResult(/*vectors_per_segment=*/2u, /*samples=*/4u,
        /*carrier=*/9.6e9, /*sample_rate=*/1.0e9, /*channel_id=*/7u);
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        ASSERT_FALSE(adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{}).has_value());
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    const auto& frame = assembled->frame;
    ASSERT_EQ(frame.segments.size(), 3u);
    EXPECT_EQ(frame.samples_per_vector, 4u);
    EXPECT_DOUBLE_EQ(frame.carrier_hz, 9.6e9);
    EXPECT_DOUBLE_EQ(frame.sample_rate_hz, 1.0e9);

    // Check each segment's fields map from input source.
    for (std::uint64_t seg_idx = 0u; seg_idx < 3u; ++seg_idx) {
        const auto& seg = frame.segments[seg_idx];
        EXPECT_EQ(seg.segment_index, seg_idx);
        EXPECT_EQ(seg.channel_id, 7u);
        EXPECT_EQ(seg.vector_count, 2u);
        EXPECT_EQ(seg.samples_per_vector, 4u);
        EXPECT_DOUBLE_EQ(seg.carrier_hz, 9.6e9);
        EXPECT_DOUBLE_EQ(seg.sample_rate_hz, 1.0e9);
        ASSERT_EQ(seg.vectors.size(), 2u);
        for (std::uint64_t v = 0u; v < 2u; ++v) {
            const auto& vec = seg.vectors[v];
            EXPECT_EQ(vec.channel_id, 7u);
            EXPECT_EQ(vec.vector_index, seg_idx * 2u + v);
            EXPECT_TRUE(std::isfinite(vec.rcv_time_s));
            EXPECT_DOUBLE_EQ(vec.platform_position_m[0], 1000.0);
            EXPECT_DOUBLE_EQ(vec.platform_position_m[1], 2000.0);
            EXPECT_DOUBLE_EQ(vec.platform_position_m[2], 3000.0);
            EXPECT_DOUBLE_EQ(vec.platform_velocity_mps[0], 100.0);
            EXPECT_EQ(vec.samples.size(), 4u);
            EXPECT_NE(vec.sample_payload_hash, 0u);
        }
    }
}

// --- PR3b: Ownership/layout/checksum invariant tests ---

TEST(CrsdApertureAssemblyAdapterNodeTest, OwnershipSampleFormatAndLayoutAreExplicitAndValid) {
    auto result = MakeValidThreeSegmentResult();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    const auto& frame = assembled->frame;
    EXPECT_EQ(frame.ownership, sar::SarPhaseHistoryOwnership::OwnedHostBuffer);
    EXPECT_EQ(frame.sample_format, sar::SarPhaseHistorySampleFormat::ComplexFloat32Interleaved);
    EXPECT_EQ(frame.layout.rank, 2u);
    EXPECT_EQ(frame.layout.shape[0], frame.total_vector_count);
    EXPECT_EQ(frame.layout.shape[1], frame.samples_per_vector * 2u);
    EXPECT_EQ(frame.layout.stride[0], frame.layout.shape[1]);
    EXPECT_EQ(frame.layout.stride[1], 1u);
    EXPECT_NE(frame.ordered_set_payload_hash, 0u);
    EXPECT_EQ(frame.split_boundary_input_hash, frame.ordered_set_payload_hash);
    EXPECT_NE(frame.split_boundary_output_hash, 0u);
}

// --- PR3b: Accounting invariant (total_vector_count == sum of segments == emitted vectors) ---

TEST(CrsdApertureAssemblyAdapterNodeTest, TotalVectorCountEqualsSumOfSegmentVectorsAndEmittedPayload) {
    auto result = MakeValidThreeSegmentResult(/*vectors_per_segment=*/3u);
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    const auto& frame = assembled->frame;
    EXPECT_EQ(frame.total_vector_count, 9u);  // 3 segments x 3 vectors each

    // Sum of segment vector_count fields must equal total.
    std::uint64_t sum_from_segments = 0u;
    for (const auto& seg : frame.segments) {
        sum_from_segments += seg.vector_count;
    }
    EXPECT_EQ(sum_from_segments, frame.total_vector_count);

    // Emitted payload vector count must equal total.
    std::uint64_t emitted = 0u;
    for (const auto& seg : frame.segments) {
        emitted += seg.vectors.size();
    }
    EXPECT_EQ(emitted, frame.total_vector_count);
}

// --- PR3b: Channel consistency enforcement ---

TEST(CrsdApertureAssemblyAdapterNodeTest, ChannelIdPropagatedCorrectlyThroughAdapterBoundary) {
    auto result = MakeValidThreeSegmentResult(2u, 4u, 9.6e9, 1.0e9, /*channel_id=*/42u);
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    for (const auto& seg : assembled->frame.segments) {
        EXPECT_EQ(seg.channel_id, 42u);
        for (const auto& vec : seg.vectors) {
            EXPECT_EQ(vec.channel_id, 42u);
        }
    }
}

TEST(CrsdApertureAssemblyAdapterNodeTest, InconsistentChannelIdAcrossSegmentsRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    // Sabotage segment 1 to have a different channel id.
    result.value.segments[1].channel_id = 99u;
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

// --- PR3b: SarAccelControlToken preservation ---

TEST(CrsdApertureAssemblyAdapterNodeTest, SarAccelControlTokenPreservedThroughAdapterBoundary) {
    auto result = MakeValidThreeSegmentResult();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }

    // Build EOS token with distinctive sidecar identity fields.
    sar::SarAccelControlToken eos_token = MakeEosToken();
    eos_token.sidecar.stream_id = 17u;
    eos_token.sidecar.backend_id = 3u;
    eos_token.sidecar.payload_byte_count = 123u;

    auto assembled = adapter.Transfer(eos_token,
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    // Marker must be EndOfStream.
    EXPECT_EQ(assembled->control.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    // synthetic flag must be cleared by adapter.
    EXPECT_FALSE(assembled->control.sidecar.synthetic);
    // Edge identity fields from input token must survive.
    EXPECT_EQ(assembled->control.sidecar.stream_id, 17u);
    EXPECT_EQ(assembled->control.sidecar.backend_id, 3u);
    EXPECT_EQ(assembled->control.sidecar.payload_byte_count, 123u);
}

// --- PR3b: Vector/channel/sample ordering ---

TEST(CrsdApertureAssemblyAdapterNodeTest, PerVectorSampleOrderSurvivesAdapterBoundary) {
    auto result = MakeValidThreeSegmentResult(2u, 4u);
    // Stamp each vector's signal with unique values to detect reordering.
    for (auto& seg : result.value.segments) {
        for (auto& vec : seg.vectors) {
            for (std::size_t s = 0; s < vec.signal.size(); ++s) {
                vec.signal[s] = std::complex<float>{
                    static_cast<float>(vec.vector_index * 100u + s),
                    static_cast<float>(vec.vector_index * 100u + s + 50u)};
            }
        }
    }
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    for (const auto& seg : assembled->frame.segments) {
        for (const auto& vec : seg.vectors) {
            ASSERT_EQ(vec.samples.size(), 4u);
            for (std::size_t s = 0; s < 4u; ++s) {
                EXPECT_FLOAT_EQ(vec.samples[s].real(),
                    static_cast<float>(vec.vector_index * 100u + s));
                EXPECT_FLOAT_EQ(vec.samples[s].imag(),
                    static_cast<float>(vec.vector_index * 100u + s + 50u));
            }
        }
    }
}

// --- PR3b: Split/merge partition metadata contract ---

TEST(CrsdApertureAssemblyAdapterNodeTest, PartitionSchemeIsPopulatedWithCorrectContractForPR4) {
    auto result = MakeValidThreeSegmentResult(2u);
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(assembled.has_value());
    const auto& scheme = assembled->frame.partition_scheme;
    EXPECT_EQ(scheme.expected_partition_count, 3u);
    EXPECT_NE(scheme.merge_ordering_key, 0u);
    ASSERT_EQ(scheme.partitions.size(), 3u);

    // Verify no overlapping or gapped vector ranges.
    std::uint64_t expected_start = 0u;
    for (const auto& partition : scheme.partitions) {
        EXPECT_EQ(partition.partition_count, 3u);
        EXPECT_EQ(partition.global_vector_start, expected_start);
        EXPECT_GT(partition.vector_count, 0u);
        EXPECT_NE(partition.input_boundary_hash, 0u);
        expected_start += partition.vector_count;
    }
    EXPECT_EQ(expected_start, assembled->frame.total_vector_count);

    // Ordering keys must be unique for deterministic merge ordering.
    std::vector<std::uint64_t> keys;
    for (const auto& partition : scheme.partitions) {
        keys.push_back(partition.ordering_key);
    }
    const auto keys_copy = keys;
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    EXPECT_EQ(keys.size(), 3u);
}

// --- PR3b: Geometry-field validation (nonfinite rejection) ---

TEST(CrsdApertureAssemblyAdapterNodeTest, NonfiniteRcvTimeRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    result.value.segments[1].vectors[0].rcv_time_s = std::numeric_limits<double>::quiet_NaN();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

TEST(CrsdApertureAssemblyAdapterNodeTest, InfinitePositionRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    result.value.segments[0].vectors[0].platform_position_m[2] =
        std::numeric_limits<double>::infinity();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

TEST(CrsdApertureAssemblyAdapterNodeTest, NanCarrierHzRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    result.value.segments[0].carrier_hz = std::numeric_limits<double>::quiet_NaN();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

// --- PR3b: Empty/truncated/dropped payload negative tests ---

TEST(CrsdApertureAssemblyAdapterNodeTest, EmptySignalVectorRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    // Drop all signal samples from one vector.
    result.value.segments[0].vectors[0].signal.clear();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

TEST(CrsdApertureAssemblyAdapterNodeTest, TruncatedSignalVectorRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult(2u, 4u);
    // Truncate to 2 samples instead of 4.
    result.value.segments[2].vectors[0].signal.resize(2u);
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

TEST(CrsdApertureAssemblyAdapterNodeTest, ZeroVectorCountSegmentRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult();
    result.value.segments[1].vector_count = 0u;
    result.value.segments[1].vectors.clear();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}

// --- PR3b: Sidecar-as-physics-misuse negative test ---
// The adapter must build its phase-history from typed CRSD reader payload, not from sidecar routing fields.
// This test verifies: when sidecar routing fields carry contradictory values, the adapter ignores them
// (or only treats them as optional cross-check when explicitly configured) and the payload is authoritative.

TEST(CrsdApertureAssemblyAdapterNodeTest, SidecarRoutingFieldsNotUsedAsPhysicsInputWhenCrossCheckDisabled) {
    auto result = MakeValidThreeSegmentResult();
    auto fake_reader = std::make_shared<FakeReader>(result);

    // Cross-check disabled (default).
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})},
        {"enable_sidecar_pulse_range_cross_check", false},
    })));

    // Tokens with wildly wrong sidecar routing fields must still be accepted and processed.
    for (std::uint64_t i = 0u; i < 3u; ++i) {
        auto token = MakeDataToken(i);
        // Overwrite sidecar routing with garbage values.
        token.sidecar.pulse_range_start = 99999u;
        token.sidecar.pulse_range_count = 99999u;
        token.sidecar.payload_byte_count = 0u;
        EXPECT_FALSE(adapter.Transfer(token,
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{}).has_value());
    }
    auto assembled = adapter.Transfer(MakeEosToken(),
        std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
    // Assembly must succeed based on typed CRSD payload, regardless of sidecar values.
    ASSERT_TRUE(assembled.has_value());
    EXPECT_EQ(assembled->frame.total_vector_count, 6u);
    EXPECT_EQ(assembled->frame.samples_per_vector, 4u);
}

// --- PR3b: Per-segment-finalization negative test ---
// No output must be produced on data tokens; output is only allowed on EOS.

TEST(CrsdApertureAssemblyAdapterNodeTest, NoOutputProducedOnDataTokensOnlyOnEos) {
    auto result = MakeValidThreeSegmentResult();
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})));

    // Every data token must return nullopt; output only on EOS.
    for (std::uint64_t i = 0u; i < 3u; ++i) {
        auto out = adapter.Transfer(MakeDataToken(i),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
        EXPECT_FALSE(out.has_value()) << "Data token " << i << " produced output - per-segment output is forbidden.";
    }

    // EOS must produce exactly one assembled output.
    std::size_t eos_output_count = 0u;
    for (int eos_attempt = 0; eos_attempt < 3; ++eos_attempt) {
        auto out = adapter.Transfer(MakeEosToken(),
            std::integral_constant<std::size_t, 0>{}, std::integral_constant<std::size_t, 0>{});
        if (out.has_value()) {
            ++eos_output_count;
        }
    }
    // Only the first EOS produces output; subsequent EOS are no-ops (completion_emitted_ guard).
    EXPECT_EQ(eos_output_count, 1u);
}

// --- PR3b: Accounting mismatch rejection ---
// If total_vector_count in read result is inconsistent, configure must throw.

TEST(CrsdApertureAssemblyAdapterNodeTest, AccountingMismatchInReadResultRejectedAtConfigure) {
    auto result = MakeValidThreeSegmentResult(2u);
    // Corrupt the total - it should be 6 but we say 99.
    result.value.total_vector_count = 99u;
    auto fake_reader = std::make_shared<FakeReader>(result);
    sar::CrsdApertureAssemblyAdapterNode adapter(sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    EXPECT_THROW(adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}})),
        graph::ConfigError);
}
