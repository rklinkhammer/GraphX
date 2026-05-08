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

#ifndef GDASHBOARD_COMMAND_WINDOW_HPP
#define GDASHBOARD_COMMAND_WINDOW_HPP

#include <ncurses.h>
#include <string>
#include <deque>

/**
 * @class CommandWindow
 * @brief User input field with command history for dashboard interaction
 *
 * CommandWindow provides an interactive command entry line with features
 * for building user commands character by character and navigating through
 * previously executed commands via history.
 *
 * Key features:
 * 1. **Text Editing**: Add/delete characters with bounds checking
 * 2. **History Navigation**: Up/Down keys cycle through previous commands
 * 3. **Text Truncation**: Long commands automatically truncated to fit display
 * 4. **Cursor Positioning**: Cursor automatically positioned after input text
 * 5. **Command Buffer**: Current input stored and cleared after execution
 *
 * Integration:
 * - Created as part of Dashboard's 4-panel layout (15% height by default)
 * - Receives keyboard input from Dashboard::CatchEvent()
 * - User enters commands one character at a time
 * - Commands executed via Dashboard::ExecuteCommand()
 * - Result shown in adjacent output area
 *
 * Example usage:
 * ```cpp
 * // Create command window for user input
 * auto cmd_window = CommandWindow(ncurses_window_ptr, width, height);
 * 
 * // Render the prompt and current input
 * cmd_window.Render();
 * 
 * // User types characters
 * cmd_window.AddChar('h');
 * cmd_window.AddChar('e');
 * cmd_window.AddChar('l');
 * cmd_window.AddChar('p');
 * 
 * // Get the command for execution
 * std::string cmd = cmd_window.GetCommand();  // "help"
 * 
 * // Add executed command to history
 * cmd_window.AddHistory("help");
 * cmd_window.ClearBuffer();
 * 
 * // User browses history with Up/Down keys
 * cmd_window.HistoryUp();    // buffer now contains "help"
 * cmd_window.HistoryDown();  // buffer cleared
 * ```
 *
 * @see Dashboard, CommandRegistry
 */
class CommandWindow {
private:
    WINDOW* win;
    int width, height;
    std::string buffer;
    std::deque<std::string> history;
    int history_index = -1;

public:
    /**
     * @brief Construct a command input window for ncurses display
     *
     * @param w The ncurses WINDOW pointer to render into
     * @param wth Width of the window in characters
     * @param hgt Height of the window in characters (including border)
     */
    CommandWindow(WINDOW* w, int wth, int hgt) 
        : win(w), width(wth), height(hgt) {}

    /**
     * @brief Render the command prompt and input buffer to the ncurses window
     *
     * Displays "> " prompt followed by the current command buffer.
     * If the buffer is too long for the window, leading characters are
     * replaced with "..." and the end of the command is shown.
     * Cursor is positioned right after the displayed text.
     *
     * @see AddChar(), DeleteChar(), GetCommand()
     */
    void Render()
    {
        if (!win)
            return;

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, "Command");

        // Truncate buffer if too long
        std::string display = buffer;
        int inner = width - 6;
        int tail  = width - 9;
        if (inner > 0 && (int)display.size() > inner && tail > 0) {
            display = "..." + display.substr(display.size() - (size_t)tail);
        }

        mvwprintw(win, 1, 2, "> %s", display.c_str());

        // --- force cursor to the command input position ---
        // Cursor should be after "> " + displayed text.
        int prompt_col = 2;                                    // where '>' begins
        int cursor_col = prompt_col + 2 + (int)display.size(); // 2 for "> "
        int max_inner_col = width - 2;                         // keep inside the box border
        if (cursor_col > max_inner_col)
            cursor_col = max_inner_col;
        if (cursor_col < 1)
            cursor_col = 1;

        wmove(win, 1, cursor_col);
        wrefresh(win);
    }

    /**
     * @brief Get the current command buffer contents
     *
     * Returns the accumulated command string that the user has typed.
     * Called before command execution to get the command to run.
     *
     * @return The current command string
     *
     * @see ClearBuffer(), AddChar()
     */
    std::string GetCommand() { 
        return buffer; 
    }

    /**
     * @brief Clear the command buffer
     *
     * Resets the buffer to empty after a command is executed.
     * Also resets history navigation to the most recent entry.
     *
     * @see GetCommand(), AddChar()
     */
    void ClearBuffer() { 
        buffer.clear(); 
        history_index = -1;
    }

    /**
     * @brief Add a character to the command buffer
     *
     * Appends a character to the input buffer if there is still room
     * (checked against window width). Does not add if buffer would exceed bounds.
     *
     * @param ch The character to add to the command
     *
     * @see DeleteChar(), ClearBuffer(), Render()
     */
    void AddChar(char ch) {
        if ((int)buffer.length() < width - 6) {
            buffer.push_back(ch);
        }
    }

    /**
     * @brief Delete the last character from the command buffer
     *
     * Removes the rightmost character if the buffer is not empty.
     * Has no effect if buffer is already empty.
     *
     * @see AddChar(), ClearBuffer()
     */
    void DeleteChar() {
        if (!buffer.empty()) {
            buffer.pop_back();
        }
    }

    /**
     * @brief Add a command to the history after execution
     *
     * Stores a successfully executed command in the history deque
     * for later retrieval via HistoryUp/HistoryDown navigation.
     * Empty commands are not added to history.
     *
     * @param cmd The command to add to history
     *
     * @see HistoryUp(), HistoryDown(), GetCommand()
     */
    void AddHistory(const std::string& cmd) {
        if (!cmd.empty()) {
            history.push_back(cmd);
            history_index = -1;
        }
    }

    /**
     * @brief Move up one command in history (toward oldest command)
     *
     * Cycles backward through the history list, replacing the buffer
     * with each previous command. Called when user presses Up arrow.
     * First press jumps to most recent command, then moves back through history.
     *
     * @see HistoryDown(), AddHistory()
     */
    void HistoryUp() {
        if (!history.empty()) {
            if (history_index < 0) {
                history_index = (int)history.size() - 1;
            } else if (history_index > 0) {
                history_index--;
            }
            buffer = history[history_index];
        }
    }

    /**
     * @brief Move down one command in history (toward newest command)
     *
     * Cycles forward through the history list. When reaching the end,
     * clears the buffer and returns to input mode. Called when user
     * presses Down arrow.
     *
     * @see HistoryUp(), AddHistory()
     */
    void HistoryDown() {
        if (!history.empty() && history_index >= 0) {
            history_index++;
            if (history_index < (int)history.size()) {
                buffer = history[history_index];
            } else {
                buffer.clear();
                history_index = -1;
            }
        }
    }

    /**
     * @brief Resize the command window dimensions
     *
     * Called when the terminal is resized or dashboard layout changes.
     * Updates window height and width, resizes the ncurses window,
     * and repositions it vertically.
     *
     * @param h New height in lines (including border)
     * @param w New width in characters (including border)
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

#endif // GDASHBOARD_COMMAND_WINDOW_HPP
