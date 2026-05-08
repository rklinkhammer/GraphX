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
 * @file FlightFSMNode.hpp
 * @brief Flight state machine node for GraphX
 *
 * Provides a reusable FlightFSMNode class that demonstrates:
 * - Multi-input node with unified message queue (MergeFn pattern)
 * - Message decoding based on SensorPayload discriminant
 * - Per-port router pattern for sensor type dispatch
 * - Configuration-based initialization
 * - Metrics collection and reporting
 *
 * Architecture:
 * - 5 input ports all receive Message type (CommonInput)
 * - Single merged queue (inherited from MergeFn base)
 * - Single processing thread decodes Message.payload (SensorPayload) internally
 * - Per-port routers handle type-specific sensor updates
 *
 * @author Robert Klinkhammer
 * @date 2025-01-03
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include "graph/Nodes.hpp"
#include "graph/Message.hpp"
#include "graph/CompletionSignal.hpp"
#include "sensor/SensorBasicTypes.hpp"
#include "sensor/SensorDataRouter.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "graph/IConfigurable.hpp"
#include "config/JsonView.hpp"
#include "avionics/config/FlightFSMNodeConfig.hpp"
#include "config/FilterFactory.hpp"
#include "graph/IMetricsCallback.hpp"
#include "avionics/estimators/EstimatorBase.hpp"
#include "avionics/estimators/AltitudeEstimator.hpp"
#include "avionics/estimators/VelocityEstimator.hpp"
#include "avionics/estimators/StateVectorAggregator.hpp"
#include "avionics/estimators/AngularRateIntegrator.hpp"
#include <nlohmann/json.hpp>

namespace avionics {

