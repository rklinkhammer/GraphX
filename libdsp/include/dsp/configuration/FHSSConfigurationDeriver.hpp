#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace dsp::configuration {

/// @brief Authoritative configuration provided by user (18 fields)
struct SourceConfiguration {
    std::vector<std::string> messages;
    double iq_center_frequency_hz = 0.0;
    std::vector<double> iq_offsets;
    std::string idle_mode;
    int32_t idle_duration_samples = 0;
    double occupied_bandwidth_hz = 0.0;
    double max_abs_cfo_hz = 0.0;
    bool enable_noise = false;
    bool enable_doppler = false;
    bool enable_multipath = false;
    bool allow_overlap = false;
    std::string message_id;
    int32_t transmit_start_sample = 0;
    int32_t frequency_index = 0;
    std::string value;
    std::string role;

    // JSON serialization for determinism
    nlohmann::json to_json() const;
    static SourceConfiguration from_json(const nlohmann::json& j);
};

/// @brief Effective configuration with derived fields (12 generated fields)
struct EffectiveConfiguration {
    // Authoritative fields (inherited from source)
    std::vector<std::string> messages;
    double iq_center_frequency_hz = 0.0;
    std::vector<double> iq_offsets;
    std::string idle_mode;
    int32_t idle_duration_samples = 0;
    double occupied_bandwidth_hz = 0.0;
    double max_abs_cfo_hz = 0.0;
    bool enable_noise = false;
    bool enable_doppler = false;
    bool enable_multipath = false;
    bool allow_overlap = false;
    std::string message_id;
    int32_t transmit_start_sample = 0;
    int32_t frequency_index = 0;
    std::string value;
    std::string role;

    // Generated fields (computed by deriver)
    std::vector<int32_t> active_frequency_indices_source;
    std::vector<int32_t> active_frequency_indices_preamble;
    std::vector<int32_t> active_frequency_indices_channelizer;
    std::vector<std::string> preamble_pulses;  // Serialized pulse data
    std::vector<std::string> rf_copies;         // Serialized RF copy data
    std::vector<std::string> impairment_copies; // Serialized impairment data
    std::string message_assembler_config;       // Serialized config
    std::vector<int32_t> pulse_frequency_indices_source;
    std::vector<int32_t> pulse_frequency_indices_preamble;
    std::vector<int32_t> pulse_frequency_indices_channelizer;

    // Metadata
    uint64_t revision = 0;  // Monotonically increasing revision number
    std::string etag;   // Format: "Rev:<revision>" for If-Match preconditions

    nlohmann::json to_json() const;
    static EffectiveConfiguration from_json(const nlohmann::json& j);
};

/// @brief FHSSConfigurationDeriver - Deterministically derives effective configuration from source
/// 
/// Key Properties:
/// - Deterministic: Same input always produces byte-identical output
/// - All 12 generated fields computed from 18 authoritative fields
/// - Validation deferred to FHSSCrossNodeValidator
/// - Fixed key ordering (no RANDU-dependent iteration)
/// - Stable floating-point operations (no epsilon-based branching)
class FHSSConfigurationDeriver {
public:
    /// @brief Derive effective configuration from source
    /// @param source Authoritative user-provided configuration
    /// @param revision Current revision number (used for ETag)
    /// @return EffectiveConfiguration with all 12 fields derived
    /// @throw std::exception if input is malformed (structural check only, no validation)
    static EffectiveConfiguration Derive(
        const SourceConfiguration& source,
        uint64_t revision = 1
    );

    /// @brief Verify determinism: Same input produces identical output
    /// @param source Configuration to verify
    /// @param num_iterations Number of derivations to produce
    /// @return true if all outputs are byte-identical
    static bool VerifyDeterminism(
        const SourceConfiguration& source,
        size_t num_iterations = 10
    );

    // Serialization helpers (deterministic JSON with sorted keys)
    static nlohmann::json SerializeWithSortedKeys(const nlohmann::json& j);

private:
    // Derivation helpers (private, deterministic)
    static std::vector<int32_t> DeriveFrequencyIndices(
        const SourceConfiguration& source,
        const std::string& phase  // "source", "preamble", or "channelizer"
    );

    static std::vector<std::string> DerivePreamblePulses(
        const SourceConfiguration& source
    );

    static std::vector<std::string> DeriveRfCopies(
        const SourceConfiguration& source
    );

    static std::vector<std::string> DeriveImpairmentCopies(
        const SourceConfiguration& source
    );

    static std::string DeriveMessageAssemblerConfig(
        const SourceConfiguration& source
    );

    static std::vector<int32_t> DerivePulseFrequencyIndices(
        const SourceConfiguration& source,
        const std::string& phase
    );
};

}  // namespace dsp::configuration
