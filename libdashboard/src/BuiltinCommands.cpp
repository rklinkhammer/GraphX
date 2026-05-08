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

#include "app/capabilities/CSVDataInjectionCapability.hpp"
#include "ui/BuiltinCommands.hpp"
#include "ui/Dashboard.hpp"
#include "ui/MetricsPanel.hpp"
#include "ui/StatusBar.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>

namespace commands {

    CommandResult cmd::CmdStatus(Dashboard *app, const std::vector<std::string> & /* args */)
    {
        if (!app)
        {
            return CommandResult(false, "Application context unavailable");
        }

        auto metrics_panel = app->GetMetricsPanel();
        // auto logging_window = app->GetLoggingWindow();
        // auto command_window = app->GetCommandWindow();
        // auto status_bar = app->GetStatusBar();

        app->AddLog("Dashboard Status:");
        if (metrics_panel)
        {
            app->AddLog("  Tiles: " + std::to_string(metrics_panel->GetTileCount()));
            if (app->GetFilterActive())
            {
                app->AddLog("  Filter: " + app->GetFilterPattern());
            }
            else
            {
                app->AddLog("  Filter: None");
            }
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdListMetrics(Dashboard *app, const std::vector<std::string> & args)
    {
        if (!app)
        {
            return CommandResult(false, "Application context unavailable");
        }
        auto metrics_panel = app->GetMetricsPanel();
        std::string node_filter = args.empty() ? "" : args[0];

        bool found_any = false;
        if (metrics_panel)
        {
            for (const auto &tile : metrics_panel->tiles)
            {
                if (!node_filter.empty() && tile.name.find(node_filter) == std::string::npos)
                {
                    continue; // Skip if filter doesn't match
                }

                app->AddLog(tile.name + " (" + std::to_string(tile.metrics.size()) + " metrics):");
                for (const auto &metric : tile.metrics)
                {
                    std::string type_str = MetricTypeToString(metric.type);
                    app->AddLog("  " + metric.name + " (" + type_str + "): " + metric.GetFormattedValue());
                }
                found_any = true;
            }
        }
        if (!found_any)
        {
            app->AddLog("No metrics found" + (node_filter.empty() ? "" : " matching: " + node_filter));
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdToggleSparklines(Dashboard *app, const std::vector<std::string> & /* args */)
    {
        if (!app)
        {
            return CommandResult(false, "Application context unavailable");
        }
        auto metrics_panel = app->GetMetricsPanel();

        if (metrics_panel)
        {
            metrics_panel->ToggleSparklines();
            std::string status = metrics_panel->AreSparklesEnabled() ? "enabled" : "disabled";
            app->AddLog("Sparklines " + status);
        }
        else
        {
            app->AddLog("No metrics panel available");
        }

        return CommandResult(true);
    }

    CommandResult cmd::CmdClearFilter(Dashboard *app, const std::vector<std::string> &)
    {
        if (!app)
        {
            return CommandResult(false, "Application context unavailable");
        }

        auto metrics_panel = app->GetMetricsPanel();
        if (metrics_panel)
        {
            metrics_panel->ClearFilter();
            app->ClearFilter();
            app->AddLog("Filter cleared");
            app->UpdateStatusBarWithFilter();
        }

        return CommandResult(true);
    }

    CommandResult cmd::CmdFilterMetrics(Dashboard *app, const std::vector<std::string> &args)
    {
        auto metrics_panel = app->GetMetricsPanel();
        std::string pattern;
        if(args.size() >= 1)
        {
            pattern = args[0];
            if (metrics_panel)
            {
                metrics_panel->SetFilterPattern(pattern);
                app->SetFilterPattern(pattern);
                app->AddLog("Filter applied: " + pattern);
                app->UpdateStatusBarWithFilter();
            }
        }
        else
        {
            app->AddLog("Usage: filter_metrics <pattern>");
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdSetConfidence(Dashboard *app, const std::vector<std::string> &args)
    {
        std::string node_name, metric_name, value_str;
        if (args.size() >= 3)
        {
            node_name = args[0];
            metric_name = args[1];
            value_str = args[2];
            try
            {
                double conf_value = std::stod(value_str);

                // Clamp to 0.0-1.0 range
                if (conf_value < 0.0)
                    conf_value = 0.0;
                if (conf_value > 1.0)
                    conf_value = 1.0;

                auto metrics_panel = app->GetMetricsPanel();

                // Find and update metric
                bool found = false;
                if (metrics_panel)
                {
                    for (auto &tile : metrics_panel->tiles)
                    {
                        if (tile.name == node_name)
                        {
                            for (auto &metric : tile.metrics)
                            {
                                if (metric.name == metric_name)
                                {
                                    metric.confidence = conf_value;
                                    // Update alert level based on new confidence
                                    if (conf_value < 0.2)
                                    {
                                        metric.alert_level = 2; // critical
                                    }
                                    else if (conf_value < 0.5)
                                    {
                                        metric.alert_level = 1; // warning
                                    }
                                    else
                                    {
                                        metric.alert_level = 0; // ok
                                    }
                                    found = true;
                                    app->AddLog("Confidence updated: " + node_name + "::" + metric_name +
                                                " = " + std::to_string((int)(conf_value * 100)) + "%");
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                if (!found)
                {
                    app->AddLog("Metric not found: " + node_name + "::" + metric_name);
                }
            }
            catch (...)
            {
                app->AddLog("Invalid confidence value: " + value_str + " (use 0.0-1.0)");
            }
        }
        else
        {
            app->AddLog("Usage: set_confidence <node> <metric> <value>");
            app->AddLog("  Example: set_confidence EstimationPipelineNode altitude_confidence 0.75");
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdShowEvents(Dashboard *app, const std::vector<std::string> &)
    {
        // Show event type counts and statistics
        auto metrics_panel = app->GetMetricsPanel();

        if (metrics_panel)
        {
            app->AddLog("Event Statistics:");
            int total_events = 0;
            for (const auto &tile : metrics_panel->tiles)
            {
                if (!tile.event_type_counts.empty())
                {
                    app->AddLog(tile.name + ":");
                    for (const auto &[evt_type, count] : tile.event_type_counts)
                    {
                        app->AddLog("  " + evt_type + ": " + std::to_string(count) + " updates");
                        total_events += count;
                    }
                }
            }
            app->AddLog("Total events: " + std::to_string(total_events));
        }
        else
        {
            app->AddLog("No metrics available");
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdShowHistory(Dashboard *app, const std::vector<std::string> &args)
    {
        // Display metric history with statistics
        auto metrics_panel = app->GetMetricsPanel();

        std::string path_str = args.empty() ? "" : args[0];

        if (path_str.empty())
        {
            app->AddLog("Usage: show_history <node>::<metric>");
            app->AddLog("  Example: show_history EstimationPipelineNode::altitude_estimate_m");
        }
        else
        {
            // Parse node::metric path
            size_t delim = path_str.find("::");
            if (delim == std::string::npos)
            {
                app->AddLog("Error: Use format node::metric (e.g., EstimationPipelineNode::altitude_estimate_m)");
            }
            else
            {
                std::string node_name = path_str.substr(0, delim);
                std::string metric_name = path_str.substr(delim + 2);

                bool found = false;
                if (metrics_panel)
                {
                    for (auto &tile : metrics_panel->tiles)
                    {
                        if (tile.name == node_name)
                        {
                            for (auto &metric : tile.metrics)
                            {
                                if (metric.name == metric_name)
                                {
                                    found = true;

                                    // Display history information
                                    app->AddLog("History for " + node_name + "::" + metric_name);

                                    if (metric.GetHistorySize() == 0)
                                    {
                                        app->AddLog("  No history data yet");
                                    }
                                    else
                                    {
                                        app->AddLog("  Current value: " + metric.GetFormattedValue());
                                        app->AddLog("  History entries: " + std::to_string(metric.GetHistorySize()) + "/" +
                                                    std::to_string(Metric::HISTORY_SIZE));

                                        // Display statistics for numeric metrics
                                        if (metric.type == MetricType::INT || metric.type == MetricType::FLOAT)
                                        {
                                            double min_val = metric.GetHistoryMin();
                                            double max_val = metric.GetHistoryMax();
                                            double avg_val = metric.GetHistoryAvg();

                                            std::ostringstream oss;
                                            oss << std::fixed << std::setprecision(3);

                                            app->AddLog("  Min: " + oss.str() + std::to_string(min_val) +
                                                        (metric.unit.empty() ? "" : " " + metric.unit));

                                            oss.str("");
                                            oss.clear();
                                            oss << std::fixed << std::setprecision(3) << max_val;
                                            app->AddLog("  Max: " + oss.str() +
                                                        (metric.unit.empty() ? "" : " " + metric.unit));

                                            oss.str("");
                                            oss.clear();
                                            oss << std::fixed << std::setprecision(3) << avg_val;
                                            app->AddLog("  Avg: " + oss.str() +
                                                        (metric.unit.empty() ? "" : " " + metric.unit));

                                            // Simple trend indicator
                                            if (metric.value_history.size() >= 2)
                                            {
                                                double first = metric.value_history.front();
                                                double last = metric.value_history.back();
                                                if (last > first * 1.01)
                                                {
                                                    app->AddLog("  Trend: /\\ Rising");
                                                }
                                                else if (last < first * 0.99)
                                                {
                                                    app->AddLog("  Trend: \\/ Falling");
                                                }
                                                else
                                                {
                                                    app->AddLog("  Trend: -- Stable");
                                                }
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
                if (!found)
                {
                    app->AddLog("Metric not found: " + node_name + "::" + metric_name);
                }
            }
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdSetMetric(Dashboard *app, const std::vector<std::string> &args)
    {
        auto metrics_panel = app->GetMetricsPanel();
        // Manual metric update: set_metric <node>::<metric> <value>
        std::string path_str, value_str;
        if (args.size() >= 2)
        {
            path_str = args[0];
            value_str = args[1];
        }
        else
        {
            app->AddLog("Usage: set_metric <node>::<metric> <value>");
            app->AddLog("  Example: set_metric EstimationPipelineNode::altitude_estimate_m 1550.5");
            return CommandResult(true);
        }
        // Parse node::metric path
        size_t delim = path_str.find("::");
        if (delim == std::string::npos)
        {
            app->AddLog("Error: Use format node::metric (e.g., EstimationPipelineNode::altitude_estimate_m)");
        }
        else
        {
            std::string node_name = path_str.substr(0, delim);
            std::string metric_name = path_str.substr(delim + 2);

            bool found = false;
            if (metrics_panel)
            {
                for (auto &tile : metrics_panel->tiles)
                {
                    if (tile.name == node_name)
                    {
                        for (auto &metric : tile.metrics)
                        {
                            if (metric.name == metric_name)
                            {
                                try
                                {
                                    // Try to parse as different types
                                    if (value_str == "true" || value_str == "false")
                                    {
                                        metric.type = MetricType::BOOL;
                                        metric.value = (value_str == "true");
                                    }
                                    else if (value_str.find('.') != std::string::npos)
                                    {
                                        metric.type = MetricType::FLOAT;
                                        metric.value = std::stod(value_str);
                                    }
                                    else
                                    {
                                        metric.type = MetricType::INT;
                                        metric.value = std::stoi(value_str);
                                    }
                                    metric.last_update = std::chrono::system_clock::now();
                                    app->AddLog("Updated: " + node_name + "::" + metric_name + " = " + value_str);
                                    found = true;
                                }
                                catch (...)
                                {
                                    app->AddLog("Error parsing value: " + value_str);
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            if (!found)
            {
                app->AddLog("Metric not found: " + node_name + "::" + metric_name);
            }
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdClearMetrics(Dashboard *app, const std::vector<std::string> &)
    {
        auto metrics_panel = app->GetMetricsPanel();

        // Clear all metrics from all tiles
        if (metrics_panel)
        {
            int cleared_count = 0;
            for (auto &tile : metrics_panel->tiles)
            {
                cleared_count += tile.metrics.size();
                tile.metrics.clear();
                tile.event_type_counts.clear();
            }
            app->AddLog("Cleared " + std::to_string(cleared_count) + " metrics from " +
                        std::to_string(metrics_panel->GetTileCount()) + " nodes");
        }
        else
        {
            app->AddLog("No metrics to clear");
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdExportMetrics(Dashboard *app, const std::vector<std::string> &args)
    {
        auto metrics_panel = app->GetMetricsPanel();

        // Export metrics to JSON or CSV format
        std::string format;
        if (args.size() >= 1)
        {
            format = args[0];
        }
        else
        {
            format = "json"; // Default format
        }

        if (metrics_panel && !metrics_panel->tiles.empty())
        {
            if (format == "json")
            {
                app->AddLog("{\"metrics\": [");
                bool first = true;
                for (const auto &tile : metrics_panel->tiles)
                {
                    for (const auto &metric : tile.metrics)
                    {
                        if (!first)
                            app->AddLog(",");
                        app->AddLog("  {\"node\": \"" + tile.name + "\", \"name\": \"" + metric.name +
                                    "\", \"value\": \"" + metric.GetFormattedValue() + "\"}");
                        first = false;
                    }
                }
                app->AddLog("]}");
                app->AddLog("Exported " + std::to_string(metrics_panel->GetTileCount()) + " nodes to JSON");
            }
            else if (format == "csv")
            {
                app->AddLog("node,metric,value,type,confidence");
                for (const auto &tile : metrics_panel->tiles)
                {
                    for (const auto &metric : tile.metrics)
                    {
                        std::string type_str = MetricTypeToString(metric.type);
                        int conf_percent = (int)(metric.confidence * 100);
                        app->AddLog(tile.name + "," + metric.name + "," + metric.GetFormattedValue() +
                                    "," + type_str + "," + std::to_string(conf_percent) + "%");
                    }
                }
                app->AddLog("Exported " + std::to_string(metrics_panel->GetTileCount()) + " nodes to CSV");
            }
            else
            {
                app->AddLog("Unknown format: " + format + " (use 'json' or 'csv')");
            }
        }
        else
        {
            app->AddLog("No metrics to export");
        }
        return CommandResult(true);
    }

    CommandResult cmd::CmdCsvStep(Dashboard *app, const std::vector<std::string> &args, 
        std::shared_ptr<app::capabilities::GraphCapability> graph_capability)
    {
 
        int nrows = 1;
        if (args.size() >= 1)
        {
            nrows = std::stoi(args[0]);
            if (nrows < 1)
                nrows = 1;  
            
        }
        auto csv_capability = graph_capability->GetCapabilityBus().Get<app::capabilities::CSVDataInjectionCapability>();
        if (!csv_capability)
        {
            app->AddLog("CSV Data Injection Capability not available");
            return CommandResult(false, "CSV Capability unavailable");
        }
        app::capabilities::CSVDataInjectionCommand csv_command(nrows);
        csv_capability->EnqueueCommand(csv_command);

        return CommandResult(true);
    }

    void RegisterBuiltinCommands(
        std::shared_ptr<CommandRegistry> registry,
        Dashboard *app,
        std::shared_ptr<app::capabilities::GraphCapability> graph_capability)
    {

        if (!registry || !app)
        {
            std::cerr << "[BuiltinCommands] Error: Invalid registry or app pointer\n";
            return;
        }

        // Status command - display current dashboard state
        registry->RegisterCommand(
            "status",
            "Display current dashboard status",
            "status",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdStatus(app, args);
            });

        // Run graph command - execute the computation graph
        registry->RegisterCommand(
            "list_metrics",
            "List all metrics",
            "list_metrics",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdListMetrics(app, args);
            });

        // Pause graph command
        registry->RegisterCommand(
            "toggle_sparklines",
            "Toggle the display of sparklines in metrics",
            "toggle_sparklines",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdToggleSparklines(app, args);
            });

        // Clear filter command
        registry->RegisterCommand(
            "clear_filter",
            "Clear any active metric filters",
            "clear_filter",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdClearFilter(app, args);
            });
        // Set Confidence command
        registry->RegisterCommand(
            "set_confidence",
            "Set confidence value for a specific metric",
            "set_confidence <node> <metric> <value> [0.0-1.0]",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdSetConfidence(app, args);
            });

        // Show Events
        registry->RegisterCommand(
            "show_events",
            "Show event type counts and statistics",
            "show_events",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdShowEvents(app, args);
            });

        // Show History
        registry->RegisterCommand(
            "show_history",
            "Display metric history with statistics",
            "show_history <node>::<metric>",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdShowHistory(app, args);
            });

        // Set Metric
        registry->RegisterCommand(
            "set_metric",
            "Manually update a metric value",
            "set_metric <node>::<metric> <value>",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdSetMetric(app, args);
            });

        // Clear Metrics
        registry->RegisterCommand(
            "clear_metrics",
            "Clear all metrics from all tiles",
            "clear_metrics",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdClearMetrics(app, args);
            });
        // Export Metrics
        registry->RegisterCommand(
            "export_metrics",
            "Export metrics to JSON or CSV format",
            "export_metrics [json|csv]",
            [app](const std::vector<std::string> &args)
            {
                return cmd::CmdExportMetrics(app, args);
            });
        // CSV Step
        registry->RegisterCommand(
            "step",
            "Step through CSV data",
            "csv [nrows]",
            [app, graph_capability](const std::vector<std::string> &args)
            {
                return cmd::CmdCsvStep(app, args, graph_capability);
            });
        
    }

} // namespace commands
