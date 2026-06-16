#include "sar/CrsdApertureAssemblyAdapterNode.hpp"

#include "config/ConfigError.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace sar {
namespace {

std::uint64_t Fnv1a64(
    const void* data,
    std::size_t size,
    std::uint64_t seed = 14695981039346656037ull) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool NearlyEqual(double lhs, double rhs) {
    return std::fabs(lhs - rhs) <= 1e-9;
}

} // namespace

CrsdApertureAssemblyAdapterNode::CrsdApertureAssemblyAdapterNode()
    : reader_(std::make_shared<graphx::sar::CrsdReader>()) {}

CrsdApertureAssemblyAdapterNode::CrsdApertureAssemblyAdapterNode(
    CrsdApertureAssemblyAdapterConfig config,
    graphx::sar::CrsdReaderPtr reader)
    : config_(std::move(config)),
      reader_(std::move(reader)) {
    if (!reader_) {
        reader_ = std::make_shared<graphx::sar::CrsdReader>();
    }
}

std::optional<SarPhaseHistoryControlMessage> CrsdApertureAssemblyAdapterNode::Transfer(
    const SarAccelControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!read_result_.success || completion_emitted_) {
        return std::nullopt;
    }

    if (input.sidecar.marker == SarFrameMarker::Watermark) {
        last_diagnostic_ = "watermark_seen";
        return std::nullopt;
    }

    if (input.sidecar.marker == SarFrameMarker::Data) {
        const auto validation = ValidateDataToken(input);
        if (validation.has_value()) {
            last_diagnostic_ = *validation;
            return std::nullopt;
        }
        return std::nullopt;
    }

    if (input.sidecar.marker == SarFrameMarker::EndOfStream) {
        if (seen_segment_indices_.size() != read_result_.value.segments.size()) {
            for (std::uint64_t expected = 0u; expected < read_result_.value.segments.size(); ++expected) {
                if (!seen_segment_indices_.contains(expected)) {
                    last_diagnostic_ = "missing_segment_index:" + std::to_string(expected);
                    return std::nullopt;
                }
            }
        }

        const auto output = BuildApertureMessage(input);
        if (!output.has_value()) {
            return std::nullopt;
        }
        completion_emitted_ = true;
        last_diagnostic_ = "ok:aperture_assembled";
        return output;
    }

    last_diagnostic_ = "unsupported_marker";
    return std::nullopt;
}

void CrsdApertureAssemblyAdapterNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("crsd_paths")) {
        auto values = cfg.TryGetStringArray("crsd_paths");
        if (!values) {
            throw values.error();
        }
        config.crsd_paths = values.value();
    }
    if (cfg.Contains("crsd_directory")) {
        auto value = cfg.TryGetString("crsd_directory");
        if (!value) {
            throw value.error();
        }
        config.crsd_directory = value.value();
    }
    if (cfg.Contains("manifest_path")) {
        auto value = cfg.TryGetString("manifest_path");
        if (!value) {
            throw value.error();
        }
        config.manifest_path = value.value();
    }
    if (cfg.Contains("require_contiguous_segment_indices")) {
        auto value = cfg.TryGetBool("require_contiguous_segment_indices");
        if (!value) {
            throw value.error();
        }
        config.require_contiguous_segment_indices = value.value();
    }
    if (cfg.Contains("enable_sidecar_pulse_range_cross_check")) {
        auto value = cfg.TryGetBool("enable_sidecar_pulse_range_cross_check");
        if (!value) {
            throw value.error();
        }
        config.enable_sidecar_pulse_range_cross_check = value.value();
    }

    graphx::sar::CrsdReadOptions options{};
    for (const auto& path : config.crsd_paths) {
        options.ordered_crsd_paths.emplace_back(path);
    }
    options.crsd_directory = config.crsd_directory;
    options.manifest_path = config.manifest_path;
    options.require_contiguous_segment_indices = config.require_contiguous_segment_indices;

    read_result_ = reader_->ReadOrderedSet(options);
    if (!read_result_.success) {
        throw graph::ConfigError(read_result_.diagnostic);
    }

    config_ = std::move(config);
    ValidateAssemblyConsistency();
    BuildExpectedSegmentIndexMap();
    Reset();
    last_diagnostic_ = "configured";
}

