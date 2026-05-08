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

#ifndef GDASHBOARD_METRICS_PANEL_HPP
#define GDASHBOARD_METRICS_PANEL_HPP

#include <ncurses.h>
#include <string>
#include <vector>
#include <algorithm>
#include <ranges>
#include <cstdlib>
#include "Metric.hpp"
#include "NodeMetricsTile.hpp"
#include "core/RangesUtilities.hpp"

// ============================================================================
// Metrics Panel - Tabbed view with scrolling
// ============================================================================

/**
 * @class MetricsPanel
 * @brief Tabbed panel displaying metrics for graph execution nodes (Phase 2: ranges-optimized)
 *
 * MetricsPanel manages a collection of NodeMetricsTiles organized as tabs,
 * with one tab per node in the execution graph. Each tab displays metrics
 * in a grid layout with support for filtering and scrolling.
 *
 * Features:
 * - **Tabbed Interface**: One tab per node, switch with arrow keys
 * - **Real-time Updates**: Metrics update as data arrives from executor
 * - **Filtering**: Filter metrics by substring pattern (case-insensitive)
 * - **Scrolling**: Navigate large metric lists with scroll offset
 * - **Sparklines**: Optional Unicode sparkline visualization
 * - **Thread-Safe Updates**: Uses mutex for callback thread safety
 * - **Ranges Integration**: Modern C++20+ algorithms for metric analysis
 *
 * Update Flow:
 * 1. Graph executor publishes metrics → MetricsCapability callback
 * 2. OnMetricsEvent() routes update to SetLatestValue()
 * 3. SetLatestValue() stores value in thread-safe queue
 * 4. UpdateAllMetrics() (called from main loop) processes updates
 * 5. Render() displays current values with visual formatting
 *
 * **Phase 2 C++26 Ranges Usage**:
 * @code
 *   const auto& metrics = panel.GetTileMetrics(tab_index);
 *   
 *   // Count critical metrics using ranges
 *   auto critical_count = ranges::count_if(metrics,
 *       [](const auto& m) { return m.alert_level == 2; });
 *   
 *   // Find first warning metric
 *   auto warning = ranges::find_if(metrics,
 *       [](const auto& m) { return m.alert_level == 1; });
 *   
 *   // Filter high-confidence metrics
 *   auto confident = metrics
 *       | views::filter([](const auto& m) { return m.confidence > 0.9; })
 *       | ranges::to<std::vector>();
 * @endcode
 *
 * @see NodeMetricsTile, Metric, RangesUtilities.hpp
 *
 * @example
 *   MetricsPanel panel(window_ptr, width, height);
 *   auto schema = capability->GetNodeMetrics("Sensor");
 *   auto tile = NodeMetricsSchemaToTile("Sensor", schema);
 *   panel.AddTile(tile);
 *   
 *   // Thread-safe update from callback
 *   panel.SetLatestValue("Sensor::temperature", 42.5);
 *   
 *   // Main loop
 *   panel.UpdateAllMetrics();
 *   panel.Render();
 */
class MetricsPanel {
private:
    WINDOW* win;                          ///< ncurses window pointer
    int width, height;                    ///< Panel dimensions
    size_t selected_tab = 0;              ///< Currently selected tab index
    int scroll_y = 0;                     ///< Vertical scroll offset for metric list
    std::string filter_pattern;           ///< Current filter pattern (substring match)
    std::vector<size_t> filtered_indices; ///< Cached indices of filtered metrics
    bool filter_active = false;           ///< Whether filtering is currently enabled
    bool sparklines_enabled = true;       ///< Display sparklines below metrics

    /**
     * @brief Check if a metric name matches the current filter pattern
     *
     * Performs case-insensitive substring matching.
     *
     * @param metric_name The name of the metric to check
     * @return true if metric matches filter or filter is empty
     */
    bool MatchesFilter(const std::string& metric_name) const {
        if (filter_pattern.empty()) return true;
        std::string lower_name = metric_name;
        std::string lower_pattern = filter_pattern;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
        return lower_name.find(lower_pattern) != std::string::npos;
    }
    
