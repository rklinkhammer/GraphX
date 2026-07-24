// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include "graph/GraphManagerCore.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace graph::dashboard {

namespace {

constexpr std::uint64_t kJavascriptSafeInteger = 9'007'199'254'740'991ULL;

struct NodeActivityTotals {
  std::uint64_t inbound_messages = 0;
  std::uint64_t outbound_messages = 0;
  std::uint64_t rejected_messages = 0;
  std::uint64_t backpressure_events = 0;
  std::uint64_t peak_queue_depth = 0;
  std::uint64_t connected_edges = 0;
  bool thread_active = false;
  bool available = true;
  std::string unavailable_reason;
};

nlohmann::json SafeMetricValue(std::uint64_t value) {
  return value <= kJavascriptSafeInteger ? nlohmann::json(value)
                                         : nlohmann::json(nullptr);
}

bool AllJavascriptSafe(std::initializer_list<std::uint64_t> values) {
  return std::ranges::all_of(
      values, [](const auto value) { return value <= kJavascriptSafeInteger; });
}

bool CheckedMetricAdd(std::uint64_t &target, std::uint64_t value) {
  if (value > kJavascriptSafeInteger ||
      target > kJavascriptSafeInteger - value) {
    return false;
  }
  target += value;
  return true;
}

nlohmann::json UnavailableDuration(std::string reason) {
  return nlohmann::json{
      {"availability", "unavailable"},
      {"reason", std::move(reason)},
      {"clock", nullptr},
      {"start_event", nullptr},
      {"end_event", nullptr},
      {"unit", "nanosecond"},
      {"count", nullptr},
      {"cumulative_total", nullptr}};
}

nlohmann::json UnavailableGraphMetricsJson(
    std::string reason = "no active runtime generation") {
  return nlohmann::json{
      {"availability", "unavailable"},
      {"unavailable_reason", std::move(reason)},
      {"total_items_processed", nullptr},
      {"total_items_rejected", nullptr},
      {"total_messages_processed", nullptr},
      {"graph_total_enqueued", nullptr},
      {"graph_total_dequeued", nullptr},
      {"backpressure_events", nullptr},
      {"peak_queue_depth", nullptr},
      {"peak_active_threads", nullptr}};
}

nlohmann::json DefaultGraphMetricsJson() {
  return UnavailableGraphMetricsJson();
}

nlohmann::json GraphMetricsJson(const graph::GraphMetrics &metrics) {
  if (metrics.aggregation_overflow.load()) {
    return UnavailableGraphMetricsJson(
        "graph aggregate overflowed uint64 representation");
  }
  const auto total_items_processed = metrics.total_items_processed.load();
  const auto total_items_rejected = metrics.total_items_rejected.load();
  const auto total_messages_processed =
      metrics.total_messages_processed.load();
  const auto graph_total_enqueued = metrics.graph_total_enqueued.load();
  const auto graph_total_dequeued = metrics.graph_total_dequeued.load();
  const auto backpressure_events = metrics.backpressure_events.load();
  const auto peak_queue_depth = metrics.peak_queue_depth.load();
  const auto peak_active_threads = metrics.peak_active_threads.load();
  const bool available = AllJavascriptSafe(
      {total_items_processed, total_items_rejected, total_messages_processed,
       graph_total_enqueued, graph_total_dequeued, backpressure_events,
       peak_queue_depth, peak_active_threads});
  if (!available) {
    return UnavailableGraphMetricsJson(
        "one or more graph metrics exceed JavaScript safe integer "
        "representation");
  }
  return nlohmann::json{
      {"availability", "available"},
      {"unavailable_reason", nullptr},
      {"total_items_processed", SafeMetricValue(total_items_processed)},
      {"total_items_rejected", SafeMetricValue(total_items_rejected)},
      {"total_messages_processed", SafeMetricValue(total_messages_processed)},
      {"graph_total_enqueued", SafeMetricValue(graph_total_enqueued)},
      {"graph_total_dequeued", SafeMetricValue(graph_total_dequeued)},
      {"backpressure_events", SafeMetricValue(backpressure_events)},
      {"peak_queue_depth", SafeMetricValue(peak_queue_depth)},
      {"peak_active_threads", SafeMetricValue(peak_active_threads)}};
}

nlohmann::json MetricDefinitionsJson() {
  const auto definition = [](std::string name, std::string field,
                             std::string scope, std::string kind,
                             std::string unit, bool monotonic,
                             std::string reset, std::string capture,
                             std::string aggregation, std::string overflow,
                             std::string representation) {
    return nlohmann::json{{"name", std::move(name)},
                          {"field", std::move(field)},
                          {"scope", std::move(scope)},
                          {"kind", std::move(kind)},
                          {"unit", std::move(unit)},
                          {"monotonic", monotonic},
                          {"availability", "explicit"},
                          {"capture", std::move(capture)},
                          {"reset", std::move(reset)},
                          {"aggregation", std::move(aggregation)},
                          {"overflow", std::move(overflow)},
                          {"numeric_representation",
                           std::move(representation)}};
  };
  const auto counter = [&](std::string name, std::string scope,
                           std::string unit) {
    const auto field = "/" + scope + (scope == "graph" ? "/" : "s/*/") + name;
    const auto aggregation =
        scope == "graph"
            ? "runtime manager aggregate loaded atomically; never re-summed "
              "by dashboard"
        : scope == "node"
            ? "checked sum of incident canonical edge counters in one "
              "collection"
            : "direct atomic edge counter; never re-summed by dashboard";
    return definition(
        std::move(name), field, std::move(scope), "counter", std::move(unit),
        true, "new_runtime_manager",
        "atomic relaxed-load within one collector snapshot", aggregation,
        "unavailable_above_javascript_safe_integer",
        "non_negative_javascript_safe_integer");
  };
  const auto current_gauge = [&](std::string name, std::string scope,
                                 std::string unit) {
    const auto field = "/" + scope + (scope == "graph" ? "/" : "s/*/") + name;
    return definition(
        std::move(name), field, std::move(scope), "gauge", std::move(unit),
        false, "sample_replaced", "instantaneous collector read",
        "no cross-sample aggregation",
        "unavailable_above_javascript_safe_integer",
        "non_negative_javascript_safe_integer");
  };
  const auto peak_gauge = [&](std::string name, std::string scope,
                              std::string unit) {
    const auto field = "/" + scope + (scope == "graph" ? "/" : "s/*/") + name;
    return definition(
        std::move(name), field, std::move(scope), "gauge", std::move(unit),
        false, "new_runtime_manager",
        "atomic relaxed-load within one collector snapshot",
        "maximum retained by the active runtime manager",
        "unavailable_above_javascript_safe_integer",
        "non_negative_javascript_safe_integer");
  };
  const auto state = [&](std::string name, std::string scope,
                         std::string unit, std::string representation) {
    const auto field = "/" + scope + "s/*/" + name;
    return definition(std::move(name), field, std::move(scope), "state",
                      std::move(unit), false, "sample_replaced",
                      "instantaneous collector read",
                      "one state per canonical record", "not_applicable",
                      std::move(representation));
  };
  const auto duration = [&](std::string name, std::string reset,
                            std::string aggregation) {
    return definition(
        name, "/edges/*/" + name, "edge", "distribution", "nanosecond",
        false, std::move(reset), "collector-qualified timing resource",
        std::move(aggregation), "unavailable_above_javascript_safe_integer",
        "structured_duration");
  };
  return nlohmann::json::array(
      {counter("total_items_processed", "graph", "item"),
       counter("total_items_rejected", "graph", "item"),
       counter("total_messages_processed", "graph", "message"),
       counter("graph_total_enqueued", "graph", "message"),
       counter("graph_total_dequeued", "graph", "message"),
       counter("backpressure_events", "graph", "event"),
       peak_gauge("peak_queue_depth", "graph", "message"),
       peak_gauge("peak_active_threads", "graph", "thread"),
       counter("inbound_messages", "node", "message"),
       counter("outbound_messages", "node", "message"),
       counter("rejected_messages", "node", "message"),
       counter("backpressure_events", "node", "event"),
       peak_gauge("peak_queue_depth", "node", "message"),
       current_gauge("connected_edges", "node", "edge"),
       state("diagnostics_available", "node", "boolean", "boolean"),
       state("activity_state", "node", "state", "enumerated_string"),
       counter("messages_enqueued", "edge", "message"),
       counter("messages_dequeued", "edge", "message"),
       counter("messages_rejected", "edge", "message"),
       counter("backpressure_events", "edge", "event"),
       current_gauge("current_queue_depth", "edge", "message"),
       peak_gauge("peak_queue_depth", "edge", "message"),
       duration("transfer_service_duration", "new_runtime_manager",
                "cumulative successful transfer-call duration and count"),
       state("initialized", "edge", "boolean", "boolean"),
       state("started", "edge", "boolean", "boolean"),
       state("thread_active", "edge", "boolean", "boolean"),
       state("activity_state", "edge", "state", "enumerated_string"),
       duration("queue_residence_duration", "not_collected",
                "explicitly unavailable; GraphX does not timestamp queue "
                "entry and exit"),
       duration("node_processing_duration", "not_collected",
                "explicitly unavailable; node service intervals are not "
                "collected here"),
       duration("end_to_end_duration", "not_collected",
                "explicitly unavailable; messages are not end-to-end "
                "correlated"),
       duration("dashboard_delivery_duration", "not_collected",
                "explicitly unavailable; browser delivery is outside the "
                "runtime metric boundary")});
}

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::shared_ptr<graph::NodeFacadeAdapterWrapper>
TryGetWrapper(const std::shared_ptr<graph::INode> &node) {
  return std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
}

std::string NodeName(const std::shared_ptr<graph::INode> &node,
                     std::size_t index) {
  if (const auto wrapper = TryGetWrapper(node); wrapper) {
    const auto name = wrapper->GetName();
    if (!name.empty()) {
      return name;
    }
  }
  return "node_" + std::to_string(index);
}

std::string NodeType(const std::shared_ptr<graph::INode> &node) {
  if (const auto wrapper = TryGetWrapper(node); wrapper) {
    const auto type = wrapper->GetType();
    if (!type.empty()) {
      return type;
    }
  }
  return "unknown";
}

std::shared_ptr<graph::IDiagnosable>
TryGetDiagnosable(const std::shared_ptr<graph::INode> &node) {
  if (const auto wrapper = TryGetWrapper(node); wrapper) {
    return wrapper->GetAdapter()->TryGetInterface<graph::IDiagnosable>();
  }
  return nullptr;
}

std::string ActivityState(bool thread_active, std::uint64_t rejected_messages,
                          std::uint64_t backpressure_events,
                          std::uint64_t enqueued_messages,
                          std::uint64_t dequeued_messages, bool initialized,
                          bool started) {
  if (thread_active) {
    return "running";
  }
  if (rejected_messages > 0 || backpressure_events > 0) {
    return "backpressured";
  }
  if (enqueued_messages > 0 || dequeued_messages > 0) {
    return "active";
  }
  if (started) {
    return "started";
  }
  if (initialized) {
    return "initialized";
  }
  return "idle";
}

} // namespace

