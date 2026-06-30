// SPDX-License-Identifier: MIT

/**
 * @file ImageTileMergeNode.cpp
 * @brief GraphX source file.
 */

#include "sar/ImageTileMergeNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <chrono>
#include <algorithm>

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

} // namespace

ImageTileMergeNode::ImageTileMergeNode(ImageTileMergeConfig config)
    : config_(config) {}

std::optional<SarControlToken> ImageTileMergeNode::Transfer(
    const SarControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = runtime::SteadyClock::now();

    if (!input.has_host_view || !graph::gpu::accel::IsValidView(input.host_view)) {
        return std::nullopt;
    }

    const auto marker = input.sidecar.marker;
    const auto tile_id = input.sidecar.tile_id;
    const auto sequence_id = input.sidecar.sequence_id;
    const auto stream_id = input.sidecar.stream_id;
    const auto token_byte_count = input.sidecar.payload_byte_count;
    const auto effective_byte_count = (marker == SarFrameMarker::Data)
                                          ? std::max<std::size_t>(token_byte_count, static_cast<std::size_t>(input.host_view.bytes))
                                          : 0u;

    if (completion_emitted_) {
        return std::nullopt;
    }

    const std::uint32_t expected_tiles = ResolveExpectedTiles();

    const auto finalize_status = [&](SarFrameMarker output_marker, bool complete) {
        stage_timing_totals_.merge_stage_time_us += runtime::ElapsedUs(stage_start);
        auto output = BuildOutputToken(
            input,
            sequence_id,
            tile_id,
            stream_id,
            effective_byte_count,
            output_marker,
            complete);
        output.sidecar.stage_timings = stage_timing_totals_;
        return std::optional<SarControlToken>(std::move(output));
    };

    if (marker == SarFrameMarker::Watermark) {
        watermark_seen_ = true;
        return finalize_status(SarFrameMarker::Watermark, false);
    }

    if (marker == SarFrameMarker::EndOfStream) {
        if (config_.require_all_tile_eos_before_complete) {
            eos_tiles_.insert(tile_id);
        }
        const std::uint32_t missing_tiles =
            (received_tiles_ >= expected_tiles) ? 0u : (expected_tiles - received_tiles_);
        const bool all_required_eos_seen =
            !config_.require_all_tile_eos_before_complete ||
            (eos_tiles_.size() >= expected_tiles);
        const bool complete =
            (missing_tiles == 0u) &&
            (!config_.require_watermark_before_complete || watermark_seen_) &&
            all_required_eos_seen;

        if (complete) {
            completion_emitted_ = true;
        }
        return finalize_status(SarFrameMarker::EndOfStream, complete);
    }

    if (!has_first_data_sequence_) {
        first_data_sequence_ = sequence_id;
        has_first_data_sequence_ = true;
    }

    const auto transfer_bytes = static_cast<std::uint64_t>(effective_byte_count);
    bytes_h2d_ += transfer_bytes;
    bytes_d2h_ += transfer_bytes;
    ++kernel_dispatches_;
    transfer_h2d_time_us_ += (transfer_bytes > 0u) ? 1u : 0u;
    kernel_exec_time_us_ += (transfer_bytes > 0u) ? 1u : 0u;
    transfer_d2h_time_us_ += (transfer_bytes > 0u) ? 1u : 0u;
    stage_timing_totals_.range_window_time_us += input.sidecar.stage_timings.range_window_time_us;
    stage_timing_totals_.range_compression_time_us += input.sidecar.stage_timings.range_compression_time_us;
    stage_timing_totals_.split_time_us += input.sidecar.stage_timings.split_time_us;
    stage_timing_totals_.h2d_stage_time_us += input.sidecar.stage_timings.h2d_stage_time_us;
    stage_timing_totals_.backprojection_stage_time_us += input.sidecar.stage_timings.backprojection_stage_time_us;
    stage_timing_totals_.d2h_stage_time_us += input.sidecar.stage_timings.d2h_stage_time_us;

    const auto [_, inserted] = seen_tiles_.insert(tile_id);
    if (!inserted) {
        ++duplicate_tiles_;
    } else {
        ++received_tiles_;

        if (has_last_tile_ && tile_id < last_tile_id_) {
            ++out_of_order_tiles_;
        }
        last_tile_id_ = tile_id;
        has_last_tile_ = true;
    }

    return finalize_status(SarFrameMarker::Data, false);
}

void ImageTileMergeNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("expected_tiles")) {
        auto value = cfg.TryGetInt("expected_tiles");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("expected_tiles must be > 0");
        }
        config.expected_tiles = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("require_watermark_before_complete")) {
        auto value = cfg.TryGetBool("require_watermark_before_complete");
        if (!value) {
            throw value.error();
        }
        config.require_watermark_before_complete = value.value();
    }

    if (cfg.Contains("require_all_tile_eos_before_complete")) {
        auto value = cfg.TryGetBool("require_all_tile_eos_before_complete");
        if (!value) {
            throw value.error();
        }
        config.require_all_tile_eos_before_complete = value.value();
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

    SetConfig(config);
}

graph::JsonView ImageTileMergeNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["expected_tiles"] = config_.expected_tiles;
    parameters_cache_["require_watermark_before_complete"] =
        config_.require_watermark_before_complete;
    parameters_cache_["require_all_tile_eos_before_complete"] =
        config_.require_all_tile_eos_before_complete;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView ImageTileMergeNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
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

std::vector<std::string> ImageTileMergeNode::GetParameterNames() const {
    return {
        "expected_tiles",
        "require_watermark_before_complete",
        "require_all_tile_eos_before_complete",
        "backend_id",
        "backend",
    };
}

void ImageTileMergeNode::Reset() {
    seen_tiles_.clear();
    eos_tiles_.clear();
    received_tiles_ = 0;
    duplicate_tiles_ = 0;
    out_of_order_tiles_ = 0;
    bytes_h2d_ = 0;
    bytes_d2h_ = 0;
    kernel_dispatches_ = 0;
    transfer_h2d_time_us_ = 0;
    kernel_exec_time_us_ = 0;
    transfer_d2h_time_us_ = 0;
    stage_timing_totals_ = {};
    last_tile_id_ = 0;
    has_last_tile_ = false;
    watermark_seen_ = false;
    completion_emitted_ = false;
    first_data_sequence_ = 0;
    has_first_data_sequence_ = false;
}

void ImageTileMergeNode::SetConfig(const ImageTileMergeConfig& config) {
    config_ = config;
    Reset();
}

const ImageTileMergeConfig& ImageTileMergeNode::GetConfig() const noexcept {
    return config_;
}

SarControlToken ImageTileMergeNode::BuildOutputToken(
    const SarControlToken& input,
    std::uint64_t sequence_id,
    std::uint32_t,
    std::uint32_t stream_id,
    std::size_t byte_count,
    SarFrameMarker marker,
    bool complete) const {
    SarControlToken out = input;
    FinalizeOutputSidecar(out, input, sequence_id, stream_id, byte_count, marker, complete);
    FinalizeOutputTickets(out, input);

    return out;
}

