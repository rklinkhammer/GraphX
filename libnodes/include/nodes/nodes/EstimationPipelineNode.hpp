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

#pragma once

#include <memory>
#include <optional>
#include <deque>
#include "graph/NamedNodes.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "avionics/estimators/SensorVoter.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include "graph/IMetricsCallback.hpp"
#include <array>
#include <Eigen/Dense>

namespace avionics::estimators {

/**
 * @class EstimationPipelineNode
 * @brief Integration node connecting all Phase 6 estimators in a complete fusion pipeline
 *
 * **Architecture:**
 * ```
 * Input StateVector
 *   ↓
 * [SensorVoter (consensus on all sources)]
 *   ↓
 * [AltitudeSelectorNode (altitude consensus)]
 *   ↓
 * [VelocitySelectorNode (velocity consensus)]
 *   ↓
 * [ComplementaryFilterNode (adaptive fusion)]
 *   ↓
 * [ExtendedKalmanFilterNode (advanced filtering)]
 *   ↓
 * Output StateVector (fused estimate with confidence)
 * ```
 *
 * **Key Features:**
 * - All nodes connected in the graph (not just called sequentially)
 * - StateVector passes through each estimator in order
 * - Each stage refines altitude and velocity estimates
 * - Confidence metrics aggregated across pipeline
 * - Metrics collected at each stage for diagnostics
 *
 * **Input Port 0:** Raw StateVector from sensors
 * **Output Port 0:** Fused StateVector with refined estimates
 *
 * **Metrics:**
 * - altitude_history: Final altitude at each step
 * - velocity_history: Final velocity at each step
 * - confidence_history: Final confidence at each step
 * - processing_stage_count: Incremented after each stage
 * - selector_switches_count: Source switches in selectors
 * - filter_disagreements: Cumulative disagreement from filters
 *
 * @see AltitudeSelectorNode
 * @see VelocitySelectorNode
 * @see ComplementaryFilterNode
 * @see ExtendedKalmanFilterNode
 */
class EstimationPipelineNode
    : public graph::NamedInteriorNode<
          graph::TypeList<sensors::StateVector>,
          graph::TypeList<sensors::StateVector>,
          EstimationPipelineNode>,
      public graph::IConfigurable,
      public graph::IDiagnosable,
      public graph::IParameterized,
      public graph::IMetricsCallbackProvider {
public:
    // ========================================================================
    // Constructors & Lifecycle
    // ========================================================================

    /**
     * @brief Default constructor
     *
     * Initializes all integrated estimator nodes with default configurations.
     */
    EstimationPipelineNode();

    /**
     * @brief Constructor with factory parameters
     *
     * Supports NodeFactory instantiation pattern.
     *
     * @param sample_interval Sampling period (passed to child nodes)
     * @param ignore_count Number of samples to ignore (passed to child nodes)
     */
    EstimationPipelineNode(std::chrono::microseconds sample_interval, int ignore_count);

    /**
     * @brief Virtual destructor
     */
    virtual ~EstimationPipelineNode() = default;

    // ========================================================================
    // Core Transfer Method (Port 0 → Port 0)
    // ========================================================================

    /**
     * @brief Process StateVector through complete estimation pipeline
     *
     * Chains all estimators together:
     * 1. SensorVoter consensus on all measurements
     * 2. AltitudeSelectorNode altitude consensus
     * 3. VelocitySelectorNode velocity consensus
     * 4. ComplementaryFilterNode adaptive fusion
     * 5. ExtendedKalmanFilterNode advanced filtering
     *
     * @param state Input StateVector from sensors
     * @return Refined StateVector with fused estimates, or std::nullopt if processing fails
     */
    std::optional<sensors::StateVector> Transfer(
        const sensors::StateVector& state,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    // ========================================================================
    // Configuration Methods
    // ========================================================================

    /**
     * @brief Set outlier threshold for SensorVoter
     *
     * @param threshold Standard deviations to detect outliers
     */
    void SetOutlierThreshold(float threshold);

    /**
     * @brief Set altitude confidence weight for selection
     *
     * @param weight Weight multiplier [0, 1]
     */
    void SetAltitudeConfidenceWeight(float weight);

    /**
     * @brief Set velocity confidence weight for selection
     *
     * @param weight Weight multiplier [0, 1]
     */
    void SetVelocityConfidenceWeight(float weight);

    /**
     * @brief Enable/disable adaptive weighting in ComplementaryFilter
     *
     * @param enable True to adapt weights based on disagreement
     */
    void EnableAdaptiveWeighting(bool enable);

    /**
     * @brief Set process noise for Kalman filter (uncertainty growth)
     *
     * @param process_noise Process noise level [0, 1]
     */
    void SetProcessNoise(float process_noise);

    /**
     * @brief Set measurement noise for Kalman filter (sensor trust)
     *
     * @param measurement_noise Measurement noise level [0, 1]
     */
    void SetMeasurementNoise(float measurement_noise);

    /**
     * @brief Set time delta for filter predictions
     *
     * @param dt_seconds Time step in seconds
     */
    void SetTimeDelta(float dt_seconds);

    // ========================================================================
    // Metrics Access
    // ========================================================================

    /**
     * @brief Get number of samples processed
     */
    size_t GetProcessedCount() const { return processed_count_; }

    /**
     * @brief Get number of processing stages completed
     */
    size_t GetStageCount() const { return stage_count_; }

    /**
     * @brief Get altitude source switches
     */
    size_t GetAltitudeSwitches() const { return altitude_primary_switches_; }

    /**
     * @brief Get total velocity source switches (sum across all axes)
     */
    size_t GetVelocitySwitches() const {
        return velocity_axis_switches_[0] + velocity_axis_switches_[1] + velocity_axis_switches_[2];
    }

    /**
     * @brief Get total source switches across all selectors
     */
    size_t GetTotalSwitches() const {
        return altitude_primary_switches_ + GetVelocitySwitches();
    }

    /**
     * @brief Get fused confidence from complementary filter
     */
    float GetFusedConfidence() const { return cf_fused_confidence_; }

    /**
     * @brief Get average confidence from final stage
     */
    float GetAverageConfidence() const;

    /**
     * @brief Get altitude history (last N samples)
     */
    const std::deque<float>& GetAltitudeHistory() const { return altitude_history_; }

    /**
     * @brief Get velocity history (last N samples)
     */
    const std::deque<float>& GetVelocityHistory() const { return velocity_history_; }

    /**
     * @brief Get confidence history (last N samples)
     */
    const std::deque<float>& GetConfidenceHistory() const { return confidence_history_; }

    /**
     * @brief Reset all metrics
     */
    void ResetMetrics();

    // ========================================================================
    // IConfigurable Implementation
    // ========================================================================

    /**
     * @brief Configure pipeline with JSON parameters
     *
     * Supported parameters:
     * - outlier_threshold (float, default 2.5): Voter threshold for consensus
     * - altitude_weight (float, default 1.0): Altitude confidence weight
     * - velocity_weight (float, default 1.0): Velocity confidence weight
     * - adaptive_weighting (bool, default true): Enable adaptive fusion weighting
     * - process_noise (float, default 0.01): Kalman filter process noise
     * - measurement_noise (float, default 0.1): Kalman filter measurement noise
     * - time_delta (float, default 0.1): Kalman filter time step
     *
     * @param cfg JSON configuration view
     * @throws ConfigError if configuration is invalid
     */
    void Configure(const graph::JsonView& cfg) override;

    // ========================================================================
    // IDiagnosable Implementation
    // ========================================================================

    /**
     * @brief Get diagnostics data as JSON
     *
     * Returns metrics from entire pipeline including:
     * - processed_count: Samples processed
     * - stage_count: Processing stages completed
     * - total_switches: Source switches across selectors
     * - cumulative_disagreement: Filter disagreement accumulation
     * - average_confidence: Final confidence estimate
     * - altitude_history_size: Samples in history
     * - child_diagnostics: Diagnostics from each stage
     *
     * @return JsonView with diagnostics data
     */
    graph::JsonView GetDiagnostics() const override;

    // ========================================================================
    // IParameterized Implementation
    // ========================================================================

    /**
     * @brief Get current parameter values as JSON
     *
     * @return JsonView with current parameter values from all stages
     */
    graph::JsonView GetParameters() const override;

    /**
     * @brief Get parameter description
     *
     * @param param_name Name of parameter
     * @return JsonView with parameter metadata (type, range, default, current)
     * @throws ConfigError if parameter unknown
     */
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;

    /**
     * @brief Get list of all parameter names
     *
     * @return Vector of parameter names
     */
    std::vector<std::string> GetParameterNames() const override;

    // ========================================================================
    // Port Metadata
    // ========================================================================

    /**
     * @brief Provide input port metadata for graph visualization
     */
    virtual std::vector<graph::PortMetadata> GetInputPortMetadata() const override;

    /**
     * @brief Provide output port metadata for graph visualization
     */
    virtual std::vector<graph::PortMetadata> GetOutputPortMetadata() const override;

public:
    // ========================================================================
    // Helper Methods: Stage 1 (Altitude Consensus)
    // ========================================================================
    
    /// Process altitude consensus voting
    void ProcessAltitudeConsensus(
        const sensors::StateVector& state,
        sensors::StateVector& output_state);
    
    // ========================================================================
    // Helper Methods: Stage 2 (Velocity Consensus)
    // ========================================================================
    
    /// Vote on velocity for a single axis
    float VoteVelocityAxis(int axis, const sensors::StateVector& state);
    
    /// Process velocity consensus voting across all axes
    void ProcessVelocityConsensus(
        const sensors::StateVector& state,
        sensors::StateVector& output_state);

    // ========================================================================
    // Helper Methods: Stage 3 (Complementary Filter)
    // ========================================================================
    
    /// Apply complementary filter fusion
    float ApplyComplementaryFilter(const sensors::StateVector& state);
    
    /// Adapt weights based on disagreement
    void AdaptWeight(size_t source_index, float current_disagreement);
    
    /// Update disagreement tracking for a source
    void UpdateDisagreement(size_t source_index, float current_error);
    
    /// Update confidence tracking for a source
    void UpdateConfidence(size_t source_index, float confidence);

    // ========================================================================
    // Helper Methods: Stage 4 (Kalman Filter)
    // ========================================================================
    
    /// Apply Kalman filter update
    void ApplyKalmanFilter(float altitude_measurement, float measurement_confidence);
    
    /// Propagate state estimate using motion model
    void PropagateState(float time_delta);
    
    /// Update state covariance (uncertainty propagation)
    void UpdateStateCovariance(float time_delta);
    
    /// Update filter with measurement
    void UpdateWithMeasurement(float altitude_measurement, float measurement_confidence);
    
    /// Trim history buffer to maximum size
    void TrimHistory(std::deque<float>& buffer) const;
    
    /// Get velocity uncertainty from covariance
    float GetVelocityUncertainty() const;

    /// Get current state uncertainty
    float GetStateUncertainty() const;

    // ========================================================================
    // IMetricsCallbackProvider Implementation
    // ========================================================================

    /**
     * @brief Set the metrics callback for reporting estimation metrics
     * @param callback Pointer to IMetricsCallback interface
     * @return true if callback was successfully set, false if nullptr
     */
    virtual bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
        metrics_callback_ = callback;
        return callback != nullptr;
    }

    /**
     * @brief Check if metrics callback is registered
     * @return true if callback is set, false otherwise
     */
    virtual bool HasMetricsCallback() const noexcept override {
        return metrics_callback_ != nullptr;
    }

    /**
     * @brief Get the registered metrics callback
     * @return Pointer to IMetricsCallback or nullptr
     */
    virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
        return metrics_callback_;
    }

