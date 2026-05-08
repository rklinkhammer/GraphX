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

#include "avionics/nodes/EstimationPipelineNode.hpp"
#include <log4cxx/logger.h>

namespace avionics::estimators {

static auto logger = log4cxx::Logger::getLogger("EstimationPipelineNode");

// ============================================================================
// Constructors
// ============================================================================

EstimationPipelineNode::EstimationPipelineNode()
    : processed_count_(0),
      stage_count_(0),
      // Stage 1: Altitude consensus
      altitude_primary_switches_(0),
      altitude_primary_source_(sensors::SensorClassificationType::UNKNOWN),
      // Stage 2: Velocity consensus (per-axis)
      velocity_axis_switches_{0, 0, 0},
      velocity_primary_source_{sensors::SensorClassificationType::UNKNOWN,
                              sensors::SensorClassificationType::UNKNOWN,
                              sensors::SensorClassificationType::UNKNOWN},
      // Stage 3: Complementary filter
      cf_adaptation_rate_(0.05f),
      cf_minimum_weight_(0.1f),
      cf_use_confidence_weighting_(true),
      cf_source_weights_{1.0f/3, 1.0f/3, 1.0f/3},
      cf_avg_disagreement_{0.0f, 0.0f, 0.0f},
      cf_avg_confidence_{1.0f, 1.0f, 1.0f},
      cf_fused_confidence_(1.0f),
      // Stage 4: Kalman filter
      ekf_process_noise_(0.01f),
      ekf_measurement_noise_(0.1f),
      ekf_time_delta_(0.1f),
      ekf_filtered_altitude_(0.0f),
      ekf_filtered_velocity_(0.0f),
      ekf_measurement_covariance_(0.1f),
      ekf_kalman_gain_(0.5f),
      ekf_last_innovation_(0.0f) {
    
    SetName("EstimationPipelineNode");
    
    // Initialize Kalman filter covariance matrices
    ekf_state_covariance_.setIdentity();
    ekf_state_covariance_(0, 0) = 1.0f;    // Altitude variance
    ekf_state_covariance_(1, 1) = 0.5f;    // Velocity variance
    
    ekf_process_covariance_.setIdentity();
    ekf_process_covariance_ *= ekf_process_noise_;
    
    {
        LOG4CXX_TRACE(logger, "EstimationPipelineNode initialized with consolidated state");
    }
}

EstimationPipelineNode::EstimationPipelineNode(
    std::chrono::microseconds sample_interval,
    int ignore_count)
    : EstimationPipelineNode() {
    // Base constructor handles all initialization
    // These parameters were used by child nodes but are no longer needed
    // in consolidated architecture
    (void)sample_interval;   // suppress unused warning
    (void)ignore_count;       // suppress unused warning
    {
        LOG4CXX_TRACE(logger, "EstimationPipelineNode initialized with factory parameters (deprecated)");
    }
}

// ============================================================================
// Core Transfer Method
// ============================================================================

std::optional<sensors::StateVector> EstimationPipelineNode::Transfer(
    const sensors::StateVector& state,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    
    processed_count_++;
    
    // Working state through all pipeline stages
    sensors::StateVector current_state = state;
    
    // ========================================================================
    // STAGE 1: Altitude Consensus Selection
    // ========================================================================
    ProcessAltitudeConsensus(state, current_state);
    stage_count_++;
    
    // ========================================================================
    // STAGE 2: Velocity Consensus Selection (Per-Axis)
    // ========================================================================
    ProcessVelocityConsensus(current_state, current_state);
    stage_count_++;
    
    // ========================================================================
    // STAGE 3: Complementary Filter (Adaptive Fusion)
    // ========================================================================
    float fused_altitude = ApplyComplementaryFilter(current_state);
    current_state.position.z = fused_altitude;
    current_state.confidence.altitude = cf_fused_confidence_;
    stage_count_++;
    
    // ========================================================================
    // STAGE 4: Extended Kalman Filter (Advanced Filtering)
    // ========================================================================
    float altitude_measurement = current_state.position.z;
    float measurement_confidence = current_state.confidence.altitude;
    ApplyKalmanFilter(altitude_measurement, measurement_confidence);
    
    current_state.position.z = ekf_filtered_altitude_;
    current_state.confidence.altitude = 1.0f - std::min(GetStateUncertainty(), 1.0f);
    stage_count_++;
    
    // ========================================================================
    // Final Output and History Recording
    // ========================================================================
    
    // Record final metrics
    altitude_history_.push_back(current_state.position.z);
    if (altitude_history_.size() > kMaxHistorySize) {
        altitude_history_.pop_front();
    }
    
    velocity_history_.push_back(current_state.velocity.z);
    if (velocity_history_.size() > kMaxHistorySize) {
        velocity_history_.pop_front();
    }
    
    // Use average confidence from all stages
    float final_confidence = (current_state.confidence.altitude +
                            current_state.confidence.velocity +
                            current_state.confidence.gps_position) / 3.0f;
    confidence_history_.push_back(final_confidence);
    if (confidence_history_.size() > kMaxHistorySize) {
        confidence_history_.pop_front();
    }
    
    LOG4CXX_TRACE(logger,
        "Pipeline complete: alt=" << current_state.position.z <<
        " vel=" << current_state.velocity.z <<
        " conf=" << final_confidence <<
        " (stage_count=" << stage_count_ << ")");
    
    // Publish metrics if callback is registered
    if (metrics_callback_) {
        // Event 1: estimation_update (every cycle)
        {
            app::metrics::MetricsEvent event;
            event.event_type = "estimation_update";
            event.source = "EstimationPipelineNode";
            event.timestamp = std::chrono::system_clock::now();
            event.data["altitude_estimate_m"] = std::to_string(current_state.position.z);
            event.data["velocity_vertical_m_s"] = std::to_string(current_state.velocity.z);
            event.data["altitude_confidence"] = std::to_string(current_state.confidence.altitude);
            event.data["velocity_confidence"] = std::to_string(current_state.confidence.velocity);
            event.data["stage_count"] = std::to_string(static_cast<int>(stage_count_));
            event.data["processed_count"] = std::to_string(static_cast<int>(processed_count_));
            metrics_callback_->PublishAsync(event);
        }
        
        // Event 2: estimation_quality (every cycle with filter details)
        {
            app::metrics::MetricsEvent event;
            event.event_type = "estimation_quality";
            event.source = "EstimationPipelineNode";
            event.timestamp = std::chrono::system_clock::now();
            event.data["fused_confidence"] = std::to_string(cf_fused_confidence_);
            event.data["ekf_filtered_altitude_m"] = std::to_string(ekf_filtered_altitude_);
            event.data["ekf_filtered_velocity_m_s"] = std::to_string(ekf_filtered_velocity_);
            event.data["ekf_kalman_gain"] = std::to_string(ekf_kalman_gain_);
            event.data["ekf_last_innovation"] = std::to_string(ekf_last_innovation_);
            event.data["ekf_process_noise"] = std::to_string(ekf_process_noise_);
            event.data["ekf_measurement_noise"] = std::to_string(ekf_measurement_noise_);
            event.data["altitude_switches"] = std::to_string(static_cast<int>(altitude_primary_switches_));
            event.data["velocity_x_switches"] = std::to_string(static_cast<int>(velocity_axis_switches_[0]));
            event.data["velocity_y_switches"] = std::to_string(static_cast<int>(velocity_axis_switches_[1]));
            event.data["velocity_z_switches"] = std::to_string(static_cast<int>(velocity_axis_switches_[2]));
            event.data["complementary_filter_weight_0"] = std::to_string(cf_source_weights_[0]);
            event.data["complementary_filter_weight_1"] = std::to_string(cf_source_weights_[1]);
            event.data["complementary_filter_weight_2"] = std::to_string(cf_source_weights_[2]);
            metrics_callback_->PublishAsync(event);
        }
    }
    
    return current_state;
}

// ============================================================================
// Configuration Methods
// ============================================================================

void EstimationPipelineNode::SetOutlierThreshold(float threshold) {
    // Set outlier threshold for both altitude and velocity voters
    altitude_voter_.SetOutlierThreshold(threshold);
    for (auto& voter : velocity_voters_) {
        voter.SetOutlierThreshold(threshold);
    }
}

void EstimationPipelineNode::SetAltitudeConfidenceWeight(float weight) {
    // Enable/disable confidence weighting in altitude voter
    altitude_voter_.SetConfidenceWeighting(weight > 0.5f);
}

void EstimationPipelineNode::SetVelocityConfidenceWeight(float weight) {
    // Enable/disable confidence weighting in velocity voters
    for (auto& voter : velocity_voters_) {
        voter.SetConfidenceWeighting(weight > 0.5f);
    }
}

void EstimationPipelineNode::EnableAdaptiveWeighting(bool enable) {
    // Enable/disable confidence weighting in complementary filter
    cf_use_confidence_weighting_ = enable;
}

void EstimationPipelineNode::SetProcessNoise(float process_noise) {
    ekf_process_noise_ = std::max(0.0f, process_noise);
    ekf_process_covariance_.setIdentity();
    ekf_process_covariance_ *= ekf_process_noise_;
}

void EstimationPipelineNode::SetMeasurementNoise(float measurement_noise) {
    ekf_measurement_noise_ = std::max(0.0f, measurement_noise);
    ekf_measurement_covariance_ = ekf_measurement_noise_;
}

void EstimationPipelineNode::SetTimeDelta(float dt_seconds) {
    ekf_time_delta_ = std::max(0.001f, dt_seconds);
}

// ============================================================================
// Metrics Access
// ============================================================================

float EstimationPipelineNode::GetAverageConfidence() const {
    if (confidence_history_.empty()) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (float conf : confidence_history_) {
        sum += conf;
    }
    return sum / static_cast<float>(confidence_history_.size());
}

void EstimationPipelineNode::ResetMetrics() {
    // Process counters
    processed_count_ = 0;
    stage_count_ = 0;
    
    // Stage 1: Altitude selector metrics
    altitude_primary_switches_ = 0;
    altitude_primary_source_ = sensors::SensorClassificationType::UNKNOWN;
    altitude_selector_history_.clear();
    altitude_selector_confidence_.clear();
    altitude_disagreement_history_.clear();
    
    // Stage 2: Velocity selector metrics (per-axis)
    for (int axis = 0; axis < 3; ++axis) {
        velocity_axis_switches_[axis] = 0;
        velocity_primary_source_[axis] = sensors::SensorClassificationType::UNKNOWN;
        velocity_selector_history_[axis].clear();
        velocity_selector_confidence_[axis].clear();
        velocity_disagreement_history_[axis].clear();
    }
    
    // Stage 3: Complementary filter metrics
    cf_source_weights_ = {1.0f/3, 1.0f/3, 1.0f/3};
    cf_fused_confidence_ = 1.0f;
    cf_altitude_history_.clear();
    cf_weight_adjustment_history_.clear();
    for (int i = 0; i < 3; ++i) {
        cf_avg_disagreement_[i] = 0.0f;
        cf_avg_confidence_[i] = 1.0f;
        cf_disagreement_history_[i].clear();
        cf_confidence_history_[i].clear();
    }
    
    // Stage 4: Kalman filter metrics and state
    ekf_filtered_altitude_ = 0.0f;
    ekf_filtered_velocity_ = 0.0f;
    ekf_kalman_gain_ = 0.5f;
    ekf_last_innovation_ = 0.0f;
    ekf_state_covariance_.setIdentity();
    ekf_state_covariance_(0, 0) = 1.0f;
    ekf_state_covariance_(1, 1) = 0.5f;
    ekf_altitude_history_.clear();
    ekf_uncertainty_history_.clear();
    ekf_innovation_history_.clear();
    
    // Final output history
    altitude_history_.clear();
    velocity_history_.clear();
    confidence_history_.clear();
    
    LOG4CXX_TRACE(logger, "EstimationPipelineNode metrics and state reset");
}

// ============================================================================
// IConfigurable Implementation
// ============================================================================

void EstimationPipelineNode::Configure(const graph::JsonView& cfg) {
    try {
        if (cfg.Contains("outlier_threshold")) {
            SetOutlierThreshold(cfg.GetFloat("outlier_threshold", 2.5f));
        }
        if (cfg.Contains("altitude_weight")) {
            SetAltitudeConfidenceWeight(cfg.GetFloat("altitude_weight", 1.0f));
        }
        if (cfg.Contains("velocity_weight")) {
            SetVelocityConfidenceWeight(cfg.GetFloat("velocity_weight", 1.0f));
        }
        if (cfg.Contains("adaptive_weighting")) {
            EnableAdaptiveWeighting(cfg.GetBool("adaptive_weighting", true));
        }
        if (cfg.Contains("process_noise")) {
            SetProcessNoise(cfg.GetFloat("process_noise", 0.01f));
        }
        if (cfg.Contains("measurement_noise")) {
            SetMeasurementNoise(cfg.GetFloat("measurement_noise", 0.1f));
        }
        if (cfg.Contains("time_delta")) {
            SetTimeDelta(cfg.GetFloat("time_delta", 0.1f));
        }
    } catch (const std::exception& e) {
        throw graph::ConfigError(std::string("EstimationPipelineNode configuration error: ") + e.what());
    }
}

// ============================================================================
// IDiagnosable Implementation
// ============================================================================

graph::JsonView EstimationPipelineNode::GetDiagnostics() const {
    nlohmann::json diag;
    
    diag["processed_count"] = GetProcessedCount();
    diag["stage_count"] = GetStageCount();
    diag["total_switches"] = GetTotalSwitches();
    diag["fused_confidence"] = GetFusedConfidence();
    diag["average_confidence"] = GetAverageConfidence();
    diag["altitude_history_size"] = GetAltitudeHistory().size();
    diag["velocity_history_size"] = GetVelocityHistory().size();
    diag["confidence_history_size"] = GetConfidenceHistory().size();
    
    // Include diagnostics from consolidated stages
    nlohmann::json altitude_selector_diag;
    altitude_selector_diag["primary_source_switches"] = altitude_primary_switches_;
    altitude_selector_diag["primary_source"] = static_cast<int>(altitude_primary_source_);
    float altitude_avg_disagreement = 0.0f;
    for (float val : altitude_disagreement_history_) {
        altitude_avg_disagreement += val;
    }
    altitude_avg_disagreement = altitude_disagreement_history_.empty() ? 0.0f : altitude_avg_disagreement / altitude_disagreement_history_.size();
    altitude_selector_diag["average_disagreement"] = altitude_avg_disagreement;
    float altitude_avg_conf = 0.0f;
    for (float val : altitude_selector_confidence_) {
        altitude_avg_conf += val;
    }
    altitude_avg_conf = altitude_selector_confidence_.empty() ? 1.0f : altitude_avg_conf / altitude_selector_confidence_.size();
    altitude_selector_diag["average_confidence"] = altitude_avg_conf;
    diag["altitude_selector"] = altitude_selector_diag;
    
    nlohmann::json velocity_selector_diag;
    velocity_selector_diag["x_axis_switches"] = velocity_axis_switches_[0];
    velocity_selector_diag["y_axis_switches"] = velocity_axis_switches_[1];
    velocity_selector_diag["z_axis_switches"] = velocity_axis_switches_[2];
    velocity_selector_diag["average_confidence"] = GetAverageVelocityConfidence();
    diag["velocity_selector"] = velocity_selector_diag;
    
    nlohmann::json complementary_filter_diag;
    complementary_filter_diag["accel_weight"] = cf_source_weights_[0];
    complementary_filter_diag["baro_weight"] = cf_source_weights_[1];
    complementary_filter_diag["gps_weight"] = cf_source_weights_[2];
    complementary_filter_diag["fused_confidence"] = cf_fused_confidence_;
    diag["complementary_filter"] = complementary_filter_diag;
    
    nlohmann::json kalman_filter_diag;
    kalman_filter_diag["filtered_altitude"] = ekf_filtered_altitude_;
    kalman_filter_diag["filtered_velocity"] = ekf_filtered_velocity_;
    kalman_filter_diag["state_uncertainty"] = GetStateUncertainty();
    kalman_filter_diag["kalman_gain"] = ekf_kalman_gain_;
    kalman_filter_diag["last_innovation"] = ekf_last_innovation_;
    diag["kalman_filter"] = kalman_filter_diag;
    
    return graph::JsonView(diag);
}

// ============================================================================
// IParameterized Implementation
// ============================================================================

graph::JsonView EstimationPipelineNode::GetParameters() const {
    nlohmann::json params;
    
    // Get parameters from consolidated stages
    nlohmann::json altitude_selector_params;
    altitude_selector_params["outlier_threshold"] = altitude_voter_.GetOutlierThreshold();
    altitude_selector_params["confidence_weighting"] = altitude_voter_.IsConfidenceWeightingEnabled();
    params["altitude_selector"] = altitude_selector_params;
    
    nlohmann::json velocity_selector_params;
    velocity_selector_params["outlier_threshold"] = velocity_voters_[0].GetOutlierThreshold();
    velocity_selector_params["confidence_weighting"] = velocity_voters_[0].IsConfidenceWeightingEnabled();
    params["velocity_selector"] = velocity_selector_params;
    
    nlohmann::json complementary_filter_params;
    complementary_filter_params["adaptation_rate"] = cf_adaptation_rate_;
    complementary_filter_params["minimum_weight"] = cf_minimum_weight_;
    complementary_filter_params["confidence_weighting"] = cf_use_confidence_weighting_;
    params["complementary_filter"] = complementary_filter_params;
    
    nlohmann::json kalman_filter_params;
    kalman_filter_params["process_noise"] = ekf_process_noise_;
    kalman_filter_params["measurement_noise"] = ekf_measurement_noise_;
    kalman_filter_params["time_delta"] = ekf_time_delta_;
    params["kalman_filter"] = kalman_filter_params;
    
    return graph::JsonView(params);
}

graph::JsonView EstimationPipelineNode::GetParameterDescription(const std::string& param_name) const {
    nlohmann::json desc;
    
    // Top-level parameters
    if (param_name == "outlier_threshold") {
        desc["name"] = "outlier_threshold";
        desc["type"] = "float";
        desc["description"] = "Outlier detection threshold for SensorVoter consensus";
        desc["min"] = 1.0f;
        desc["max"] = 5.0f;
        desc["default"] = 2.5f;
    } else if (param_name == "altitude_weight") {
        desc["name"] = "altitude_weight";
        desc["type"] = "float";
        desc["description"] = "Confidence weight for altitude selection";
        desc["min"] = 0.0f;
        desc["max"] = 2.0f;
        desc["default"] = 1.0f;
    } else if (param_name == "velocity_weight") {
        desc["name"] = "velocity_weight";
        desc["type"] = "float";
        desc["description"] = "Confidence weight for velocity selection";
        desc["min"] = 0.0f;
        desc["max"] = 2.0f;
        desc["default"] = 1.0f;
    } else if (param_name == "adaptive_weighting") {
        desc["name"] = "adaptive_weighting";
        desc["type"] = "bool";
        desc["description"] = "Enable adaptive weighting in complementary filter";
        desc["default"] = true;
    } else if (param_name == "process_noise") {
        desc["name"] = "process_noise";
        desc["type"] = "float";
        desc["description"] = "Process noise variance for Kalman filter";
        desc["min"] = 0.0001f;
        desc["max"] = 1.0f;
        desc["default"] = 0.01f;
    } else if (param_name == "measurement_noise") {
        desc["name"] = "measurement_noise";
        desc["type"] = "float";
        desc["description"] = "Measurement noise variance for Kalman filter";
        desc["min"] = 0.0001f;
        desc["max"] = 1.0f;
        desc["default"] = 0.1f;
    } else if (param_name == "time_delta") {
        desc["name"] = "time_delta";
        desc["type"] = "float";
        desc["description"] = "Time step for Kalman filter (seconds)";
        desc["min"] = 0.001f;
        desc["max"] = 1.0f;
        desc["default"] = 0.1f;
    }
    
    return graph::JsonView(desc);
}

std::vector<std::string> EstimationPipelineNode::GetParameterNames() const {
    return {
        "outlier_threshold",
        "altitude_weight",
        "velocity_weight",
        "adaptive_weighting",
        "process_noise",
        "measurement_noise",
        "time_delta"
    };
}

// ============================================================================
// Port Metadata
// ============================================================================

std::vector<graph::PortMetadata> EstimationPipelineNode::GetInputPortMetadata() const {
    std::vector<graph::PortMetadata> metadata;
    metadata.push_back({
        0,                              // port_index
        "StateVector",                  // payload_type
        "input",                        // direction
        "Input"                         // port_name
    });
    return metadata;
}

std::vector<graph::PortMetadata> EstimationPipelineNode::GetOutputPortMetadata() const {
    std::vector<graph::PortMetadata> metadata;
    metadata.push_back({
        0,                              // port_index
        "StateVector",                  // payload_type
        "output",                       // direction
        "Output"                        // port_name
    });
    return metadata;
}

// ============================================================================
// Helper Methods
// ============================================================================

float EstimationPipelineNode::GetStateUncertainty() const {
    return std::sqrt(std::abs(ekf_state_covariance_(0, 0)));
}

float EstimationPipelineNode::GetAverageVelocityConfidence() const {
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!velocity_selector_confidence_[i].empty()) {
            sum += velocity_selector_confidence_[i].back();
            count++;
        }
    }
    return count > 0 ? sum / count : 1.0f;
}