void ImageTileMergeNode::FinalizeOutputSidecar(
    SarControlToken& out,
    const SarControlToken& input,
    const std::uint64_t sequence_id,
    const std::uint32_t stream_id,
    const std::size_t byte_count,
    const SarFrameMarker marker,
    const bool complete) const {
    const std::uint32_t expected_tiles = ResolveExpectedTiles();
    const std::uint32_t missing_tiles =
        (received_tiles_ >= expected_tiles) ? 0u : (expected_tiles - received_tiles_);

    out.sidecar.sequence_id = input.sidecar.sequence_id ? input.sidecar.sequence_id : sequence_id;
    out.sidecar.batch_id = input.sidecar.batch_id;
    out.sidecar.aperture_id =
        input.sidecar.aperture_id ? input.sidecar.aperture_id : sequence_id;
    out.sidecar.pulse_range_start =
        input.sidecar.pulse_range_start ? input.sidecar.pulse_range_start : sequence_id;
    out.sidecar.pulse_range_count =
        input.sidecar.pulse_range_count
            ? input.sidecar.pulse_range_count
            : ((marker == SarFrameMarker::Data) ? 1u : 0u);
    out.sidecar.stream_id = input.sidecar.stream_id ? input.sidecar.stream_id : stream_id;
    out.sidecar.tile_id = input.sidecar.tile_id;
    out.sidecar.tile_count = input.sidecar.tile_count ? input.sidecar.tile_count : expected_tiles;
    out.sidecar.backend_id = input.sidecar.backend_id;
    out.sidecar.backend = input.sidecar.backend;
    out.sidecar.marker = marker;
    out.sidecar.synthetic = input.sidecar.synthetic;
    out.sidecar.payload_byte_count = byte_count;

    out.sidecar.expected_tiles = expected_tiles;
    out.sidecar.received_tiles = received_tiles_;
    out.sidecar.duplicate_tiles = duplicate_tiles_;
    out.sidecar.missing_tiles = missing_tiles;
    out.sidecar.out_of_order_tiles = out_of_order_tiles_;
    out.sidecar.bytes_h2d = bytes_h2d_;
    out.sidecar.bytes_d2h = bytes_d2h_;
    out.sidecar.kernel_dispatches = kernel_dispatches_;
    out.sidecar.transfer_h2d_time_us = transfer_h2d_time_us_;
    out.sidecar.kernel_exec_time_us = kernel_exec_time_us_;
    out.sidecar.transfer_d2h_time_us = transfer_d2h_time_us_;
    out.sidecar.stage_timings = stage_timing_totals_;
    out.sidecar.watermark_seen = watermark_seen_;
    out.sidecar.merge_complete = complete;

    if (has_first_data_sequence_ && sequence_id >= first_data_sequence_) {
        out.sidecar.fanin_wait_ms = sequence_id - first_data_sequence_;
    }

    if (marker != SarFrameMarker::Data) {
        out.sidecar.bytes_h2d = bytes_h2d_;
        out.sidecar.bytes_d2h = bytes_d2h_;
    } else if (byte_count == 0u) {
        out.sidecar.bytes_h2d = bytes_h2d_;
        out.sidecar.bytes_d2h = bytes_d2h_;
    }
}

void ImageTileMergeNode::FinalizeOutputTickets(
    SarControlToken& out,
    const SarControlToken& input) const {
    out.host_view = input.host_view;
    out.has_host_view = true;

    out.transfer_ticket.backend = input.host_view.backend;
    out.transfer_ticket.transfer_id = out.sidecar.sequence_id;
    out.transfer_ticket.execution_queue_id =
        (input.sidecar.d2h_queue_id > 0u)
            ? input.sidecar.d2h_queue_id
            : ((input.sidecar.h2d_queue_id > 0u)
                   ? input.sidecar.h2d_queue_id
                   : (static_cast<std::uint64_t>(config_.backend_id) + 1u));
    out.transfer_ticket.dst_host = input.host_view;
    out.transfer_ticket = runtime::MakeSyntheticTransferTicket(out.transfer_ticket);
    out.has_transfer_ticket = true;

    out.kernel_ticket.backend = ToAccelBackendKind(out.sidecar.backend);
    out.kernel_ticket.kernel_id = static_cast<std::uint64_t>(out.sidecar.backend_id) + 3301u;
    out.kernel_ticket.execution_queue_id =
        (input.sidecar.kernel_queue_id > 0u)
            ? input.sidecar.kernel_queue_id
            : (static_cast<std::uint64_t>(out.sidecar.backend_id) + 1u);
    out.kernel_ticket.arg_count = 1;
    out.kernel_ticket = runtime::MakeSyntheticKernelTicket(out.kernel_ticket);
    out.has_kernel_ticket =
        out.kernel_ticket.backend != graph::gpu::accel::BackendKind::Unknown;
}

std::uint32_t ImageTileMergeNode::ResolveExpectedTiles() const noexcept {
    return std::max<std::uint32_t>(1u, config_.expected_tiles);
}

} // namespace sar
