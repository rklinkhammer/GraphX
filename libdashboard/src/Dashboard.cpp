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

/**
 * @file Dashboard.cpp
 * @brief Implementation of the interactive ncurses dashboard UI
 *
 * This file implements the Dashboard class, which manages the real-time
 * metrics visualization and command input for the graph execution engine.
 *
 * Key Implementation Details:
 * 1. **Terminal Setup**: Uses ncurses with UTF-8 locale support
 * 2. **Window Layout**: Creates 4 ncurses windows (metrics, logs, commands, status)
 * 3. **Panel Management**: Stacks panels in Z-order for proper rendering
 * 4. **30 FPS Event Loop**: Main thread runs at 30 FPS, processes keyboard input
 * 5. **Thread-Safe Metrics**: Mutex protects latest_values_ from callback thread updates
 * 6. **Command Execution**: Routes keyboard input to CommandRegistry
 * 7. **Graceful Shutdown**: Restores terminal state on exit
 *
 * Threading Model:
 * - Dashboard runs on main thread (UI thread)
 * - Metrics callbacks arrive from executor thread
 * - SetLatestValue() uses mutex to safely queue metric updates
 * - UpdateAllMetrics() processes queue without blocking callbacks
 * - No blocking operations in 30 FPS render loop
 *
 * File-level Responsibilities:
 * - Terminal initialization and cleanup (initscr, endwin)
 * - Window creation and resizing
 * - Keyboard event handling and dispatching
 * - Metrics panel updates from callbacks
 * - Status bar updates and logging
 */

#include "app/SignalHandler.hpp"
#include "ui/Dashboard.hpp"
#include <signal.h>
#include <locale.h>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>

/**
 * @brief Initialize the dashboard after construction
 *
 * Sets up the ncurses terminal, creates windows and panels, and subscribes
 * to metrics events. Must be called before Run().
 *
 * @return True if initialization succeeded, false on error
 *
 * @note Sets up terminal for UTF-8, creates 4-panel layout
 * @note Registers this dashboard as metrics subscriber
 * @note Minimum terminal size: 10 rows x 40 columns
 */
bool Dashboard::Initialize() {
    if (!SetupTerminal()) return false;
    if (!CreateWindows()) return false;
    if (!CreatePanels()) return false;
    
    // Clear and refresh standard screen
    erase();
    refresh();
    
    // Initialize last_update for metric callback simulation
    last_update_ = std::chrono::steady_clock::now();
    
    if(metrics_capability_) {
        auto schemas = metrics_capability_->GetNodeMetricsSchemas();
        for(const auto& schema : schemas) {
            auto tile = NodeMetricsSchemaToTileWithMetrics(schema);
            AddMetricsTile(tile);
        }
        metrics_capability_->RegisterMetricsCallback(this);
    
    }
    app::SignalHandler::GetInstance().RegisterScreenResize([this]() {
        this->SetResizeSignal(true);
    });
    status_bar->SetText("F1=Quit | Tab=Next Tab | Shift+Tab=Prev Tab | Arrows=Scroll");
    return true;
}

/**
 * @brief Set up ncurses terminal with proper locale and configuration
 *
 * Initializes ncurses mode, sets UTF-8 locale for Unicode support,
 * configures keyboard input, and enables color mode.
 *
 * @return True if setup succeeded, false if terminal too small or other error
 *
 * @note Validates minimum terminal size (10x40)
 * @note Enables raw input mode (cbreak) without local echo
 * @note Enables color support for future color-coded metrics
 *
 * @see Initialize, CreateWindows
 */
bool Dashboard::SetupTerminal() {
    // Use user's locale (should be UTF-8 if environment is set)
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();

    timeout(100);          // tick every 100ms
    keypad(stdscr, TRUE);

    curs_set(1);           // visible, but you must wmove() in CommandWindow::Render()

    if (has_colors()) {
        start_color();
        use_default_colors(); // optional
    }

    clear();
    refresh();

    getmaxyx(stdscr, term_h, term_w);

    if (term_h < 10 || term_w < 40) {
        endwin();
        fprintf(stderr, "Terminal too small. Minimum: 10x40\n");
        return false;
    }

    return true;
}