// ============================================================================
// STAGE 1: Altitude Consensus - Helper Methods
// ============================================================================

void EstimationPipelineNode::ProcessAltitudeConsensus(
    const sensors::StateVector& state,
    sensors::StateVector& output_state) {
    
    // Phase 7: Read per-sensor altitude sources from StateVector instead of creating fake votes
    // The voting already happened in FlightFSMNode - we just use the result
    
    // Store voted altitude for history tracking
    altitude_selector_history_.push_back(state.position.z);
    if (altitude_selector_history_.size() > kMaxHistorySize) {
        altitude_selector_history_.pop_front();
    }
    
    // Store altitude confidence
    altitude_selector_confidence_.push_back(state.confidence.altitude);
    if (altitude_selector_confidence_.size() > kMaxHistorySize) {
        altitude_selector_confidence_.pop_front();
    }
    
    // Calculate disagreement between per-sensor sources and voted result
    float error_accel = std::abs(state.altitude_sources.from_accel - state.position.z);
    float error_baro = std::abs(state.altitude_sources.from_baro - state.position.z);
    float error_gps = std::abs(state.altitude_sources.from_gps - state.position.z);
    float avg_disagreement = (error_accel + error_baro + error_gps) / 3.0f;
    
    altitude_disagreement_history_.push_back(avg_disagreement);
    if (altitude_disagreement_history_.size() > kMaxHistorySize) {
        altitude_disagreement_history_.pop_front();
    }
    
    // Output is already the voted consensus from FlightFSMNode
    output_state.position.z = state.position.z;
    output_state.confidence.altitude = state.confidence.altitude;
    
    LOG4CXX_TRACE(logger,
        "Stage 1 (Altitude Consensus): alt=" << state.position.z <<
        " accel=" << state.altitude_sources.from_accel <<
        " baro=" << state.altitude_sources.from_baro <<
        " gps=" << state.altitude_sources.from_gps <<
        " disagreement=" << avg_disagreement);
}

