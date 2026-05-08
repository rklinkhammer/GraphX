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

#include "ui/MetricsTileWindow.hpp"
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

MetricsTileWindow::MetricsTileWindow(const MetricDescriptor& descriptor, const json& field_spec)
    : descriptor_(descriptor), field_spec_(field_spec) {
    ParseAlertThresholds();
}

std::string MetricsTileWindow::GetMetricId() const {
    return descriptor_.metric_id;
}

std::string MetricsTileWindow::GetDisplayType() const {
    if (field_spec_.contains("display_type")) {
        return field_spec_["display_type"].get<std::string>();
    }
    return "gauge";
}

AlertStatus MetricsTileWindow::GetStatus() const {
    return current_status_;
}

void MetricsTileWindow::UpdateValue(double value) {
    current_value_ = value;

    // Add to history
    value_history_.push_back(value);
    if (value_history_.size() > HISTORY_SIZE) {
        value_history_.pop_front();
    }

    // Update status based on new value
    UpdateStatus();
}

void MetricsTileWindow::UpdateStatus() {
    // Check critical first (highest priority) - exclusive lower bound
    if (current_value_ > critical_min_) {
        current_status_ = AlertStatus::CRITICAL;
    }
    // Check OK range (inclusive on both ends)
    else if (current_value_ >= ok_min_ && current_value_ <= ok_max_) {
        current_status_ = AlertStatus::OK;
    }
    // Check warning range (exclusive on lower end since OK is inclusive)
    else if (current_value_ > ok_max_ && current_value_ <= critical_min_) {
        current_status_ = AlertStatus::WARNING;
    }
    // Default to OK if below range
    else {
        current_status_ = AlertStatus::OK;
    }
}

void MetricsTileWindow::ParseAlertThresholds() {
    if (!field_spec_.contains("alert_rule")) {
        return;
    }

    const auto& alert_rule = field_spec_["alert_rule"];

    // Parse OK range
    if (alert_rule.contains("ok") && alert_rule["ok"].is_array() && alert_rule["ok"].size() >= 2) {
        ok_min_ = alert_rule["ok"][0].get<double>();
        ok_max_ = alert_rule["ok"][1].get<double>();
    }

    // Parse WARNING range
    if (alert_rule.contains("warning") && alert_rule["warning"].is_array() && alert_rule["warning"].size() >= 2) {
        warning_min_ = alert_rule["warning"][0].get<double>();
        warning_max_ = alert_rule["warning"][1].get<double>();
    }

    // Parse CRITICAL threshold
    if (alert_rule.contains("critical_min")) {
        critical_min_ = alert_rule["critical_min"].get<double>();
    }
}

ftxui::Color MetricsTileWindow::GetStatusColor() const {
    switch (current_status_) {
        case AlertStatus::OK:
            return ftxui::Color::Green;
        case AlertStatus::WARNING:
            return ftxui::Color::Yellow;
        case AlertStatus::CRITICAL:
            return ftxui::Color::Red;
        default:
            return ftxui::Color::White;
    }
}

std::string MetricsTileWindow::GenerateSparkline() const {
    if (value_history_.size() < 2) {
        return "";
    }

    // Sparkline characters (8 levels)
    static constexpr const char* kBars[8] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    // Determine display range (up to 30 chars)
    size_t display_count = std::min(size_t(30), value_history_.size());
    size_t start_idx = value_history_.size() - display_count;

    // Find min/max in range
    double min_val = value_history_[start_idx];
    double max_val = value_history_[start_idx];
    for (size_t i = start_idx; i < value_history_.size(); ++i) {
        min_val = std::min(min_val, value_history_[i]);
        max_val = std::max(max_val, value_history_[i]);
    }

    // Handle flat case
    double range = max_val - min_val;
    if (range < 0.0001) {
        std::string result;
        for (size_t i = 0; i < display_count; ++i) {
            result += "▄";
        }
        return result;
    }

    // Generate sparkline
    std::string result;
    for (size_t i = start_idx; i < value_history_.size(); ++i) {
        double normalized = (value_history_[i] - min_val) / range;
        int level = static_cast<int>(normalized * 7.0);
        level = std::min(7, level);
        result += kBars[level];
    }

    return result;
}

ftxui::Element MetricsTileWindow::Render() const {
    using namespace ftxui;

    // Format current value
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << current_value_;
    std::string value_str = oss.str();

    // Generate sparkline
    std::string sparkline = GenerateSparkline();

    // Build the element
    std::vector<Element> elements;

    // Title with metric name
    elements.push_back(text(descriptor_.metric_name) | bold);

    // Current value with color and unit
    std::string value_text = value_str + " " + descriptor_.unit;
    elements.push_back(
        text(value_text) | color(GetStatusColor())
    );

    // Status indicator
    std::string status_str;
    switch (current_status_) {
        case AlertStatus::OK:
            status_str = "OK";
            break;
        case AlertStatus::WARNING:
            status_str = "WARNING";
            break;
        case AlertStatus::CRITICAL:
            status_str = "CRITICAL";
            break;
    }
    elements.push_back(
        text(status_str) | color(GetStatusColor())
    );

    // Sparkline if available
    if (!sparkline.empty()) {
        elements.push_back(text(sparkline));
    }

    // Combine all elements vertically
    return vbox(elements);
}