bool Dashboard::CreateWindows()
{
    // Calculate heights based on percentages
    // Metrics: 50%, Logs: 25%, Command: 20%, Status: 5%
    int metrics_h = std::max(3, term_h / 2);
    int log_h = std::max(3, term_h / 4);
    int cmd_h = std::max(3, (term_h * 20) / 100);
    int status_h = std::max(1, term_h - (metrics_h + log_h + cmd_h));

    metrics_win = newwin(metrics_h, term_w, 0, 0);
    log_win = newwin(log_h, term_w, metrics_h, 0);
    cmd_win = newwin(cmd_h, term_w, metrics_h + log_h, 0);
    status_win = newwin(status_h, term_w, metrics_h + log_h + cmd_h, 0);

    if (!metrics_win || !log_win || !cmd_win || !status_win)
    {
        if (metrics_win)
            delwin(metrics_win);
        if (log_win)
            delwin(log_win);
        if (cmd_win)
            delwin(cmd_win);
        if (status_win)
            delwin(status_win);
        metrics_win = log_win = cmd_win = status_win = nullptr;
        return false;
    }

    leaveok(metrics_win, TRUE);
    leaveok(log_win, TRUE);
    leaveok(status_win, TRUE);
    leaveok(cmd_win, FALSE);

    return true;
}

bool Dashboard::CreatePanels() {
    int metrics_h = term_h / 2;
    int log_h = term_h / 4;
    int cmd_h = (term_h * 20) / 100;

    metrics_panel = new MetricsPanel(metrics_win, term_w, metrics_h);
    log_window = new LogWindow(log_win, term_w, log_h);
    command_window = new CommandWindow(cmd_win, term_w, cmd_h);
    status_bar = new StatusBar(status_win, term_w, 1);

    return (metrics_panel && log_window && command_window && status_bar);
}

void Dashboard::AddMetricsTile(const NodeMetricsTile& tile) {
    if (metrics_panel) {
        metrics_panel->AddTile(tile);
    }
}

void Dashboard::AddLog(const std::string& line) {
    if (log_window) {
        log_window->AddLog(line);
    }
}

void Dashboard::OnMetricsEvent(const app::metrics::MetricsEvent& event) {
    // Find or create tile for this source
    NodeMetricsTile* tile = nullptr;
    if (metrics_panel) {
        for (auto& t : metrics_panel->tiles) {
            if (t.name == event.source) {
                tile = &t;
                break;
            }
        }
        
        if (!tile) {
            // Create new tile for this source
            NodeMetricsTile new_tile(event.source);
            metrics_panel->AddTile(new_tile);
            tile = &metrics_panel->tiles.back();
        }
    }
    
    if (!tile) return;
    
    // Record the event type
    tile->RecordEvent(event.event_type);
    
    // Update metrics from event data
    for (const auto& [key, value_str] : event.data) {
        // Find or create metric
        Metric* metric = nullptr;
        for (auto& m : tile->metrics) {
            if (m.name == key) {
                metric = &m;
                break;
            }
        }
        
        if (!metric) {
            // Create new metric
            Metric new_metric;
            new_metric.name = key;
            new_metric.event_type = event.event_type;
            tile->metrics.push_back(new_metric);
            metric = &tile->metrics.back();
        }
        
        // Parse and update value
        metric->last_update = event.timestamp;
        metric->event_type = event.event_type;
        
        try {
            // Try to parse as different types
            if (value_str == "true" || value_str == "false") {
                metric->type = MetricType::BOOL;
                metric->value = (value_str == "true");
                metric->AddToHistory((value_str == "true") ? 1.0 : 0.0);
            } else if (value_str.find('.') != std::string::npos) {
                metric->type = MetricType::FLOAT;
                double numeric_val = std::stod(value_str);
                metric->value = numeric_val;
                metric->AddToHistory(numeric_val);
                
                // Extract confidence if metric name contains "confidence"
                if (key.find("confidence") != std::string::npos) {
                    metric->confidence = numeric_val;
                    // Auto-set alert level based on confidence
                    if (metric->confidence < 0.2) {
                        metric->alert_level = 2;  // critical
                    } else if (metric->confidence < 0.5) {
                        metric->alert_level = 1;  // warning
                    } else {
                        metric->alert_level = 0;  // ok
                    }
                }
            } else {
                // Try int first, otherwise string
                try {
                    metric->type = MetricType::INT;
                    int int_val = std::stoi(value_str);
                    metric->value = int_val;
                    metric->AddToHistory(static_cast<double>(int_val));
                } catch (...) {
                    metric->type = MetricType::STRING;
                    metric->value = value_str;
                    // Try to parse as number for history
                    try {
                        metric->AddToHistory(std::stod(value_str));
                    } catch (...) {
                        // Skip history for non-numeric strings
                    }
                }
            }
        } catch (...) {
            // Fallback to string
            metric->type = MetricType::STRING;
            metric->value = value_str;
        }
    }
}

