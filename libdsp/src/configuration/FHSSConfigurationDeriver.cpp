#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include <algorithm>
#include <numeric>
#include <cstring>

namespace dsp::configuration {

// Deterministic JSON serialization with sorted keys
nlohmann::json FHSSConfigurationDeriver::SerializeWithSortedKeys(const nlohmann::json& j) {
    nlohmann::json result;
    
    if (j.is_object()) {
        // Get all keys and sort them
        std::vector<std::string> keys;
        for (const auto& [key, _] : j.items()) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        
        // Rebuild in sorted order
        for (const auto& key : keys) {
            result[key] = SerializeWithSortedKeys(j[key]);
        }
    } else if (j.is_array()) {
        for (const auto& elem : j) {
            result.push_back(SerializeWithSortedKeys(elem));
        }
    } else {
        result = j;
    }
    
    return result;
}

// SourceConfiguration JSON serialization
nlohmann::json SourceConfiguration::to_json() const {
    nlohmann::json j;
    j["messages"] = messages;
    j["iq_center_frequency_hz"] = iq_center_frequency_hz;
    j["iq_offsets"] = iq_offsets;
    j["idle_mode"] = idle_mode;
    j["idle_duration_samples"] = idle_duration_samples;
    j["occupied_bandwidth_hz"] = occupied_bandwidth_hz;
    j["max_abs_cfo_hz"] = max_abs_cfo_hz;
    j["enable_noise"] = enable_noise;
    j["enable_doppler"] = enable_doppler;
    j["enable_multipath"] = enable_multipath;
    j["allow_overlap"] = allow_overlap;
    j["message_id"] = message_id;
    j["transmit_start_sample"] = transmit_start_sample;
    j["frequency_index"] = frequency_index;
    j["value"] = value;
    j["role"] = role;
    
    return FHSSConfigurationDeriver::SerializeWithSortedKeys(j);
}

SourceConfiguration SourceConfiguration::from_json(const nlohmann::json& j) {
    SourceConfiguration cfg;
    cfg.messages = j.at("messages").get<std::vector<std::string>>();
    cfg.iq_center_frequency_hz = j.at("iq_center_frequency_hz").get<double>();
    cfg.iq_offsets = j.at("iq_offsets").get<std::vector<double>>();
    cfg.idle_mode = j.at("idle_mode").get<std::string>();
    cfg.idle_duration_samples = j.at("idle_duration_samples").get<int32_t>();
    cfg.occupied_bandwidth_hz = j.at("occupied_bandwidth_hz").get<double>();
    cfg.max_abs_cfo_hz = j.at("max_abs_cfo_hz").get<double>();
    cfg.enable_noise = j.at("enable_noise").get<bool>();
    cfg.enable_doppler = j.at("enable_doppler").get<bool>();
    cfg.enable_multipath = j.at("enable_multipath").get<bool>();
    cfg.allow_overlap = j.at("allow_overlap").get<bool>();
    cfg.message_id = j.at("message_id").get<std::string>();
    cfg.transmit_start_sample = j.at("transmit_start_sample").get<int32_t>();
    cfg.frequency_index = j.at("frequency_index").get<int32_t>();
    cfg.value = j.at("value").get<std::string>();
    cfg.role = j.at("role").get<std::string>();
    return cfg;
}

// EffectiveConfiguration JSON serialization
nlohmann::json EffectiveConfiguration::to_json() const {
    nlohmann::json j;
    // Authoritative fields
    j["messages"] = messages;
    j["iq_center_frequency_hz"] = iq_center_frequency_hz;
    j["iq_offsets"] = iq_offsets;
    j["idle_mode"] = idle_mode;
    j["idle_duration_samples"] = idle_duration_samples;
    j["occupied_bandwidth_hz"] = occupied_bandwidth_hz;
    j["max_abs_cfo_hz"] = max_abs_cfo_hz;
    j["enable_noise"] = enable_noise;
    j["enable_doppler"] = enable_doppler;
    j["enable_multipath"] = enable_multipath;
    j["allow_overlap"] = allow_overlap;
    j["message_id"] = message_id;
    j["transmit_start_sample"] = transmit_start_sample;
    j["frequency_index"] = frequency_index;
    j["value"] = value;
    j["role"] = role;
    
    // Generated fields
    j["active_frequency_indices_source"] = active_frequency_indices_source;
    j["active_frequency_indices_preamble"] = active_frequency_indices_preamble;
    j["active_frequency_indices_channelizer"] = active_frequency_indices_channelizer;
    j["preamble_pulses"] = preamble_pulses;
    j["rf_copies"] = rf_copies;
    j["impairment_copies"] = impairment_copies;
    j["message_assembler_config"] = message_assembler_config;
    j["pulse_frequency_indices_source"] = pulse_frequency_indices_source;
    j["pulse_frequency_indices_preamble"] = pulse_frequency_indices_preamble;
    j["pulse_frequency_indices_channelizer"] = pulse_frequency_indices_channelizer;
    
    // Metadata
    j["revision"] = revision;
    j["etag"] = etag;
    
    return FHSSConfigurationDeriver::SerializeWithSortedKeys(j);
}

EffectiveConfiguration EffectiveConfiguration::from_json(const nlohmann::json& j) {
    EffectiveConfiguration cfg;
    cfg.messages = j.at("messages").is_null() ? std::vector<std::string>() : j.at("messages").get<std::vector<std::string>>();
    cfg.iq_center_frequency_hz = j.at("iq_center_frequency_hz").get<double>();
    cfg.iq_offsets = j.at("iq_offsets").is_null() ? std::vector<double>() : j.at("iq_offsets").get<std::vector<double>>();
    cfg.idle_mode = j.at("idle_mode").get<std::string>();
    cfg.idle_duration_samples = j.at("idle_duration_samples").get<int32_t>();
    cfg.occupied_bandwidth_hz = j.at("occupied_bandwidth_hz").get<double>();
    cfg.max_abs_cfo_hz = j.at("max_abs_cfo_hz").get<double>();
    cfg.enable_noise = j.at("enable_noise").get<bool>();
    cfg.enable_doppler = j.at("enable_doppler").get<bool>();
    cfg.enable_multipath = j.at("enable_multipath").get<bool>();
    cfg.allow_overlap = j.at("allow_overlap").get<bool>();
    cfg.message_id = j.at("message_id").get<std::string>();
    cfg.transmit_start_sample = j.at("transmit_start_sample").get<int32_t>();
    cfg.frequency_index = j.at("frequency_index").get<int32_t>();
    cfg.value = j.at("value").get<std::string>();
    cfg.role = j.at("role").get<std::string>();
    
    cfg.active_frequency_indices_source = j.at("active_frequency_indices_source").is_null() ? std::vector<int32_t>() : j.at("active_frequency_indices_source").get<std::vector<int32_t>>();
    cfg.active_frequency_indices_preamble = j.at("active_frequency_indices_preamble").is_null() ? std::vector<int32_t>() : j.at("active_frequency_indices_preamble").get<std::vector<int32_t>>();
    cfg.active_frequency_indices_channelizer = j.at("active_frequency_indices_channelizer").is_null() ? std::vector<int32_t>() : j.at("active_frequency_indices_channelizer").get<std::vector<int32_t>>();
    cfg.preamble_pulses = j.at("preamble_pulses").is_null() ? std::vector<std::string>() : j.at("preamble_pulses").get<std::vector<std::string>>();
    cfg.rf_copies = j.at("rf_copies").is_null() ? std::vector<std::string>() : j.at("rf_copies").get<std::vector<std::string>>();
    cfg.impairment_copies = j.at("impairment_copies").is_null() ? std::vector<std::string>() : j.at("impairment_copies").get<std::vector<std::string>>();
    cfg.message_assembler_config = j.at("message_assembler_config").get<std::string>();
    cfg.pulse_frequency_indices_source = j.at("pulse_frequency_indices_source").is_null() ? std::vector<int32_t>() : j.at("pulse_frequency_indices_source").get<std::vector<int32_t>>();
    cfg.pulse_frequency_indices_preamble = j.at("pulse_frequency_indices_preamble").is_null() ? std::vector<int32_t>() : j.at("pulse_frequency_indices_preamble").get<std::vector<int32_t>>();
    cfg.pulse_frequency_indices_channelizer = j.at("pulse_frequency_indices_channelizer").is_null() ? std::vector<int32_t>() : j.at("pulse_frequency_indices_channelizer").get<std::vector<int32_t>>();
    
    cfg.revision = j.at("revision").get<uint64_t>();
    cfg.etag = j.at("etag").get<std::string>();
    
    return cfg;
}

// Deterministic derivation functions
std::vector<int32_t> FHSSConfigurationDeriver::DeriveFrequencyIndices(
    const SourceConfiguration& source,
    const std::string& phase
) {
    (void)phase;  // phase reserved for future phase-specific derivation
    std::vector<int32_t> indices;
    
    // Derive frequency indices based on phase (source/preamble/channelizer)
    // This is deterministic: same input always produces same output
    if (!source.messages.empty() && source.frequency_index >= 0) {
        // Start with base frequency index
        indices.push_back(source.frequency_index);
        
        // Add offsets based on number of messages
        for (size_t i = 1; i < source.messages.size(); ++i) {
            // Deterministic calculation (no randomness)
            int32_t offset = static_cast<int32_t>(i * 2);  // Fixed offset pattern
            indices.push_back(source.frequency_index + offset);
        }
    }
    
    // Sort for determinism
    std::sort(indices.begin(), indices.end());
    
    return indices;
}

std::vector<std::string> FHSSConfigurationDeriver::DerivePreamblePulses(
    const SourceConfiguration& source
) {
    std::vector<std::string> pulses;
    
    if (!source.messages.empty()) {
        // Derive preamble pulses for each message
        for (size_t i = 0; i < source.messages.size(); ++i) {
            // Deterministic pulse pattern based on message index
            std::string pulse = "pulse_msg_" + std::to_string(i) + 
                              "_freq_" + std::to_string(source.frequency_index);
            pulses.push_back(pulse);
        }
    }
    
    return pulses;
}

std::vector<std::string> FHSSConfigurationDeriver::DeriveRfCopies(
    const SourceConfiguration& source
) {
    std::vector<std::string> copies;
    
    if (!source.messages.empty()) {
        // Create RF copies for each message
        for (size_t i = 0; i < source.messages.size(); ++i) {
            std::string copy = "rf_copy_msg_" + std::to_string(i) + 
                             "_bw_" + std::to_string(static_cast<int32_t>(source.occupied_bandwidth_hz));
            copies.push_back(copy);
        }
    }
    
    return copies;
}

std::vector<std::string> FHSSConfigurationDeriver::DeriveImpairmentCopies(
    const SourceConfiguration& source
) {
    std::vector<std::string> copies;
    
    if (source.enable_doppler || source.enable_multipath || source.enable_noise) {
        // Create impairment models
        if (source.enable_doppler) {
            copies.push_back("impairment_doppler");
        }
        if (source.enable_multipath) {
            copies.push_back("impairment_multipath");
        }
        if (source.enable_noise) {
            copies.push_back("impairment_noise");
        }
    }
    
    return copies;
}

std::string FHSSConfigurationDeriver::DeriveMessageAssemblerConfig(
    const SourceConfiguration& source
) {
    // Create deterministic message assembler configuration
    nlohmann::json config;
    config["num_messages"] = source.messages.size();
    config["allow_overlap"] = source.allow_overlap;
    config["start_sample"] = source.transmit_start_sample;
    config["cfo_max_hz"] = source.max_abs_cfo_hz;
    config["idle_mode"] = source.idle_mode;
    config["idle_duration_samples"] = source.idle_duration_samples;
    
    return SerializeWithSortedKeys(config).dump();
}

std::vector<int32_t> FHSSConfigurationDeriver::DerivePulseFrequencyIndices(
    const SourceConfiguration& source,
    const std::string& phase
) {
    // Similar to DeriveFrequencyIndices but specific to pulse phase
    return DeriveFrequencyIndices(source, phase);
}

// Main derivation function
EffectiveConfiguration FHSSConfigurationDeriver::Derive(
    const SourceConfiguration& source,
    uint64_t revision
) {
    EffectiveConfiguration effective;
    
    // Copy authoritative fields
    effective.messages = source.messages;
    effective.iq_center_frequency_hz = source.iq_center_frequency_hz;
    effective.iq_offsets = source.iq_offsets;
    effective.idle_mode = source.idle_mode;
    effective.idle_duration_samples = source.idle_duration_samples;
    effective.occupied_bandwidth_hz = source.occupied_bandwidth_hz;
    effective.max_abs_cfo_hz = source.max_abs_cfo_hz;
    effective.enable_noise = source.enable_noise;
    effective.enable_doppler = source.enable_doppler;
    effective.enable_multipath = source.enable_multipath;
    effective.allow_overlap = source.allow_overlap;
    effective.message_id = source.message_id;
    effective.transmit_start_sample = source.transmit_start_sample;
    effective.frequency_index = source.frequency_index;
    effective.value = source.value;
    effective.role = source.role;
    
    // Derive all 12 generated fields (deterministic)
    effective.active_frequency_indices_source = DeriveFrequencyIndices(source, "source");
    effective.active_frequency_indices_preamble = DeriveFrequencyIndices(source, "preamble");
    effective.active_frequency_indices_channelizer = DeriveFrequencyIndices(source, "channelizer");
    effective.preamble_pulses = DerivePreamblePulses(source);
    effective.rf_copies = DeriveRfCopies(source);
    effective.impairment_copies = DeriveImpairmentCopies(source);
    effective.message_assembler_config = DeriveMessageAssemblerConfig(source);
    effective.pulse_frequency_indices_source = DerivePulseFrequencyIndices(source, "source");
    effective.pulse_frequency_indices_preamble = DerivePulseFrequencyIndices(source, "preamble");
    effective.pulse_frequency_indices_channelizer = DerivePulseFrequencyIndices(source, "channelizer");
    
    // Set metadata
    effective.revision = revision;
    effective.etag = "Rev:" + std::to_string(revision);
    
    return effective;
}

bool FHSSConfigurationDeriver::VerifyDeterminism(
    const SourceConfiguration& source,
    size_t num_iterations
) {
    if (num_iterations < 2) return true;
    
    // Derive multiple times
    std::vector<std::string> outputs;
    for (size_t i = 0; i < num_iterations; ++i) {
        EffectiveConfiguration cfg = Derive(source, 1);
        outputs.push_back(cfg.to_json().dump());
    }
    
    // Compare all outputs to first
    const std::string& first = outputs[0];
    for (size_t i = 1; i < outputs.size(); ++i) {
        if (outputs[i] != first) {
            return false;  // Output differs - not deterministic
        }
    }
    
    return true;  // All outputs identical - deterministic
}

}  // namespace dsp::configuration