// ============================================================================
// STAGE 2: Velocity Consensus - Helper Methods
// ============================================================================

float EstimationPipelineNode::VoteVelocityAxis(
    int axis,
    const sensors::StateVector& state) {
    
    // Phase 7: Voting already happened in FlightFSMNode - just use the voted result
    // The StateVector contains per-sensor velocity sources for visibility
    
    float voted_velocity = 0.0f;
    float confidence = 1.0f;
    
    if (axis == 2) {  // Z-axis (vertical)
        voted_velocity = state.velocity.z;
        confidence = state.confidence.velocity;
        
        // Record axis-specific metrics
        if (!velocity_selector_confidence_[axis].empty()) {
            velocity_selector_confidence_[axis].back() = confidence;
        } else {
            velocity_selector_confidence_[axis].push_back(confidence);
        }
        
        // Calculate disagreement
        float error_accel = std::abs(state.velocity_sources.z_from_accel - voted_velocity);
        float error_baro = std::abs(state.velocity_sources.z_from_baro - voted_velocity);
        float error_gps = std::abs(state.velocity_sources.z_from_gps - voted_velocity);
        float avg_disagreement = (error_accel + error_baro + error_gps) / 3.0f;
        
        velocity_disagreement_history_[axis].push_back(avg_disagreement);
        if (velocity_disagreement_history_[axis].size() > kMaxHistorySize) {
            velocity_disagreement_history_[axis].pop_front();
        }
    }
    
    // Update history
    velocity_selector_history_[axis].push_back(voted_velocity);
    if (velocity_selector_history_[axis].size() > kMaxHistorySize) {
        velocity_selector_history_[axis].pop_front();
    }
    
    return voted_velocity;
}