    app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
        app::metrics::NodeMetricsSchema schema;
        schema.node_name = GetName();
        schema.node_type = "processor";
        
        schema.metrics_schema = {
            {"fields", {
                {
                    {"name", "altitude_estimate_m"},
                    {"type", "double"},
                    {"unit", "m"},
                    {"precision", 2},
                    {"description", "Estimated altitude"},
                    {"alert_rule", {
                        {"ok", {0, 10000}},
                        {"warning", {-500, 12000}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "velocity_vertical_m_s"},
                    {"type", "double"},
                    {"unit", "m/s"},
                    {"precision", 2},
                    {"description", "Estimated vertical velocity"},
                    {"alert_rule", {
                        {"ok", {-10, 10}},
                        {"warning", {-20, 20}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "ekf_filtered_altitude_m"},
                    {"type", "double"},
                    {"unit", "m"},
                    {"precision", 2},
                    {"description", "EKF Filtered altitude"},
                    {"alert_rule", {
                        {"ok", {0, 10000}},
                        {"warning", {-500, 12000}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "ekf_filtered_velocity_m_s"},
                    {"type", "double"},
                    {"unit", "m/s"},
                    {"precision", 2},
                    {"description", "EKF Filtered vertical velocity"},
                    {"alert_rule", {
                        {"ok", {-10, 10}},
                        {"warning", {-20, 20}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "filter_status"},
                    {"type", "int"},
                    {"unit", "enum"},
                    {"description", "0=OK, 1=WARN, 2=CRITICAL"},
                    {"alert_rule", {
                        {"ok", {0, 0}},
                        {"warning", {0, 1}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "processed_count"},
                    {"type", "int"},
                    {"unit", "count"},
                    {"description", "Number of filter updates"},
                    {"alert_rule", {
                        {"ok", {0, 1000000}},
                        {"warning", {-100, 2000000}},
                        {"critical", "outside"}
                    }}
                }
            }},
            {"metadata", {
                {"node_type", "processor"},
                {"description", "Estimation pipeline integrating altitude and velocity"},
                {"refresh_rate_hz", 100}
            }}
        };
        
        schema.event_types = {"estimation_update", "estimation_quality"};
        return schema;
    }
    
private:

    // ========================================================================
    // Metrics Callback
    // ========================================================================
    graph::IMetricsCallback* metrics_callback_{nullptr};

    // ========================================================================
    // Process Tracking
    // ========================================================================
    size_t processed_count_ = 0;
    size_t stage_count_ = 0;

    // ========================================================================
    // STAGE 1: Altitude Consensus Selection
    // ========================================================================
    
    /// Core voting algorithm for altitude
    SensorVoter altitude_voter_;
    
    /// History of altitude consensus results from stage 1
    std::deque<float> altitude_selector_history_;
    
    /// History of altitude consensus confidence
    std::deque<float> altitude_selector_confidence_;
    
    /// History of altitude disagreement between sources
    std::deque<float> altitude_disagreement_history_;
    
    /// Counter for primary source switches in altitude voting
    size_t altitude_primary_switches_ = 0;
    
    /// Previous primary altitude source (for switch detection)
    sensors::SensorClassificationType altitude_primary_source_ = sensors::SensorClassificationType::UNKNOWN;

    // ========================================================================
    // STAGE 2: Velocity Consensus Selection (Per-Axis)
    // ========================================================================
    
    /// Three SensorVoter instances (one per axis: X, Y, Z)
    std::array<SensorVoter, 3> velocity_voters_;
    
    /// Per-axis velocity history from stage 2
    std::array<std::deque<float>, 3> velocity_selector_history_;
    
    /// Per-axis velocity confidence history
    std::array<std::deque<float>, 3> velocity_selector_confidence_;
    
    /// Per-axis disagreement history
    std::array<std::deque<float>, 3> velocity_disagreement_history_;
    
    /// Per-axis source switch counters
    std::array<size_t, 3> velocity_axis_switches_{};
    
    /// Previous primary source per axis
    std::array<sensors::SensorClassificationType, 3> velocity_primary_source_;

    // ========================================================================
    // STAGE 3: Complementary Filter (Adaptive Fusion)
    // ========================================================================
    
    /// Adaptation rate for weight changes [0.0, 1.0]
    float cf_adaptation_rate_ = 0.05f;
    
    /// Minimum weight threshold for any source [0.0, 0.5]
    float cf_minimum_weight_ = 0.1f;
    
    /// Enable confidence-weighted fusion
    bool cf_use_confidence_weighting_ = true;
    
    /// Current weights for three sources (accel, baro, GPS)
    std::array<float, 3> cf_source_weights_{1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f};
    
    /// Per-source disagreement history
    std::array<std::deque<float>, 3> cf_disagreement_history_;
    
    /// Average disagreement per source
    std::array<float, 3> cf_avg_disagreement_{0.0f, 0.0f, 0.0f};
    
    /// Per-source confidence history
    std::array<std::deque<float>, 3> cf_confidence_history_;
    
    /// Average confidence per source
    std::array<float, 3> cf_avg_confidence_{1.0f, 1.0f, 1.0f};
    
    /// Fused confidence output from stage 3
    float cf_fused_confidence_ = 1.0f;
    
    /// Fused altitude history from stage 3
    std::deque<float> cf_altitude_history_;
    
    /// Weight adjustment history for diagnostics
    std::deque<float> cf_weight_adjustment_history_;

    // ========================================================================
    // STAGE 4: Extended Kalman Filter
    // ========================================================================
    
    /// Process noise variance (uncertainty growth)
    float ekf_process_noise_ = 0.01f;
    
    /// Measurement noise variance (sensor trust)
    float ekf_measurement_noise_ = 0.1f;
    
    /// Time step for filter predictions (seconds)
    float ekf_time_delta_ = 0.1f;
    
    /// Filtered altitude estimate
    float ekf_filtered_altitude_ = 0.0f;
    
    /// Filtered velocity estimate
    float ekf_filtered_velocity_ = 0.0f;
    
    /// State covariance matrix (2x2: altitude × velocity)
    Eigen::Matrix2f ekf_state_covariance_;
    
    /// Process covariance matrix
    Eigen::Matrix2f ekf_process_covariance_;
    
    /// Measurement covariance
    float ekf_measurement_covariance_ = 0.1f;
    
    /// Kalman gain from last update
    float ekf_kalman_gain_ = 0.5f;
    
    /// Last innovation (measurement residual)
    float ekf_last_innovation_ = 0.0f;
    
    /// Filtered altitude history from stage 4
    std::deque<float> ekf_altitude_history_;
    
    /// State uncertainty history
    std::deque<float> ekf_uncertainty_history_;
    
    /// Innovation history for diagnostics
    std::deque<float> ekf_innovation_history_;

    // ========================================================================
    // Final Output
    // ========================================================================
    
    /// Final altitude output from entire pipeline
    std::deque<float> altitude_history_;
    
    /// Final velocity output from entire pipeline
    std::deque<float> velocity_history_;
    
    /// Final confidence output from entire pipeline
    std::deque<float> confidence_history_;

    // ========================================================================
    // Constants
    // ========================================================================
    static constexpr size_t kMaxHistorySize = 1000;
    static constexpr size_t ACCEL_INDEX = 0;
    static constexpr size_t BARO_INDEX = 1;
    static constexpr size_t GPS_INDEX = 2;
    
    /// Get average velocity confidence across all axes
    float GetAverageVelocityConfidence() const;
};

}  // namespace avionics::estimators
