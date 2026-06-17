/**
 * @file NodePluginInstance.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <memory>
#include <string>

#include <log4cxx/logger.h>

namespace graph {

// Shared plugin node wrapper used by plugin creation and typed extraction.
template <typename NodeT>
struct NodePluginInstance {
    std::shared_ptr<NodeT> node;
    std::string name;
    std::string type;
    log4cxx::LoggerPtr logger;

    NodePluginInstance(std::shared_ptr<NodeT> n,
                       std::string nm,
                       const char* logger_name)
        : node(std::move(n)),
          name(std::move(nm)),
          type(""),
          logger(log4cxx::Logger::getLogger(logger_name)) {
        LOG4CXX_TRACE(logger, "Created plugin instance");

        try {
            if (node) {
                type = node->GetNodeTypeName();
                LOG4CXX_TRACE(logger, "Node type: " << type);
            } else {
                type = "Unknown";
                LOG4CXX_WARN(logger, "Node is null in NodePluginInstance constructor");
            }
        } catch (const std::exception& e) {
            type = "Unknown";
            LOG4CXX_ERROR(logger, "Exception getting node type: " << e.what());
        } catch (...) {
            type = "Unknown";
            LOG4CXX_ERROR(logger, "Unknown exception getting node type");
        }
    }

    ~NodePluginInstance() {
        LOG4CXX_TRACE(logger, "Destroying plugin instance");
    }
};

}  // namespace graph
