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

#ifndef GDASHBOARD_LOG_WINDOW_HPP
#define GDASHBOARD_LOG_WINDOW_HPP

#include <ncurses.h>
#include <string>
#include <deque>
#include <algorithm>

/**
 * @class LogWindow
 * @brief Scrollable log display panel for dashboard event logging
 *
 * LogWindow provides a scrollable viewing area for log messages from the
 * graph execution engine and dashboard system. Logs are displayed in a
 * ncurses window with automatic text truncation and circular buffer management.
 *
 * Key features:
 * 1. **Automatic Scrolling**: Auto-scrolls to bottom when new logs added
 * 2. **Scrolling Control**: Manual scroll up/down with arrow keys or commands
 * 3. **Buffer Management**: Circular buffer (max 1000 logs) prevents unbounded growth
 * 4. **Text Wrapping**: Long lines automatically truncated to fit window width
 * 5. **Window Resizing**: Supports dynamic height/width changes during execution
 *
 * Integration:
 * - Created as part of Dashboard's 4-panel layout (15% height by default)
 * - Receives log4cxx events via custom log appender
 * - Renders as part of Dashboard::Render() during main event loop
 *
 * Example usage:
 * ```cpp
 * // Create log window for a portion of the terminal
 * auto log_window = LogWindow(ncurses_window_ptr, width, height);
 * 
 * // Add a log entry (typically from log4cxx appender)
 * log_window.AddLog("[INFO] Graph started");
 * log_window.AddLog("[DEBUG] Processing node: Sensor");
 * 
 * // Render to display current view
 * log_window.Render();
 * 
 * // User scrolls up/down to view history
 * log_window.ScrollUp();
 * log_window.ScrollDown();
 * 
 * // Window resized during execution
 * log_window.Resize(new_height, new_width, new_y_position);
 * log_window.Render();
 * ```
 *
 * @see Dashboard, log4cxx integration
 */
class LogWindow {
private:
    WINDOW* win;
    int width, height;
    std::deque<std::string> logs;
    int scroll_y = 0;
    const int MAX_LOGS = 1000;

public:
    /**
     * @brief Construct a log window for ncurses display
     *
     * @param w The ncurses WINDOW pointer to render into
     * @param wth Width of the window in characters
     * @param hgt Height of the window in characters (including border)
     */
    LogWindow(WINDOW* w, int wth, int hgt) 
        : win(w), width(wth), height(hgt) {}

    /**
     * @brief Add a log message to the display buffer
     *
     * Adds the message to the circular buffer and auto-scrolls to show
     * the new message at the bottom of the window.
     *
     * If buffer exceeds MAX_LOGS (1000), oldest logs are discarded.
     *
     * @param line The log message text to add
     *
     * @see Render(), ScrollDown()
     */
    void AddLog(const std::string& line) {
        logs.push_back(line);
        if ((int)logs.size() > MAX_LOGS) {
            logs.pop_front();
        }
        // Auto-scroll to bottom when adding logs
        scroll_y = std::max(0, (int)logs.size() - (height - 2));
    }

    /**
     * @brief Render the log window to the ncurses display
     *
     * Clears the window, draws a border box, adds a "Logs" header,
     * and displays the visible portion of the log buffer starting
     * at the current scroll position. Lines are truncated if longer
     * than window width.
     *
     * @see AddLog(), ScrollUp(), ScrollDown()
     */
    void Render() {
        if (!win) return;
        
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, "Logs");

        int y = 1;
        for (int i = scroll_y; i < (int)logs.size() && y < height - 2; ++i, ++y) {
            // Truncate long lines to fit window
            std::string line = logs[i];
            if ((int)line.length() > width - 4) {
                line = line.substr(0, width - 4);
            }
            mvwprintw(win, y, 2, "%s", line.c_str());
        }

        wrefresh(win);
    }

    /**
     * @brief Scroll down one line in the log buffer
     *
     * Moves the view down one line if not already at the bottom.
     * Has no effect if all logs fit in the window.
     *
     * @see ScrollUp()
     */
    void ScrollDown() { 
        int max_scroll = std::max(0, (int)logs.size() - (height - 2));
        if (scroll_y < max_scroll) {
            scroll_y++;
        }
    }

    /**
     * @brief Scroll up one line in the log buffer
     *
     * Moves the view up one line if not already at the top.
     * Has no effect if already showing the first log.
     *
     * @see ScrollDown()
     */
    void ScrollUp() { 
        if (scroll_y > 0) {
            scroll_y--;
        }
    }

    /**
     * @brief Resize the log window dimensions
     *
     * Called when the terminal is resized or dashboard layout changes.
     * Updates window height and width, resizes the ncurses window,
     * and repositions it vertically.
     *
     * @param h New height in lines (including border)
     * @param w New width in characters (including border)
     * @param starty New Y position in the terminal
     *
     * @see AddLog(), Render()
     */
    void Resize(int h, int w, int starty) {
        height = h;
        width = w;
        if (win) {
            wresize(win, h, w);
            mvwin(win, starty, 0);
        }
    }
};

#endif // GDASHBOARD_LOG_WINDOW_HPP