void EstimationPipelineNode::ProcessVelocityConsensus(
    const sensors::StateVector& state,
    sensors::StateVector& output_state) {
    
    // Phase 7: Voting already happened in FlightFSMNode - just use the result
    // Simply copy the voted velocity values from input to output
    
    output_state.velocity.x = state.velocity.x;
    output_state.velocity.y = state.velocity.y;
    output_state.velocity.z = state.velocity.z;
    output_state.confidence.velocity = state.confidence.velocity;
    
    LOG4CXX_TRACE(logger,
        "Stage 2 (Velocity Consensus): vx=" << state.velocity.x <<
        " vy=" << state.velocity.y <<
        " vz=" << state.velocity.z <<
        " accel=" << state.velocity_sources.z_from_accel <<
        " baro=" << state.velocity_sources.z_from_baro <<
        " gps=" << state.velocity_sources.z_from_gps);
}

// ============================================================================
// STAGE 3: Complementary Filter - Helper Methods
// ============================================================================

void EstimationPipelineNode::UpdateDisagreement(size_t source_index, float current_error) {
    if (source_index >= 3) return;
    
    // Add to history
    cf_disagreement_history_[source_index].push_back(current_error);
    if (cf_disagreement_history_[source_index].size() > kMaxHistorySize) {
        cf_disagreement_history_[source_index].pop_front();
    }
    
    // Recalculate average
    float sum = 0.0f;
    for (float val : cf_disagreement_history_[source_index]) {
        sum += val;
    }
    cf_avg_disagreement_[source_index] = cf_disagreement_history_[source_index].empty()
                                             ? 0.0f
                                             : sum / cf_disagreement_history_[source_index].size();
}

