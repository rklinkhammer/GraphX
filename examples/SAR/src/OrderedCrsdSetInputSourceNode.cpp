// SPDX-License-Identifier: MIT

/**
 * @file OrderedCrsdSetInputSourceNode.cpp
 * @brief GraphX source file.
 */

#include "sar/OrderedCrsdSetInputSourceNode.hpp"

#include "config/ConfigError.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include <atomic>
#include <filesystem>

namespace sar {
namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

std::uint64_t NextOpaqueTokenId() {
    static std::atomic<std::uint64_t> next_id{1u};
    return next_id.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

OrderedCrsdSetInputSourceNode::OrderedCrsdSetInputSourceNode()
    : reader_(std::make_shared<graphx::sar::CrsdReader>()) {}

OrderedCrsdSetInputSourceNode::OrderedCrsdSetInputSourceNode(
    OrderedCrsdSetInputSourceConfig config,
    graphx::sar::CrsdReaderPtr reader)
    : config_(std::move(config)),
      reader_(std::move(reader)) {
    if (!reader_) {
        reader_ = std::make_shared<graphx::sar::CrsdReader>();
    }
}

std::optional<SarAccelControlToken> OrderedCrsdSetInputSourceNode::Produce(
    std::integral_constant<std::size_t, 0>) {
    if (!read_result_.success) {
        return std::nullopt;
    }
    if (eos_emitted_) {
        return std::nullopt;
    }
    if (next_segment_index_ < read_result_.value.segments.size()) {
        return MakeSegmentToken(read_result_.value.segments.at(next_segment_index_++));
    }
    eos_emitted_ = true;
    return MakeEndOfStreamToken();
}

void OrderedCrsdSetInputSourceNode::Configure(const graph::JsonView& cfg) {
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
    if (cfg.Contains("stream_id")) {
        auto value = cfg.TryGetInt("stream_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("stream_id must be >= 0");
        }
        config.stream_id = static_cast<std::uint32_t>(value.value());
    }
    if (cfg.Contains("backend_id")) {
        auto value = cfg.TryGetInt("backend_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("backend_id must be >= 0");
        }
        config.backend_id = static_cast<std::uint32_t>(value.value());
    }
    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    std::size_t mode_count = 0u;
    if (!config.crsd_paths.empty()) {
        ++mode_count;
    }
    if (!config.crsd_directory.empty()) {
        ++mode_count;
    }
    if (!config.manifest_path.empty()) {
        ++mode_count;
    }
    if (mode_count != 1u) {
        throw graph::ConfigError("exactly one of crsd_paths, crsd_directory, or manifest_path must be set");
    }

    graphx::sar::CrsdReadOptions options{};
    for (const auto& path : config.crsd_paths) {
        options.ordered_crsd_paths.push_back(std::filesystem::path(path));
    }
    options.crsd_directory = std::filesystem::path(config.crsd_directory);
    options.manifest_path = std::filesystem::path(config.manifest_path);
    options.require_contiguous_segment_indices = true;

    read_result_ = reader_->ReadOrderedSet(options);
    if (!read_result_.success) {
        throw graph::ConfigError(read_result_.diagnostic);
    }

    SetConfig(config);
}

graph::JsonView OrderedCrsdSetInputSourceNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["crsd_paths"] = config_.crsd_paths;
    parameters_cache_["crsd_directory"] = config_.crsd_directory;
    parameters_cache_["manifest_path"] = config_.manifest_path;
    parameters_cache_["stream_id"] = config_.stream_id;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    parameters_cache_["segment_count"] = read_result_.value.segments.size();
    parameters_cache_["total_vector_count"] = read_result_.value.total_vector_count;
    parameters_cache_["ordered_set_payload_hash"] = read_result_.value.ordered_set_payload_hash;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView OrderedCrsdSetInputSourceNode::GetParameterDescription(const std::string& param_name) const {
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

std::vector<std::string> OrderedCrsdSetInputSourceNode::GetParameterNames() const {
    return {
        "crsd_paths",
        "crsd_directory",
        "manifest_path",
        "stream_id",
        "backend_id",
        "backend",
    };
}

void OrderedCrsdSetInputSourceNode::Reset() {
    next_segment_index_ = 0;
    eos_emitted_ = false;
}

void OrderedCrsdSetInputSourceNode::SetConfig(const OrderedCrsdSetInputSourceConfig& config) {
    config_ = config;
    Reset();
}

const OrderedCrsdSetInputSourceConfig& OrderedCrsdSetInputSourceNode::GetConfig() const noexcept {
    return config_;
}

const graphx::sar::CrsdReadResult& OrderedCrsdSetInputSourceNode::GetLastReadResult() const noexcept {
    return read_result_;
}

SarAccelControlToken OrderedCrsdSetInputSourceNode::MakeSegmentToken(
    const graphx::sar::CrsdSegmentRecord& segment) const {
    SarAccelControlToken out{};
    out.token_id = NextOpaqueTokenId();
    out.sidecar.sequence_id = segment.segment_index;
    out.sidecar.batch_id = config_.stream_id;
    out.sidecar.aperture_id = segment.segment_index;
    out.sidecar.pulse_range_start = segment.global_vector_start;
    out.sidecar.pulse_range_count = static_cast<std::uint32_t>(segment.vector_count);
    out.sidecar.stream_id = config_.stream_id;
    out.sidecar.tile_id = static_cast<std::uint32_t>(segment.segment_index);
    out.sidecar.tile_count = static_cast<std::uint32_t>(read_result_.value.segments.size());
    out.sidecar.backend_id = config_.backend_id;
    out.sidecar.backend = config_.backend;
    out.sidecar.marker = SarFrameMarker::Data;
    out.sidecar.synthetic = false;
    out.sidecar.payload_byte_count = static_cast<std::size_t>(
        segment.vector_count * segment.samples_per_vector * sizeof(float) * 2u);

    auto& host_view = out.host_view;
    const auto backend = ToAccelBackendKind(config_.backend);
    host_view.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    host_view.host_ptr = runtime::OpaqueHostPointer();
    host_view.bytes = static_cast<std::uint64_t>(out.sidecar.payload_byte_count);
    host_view.dtype = graph::gpu::accel::DataType::Float32;
    host_view.layout = MakeAccelVectorLayout(
        static_cast<std::uint64_t>(segment.vector_count * segment.samples_per_vector * 2u));
    host_view.allocator_id = static_cast<std::uint64_t>(config_.backend_id) + 1u;
    out.has_host_view = true;
    return out;
}

SarAccelControlToken OrderedCrsdSetInputSourceNode::MakeEndOfStreamToken() const {
    SarAccelControlToken out{};
    out.token_id = NextOpaqueTokenId();
    out.sidecar.sequence_id = static_cast<std::uint64_t>(read_result_.value.segments.size());
    out.sidecar.batch_id = config_.stream_id;
    out.sidecar.aperture_id = out.sidecar.sequence_id;
    out.sidecar.pulse_range_start = read_result_.value.total_vector_count;
    out.sidecar.pulse_range_count = 0;
    out.sidecar.stream_id = config_.stream_id;
    out.sidecar.tile_id = 0u;
    out.sidecar.tile_count = static_cast<std::uint32_t>(read_result_.value.segments.size());
    out.sidecar.backend_id = config_.backend_id;
    out.sidecar.backend = config_.backend;
    out.sidecar.marker = SarFrameMarker::EndOfStream;
    out.sidecar.synthetic = false;
    out.sidecar.payload_byte_count = 0u;

    auto& host_view = out.host_view;
    const auto backend = ToAccelBackendKind(config_.backend);
    host_view.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    host_view.host_ptr = runtime::OpaqueHostPointer();
    host_view.bytes = static_cast<std::uint64_t>(sizeof(float));
    host_view.dtype = graph::gpu::accel::DataType::Float32;
    host_view.layout = MakeAccelVectorLayout(1u);
    host_view.allocator_id = static_cast<std::uint64_t>(config_.backend_id) + 1u;
    out.has_host_view = true;
    return out;
}

} // namespace sar
