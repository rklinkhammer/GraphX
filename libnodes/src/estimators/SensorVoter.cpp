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
 * @file SensorVoter.cpp
 * @brief Implementation of multi-source sensor consensus voting
 *
 * @author Robert Klinkhammer
 * @date January 6, 2026
 */

#include "avionics/estimators/SensorVoter.hpp"
#include <cmath>
#include <numeric>

namespace avionics::estimators {

SensorVoter::SensorVoter()
    : outlier_threshold_(2.0f),
      use_confidence_weighting_(true) {
}

SensorVoter::ConsensusResult SensorVoter::Vote(const AltitudeVote& votes) {
    // Extract altitude values into array
    std::array<float, 3> altitudes = {
        votes.accel_altitude,
        votes.barometer_altitude,
        votes.gps_altitude
    };

    // Extract confidence values
    std::array<float, 3> confidences = {
        votes.accel_confidence,
        votes.barometer_confidence,
        votes.gps_confidence
    };

    // Compute statistics for outlier detection
    auto [mean, std_dev] = ComputeStatistics(altitudes);

    // Identify outliers
    std::array<bool, 3> is_outlier = {
        IsOutlier(altitudes[0], mean, std_dev),
        IsOutlier(altitudes[1], mean, std_dev),
        IsOutlier(altitudes[2], mean, std_dev)
    };

    // Count non-outliers
    int non_outlier_count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!is_outlier[i]) {
            non_outlier_count++;
        }
    }

    // Compute consensus altitude
    float consensus_altitude;

    if (non_outlier_count >= 2) {
        // Use weighted average of all sources (outliers get zero weight)
        float sum_weighted = 0.0f;
        float sum_weights = 0.0f;

        for (int i = 0; i < 3; ++i) {
            float weight = is_outlier[i] ? 0.0f : confidences[i];
            sum_weighted += altitudes[i] * weight;
            sum_weights += weight;
        }

        consensus_altitude = (sum_weights > 1e-6f) ? (sum_weighted / sum_weights) : 
                            ComputeWeightedAverage(altitudes, confidences);
    } else if (non_outlier_count == 1) {
        // Use single remaining source
        for (int i = 0; i < 3; ++i) {
            if (!is_outlier[i]) {
                consensus_altitude = altitudes[i];
                break;
            }
        }
    } else {
        // Safety fallback: use median of all sources
        std::array<float, 3> sorted = altitudes;
        std::sort(sorted.begin(), sorted.end());
        consensus_altitude = sorted[1];  // Median
    }

    // Compute overall confidence
    float consensus_confidence = 0.0f;
    if (non_outlier_count >= 2) {
        // Average confidence of non-outlier sources
        float sum_conf = 0.0f;
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            if (!is_outlier[i]) {
                sum_conf += confidences[i];
                count++;
            }
        }
        consensus_confidence = sum_conf / count;
    } else if (non_outlier_count == 1) {
        // Single source confidence
        for (int i = 0; i < 3; ++i) {
            if (!is_outlier[i]) {
                consensus_confidence = confidences[i];
                break;
            }
        }
    } else {
        // All outliers: degrade confidence
        consensus_confidence = 0.3f;
    }

    // Clamp confidence to valid range
    consensus_confidence = std::max(0.0f, std::min(1.0f, consensus_confidence));

    // Identify best sources
    sensors::SensorClassificationType primary, secondary;
    IdentifyBestSources(votes, primary, secondary);

    // Compute disagreement metric
    float disagreement = ComputeDisagreement(altitudes);

    return SensorVoter::ConsensusResult{
        .altitude = consensus_altitude,
        .confidence = consensus_confidence,
        .primary_source = primary,
        .secondary_source = secondary,
        .voting_disagreement = disagreement
    };
}

void SensorVoter::SetOutlierThreshold(float sigma_threshold) {
    outlier_threshold_ = std::max(0.5f, std::min(5.0f, sigma_threshold));
}