void EstimationPipelineNode::UpdateConfidence(size_t source_index, float confidence) {
    if (source_index >= 3) return;
    
    // Add to history
    cf_confidence_history_[source_index].push_back(confidence);
    if (cf_confidence_history_[source_index].size() > kMaxHistorySize) {
        cf_confidence_history_[source_index].pop_front();
    }
    
    // Recalculate average
    float sum = 0.0f;
    for (float val : cf_confidence_history_[source_index]) {
        sum += val;
    }
    cf_avg_confidence_[source_index] = cf_confidence_history_[source_index].empty()
                                           ? 1.0f
                                           : sum / cf_confidence_history_[source_index].size();
}

void EstimationPipelineNode::AdaptWeight(size_t source_index, float current_disagreement) {
    if (source_index >= 3) return;
    
    // Calculate weight adjustment factor
    float disagreement_factor = std::exp(-current_disagreement);
    
    // Apply confidence weighting if enabled
    float adjustment_weight = disagreement_factor;
    if (cf_use_confidence_weighting_) {
        adjustment_weight *= cf_avg_confidence_[source_index];
    }
    
    // Blend with previous weight
    float old_weight = cf_source_weights_[source_index];
    float new_weight = old_weight + cf_adaptation_rate_ * (adjustment_weight - old_weight);
    
    // Apply minimum weight threshold
    new_weight = std::max(new_weight, cf_minimum_weight_);
    cf_source_weights_[source_index] = new_weight;
    
    // Normalize weights to sum to 1.0
    float total_weight = cf_source_weights_[0] + cf_source_weights_[1] + cf_source_weights_[2];
    if (total_weight > 0.0f) {
        cf_source_weights_[0] /= total_weight;
        cf_source_weights_[1] /= total_weight;
        cf_source_weights_[2] /= total_weight;
    }
}

