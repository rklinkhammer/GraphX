// MIT License
//
// Copyright (c) 2025 graphlib contributors
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

#include "app/SignalHandler.hpp"
#include <iostream>
#include <log4cxx/logger.h>
#include <signal.h>
#include <string.h>

namespace app {

static log4cxx::LoggerPtr g_logger =
    log4cxx::Logger::getLogger("app.SignalHandler");

// Static instance
static SignalHandler* g_instance = nullptr;

SignalHandler& SignalHandler::GetInstance() {
    if (!g_instance) {
        static SignalHandler instance;
        g_instance = &instance;
    }
    return *g_instance;
}

SignalHandler::SignalHandler()
    : shutdown_requested_(false), trigger_signal_(0) {
    LOG4CXX_TRACE(g_logger, "SignalHandler constructed");
}

void SignalHandler::Register(SignalCallback callback) {
    callback_ = callback;
    LOG4CXX_TRACE(g_logger, "Shutdown callback registered");
}

void SignalHandler::RegisterScreenResize(SignalCallback callback) {
    struct sigaction sa;
    resize_callback_ = callback;
    // Initialize sigaction structure
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SignalHandler::HandleResizeSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, nullptr);

    LOG4CXX_TRACE(g_logger, "Screen resize callback registered");
}

void SignalHandler::HandleSignal(int signal) {
    SignalHandler& handler = GetInstance();
    handler.RequestShutdown(signal);
}

void SignalHandler::HandleResizeSignal(int signal) {
    SignalHandler& handler = GetInstance();
    if(handler.resize_callback_) {
        try {
            handler.resize_callback_();
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(g_logger,
                "Exception in resize callback: " << e.what());
        }
    }
    handler.RequestShutdown(signal);
}

void SignalHandler::RegisterSignals() {
    struct sigaction sa;
    
    // Initialize sigaction structure
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SignalHandler::HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // Register SIGINT (Ctrl+C)
    sigaction(SIGINT, &sa, nullptr);
    LOG4CXX_TRACE(g_logger, "Registered SIGINT handler");

    // Register SIGTERM (termination signal)
    sigaction(SIGTERM, &sa, nullptr);
    LOG4CXX_TRACE(g_logger, "Registered SIGTERM handler");

#ifndef _WIN32
    // Register SIGHUP (hangup/reload signal) on POSIX systems
    sigaction(SIGHUP, &sa, nullptr);
    LOG4CXX_TRACE(g_logger, "Registered SIGHUP handler");
#endif

    LOG4CXX_TRACE(g_logger, "Signal handlers registered for graceful shutdown");
}

bool SignalHandler::ShouldShutdown() const {
    return shutdown_requested_.load(std::memory_order_acquire);
}

void SignalHandler::RequestShutdown(int signal_number) {
    // Set atomic flag (lock-free)
    shutdown_requested_.store(true, std::memory_order_release);

    // Store signal number for logging
    if (signal_number > 0) {
        trigger_signal_.store(signal_number, std::memory_order_release);
    }

    // Log shutdown request
    const char* signal_name = "Unknown";
    switch (signal_number) {
        case SIGINT:
            signal_name = "SIGINT (Ctrl+C)";
            break;
        case SIGTERM:
            signal_name = "SIGTERM";
            break;
#ifndef _WIN32
        case SIGHUP:
            signal_name = "SIGHUP";
            break;
#endif
        default:
            signal_name = "Manual request";
    }

    LOG4CXX_WARN(g_logger, "Shutdown requested: " << signal_name);

    // Call registered callback if available
    if (callback_) {
        try {
            callback_();
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(g_logger,
                "Exception in shutdown callback: " << e.what());
        }
    }
}

void SignalHandler::Reset() {
    shutdown_requested_.store(false, std::memory_order_release);
    trigger_signal_.store(0, std::memory_order_release);
    LOG4CXX_TRACE(g_logger, "Signal handler state reset");
}

int SignalHandler::GetTriggerSignal() const {
    return trigger_signal_.load(std::memory_order_acquire);
}

}  // namespace app
