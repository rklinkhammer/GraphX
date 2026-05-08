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
 * @file StateVectorAggregator.hpp
 * @brief Aggregates individual estimator outputs into consolidated StateVector
 *
 * Combines altitude, velocity, and attitude estimator outputs with:
 * - Cross-validation (consistency checks between estimates)
 * - Confidence scoring (consensus-based)
 * - Validity flags (sensor health bitmap)
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include "EstimatorBase.hpp"
#include "sensor/SensorDataTypes.hpp"
#include "avionics/estimators/SensorVoter.hpp"
#include <vector>
#include <cmath>
#include <chrono>

namespace avionics::estimators {

/**
 * Aggregates estimator outputs into StateVector for downstream nodes.
 * 
 * Combines:
 * - Altitude estimate + confidence
 * - Vertical velocity estimate + confidence
 * - Vertical acceleration (from accel sensor)
 * - Sensor health bitmap
 * - Consolidated validity flags
 * 
 * MVP Phase 1: Altitude + Velocity aggregation
 */
class StateVectorAggregator {
public:
    /**
     * Configuration for aggregation logic.
     */
    struct Config {
        // Cross-validation
        float altitude_velocity_mismatch_threshold = 5.0f;  ///< m or m/s discrepancy tolerance
        
        // History for consistency checks
        static constexpr size_t STATE_HISTORY_SIZE = 3;  ///< Keep last 3 states for trend
    };
    
    StateVectorAggregator() {
        config_ = Config();
        altitude_voter_.SetOutlierThreshold(2.5f);
    }
    
    explicit StateVectorAggregator(const Config& config) : config_(config) {
        altitude_voter_.SetOutlierThreshold(2.5f);
    }
    