graph::JsonView CrsdApertureAssemblyAdapterNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["crsd_paths"] = config_.crsd_paths;
    parameters_cache_["crsd_directory"] = config_.crsd_directory;
    parameters_cache_["manifest_path"] = config_.manifest_path;
    parameters_cache_["require_contiguous_segment_indices"] = config_.require_contiguous_segment_indices;
    parameters_cache_["enable_sidecar_pulse_range_cross_check"] = config_.enable_sidecar_pulse_range_cross_check;
    parameters_cache_["segment_count"] = read_result_.value.segments.size();
    parameters_cache_["total_vector_count"] = read_result_.value.total_vector_count;
    parameters_cache_["last_diagnostic"] = last_diagnostic_;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView CrsdApertureAssemblyAdapterNode::GetParameterDescription(const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            switch (field.type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> CrsdApertureAssemblyAdapterNode::GetParameterNames() const {
    return {
        "crsd_paths",
        "crsd_directory",
        "manifest_path",
        "require_contiguous_segment_indices",
        "enable_sidecar_pulse_range_cross_check",
    };
}

void CrsdApertureAssemblyAdapterNode::Reset() {
    seen_segment_indices_.clear();
    received_segment_order_.clear();
    expected_next_segment_index_ = 0u;
    completion_emitted_ = false;
    last_frame_.reset();
}

const CrsdApertureAssemblyAdapterConfig& CrsdApertureAssemblyAdapterNode::GetConfig() const noexcept {
    return config_;
}

const std::string& CrsdApertureAssemblyAdapterNode::GetLastDiagnostic() const noexcept {
    return last_diagnostic_;
}

const std::optional<SarPhaseHistoryApertureFrame>& CrsdApertureAssemblyAdapterNode::GetLastFrame() const noexcept {
    return last_frame_;
}

void CrsdApertureAssemblyAdapterNode::BuildExpectedSegmentIndexMap() {
    segments_by_index_.clear();
    for (const auto& segment : read_result_.value.segments) {
        segments_by_index_[segment.segment_index] = &segment;
    }
}

void CrsdApertureAssemblyAdapterNode::ValidateAssemblyConsistency() const {
    const auto& segments = read_result_.value.segments;
    if (segments.empty()) {
        throw graph::ConfigError("missing_product_crsd");
    }

    const auto expected_samples_per_vector = segments.front().samples_per_vector;
    const auto expected_carrier_hz = segments.front().carrier_hz;
    const auto expected_sample_rate_hz = segments.front().sample_rate_hz;
    const auto expected_channel_id = segments.front().channel_id;

    // Validate segment-level fields and cross-segment consistency.
    for (const auto& segment : segments) {
        if (segment.samples_per_vector == 0u) {
            throw graph::ConfigError(
                "zero_samples_per_vector:" + std::to_string(segment.segment_index));
        }
        if (segment.vector_count == 0u) {
            throw graph::ConfigError(
                "zero_vector_count:" + std::to_string(segment.segment_index));
        }
        if (!std::isfinite(segment.carrier_hz)) {
            throw graph::ConfigError(
                "nonfinite_carrier_hz:" + std::to_string(segment.segment_index));
        }
        if (!std::isfinite(segment.sample_rate_hz)) {
            throw graph::ConfigError(
                "nonfinite_sample_rate_hz:" + std::to_string(segment.segment_index));
        }
        if (segment.samples_per_vector != expected_samples_per_vector) {
            throw graph::ConfigError(
                "inconsistent_samples_per_vector:" + std::to_string(segment.segment_index));
        }
        if (!NearlyEqual(segment.carrier_hz, expected_carrier_hz) &&
            !(segment.carrier_hz == 0.0 && expected_carrier_hz == 0.0)) {
            throw graph::ConfigError(
                "inconsistent_carrier_hz:" + std::to_string(segment.segment_index));
        }
        if (!NearlyEqual(segment.sample_rate_hz, expected_sample_rate_hz) &&
            !(segment.sample_rate_hz == 0.0 && expected_sample_rate_hz == 0.0)) {
            throw graph::ConfigError(
                "inconsistent_sample_rate_hz:" + std::to_string(segment.segment_index));
        }
        if (segment.channel_id != expected_channel_id) {
            throw graph::ConfigError(
                "inconsistent_channel_id:" + std::to_string(segment.segment_index));
        }

        // Validate per-vector geometry fields.
        for (const auto& vec : segment.vectors) {
            if (!std::isfinite(vec.rcv_time_s)) {
                throw graph::ConfigError(
                    "nonfinite_rcv_time_s:segment:" + std::to_string(segment.segment_index) +
                    ":vector:" + std::to_string(vec.vector_index));
            }
            for (const auto& p : vec.platform_position_m) {
                if (!std::isfinite(p)) {
                    throw graph::ConfigError(
                        "nonfinite_platform_position:segment:" + std::to_string(segment.segment_index) +
                        ":vector:" + std::to_string(vec.vector_index));
                }
            }
            for (const auto& v : vec.platform_velocity_mps) {
                if (!std::isfinite(v)) {
                    throw graph::ConfigError(
                        "nonfinite_platform_velocity:segment:" + std::to_string(segment.segment_index) +
                        ":vector:" + std::to_string(vec.vector_index));
                }
            }
            if (vec.signal.size() != segment.samples_per_vector) {
                throw graph::ConfigError(
                    "signal_size_mismatch:segment:" + std::to_string(segment.segment_index) +
                    ":vector:" + std::to_string(vec.vector_index));
            }
        }
    }

    // Validate accounting: total_vector_count must equal sum of all segment vector_count fields.
    std::uint64_t computed_total = 0u;
    for (const auto& segment : segments) {
        computed_total += segment.vector_count;
    }
    if (computed_total != read_result_.value.total_vector_count) {
        throw graph::ConfigError(
            "total_vector_count_mismatch:expected:" + std::to_string(computed_total) +
            ":found:" + std::to_string(read_result_.value.total_vector_count));
    }
}

std::optional<std::string> CrsdApertureAssemblyAdapterNode::ValidateDataToken(
    const SarAccelControlToken& input) {
    const auto segment_index = input.sidecar.sequence_id;

    if (!segments_by_index_.contains(segment_index)) {
        return "unexpected_segment_index:" + std::to_string(segment_index);
    }

    if (!seen_segment_indices_.insert(segment_index).second) {
        return "duplicate_segment_index:" + std::to_string(segment_index);
    }

    if (config_.require_contiguous_segment_indices) {
        if (segment_index != expected_next_segment_index_) {
            return "out_of_order_segment_index:" + std::to_string(segment_index);
        }
        ++expected_next_segment_index_;
    }

    if (config_.enable_sidecar_pulse_range_cross_check) {
        const auto* segment = segments_by_index_.at(segment_index);
        if (input.sidecar.pulse_range_start != segment->global_vector_start ||
            static_cast<std::uint64_t>(input.sidecar.pulse_range_count) != segment->vector_count) {
            return "pulse_range_mismatch:" + std::to_string(segment_index);
        }
    }

    received_segment_order_.push_back(segment_index);
    return std::nullopt;
}

std::optional<SarPhaseHistoryControlMessage> CrsdApertureAssemblyAdapterNode::BuildApertureMessage(
    const SarAccelControlToken& eos_token) {
    SarPhaseHistoryApertureFrame frame{};
    frame.total_vector_count = read_result_.value.total_vector_count;
    frame.ordered_set_payload_hash = read_result_.value.ordered_set_payload_hash;
    frame.control_marker = SarPhaseHistoryControlMarker::EndOfStream;
    frame.ownership = SarPhaseHistoryOwnership::OwnedHostBuffer;
    frame.sample_format = SarPhaseHistorySampleFormat::ComplexFloat32Interleaved;

    const auto& segments = read_result_.value.segments;
    frame.segments.reserve(segments.size());

    std::uint64_t folded_hash = 14695981039346656037ull;
    for (const auto& segment : segments) {
        SarPhaseHistorySegment out_segment{};
        out_segment.segment_index = segment.segment_index;
        out_segment.channel_id = segment.channel_id;
        out_segment.global_vector_start = segment.global_vector_start;
        out_segment.vector_count = segment.vector_count;
        out_segment.samples_per_vector = segment.samples_per_vector;
        out_segment.carrier_hz = segment.carrier_hz;
        out_segment.sample_rate_hz = segment.sample_rate_hz;
        out_segment.payload_hash = segment.payload_hash;
        out_segment.first_vector_hash = segment.first_vector_hash;
        out_segment.last_vector_hash = segment.last_vector_hash;
        out_segment.vectors.reserve(segment.vectors.size());

        for (const auto& vector : segment.vectors) {
            SarPhaseHistoryVector out_vector{};
            out_vector.vector_index = vector.vector_index;
            out_vector.channel_id = vector.channel_id;
            out_vector.rcv_time_s = vector.rcv_time_s;
            out_vector.platform_position_m = vector.platform_position_m;
            out_vector.platform_velocity_mps = vector.platform_velocity_mps;
            out_vector.samples = vector.signal;
            out_vector.sample_payload_hash = Fnv1a64(
                vector.signal.data(),
                vector.signal.size() * sizeof(std::complex<float>));
            out_segment.vectors.push_back(std::move(out_vector));
        }

        frame.segments.push_back(std::move(out_segment));
        folded_hash = Fnv1a64(
            &segment.payload_hash,
            sizeof(segment.payload_hash),
            folded_hash);
    }

    frame.samples_per_vector = frame.segments.empty() ? 0u : frame.segments.front().samples_per_vector;
    frame.carrier_hz = frame.segments.empty() ? 0.0 : frame.segments.front().carrier_hz;
    frame.sample_rate_hz = frame.segments.empty() ? 0.0 : frame.segments.front().sample_rate_hz;

    frame.layout.rank = 2u;
    frame.layout.shape[0] = frame.total_vector_count;
    frame.layout.shape[1] = frame.samples_per_vector * 2u;
    frame.layout.stride[0] = frame.layout.shape[1];
    frame.layout.stride[1] = 1u;

    frame.split_boundary_input_hash = read_result_.value.ordered_set_payload_hash;
    frame.split_boundary_output_hash = folded_hash;

    // Build split/merge partition scheme: one partition per segment, covering its vector range,
    // deterministically keyed for stable merge ordering in PR4.
    SarAperturePartitionScheme scheme{};
    scheme.expected_partition_count = static_cast<std::uint64_t>(frame.segments.size());
    scheme.merge_ordering_key = frame.split_boundary_input_hash;
    scheme.partitions.reserve(frame.segments.size());
    for (const auto& seg : frame.segments) {
        SarAperturePartition partition{};
        partition.partition_id = seg.segment_index;
        partition.partition_count = scheme.expected_partition_count;
        partition.global_vector_start = seg.global_vector_start;
        partition.vector_count = seg.vector_count;
        partition.ordering_key = seg.segment_index;
        partition.input_boundary_hash = seg.payload_hash;
        partition.output_boundary_hash = seg.payload_hash;
        scheme.partitions.push_back(partition);
    }
    frame.partition_scheme = std::move(scheme);

    // Accounting invariant: verify total_vector_count equals emitted payload vector count.
    std::uint64_t emitted_vector_count = 0u;
    for (const auto& seg : frame.segments) {
        emitted_vector_count += seg.vectors.size();
    }
    if (emitted_vector_count != frame.total_vector_count) {
        last_diagnostic_ = "accounting_invariant_failure:emitted:" +
            std::to_string(emitted_vector_count) +
            ":expected:" + std::to_string(frame.total_vector_count);
        return std::nullopt;
    }

    SarPhaseHistoryControlMessage output{};
    output.control = eos_token;
    output.control.sidecar.synthetic = false;
    output.control.sidecar.marker = SarFrameMarker::EndOfStream;
    output.frame = std::move(frame);

    last_frame_ = output.frame;
    return output;
}

} // namespace sar