float EstimationPipelineNode::ApplyComplementaryFilter(const sensors::StateVector& state) {
    // Extract altitude and confidences
    float accel_altitude = state.position.z;
    float baro_altitude = state.position.z;
    float gps_altitude = state.position.z;
    
    float accel_conf = state.confidence.altitude;
    float baro_conf = state.confidence.altitude;
    float gps_conf = state.confidence.gps_position;
    
    // Apply weighted fusion
    float fused_altitude = (cf_source_weights_[0] * accel_altitude +
                           cf_source_weights_[1] * baro_altitude +
                           cf_source_weights_[2] * gps_altitude);
    
    // Calculate disagreements
    float accel_disagreement = std::abs(accel_altitude - fused_altitude);
    float baro_disagreement = std::abs(baro_altitude - fused_altitude);
    float gps_disagreement = std::abs(gps_altitude - fused_altitude);
    
    // Update disagreement and confidence tracking
    UpdateDisagreement(ACCEL_INDEX, accel_disagreement);
    UpdateDisagreement(BARO_INDEX, baro_disagreement);
    UpdateDisagreement(GPS_INDEX, gps_disagreement);
    
    UpdateConfidence(ACCEL_INDEX, accel_conf);
    UpdateConfidence(BARO_INDEX, baro_conf);
    UpdateConfidence(GPS_INDEX, gps_conf);
    
    // Adapt weights
    AdaptWeight(ACCEL_INDEX, cf_avg_disagreement_[ACCEL_INDEX]);
    AdaptWeight(BARO_INDEX, cf_avg_disagreement_[BARO_INDEX]);
    AdaptWeight(GPS_INDEX, cf_avg_disagreement_[GPS_INDEX]);
    
    // Calculate fused confidence as weighted average
    cf_fused_confidence_ = (cf_source_weights_[0] * accel_conf +
                           cf_source_weights_[1] * baro_conf +
                           cf_source_weights_[2] * gps_conf);
    
    // Maintain fused altitude history
    cf_altitude_history_.push_back(fused_altitude);
    if (cf_altitude_history_.size() > kMaxHistorySize) {
        cf_altitude_history_.pop_front();
    }
    
    return fused_altitude;
}

