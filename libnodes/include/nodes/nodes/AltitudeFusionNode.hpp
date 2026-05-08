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
 * @file AltitudeFusionNode.hpp
 * @brief Altitude fusion graph node combining barometric and GPS altitude
 *
 * AltitudeFusionNode is a production graph node that refines altitude estimates
 * from FlightFSMNode using advanced multi-source fusion. It ingests StateVector
 * messages, extracts barometric and GPS altitude readings, performs fusion with
 * outlier rejection, and produces enriched StateVector output with cumulative
 * height gain/loss tracking.
 *
 * Architecture:
 * - Inherits from NamedInteriorNode for compile-time type safety and runtime
 *   node identification
 * - Single input port (0): Receives StateVector from FlightFSMNode
 * - Single output port (0): Produces refined StateVector to downstream nodes
 * - Embedded AltitudeFusionEstimator for fusion algorithm
 * - Metrics collection for confidence tracking and diagnostics
 *
 * Data Flow:
 * 1. Input: StateVector(altitude_m, gps_altitude_m, temperature_c, timestamp_us)
 * 2. Extract: BarometricAltitudeReading, GPSAltitudeReading
 * 3. Fuse: Call AltitudeFusionEstimator::Update()
 * 4. Enrich: Add height_gain_m, height_loss_m, altitude_confidence, altitude_source
 * 5. Output: Enhanced StateVector
 *
 * Integration Pattern:
 *   FlightFSMNode(port 0) → AddEdge → AltitudeFusionNode(port 0 in)
 *   AltitudeFusionNode(port 0 out) → AddEdge → FlightLoggerNode(port 0)
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <cmath>

#include "graph/NamedNodes.hpp"
#include "graph/Message.hpp"
#include "sensor/SensorBasicTypes.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "avionics/estimators/AltitudeFusionEstimator.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include "graph/IMetricsCallback.hpp"
#include <nlohmann/json.hpp>

