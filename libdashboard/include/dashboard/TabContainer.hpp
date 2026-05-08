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

#ifndef GDASHBOARD_TAB_CONTAINER_HPP
#define GDASHBOARD_TAB_CONTAINER_HPP

#include <string>
#include <vector>
#include <memory>
#include <ftxui/component/component.hpp>

/**
 * @brief Container for managing tabbed interface
 *
 * Provides a tab-based container for organizing UI components.
 */
class TabContainer {
public:
    /**
     * @brief Default constructor
     */
    TabContainer() = default;

    /**
     * @brief Add a tab with content
     *
     * @param name Name of the tab
     * @param content FTXUI element to display in the tab
     */
    void AddTab(const std::string& name, ftxui::Element content);

    /**
     * @brief Get the container as an FTXUI element
     *
     * @return FTXUI element for display
     */
    ftxui::Element Render() const;

    /**
     * @brief Get current active tab index
     *
     * @return Index of active tab
     */
    size_t GetActiveTab() const;

    /**
     * @brief Set active tab by index
     *
     * @param index Index of tab to activate
     */
    void SetActiveTab(size_t index);

private:
    std::vector<std::string> tab_names_;
    std::vector<ftxui::Element> tab_contents_;
    size_t active_tab_ = 0;
};

#endif // GDASHBOARD_TAB_CONTAINER_HPP
