/**
 * @file NodePluginInstance.hpp
 * @brief Node Plugin Instance Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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
/**
 * @struct NodePluginInstance
 * @brief Node Plugin Instance data record.
 *
 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
 */
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
          /**
           * @brief Executes the Logger operation.
           *
           * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
           * @return Method-specific result, status, or produced value when the signature provides one.
           */
          logger(log4cxx::Logger::getLogger(logger_name)) {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(logger, "Created plugin instance");

        try {
            if (node) {
                type = node->GetNodeTypeName();
                /**
                 * @brief Executes the Log4 Cxx Trace operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param logger Input or configuration value consumed by the method.
                 * @param type Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                LOG4CXX_TRACE(logger, "Node type: " << type);
            } else {
                type = "Unknown";
                /**
                 * @brief Executes the Log4 Cxx Warn operation.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param logger Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                LOG4CXX_WARN(logger, "Node is null in NodePluginInstance constructor");
            }
        } catch (const std::exception& e) {
            type = "Unknown";
            /**
             * @brief Executes the Log4 Cxx Error operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_ERROR(logger, "Exception getting node type: " << e.what());
        } catch (...) {
            type = "Unknown";
            /**
             * @brief Executes the Log4 Cxx Error operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param logger Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            LOG4CXX_ERROR(logger, "Unknown exception getting node type");
        }
    }

    /**
     * @brief Releases resources owned by Node Plugin Instance.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~NodePluginInstance() {
        /**
         * @brief Executes the Log4 Cxx Trace operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param logger Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        LOG4CXX_TRACE(logger, "Destroying plugin instance");
    }
};

}  // namespace graph