void GraphSnapshotCollector::BindRuntimeSession(
    std::shared_ptr<GraphRuntimeSession> runtime_session) {
  runtime_session_ = runtime_session;
}

void GraphSnapshotCollector::InjectNextCollectionInterruptionForTesting() {
  interrupt_next_collection_.store(true);
}

bool GraphSnapshotCollector::ConsumeCollectionInterruption() const {
  return interrupt_next_collection_.exchange(false);
}

nlohmann::json GraphSnapshotCollector::GetMetricsSnapshot() const {
  const auto sampled_at = MonotonicMilliseconds();
  nlohmann::json snapshot{{"schema", "graphx.dashboard.metrics.v1"},
                          {"active_generation", 0},
                          {"active_run_epoch", 0},
                          {"active_config_revision", 0},
                          {"active_config_etag", ""},
                          {"capture_id", "inactive"},
                          {"sampled_at_monotonic_ms", sampled_at},
                          {"collection_interval",
                           {{"state", "unavailable"},
                            {"reason", "no compatible previous sample"},
                            {"clock", "steady_clock"},
                            {"duration_ms", nullptr}}},
                          {"rate_availability",
                           {{"state", "unavailable"},
                            {"reason",
                             "rates require two compatible same-run samples"}}},
                          {"qualified_rates", nlohmann::json::array()},
                          {"identity_availability",
                           {{"state", "unavailable"},
                            {"reason", "no active runtime generation"}}},
                          {"metric_definitions", MetricDefinitionsJson()},
                          {"graph", DefaultGraphMetricsJson()},
                          {"nodes", nlohmann::json::array()},
                          {"edges", nlohmann::json::array()}};

  if (ConsumeCollectionInterruption()) {
    return snapshot;
  }

  const auto runtime_session = runtime_session_.lock();
  if (!runtime_session) {
    return snapshot;
  }
  const auto generation = runtime_session->SnapshotGeneration();
  snapshot["active_generation"] = generation.generation;
  snapshot["active_run_epoch"] = generation.run_epoch;
  snapshot["active_config_revision"] = generation.config_revision;
  snapshot["active_config_etag"] = generation.config_etag;
  snapshot["capture_id"] =
      "g" + std::to_string(generation.generation) + "-r" +
      std::to_string(generation.run_epoch) + "-c" +
      std::to_string(generation.config_revision) + "-m" +
      std::to_string(sampled_at);

  const auto graph_manager = generation.graph_manager;
  if (!graph_manager) {
    return snapshot;
  }

  const auto &graph_metrics = graph_manager->GetMetrics();
  snapshot["graph"] = GraphMetricsJson(graph_metrics);
  {
    const bool aggregate_valid = !graph_metrics.aggregation_overflow.load();
    const auto enqueued = graph_metrics.graph_total_enqueued.load();
    const auto dequeued = graph_metrics.graph_total_dequeued.load();
    const std::lock_guard rate_lock(rate_mutex_);
    const auto &previous = previous_rate_sample_;
    const bool same_identity =
        previous.valid && previous.generation == generation.generation &&
        previous.run_epoch == generation.run_epoch &&
        previous.config_revision == generation.config_revision &&
        previous.config_etag == generation.config_etag;
    const bool positive_interval =
        same_identity && sampled_at > previous.sampled_at_monotonic_ms;
    const bool counters_representable =
        aggregate_valid && AllJavascriptSafe({enqueued, dequeued});
    const bool counters_monotonic =
        counters_representable && positive_interval &&
        enqueued >= previous.enqueued &&
        dequeued >= previous.dequeued;
    if (counters_monotonic) {
      const auto duration_ms =
          sampled_at - previous.sampled_at_monotonic_ms;
      const auto per_second = [duration_ms](std::uint64_t delta) {
        return static_cast<double>(delta) * 1000.0 /
               static_cast<double>(duration_ms);
      };
      snapshot["collection_interval"] = {
          {"state", "available"},
          {"reason", nullptr},
          {"clock", "steady_clock"},
          {"duration_ms", duration_ms}};
      snapshot["rate_availability"] = {
          {"state", "available"}, {"reason", nullptr}};
      snapshot["qualified_rates"] = nlohmann::json::array(
          {{{"name", "graph_total_enqueued"},
            {"scope", "graph"},
            {"unit", "messages_per_second"},
            {"interval_ms", duration_ms},
            {"value", per_second(enqueued - previous.enqueued)}},
           {{"name", "graph_total_dequeued"},
            {"scope", "graph"},
            {"unit", "messages_per_second"},
            {"interval_ms", duration_ms},
            {"value", per_second(dequeued - previous.dequeued)}}});
    } else if (!counters_representable) {
      snapshot["rate_availability"]["reason"] =
          aggregate_valid
              ? "rate counters exceed JavaScript safe integer representation"
              : "graph aggregate overflowed uint64 representation";
    } else if (same_identity && positive_interval) {
      snapshot["rate_availability"]["reason"] =
          "counter regression invalidated the interval";
    } else if (same_identity) {
      snapshot["rate_availability"]["reason"] =
          "zero or negative monotonic interval";
    } else if (previous.valid) {
      snapshot["rate_availability"]["reason"] =
          "generation, run, or configuration identity changed";
    }
    previous_rate_sample_ =
        counters_representable
            ? PreviousRateSample{
                  .generation = generation.generation,
                  .run_epoch = generation.run_epoch,
                  .config_revision = generation.config_revision,
                  .config_etag = generation.config_etag,
                  .sampled_at_monotonic_ms = sampled_at,
                  .enqueued = enqueued,
                  .dequeued = dequeued,
                  .valid = true}
            : PreviousRateSample{};
  }

  const auto &nodes = graph_manager->GetNodes();
  const auto &edges = graph_manager->GetEdges();
  const auto &runtime_node_ids = graph_manager->GetCanonicalNodeIds();
  if (!generation.identity_error.empty() ||
      runtime_node_ids.size() != nodes.size() ||
      generation.canonical_edges.size() != edges.size()) {
    snapshot["identity_availability"] = {
        {"state", "unavailable"},
        {"reason", generation.identity_error.empty()
                       ? "runtime and configuration identity cardinality differ"
                       : generation.identity_error}};
    snapshot["graph"] = UnavailableGraphMetricsJson(
        snapshot["identity_availability"]["reason"].get<std::string>());
    return snapshot;
  }
  const std::set<std::string> configured_node_ids(
      generation.canonical_node_ids.begin(),
      generation.canonical_node_ids.end());
  const std::set<std::string> runtime_node_id_set(runtime_node_ids.begin(),
                                                  runtime_node_ids.end());
  if (runtime_node_id_set.size() != runtime_node_ids.size() ||
      runtime_node_id_set != configured_node_ids ||
      runtime_node_id_set.contains("")) {
    snapshot["identity_availability"] = {
        {"state", "unavailable"},
        {"reason",
         "runtime node identities do not match the active configuration"}};
    snapshot["graph"] = UnavailableGraphMetricsJson(
        snapshot["identity_availability"]["reason"].get<std::string>());
    return snapshot;
  }
  std::unordered_map<std::string,
                     const GraphRuntimeSession::GenerationSnapshot::EdgeIdentity *>
      configured_edges;
  for (const auto &identity : generation.canonical_edges) {
    if (!configured_edges.emplace(identity.edge_id, &identity).second) {
      snapshot["identity_availability"] = {
          {"state", "unavailable"},
          {"reason", "active configuration contains duplicate edge identity"}};
      snapshot["graph"] = UnavailableGraphMetricsJson(
          snapshot["identity_availability"]["reason"].get<std::string>());
      return snapshot;
    }
  }
  snapshot["identity_availability"] = {
      {"state", "available"},
      {"reason", nullptr},
      {"canonical_node_count", generation.canonical_node_ids.size()},
      {"canonical_edge_count", generation.canonical_edges.size()}};
  std::vector<NodeActivityTotals> node_totals(nodes.size());
  std::set<std::string> published_edges;

  for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
    const auto *metadata = graph_manager->GetEdgeMetadata(edge_index);
    const auto edge_metrics = graph_manager->GetEdgeMetrics(edge_index);
    if (!metadata || !edge_metrics) {
      snapshot["identity_availability"] = {
          {"state", "unavailable"},
          {"reason",
           "runtime edge metadata or metrics are missing for a canonical "
           "edge"}};
      snapshot["graph"] = UnavailableGraphMetricsJson(
          snapshot["identity_availability"]["reason"].get<std::string>());
      snapshot["nodes"] = nlohmann::json::array();
      snapshot["edges"] = nlohmann::json::array();
      return snapshot;
    }
    if (metadata->source_node_id >= runtime_node_ids.size() ||
        metadata->dest_node_id >= runtime_node_ids.size()) {
      snapshot["identity_availability"] = {
          {"state", "unavailable"},
          {"reason", "runtime edge metadata disagrees with canonical identity"}};
      snapshot["graph"] = UnavailableGraphMetricsJson(
          snapshot["identity_availability"]["reason"].get<std::string>());
      snapshot["nodes"] = nlohmann::json::array();
      snapshot["edges"] = nlohmann::json::array();
      return snapshot;
    }
    const auto runtime_edge_id =
        CanonicalEdgeId(runtime_node_ids[metadata->source_node_id],
                        metadata->source_port_id,
                        runtime_node_ids[metadata->dest_node_id],
                        metadata->dest_port_id);
    const auto configured_edge = configured_edges.find(runtime_edge_id);
    if (configured_edge == configured_edges.end() ||
        !published_edges.insert(runtime_edge_id).second) {
      snapshot["identity_availability"] = {
          {"state", "unavailable"},
          {"reason", "runtime edge metadata disagrees with canonical identity"}};
      snapshot["graph"] = UnavailableGraphMetricsJson(
          snapshot["identity_availability"]["reason"].get<std::string>());
      snapshot["nodes"] = nlohmann::json::array();
      snapshot["edges"] = nlohmann::json::array();
      return snapshot;
    }
    const auto &identity = *configured_edge->second;

    const auto messages_enqueued = edge_metrics->messages_enqueued.load();
    const auto messages_dequeued = edge_metrics->messages_dequeued.load();
    const auto messages_rejected = edge_metrics->messages_rejected.load();
    const auto backpressure_events = edge_metrics->backpressure_events.load();
    const auto recorded_peak_queue_depth = edge_metrics->peak_queue_depth.load();
    const auto initialized = edge_metrics->initialized.load();
    const auto started = edge_metrics->started.load();
    const auto current_queue_depth =
        edges[edge_index] ? edges[edge_index]->GetQueueSize() : 0u;
    const auto peak_queue_depth =
        std::max<std::uint64_t>(recorded_peak_queue_depth,
                                current_queue_depth);
    const auto thread_active =
        edges[edge_index]
            ? edges[edge_index]->GetEdgeThreadMetrics().thread_active.load()
            : false;
    const auto transfer_total = edge_metrics->total_queue_time_ns.load();
    const bool edge_object_available = static_cast<bool>(edges[edge_index]);
    const bool edge_metrics_available =
        edge_object_available &&
        AllJavascriptSafe(
            {messages_enqueued, messages_dequeued, messages_rejected,
             backpressure_events, current_queue_depth, peak_queue_depth});
    const auto edge_unavailable_reason =
        !edge_object_available
            ? "runtime edge object is unavailable"
        : !edge_metrics_available
            ? "one or more edge metrics exceed JavaScript safe integer "
              "representation"
            : "";
    const auto activity_state =
        edge_metrics_available
            ? ActivityState(thread_active, messages_rejected,
                            backpressure_events, messages_enqueued,
                            messages_dequeued, initialized, started)
            : "unavailable";

    if (metadata->source_node_id < node_totals.size()) {
      auto &source = node_totals[metadata->source_node_id];
      source.available =
          source.available && edge_metrics_available &&
          CheckedMetricAdd(source.outbound_messages, messages_enqueued) &&
          CheckedMetricAdd(source.rejected_messages, messages_rejected) &&
          CheckedMetricAdd(source.backpressure_events, backpressure_events);
      if (source.available) {
        source.peak_queue_depth =
            std::max(source.peak_queue_depth, peak_queue_depth);
      } else {
        source.unavailable_reason =
            "incident edge metrics are unavailable or exceed JavaScript safe "
            "integer representation";
      }
      source.available =
          source.available && CheckedMetricAdd(source.connected_edges, 1);
      source.thread_active = source.thread_active || thread_active;
    }
    if (metadata->dest_node_id < node_totals.size()) {
      auto &dest = node_totals[metadata->dest_node_id];
      dest.available =
          dest.available && edge_metrics_available &&
          CheckedMetricAdd(dest.inbound_messages, messages_dequeued) &&
          CheckedMetricAdd(dest.rejected_messages, messages_rejected) &&
          CheckedMetricAdd(dest.backpressure_events, backpressure_events);
      if (dest.available) {
        dest.peak_queue_depth =
            std::max(dest.peak_queue_depth, peak_queue_depth);
      } else {
        dest.unavailable_reason =
            "incident edge metrics are unavailable or exceed JavaScript safe "
            "integer representation";
      }
      dest.available =
          dest.available && CheckedMetricAdd(dest.connected_edges, 1);
      dest.thread_active = dest.thread_active || thread_active;
    }

    const auto current_depth_availability =
        edge_object_available && current_queue_depth <= kJavascriptSafeInteger
            ? nlohmann::json{{"state", "available"}, {"reason", nullptr}}
            : nlohmann::json{
                  {"state", "unavailable"},
                  {"reason",
                   edge_object_available
                       ? "current queue depth exceeds JavaScript safe integer "
                         "representation"
                       : "runtime edge object is unavailable"}};
    const bool transfer_available =
        edge_metrics_available && messages_dequeued > 0 &&
        transfer_total <= kJavascriptSafeInteger;
    snapshot["edges"].push_back(
        {{"edge_id", identity.edge_id},
         {"source_node_id", identity.source_node_id},
         {"source_port", identity.source_port},
         {"destination_node_id", identity.destination_node_id},
         {"destination_port", identity.destination_port},
         {"availability",
          edge_metrics_available ? "available" : "unavailable"},
         {"unavailable_reason",
          edge_metrics_available ? nlohmann::json(nullptr)
                                 : nlohmann::json(edge_unavailable_reason)},
         {"edge_index", edge_index},
         {"source_node_index", metadata->source_node_id},
         {"source_node_name", metadata->source_node_id < nodes.size()
                                  ? NodeName(nodes[metadata->source_node_id],
                                             metadata->source_node_id)
                                  : std::string{}},
         {"source_port_index", metadata->source_port_id},
         {"destination_node_index", metadata->dest_node_id},
         {"destination_node_name",
          metadata->dest_node_id < nodes.size()
              ? NodeName(nodes[metadata->dest_node_id], metadata->dest_node_id)
              : std::string{}},
         {"destination_port_index", metadata->dest_port_id},
         {"message_type", metadata->message_type_demangled.empty()
                              ? metadata->message_type_name
                              : metadata->message_type_demangled},
         {"messages_enqueued",
          edge_metrics_available ? SafeMetricValue(messages_enqueued)
                                 : nlohmann::json(nullptr)},
         {"messages_dequeued",
          edge_metrics_available ? SafeMetricValue(messages_dequeued)
                                 : nlohmann::json(nullptr)},
         {"messages_rejected",
          edge_metrics_available ? SafeMetricValue(messages_rejected)
                                 : nlohmann::json(nullptr)},
         {"backpressure_events",
          edge_metrics_available ? SafeMetricValue(backpressure_events)
                                 : nlohmann::json(nullptr)},
         {"current_queue_depth",
          current_depth_availability.at("state") == "available"
              ? SafeMetricValue(current_queue_depth)
              : nlohmann::json(nullptr)},
         {"current_queue_depth_availability", current_depth_availability},
         {"peak_queue_depth",
          edge_metrics_available ? SafeMetricValue(peak_queue_depth)
                                 : nlohmann::json(nullptr)},
         {"transfer_service_duration",
          transfer_available
              ? nlohmann::json{
                    {"availability", "available"},
                    {"reason", nullptr},
                    {"clock", "steady_clock"},
                    {"start_event", "DynamicEdge TransferTo call entry"},
                    {"end_event", "DynamicEdge TransferTo call return"},
                    {"unit", "nanosecond"},
                    {"count", SafeMetricValue(messages_dequeued)},
                    {"cumulative_total", SafeMetricValue(transfer_total)}}
              : UnavailableDuration(
                    !edge_object_available || !edge_metrics_available
                        ? edge_unavailable_reason
                    : messages_dequeued == 0
                        ? "no successful transfer calls were observed"
                    : transfer_total > kJavascriptSafeInteger
                        ? "cumulative transfer duration exceeds JavaScript "
                          "safe integer representation"
                        : edge_unavailable_reason)},
         {"queue_residence_duration",
          UnavailableDuration(
              "queue entry and dequeue timestamps are not collected")},
         {"node_processing_duration",
          UnavailableDuration("node service intervals are not collected")},
         {"end_to_end_duration",
          UnavailableDuration(
              "messages are not correlated end-to-end by the collector")},
         {"dashboard_delivery_duration",
          UnavailableDuration(
              "browser delivery is outside the runtime metric boundary")},
         {"initialized", initialized},
         {"started", started},
         {"thread_active", thread_active},
         {"activity_state", activity_state}});
  }

  if (published_edges.size() != configured_edges.size()) {
    snapshot["identity_availability"] = {
        {"state", "unavailable"},
        {"reason", "not every canonical edge was published"}};
    snapshot["graph"] = UnavailableGraphMetricsJson(
        snapshot["identity_availability"]["reason"].get<std::string>());
    snapshot["nodes"] = nlohmann::json::array();
    snapshot["edges"] = nlohmann::json::array();
    return snapshot;
  }

  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    const auto &totals = node_totals[node_index];
    const auto activity_state = totals.available
                                    ? ActivityState(
                                          totals.thread_active,
                                          totals.rejected_messages,
                                          totals.backpressure_events,
                                          totals.outbound_messages,
                                          totals.inbound_messages,
                                          totals.connected_edges > 0,
                                          totals.thread_active)
                                    : "unavailable";
    snapshot["nodes"].push_back(
        {{"node_id", runtime_node_ids[node_index]},
         {"availability", totals.available ? "available" : "unavailable"},
         {"unavailable_reason",
          totals.available ? nlohmann::json(nullptr)
                           : nlohmann::json(totals.unavailable_reason)},
         {"node_index", node_index},
         {"name", NodeName(nodes[node_index], node_index)},
         {"type", NodeType(nodes[node_index])},
         {"inbound_messages",
          totals.available ? SafeMetricValue(totals.inbound_messages)
                           : nlohmann::json(nullptr)},
         {"outbound_messages",
          totals.available ? SafeMetricValue(totals.outbound_messages)
                           : nlohmann::json(nullptr)},
         {"rejected_messages",
          totals.available ? SafeMetricValue(totals.rejected_messages)
                           : nlohmann::json(nullptr)},
         {"backpressure_events",
          totals.available ? SafeMetricValue(totals.backpressure_events)
                           : nlohmann::json(nullptr)},
         {"peak_queue_depth",
          totals.available ? SafeMetricValue(totals.peak_queue_depth)
                           : nlohmann::json(nullptr)},
         {"connected_edges",
          totals.available
              ? SafeMetricValue(
                    static_cast<std::uint64_t>(totals.connected_edges))
              : nlohmann::json(nullptr)},
         {"diagnostics_available",
          static_cast<bool>(TryGetDiagnosable(nodes[node_index]))},
         {"activity_state", activity_state}});
  }

  return snapshot;
}

