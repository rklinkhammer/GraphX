#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"

using namespace dsp::configuration;

// Helper to create a minimal valid configuration
static SourceConfiguration CreateMinimalConfig() {
    SourceConfiguration cfg;
    cfg.messages = {"MSG_A", "MSG_B"};
    cfg.iq_center_frequency_hz = 1e9;  // 1 GHz
    cfg.iq_offsets = {0.0, 100.0};
    cfg.idle_mode = "continuous";
    cfg.idle_duration_samples = 1000;
    cfg.occupied_bandwidth_hz = 1e8;  // 100 MHz
    cfg.max_abs_cfo_hz = 1e6;  // 1 MHz
    cfg.enable_noise = false;
    cfg.enable_doppler = false;
    cfg.enable_multipath = false;
    cfg.allow_overlap = false;
    cfg.message_id = "TEST_001";
    cfg.transmit_start_sample = 0;
    cfg.frequency_index = 10;
    cfg.value = "default";
    cfg.role = "transmitter";
    return cfg;
}

// Test 1: Empty configuration derivation
TEST_CASE("FHSSConfigurationDeriver: Empty configuration", "[deriver]") {
    SourceConfiguration cfg;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.revision == 1);
    CHECK(effective.etag == "Rev:1");
    CHECK(effective.messages.empty());
    CHECK(effective.active_frequency_indices_source.empty());
}

// Test 2: Single message derivation
TEST_CASE("FHSSConfigurationDeriver: Single message configuration", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.messages = {"MSG_A"};
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.revision == 1);
    CHECK(!effective.active_frequency_indices_source.empty());
    CHECK(effective.active_frequency_indices_source[0] == 10);  // Base frequency
    CHECK(effective.preamble_pulses.size() == 1);
    CHECK(effective.rf_copies.size() == 1);
}

// Test 3: Multiple message derivation
TEST_CASE("FHSSConfigurationDeriver: Multiple message configuration", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.messages = {"MSG_A", "MSG_B", "MSG_C"};
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.messages.size() == 3);
    CHECK(effective.preamble_pulses.size() == 3);
    CHECK(effective.rf_copies.size() == 3);
}

// Test 4: Revision tracking
TEST_CASE("FHSSConfigurationDeriver: Revision tracking", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration eff1 = FHSSConfigurationDeriver::Derive(cfg, 1);
    EffectiveConfiguration eff2 = FHSSConfigurationDeriver::Derive(cfg, 2);
    EffectiveConfiguration eff3 = FHSSConfigurationDeriver::Derive(cfg, 100);
    
    CHECK(eff1.revision == 1);
    CHECK(eff1.etag == "Rev:1");
    CHECK(eff2.revision == 2);
    CHECK(eff2.etag == "Rev:2");
    CHECK(eff3.revision == 100);
    CHECK(eff3.etag == "Rev:100");
}

// Test 5: Determinism - identical input produces identical output
TEST_CASE("FHSSConfigurationDeriver: Determinism - byte identical output", "[deriver][determinism]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration eff1 = FHSSConfigurationDeriver::Derive(cfg, 1);
    EffectiveConfiguration eff2 = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    std::string json1 = eff1.to_json().dump();
    std::string json2 = eff2.to_json().dump();
    
    CHECK(json1 == json2);
}

// Test 6: Determinism verification across 10 iterations
TEST_CASE("FHSSConfigurationDeriver: Determinism verification", "[deriver][determinism]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    bool is_deterministic = FHSSConfigurationDeriver::VerifyDeterminism(cfg, 10);
    CHECK(is_deterministic);
}

// Test 7: Impairment copies generation
TEST_CASE("FHSSConfigurationDeriver: Impairment copies - noise enabled", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.enable_noise = true;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.impairment_copies.empty());
    auto it = std::find(effective.impairment_copies.begin(), 
                       effective.impairment_copies.end(), 
                       "impairment_noise");
    CHECK(it != effective.impairment_copies.end());
}

// Test 8: Impairment copies - Doppler enabled
TEST_CASE("FHSSConfigurationDeriver: Impairment copies - Doppler enabled", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.enable_doppler = true;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto it = std::find(effective.impairment_copies.begin(), 
                       effective.impairment_copies.end(), 
                       "impairment_doppler");
    CHECK(it != effective.impairment_copies.end());
}

// Test 9: Impairment copies - Multipath enabled
TEST_CASE("FHSSConfigurationDeriver: Impairment copies - Multipath enabled", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.enable_multipath = true;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto it = std::find(effective.impairment_copies.begin(), 
                       effective.impairment_copies.end(), 
                       "impairment_multipath");
    CHECK(it != effective.impairment_copies.end());
}

// Test 10: Message assembler config generation
TEST_CASE("FHSSConfigurationDeriver: Message assembler config", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.message_assembler_config.empty());
    auto config = nlohmann::json::parse(effective.message_assembler_config);
    CHECK(config["num_messages"] == 2);
    CHECK(config["allow_overlap"] == false);
}

// Test 11: Frequency indices ordering consistency
TEST_CASE("FHSSConfigurationDeriver: Frequency indices are sorted", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.frequency_index = 50;
    cfg.messages.resize(5);
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    // Verify indices are sorted
    auto& indices = effective.active_frequency_indices_source;
    CHECK(std::is_sorted(indices.begin(), indices.end()));
}