float SensorVoter::GetOutlierThreshold() const {
    return outlier_threshold_;
}

void SensorVoter::SetConfidenceWeighting(bool enabled) {
    use_confidence_weighting_ = enabled;
}

bool SensorVoter::IsConfidenceWeightingEnabled() const {
    return use_confidence_weighting_;
}

void SensorVoter::Reset() {
    outlier_threshold_ = 2.0f;
    use_confidence_weighting_ = true;
}

std::pair<float, float> SensorVoter::ComputeStatistics(
    const std::array<float, 3>& values) const {
    // Compute mean
    float mean = (values[0] + values[1] + values[2]) / 3.0f;

    // Compute standard deviation
    float sum_sq_diff = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    float variance = sum_sq_diff / 3.0f;
    float std_dev = std::sqrt(variance);

    return {mean, std_dev};
}

bool SensorVoter::IsOutlier(float value, float mean, float std_dev) const {
    if (std_dev < 1e-6f) {
        // All values essentially equal, no outliers
        return false;
    }

    float z_score = std::abs(value - mean) / std_dev;
    return z_score > outlier_threshold_;
}

void SensorVoter::IdentifyBestSources(
    const AltitudeVote& votes,
    sensors::SensorClassificationType& primary,
    sensors::SensorClassificationType& secondary) const {
    // Find two highest confidence sources
    std::array<std::pair<float, sensors::SensorClassificationType>, 3> sources = {{
        {votes.accel_confidence, sensors::SensorClassificationType::ACCELEROMETER},
        {votes.barometer_confidence, sensors::SensorClassificationType::BAROMETRIC},
        {votes.gps_confidence, sensors::SensorClassificationType::GPS_POSITION}
    }};

    // Sort by confidence (descending)
    std::sort(sources.begin(), sources.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

    primary = sources[0].second;
    secondary = sources[1].second;
}

float SensorVoter::GetSourceAltitude(
    const AltitudeVote& votes,
    sensors::SensorClassificationType source) const {
    switch (source) {
        case sensors::SensorClassificationType::ACCELEROMETER:
            return votes.accel_altitude;
        case sensors::SensorClassificationType::BAROMETRIC:
            return votes.barometer_altitude;
        case sensors::SensorClassificationType::GPS_POSITION:
            return votes.gps_altitude;
        default:
            return 0.0f;
    }
}

float SensorVoter::GetSourceConfidence(
    const AltitudeVote& votes,
    sensors::SensorClassificationType source) const {
    switch (source) {
        case sensors::SensorClassificationType::ACCELEROMETER:
            return votes.accel_confidence;
        case sensors::SensorClassificationType::BAROMETRIC:
            return votes.barometer_confidence;
        case sensors::SensorClassificationType::GPS_POSITION:
            return votes.gps_confidence;
        default:
            return 0.0f;
    }
}

float SensorVoter::ComputeWeightedAverage(
    const std::array<float, 3>& altitudes,
    const std::array<float, 3>& confidences) const {
    if (!use_confidence_weighting_) {
        // Simple average
        return (altitudes[0] + altitudes[1] + altitudes[2]) / 3.0f;
    }

    // Confidence-weighted average
    float sum_weighted = 0.0f;
    float sum_weights = 0.0f;

    for (int i = 0; i < 3; ++i) {
        float weight = confidences[i];
        sum_weighted += altitudes[i] * weight;
        sum_weights += weight;
    }

    if (sum_weights < 1e-6f) {
        // All confidences zero: fallback to simple average
        return (altitudes[0] + altitudes[1] + altitudes[2]) / 3.0f;
    }

    return sum_weighted / sum_weights;
}

float SensorVoter::ComputeDisagreement(
    const std::array<float, 3>& altitudes) const {
    auto [mean, std_dev] = ComputeStatistics(altitudes);
    return std_dev;
}

}  // namespace avionics::estimators
