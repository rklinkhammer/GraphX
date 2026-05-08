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

#include "ui/TabContainer.hpp"
#include <ftxui/dom/elements.hpp>

void TabContainer::AddTab(const std::string& name, ftxui::Element content) {
    tab_names_.push_back(name);
    tab_contents_.push_back(content);
}

ftxui::Element TabContainer::Render() const {
    using namespace ftxui;

    if (tab_contents_.empty()) {
        return text("No tabs");
    }

    // Simple tab rendering
    std::vector<Element> tabs;
    for (size_t i = 0; i < tab_names_.size(); ++i) {
        if (i == active_tab_) {
            tabs.push_back(text(tab_names_[i]) | bold | underlined);
        } else {
            tabs.push_back(text(tab_names_[i]));
        }
    }

    return vbox(
        hbox(tabs),
        separator(),
        tab_contents_[active_tab_]
    );
}

size_t TabContainer::GetActiveTab() const {
    return active_tab_;
}

void TabContainer::SetActiveTab(size_t index) {
    if (index < tab_contents_.size()) {
        active_tab_ = index;
    }
}
