/**
 * @file MetricsCapability.hpp
 * @brief Metrics Capability Graph runtime support.
 *
 * @details Provides capability API used to share runtime services between policies, nodes, and executors. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#pragma once

#include "metrics/MetricsEvent.hpp"
#include "metrics/NodeMetricsSchema.hpp"
#include "metrics/IMetricsSubscriber.hpp"
#include "graph/CapabilityBus.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <type_traits>


namespace capabilities {

enum class MetricsRejectionCategory : std::size_t {
    SchemaContract,
    SampleContract,
    AuthorityMismatch,
    SubscriberFailure,
    Internal,
    Count
};

struct MetricsRejectionDiagnostics {
    std::array<std::uint64_t,
               static_cast<std::size_t>(MetricsRejectionCategory::Count)>
        categories{};

    [[nodiscard]] std::uint64_t Total() const noexcept {
        std::uint64_t total = 0U;
        for (const auto value : categories) total += value;
        return total;
    }

    [[nodiscard]] std::uint64_t Get(
        const MetricsRejectionCategory category) const noexcept {
        return categories[static_cast<std::size_t>(category)];
    }
};

/**
 * @class MetricsCapability
 * @brief Capability for metrics discovery and real-time event subscription
 *
 * MetricsCapability is the interface through which the dashboard discovers
 * available metrics from graph nodes and subscribes to real-time updates.
 *
 * Key responsibilities:
 * 1. **Discovery**: Provide schemas describing node metrics (names, units, types)
 * 2. **Subscription**: Register callbacks to receive metric events
 * 3. **Publishing**: Notify subscribers when metrics are updated
 *
 * Execution Flow:
 * 1. Dashboard calls GetNodeMetricsSchemas() during initialization
 * 2. Creates MetricsTiles from schemas for UI rendering
 * 3. Registers Dashboard as a subscriber via RegisterMetricsCallback()
 * 4. During execution, graph nodes publish metrics through this capability
 * 5. Published metrics trigger OnMetricsEvent() on all registered subscribers
 * 6. Dashboard updates display with new values
 *
 * Thread Safety:
 * - Callback registration/unregistration use mutex protection
 * - Publishing should be done from executor thread
 * - Subscriber callbacks are invoked from publishing thread
 * - Dashboard must handle thread-safe updates in OnMetricsEvent()
 *
 * Example Use:
 * ```cpp
 * auto metrics_cap = bus->GetCapability<MetricsCapability>();
 * 
 * // Discover available metrics
 * auto schemas = metrics_cap->GetNodeMetricsSchemas();
 * for (const auto& schema : schemas) {
 *     std::cout << "Node: " << schema.node_name << "\n";
 * }
 * 
 * // Register for updates
 * metrics_cap->RegisterMetricsCallback(subscriber);
 * 
 * // During execution, MetricsPolicy publishes events:
 * app::metrics::MetricsEvent event;
 * event.node_name = "Sensor";
 * event.metric_id = "temperature";
 * event.data = 42.5;
 * metrics_cap->PublishMetricsEvent(event);
 * 
 * // All registered subscribers are notified
 * ```
 *
 * @see IMetricsSubscriber, MetricsEvent, NodeMetricsSchema
 */
