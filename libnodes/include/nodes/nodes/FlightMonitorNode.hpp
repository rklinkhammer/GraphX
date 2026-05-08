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

#include "graph/NamedNodes.hpp"
#include "avionics/config/FlightPhaseClassifier.hpp"
#include "avionics/config/FeedbackParameterComputer.hpp"
#include "avionics/messages/AvionicsMessages.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "graph/IMetricsCallback.hpp"
#include <optional>
#include <cstdint>

namespace avionics {

/**
 * @class FlightMonitorNode
 * @brief Production node that monitors flight phases and outputs adaptive control parameters.
 *
 * An InteriorNode that combines:
 * 1. FlightPhaseClassifier: Detects flight phase from StateVector input
 * 2. FeedbackParameterComputer: Maps phase to control parameters
 *
 * Template Parameters:
 * - StateVector: sensors::StateVector (flight state data)
 * - PhaseAdaptiveFeedbackMsg: PhaseAdaptiveFeedbackMsg (phase + parameters)
 *
 * Architecture:
 * - Input port: StateVector at 50 Hz (from estimators)
 * - Output port: PhaseAdaptiveFeedbackMsg at 10 Hz (decimated 5:1)
 * - Processing: Phase classification + parameter computation every sample
 * - Output: Emitted every 5th sample (50/5 = 10 Hz)
 *
 * Features:
 * - Continuous phase detection with hysteresis
 * - Smooth parameter blending during transitions
 * - Decimation to reduce downstream processing load
 * - State-preserving across Transfer() calls
 */
class FlightMonitorNode : public graph::NamedInteriorNode<
    graph::TypeList<sensors::StateVector>,
    graph::TypeList<PhaseAdaptiveFeedbackMsg>,
    FlightMonitorNode>,
    public graph::IMetricsCallbackProvider {
public:
    /**
     * @brief Constructor
     * 
     * Initializes the phase classifier and parameter computer.
     * 
     * @param node_name Name of this node in the execution graph (default: "FlightMonitor")
     */
    explicit FlightMonitorNode(const std::string& node_name = "FlightMonitor")
        : classifier_(),
          computer_(),
          decimation_counter_(0),
          sample_count_(0),
          output_count_(0),
          last_reported_phase_(FlightPhaseClassifier::FlightPhase::Rail),
          phase_transition_timestamp_us_(0) {
        SetName(node_name);
    }

    /**
     * @brief Destructor
     */
    virtual ~FlightMonitorNode() = default;

    /**
     * @brief Process one StateVector sample and optionally output feedback message.
     *
     * This is the main processing method called at 50 Hz by the framework.
     *
     * Processing:
     * 1. Classify flight phase from input StateVector
     * 2. Compute adaptive parameters for current phase
     * 3. Track decimation counter (output every 5th sample)
     * 4. Return optional output message
     *
     * @param input Input state vector from estimators
     * @param in_port Input port selector (always 0 for single input)
     * @param out_port Output port selector (always 0 for single output)
     * @return Optional output message (set every 5th call, empty otherwise)
     */
    std::optional<PhaseAdaptiveFeedbackMsg> Transfer(
        const sensors::StateVector& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    /**
     * @brief Get current flight phase
     * @return Current FlightPhase
     */
    FlightPhaseClassifier::FlightPhase GetCurrentPhase() const {
        return classifier_.GetCurrentPhase();
    }

    /**
     * @brief Get previous flight phase
     * @return Previous FlightPhase (before last transition)
     */
    FlightPhaseClassifier::FlightPhase GetPreviousPhase() const {
        return classifier_.GetPreviousPhase();
    }

    /**
     * @brief Get last phase transition info
     * @return PhaseTransition struct with transition metadata
     */
    const FlightPhaseClassifier::PhaseTransition& GetLastTransition() const {
        return classifier_.GetLastTransition();
    }

    /**
     * @brief Get reference to the phase classifier
     * 
     * Allows external configuration of phase detection thresholds.
     * @return Reference to FlightPhaseClassifier
     */
    FlightPhaseClassifier& GetClassifier() { return classifier_; }
    const FlightPhaseClassifier& GetClassifier() const { return classifier_; }

    /**
     * @brief Get reference to the parameter computer
     * 
     * Allows external configuration of control parameters.
     * @return Reference to FeedbackParameterComputer
     */
    FeedbackParameterComputer& GetComputer() { return computer_; }
    const FeedbackParameterComputer& GetComputer() const { return computer_; }

    /**
     * @brief Reset all internal state
     * 
     * Resets phase classifier and decimation counter.
     * Useful for restarting a mission.
     */
    void Reset();

    /**
     * @brief Get the decimation ratio (input Hz / output Hz)
     * @return Decimation ratio (should be 5 for 50 Hz input, 10 Hz output)
     */
    static constexpr uint32_t GetDecimationRatio() { return 5; }

    /**
     * @brief Get input frequency (Hz)
     * @return Expected input frequency (50 Hz)
     */
    static constexpr uint32_t GetInputFrequencyHz() { return 50; }

    /**
     * @brief Get output frequency (Hz)
     * @return Expected output frequency (10 Hz)
     */
    static constexpr uint32_t GetOutputFrequencyHz() { 
        return GetInputFrequencyHz() / GetDecimationRatio(); 
    }

    /**
     * @brief Get sample count (for debugging)
     * @return Total number of 50 Hz samples processed
     */
    uint64_t GetSampleCount() const { return sample_count_; }

    /**
     * @brief Get output count (for debugging)
     * @return Number of 10 Hz messages emitted
     */
    uint64_t GetOutputCount() const { return output_count_; }

    // ========== IMetricsCallbackProvider Interface ==========

    /**
     * @brief Set the metrics callback for async event publishing
     *
     * Called by MetricsPublisher during AutoDiscoverMetricsNodes().
     * The callback is used to publish phase transition events.
     *
     * @param callback Raw pointer to IMetricsCallback implementation
     * @return true if callback was set successfully, false otherwise
     *
     * Thread-Safety: Safe to call from any thread (stores pointer)
     */
    bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override;

    /**
     * @brief Check if a metrics callback has been set
     *
     * @return true if callback is set and not null
     *
     * Thread-Safety: Safe to call from any thread (reads pointer)
     */
    bool HasMetricsCallback() const noexcept override;

    /**
     * @brief Get the current metrics callback
     *
     * @return Raw pointer to IMetricsCallback, or nullptr if not set
     *
     * Thread-Safety: Safe to call from any thread (reads pointer)
     */
    graph::IMetricsCallback* GetMetricsCallback() const noexcept override;

        app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
        app::metrics::NodeMetricsSchema schema;

        schema.node_name = GetName();
        schema.node_type = "processor";
        schema.event_types = {"phase_transition"};
        schema.metrics_schema = {
            {"fields", {
                {
                    {"name", "previous_phase"},
                    {"type", "string"},
                    {"description", "Previous flight phase before last transition"}
                },
                {
                    {"name", "current_phase"},
                    {"type", "string"},
                    {"description", "Current flight phase"}
                },
                {
                    {"name", "timestamp_us"},
                    {"type", "int"},
                    {"unit", "microseconds"},
                    {"description", "Time spent in current phase"},
                    {"alert_rule", {
                        {"ok", {0, 3600000000}},
                        {"warning", {0, 7200000000}},
                        {"critical", "outside"}
                    }}
                }
            }},
            {"metadata", {
                {"node_type", "processor"},
                {"description", "Flight phase monitor and adaptive parameter node"},
                {"refresh_rate_hz", 10},
                {"critical_metrics", {"current_phase", "is_phase_transition"}},
                {"trend_metrics", {"time_in_phase_us", "phase_count"}},
                {"alarm_conditions", {
                    "is_phase_transition == true",
                    "current_phase != previous_phase"
                }}
            }}
        };
            
        return schema;
    }
    
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

private:
    // ===== Component Members =====
    FlightPhaseClassifier classifier_;
    FeedbackParameterComputer computer_;

    // ===== Decimation State =====
    uint32_t decimation_counter_;  ///< Counts samples (0 to 4)
    uint64_t sample_count_;         ///< Total 50 Hz samples processed
    uint64_t output_count_;         ///< Total 10 Hz messages emitted

    // ===== Transition Tracking =====
    FlightPhaseClassifier::FlightPhase last_reported_phase_;
    uint64_t phase_transition_timestamp_us_;

    // ===== Metrics Callback =====
    graph::IMetricsCallback* metrics_callback_ = nullptr;

    /**
     * @brief Internal method to build output message
     * 
     * Creates an output message with current state information.
     *
     * @param input Input state vector
     * @return Constructed message
     */
    PhaseAdaptiveFeedbackMsg BuildOutputMessage(
        const sensors::StateVector& input
    );

    /**
     * @brief Check if we should emit output (every Nth sample)
     * 
     * @return true if this sample should generate output (decimation_counter_ == 0)
     */
    bool ShouldEmitOutput() const {
        return (decimation_counter_ % GetDecimationRatio()) == 0;
    }
};

}  // namespace avionics