// ============================================================================
// STAGE 4: Extended Kalman Filter - Helper Methods
// ============================================================================

void EstimationPipelineNode::PropagateState(float time_delta) {
    // Simple constant velocity motion model:
    // altitude(k+1) = altitude(k) + velocity(k) * dt
    ekf_filtered_altitude_ += ekf_filtered_velocity_ * time_delta;
    // Velocity remains constant in this simple model
}

void EstimationPipelineNode::UpdateStateCovariance(float time_delta) {
    // Simplified covariance update: P = F*P*F^T + Q
    // For constant velocity model: F = [[1, dt], [0, 1]]
    
    ekf_state_covariance_(0, 0) +=
        2.0f * ekf_state_covariance_(0, 1) * time_delta +
        ekf_state_covariance_(1, 1) * time_delta * time_delta +
        ekf_process_covariance_(0, 0);
    
    ekf_state_covariance_(1, 1) += ekf_process_covariance_(1, 1);
    
    // Ensure covariance stays positive
    ekf_state_covariance_(0, 0) = std::max(0.0001f, ekf_state_covariance_(0, 0));
    ekf_state_covariance_(1, 1) = std::max(0.0001f, ekf_state_covariance_(1, 1));
}

void EstimationPipelineNode::UpdateWithMeasurement(
    float altitude_measurement,
    float measurement_confidence) {
    
    // Innovation: difference between measurement and prediction
    ekf_last_innovation_ = altitude_measurement - ekf_filtered_altitude_;
    
    // Adaptive measurement noise based on confidence
    float adaptive_meas_noise = ekf_measurement_covariance_ / std::max(0.1f, measurement_confidence);
    
    // Kalman gain calculation (simplified for scalar case)
    // K = P / (P + R)
    float pred_uncertainty = ekf_state_covariance_(0, 0);
    ekf_kalman_gain_ = pred_uncertainty / (pred_uncertainty + adaptive_meas_noise + 1e-6f);
    
    // Update state estimate
    ekf_filtered_altitude_ += ekf_kalman_gain_ * ekf_last_innovation_;
    
    // Update state covariance (simplified)
    // P = (1 - K) * P
    ekf_state_covariance_(0, 0) *= (1.0f - ekf_kalman_gain_);
}

