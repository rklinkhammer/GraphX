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

#ifndef GDASHBOARD_STATUS_BAR_HPP
#define GDASHBOARD_STATUS_BAR_HPP

#include <ncurses.h>
#include <string>
#include <algorithm>

/**
 * @class StatusBar
 * @brief Minimal single-line status indicator at the bottom of dashboard
 *
 * StatusBar displays brief status information at the very bottom of the dashboard
 * using reverse video (inverted colors) for visual distinction. Typically shows
 * the current execution state, error messages, or user feedback.
 *
 * Key features:
 * 1. **Reverse Video**: Displays with inverted colors for prominence
 * 2. **Single Line**: Minimal height (2% of dashboard by default)
 * 3. **Text Truncation**: Automatically truncates text to fit window width
 * 4. **Status Updates**: Text can be updated during execution
 *
 * Integration:
 * - Created as part of Dashboard's 4-panel layout (2% height)
 * - Bottom-most panel for constant visibility
 * - Updated by Dashboard::ExecuteCommand() or other status events
 * - Renders as part of Dashboard::Render() during main event loop
 *
 * Example usage:
 * ```cpp
 * // Create status bar for the bottom of the terminal
 * auto status_bar = StatusBar(ncurses_window_ptr, width, height);
 * 
 * // Update status text
 * status_bar.SetText("Status: Running");
 * status_bar.Render();
 * 
 * // Update with different state
 * status_bar.SetText("Status: Paused");
 * status_bar.Render();
 * 
 * // Window resized
 * status_bar.Resize(new_height, new_width, new_y_position);
 * status_bar.Render();
 * ```
 *
 * @see Dashboard, LogWindow
 */
class StatusBar {
private:
    WINDOW* win;
    int width, height;
    std::string text;

public:
    /**
     * @brief Construct a status bar for ncurses display
     *
     * @param w The ncurses WINDOW pointer to render into
     * @param wth Width of the window in characters
     * @param hgt Height of the window in characters (usually 1-2)
     */
    StatusBar(WINDOW* w, int wth, int hgt) 
        : win(w), width(wth), height(hgt) {}

    /**
     * @brief Set the status text to display
     *
     * Updates the internal status string. If the text is longer than the window
     * width, it is automatically truncated to fit. The next Render() call will
     * display this new text.
     *
     * @param t The status text to display (e.g., "Status: Running")
     *
     * @see Render(), GetText()
     */
    void SetText(const std::string& t) {
        text = t;
        if ((int)text.length() > width - 4) {
            text = text.substr(0, width - 4);
        }
    }

    /**
     * @brief Render the status bar to the ncurses display
     *
     * Displays the current status text in reverse video (inverted colors)
     * for visual prominence. The text is centered or left-aligned depending
     * on implementation.
     *
     * @see SetText()
     */
    void Render() {
        if (!win) return;
        
        werase(win);
        wattron(win, A_REVERSE);
        mvwprintw(win, 0, 1, "%s", text.c_str());
        wattroff(win, A_REVERSE);
        wrefresh(win);
    }

    /**
     * @brief Resize the status bar dimensions
     *
     * Called when the terminal is resized or dashboard layout changes.
     * Updates window height and width, resizes the ncurses window,
     * and repositions it vertically.
     *
     * @param h New height in lines (usually 1-2)
     * @param w New width in characters
     * @param starty New Y position in the terminal
     *
     * @see Render()
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

#endif // GDASHBOARD_STATUS_BAR_HPP