    /**
     * @brief Rebuild the filtered indices cache (Phase 2: ranges-ready)
     *
     * Iterates through all metrics in the current tab and builds
     * a list of indices for metrics that match the filter pattern.
     * Called whenever filter or selected tab changes.
     *
     * **Phase 2 Modernization Note**:
     * This method is a candidate for modernization using std::ranges.
     * Future implementation using views::filter + ranges::to<vector>:
     *
     * @code
     *   void RebuildFilteredIndices() {
     *       filtered_indices.clear();
     *       if (selected_tab < tiles.size()) {
     *           const auto& metrics = tiles[selected_tab].metrics;
     *           std::vector<size_t> all_indices;
     *           for (size_t i = 0; i < metrics.size(); ++i) {
     *               all_indices.push_back(i);
     *           }
     *           
     *           // Ranges approach: filter indices based on metric names
     *           filtered_indices = all_indices
     *               | std::views::filter([this, &metrics](size_t i) {
     *                   return MatchesFilter(metrics[i].name);
     *               })
     *               | std::ranges::to<std::vector>();
     *       }
     *   }
     * @endcode
     *
     * @see MatchesFilter()
     */
    void RebuildFilteredIndices() {
        // Phase 2: Modern C++26 ranges approach for efficient filtering
        if (selected_tab >= tiles.size()) {
            filtered_indices.clear();
            return;
        }
        
        const auto& metrics = tiles[selected_tab].metrics;
        
        // Build all indices and filter using ranges::to<>
        // This is more composable and clearer than manual loop
        std::vector<size_t> all_indices;
        for (size_t i = 0; i < metrics.size(); ++i) {
            all_indices.push_back(i);
        }
        
        // Filter using ranges (lazy evaluation until collected)
        filtered_indices = all_indices
            | std::views::filter([this, &metrics](size_t i) {
                return MatchesFilter(metrics[i].name);
            })
            | std::ranges::to<std::vector>();
    }

public:
    std::vector<NodeMetricsTile> tiles;  ///< Collection of node metric tiles (tabs)
    
    /**
     * @brief Construct a MetricsPanel
     *
     * @param w ncurses window to render into
     * @param wth Panel width in characters
     * @param hgt Panel height in characters
     */
    MetricsPanel(WINDOW* w, int wth, int hgt) 
        : win(w), width(wth), height(hgt) {}

    /**
     * @brief Add a metric tile (node) as a new tab
     *
     * @param tile The NodeMetricsTile to add
     *
     * @see NodeMetricsTile
     */
    void AddTile(const NodeMetricsTile& tile) { 
        tiles.push_back(tile);
        RebuildFilteredIndices();
    }

    /**
     * @brief Apply a filter pattern to current metrics
     *
     * Only metrics whose names contain the pattern (case-insensitive)
     * will be displayed. Resets scroll position.
     *
     * @param pattern The filter pattern (substring to match)
     */
    void SetFilterPattern(const std::string& pattern) {
        filter_pattern = pattern;
        filter_active = !pattern.empty();
        scroll_y = 0;
        RebuildFilteredIndices();
    }
    
    /**
     * @brief Clear the current filter pattern
     *
     * All metrics will be displayed again.
     */
    void ClearFilter() {
        filter_pattern.clear();
        filter_active = false;
        scroll_y = 0;
        filtered_indices.clear();
    }
    
    /**
     * @brief Get the current filter pattern
     *
     * @return The active filter pattern string
     */
    std::string GetFilterPattern() const { return filter_pattern; }
    
    /**
     * @brief Check if filtering is currently active
     *
     * @return true if a filter pattern is applied
     */
    bool IsFilterActive() const { return filter_active; }

    /**
     * @brief Switch to the next tab (next node)
     *
     * Wraps around to first tab if at last tab.
     */
    void NextTab() { 
        if (!tiles.empty()) {
            selected_tab = (selected_tab + 1) % tiles.size();
            scroll_y = 0;
            RebuildFilteredIndices();
        }
    }

    void PrevTab() { 
        if (!tiles.empty()) {
            selected_tab = (selected_tab + tiles.size() - 1) % tiles.size();
            scroll_y = 0;
            RebuildFilteredIndices();
        }
    }

