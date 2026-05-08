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
 * @file ConfidenceAggregator.hpp
 * @brief Weighted voting system for sensor confidence fusion
 *
 * Aggregates confidence metrics from multiple estimators using weighted voting:
 * - Independent sensor measurements with individual confidence values
 * - Weighted average fusion based on measurement confidence
 * - Outlier detection and downweighting
 * - Overall system confidence assessment
 *
 * Phase 3 Task 3.5 Component: Multi-sensor confidence fusion
 *
 * @author Robert Klinkhammer
 * @date 2026-01-05
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>

namespace avionics::estimators {

/**
 * @struct SensorReading
 * @brief Individual sensor measurement with confidence
 */
struct SensorReading {
    /// Measurement value (e.g., heading in radians, altitude in meters)
    float value;
    
    /// Confidence level [0.0, 1.0]
    float confidence;
    
    /// Is this reading valid/enabled
    bool valid;
    
    /// Constructor
    SensorReading(float v = 0.0f, float c = 0.0f, bool valid = true)
        : value(v), confidence(c), valid(valid) {}
};

/**
 * @struct AggregationResult
 * @brief Result from confidence voting
 */
struct AggregationResult {
    /// Weighted average of measurements
    float fused_value;
    
    /// Overall system confidence
    float overall_confidence;
    
    /// Number of measurements used in vote
    uint32_t vote_count;
    
    /// Was aggregation successful (at least 1 valid measurement)
    bool valid;
};

/**
 * @class ConfidenceAggregator
 * @brief Weighted voting system for multi-sensor fusion
 *
 * Implements confidence-weighted aggregation for combining measurements
 * from multiple sensors with different reliability levels.
 *
 * Phase 3 Task 3.5 Component: Confidence voting and fusion
 *
 * Features:
 * - Weighted average: sum(value * confidence) / sum(confidence)
 * - Outlier detection: flags measurements >2σ from weighted mean
 * - Confidence downweighting: reduces weight of low-confidence measurements
 * - System confidence: weighted average of individual confidences
 */
class ConfidenceAggregator {
public:
    /**
     * @brief Construct confidence aggregator
     * 
     * @param outlier_threshold Standard deviations for outlier detection (default: 2.0)
     * @param outlier_penalty Confidence multiplier for detected outliers (default: 0.25)
     */
    explicit ConfidenceAggregator(float outlier_threshold = 2.0f,
                                  float outlier_penalty = 0.25f);

    /**
     * @brief Aggregate readings using weighted voting
     *
     * Computes weighted average of measurements, detects outliers,
     * and produces overall system confidence estimate.
     *
     * Algorithm:
     * 1. Compute initial weighted mean: sum(v*c) / sum(c)
     * 2. Compute weighted standard deviation
     * 3. Detect outliers: |v - mean| > threshold * stddev
     * 4. Apply outlier penalty to their confidence
     * 5. Recompute with penalized weights
     * 6. Return fused value and overall confidence
     *
     * @param readings Array of sensor readings (up to 8 sensors)
     * @param num_readings Number of valid readings in array
     * @return Aggregation result with fused value and confidence
     */
    AggregationResult Aggregate(const SensorReading* readings, uint32_t num_readings);

    /**
     * @brief Aggregate two measurements (common case)
     * @param reading1 First sensor measurement
     * @param reading2 Second sensor measurement
     * @return Aggregation result
     */
    AggregationResult AggregatePair(const SensorReading& reading1,
                                    const SensorReading& reading2);

    /**
     * @brief Aggregate three measurements
     * @param r1 First reading
     * @param r2 Second reading
     * @param r3 Third reading
     * @return Aggregation result
     */
    AggregationResult AggregateTriple(const SensorReading& r1,
                                      const SensorReading& r2,
                                      const SensorReading& r3);

    /**
     * @brief Set outlier detection threshold (in standard deviations)
     * 
     * Higher values = less aggressive outlier detection
     * Typical range: 1.5 to 3.0 (2.0 = ~95% confidence interval)
     * 
     * @param threshold Standard deviations for outlier boundary
     */
    void SetOutlierThreshold(float threshold) {
        outlier_threshold_ = std::max(0.5f, threshold);
    }

    /**
     * @brief Set outlier penalty factor
     * 
     * When a measurement is detected as outlier, its confidence
     * is multiplied by this factor. Range [0.0, 1.0].
     * 
     * @param penalty Confidence multiplier (0.0 = ignore outlier, 1.0 = no penalty)
     */
    void SetOutlierPenalty(float penalty) {
        outlier_penalty_ = std::clamp(penalty, 0.0f, 1.0f);
    }

    /**
     * @brief Get outlier detection threshold
     * @return Current threshold in standard deviations
     */
    float GetOutlierThreshold() const { return outlier_threshold_; }

    /**
     * @brief Get outlier penalty factor
     * @return Current penalty [0.0, 1.0]
     */
    float GetOutlierPenalty() const { return outlier_penalty_; }

    /**
     * @brief Get last aggregation result
     * @return Most recent aggregation result
     */
    AggregationResult GetLastResult() const { return last_result_; }

    /**
     * @brief Get last outlier detection status
     * 
     * @param reading_index Index of sensor (0 to num_readings-1)
     * @return true if that sensor was detected as outlier
     */
    bool WasOutlier(uint32_t reading_index) const {
        if (reading_index >= last_outlier_flags_.size()) return false;
        return last_outlier_flags_[reading_index];
    }

    /**
     * @brief Get effective weight used for each reading
     * 
     * Effective weight = confidence * (1.0 if not outlier, penalty if outlier)
     * 
     * @param reading_index Index of sensor
     * @return Effective weight [0.0, 1.0]
     */
    float GetEffectiveWeight(uint32_t reading_index) const {
        if (reading_index >= last_effective_weights_.size()) return 0.0f;
        return last_effective_weights_[reading_index];
    }

    /**
     * @brief Reset all internal state
     */
    void Reset();

    /**
     * @brief Get aggregation count (diagnostic)
     * @return Number of Aggregate calls since creation/reset
     */
    uint32_t GetAggregationCount() const { return aggregation_count_; }

private:
    /// Helper: compute weighted mean of readings
    float ComputeWeightedMean(const SensorReading* readings, 
                             uint32_t num_readings,
                             const float* weights) const;
    
    /// Helper: compute weighted variance
    float ComputeWeightedVariance(const SensorReading* readings,
                                  uint32_t num_readings,
                                  const float* weights,
                                  float mean) const;
    
    /// Helper: detect outliers and return penalized weights
    void DetectOutliersAndPenalize(const SensorReading* readings,
                                   uint32_t num_readings,
                                   float mean,
                                   float stddev,
                                   float* penalized_weights);

    // Configuration
    float outlier_threshold_;    // Standard deviations for outlier detection
    float outlier_penalty_;      // Confidence multiplier for outliers

    // State
    AggregationResult last_result_;
    std::array<bool, 8> last_outlier_flags_;
    std::array<float, 8> last_effective_weights_;
    uint32_t aggregation_count_;
};

} // namespace avionics::estimators

