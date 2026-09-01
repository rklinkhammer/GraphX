/**
 * @file MetricsPolicy.cpp
 * @brief Metrics Policy Graph runtime support.
 *
 * @details Provides executor policy integration for commands, metrics, completion, and data injection. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include "graph/GraphManagerCore.hpp"
#include "capabilities/GraphCapability.hpp"
#include "graph/CapabilityContext.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/CompletionSignal.hpp"
#include "policies/MetricsPolicy.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "metrics/IMetricsCallback.hpp"

#include <chrono>
#include <charconv>
#include <cmath>
#include <optional>
#include <thread>
#include <log4cxx/logger.h>

namespace policies {

namespace {

constexpr std::size_t kMaximumEventFields = 64U;
constexpr std::size_t kMaximumEventBytes = 16384U;
constexpr std::size_t kMaximumMetricIdBytes = 128U;
constexpr std::size_t kMaximumScalarStringBytes = 1024U;

std::vector<app::metrics::NodeMetricsSchema::MetricDescriptor>
ParseDescriptors(const nlohmann::json& schema) {
    std::vector<app::metrics::NodeMetricsSchema::MetricDescriptor> result;
    if (!schema.is_object() || !schema.contains("fields") ||
        !schema["fields"].is_array() || schema["fields"].size() > 64U) {
        return result;
    }
    for (const auto& field : schema["fields"]) {
        if (!field.is_object()) {
            continue;
        }
        const auto bounded_string = [&field](const char* key,
                                             std::size_t maximum)
            -> std::optional<std::string> {
            const auto found = field.find(key);
            if (found == field.end() || !found->is_string()) {
                return std::nullopt;
            }
            const auto& value = found->get_ref<const std::string&>();
            if (value.size() > maximum) return std::nullopt;
            return value;
        };
        const auto metric_id = bounded_string("name", kMaximumMetricIdBytes);
        const auto scalar_type = bounded_string("type", 16U);
        const auto unit = bounded_string("unit", 32U);
        const auto semantics = bounded_string("semantics", 32U);
        const auto aggregation = bounded_string("aggregation", 32U);
        const auto availability = bounded_string("availability_rule", 256U);
        if (!metric_id || !scalar_type || !unit || !semantics ||
            !aggregation || !availability) {
            continue;
        }
        const bool type_valid = *scalar_type == "boolean" ||
            *scalar_type == "integer" || *scalar_type == "unsigned" ||
            *scalar_type == "number" || *scalar_type == "string";
        const bool semantics_valid = *semantics == "gauge" ||
            *semantics == "monotonic_counter" || *semantics == "state";
        const bool aggregation_valid = *aggregation == "sum" ||
            *aggregation == "min" || *aggregation == "max" ||
            *aggregation == "average" || *aggregation == "rate" ||
            *aggregation == "none";
        if (metric_id->empty() || availability->empty() || !type_valid || !semantics_valid ||
            !aggregation_valid) {
            continue;
        }
        result.push_back({.metric_id = *metric_id,
                          .scalar_type = *scalar_type,
                          .unit = *unit,
                          .semantics = *semantics,
                          .aggregation = *aggregation,
                          .availability_rule = *availability});
    }
    return result;
}

std::optional<app::metrics::MetricScalar>
ParseScalar(const std::string& text, const std::string& scalar_type) {
    if (scalar_type == "string") {
        if (text.size() <= kMaximumScalarStringBytes) {
            return app::metrics::MetricScalar{text};
        }
        return std::nullopt;
    }
    if (scalar_type == "boolean") {
        if (text == "true") return app::metrics::MetricScalar{true};
        if (text == "false") return app::metrics::MetricScalar{false};
        return std::nullopt;
    }
    if (scalar_type == "integer") {
        std::int64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
            return app::metrics::MetricScalar{value};
        }
        return std::nullopt;
    }
    if (scalar_type == "unsigned") {
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
            return app::metrics::MetricScalar{value};
        }
        return std::nullopt;
    }
    if (scalar_type == "number") {
        double value = 0.0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
            std::isfinite(value)) {
            return app::metrics::MetricScalar{value};
        }
    }
    return std::nullopt;
}

bool EventWithinBounds(const app::metrics::MetricsEvent& event) {
    if (event.source.size() > 256U || event.event_type.size() > 128U ||
        event.data.size() > kMaximumEventFields ||
        event.samples.size() > kMaximumEventFields) {
        return false;
    }
    if (!app::metrics::MetricsEventWithinEncodedBound(event,
                                                       kMaximumEventBytes)) {
        return false;
    }
    for (const auto& [key, value] : event.data) {
        if (key.empty() || key.size() > kMaximumMetricIdBytes ||
            value.size() > kMaximumScalarStringBytes) {
            return false;
        }
    }
    return true;
}

}  // namespace


/**
 * @brief Init metrics sources.
 * @param context Parameter for init metrics sources.
 */