    /**
     * Aggregate outputs from estimators into a StateVector.
     * 
     * Process:
     * 1. Copy altitude + confidence from AltitudeEstimator
     * 2. Copy velocity + confidence from VelocityEstimator
     * 3. Cross-validate (check for altitude/velocity mismatch)
     * 4. Adjust confidence based on cross-validation
     * 5. Consolidate validity flags
     * 6. Create sensor health bitmap
     * 7. Package into StateVector
     * 
     * @param alt_out Altitude estimator output
     * @param vel_out Velocity estimator output
     * @param accel_z Vertical acceleration (from raw sensor, m/s²)
     * @param timestamp_us Timestamp of aggregation (microseconds)
     * @return StateVector with consolidated estimates
     */
    sensors::StateVector Aggregate(
        const EstimatorOutput& alt_out,
        const EstimatorOutput& vel_out,
        float accel_z,
        uint64_t timestamp_us)
    {
        sensors::StateVector state;
        
        // Copy position data
        state.position.x = 0.0f;
        state.position.y = 0.0f;
        state.position.z = alt_out.value;  // Altitude (m)
        
        // Copy velocity data
        state.velocity.x = 0.0f;
        state.velocity.y = 0.0f;
        state.velocity.z = vel_out.value;  // Vertical velocity (m/s)
        
        // Copy acceleration data
        state.acceleration.x = 0.0f;
        state.acceleration.y = 0.0f;
        state.acceleration.z = accel_z;    // Vertical acceleration (m/s²)
        
        // Attitude (not used in MVP)
        state.attitude = sensors::Quaternion();  // Identity
        
        // Angular velocity (not used in MVP)
        state.angular_velocity.x = 0.0f;
        state.angular_velocity.y = 0.0f;
        state.angular_velocity.z = 0.0f;
        
        // Timestamp
        state.timestamp = std::chrono::nanoseconds(timestamp_us * 1000);  // Convert microseconds to nanoseconds
        
        // Initialize consolidated confidence and flags
        float confidence_altitude = alt_out.confidence;
        float confidence_velocity = vel_out.confidence;
        uint32_t consolidated_flags = alt_out.validity_flags | vel_out.validity_flags;
        (void)consolidated_flags;  // Suppress unused warning (Phase 2 will use)
        
        // Cross-validate: altitude and velocity should be consistent
        // (simplified check in MVP)
        if (sample_history_.size() >= 2) {
            const auto& prev_state = sample_history_.back();
            
            float dh = alt_out.value - prev_state.altitude;
            float dt = (timestamp_us - prev_state.timestamp_us) * 1e-6f;
            
            if (dt > 0.01f) {  // Need at least 10 ms
                float implied_velocity = dh / dt;
                float velocity_error = std::abs(implied_velocity - vel_out.value);
                
                if (velocity_error > config_.altitude_velocity_mismatch_threshold) {
                    // Mismatch detected; mark and reduce confidence
                    consolidated_flags |= ValidityFlags::BUFFER_UNDERFLOW;  // Reuse for mismatch
                    confidence_altitude *= 0.8f;
                    confidence_velocity *= 0.8f;
                }
            }
        }
        
        // Store in history for next iteration
        StoredState stored{
            alt_out.value,
            vel_out.value,
            timestamp_us,
            confidence_altitude,
            confidence_velocity
        };
        sample_history_.push_back(stored);
        if (sample_history_.size() > Config::STATE_HISTORY_SIZE) {
            sample_history_.erase(sample_history_.begin());
        }
        
        // Create sensor health bitmap
        // Bit 0: Accel valid (1 = valid, 0 = error)
        // Bit 1: Baro valid
        // Bit 2: Gyro valid (not used in MVP)
        // Bit 3: Mag valid (not used in MVP)
        // Bit 4: GPS valid
        
        uint8_t health = 0;
        health |= (!(alt_out.HasFlag(ValidityFlags::SENSOR_DROPOUT)) ? 0x02 : 0);
        health |= (!(vel_out.HasFlag(ValidityFlags::SENSOR_DROPOUT)) ? 0x01 : 0);
        (void)health;  // Suppress unused warning (Phase 2 will include in StateVector)
        
        // Package into extra fields (StateVector may not have confidence explicitly)
        // For now, store in validity_flags via bit manipulation
        // In Phase 2+, extend StateVector structure to include confidence directly
        
        // Clamp confidences
        confidence_altitude = std::max(0.0f, std::min(1.0f, confidence_altitude));
        confidence_velocity = std::max(0.0f, std::min(1.0f, confidence_velocity));
        
        // Note: StateVector doesn't have explicit confidence field in current definition
        // Phase 2 will extend this. For MVP, use validity flags.
        
        return state;
    }
    