nlohmann::json GraphSnapshotCollector::GetEdgeMetricsSnapshot() const {
  const auto metrics_snapshot = GetMetricsSnapshot();
  return nlohmann::json{
      {"schema", "graphx.dashboard.edge_metrics.v1"},
      {"active_generation", metrics_snapshot.value("active_generation", 0u)},
      {"active_run_epoch", metrics_snapshot.value("active_run_epoch", 0u)},
      {"active_config_revision",
       metrics_snapshot.value("active_config_revision", 0u)},
      {"active_config_etag",
       metrics_snapshot.value("active_config_etag", std::string{})},
      {"capture_id", metrics_snapshot.value("capture_id", std::string{})},
      {"sampled_at_monotonic_ms",
       metrics_snapshot.value("sampled_at_monotonic_ms", 0u)},
      {"identity_availability", metrics_snapshot.at("identity_availability")},
      {"edges", metrics_snapshot.at("edges")}};
}

nlohmann::json GraphSnapshotCollector::GetDiagnosticsSnapshot() const {
  const auto sampled_at = MonotonicMilliseconds();
  nlohmann::json snapshot{{"schema", "graphx.dashboard.diagnostics.v1"},
                          {"active_generation", 0},
                          {"active_run_epoch", 0},
                          {"active_config_revision", 0},
                          {"active_config_etag", ""},
                          {"capture_id", "inactive"},
                          {"sampled_at_monotonic_ms", sampled_at},
                          {"identity_availability",
                           {{"state", "unavailable"},
                            {"reason", "no active runtime generation"}}},
                          {"nodes", nlohmann::json::array()}};

  if (ConsumeCollectionInterruption()) {
    return snapshot;
  }

  const auto runtime_session = runtime_session_.lock();
  if (!runtime_session) {
    return snapshot;
  }
  const auto generation = runtime_session->SnapshotGeneration();
  snapshot["active_generation"] = generation.generation;
  snapshot["active_run_epoch"] = generation.run_epoch;
  snapshot["active_config_revision"] = generation.config_revision;
  snapshot["active_config_etag"] = generation.config_etag;
  snapshot["capture_id"] =
      "g" + std::to_string(generation.generation) + "-r" +
      std::to_string(generation.run_epoch) + "-c" +
      std::to_string(generation.config_revision) + "-m" +
      std::to_string(sampled_at);

  const auto graph_manager = generation.graph_manager;
  if (!graph_manager) {
    return snapshot;
  }

  const auto &nodes = graph_manager->GetNodes();
  const auto &runtime_node_ids = graph_manager->GetCanonicalNodeIds();
  if (!generation.identity_error.empty() ||
      runtime_node_ids.size() != nodes.size()) {
    snapshot["identity_availability"] = {
        {"state", "unavailable"},
        {"reason", generation.identity_error.empty()
                       ? "runtime and configuration identity cardinality differ"
                       : generation.identity_error}};
    return snapshot;
  }
  const std::set<std::string> configured_node_ids(
      generation.canonical_node_ids.begin(),
      generation.canonical_node_ids.end());
  const std::set<std::string> runtime_node_id_set(runtime_node_ids.begin(),
                                                  runtime_node_ids.end());
  if (runtime_node_id_set.size() != runtime_node_ids.size() ||
      runtime_node_id_set != configured_node_ids ||
      runtime_node_id_set.contains("")) {
    snapshot["identity_availability"] = {
        {"state", "unavailable"},
        {"reason",
         "runtime node identities do not match the active configuration"}};
    return snapshot;
  }
  snapshot["identity_availability"] = {
      {"state", "available"}, {"reason", nullptr}};
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    const auto diagnosable = TryGetDiagnosable(nodes[node_index]);
    snapshot["nodes"].push_back(
        {{"node_id", runtime_node_ids[node_index]},
         {"availability", diagnosable ? "available" : "unavailable"},
         {"unavailable_reason",
          diagnosable ? nlohmann::json(nullptr)
                      : nlohmann::json("node does not implement diagnostics")},
         {"node_index", node_index},
         {"name", NodeName(nodes[node_index], node_index)},
         {"type", NodeType(nodes[node_index])},
         {"diagnostics", diagnosable ? diagnosable->GetDiagnostics().Raw()
                                     : nlohmann::json(nullptr)}});
  }

  return snapshot;
}

} // namespace graph::dashboard
