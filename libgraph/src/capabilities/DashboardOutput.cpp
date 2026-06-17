/**
 * @file DashboardOutput.cpp
 * @brief GraphX source file.
 */

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

#include "capabilities/DashboardOutput.hpp"
#include "capabilities/DashboardCapability.hpp"
#include <sstream>
#include <stdexcept>

namespace capabilities {

DashboardOutput::DashboardOutput(
    std::shared_ptr<DashboardCapability> dashboard)
    : dashboard_(dashboard) {
    if (!dashboard) {
        throw std::invalid_argument(
            "DashboardOutput: dashboard capability cannot be null");
    }
}

/**
 * @brief Write message.
 * @param message Parameter for write message.
 */
void DashboardOutput::WriteMessage(const std::string& message) {
    if (dashboard_) {
        dashboard_->AddLog("[OK] " + message);
    }
}

/**
 * @brief Write error.
 * @param error Parameter for write error.
 */
void DashboardOutput::WriteError(const std::string& error) {
    if (dashboard_) {
        dashboard_->AddLog("[ERROR] " + error);
    }
}

/**
 * @brief Write warning.
 * @param warning Parameter for write warning.
 */
void DashboardOutput::WriteWarning(const std::string& warning) {
    if (dashboard_) {
        dashboard_->AddLog("[WARN] " + warning);
    }
}

/**
 * @brief Write help.
 * @param commands Parameter for write help.
 */
void DashboardOutput::WriteHelp(const std::vector<CommandInfo>& commands) {
    if (!dashboard_) {
        return;
    }

    // Write help header
    dashboard_->AddLog("[HELP] Available commands:");

    // Format and write each command
    for (const auto& cmd : commands) {
        std::ostringstream oss;
        oss << "[HELP]   " << cmd.name << " - " << cmd.description;
        dashboard_->AddLog(oss.str());

        // Add usage if available
        if (!cmd.usage.empty()) {
            std::ostringstream usage_oss;
            usage_oss << "[HELP]     Usage: " << cmd.usage;
            dashboard_->AddLog(usage_oss.str());
        }
    }
}

}  // namespace capabilities