void Dashboard::HandleInput(int ch) {
    if (!command_window) return;
    if (ch == ERR) return;

    switch (ch) {
        case '\t':
            if (metrics_panel) metrics_panel->NextTab();
            break;
        case KEY_BTAB:
            if (metrics_panel) metrics_panel->PrevTab();
            break;
        case KEY_UP:
            if (metrics_panel) metrics_panel->ScrollUp();
            if (log_window) log_window->ScrollUp();
            break;
        case KEY_DOWN:
            if (metrics_panel) metrics_panel->ScrollDown();
            if (log_window) log_window->ScrollDown();
            break;
        case 10: // Enter
        {
            const std::string cmd = command_window->GetCommand();
            if (!cmd.empty()) {
                AddLog("> " + cmd);
                command_window->AddHistory(cmd);
                dashboard_capability_->EnqueueCommand(cmd);
                command_window->ClearBuffer();
            }
            break;
        }
        case 8: // Ctrl-H on some terminals
        case KEY_BACKSPACE:
        case 127:
            command_window->DeleteChar();
            break;

        case KEY_RESIZE:
            HandleResize();
            break;

        default:
            if (ch >= 0 && ch <= 255 &&
                isprint(static_cast<unsigned char>(ch))) {
                command_window->AddChar(static_cast<char>(ch));
            }
            break;
    }
}


// void Dashboard::ExecuteCommand(const std::string& cmd) {
//     // Split input into command and arguments
//     std::istringstream iss(cmd);
//     std::string command_name;
//     iss >> command_name;
    
//     std::vector<std::string> args;
//     std::string arg;
//     while (iss >> arg) {
//         args.push_back(arg);
//     }
        
//     if (command_name == "help") {
//         if (!registry_) {
//             AddLog("[ERROR] Command registry not initialized");
//             return;
//         }
//         registry_->GenerateHelpText(this);
//         return;
//     }
    
//     // Execute via registry if available
//     if (!registry_) {
//         AddLog("[ERROR] Command registry not initialized");
//         return;
//     }
    
//     CommandResult result = registry_->ExecuteCommand(command_name, args);
//     if (!result.success) {
//         AddLog("[ERROR] " + result.message);
//     }
// }

void Dashboard::UpdateStatusBarWithFilter() {
    std::string status_text = "F1=Quit | Tab=Next Tab | Shift+Tab=Prev Tab | Arrows=Scroll";
    if (filter_active_) {
        status_text += " | Filter: " + filter_pattern_;
    }
    if (status_bar) {
        status_bar->SetText(status_text);
    }
}

void Dashboard::Render() {
    if (metrics_panel) metrics_panel->Render();
    if (log_window) log_window->Render();
    if (command_window) command_window->Render();
    if (status_bar) status_bar->Render();
}

void Dashboard::HandleResize() {
    endwin();
    refresh();
    clear();

    int new_h, new_w;
    getmaxyx(stdscr, new_h, new_w);
    term_h = new_h;
    term_w = new_w;

    int metrics_h = term_h / 2;
    int log_h = term_h / 4;
    int cmd_h = (term_h * 20) / 100;
    int status_h = std::max(1, term_h - (metrics_h + log_h + cmd_h));

    if (metrics_panel) metrics_panel->Resize(metrics_h, term_w, 0);
    if (log_window) log_window->Resize(log_h, term_w, metrics_h);
    if (command_window) command_window->Resize(cmd_h, term_w, metrics_h + log_h);
    if (status_bar) status_bar->Resize(status_h, term_w, metrics_h + log_h + cmd_h);

    Render();
}

void Dashboard::Cleanup() {
    delete metrics_panel;
    delete log_window;
    delete command_window;
    delete status_bar;

    if (metrics_win) delwin(metrics_win);
    if (log_win) delwin(log_win);
    if (cmd_win) delwin(cmd_win);
    if (status_win) delwin(status_win);

    endwin();
}

void Dashboard::Run() {
    int ch;
    while ((ch = getch()) != KEY_F(1) && !graph_capability_->IsStopped()) { // F1 to quit
        
        HandleInput(ch);
        
        // Check for resize
        if (ch == KEY_RESIZE || GetResizeSignal()) {
            HandleResize();
            SetResizeSignal(false);
        }

        Render();
    }
    graph_capability_->SetStopped();
}
