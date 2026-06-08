#include "sar/GotchaReplaySourceNode.hpp"

#include "config/ConfigError.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

std::uint64_t RequireUInt64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        throw graph::ConfigError(std::string("Missing required Gotcha field: ") + key);
    }
    const auto& value = object.at(key);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw graph::ConfigError(std::string("Gotcha field must be an integer: ") + key);
    }
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
        throw graph::ConfigError(std::string("Gotcha field must be >= 0: ") + key);
    }
    return static_cast<std::uint64_t>(signed_value);
}

std::uint32_t RequireUInt32(const nlohmann::json& object, const char* key, std::uint32_t default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    const auto value = RequireUInt64(object, key);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw graph::ConfigError(std::string("Gotcha field exceeds uint32_t range: ") + key);
    }
    return static_cast<std::uint32_t>(value);
}

float RequireFloat(const nlohmann::json& object, const char* key, float default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    const auto& value = object.at(key);
    if (!value.is_number()) {
        throw graph::ConfigError(std::string("Gotcha field must be numeric: ") + key);
    }
    return value.get<float>();
}

std::string RequireString(const nlohmann::json& object, const char* key, const std::string& default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    const auto& value = object.at(key);
    if (!value.is_string()) {
        throw graph::ConfigError(std::string("Gotcha field must be a string: ") + key);
    }
    return value.get<std::string>();
}

std::array<float, 3> RequireTriplet(const nlohmann::json& object,
                                   const char* key,
                                   const std::array<float, 3>& default_value) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return default_value;
    }
    const auto& value = object.at(key);
    if (!value.is_array() || value.size() != 3u) {
        throw graph::ConfigError(std::string("Gotcha field must be a 3-element array: ") + key);
    }
    return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
}

SarIqSample ParseIqSample(const nlohmann::json& sample, std::size_t index) {
    if (sample.is_object()) {
        if (!sample.contains("real") || !sample.contains("imag")) {
            throw graph::ConfigError("Gotcha IQ sample object must contain real and imag");
        }
        return SarIqSample(sample.at("real").get<float>(), sample.at("imag").get<float>());
    }

    if (sample.is_array() && sample.size() == 2u) {
        return SarIqSample(sample.at(0).get<float>(), sample.at(1).get<float>());
    }

    std::ostringstream builder;
    builder << "Gotcha IQ sample " << index << " must be an object or 2-element array";
    throw graph::ConfigError(builder.str());
}