void MetricsPolicy::InitMetricsSources(capabilities::GraphCapability& context) {
    graph::CapabilityContext capability_context{context};
    auto nodes_result = capability_context.Nodes();
    if (!nodes_result) {
        LOG4CXX_WARN(metrics_logger, "MetricsPolicy::InitMetricsSources() - no GraphManager");
        return ;
    }   
    std::vector<app::metrics::NodeMetricsSchema> schemas;
    
    auto nodes = *nodes_result;
    const auto graph_manager = context.GetGraphManager();
    if (!graph_manager) {
        LOG4CXX_WARN(metrics_logger, "MetricsPolicy::InitMetricsSources() - no GraphManager identity authority");
        return;
    }
    const auto& canonical_ids = graph_manager->GetCanonicalNodeIds();
    const auto generation = metrics_capability_->GetGraphGeneration();
    if (canonical_ids.size() != nodes.size() || generation == 0U) {
        LOG4CXX_WARN(metrics_logger, "MetricsPolicy::InitMetricsSources() - missing canonical identity or graph generation");
        metrics_capability_->RecordRejected(
            capabilities::MetricsRejectionCategory::AuthorityMismatch);
        return;
    }
    for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
        if (!nodes[node_idx]) {
            continue;
        }
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - checking Node[" 
                      << node_idx << "]");

        auto metrics_node =
            capability_context.NodeCapability<graph::IMetricsCallbackProvider>(nodes[node_idx]);
        if (!metrics_node) {
            LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - Node[" 
                          << node_idx << "] does not implement IMetricsCallbackProvider");
            continue;
        }
        auto descriptor = capability_context.DescribeNode(nodes[node_idx]);
        LOG4CXX_TRACE(metrics_logger, "MetricsPolicy::InitMetricsSources() - Node[" 
                     << node_idx << "] '" << descriptor.name << "' supports IMetricsCallbackProvider");
    
        auto schema = (*metrics_node)->GetNodeMetricsSchema();
        schema.target.kind = app::metrics::MetricTarget::Kind::Node;
        schema.target.node_id = canonical_ids[node_idx];
        schema.graph_generation = generation;
        schema.descriptors = ParseDescriptors(schema.metrics_schema);
        const auto declared_field_count =
            schema.metrics_schema.is_object() &&
            schema.metrics_schema.contains("fields") &&
            schema.metrics_schema["fields"].is_array()
                ? schema.metrics_schema["fields"].size() : 0U;
        const auto rejection_count = declared_field_count > 64U
            ? 1U : declared_field_count - schema.descriptors.size();
        for (std::size_t rejected = 0U; rejected < rejection_count; ++rejected) {
            metrics_capability_->RecordRejected(
                capabilities::MetricsRejectionCategory::SchemaContract);
        }

        auto metrics_callback = std::make_shared<MetricsCapabilityCallback>();
        metrics_callback->on_publish_async_ =
            [this, target = schema.target, generation,
             descriptors = schema.descriptors](const app::metrics::MetricsEvent& publisher_event) -> bool {
            if (!EventWithinBounds(publisher_event)) {
                metrics_capability_->RecordRejected(
                    capabilities::MetricsRejectionCategory::SampleContract);
                return false;
            }
            app::metrics::MetricsEvent event;
            event.timestamp = publisher_event.timestamp;
            event.source = publisher_event.source;
            event.event_type = publisher_event.event_type;
            event.data = publisher_event.data;
            event.target = target;
            event.graph_generation = generation;
            for (const auto& descriptor : descriptors) {
                const auto found = publisher_event.data.find(descriptor.metric_id);
                if (found == publisher_event.data.end()) {
                    continue;
                }
                const auto scalar = ParseScalar(found->second, descriptor.scalar_type);
                if (!scalar) {
                    metrics_capability_->RecordRejected(
                        capabilities::MetricsRejectionCategory::SampleContract);
                    continue;
                }
                event.samples.push_back({
                    .metric_id = descriptor.metric_id,
                    .scalar_type = descriptor.scalar_type,
                    .unit = descriptor.unit,
                    .semantics = descriptor.semantics,
                    .aggregation = descriptor.aggregation,
                    .availability_rule = descriptor.availability_rule,
                    .value = *scalar,
                    .available = true,
                    .unavailable_reason = {},
                    .counter_epoch = generation});
            }
            if (!capabilities::MetricsCapability::ValidateEventContract(event)) {
                metrics_capability_->RecordRejected(
                    capabilities::MetricsRejectionCategory::SampleContract);
                return false;
            }
            if (!metrics_event_queue_.Enqueue(std::move(event))) {
                metrics_capability_->RecordDroppedQueueFull();
                return false;
            }
            return true;
        };
        (*metrics_node)->SetMetricsCallbackShared(metrics_callback);
        metrics_sources_.push_back(*metrics_node);
        AddNodeMetrics(canonical_ids[node_idx], metrics_callback, schema);
        if (!schema.descriptors.empty()) {
            schemas.push_back(std::move(schema));
        }
    }
    metrics_capability_->SetNodeMetricsSchemas(schemas);
}

}  // namespace policies