    /**
     * Aggregate outputs with consensus voting on altitude/velocity sources.
     * 
     * Phase 7: New voting-based aggregation that:
     * 1. Performs consensus voting on three altitude sources (accel, baro, GPS)
     * 2. Performs per-axis voting on three velocity sources
     * 3. Populates altitude_sources and velocity_sources in StateVector
     * 4. Returns voted consensus results + per-sensor estimates
     * 
     * @param alt_accel Altitude from integrated acceleration (m)
     * @param alt_baro Altitude from barometric pressure (m)
     * @param alt_gps GPS altitude (m)
     * @param conf_accel_alt Confidence in accel altitude
     * @param conf_baro_alt Confidence in baro altitude
     * @param conf_gps_alt Confidence in GPS altitude
     * @param vel_accel Velocity from integrated acceleration (m/s)
     * @param vel_baro Velocity from barometric altitude rate (m/s)
     * @param vel_gps GPS vertical speed (m/s)
     * @param conf_accel_vel Confidence in accel velocity
     * @param conf_baro_vel Confidence in baro velocity
     * @param conf_gps_vel Confidence in GPS velocity
     * @param accel_z Raw vertical acceleration (m/s²)
     * @param timestamp_us Timestamp (microseconds)
     * @return StateVector with voted consensus + per-sensor sources
     */
    sensors::StateVector AggregateWithVoting(
        float alt_accel, float alt_baro, float alt_gps,
        float conf_accel_alt, float conf_baro_alt, float conf_gps_alt,
        float vel_accel, float vel_baro, float vel_gps,
        float conf_accel_vel, float conf_baro_vel, float conf_gps_vel,
        float accel_z,
        uint64_t timestamp_us)
    {
        sensors::StateVector state;
        
        // Copy basic state
        state.position.x = 0.0f;
        state.position.y = 0.0f;
        state.velocity.x = 0.0f;
        state.velocity.y = 0.0f;
        state.acceleration.x = 0.0f;
        state.acceleration.y = 0.0f;
        state.acceleration.z = accel_z;
        
        state.attitude = sensors::Quaternion();  // Identity
        state.angular_velocity.x = 0.0f;
        state.angular_velocity.y = 0.0f;
        state.angular_velocity.z = 0.0f;
        
        state.timestamp = std::chrono::nanoseconds(timestamp_us * 1000);
        
        // Store per-sensor sources for downstream visibility
        state.altitude_sources.from_accel = alt_accel;
        state.altitude_sources.from_baro = alt_baro;
        state.altitude_sources.from_gps = alt_gps;
        state.altitude_sources.confidence_accel = conf_accel_alt;
        state.altitude_sources.confidence_baro = conf_baro_alt;
        state.altitude_sources.confidence_gps = conf_gps_alt;
        
        state.velocity_sources.z_from_accel = vel_accel;
        state.velocity_sources.z_from_baro = vel_baro;
        state.velocity_sources.z_from_gps = vel_gps;
        state.velocity_sources.confidence_accel = conf_accel_vel;
        state.velocity_sources.confidence_baro = conf_baro_vel;
        state.velocity_sources.confidence_gps = conf_gps_vel;
        
        // Perform consensus voting on altitude
        SensorVoter::AltitudeVote altitude_vote{
            .accel_altitude = alt_accel,
            .barometer_altitude = alt_baro,
            .gps_altitude = alt_gps,
            .accel_confidence = conf_accel_alt,
            .barometer_confidence = conf_baro_alt,
            .gps_confidence = conf_gps_alt
        };
        
        auto altitude_consensus = altitude_voter_.Vote(altitude_vote);
        state.position.z = altitude_consensus.altitude;
        state.confidence.altitude = altitude_consensus.confidence;
        
        // Perform consensus voting on velocity (simplified: use average of three sources)
        float voted_velocity = (vel_accel * conf_accel_vel +
                               vel_baro * conf_baro_vel +
                               vel_gps * conf_gps_vel) /
                              (conf_accel_vel + conf_baro_vel + conf_gps_vel + 1e-6f);
        state.velocity.z = voted_velocity;
        state.confidence.velocity = (conf_accel_vel + conf_baro_vel + conf_gps_vel) / 3.0f;
        
        // Store in history for next iteration
        StoredState stored{
            altitude_consensus.altitude,
            voted_velocity,
            timestamp_us,
            altitude_consensus.confidence,
            state.confidence.velocity
        };
        sample_history_.push_back(stored);
        if (sample_history_.size() > Config::STATE_HISTORY_SIZE) {
            sample_history_.erase(sample_history_.begin());
        }
        
        return state;
    }
    
    /**
     * Get the altitude voter for testing and diagnostics.
     */
    const SensorVoter& GetAltitudeVoter() const {
        return altitude_voter_;
    }
    
    /**
     * Set outlier threshold for voting.
     */
    void SetOutlierThreshold(float threshold) {
        altitude_voter_.SetOutlierThreshold(threshold);
    }
    
    /**
     * Reset aggregator state (e.g., on sensor dropout recovery).
     */
    void Reset() {
        sample_history_.clear();
    }

private:
    Config config_;
    SensorVoter altitude_voter_;  ///< Consensus voter for altitude sources (Phase 7)
    
    /**
     * Internal state for cross-validation.
     */
    struct StoredState {
        float altitude;
        float velocity;
        uint64_t timestamp_us;
        float confidence_altitude;
        float confidence_velocity;
    };
    
    std::vector<StoredState> sample_history_;
};

} // namespace avionics::estimators