/**
 * @class MetricsCapability
 * @brief Metrics Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class MetricsCapability {
private:
    struct SubscriberEntry {
        explicit SubscriberEntry(app::metrics::IMetricsSubscriber* value)
            : callback(value) {}
        app::metrics::IMetricsSubscriber* callback;
        bool active{true};
        std::size_t in_flight{0U};
    };

    static bool IsValidUtf8(const std::string_view text) noexcept {
        for (std::size_t index = 0U; index < text.size();) {
            const auto first = static_cast<unsigned char>(text[index]);
            if (first <= 0x7FU) { ++index; continue; }
            std::size_t trailing = 0U;
            std::uint32_t code_point = 0U;
            std::uint32_t minimum = 0U;
            if ((first & 0xE0U) == 0xC0U) {
                trailing = 1U; code_point = first & 0x1FU; minimum = 0x80U;
            } else if ((first & 0xF0U) == 0xE0U) {
                trailing = 2U; code_point = first & 0x0FU; minimum = 0x800U;
            } else if ((first & 0xF8U) == 0xF0U) {
                trailing = 3U; code_point = first & 0x07U; minimum = 0x10000U;
            } else {
                return false;
            }
            if (index + trailing >= text.size()) return false;
            for (std::size_t offset = 1U; offset <= trailing; ++offset) {
                const auto next = static_cast<unsigned char>(text[index + offset]);
                if ((next & 0xC0U) != 0x80U) return false;
                code_point = (code_point << 6U) | (next & 0x3FU);
            }
            if (code_point < minimum || code_point > 0x10FFFFU ||
                (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
                return false;
            }
            index += trailing + 1U;
        }
        return true;
    }

    static bool EventWithinContract(
        const app::metrics::MetricsEvent& event) noexcept {
        if (event.samples.size() > 64U || event.data.size() > 64U ||
            (!event.samples.empty() && event.graph_generation == 0U) ||
            event.source.size() > 256U ||
            event.event_type.size() > 128U ||
            event.timestamp < std::chrono::system_clock::time_point{} ||
            event.timestamp > std::chrono::system_clock::now() +
                                  std::chrono::seconds(1) ||
            !IsValidUtf8(event.source) || !IsValidUtf8(event.event_type) ||
            !app::metrics::MetricsEventWithinEncodedBound(event)) {
            return false;
        }
        const auto& target = event.target;
        const auto port_shape_valid = [](const std::string& kind,
                                         const auto& port) {
            return (kind == "index" &&
                    std::holds_alternative<std::uint64_t>(port)) ||
                   (kind == "name" &&
                    std::holds_alternative<std::string>(port) &&
                    !std::get<std::string>(port).empty() &&
                    std::get<std::string>(port).size() <= 128U);
        };
        const bool target_shape_valid =
            target.kind == app::metrics::MetricTarget::Kind::Node
                ? !target.node_id.empty() && target.node_id.size() <= 256U &&
                    target.source_node_id.empty() &&
                    target.source_port_kind.empty() &&
                    std::holds_alternative<std::uint64_t>(target.source_port) &&
                    std::get<std::uint64_t>(target.source_port) == 0U &&
                    target.target_node_id.empty() &&
                    target.target_port_kind.empty() &&
                    std::holds_alternative<std::uint64_t>(target.target_port) &&
                    std::get<std::uint64_t>(target.target_port) == 0U
                : target.node_id.empty() &&
                    !target.source_node_id.empty() &&
                    target.source_node_id.size() <= 256U &&
                    !target.target_node_id.empty() &&
                    target.target_node_id.size() <= 256U &&
                    port_shape_valid(target.source_port_kind,
                                     target.source_port) &&
                    port_shape_valid(target.target_port_kind,
                                     target.target_port);
        if (!target_shape_valid) return false;
        if (!IsValidUtf8(target.node_id) ||
            !IsValidUtf8(target.source_node_id) ||
            !IsValidUtf8(target.source_port_kind) ||
            !IsValidUtf8(target.target_node_id) ||
            !IsValidUtf8(target.target_port_kind)) return false;
        const auto port_utf8_valid = [](const auto& port) {
            if (std::holds_alternative<std::string>(port)) {
                const auto& name = std::get<std::string>(port);
                if (!IsValidUtf8(name)) return false;
            }
            return true;
        };
        if (!port_utf8_valid(target.source_port) ||
            !port_utf8_valid(target.target_port)) {
            return false;
        }
        for (const auto& [key, value] : event.data) {
            if (key.empty() || key.size() > 128U || value.size() > 1024U ||
                !IsValidUtf8(key) || !IsValidUtf8(value)) {
                return false;
            }
        }
        for (const auto& sample : event.samples) {
            const bool type_valid =
                (sample.scalar_type == "boolean" &&
                 std::holds_alternative<bool>(sample.value)) ||
                (sample.scalar_type == "integer" &&
                 std::holds_alternative<std::int64_t>(sample.value)) ||
                (sample.scalar_type == "unsigned" &&
                 std::holds_alternative<std::uint64_t>(sample.value)) ||
                (sample.scalar_type == "number" &&
                 std::holds_alternative<double>(sample.value)) ||
                (sample.scalar_type == "string" &&
                 std::holds_alternative<std::string>(sample.value));
            const bool strings_valid = IsValidUtf8(sample.metric_id) &&
                IsValidUtf8(sample.scalar_type) && IsValidUtf8(sample.unit) &&
                IsValidUtf8(sample.semantics) &&
                IsValidUtf8(sample.aggregation) &&
                IsValidUtf8(sample.availability_rule) &&
                IsValidUtf8(sample.unavailable_reason);
            const bool semantics_valid = sample.semantics == "gauge" ||
                sample.semantics == "monotonic_counter" ||
                sample.semantics == "state";
            const bool aggregation_valid = sample.aggregation == "sum" ||
                sample.aggregation == "min" || sample.aggregation == "max" ||
                sample.aggregation == "average" ||
                sample.aggregation == "rate" || sample.aggregation == "none";
            const bool bounds_valid = !sample.metric_id.empty() &&
                sample.metric_id.size() <= 128U && sample.unit.size() <= 32U &&
                !sample.availability_rule.empty() &&
                sample.availability_rule.size() <= 256U &&
                sample.unavailable_reason.size() <= 256U &&
                (sample.available || !sample.unavailable_reason.empty());
            if (!type_valid || !strings_valid || !semantics_valid ||
                !aggregation_valid || !bounds_valid ||
                (!std::holds_alternative<double>(sample.value) ? false
                    : !std::isfinite(std::get<double>(sample.value))) ||
                (std::holds_alternative<std::string>(sample.value) &&
                 (std::get<std::string>(sample.value).size() > 1024U ||
                  !IsValidUtf8(std::get<std::string>(sample.value))))) {
                return false;
            }
        }
        return true;
    }

public:
    [[nodiscard]] static bool ValidateEventContract(
        const app::metrics::MetricsEvent& event) noexcept {
        return EventWithinContract(event);
    }

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~MetricsCapability() = default;
    
    /**
     * @brief Register a callback to receive metrics events
     *
     * Called during dashboard initialization to subscribe to metric updates.
     * The callback will be invoked for each metric event during execution.
     *
     * @param callback The subscriber to register
     *
     * @see UnregisterMetricsCallback, IMetricsSubscriber
     */
    virtual void RegisterMetricsCallback(
        app::metrics::IMetricsSubscriber* callback) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        if (callback == nullptr) {
            return;
        }
        std::unique_lock<std::mutex> lock(callbacks_lock_);
        const auto found = std::ranges::find_if(
            subscribers_, [callback](const auto& entry) {
                return entry->callback == callback && entry->active;
            });
        if (found == subscribers_.end()) {
            subscribers_.push_back(std::make_shared<SubscriberEntry>(callback));
        }
    }

    /**
     * @brief Unregister a callback from metrics events
     *
     * Removes the subscriber from the notification list.
     *
     * @param callback The subscriber to unregister
     */
    virtual void UnregisterMetricsCallback(
        app::metrics::IMetricsSubscriber* callback) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::unique_lock<std::mutex> lock(callbacks_lock_);
        const auto found = std::ranges::find_if(
            subscribers_, [callback](const auto& entry) {
                return entry->callback == callback;
            });
        if (found == subscribers_.end()) {
            return;
        }
        const auto entry = *found;
        entry->active = false;
        subscribers_.erase(found);
        callbacks_condition_.wait(lock, [&entry] {
            return entry->in_flight == 0U;
        });
    }   

    /**
     * @brief Get the number of registered metric callbacks
     *
     * @return Count of currently registered subscribers
     */
    virtual size_t GetCallbackCount() {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::unique_lock<std::mutex> lock(callbacks_lock_);
        return subscribers_.size();
    }   

    /**
     * @brief Set the metrics schemas discovered from the graph
     *
     * Called by the graph builder to populate available metrics descriptions.
     * Should be called before RegisterMetricsCallback().
     *
     * @param schemas Vector of metric schemas for all nodes
     *
     * @see GetNodeMetricsSchemas, NodeMetricsSchema
     */
    virtual void SetNodeMetricsSchemas(std::vector<app::metrics::NodeMetricsSchema> schemas) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> publication_guard(publication_lock_);
        std::unique_lock<std::mutex> lock(callbacks_lock_);
        constexpr std::size_t kMaximumSchemas = 2048U;
        constexpr std::size_t kMaximumDescriptors = 4096U;
        constexpr std::size_t kMaximumSchemaBytes = 262144U;
        constexpr std::size_t kMaximumJsonItems = 4096U;
        constexpr std::size_t kMaximumJsonDepth = 64U;
        std::size_t descriptor_count = 0U;
        // Exact JSON array framing for the flattened HTTP schema form. Each
        // descriptor repeats its complete target by wire contract.
        std::size_t encoded_bytes = 2U;
        std::vector<app::metrics::NodeMetricsSchema> accepted;
        std::set<std::string> accepted_metric_keys;
        const bool reject_all_schemas = schemas.size() > kMaximumSchemas;
        if (reject_all_schemas) {
            RecordRejected(MetricsRejectionCategory::SchemaContract);
        }
        accepted.reserve(std::min(schemas.size(), kMaximumSchemas));
        const auto target_is_valid = [](const app::metrics::MetricTarget& target) {
            constexpr std::size_t kMaximumNodeIdBytes = 256U;
            constexpr std::size_t kMaximumPortNameBytes = 128U;
            if (target.kind == app::metrics::MetricTarget::Kind::Node) {
                return !target.node_id.empty() &&
                       target.node_id.size() <= kMaximumNodeIdBytes &&
                       IsValidUtf8(target.node_id) &&
                       target.source_node_id.empty() &&
                       target.source_port_kind.empty() &&
                       std::holds_alternative<std::uint64_t>(
                           target.source_port) &&
                       std::get<std::uint64_t>(target.source_port) == 0U &&
                       target.target_node_id.empty() &&
                       target.target_port_kind.empty() &&
                       std::holds_alternative<std::uint64_t>(
                           target.target_port) &&
                       std::get<std::uint64_t>(target.target_port) == 0U;
            }
            const auto port_is_valid = [](const std::string& kind,
                                          const auto& port) {
                if (kind == "index") {
                    return std::holds_alternative<std::uint64_t>(port);
                }
                if (kind != "name" ||
                    !std::holds_alternative<std::string>(port)) {
                    return false;
                }
                const auto& name = std::get<std::string>(port);
                return !name.empty() && name.size() <= kMaximumPortNameBytes &&
                       IsValidUtf8(name);
            };
            return target.node_id.empty() &&
                   !target.source_node_id.empty() &&
                   target.source_node_id.size() <= kMaximumNodeIdBytes &&
                   IsValidUtf8(target.source_node_id) &&
                   !target.target_node_id.empty() &&
                   target.target_node_id.size() <= kMaximumNodeIdBytes &&
                   IsValidUtf8(target.target_node_id) &&
                   port_is_valid(target.source_port_kind,
                                 target.source_port) &&
                   port_is_valid(target.target_port_kind,
                                 target.target_port);
        };
        const auto descriptor_is_valid = [](const auto& descriptor) {
            const bool type_valid = descriptor.scalar_type == "boolean" ||
                descriptor.scalar_type == "integer" ||
                descriptor.scalar_type == "unsigned" ||
                descriptor.scalar_type == "number" ||
                descriptor.scalar_type == "string";
            const bool semantics_valid = descriptor.semantics == "gauge" ||
                descriptor.semantics == "monotonic_counter" ||
                descriptor.semantics == "state";
            const bool aggregation_valid = descriptor.aggregation == "sum" ||
                descriptor.aggregation == "min" ||
                descriptor.aggregation == "max" ||
                descriptor.aggregation == "average" ||
                descriptor.aggregation == "rate" ||
                descriptor.aggregation == "none";
            return !descriptor.metric_id.empty() &&
                   descriptor.metric_id.size() <= 128U &&
                   descriptor.unit.size() <= 32U && type_valid &&
                   semantics_valid && aggregation_valid &&
                   !descriptor.availability_rule.empty() &&
                   descriptor.availability_rule.size() <= 256U &&
                   IsValidUtf8(descriptor.metric_id) &&
                   IsValidUtf8(descriptor.scalar_type) &&
                   IsValidUtf8(descriptor.unit) &&
                   IsValidUtf8(descriptor.semantics) &&
                   IsValidUtf8(descriptor.aggregation) &&
                   IsValidUtf8(descriptor.availability_rule);
        };
        const auto target_key = [](const app::metrics::MetricTarget& target) {
            const auto encode = [](const std::string& value) {
                return std::to_string(value.size()) + ":" + value;
            };
            const auto encode_port = [](const std::string& kind, const auto& port) {
                return kind == "index"
                    ? kind + ":" + std::to_string(std::get<std::uint64_t>(port))
                    : kind + ":" +
                        std::to_string(std::get<std::string>(port).size()) + ":" +
                        std::get<std::string>(port);
            };
            return target.kind == app::metrics::MetricTarget::Kind::Node
                ? "node:" + encode(target.node_id)
                : "edge:" + encode(target.source_node_id) + "|" +
                    encode_port(target.source_port_kind, target.source_port) +
                    "|" + encode(target.target_node_id) + "|" +
                    encode_port(target.target_port_kind, target.target_port);
        };
        // Do not serialize an attacker- or plugin-controlled JSON tree merely
        // to discover that it is over the wire budget.  This estimator stops
        // at the same byte budget and at explicit item/depth budgets, keeping
        // schema admission work bounded before dump() is permitted.
        const auto bounded_json_size = [](const nlohmann::json& root)
            -> std::optional<std::size_t> {
            std::size_t bytes = 0U;
            std::size_t items = 0U;
            const auto visit = [&](const auto& self, const nlohmann::json& value,
                                   std::size_t depth) -> bool {
                if (depth > kMaximumJsonDepth ||
                    ++items > kMaximumJsonItems) {
                    return false;
                }
                if (value.is_string()) {
                    bytes += value.get_ref<const std::string&>().size() + 2U;
                } else if (value.is_array()) {
                    bytes += 2U;
                    for (const auto& child : value) {
                        if (!self(self, child, depth + 1U)) return false;
                    }
                } else if (value.is_object()) {
                    bytes += 2U;
                    for (auto iterator = value.begin(); iterator != value.end();
                         ++iterator) {
                        bytes += iterator.key().size() + 3U;
                        if (!self(self, iterator.value(), depth + 1U)) {
                            return false;
                        }
                    }
                } else {
                    // All scalar JSON encodings fit comfortably in this
                    // conservative allowance.
                    bytes += 32U;
                }
                return bytes <= kMaximumSchemaBytes;
            };
            if (!visit(visit, root, 0U)) return std::nullopt;
            return bytes;
        };
        const auto schema_count = reject_all_schemas ? 0U : schemas.size();
        for (std::size_t schema_index = 0U; schema_index < schema_count;
             ++schema_index) {
            auto& schema = schemas[schema_index];
            if (accepted.size() >= kMaximumSchemas) {
                RecordRejected(MetricsRejectionCategory::SchemaContract);
                break;
            }
            std::size_t schema_bytes = 0U;
            const bool valid_target = target_is_valid(schema.target);
            bool valid = graph_generation_ != 0U &&
                         schema.graph_generation == graph_generation_ &&
                         valid_target &&
                         schema.node_name.size() <= 256U &&
                         schema.node_type.size() <= 256U &&
                         IsValidUtf8(schema.node_name) &&
                         IsValidUtf8(schema.node_type) &&
                         schema.event_types.size() <= 64U &&
                         schema.descriptors.size() <= 64U &&
                         descriptor_count + schema.descriptors.size() <=
                             kMaximumDescriptors;
            std::vector<std::string> candidate_metric_keys;
            const auto metrics_size = bounded_json_size(schema.metrics_schema);
            const auto hints_size = bounded_json_size(schema.display_hints);
            if (!valid || !metrics_size || !hints_size) {
                RecordRejected(MetricsRejectionCategory::SchemaContract);
                continue;
            }
            try {
                schema_bytes = schema.metrics_schema.dump().size() +
                    schema.display_hints.dump().size() +
                    schema.node_name.size() + schema.node_type.size() +
                    target_key(schema.target).size() +
                    256U;  // JSON field names, delimiters, generation, framing.
            } catch (...) {
                RecordRejected(MetricsRejectionCategory::SchemaContract);
                continue;
            }
            for (const auto& event_type : schema.event_types) {
                if (event_type.empty() || event_type.size() > 128U ||
                    !IsValidUtf8(event_type)) {
                    valid = false;
                    break;
                }
                schema_bytes += event_type.size();
            }
            if (!valid) {
                RecordRejected(MetricsRejectionCategory::SchemaContract);
                continue;
            }
            for (const auto& descriptor : schema.descriptors) {
                if (!descriptor_is_valid(descriptor)) {
                    valid = false;
                    break;
                }
                schema_bytes += descriptor.metric_id.size() +
                    descriptor.scalar_type.size() + descriptor.unit.size() +
                    descriptor.semantics.size() + descriptor.aggregation.size() +
                    descriptor.availability_rule.size() + 192U;
                if (valid_target) {
                    const auto key = target_key(schema.target) + "|metric:" +
                        std::to_string(descriptor.metric_id.size()) + ":" +
                        descriptor.metric_id;
                    valid = valid && !accepted_metric_keys.contains(key) &&
                            std::ranges::find(candidate_metric_keys, key) ==
                                candidate_metric_keys.end();
                    candidate_metric_keys.push_back(key);
                    try {
                        nlohmann::json target_json;
                        if (schema.target.kind ==
                            app::metrics::MetricTarget::Kind::Node) {
                            target_json = {{"kind", "node"},
                                           {"node_id", schema.target.node_id}};
                        } else {
                            const auto port_json = [](const std::string& kind,
                                                      const auto& port) {
                                nlohmann::json value{{"kind", kind}};
                                std::visit([&value](const auto& typed) {
                                    value["value"] = typed;
                                }, port);
                                return value;
                            };
                            target_json = {
                                {"kind", "edge"},
                                {"source_node_id", schema.target.source_node_id},
                                {"source_port", port_json(
                                    schema.target.source_port_kind,
                                    schema.target.source_port)},
                                {"target_node_id", schema.target.target_node_id},
                                {"target_port", port_json(
                                    schema.target.target_port_kind,
                                    schema.target.target_port)}};
                        }
                        const auto wire_entry = nlohmann::json{
                            {"target", std::move(target_json)},
                            {"graph_generation", schema.graph_generation},
                            {"metric_id", descriptor.metric_id},
                            {"scalar_type", descriptor.scalar_type},
                            {"scalar_encoding",
                             descriptor.scalar_type == "integer" ||
                                     descriptor.scalar_type == "unsigned"
                                 ? "decimal_string" : "native"},
                            {"unit", descriptor.unit},
                            {"semantics", descriptor.semantics},
                            {"aggregation", descriptor.aggregation},
                            {"availability_rule",
                             descriptor.availability_rule}};
                        schema_bytes += wire_entry.dump().size() +
                            (descriptor_count +
                                     candidate_metric_keys.size() > 1U
                                 ? 1U : 0U);
                    } catch (...) {
                        valid = false;
                    }
                }
            }
            const auto next_descriptor_count =
                descriptor_count + schema.descriptors.size();
            const auto next_encoded_bytes = encoded_bytes + schema_bytes;
            if (next_encoded_bytes > kMaximumSchemaBytes || !valid) {
                RecordRejected(MetricsRejectionCategory::SchemaContract);
                continue;
            }
            descriptor_count = next_descriptor_count;
            encoded_bytes = next_encoded_bytes;
            accepted_metric_keys.insert(candidate_metric_keys.begin(),
                                        candidate_metric_keys.end());
            accepted.push_back(std::move(schema));
        }
        schemas_ = std::move(accepted);
        // Refresh consumers use this sequence as the complete authority
        // version, not merely the numeric graph generation. Same-generation
        // schema replacement must therefore invalidate in-flight snapshots.
        generation_sequence_.fetch_add(1U, std::memory_order_acq_rel);
        const auto generation = graph_generation_;
        const auto schema_snapshot = schemas_;
        std::vector<std::shared_ptr<SubscriberEntry>> callbacks;
        callbacks.reserve(subscribers_.size());
        for (const auto& entry : subscribers_) {
            if (entry->active) {
                ++entry->in_flight;
                callbacks.push_back(entry);
            }
        }
        lock.unlock();
        for (const auto& entry : callbacks) {
            try {
                entry->callback->OnMetricsSchemasChanged(
                    generation, schema_snapshot);
            } catch (...) {
                RecordRejected(MetricsRejectionCategory::SubscriberFailure);
            }
            {
                std::lock_guard<std::mutex> callback_lock(callbacks_lock_);
                --entry->in_flight;
            }
            callbacks_condition_.notify_all();
        }
    }

    /**
     * @brief Get all available node metrics schemas
     *
     * Returns the metric schema for each node in the graph.
     * Used by dashboard to discover what metrics to display.
     *
     * @return Vector of NodeMetricsSchema describing all nodes
     *
     * @see NodeMetricsSchema
     */
    std::vector<app::metrics::NodeMetricsSchema> GetNodeMetricsSchemas() {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        return schemas_;
    }

    /// Clear schemas and generation-specific values while preserving this
    /// capability object and its subscribers.
    virtual void ResetGeneration(const std::uint64_t generation = 0U) {
        std::lock_guard<std::mutex> publication_guard(publication_lock_);
        std::vector<std::shared_ptr<SubscriberEntry>> callbacks;
        {
            std::lock_guard<std::mutex> lock(callbacks_lock_);
            schemas_.clear();
            graph_generation_ = generation;
            generation_sequence_.fetch_add(1U, std::memory_order_acq_rel);
            callbacks.reserve(subscribers_.size());
            for (const auto& entry : subscribers_) {
                if (entry->active) {
                    ++entry->in_flight;
                    callbacks.push_back(entry);
                }
            }
        }
        for (const auto& entry : callbacks) {
            try {
                entry->callback->OnMetricsGenerationReset(generation);
            } catch (...) {
                RecordRejected(MetricsRejectionCategory::SubscriberFailure);
            }
            {
                std::lock_guard<std::mutex> lock(callbacks_lock_);
                --entry->in_flight;
            }
            callbacks_condition_.notify_all();
        }
    }

    [[nodiscard]] std::uint64_t GetGraphGeneration() const {
        std::lock_guard<std::mutex> lock(callbacks_lock_);
        return graph_generation_;
    }

    [[nodiscard]] std::uint64_t GetGenerationSequence() const noexcept {
        return generation_sequence_.load(std::memory_order_acquire);
    }

    void RecordRejected(
        const MetricsRejectionCategory category =
            MetricsRejectionCategory::Internal) noexcept {
        rejection_categories_[static_cast<std::size_t>(category)]
            .fetch_add(1U, std::memory_order_relaxed);
    }

    void RecordDroppedQueueFull() noexcept {
        dropped_queue_full_.fetch_add(1U, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t RejectedCount() const noexcept {
        return GetRejectionDiagnostics().Total();
    }

    [[nodiscard]] std::uint64_t RejectionCount(
        const MetricsRejectionCategory category) const noexcept {
        return rejection_categories_[static_cast<std::size_t>(category)]
            .load(std::memory_order_relaxed);
    }

    [[nodiscard]] MetricsRejectionDiagnostics GetRejectionDiagnostics()
        const noexcept {
        MetricsRejectionDiagnostics snapshot;
        for (std::size_t index = 0U; index < snapshot.categories.size();
             ++index) {
            snapshot.categories[index] =
                rejection_categories_[index].load(std::memory_order_relaxed);
        }
        return snapshot;
    }

    [[nodiscard]] std::uint64_t DroppedQueueFullCount() const noexcept {
        return dropped_queue_full_.load(std::memory_order_relaxed);
    }

    /// Publish a metric already bound by its producer to one exact canonical
    /// edge tuple.  This is the generic edge-publisher path; it never derives
    /// edge activity from endpoint node metrics or diagnostic names.  The
    /// matching edge schema is registered through SetNodeMetricsSchemas().
    virtual bool PublishExactEdgeMetrics(
        const app::metrics::MetricsEvent& event) noexcept {
        // The count check must precede all sample iteration so hostile vectors
        // cannot make publisher or subscriber work scale beyond the contract.
        if (event.samples.size() > 64U) {
            RecordRejected(MetricsRejectionCategory::SampleContract);
            return false;
        }
        const auto& target = event.target;
        const auto port_valid = [](const std::string& kind, const auto& port) {
            return (kind == "index" &&
                    std::holds_alternative<std::uint64_t>(port)) ||
                   (kind == "name" &&
                    std::holds_alternative<std::string>(port) &&
                    !std::get<std::string>(port).empty() &&
                    std::get<std::string>(port).size() <= 128U);
        };
        const bool exact =
            target.kind == app::metrics::MetricTarget::Kind::Edge &&
            !target.source_node_id.empty() && target.source_node_id.size() <= 256U &&
            !target.target_node_id.empty() && target.target_node_id.size() <= 256U &&
            port_valid(target.source_port_kind, target.source_port) &&
            port_valid(target.target_port_kind, target.target_port);
        if (!exact || event.graph_generation == 0U ||
            event.graph_generation != GetGraphGeneration()) {
            RecordRejected(MetricsRejectionCategory::AuthorityMismatch);
            return false;
        }
        if (!EventWithinContract(event)) {
            RecordRejected(MetricsRejectionCategory::SampleContract);
            return false;
        }
        const auto sample_valid = [](const app::metrics::MetricSample& sample) {
            const bool type_valid =
                (sample.scalar_type == "boolean" &&
                 std::holds_alternative<bool>(sample.value)) ||
                (sample.scalar_type == "integer" &&
                 std::holds_alternative<std::int64_t>(sample.value)) ||
                (sample.scalar_type == "unsigned" &&
                 std::holds_alternative<std::uint64_t>(sample.value)) ||
                (sample.scalar_type == "number" &&
                 std::holds_alternative<double>(sample.value)) ||
                (sample.scalar_type == "string" &&
                 std::holds_alternative<std::string>(sample.value));
            const bool semantics_valid = sample.semantics == "gauge" ||
                sample.semantics == "monotonic_counter" ||
                sample.semantics == "state";
            const bool aggregation_valid = sample.aggregation == "sum" ||
                sample.aggregation == "min" || sample.aggregation == "max" ||
                sample.aggregation == "average" ||
                sample.aggregation == "rate" || sample.aggregation == "none";
            return type_valid && semantics_valid && aggregation_valid &&
                (!std::holds_alternative<double>(sample.value) ||
                 std::isfinite(std::get<double>(sample.value))) &&
                !sample.metric_id.empty() &&
                sample.metric_id.size() <= 128U && sample.unit.size() <= 32U &&
                !sample.availability_rule.empty() &&
                sample.availability_rule.size() <= 256U &&
                sample.unavailable_reason.size() <= 256U &&
                (sample.available || !sample.unavailable_reason.empty()) &&
                (!std::holds_alternative<std::string>(sample.value) ||
                 std::get<std::string>(sample.value).size() <= 1024U);
        };
        for (const auto& sample : event.samples) {
            if (!sample_valid(sample)) {
                RecordRejected(MetricsRejectionCategory::SampleContract);
                return false;
            }
        }
        try {
            return InvokeSubscribers(event);
        } catch (...) {
            RecordRejected();
            return false;
        }
    }
    
    /**
     * @brief Invoke all registered subscribers with a metrics event
     *
     * Notifies all registered subscribers that a metric has been updated.
     * Typically called by MetricsPolicy during graph execution.
     * Invocation happens on the publishing thread; subscribers must be thread-safe.
     *
     * @param event The metrics event to publish (node name, metric, value, timestamp)
     *
     * @see MetricsEvent, IMetricsSubscriber, MetricsPolicy
     */
    virtual bool InvokeSubscribers(const app::metrics::MetricsEvent& event) {
        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param callbacks_lock_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        if (!EventWithinContract(event)) {
            RecordRejected(MetricsRejectionCategory::SampleContract);
            return false;
        }
        std::lock_guard<std::mutex> publication_guard(publication_lock_);
        std::vector<std::shared_ptr<SubscriberEntry>> callbacks;
        {
            std::lock_guard<std::mutex> lock(callbacks_lock_);
            if (!event.samples.empty()) {
                bool event_is_authoritative =
                    event.graph_generation == graph_generation_;
                for (const auto& sample : event.samples) {
                    const bool descriptor_is_authoritative =
                        std::ranges::any_of(schemas_, [&](const auto& schema) {
                            if (schema.graph_generation !=
                                    event.graph_generation ||
                                schema.target != event.target) {
                                return false;
                            }
                            return std::ranges::any_of(
                                schema.descriptors, [&](const auto& descriptor) {
                                    return descriptor.metric_id == sample.metric_id &&
                                        descriptor.scalar_type == sample.scalar_type &&
                                        descriptor.unit == sample.unit &&
                                        descriptor.semantics == sample.semantics &&
                                        descriptor.aggregation == sample.aggregation &&
                                        descriptor.availability_rule ==
                                            sample.availability_rule;
                                });
                        });
                    if (!descriptor_is_authoritative) {
                        event_is_authoritative = false;
                        break;
                    }
                }
                if (!event_is_authoritative) {
                    RecordRejected(MetricsRejectionCategory::AuthorityMismatch);
                    return false;
                }
            }
            callbacks.reserve(subscribers_.size());
            for (const auto& entry : subscribers_) {
                if (entry->active) {
                    ++entry->in_flight;
                    callbacks.push_back(entry);
                }
            }
        }
        for (const auto& entry : callbacks) {
            try {
                entry->callback->OnMetricsEvent(event);
            } catch (...) {
                RecordRejected(MetricsRejectionCategory::SubscriberFailure);
            }
            {
                std::lock_guard<std::mutex> lock(callbacks_lock_);
                --entry->in_flight;
            }
            callbacks_condition_.notify_all();
        }
        return true;
    }
    
protected:
    /// @brief Mutex protecting subscriber list and schemas
    mutable std::mutex callbacks_lock_;
    std::mutex publication_lock_;
    std::condition_variable callbacks_condition_;

    /// @brief List of registered metrics subscribers
    std::vector<std::shared_ptr<SubscriberEntry>> subscribers_;
    
    /// @brief Cached metric schemas discovered from graph nodes
    std::vector<app::metrics::NodeMetricsSchema> schemas_;
    std::uint64_t graph_generation_{0U};
    std::atomic<std::uint64_t> generation_sequence_{0U};
    // Lifetime counters are intentionally preserved across generation resets;
    // they are finite fixed categories rather than an unbounded error history.
    std::array<std::atomic<std::uint64_t>,
               static_cast<std::size_t>(MetricsRejectionCategory::Count)>
        rejection_categories_{};
    std::atomic<std::uint64_t> dropped_queue_full_{0U};
};

}  // namespace capabilities