namespace avionics {

/**
 * @class AltitudeFusionNode
 * @brief Multi-source altitude fusion processing node
 *
 * Transforms StateVector input by refining altitude estimates through
 * fusion of barometric and GPS sources. Provides outlier rejection,
 * vertical velocity filtering, and cumulative height tracking.
 */
class AltitudeFusionNode :
    public graph::NamedInteriorNode<
        graph::TypeList<sensors::StateVector>,
        graph::TypeList<sensors::StateVector>,
        AltitudeFusionNode>,
    public graph::IConfigurable,
    public graph::IDiagnosable,
    public graph::IMetricsCallbackProvider
{
public:
    // =========================================================================
    // Port Specifications
    // =========================================================================
    
    static constexpr char kInputPort[] = "Input";
    static constexpr char kOutputPort[] = "Output";

    /**
     * Port metadata specification for compile-time type safety and
     * runtime reflection. Two ports: input and output, both StateVector.
     */
    using Ports = std::tuple<
        graph::PortSpec<
            0, sensors::StateVector, graph::PortDirection::Input, kInputPort,
            graph::PayloadList<sensors::StateVector>>,
        graph::PortSpec<
            0, sensors::StateVector, graph::PortDirection::Output, kOutputPort,
            graph::PayloadList<sensors::StateVector>>
    >;

    // =========================================================================
    // Metrics Structure
    // =========================================================================

    /**
     * @struct Metrics
     * @brief Runtime metrics for altitude fusion diagnostics
     *
     * Tracks frame throughput, fusion quality, outlier rejection,
     * and source selection statistics.
     */
    struct Metrics {
        uint64_t frames_processed = 0;           ///< Total input frames processed
        uint64_t fusion_updates = 0;             ///< Successful fusion computations
        uint64_t outlier_rejections = 0;         ///< Inputs rejected as outliers
        float avg_fusion_confidence = 0.0f;      ///< EMA of fusion confidence [0,1]
        uint8_t primary_source_switches = 0;     ///< Transitions between sources
    };

    // =========================================================================
    // Lifecycle and Configuration
    // =========================================================================

    /**
     * @brief Construct altitude fusion node with default configuration
     *
     * Default fusion parameters:
     * - Outlier threshold: 2.0 sigma
     * - GPS confidence multiplier: 0.8 (lower than barometer)
     * - Max altitude jump: 50 meters
     * - Vertical velocity filter tau: 0.318 seconds (0.5 Hz cutoff)
     */
    AltitudeFusionNode();

    /**
     * @brief Virtual destructor for safe polymorphism
     */
    virtual ~AltitudeFusionNode() = default;

    /**
     * @brief Initialize the fusion node
     *
     * Creates AltitudeFusionEstimator with current configuration.
     * Called by GraphManager before Start().
     *
     * @return true if initialization successful, false otherwise
     */
    virtual bool Init() override;

    /**
     * @brief Configure from JSON (IConfigurable interface)
     *
     * Applies configuration parameters to the embedded estimator:
     * {
     *     "outlier_threshold": 2.0,
     *     "gps_confidence_mult": 0.8,
     *     "max_altitude_jump": 50.0,
     *     "vv_filter_tau": 0.318
     * }
     *
     * @param cfg JSON configuration view
     * @throws graph::ConfigError if configuration fails
     */
    virtual void Configure(const graph::JsonView& cfg) override;

    /**
     * @brief Get current configuration as JSON string
     *
     * @return Configuration as JSON string
     */
    std::string GetConfiguration() const;

    /**
     * @brief Get diagnostics and metrics as JSON (IDiagnosable interface)
     *
     * Returns current fusion state and performance metrics:
     * {
     *     "frames_processed": <count>,
     *     "fusion_updates": <count>,
     *     "outlier_rejections": <count>,
     *     "avg_fusion_confidence": <0.0-1.0>,
     *     "primary_source_switches": <count>,
     *     "last_altitude_baro": <meters>,
     *     "last_altitude_gps": <meters>,
     *     "last_altitude_fused": <meters>
     * }
     */
    virtual graph::JsonView GetDiagnostics() const override;

    // =========================================================================
    // Core Processing (from InteriorNode interface)
    // =========================================================================

    /**
     * @brief Transform StateVector through altitude fusion
     *
     * Main processing method called by edge thread for each input message.
     * Extracts altitude readings from input, performs fusion using the
     * embedded AltitudeFusionEstimator, and enriches output StateVector
     * with fusion results.
     *
     * Algorithm:
     * 1. Validate input StateVector
     * 2. Extract barometric and GPS altitude readings
     * 3. Compute time delta since last frame
     * 4. Call fusion_estimator_->Update()
     * 5. Enrich output with fusion state (height gain/loss, confidence)
     * 6. Track metrics
     * 7. Return enriched output or nullopt if invalid
     *
     * @param input Input StateVector from FlightFSMNode
     * @param [in] input_port Input port index (always 0)
     * @param [in] output_port Output port index (always 0)
     * @return Enriched StateVector, or std::nullopt if processing failed
     */
    virtual std::optional<sensors::StateVector> Transfer(
        const sensors::StateVector& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    // =========================================================================
    // Metrics and Diagnostics
    // =========================================================================

    /**
     * @brief Get runtime metrics
     *
     * Returns reference to internal metrics structure with frame counts,
     * fusion quality statistics, and outlier counts. For diagnostics and
     * performance analysis.
     *
     * @return Const reference to Metrics struct
     */
    const Metrics& GetMetrics() const { return metrics_; }

    // =========================================================================
    // IMetricsCallbackProvider Implementation
    // =========================================================================

    /**
     * @brief Set the metrics callback for reporting altitude fusion metrics
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
                    {"name", "altitude_fused"},
                    {"type", "double"},
                    {"unit", "m"},
                    {"precision", 2},
                    {"description", "Fused altitude estimate"},
                    {"alert_rule", {
                        {"ok", {0.0, 10000.0}},
                        {"warning", {-500.0, 12000.0}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "confidence"},
                    {"type", "double"},
                    {"unit", "percent"},
                    {"precision", 1},
                    {"description", "Fusion confidence level"},
                    {"alert_rule", {
                        {"ok", {75.0, 100.0}},
                        {"warning", {50.0, 100.0}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "source_quality_baro"},
                    {"type", "double"},
                    {"unit", "percent"},
                    {"precision", 1},
                    {"description", "Barometer data quality"},
                    {"alert_rule", {
                        {"ok", {70.0, 100.0}},
                        {"warning", {50.0, 100.0}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "source_quality_gps"},
                    {"type", "double"},
                    {"unit", "percent"},
                    {"precision", 1},
                    {"description", "GPS altitude quality"},
                    {"alert_rule", {
                        {"ok", {70.0, 100.0}},
                        {"warning", {50.0, 100.0}},
                        {"critical", "outside"}
                    }}
                },
                {   {"name", "outlier_count"},
                    {"type", "int"},
                    {"unit", "count"},
                    {"description", "Number of outlier rejections"},
                    {"alert_rule", {
                        {"ok", {0.0, 100.0}},
                        {"warning", {0.0, 500.0}},
                        {"critical", "outside"}
                    }}
                },
                {   {"name", "primary_source_switches"},
                    {"type", "int"},
                    {"unit", "count"},
                    {"description", "Number of primary source switches"},
                    {"alert_rule", {
                        {"ok", {0.0, 50.0}},
                        {"warning", {0.0, 200.0}},
                        {"critical", "outside"}
                    }}
                },
                {
                    {"name", "fusion_updates"},
                    {"type", "int"},
                    {"unit", "count"},
                    {"description", "Number of fusion operations"},
                    {"alert_rule", {
                        {"ok", {0.0, 1000000.0}},
                        {"warning", {-100.0, 2000000.0}},
                        {"critical", "outside"}
                    }}
                },
                { 
                    {"name", "frames_processed"},
                    {"type", "int"},
                    {"unit", "count"},
                    {"description", "Number of frames processed"},
                    {"alert_rule", {
                        {"ok", {0.0, 1000000.0}},
                        {"warning", {-100.0, 2000000.0}},
                        {"critical", "outside"}
                    }}
                }
            }},
            {"metadata", {
                {"node_type", "processor"},
                {"description", "Altitude fusion from multiple sensors"},
                {"refresh_rate_hz", 50},
                {"critical_metrics", {"confidence", "source_quality_baro"}}
            }}
        };
        
        schema.event_types = {"altitude_fusion_quality", "outlier_detected"};
        
        return schema;
    }
private:
    // =========================================================================
    // Metrics Callback
    // =========================================================================
    graph::IMetricsCallback* metrics_callback_{nullptr};

    // =========================================================================
    // Private Members
    // =========================================================================

    /**
     * Embedded altitude fusion estimator performing dual-source fusion
     * with outlier detection, vertical velocity filtering, and height tracking
     */
    std::unique_ptr<estimators::AltitudeFusionEstimator> fusion_estimator_;

    /**
     * Runtime metrics (frame count, fusion updates, outlier rejections, etc.)
     */
    Metrics metrics_;

    /**
     * Previous timestamp for delta time computation (nanoseconds)
     * Used to compute dt for continuous time filtering
     */
    std::chrono::nanoseconds previous_timestamp_;

    /**
     * Last valid fusion state for fallback if current update fails
     * Prevents NaN/Inf propagation to output
     */
    estimators::FusedAltitudeState last_valid_fusion_state_;

    /**
     * Configuration parameters (stored for GetConfiguration())
     */
    struct Config {
        float outlier_threshold = 2.0f;
        float gps_confidence_mult = 0.8f;
        float max_altitude_jump = 50.0f;
        float vv_filter_tau = 0.318f;
    } current_config_;

    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Extract altitude readings from input StateVector
     *
     * Populates barometric and GPS readings from StateVector fields.
     * Sets confidence values based on sensor health indicators.
     *
     * @param [in] input Input StateVector
     * @param [out] baro_out Barometric altitude reading
     * @param [out] gps_out GPS altitude reading
     */
    void ExtractAltitudeReadings(
        const sensors::StateVector& input,
        estimators::BarometricAltitudeReading& baro_out,
        estimators::GPSAltitudeReading& gps_out);

    /**
     * @brief Enrich output StateVector with fusion results
     *
     * Updates StateVector fields with refined altitude and adds new fields:
     * - altitude_m: Fused altitude estimate
     * - vertical_velocity_ms: Filtered vertical velocity
     * - height_gain_m: Cumulative height above starting altitude
     * - height_loss_m: Cumulative descent from peaks
     * - altitude_confidence: Fusion confidence [0, 1]
     * - altitude_source: 0=Barometer, 1=GPS, 2=Fused
     *
     * @param [in,out] output StateVector to enrich (modified in place)
     * @param [in] fusion_state Fusion results from estimator
     */
    void EnrichStateVector(
        sensors::StateVector& output,
        const estimators::FusedAltitudeState& fusion_state);
};

}  // namespace avionics

