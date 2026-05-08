// MIT License
//
// Copyright (c) 2025 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/**
 * @file AltitudeFusionNode.cpp
 * @brief Implementation of AltitudeFusionNode for altitude fusion
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#include "avionics/nodes/AltitudeFusionNode.hpp"
#include <nlohmann/json.hpp>
#include <log4cxx/logger.h>
#include "config/JsonView.hpp"

namespace avionics {

// Static logger
static auto log = log4cxx::Logger::getLogger("avionics.altitude_fusion_node");

// ===================================================================
// Constructor
// ===================================================================

AltitudeFusionNode::AltitudeFusionNode()
    : previous_timestamp_(0)
{
    SetName("AltitudeFusionNode");
}

// ===================================================================
// Lifecycle
// ===================================================================

bool AltitudeFusionNode::Init()
{
    LOG4CXX_TRACE(log, "Initializing AltitudeFusionNode");

    // Create estimator with current config
    fusion_estimator_ = std::make_unique<estimators::AltitudeFusionEstimator>(
        current_config_.outlier_threshold,
        current_config_.gps_confidence_mult,
        current_config_.max_altitude_jump,
        current_config_.vv_filter_tau);

    // Initialize last valid state to zeros
    last_valid_fusion_state_ = estimators::FusedAltitudeState{
        .altitude_m = 0.0f,
        .vertical_velocity_ms = 0.0f,
        .height_gain_m = 0.0f,
        .height_loss_m = 0.0f,
        .pressure_altitude_m = 0.0f,
        .gps_altitude_m = 0.0f,
        .overall_confidence = 1.0f,
        .primary_source = 2,  // Fused
        .valid = true
    };

    // Reset metrics
    metrics_.frames_processed = 0;
    metrics_.fusion_updates = 0;
    metrics_.outlier_rejections = 0;
    metrics_.avg_fusion_confidence = 1.0f;
    metrics_.primary_source_switches = 0;

    LOG4CXX_TRACE(log, "AltitudeFusionNode initialized successfully");

    return graph::NamedInteriorNode<
        graph::TypeList<sensors::StateVector>,
        graph::TypeList<sensors::StateVector>,
        AltitudeFusionNode>::Init();
}

// ===================================================================
// Configuration
// ===================================================================

void AltitudeFusionNode::Configure(const graph::JsonView& cfg)
{
    (void)cfg;
    // For now, just log that configuration was called
    // Full implementation depends on JsonView API details
    LOG4CXX_TRACE(log, "AltitudeFusionNode::Configure called");
}

std::string AltitudeFusionNode::GetConfiguration() const
{
    nlohmann::json config;
    config["outlier_threshold"] = current_config_.outlier_threshold;
    config["gps_confidence_mult"] = current_config_.gps_confidence_mult;
    config["max_altitude_jump"] = current_config_.max_altitude_jump;
    config["vv_filter_tau"] = current_config_.vv_filter_tau;
    return config.dump(2);
}

graph::JsonView AltitudeFusionNode::GetDiagnostics() const
{
    nlohmann::json diag;
    
    // Processing statistics
    diag["frames_processed"] = metrics_.frames_processed;
    diag["fusion_updates"] = metrics_.fusion_updates;
    diag["outlier_rejections"] = metrics_.outlier_rejections;
    diag["avg_fusion_confidence"] = metrics_.avg_fusion_confidence;
    diag["primary_source_switches"] = metrics_.primary_source_switches;
    
    // Last valid altitude readings
    diag["last_altitude_baro"] = last_valid_fusion_state_.pressure_altitude_m;
    diag["last_altitude_gps"] = last_valid_fusion_state_.gps_altitude_m;
    diag["last_altitude_fused"] = last_valid_fusion_state_.altitude_m;
    diag["last_vertical_velocity"] = last_valid_fusion_state_.vertical_velocity_ms;
    diag["last_height_gain"] = last_valid_fusion_state_.height_gain_m;
    diag["last_height_loss"] = last_valid_fusion_state_.height_loss_m;
    
    // Configuration snapshot
    diag["config_outlier_threshold"] = current_config_.outlier_threshold;
    diag["config_gps_confidence_mult"] = current_config_.gps_confidence_mult;
    diag["config_max_altitude_jump"] = current_config_.max_altitude_jump;
    diag["config_vv_filter_tau"] = current_config_.vv_filter_tau;
    
    return graph::JsonView(diag);
}

// ===================================================================
// Core Processing
// ===================================================================

std::optional<sensors::StateVector> AltitudeFusionNode::Transfer(
    const sensors::StateVector& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>)
{
    // Track frame processing
    metrics_.frames_processed++;

    // Extract altitude readings
    estimators::BarometricAltitudeReading baro;
    estimators::GPSAltitudeReading gps;
    ExtractAltitudeReadings(input, baro, gps);

    // Compute time delta
    float dt = 0.001f;  // Default 1ms if first frame
    if (previous_timestamp_.count() > 0) {
        auto delta_ns = input.timestamp - previous_timestamp_;
        dt = delta_ns.count() / 1e9f;
        
        // Clamp dt to reasonable range [0.5ms, 100ms]
        if (dt < 0.0005f) dt = 0.0005f;
        if (dt > 0.1f) dt = 0.1f;
    }
    previous_timestamp_ = input.timestamp;

    // Perform fusion
    if (!fusion_estimator_) {
        LOG4CXX_WARN(log, "Fusion estimator not initialized");
        return std::nullopt;
    }

    estimators::FusedAltitudeState fusion_state;

    // Handle sensor availability
    bool baro_valid = baro.valid && baro.confidence > 0.1f;
    bool gps_valid = gps.valid && gps.confidence > 0.1f;

    if (baro_valid && gps_valid) {
        // Dual source fusion
        fusion_state = fusion_estimator_->Update(baro, gps, dt);
        metrics_.fusion_updates++;
    } else if (baro_valid) {
        // Barometric only
        fusion_state = fusion_estimator_->UpdateBarometricOnly(baro, dt);
        metrics_.fusion_updates++;
    } else if (gps_valid) {
        // GPS only
        fusion_state = fusion_estimator_->UpdateGPSOnly(gps, dt);
        metrics_.fusion_updates++;
    } else {
        // Both invalid - use last valid state
        //LOG4CXX_WARN(log, "Both altitude sources invalid, using cached state");
        fusion_state = last_valid_fusion_state_;
        metrics_.outlier_rejections++;
    }

    // Update moving average confidence
    metrics_.avg_fusion_confidence = 
        0.95f * metrics_.avg_fusion_confidence + 
        0.05f * fusion_state.overall_confidence;

    // Cache valid state
    if (fusion_state.valid) {
        last_valid_fusion_state_ = fusion_state;
    }

    // Build output
    sensors::StateVector output = input;
    EnrichStateVector(output, fusion_state);

    // Publish metrics if callback is registered
    if (metrics_callback_) {
        LOG4CXX_TRACE(log, "Publishing altitude fusion metrics");
        // Event 1: altitude_fusion_quality (every cycle)
        {
            app::metrics::MetricsEvent event;
            event.event_type = "altitude_fusion_quality";
            event.source = "AltitudeFusionNode";
            event.timestamp = std::chrono::system_clock::now();
            event.data["fused_altitude_m"] = std::to_string(fusion_state.altitude_m);
            event.data["baro_altitude_m"] = std::to_string(fusion_state.pressure_altitude_m);
            event.data["gps_altitude_m"] = std::to_string(fusion_state.gps_altitude_m);
            event.data["vertical_velocity_m_s"] = std::to_string(fusion_state.vertical_velocity_ms);
            event.data["overall_confidence"] = std::to_string(fusion_state.overall_confidence);
            event.data["height_gain_m"] = std::to_string(fusion_state.height_gain_m);
            event.data["height_loss_m"] = std::to_string(fusion_state.height_loss_m);
            event.data["primary_source"] = std::to_string(static_cast<int>(fusion_state.primary_source));
            event.data["valid"] = std::to_string(fusion_state.valid);
            event.data["frames_processed"] = std::to_string(static_cast<int>(metrics_.frames_processed));
            metrics_callback_->PublishAsync(event);
        }
        
        // Event 2: outlier_detected (if outlier was rejected)
        if (!fusion_state.valid || metrics_.outlier_rejections > 0) {
            app::metrics::MetricsEvent event;
            event.event_type = "outlier_detected";
            event.source = "AltitudeFusionNode";
            event.timestamp = std::chrono::system_clock::now();
            event.data["outlier_count"] = std::to_string(static_cast<int>(metrics_.outlier_rejections));
            event.data["avg_fusion_confidence"] = std::to_string(metrics_.avg_fusion_confidence);
            event.data["fusion_updates"] = std::to_string(static_cast<int>(metrics_.fusion_updates));
            event.data["primary_source_switches"] = std::to_string(static_cast<int>(metrics_.primary_source_switches));
            metrics_callback_->PublishAsync(event);
        }
    }

    return output;
}

// ===================================================================
// Private Helpers
// ===================================================================

void AltitudeFusionNode::ExtractAltitudeReadings(
    const sensors::StateVector& input,
    estimators::BarometricAltitudeReading& baro_out,
    estimators::GPSAltitudeReading& gps_out)
{
    // Extract barometric altitude from position.z
    baro_out.altitude_m = input.position.z;
    baro_out.vertical_velocity_ms = input.velocity.z;
    baro_out.valid = (input.position.z > -500.0f && input.position.z < 10000.0f);
    
    // Estimate barometer confidence based on validity and recent updates
    // In production, this would come from BarometricSensorAdapter health status
    baro_out.confidence = baro_out.valid ? 0.95f : 0.0f;

    // Extract GPS altitude
    gps_out.altitude_m = input.position.z;  // GPS altitude is position.z when fix is valid
    gps_out.horizontal_dop = input.horizontal_accuracy > 0 ? input.horizontal_accuracy : 10.0f;
    gps_out.vertical_dop = input.vertical_accuracy > 0 ? input.vertical_accuracy : (gps_out.horizontal_dop * 1.5f);
    
    // GPS fix quality: convert boolean to enum-like value
    // 0=no fix, 1=standard GPS
    gps_out.fix_quality = input.gps_fix_valid ? 1 : 0;
    
    gps_out.satellite_count = input.num_satellites;
    gps_out.valid = (input.gps_fix_valid && 
                     input.position.z > -500.0f && 
                     input.position.z < 10000.0f);
    
    // GPS confidence based on fix quality and accuracy
    if (!input.gps_fix_valid) {
        // No fix
        gps_out.confidence = 0.0f;
    } else if (input.num_satellites >= 10) {
        // Good satellite count: medium-high confidence
        gps_out.confidence = std::max(0.1f, 0.75f - static_cast<float>(input.horizontal_accuracy * 0.01));
    } else if (input.num_satellites >= 5) {
        // Moderate satellite count
        gps_out.confidence = std::max(0.1f, 0.65f - static_cast<float>(input.horizontal_accuracy * 0.01));
    } else {
        // Low satellite count: low confidence
        gps_out.confidence = std::max(0.0f, 0.4f - static_cast<float>(input.horizontal_accuracy * 0.01));
    }

    LOG4CXX_TRACE(log, "Extracted readings: "
                  << "baro(" << baro_out.altitude_m << "m, conf=" << baro_out.confidence << ") "
                  << "gps(" << gps_out.altitude_m << "m, fix=" << static_cast<int>(gps_out.fix_quality)
                  << ", sats=" << static_cast<int>(gps_out.satellite_count)
                  << ", conf=" << gps_out.confidence << ")");
}

void AltitudeFusionNode::EnrichStateVector(
    sensors::StateVector& output,
    const estimators::FusedAltitudeState& fusion_state)
{
    // Update altitude/position in z-component
    output.position.z = fusion_state.altitude_m;
    output.velocity.z = fusion_state.vertical_velocity_ms;
    
    // Update altitude confidence
    output.confidence.altitude = fusion_state.overall_confidence;

    LOG4CXX_TRACE(log, "Enriched output: "
                  << "altitude=" << output.position.z << "m, "
                  << "vv=" << output.velocity.z << "m/s, "
                  << "confidence=" << output.confidence.altitude);
}

}  // namespace avionics
