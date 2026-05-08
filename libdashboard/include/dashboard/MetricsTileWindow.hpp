// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#ifndef GDASHBOARD_METRICS_TILE_WINDOW_HPP
#define GDASHBOARD_METRICS_TILE_WINDOW_HPP

#include <string>
#include <deque>
#include <nlohmann/json.hpp>
#include <ftxui/component/component.hpp>

using json = nlohmann::json;

/**
 * @brief Alert status enumeration for metrics
 */
enum class AlertStatus {
    OK = 0,
    WARNING = 1,
    CRITICAL = 2
};

/**
 * @brief Descriptor for a metric tile
 */
struct MetricDescriptor {
    std::string node_name;      ///< Name of the node producing this metric
    std::string metric_name;    ///< Name of the metric
    std::string metric_id;      ///< Unique identifier for the metric
    std::string unit;           ///< Unit of measurement
    double min_value = 0.0;     ///< Minimum expected value
    double max_value = 100.0;   ///< Maximum expected value
};

/**
 * @brief Metrics tile window with alert status and color coding
 *
 * Represents a single metric tile that displays:
 * - Current metric value
 * - Alert status (OK, WARNING, CRITICAL) based on configurable thresholds
 * - Color coding based on alert level
 * - Sparkline history of recent values
 *
 * Alert thresholds are defined in the field spec JSON:
 * - alert_rule.ok: [min, max] range for OK status
 * - alert_rule.warning: [min, max] range for WARNING status
 * - alert_rule.critical_min: value above which status is CRITICAL
 */
class MetricsTileWindow {
public:
    /**
     * @brief Construct a metrics tile from descriptor and field spec
     *
     * @param descriptor The metric descriptor
     * @param field_spec JSON object containing field specification including alert rules
     */
    MetricsTileWindow(const MetricDescriptor& descriptor, const json& field_spec);

    /**
     * @brief Get the metric ID
     * @return Unique identifier for this metric
     */
    std::string GetMetricId() const;

    /**
     * @brief Get the display type
     * @return Display type string (e.g., "gauge", "number", "sparkline")
     */
    std::string GetDisplayType() const;

    /**
     * @brief Get the current alert status
     * @return Current AlertStatus (OK, WARNING, or CRITICAL)
     */
    AlertStatus GetStatus() const;

    /**
     * @brief Update the metric value
     *
     * Updates the current value and re-evaluates alert status based on
     * configured thresholds. Also maintains a history for sparkline generation.
     *
     * @param value The new metric value
     */
    void UpdateValue(double value);

    /**
     * @brief Render the metrics tile as an FTXUI element
     *
     * Returns an FTXUI element that displays:
     * - Metric name
     * - Current value with unit
     * - Alert status with color coding
     * - Sparkline of recent values
     *
     * @return FTXUI Element for display
     */
    ftxui::Element Render() const;

private:
    MetricDescriptor descriptor_;
    json field_spec_;
    double current_value_ = 0.0;
    AlertStatus current_status_ = AlertStatus::OK;
    std::deque<double> value_history_;
    static constexpr size_t HISTORY_SIZE = 60;  // Maintain last 60 values

    /**
     * @brief Update alert status based on current value and thresholds
     */
    void UpdateStatus();

    /**
     * @brief Parse alert thresholds from field spec JSON
     */
    void ParseAlertThresholds();

    // Alert threshold ranges
    double ok_min_ = 0.0;
    double ok_max_ = 50.0;
    double warning_min_ = 50.0;
    double warning_max_ = 80.0;
    double critical_min_ = 80.0;

    /**
     * @brief Get color for current alert status
     * @return FTXUI color for the current status
     */
    ftxui::Color GetStatusColor() const;

    /**
     * @brief Generate sparkline string from value history
     * @return ASCII/Unicode sparkline representation
     */
    std::string GenerateSparkline() const;
};

#endif // GDASHBOARD_METRICS_TILE_WINDOW_HPP