    void Render() {
        if (!win) return;
        
        werase(win);
        box(win, 0, 0);

        // Render tab headers
        int x = 2;
        for (size_t i = 0; i < tiles.size(); ++i) {
            if (i == selected_tab) {
                wattron(win, A_REVERSE);
            }
            mvwprintw(win, 0, x, "[%s]", tiles[i].name.c_str());
            if (i == selected_tab) {
                wattroff(win, A_REVERSE);
            }
            x += (int)tiles[i].name.size() + 3;
        }

        // Render metrics of selected tab
        if (!tiles.empty() && selected_tab < tiles.size()) {
            const auto& metrics = tiles[selected_tab].metrics;
            const auto& tile = tiles[selected_tab];
            int y = 1;
            
            // Display event type info in header
            if (!tile.event_type_counts.empty()) {
                std::string events_info = "Events: ";
                for (const auto& [evt_type, count] : tile.event_type_counts) {
                    events_info += evt_type + "(" + std::to_string(count) + ") ";
                }
                mvwprintw(win, 1, 2, "%s", events_info.c_str());
                y = 2;
            }
            
            // Display filter indicator if active
            if (filter_active) {
                std::string filter_info = "[Filter: " + filter_pattern + "] ";
                wattron(win, A_BOLD);
                mvwprintw(win, y, 2, "%s (Showing %zu/%zu metrics)", 
                    filter_info.c_str(), filtered_indices.size(), metrics.size());
                wattroff(win, A_BOLD);
                y++;
            }
            
            // Render metrics based on filter
            size_t display_count = 0;
            const auto& indices_to_use = filter_active ? filtered_indices : std::vector<size_t>();
            size_t max_items = filter_active ? filtered_indices.size() : metrics.size();
            
            for (size_t idx = 0; idx < max_items; ++idx) {
                if (display_count < (size_t)scroll_y) {
                    display_count++;
                    continue;
                }
                if (y >= height - 1) break;
                
                // Get metric index (filtered or not)
                size_t metric_idx = filter_active ? filtered_indices[idx] : idx;
                const auto& m = metrics[metric_idx];
                
                // Apply alert-level styling based on alert_level or confidence
                int attr = 0;
                int actual_alert_level = m.alert_level;
                
                // Override alert level based on confidence if metric has confidence
                if (m.confidence < 0.2) {
                    actual_alert_level = 2;  // critical
                } else if (m.confidence < 0.5) {
                    actual_alert_level = 1;  // warning
                }
                
                if (actual_alert_level == 1) {
                    attr = A_BOLD;
                    wattron(win, attr);
                } else if (actual_alert_level == 2) {
                    attr = A_BOLD | A_BLINK;
                    wattron(win, attr);
                }
                
                // Use GetFormattedValue() for type-aware rendering
                std::string display_value = m.GetFormattedValue();
                if (!m.unit.empty() && m.name.find("confidence") == std::string::npos) {
                    display_value += " " + m.unit;
                }
                
                // Add confidence indicator if applicable (but skip if it IS a confidence metric)
                std::string confidence_indicator;
                if (m.name.find("confidence") == std::string::npos && m.confidence < 1.0) {
                    int conf_percent = (int)(m.confidence * 100);
                    confidence_indicator = " [conf:" + std::to_string(conf_percent) + "%]";
                }
                
                mvwprintw(win, y, 2, "%s: %s%s", m.name.c_str(), display_value.c_str(), 
                          confidence_indicator.c_str());
                
                // Display sparkline below metric (for numeric types with history)
                if (sparklines_enabled && (m.type == MetricType::INT || m.type == MetricType::FLOAT) && m.GetHistorySize() >= 2) {
                    // Use ASCII-safe sparkline if DASHBOARD_ASCII_SPARKLINES is set
                    bool use_ascii = (getenv("DASHBOARD_ASCII_SPARKLINES") != nullptr);
                    std::string sparkline = use_ascii ? 
                        m.GenerateSparklineASCII(width - 6) : 
                        m.GenerateSparkline(width - 6);  // Leave margin for borders/indent
                    if (!sparkline.empty()) {
                        mvwprintw(win, y + 1, 4, "%s", sparkline.c_str());
                        y++;  // Sparkline takes an extra line
                    }
                }
                
                // Clean up attributes
                if (actual_alert_level >= 1) {
                    wattroff(win, attr);
                }
                
                y++;
                display_count++;
            }
        }

        wrefresh(win);
    }

    void ScrollDown() { 
        if (!tiles.empty() && selected_tab < tiles.size()) {
            const auto& display_size = filter_active ? (int)filtered_indices.size() : (int)tiles[selected_tab].metrics.size();
            int header_lines = 2 + (filter_active ? 1 : 0);
            int max_scroll = std::max(0, display_size - (height - header_lines));
            if (scroll_y < max_scroll) {
                scroll_y++;
            }
        }
    }

    void ScrollUp() { 
        if (scroll_y > 0) {
            scroll_y--;
        }
    }

    void Resize(int h, int w, int starty) {
        height = h;
        width = w;
        if (win) {
            wresize(win, h, w);
            mvwin(win, starty, 0);
        }
    }

    size_t GetTileCount() const { return tiles.size(); }
    
    void ToggleSparklines() { 
        sparklines_enabled = !sparklines_enabled;
    }
    
    bool AreSparklesEnabled() const { 
        return sparklines_enabled;
    }
};

#endif // GDASHBOARD_METRICS_PANEL_HPP