    /**
     * Multi-input flight state machine node using MergeFn pattern.
     * 
     * Architecture:
     * - Inherits from MergeNode5 (transitional to MergeFn<Message, StateVector>)
     * - 5 input ports: Accel, Gyro, Mag, Baro, GPS (all Message type)
     * - Unified merge queue: All inputs feed single FIFO queue
     * - Single thread: Dequeues messages, decodes SensorPayload, dispatches to routers
     * - Type dispatch: Message.payload contains SensorPayload discriminant
     * 
     * Message Flow:
     * 1. Source nodes enqueue Message (CommonInput type) on input ports 0-4
     * 2. All messages feed merge_queue (no per-port queues, no polling)
     * 3. MergeFn thread dequeues Message
     * 4. Process() examines message.payload (SensorPayload variant)
     * 5. Router dispatches to sensor-specific handlers
     * 6. State updates applied, output generated
     */
    class FlightFSMNode : public graph::MergeNode<5, ::graph::message::Message, sensors::StateVector, FlightFSMNode>,
    public graph::IConfigurable,
    public graph::IDiagnosable,
    public graph::IMetricsCallbackProvider {
    
    public:
        static constexpr char kAccelPort[] = "Accel";
        static constexpr char kGyroPort[]  = "Gyro";
        static constexpr char kMagPort[]   = "Mag";
        static constexpr char kBaroPort[]  = "Baro";
        static constexpr char kGPSPort[]   = "GPS";
        static constexpr char kOutputPort[] = "output";

        FlightFSMNode() : MergeNode<5, ::graph::message::Message, sensors::StateVector, FlightFSMNode>() {
            SetName("FlightFSMNode");
        }

        using Ports = std::tuple<
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kAccelPort,
                graph::PayloadList<sensors::AccelerometerData>>,
            graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Input, kGyroPort,
                graph::PayloadList<sensors::GyroscopeData>>,
            graph::PortSpec<2, ::graph::message::Message, graph::PortDirection::Input, kMagPort,
                graph::PayloadList<sensors::MagnetometerData>>,
            graph::PortSpec<3, ::graph::message::Message, graph::PortDirection::Input, kBaroPort,
                graph::PayloadList<sensors::BarometricData>>,
            graph::PortSpec<4, ::graph::message::Message, graph::PortDirection::Input, kGPSPort,
                graph::PayloadList<sensors::GPSPositionData>>,
            graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutputPort,
                graph::PayloadList<sensors::StateVector>>
            >;

        virtual ~FlightFSMNode() = default;
        
        /**
         * Configure this node from JSON.
         * 
         * Applies FlightFSMNodeConfig:
         * - Sets flight mode
         * - Initializes filter from config
         * - Allocates state buffer
         * 
         * @param config JSON view containing configuration
         * @throws graph::ConfigError if configuration is invalid
         */
        void Configure(const graph::JsonView& config_json) override {
            using namespace graph;
            
            // Parse configuration
            try {
                config_ = ConfigParser<FlightFSMNodeConfig>::Parse(config_json);
                
                // Log configuration
                std::cout << "FlightFSMNode configured:" << std::endl
                         << "  Flight Mode: " << config_.flight_mode << std::endl
                         << "  State Buffer Size: " << config_.state_buffer_size << std::endl;
                
                // Create filter based on config
                CreateFilter();
                
                // Initialize state buffer
                InitializeStateBuffer();
                
            } catch (const ConfigError& e) {
                std::cerr << "Configuration error: " << e.what() << std::endl;
                throw;
            }
        }
        
        /**
         * Get the configured flight FSM node configuration.
         */
        const graph::FlightFSMNodeConfig& GetConfig() const {
            return config_;
        }
        
        /**
         * Check if filter is configured and of a specific type.
         */
        bool HasFilter(graph::FilterConfig::Type type) const {
            return config_.filter.IsType(type);
        }
        
        /**
         * Get diagnostics and runtime metrics as JSON.
         * Implements IDiagnosable interface.
         * 
         * Returns JSON with:
         * - frames_processed: Number of messages processed
         * - estimators_active: Whether altitude/velocity estimators are running
         * - current_state: Current flight phase
         * - altitude_variance: Current altitude estimate variance
         * - velocity_variance: Current velocity estimate variance
         * - magnetometer_heading: Latest heading from magnetometer
         * - gps_satellites: Number of satellites in view
         * - gps_fix_valid: Whether GPS fix is valid
         */
        graph::JsonView GetDiagnostics() const override {
            nlohmann::json diag;
            
            // Frame counting and basic stats
            diag["frames_processed"] = 0;  // TODO: Add frame counter if needed
            diag["estimators_active"] = (altitude_estimator_ != nullptr && velocity_estimator_ != nullptr);
            
            // Magnetometer diagnostics
            diag["magnetometer_heading_rad"] = magnetometer_heading_rad_;
            diag["mag_confidence"] = magnetometer_confidence_;
            
            // GPS diagnostics
            diag["gps_num_satellites"] = gps_num_satellites_;
            diag["gps_fix_valid"] = gps_fix_valid_;
            diag["gps_confidence"] = gps_confidence_;
            diag["gps_latitude"] = gps_latitude_;
            diag["gps_longitude"] = gps_longitude_;
            diag["gps_horizontal_accuracy"] = gps_horizontal_accuracy_;
            diag["gps_vertical_accuracy"] = gps_vertical_accuracy_;
            
            // Output decimation
            diag["output_decimation_counter"] = output_decimation_counter_;
            diag["output_decimation_factor"] = OUTPUT_DECIMATION_FACTOR;
            
            return graph::JsonView(diag);
        }
        
        bool Init() override
        {
            // Create estimators with MVP config
            altitude_estimator_ = std::make_unique<estimators::AltitudeEstimator>();
            velocity_estimator_ = std::make_unique<estimators::VelocityEstimator>();
            angular_rate_integrator_ = std::make_unique<estimators::AngularRateIntegrator>();
            aggregator_ = std::make_unique<estimators::StateVectorAggregator>();
            
            // Initialize with ground state (0 m, 0 m/s)
            estimators::EstimatorConfig config;
            config.sample_rate_hz = 100.0f;
            config.confidence_initial = 0.5f;
            config.confidence_min = 0.0f;
            config.confidence_max = 1.0f;
            
            altitude_estimator_->Initialize(config, 0.0f);
            velocity_estimator_->Initialize(config, 0.0f);
            
            // MergeFn Pattern: Per-Port Router Registration
            // Register handlers for accel, gyro, mag, baro, and GPS ports
            
            router_port0_.RegisterHandler<sensors::AccelerometerData>(
                [this](const auto &d)
                { 
                    this->handle_accel(d);
                });

            router_port1_.RegisterHandler<sensors::GyroscopeData>(
                [this](const auto &d)
                { 
                    this->handle_gyro(d);
                });

            router_port2_.RegisterHandler<sensors::MagnetometerData>(
                [this](const auto &d)
                { 
                    this->handle_magnetometer(d);
                });

            router_port3_.RegisterHandler<sensors::BarometricData>(
                [this](const auto &d)
                { 
                    this->handle_baro(d);
                });

            router_port4_.RegisterHandler<sensors::GPSPositionData>(
                [this](const auto &d)
                { 
                    this->handle_gps(d);
                });

            //Optional: Order messages by sensor timestamp (deterministic processing)

            SetInputComparator([](const ::graph::message::Message &a, const ::graph::message::Message &b) {
                const auto *payload_a = a.try_get<sensors::SensorPayload>();
                const auto *payload_b = b.try_get<sensors::SensorPayload>();
                assert(payload_a && payload_b && "Messages must contain SensorPayload");
            
                auto timestamp_a = std::visit([](const auto &sample) { return sample.timestamp; }, *payload_a);
                auto timestamp_b = std::visit([](const auto &sample) { return sample.timestamp; }, *payload_b);
            
                return timestamp_a < timestamp_b;
            });

            return MergeNode<5, ::graph::message::Message, sensors::StateVector, FlightFSMNode>::Init();
        }

        std::optional<sensors::StateVector> Process(const ::graph::message::Message& input, std::integral_constant<std::size_t, 0> = {}) override {
            /**
             * MergeFn Pattern: CommonInput Message Decoding
             * 
             * Input: Message (CommonInput type containing SensorPayload)
             * Processing:
             * 1. Extract SensorPayload from Message (contains type discriminant)
             * 2. Dispatch through per-port routers based on SensorPayload variant type
             * 3. Routers trigger handle_accel/handle_baro for MVP (altitude + velocity)
             * 4. Accel handler generates decimated StateVector output (50 Hz)
             * 
             * Key Design:
             * - All 5 input ports deliver the same Message type (CommonInput)
             * - Type decoding happens here in Process(), not at framework level
             * - Per-port routers handle type-specific dispatch
             * - Estimators update internally; accel handler triggers output
             */
            
             LOG4CXX_TRACE(log4cxx::Logger::getLogger("avionics.FlightFSMNode"),
                          "FlightFSMNode processing input message.");
            
            // Extract SensorPayload from Message
            const auto *sensor_payload = input.try_get<sensors::SensorPayload>();
            assert(sensor_payload && "Message must contain SensorPayload");

            // Dispatch through per-port routers to update estimators
            // Each router will call the appropriate handle_*() callback based on data type
            router_port0_.Dispatch(*sensor_payload); // Accelerometer router → handle_accel()
            router_port1_.Dispatch(*sensor_payload); // Gyroscope router → handle_gyro()
            router_port2_.Dispatch(*sensor_payload); // Magnetometer router → handle_magnetometer()
            router_port3_.Dispatch(*sensor_payload); // Barometer router → handle_baro()
            router_port4_.Dispatch(*sensor_payload); // GPS router → handle_gps()

            // Output is generated in handle_accel() with decimation
            // Accel runs at 100 Hz but output every 2nd sample (50 Hz)
            if (pending_state_vector_) {
                auto result = *pending_state_vector_;
                pending_state_vector_ = std::nullopt;
                return result;
            }
            
            return std::nullopt;
        }
        
        // ====================================================================
        // IMetricsCallbackProvider Implementation
        // ====================================================================
        
        /**
         * @brief Set the metrics callback for reporting sensor and estimator metrics
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
        virtual graph::IMetricsCallback* GetMetricsCallback() const noexcept override{
            return metrics_callback_;
        }

        app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
            app::metrics::NodeMetricsSchema schema;
            schema.node_name = GetName();
            schema.node_type = "processor";
            
            schema.metrics_schema = {
                {"fields", {
                    {
                        {"name", "gps_confidence"},
                        {"type", "double"},
                        {"unit", "percent"},
                        {"precision", 1},
                        {"description", "GPS data confidence (0-100%)"},
                        {"alert_rule", {
                            {"ok", {75.0, 100.0}},
                            {"warning", {50.0, 100.0}},
                            {"critical", "outside"}
                        }}
                    },
                    {
                        {"name", "mag_confidence"},
                        {"type", "double"},
                        {"unit", "percent"},
                        {"precision", 1},
                        {"description", "Magnetometer data confidence (0-100%)"},
                        {"alert_rule", {
                            {"ok", {75.0, 100.0}},
                            {"warning", {50.0, 100.0}},
                            {"critical", "outside"}
                        }}
                    },
                    {
                        {"name", "altitude_confidence"},
                        {"type", "double"},
                        {"unit", "percent"},
                        {"precision", 1},
                        {"description", "Altitude data confidence (0-100%)"},
                        {"diaplay_type", "gauge"},
                        {"alert_rule", {
                            {"ok", {75.0, 100.0}},
                            {"warning", {50.0, 100.0}},
                            {"critical", "outside"}
                        }}
                    },
                    {
                        {"name", "altitude_estimate_m"},
                        {"type", "double"},
                        {"unit", "m"},
                        {"precision", 2},
                        {"description", "Estimated altitude above ground"},
                        {"alert_rule", {
                            {"ok", {0.0, 10000.0}},
                            {"warning", {-500.0, 12000.0}},
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
                            {"ok", {-10.0, 10.0}},
                            {"warning", {-20.0, 20.0}},
                            {"critical", "outside"}
                        }}
                    },
                    {
                        {"name", "gps_fix_valid"},
                        {"type", "bool"},
                        {"unit", "bool"},
                        {"description", "GPS fix validity status"},
                        {"alert_rule", {
                            {"ok", {1.0, 1.0}},
                            {"warning", {0.0, 1.0}},
                            {"critical", "outside"}
                        }}
                    },
                    {
                        {"name", "gps_num_satellites"},
                        {"type", "int"},
                        {"unit", "count"},
                        {"description", "Number of GPS satellites in use"},
                        {"alert_rule", {
                            {"ok", {6.0, 20.0}},
                            {"warning", {3.0, 25.0}},
                            {"critical", "outside"}
                        }}
                    }
                }},
                {"metadata", {
                    {"node_type", "processor"},
                    {"description", "Multi-input sensor fusion hub (5 inputs → StateVector output)"},
                    {"refresh_rate_hz", 50},
                    {"critical_metrics", {"gps_fix_valid", "mag_confidence"}},
                    {"trend_metrics", {"altitude_estimate_m", "velocity_vertical_m_s"}},
                    {"alarm_conditions", {
                        "gps_fix_valid == false",
                        "mag_confidence < 0.5",
                        "gps_num_satellites < 4"
                    }}
                }}
            };
            
            schema.event_types = {"sensor_data_quality", "estimator_output"};
            
            return schema;
        }

        private:
            // ====================================================================
            // Metrics Callback
            // ====================================================================
            graph::IMetricsCallback* metrics_callback_{nullptr};
            
            /**
             * Create and initialize the filter based on configuration.
             */
            void CreateFilter() {
                std::string filter_description = graph::FilterFactory::CreateFilter(config_.filter);
                std::cout << "  Filter: " << filter_description << std::endl;
            }
            
            /**
             * Initialize the state history buffer.
             */
            void InitializeStateBuffer() {
                if (config_.state_buffer_size <= 0) {
                    throw graph::ConfigError("State buffer size must be positive");
                }
                std::cout << "  State buffer initialized with " << config_.state_buffer_size << " slots" << std::endl;
            }
            
              // Configuration and state
            graph::FlightFSMNodeConfig config_;  ///< Current configuration
            
            // MVP Phase 1 Estimators
            std::unique_ptr<estimators::AltitudeEstimator> altitude_estimator_;
            std::unique_ptr<estimators::VelocityEstimator> velocity_estimator_;
            std::unique_ptr<estimators::AngularRateIntegrator> angular_rate_integrator_;
            std::unique_ptr<estimators::StateVectorAggregator> aggregator_;
            
            // Latest barometric altitude and timestamp (for velocity blending)
            float baro_altitude_latest_m_ = 0.0f;
            uint64_t baro_timestamp_latest_us_ = 0;
            
            // Latest magnetometer heading and confidence (for StateVector)
            float magnetometer_heading_rad_ = 0.0f;
            float magnetometer_confidence_ = 0.0f;
            
            // Latest GPS position and accuracy data (for StateVector)
            double gps_latitude_ = 0.0;
            double gps_longitude_ = 0.0;
            double gps_horizontal_accuracy_ = 0.0;
            double gps_vertical_accuracy_ = 0.0;
            uint8_t gps_num_satellites_ = 0;
            bool gps_fix_valid_ = false;
            float gps_confidence_ = 0.0f;  // Based on fix validity and satellite count
            
            // Latest altitude estimator output (for aggregation)
            estimators::EstimatorOutput altitude_output_;
            
            // Output decimation: 100 Hz accel → 50 Hz StateVector
            size_t output_decimation_counter_ = 0;
            static constexpr size_t OUTPUT_DECIMATION_FACTOR = 2;  // Every 2nd accel
            
            // Pending state vector to output in next Process() call
            std::optional<sensors::StateVector> pending_state_vector_;
            
            // MVP Phase 1 Sensor Handlers
            
            /**
             * Handle accelerometer data from port 0.
             * Triggers VelocityEstimator update and StateVector generation (decimated 50 Hz).
             */
            void handle_accel(const sensors::AccelerometerData& data) {
                if (!velocity_estimator_ || !aggregator_) return;
                
                // Convert timestamp from nanoseconds to microseconds
                uint64_t timestamp_us = data.timestamp.count() / 1000;
                
                // Update velocity estimator with accel (100 Hz input)
                auto vel_out = velocity_estimator_->Update(data.acceleration.z, timestamp_us);
                
                // Every 2nd accel sample, generate StateVector output
                output_decimation_counter_++;
                if (output_decimation_counter_ >= OUTPUT_DECIMATION_FACTOR) {
                    output_decimation_counter_ = 0;
                    
                    // Aggregate altitude + velocity into StateVector
                    pending_state_vector_ = aggregator_->Aggregate(
                        altitude_output_,      // Latest altitude estimate
                        vel_out,               // Current velocity estimate
                        data.acceleration.z,   // Raw accel for StateVector
                        timestamp_us
                    );
                    
                    // Add attitude data from gyroscope integration
                    if (angular_rate_integrator_) {
                        pending_state_vector_->attitude = angular_rate_integrator_->GetAttitude();
                        pending_state_vector_->angular_velocity = angular_rate_integrator_->GetAngularVelocity();
                    }
                    
                    // Add heading data from magnetometer
                    pending_state_vector_->heading_rad = magnetometer_heading_rad_;
                    pending_state_vector_->confidence.heading = magnetometer_confidence_;
                    
                    // Add position data from GPS
                    pending_state_vector_->latitude = gps_latitude_;
                    pending_state_vector_->longitude = gps_longitude_;
                    pending_state_vector_->horizontal_accuracy = gps_horizontal_accuracy_;
                    pending_state_vector_->vertical_accuracy = gps_vertical_accuracy_;
                    pending_state_vector_->num_satellites = gps_num_satellites_;
                    pending_state_vector_->gps_fix_valid = gps_fix_valid_;
                    pending_state_vector_->confidence.gps_position = gps_confidence_;
                    
                    // Publish estimator_output metrics (50 Hz, decimated)
                    if (metrics_callback_) {
                        app::metrics::MetricsEvent event;
                        event.event_type = "estimator_output";
                        event.source = "FlightFSMNode";
                        event.timestamp = std::chrono::system_clock::now();
                        event.data["altitude_estimate_m"] = std::to_string(altitude_output_.value);
                        event.data["altitude_confidence"] = altitude_output_.confidence;
                        event.data["velocity_vertical_m_s"] = std::to_string(pending_state_vector_->velocity.z);
                        event.data["position_latitude"] = std::to_string(pending_state_vector_->latitude);
                        event.data["position_longitude"] = std::to_string(pending_state_vector_->longitude);
                        event.data["heading_rad"] = std::to_string(pending_state_vector_->heading_rad);
                        event.data["attitude_w"] = std::to_string(pending_state_vector_->attitude.w);
                        event.data["attitude_x"] = std::to_string(pending_state_vector_->attitude.x);
                        event.data["attitude_y"] = std::to_string(pending_state_vector_->attitude.y);
                        event.data["attitude_z"] = std::to_string(pending_state_vector_->attitude.z);
                        metrics_callback_->PublishAsync(event);
                    }
                }
                
                // Publish sensor_data_quality metrics (100 Hz, every accel)
                if (metrics_callback_) {
                    app::metrics::MetricsEvent event;
                    event.event_type = "sensor_data_quality";
                    event.source = "FlightFSMNode";
                    event.timestamp = std::chrono::system_clock::now();
                    event.data["magnetometer_heading_rad"] = std::to_string(magnetometer_heading_rad_);
                    event.data["mag_confidence"] = std::to_string(magnetometer_confidence_);
                    event.data["gps_num_satellites"] = std::to_string(static_cast<int>(gps_num_satellites_));
                    event.data["gps_fix_valid"] = std::to_string(gps_fix_valid_);
                    event.data["gps_confidence"] = std::to_string(gps_confidence_);
                    event.data["gps_horizontal_accuracy_m"] = std::to_string(gps_horizontal_accuracy_);
                    event.data["gps_vertical_accuracy_m"] = std::to_string(gps_vertical_accuracy_);
                    metrics_callback_->PublishAsync(event);
                }
            }
            
            /**
             * Handle barometric data from port 3.
             * Updates AltitudeEstimator and stores for next accel output.
             */
            void handle_baro(const sensors::BarometricData& data) {
                if (!altitude_estimator_) return;
                
                // Convert timestamp from nanoseconds to microseconds
                uint64_t timestamp_us = data.timestamp.count() / 1000;
                
                // Update altitude estimator with barometric pressure
                altitude_output_ = altitude_estimator_->Update(
                    data.pressure_pa,
                    timestamp_us
                );
                
                // Store latest baro reading for velocity complementary filter
                baro_altitude_latest_m_ = altitude_output_.value;
                baro_timestamp_latest_us_ = timestamp_us;
            }
            
            /**
             * Handle gyroscope data from port 1.
             * Updates angular rate estimates for attitude propagation.
             */
            void handle_gyro(const sensors::GyroscopeData& data) {
                if (!angular_rate_integrator_) return;
                
                // Integrate gyroscope data to get attitude estimate
                uint64_t timestamp_us = static_cast<uint64_t>(data.GetTimestampSeconds() * 1000000);
                angular_rate_integrator_->Update(data.angular_velocity, timestamp_us);
            }
            
            /**
             * Handle magnetometer data from port 2.
             * Provides heading reference and validates magnetic field.
             */
            void handle_magnetometer(const sensors::MagnetometerData& data) {
                if (!velocity_estimator_) return;
                
                // Validate magnetic field magnitude (Earth's field: 25-65 µT typical)
                float mag_magnitude = std::sqrt(
                    data.magnetic_field.x * data.magnetic_field.x +
                    data.magnetic_field.y * data.magnetic_field.y +
                    data.magnetic_field.z * data.magnetic_field.z
                );
                
                // Reject if field is too weak or too strong (indicating interference)
                if (mag_magnitude < 10.0f || mag_magnitude > 100.0f) {
                    magnetometer_confidence_ = 0.0f;  // Invalid measurement
                    return;
                }
                
                // Compute heading from X,Y magnetic field components
                float heading_rad = std::atan2(data.magnetic_field.y, data.magnetic_field.x);
                
                // Normalize to [0, 2π)
                if (heading_rad < 0.0f) {
                    heading_rad += 2.0f * M_PI;
                }
                
                // Store heading for next StateVector generation
                magnetometer_heading_rad_ = heading_rad;
                
                // Compute confidence based on magnetic field magnitude
                // Peak confidence at Earth's nominal field (~50 µT)
                const float nominal_field = 50.0f;
                float field_ratio = mag_magnitude / nominal_field;
                magnetometer_confidence_ = 1.0f / (1.0f + (field_ratio - 1.0f) * (field_ratio - 1.0f));
                magnetometer_confidence_ = std::max(0.5f, magnetometer_confidence_);  // Minimum 0.5
            }
            
            /**
             * Handle GPS position/altitude data from port 4.
             * Updates altitude and position estimates, blends with accelerometer.
             */
            void handle_gps(const sensors::GPSPositionData& data) {
                if (!altitude_estimator_ || !velocity_estimator_) return;
                
                // Store GPS fix validity and satellite count
                gps_fix_valid_ = data.fix_valid;
                gps_num_satellites_ = data.num_satellites;
                
                // Compute GPS confidence based on fix validity and satellite count
                // Confidence is high (0.95+) with 8+ satellites, degrades with fewer
                if (data.fix_valid && data.num_satellites > 0) {
                    // Base confidence increases with satellite count
                    // 4 satellites = 0.6, 8 satellites = 0.95, 12+ = 1.0
                    gps_confidence_ = std::min(1.0f, 0.5f + (data.num_satellites / 16.0f));
                } else {
                    gps_confidence_ = 0.0f;  // No fix = no confidence
                }
                
                // Store position and accuracy from GPS
                if (data.fix_valid) {
                    gps_latitude_ = data.latitude;
                    gps_longitude_ = data.longitude;
                    gps_horizontal_accuracy_ = data.horizontal_accuracy;
                    gps_vertical_accuracy_ = data.vertical_accuracy;
                    
                    // Update altitude estimator with GPS altitude
                    altitude_output_ = altitude_estimator_->Update(
                        static_cast<float>(data.altitude),
                        data.GetTimestampSeconds() * 1000000  // Convert to microseconds
                    );
                    
                    // Store GPS altitude for blending with accelerometer estimates
                    baro_altitude_latest_m_ = static_cast<float>(data.altitude);
                    baro_timestamp_latest_us_ = static_cast<uint64_t>(data.GetTimestampSeconds() * 1000000);
                    
                    // Update velocity estimator with GPS ground speed if available
                    auto vel_out = velocity_estimator_->Update(
                        static_cast<float>(data.ground_speed),
                        data.GetTimestampSeconds() * 1000000
                    );
                    (void)vel_out;  // Use to avoid unused warning
                }
            }
            
            // MergeFn Pattern: Per-Port Routers
            // 
            // Each input port (0-4) has a dedicated SensorDataRouter.
            // All 5 ports feed the same Message type (CommonInput) into a unified queue.
            // When messages are dequeued, Process() examines the SensorPayload
            // and routes to the appropriate router based on the variant type.
            // 
            // This is more efficient than N separate queues with polling:
            // - Single merged queue (inherited from MergeFn base)
            // - No polling (messages arrive directly)
            // - Per-port routers handle type discrimination
            
            sensors::SensorDataRouter router_port0_; ///< Accelerometer hardware device router
            sensors::SensorDataRouter router_port1_; ///< Gyroscope hardware device router
            sensors::SensorDataRouter router_port2_; ///< Magnetometer hardware device router
            sensors::SensorDataRouter router_port3_; ///< Barometer hardware device router
            sensors::SensorDataRouter router_port4_; ///< GPS hardware device router

    };

} // namespace avionics