GotchaNormalizedPulseRecord ParseRecord(const nlohmann::json& record, std::size_t index) {
    if (!record.is_object()) {
        std::ostringstream builder;
        builder << "Gotcha record " << index << " must be an object";
        throw graph::ConfigError(builder.str());
    }

    GotchaNormalizedPulseRecord out{};
    out.frame_id = RequireUInt64(record, "frame_id");
    out.pass_id = RequireUInt64(record, "pass_id");
    out.pulse_block_id = RequireUInt64(record, "pulse_block_id");
    out.range_bin_start = RequireUInt64(record, "range_bin_start");
    out.range_bin_count = RequireUInt32(record, "range_bin_count", 0u);
    out.aperture_span_start = RequireUInt64(record, "aperture_span_start");
    out.aperture_span_count = RequireUInt32(record, "aperture_span_count", 1u);
    out.timestamp_us = RequireUInt64(record, "timestamp_us");
    out.ordering_key = RequireUInt64(record, "ordering_key");
    out.stream_id = RequireUInt32(record, "stream_id", 0u);
    out.backend_id = RequireUInt32(record, "backend_id", 0u);

    if (record.contains("backend")) {
        out.backend = ParseBackendKind(static_cast<int>(RequireUInt64(record, "backend")));
    }

    out.platform_position_m = RequireTriplet(record, "platform_position_m", {0.0f, 0.0f, 0.0f});
    out.platform_velocity_mps = RequireTriplet(record, "platform_velocity_mps", {0.0f, 0.0f, 0.0f});
    out.scene_center_m = RequireTriplet(record, "scene_center_m", {0.0f, 0.0f, 0.0f});
    out.carrier_hz = RequireFloat(record, "carrier_hz", 0.0f);
    out.bandwidth_hz = RequireFloat(record, "bandwidth_hz", 0.0f);
    out.sample_rate_hz = RequireFloat(record, "sample_rate_hz", 0.0f);
    out.calibration_gain = RequireFloat(record, "calibration_gain", 1.0f);
    out.calibration_phase_rad = RequireFloat(record, "calibration_phase_rad", 0.0f);
    out.polarization = RequireString(record, "polarization", "unknown");
    out.coordinate_frame = RequireString(record, "coordinate_frame", "unknown");
    out.sample_layout = RequireString(record, "sample_layout", "interleaved_complex_f32");
    out.endianness = RequireString(record, "endianness", "native");

    if (!record.contains("iq_samples") || !record.at("iq_samples").is_array()) {
        throw graph::ConfigError("Gotcha record must contain an iq_samples array");
    }

    const auto& samples = record.at("iq_samples");
    out.iq_samples.reserve(samples.size());
    for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index) {
        out.iq_samples.push_back(ParseIqSample(samples.at(sample_index), sample_index));
    }

    if (out.range_bin_count == 0u) {
        out.range_bin_count = static_cast<std::uint32_t>(out.iq_samples.size());
    }
    if (out.range_bin_count != out.iq_samples.size()) {
        throw graph::ConfigError("range_bin_count must match iq_samples size when provided");
    }

    if (out.ordering_key == 0u) {
        out.ordering_key = out.frame_id;
    }

    return out;
}

} // namespace

std::vector<GotchaNormalizedPulseRecord> GotchaOfflineConverter::LoadFromFile(
    const std::filesystem::path& fixture_path) const {
    std::ifstream input(fixture_path);
    if (!input) {
        throw graph::ConfigError("Unable to open Gotcha fixture file: " + fixture_path.string());
    }

    nlohmann::json document;
    input >> document;
    return LoadFromJson(document);
}

std::vector<GotchaNormalizedPulseRecord> GotchaOfflineConverter::LoadFromJson(
    const nlohmann::json& document) const {
    if (document.is_object() && document.contains("schema")) {
        const auto schema = document.at("schema").get<std::string>();
        if (schema != "graphx.sar.gotcha.normalized.v1") {
            throw graph::ConfigError("Unsupported Gotcha fixture schema: " + schema);
        }
    }

    const nlohmann::json* records_json = nullptr;
    if (document.is_object() && document.contains("records")) {
        records_json = &document.at("records");
    } else if (document.is_array()) {
        records_json = &document;
    }

    if (records_json == nullptr || !records_json->is_array()) {
        throw graph::ConfigError("Gotcha fixture must provide an array of records");
    }

    std::vector<GotchaNormalizedPulseRecord> records;
    records.reserve(records_json->size());
    for (std::size_t index = 0; index < records_json->size(); ++index) {
        records.push_back(ParseRecord(records_json->at(index), index));
    }
    return records;
}

GotchaReplaySourceNode::GotchaReplaySourceNode(GotchaReplaySourceConfig config)
    : config_(std::move(config)) {}

std::optional<SarPulseBlockMessage> GotchaReplaySourceNode::Produce(
    std::integral_constant<std::size_t, 0>) {
    if (eos_emitted_) {
        return std::nullopt;
    }

    if (next_record_index_ < config_.records.size()) {
        auto out = MakeMessage(config_.records[next_record_index_]);
        ++next_record_index_;
        return out;
    }

    if (config_.emit_watermark && !watermark_emitted_) {
        watermark_emitted_ = true;
        return MakeControlMessage(SarFrameMarker::Watermark);
    }

    eos_emitted_ = true;
    return MakeControlMessage(SarFrameMarker::EndOfStream);
}

void GotchaReplaySourceNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("fixture_path")) {
        auto value = cfg.TryGetString("fixture_path");
        if (!value) {
            throw value.error();
        }
        if (value.value().empty()) {
            throw graph::ConfigError("fixture_path must not be empty");
        }
        config.fixture_path = value.value();
    } else {
        throw graph::ConfigError("fixture_path is required");
    }

    if (cfg.Contains("emit_watermark")) {
        auto value = cfg.TryGetBool("emit_watermark");
        if (!value) {
            throw value.error();
        }
        config.emit_watermark = value.value();
    }

    config.records = converter_.LoadFromFile(config.fixture_path);
    SetConfig(config);
}

graph::JsonView GotchaReplaySourceNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["fixture_path"] = config_.fixture_path.string();
    parameters_cache_["emit_watermark"] = config_.emit_watermark;
    parameters_cache_["record_count"] = config_.records.size();
    return graph::JsonView(parameters_cache_);
}

graph::JsonView GotchaReplaySourceNode::GetParameterDescription(const std::string& param_name) const {
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

std::vector<std::string> GotchaReplaySourceNode::GetParameterNames() const {
    return {
        "fixture_path",
        "emit_watermark",
    };
}

void GotchaReplaySourceNode::Reset() {
    next_record_index_ = 0;
    watermark_emitted_ = false;
    eos_emitted_ = false;
}

void GotchaReplaySourceNode::SetConfig(const GotchaReplaySourceConfig& config) {
    config_ = config;
    Reset();
}

const GotchaReplaySourceConfig& GotchaReplaySourceNode::GetConfig() const noexcept {
    return config_;
}

SarPulseBlockMessage GotchaReplaySourceNode::MakeMessage(const GotchaNormalizedPulseRecord& record) const {
    SarPulseBlockMessage out{};
    out.envelope.sequence_id = record.ordering_key;
    out.envelope.batch_id = record.pass_id;
    out.envelope.aperture_id = record.pulse_block_id;
    out.envelope.pulse_range_start = record.range_bin_start;
    out.envelope.pulse_range_count = record.range_bin_count;
    out.envelope.stream_id = record.stream_id;
    out.envelope.tile_id = static_cast<std::uint32_t>(record.aperture_span_start);
    out.envelope.tile_count = record.aperture_span_count;
    out.envelope.backend_id = record.backend_id;
    out.envelope.backend = record.backend;
    out.envelope.marker = SarFrameMarker::Data;
    out.envelope.synthetic = false;

    out.buffer.buffer_id = record.pulse_block_id;
    out.buffer.device_index = record.backend_id;
    out.buffer.backend = record.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    out.iq_samples = record.iq_samples;
    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);
    return out;
}

SarPulseBlockMessage GotchaReplaySourceNode::MakeControlMessage(SarFrameMarker marker) const {
    SarPulseBlockMessage out{};
    const auto control_sequence_id = static_cast<std::uint64_t>(config_.records.size());
    out.envelope.sequence_id = control_sequence_id;
    out.envelope.batch_id = config_.records.empty() ? 0u : config_.records.front().pass_id;
    out.envelope.aperture_id = control_sequence_id;
    out.envelope.pulse_range_start = control_sequence_id;
    out.envelope.pulse_range_count = 0u;
    out.envelope.stream_id = config_.records.empty() ? 0u : config_.records.front().stream_id;
    out.envelope.tile_id = 0u;
    out.envelope.tile_count = config_.records.empty() ? 0u : config_.records.front().aperture_span_count;
    out.envelope.backend_id = config_.records.empty() ? 0u : config_.records.front().backend_id;
    out.envelope.backend = config_.records.empty() ? SarBackendKind::Host : config_.records.front().backend;
    out.envelope.marker = marker;
    out.envelope.synthetic = false;
    out.buffer.buffer_id = control_sequence_id;
    out.buffer.device_index = out.envelope.backend_id;
    out.buffer.backend = out.envelope.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    out.buffer.byte_count = 0u;
    return out;
}

} // namespace sar