void EstimationPipelineNode::ApplyKalmanFilter(
    float altitude_measurement,
    float measurement_confidence) {
    
    // Step 1: Propagate state
    PropagateState(ekf_time_delta_);
    
    // Step 2: Propagate uncertainty
    UpdateStateCovariance(ekf_time_delta_);
    
    // Step 3: Update with measurement
    UpdateWithMeasurement(altitude_measurement, measurement_confidence);
    
    // Track history
    ekf_altitude_history_.push_back(ekf_filtered_altitude_);
    if (ekf_altitude_history_.size() > kMaxHistorySize) {
        ekf_altitude_history_.pop_front();
    }
    
    float state_uncertainty = GetStateUncertainty();
    ekf_uncertainty_history_.push_back(state_uncertainty);
    if (ekf_uncertainty_history_.size() > kMaxHistorySize) {
        ekf_uncertainty_history_.pop_front();
    }
    
    ekf_innovation_history_.push_back(ekf_last_innovation_);
    if (ekf_innovation_history_.size() > kMaxHistorySize) {
        ekf_innovation_history_.pop_front();
    }
}

void EstimationPipelineNode::TrimHistory(std::deque<float>& buffer) const {
    while (buffer.size() > kMaxHistorySize) {
        buffer.pop_front();
    }
}

float EstimationPipelineNode::GetVelocityUncertainty() const {
    return std::sqrt(std::abs(ekf_state_covariance_(1, 1)));
}

}  // namespace avionics::estimators
