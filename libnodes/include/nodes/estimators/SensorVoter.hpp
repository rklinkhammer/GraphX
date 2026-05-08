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
 * @file SensorVoter.hpp
 * @brief Multi-source sensor consensus voting for altitude estimation
 *
 * @details
 * Implements a voting mechanism to combine measurements from multiple independent
 * altitude sources (accelerometer integration, barometer, GPS) and determine a
 * consensus altitude value. Features include:
 *
 * - **Outlier Detection**: Rejects measurements that deviate significantly from
 *   consensus (configurable sigma threshold)
 * - **Confidence Weighting**: Weights each source by its confidence metric
 * - **Majority Voting**: Fallback mechanism when sources strongly disagree
 * - **Source Disagreement Tracking**: Quantifies multi-source variance
 *
 * **Example Usage**:
 * ```cpp
 * SensorVoter voter;
 * voter.SetOutlierThreshold(2.0f);  // 2-sigma threshold
 *
 * SensorVoter::AltitudeVote votes = {
 *     .accel_altitude = 1000.0f,
 *     .barometer_altitude = 1001.5f,
 *     .gps_altitude = 998.2f,
 *     .accel_confidence = 0.9f,
 *     .barometer_confidence = 0.95f,
 *     .gps_confidence = 0.85f
 * };
 *
 * auto result = voter.Vote(votes);
 * std::cout << "Consensus altitude: " << result.altitude << "m\n";
 * std::cout << "Consensus confidence: " << result.confidence << "\n";
 * std::cout << "Disagreement: " << result.voting_disagreement << "m\n";
 * ```
 *
 * @author Robert Klinkhammer
 * @date January 6, 2026
 */

#pragma once

#include <cmath>
#include <array>
#include <algorithm>
#include <iostream>
#include "sensor/SensorClassificationType.hpp"

namespace avionics::estimators {

/**
 * @class SensorVoter
 * @brief Consensus voting for multi-source altitude measurement
 *
 * Combines independent altitude sources (accel, barometer, GPS) into a single
 * consensus value with outlier rejection and confidence weighting.
 */
class SensorVoter {
public:
    // Uses framework's standard sensors::SensorClassificationType for source identification
    // Type mapping: ACCELEROMETER→ACCELEROMETER, BAROMETER→BAROMETRIC, GPS→GPS_POSITION

    /**
     * @struct AltitudeVote
     * @brief Input measurements from three altitude sources
     */
    struct AltitudeVote {
        float accel_altitude;           ///< Altitude from accel integration (m)
        float barometer_altitude;       ///< Altitude from pressure (m)
        float gps_altitude;             ///< Altitude from GPS (m)

        float accel_confidence;         ///< Trust level [0.0, 1.0]
        float barometer_confidence;     ///< Trust level [0.0, 1.0]
        float gps_confidence;           ///< Trust level [0.0, 1.0]
    };

    /**
     * @struct ConsensusResult
     * @brief Output of voting process
     */
    struct ConsensusResult {
        float altitude;                                   ///< Voted consensus altitude (m)
        float confidence;                                 ///< Overall confidence [0.0, 1.0]
        sensors::SensorClassificationType primary_source;          ///< Best source (ACCELEROMETER, BAROMETRIC, GPS_POSITION)
        sensors::SensorClassificationType secondary_source;        ///< Second-best source
        float voting_disagreement;                        ///< Multi-source variance (m)
    };

    /**
     * @brief Construct a SensorVoter with default parameters
     *
     * Default configuration:
     * - Outlier threshold: 2.0 sigma
     * - Confidence weighting: enabled
     */
    SensorVoter();

    /**
     * @brief Destructor
     */
    ~SensorVoter() = default;

    /**
     * @brief Perform consensus voting on altitude measurements
     *
     * Voting algorithm:
     * 1. Remove outliers (sources deviating >threshold sigma)
     * 2. Weight remaining sources by confidence
     * 3. Compute weighted average
     * 4. Calculate disagreement metric
     *
     * @param votes Input measurements from 3 sources
     * @return Consensus result with voted altitude and metadata
     *
     * @details
     * - If all 3 sources nominal: weighted average of all three
     * - If 1 outlier: weighted average of remaining 2
     * - If 2 outliers: fallback to remaining source
     * - If all outliers: return middle value (safety fallback)
     */
    ConsensusResult Vote(const AltitudeVote& votes);

    /**
     * @brief Set outlier detection threshold (sigma)
     *
     * Sources deviating more than (threshold * standard_deviation) from the
     * mean are flagged as outliers and given reduced weight.
     *
     * @param sigma_threshold Standard deviations (typical: 2.0 or 3.0)
     */
    void SetOutlierThreshold(float sigma_threshold);

    /**
     * @brief Get current outlier threshold
     *
     * @return Threshold in standard deviations
     */
    float GetOutlierThreshold() const;

    /**
     * @brief Enable/disable confidence-weighted voting
     *
     * When enabled: sources are weighted by their confidence metric
     * When disabled: all sources weighted equally (simple average)
     *
     * @param enabled true to use confidence weighting
     */
    void SetConfidenceWeighting(bool enabled);

    /**
     * @brief Check if confidence weighting is enabled
     *
     * @return true if confidence weighting active
     */
    bool IsConfidenceWeightingEnabled() const;

    /**
     * @brief Reset internal state (for testing)
     */
    void Reset();

private:
    float outlier_threshold_;           ///< Sigma threshold for outlier detection
    bool use_confidence_weighting_;     ///< Whether to apply confidence weights

    /**
     * @brief Compute sample mean and standard deviation
     *
     * @param values Array of altitude values
     * @param count Number of values
     * @return Pair of {mean, std_dev}
     */
    std::pair<float, float> ComputeStatistics(
        const std::array<float, 3>& values) const;

    /**
     * @brief Check if a value is an outlier
     *
     * @param value Measurement value
     * @param mean Mean of all measurements
     * @param std_dev Standard deviation
     * @return true if value is outlier
     */
    bool IsOutlier(float value, float mean, float std_dev) const;

    /**
     * @brief Identify best and second-best sources by confidence
     *
     * @param votes Input vote measurements
     * @param primary Output: best source by confidence
     * @param secondary Output: second-best source
     */
    void IdentifyBestSources(
        const AltitudeVote& votes,
        sensors::SensorClassificationType& primary,
        sensors::SensorClassificationType& secondary) const;

    /**
     * @brief Get altitude value for a specific source
     *
     * @param votes Input vote measurements
     * @param source Source to extract from
     * @return Altitude value (m)
     */
    float GetSourceAltitude(
        const AltitudeVote& votes,
        sensors::SensorClassificationType source) const;

    /**
     * @brief Get confidence for a specific source
     *
     * @param votes Input vote measurements
     * @param source Source to extract from
     * @return Confidence value [0.0, 1.0]
     */
    float GetSourceConfidence(
        const AltitudeVote& votes,
        sensors::SensorClassificationType source) const;

    /**
     * @brief Compute weighted average altitude
     *
     * @param altitudes Array of 3 altitude measurements
     * @param confidences Array of 3 confidence values
     * @return Weighted average
     */
    float ComputeWeightedAverage(
        const std::array<float, 3>& altitudes,
        const std::array<float, 3>& confidences) const;

    /**
     * @brief Compute voting disagreement metric
     *
     * Measures variance across sources to quantify confidence in consensus.
     *
     * @param altitudes Array of altitude values
     * @return Standard deviation of altitudes
     */
    float ComputeDisagreement(
        const std::array<float, 3>& altitudes) const;
};

}  // namespace avionics::estimators