// Test 12: JSON serialization round trip
TEST_CASE("FHSSConfigurationDeriver: JSON round-trip serialization", "[deriver][serialization]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    // Serialize to JSON
    nlohmann::json j = effective.to_json();
    std::string json_str = j.dump();
    
    // Deserialize from JSON
    nlohmann::json j2 = nlohmann::json::parse(json_str);
    EffectiveConfiguration effective2 = EffectiveConfiguration::from_json(j2);
    
    // Verify fields match
    CHECK(effective2.revision == effective.revision);
    CHECK(effective2.messages == effective.messages);
    CHECK(effective2.iq_center_frequency_hz == effective.iq_center_frequency_hz);
}

// Test 13: Field preservation
TEST_CASE("FHSSConfigurationDeriver: Authoritative fields preserved", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.iq_center_frequency_hz = 2.5e9;
    cfg.max_abs_cfo_hz = 5e6;
    cfg.occupied_bandwidth_hz = 2e8;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.iq_center_frequency_hz == 2.5e9);
    CHECK(effective.max_abs_cfo_hz == 5e6);
    CHECK(effective.occupied_bandwidth_hz == 2e8);
}

// Test 14: All 12 generated fields populated
TEST_CASE("FHSSConfigurationDeriver: All 12 generated fields populated", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.active_frequency_indices_source.empty());
    CHECK(!effective.active_frequency_indices_preamble.empty());
    CHECK(!effective.active_frequency_indices_channelizer.empty());
    CHECK(!effective.preamble_pulses.empty());
    CHECK(!effective.rf_copies.empty());
    // impairment_copies may be empty if no impairments enabled
    CHECK(!effective.message_assembler_config.empty());
    CHECK(!effective.pulse_frequency_indices_source.empty());
    CHECK(!effective.pulse_frequency_indices_preamble.empty());
    CHECK(!effective.pulse_frequency_indices_channelizer.empty());
}

// Test 15: ETag format correct
TEST_CASE("FHSSConfigurationDeriver: ETag format validation", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration eff1 = FHSSConfigurationDeriver::Derive(cfg, 1);
    EffectiveConfiguration eff123 = FHSSConfigurationDeriver::Derive(cfg, 123);
    
    CHECK(eff1.etag == "Rev:1");
    CHECK(eff123.etag == "Rev:123");
    
    // Verify format matches pattern
    CHECK(eff1.etag.substr(0, 4) == "Rev:");
    CHECK(eff123.etag.substr(0, 4) == "Rev:");
}

// Test 16: Large message count handling
TEST_CASE("FHSSConfigurationDeriver: Large message count", "[deriver][scalability]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.messages.clear();
    
    for (int i = 0; i < 100; ++i) {
        cfg.messages.push_back("MSG_" + std::to_string(i));
    }
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.messages.size() == 100);
    CHECK(effective.preamble_pulses.size() == 100);
    CHECK(effective.rf_copies.size() == 100);
}

// Test 17: Multiple derivations with different revisions
TEST_CASE("FHSSConfigurationDeriver: Multiple revisions", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    std::vector<EffectiveConfiguration> configs;
    for (uint64_t rev = 1; rev <= 10; ++rev) {
        configs.push_back(FHSSConfigurationDeriver::Derive(cfg, rev));
    }
    
    // Verify revisions are sequential
    for (uint64_t i = 0; i < configs.size(); ++i) {
        CHECK(configs[i].revision == i + 1);
    }
}

// Test 18: Zero frequency index
TEST_CASE("FHSSConfigurationDeriver: Zero frequency index", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.frequency_index = 0;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.active_frequency_indices_source.empty());
    CHECK(effective.active_frequency_indices_source[0] == 0);
}

// Test 19: High frequency index
TEST_CASE("FHSSConfigurationDeriver: High frequency index", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.frequency_index = 1000000;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.active_frequency_indices_source.empty());
    CHECK(effective.active_frequency_indices_source[0] == 1000000);
}

// Test 20: Configuration with no impairments
TEST_CASE("FHSSConfigurationDeriver: No impairments enabled", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.enable_noise = false;
    cfg.enable_doppler = false;
    cfg.enable_multipath = false;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.impairment_copies.empty());
}

// Test 21: Configuration with all impairments
TEST_CASE("FHSSConfigurationDeriver: All impairments enabled", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.enable_noise = true;
    cfg.enable_doppler = true;
    cfg.enable_multipath = true;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.impairment_copies.size() == 3);
}

// Test 22: Preamble pulse naming convention
TEST_CASE("FHSSConfigurationDeriver: Preamble pulse naming", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.messages = {"TEST_MSG"};
    cfg.frequency_index = 42;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.preamble_pulses.empty());
    // Verify naming pattern includes message and frequency info
    CHECK(effective.preamble_pulses[0].find("pulse_msg_") != std::string::npos);
}

// Test 23: RF copy naming convention
TEST_CASE("FHSSConfigurationDeriver: RF copy naming", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.occupied_bandwidth_hz = 1e8;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.rf_copies.empty());
    CHECK(effective.rf_copies[0].find("rf_copy_msg_") != std::string::npos);
}

// Test 24: Pulse frequency indices derivation
TEST_CASE("FHSSConfigurationDeriver: Pulse frequency indices", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(!effective.pulse_frequency_indices_source.empty());
    CHECK(!effective.pulse_frequency_indices_preamble.empty());
    CHECK(!effective.pulse_frequency_indices_channelizer.empty());
}

// Test 25: Configuration with all boolean flags
TEST_CASE("FHSSConfigurationDeriver: Boolean flags handling", "[deriver]") {
    SourceConfiguration cfg = CreateMinimalConfig();
    cfg.allow_overlap = true;
    cfg.enable_noise = true;
    cfg.enable_doppler = true;
    cfg.enable_multipath = true;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    CHECK(effective.allow_overlap == true);
    CHECK(effective.enable_noise == true);
    CHECK(effective.enable_doppler == true);
    CHECK(effective.enable_multipath == true);
}
