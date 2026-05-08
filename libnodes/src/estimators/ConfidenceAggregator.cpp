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

#include "avionics/estimators/ConfidenceAggregator.hpp"
#include <cmath>

namespace avionics::estimators {

ConfidenceAggregator::ConfidenceAggregator(float outlier_threshold, float outlier_penalty)
    : outlier_threshold_(outlier_threshold),
      outlier_penalty_(outlier_penalty),
      last_result_{0.0f, 0.0f, 0, false},
      aggregation_count_(0) {
    
    // Initialize outlier flags and weights
    last_outlier_flags_.fill(false);
    last_effective_weights_.fill(0.0f);
}

AggregationResult ConfidenceAggregator::Aggregate(const SensorReading* readings,
                                                   uint32_t num_readings) {
    AggregationResult result{0.0f, 0.0f, 0, false};
    aggregation_count_++;
    
    // Validate input
    if (!readings || num_readings == 0) {
        last_result_ = result;
        return result;
    }
    
    if (num_readings > 8) {
        num_readings = 8;  // Cap at 8 sensors
    }
    
    // Count valid readings
    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (readings[i].valid && readings[i].confidence > 0.0f) {
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        last_result_ = result;
        return result;
    }
    
    // Initialize weights from confidence values
    float weights[8] = {0.0f};
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (readings[i].valid) {
            weights[i] = readings[i].confidence;
        }
    }
    
    // First pass: compute weighted mean
    float mean = ComputeWeightedMean(readings, num_readings, weights);
    
    // Second pass: compute weighted standard deviation
    float variance = ComputeWeightedVariance(readings, num_readings, weights, mean);
    float stddev = std::sqrt(variance);
    
    // Third pass: detect outliers and apply penalties
    DetectOutliersAndPenalize(readings, num_readings, mean, stddev, weights);
    
    // Fourth pass: recompute weighted mean with penalized weights
    float final_mean = ComputeWeightedMean(readings, num_readings, weights);
    
    // Compute overall confidence as weighted average of confidences
    float confidence_sum = 0.0f;
    float weight_sum = 0.0f;
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (readings[i].valid) {
            confidence_sum += readings[i].confidence * weights[i];
            weight_sum += weights[i];
        }
    }
    
    float overall_confidence = (weight_sum > 1e-6f) ? (confidence_sum / weight_sum) : 0.0f;
    
    // Store results
    result.fused_value = final_mean;
    result.overall_confidence = overall_confidence;
    result.vote_count = valid_count;
    result.valid = true;
    
    last_result_ = result;
    
    // Store effective weights for diagnostics
    for (uint32_t i = 0; i < num_readings; ++i) {
        last_effective_weights_[i] = weights[i];
    }
    for (uint32_t i = num_readings; i < 8; ++i) {
        last_effective_weights_[i] = 0.0f;
    }
    
    return result;
}

AggregationResult ConfidenceAggregator::AggregatePair(const SensorReading& reading1,
                                                      const SensorReading& reading2) {
    SensorReading readings[2] = {reading1, reading2};
    return Aggregate(readings, 2);
}

AggregationResult ConfidenceAggregator::AggregateTriple(const SensorReading& r1,
                                                       const SensorReading& r2,
                                                       const SensorReading& r3) {
    SensorReading readings[3] = {r1, r2, r3};
    return Aggregate(readings, 3);
}

void ConfidenceAggregator::Reset() {
    last_result_ = AggregationResult{0.0f, 0.0f, 0, false};
    last_outlier_flags_.fill(false);
    last_effective_weights_.fill(0.0f);
    aggregation_count_ = 0;
}

float ConfidenceAggregator::ComputeWeightedMean(const SensorReading* readings,
                                                uint32_t num_readings,
                                                const float* weights) const {
    float weighted_sum = 0.0f;
    float weight_sum = 0.0f;
    
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (readings[i].valid && weights[i] > 0.0f) {
            weighted_sum += readings[i].value * weights[i];
            weight_sum += weights[i];
        }
    }
    
    if (weight_sum < 1e-6f) {
        return 0.0f;
    }
    
    return weighted_sum / weight_sum;
}

float ConfidenceAggregator::ComputeWeightedVariance(const SensorReading* readings,
                                                    uint32_t num_readings,
                                                    const float* weights,
                                                    float mean) const {
    float weighted_sum_sq = 0.0f;
    float weight_sum = 0.0f;
    
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (readings[i].valid && weights[i] > 0.0f) {
            float diff = readings[i].value - mean;
            weighted_sum_sq += diff * diff * weights[i];
            weight_sum += weights[i];
        }
    }
    
    if (weight_sum < 1e-6f) {
        return 0.0f;
    }
    
    return weighted_sum_sq / weight_sum;
}

void ConfidenceAggregator::DetectOutliersAndPenalize(const SensorReading* readings,
                                                     uint32_t num_readings,
                                                     float mean,
                                                     float stddev,
                                                     float* penalized_weights) {
    // Reset outlier flags
    for (uint32_t i = 0; i < 8; ++i) {
        last_outlier_flags_[i] = false;
    }
    
    // If stddev is very small, can't reliably detect outliers
    if (stddev < 1e-6f) {
        return;
    }
    
    // Check each reading for outlier status
    float outlier_bound = outlier_threshold_ * stddev;
    
    for (uint32_t i = 0; i < num_readings; ++i) {
        if (!readings[i].valid || penalized_weights[i] < 1e-6f) {
            continue;
        }
        
        float deviation = std::abs(readings[i].value - mean);
        
        if (deviation > outlier_bound) {
            // Mark as outlier and apply penalty
            last_outlier_flags_[i] = true;
            penalized_weights[i] *= outlier_penalty_;
        }
    }
}

} // namespace avionics::estimators
