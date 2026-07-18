// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include "graph/GraphManagerCore.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace graph::dashboard {

namespace {

struct NodeActivityTotals {
  std::uint64_t inbound_messages = 0;
  std::uint64_t outbound_messages = 0;
  std::uint64_t rejected_messages = 0;
  std::uint64_t backpressure_events = 0;
  std::uint64_t peak_queue_depth = 0;
  std::size_t connected_edges = 0;
  bool thread_active = false;
};

nlohmann::json DefaultGraphMetricsJson() {
  return nlohmann::json{
      {"total_items_processed", 0},    {"total_items_rejected", 0},
      {"total_messages_processed", 0}, {"graph_total_enqueued", 0},
      {"graph_total_dequeued", 0},     {"backpressure_events", 0},
      {"peak_queue_depth", 0},         {"peak_active_threads", 0}};
}

nlohmann::json GraphMetricsJson(const graph::GraphMetrics &metrics) {
  return nlohmann::json{
      {"total_items_processed", metrics.total_items_processed.load()},
      {"total_items_rejected", metrics.total_items_rejected.load()},
      {"total_messages_processed", metrics.total_messages_processed.load()},
      {"graph_total_enqueued", metrics.graph_total_enqueued.load()},
      {"graph_total_dequeued", metrics.graph_total_dequeued.load()},
      {"backpressure_events", metrics.backpressure_events.load()},
      {"peak_queue_depth", metrics.peak_queue_depth.load()},
      {"peak_active_threads", metrics.peak_active_threads.load()}};
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

std::string ActivityColor(const std::string &state) {
  if (state == "running") {
    return "#0f766e";
  }
  if (state == "active") {
    return "#2563eb";
  }
  if (state == "backpressured") {
    return "#dc2626";
  }
  if (state == "started") {
    return "#d97706";
  }
  if (state == "initialized") {
    return "#6b7280";
  }
  return "#9ca3af";
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
  nlohmann::json snapshot{{"schema", "graphx.dashboard.metrics.v1"},
                          {"active_generation", 0},
                          {"active_config_revision", 0},
                          {"active_config_etag", ""},
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
  snapshot["active_config_revision"] = generation.config_revision;
  snapshot["active_config_etag"] = generation.config_etag;

  const auto graph_manager = generation.graph_manager;
  if (!graph_manager) {
    return snapshot;
  }

  const auto &graph_metrics = graph_manager->GetMetrics();
  snapshot["graph"] = GraphMetricsJson(graph_metrics);

  const auto &nodes = graph_manager->GetNodes();
  const auto &edges = graph_manager->GetEdges();
  std::vector<NodeActivityTotals> node_totals(nodes.size());

  for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
    const auto *metadata = graph_manager->GetEdgeMetadata(edge_index);
    const auto edge_metrics = graph_manager->GetEdgeMetrics(edge_index);
    if (!metadata || !edge_metrics) {
      continue;
    }

    const auto messages_enqueued = edge_metrics->messages_enqueued.load();
    const auto messages_dequeued = edge_metrics->messages_dequeued.load();
    const auto messages_rejected = edge_metrics->messages_rejected.load();
    const auto backpressure_events = edge_metrics->backpressure_events.load();
    const auto peak_queue_depth = edge_metrics->peak_queue_depth.load();
    const auto initialized = edge_metrics->initialized.load();
    const auto started = edge_metrics->started.load();
    const auto thread_active =
        edges[edge_index]
            ? edges[edge_index]->GetEdgeThreadMetrics().thread_active.load()
            : false;
    const auto activity_state = ActivityState(
        thread_active, messages_rejected, backpressure_events,
        messages_enqueued, messages_dequeued, initialized, started);

    if (metadata->source_node_id < node_totals.size()) {
      auto &source = node_totals[metadata->source_node_id];
      source.outbound_messages += messages_enqueued;
      source.rejected_messages += messages_rejected;
      source.backpressure_events += backpressure_events;
      source.peak_queue_depth =
          std::max(source.peak_queue_depth, peak_queue_depth);
      source.connected_edges += 1;
      source.thread_active = source.thread_active || thread_active;
    }
    if (metadata->dest_node_id < node_totals.size()) {
      auto &dest = node_totals[metadata->dest_node_id];
      dest.inbound_messages += messages_dequeued;
      dest.rejected_messages += messages_rejected;
      dest.backpressure_events += backpressure_events;
      dest.peak_queue_depth = std::max(dest.peak_queue_depth, peak_queue_depth);
      dest.connected_edges += 1;
      dest.thread_active = dest.thread_active || thread_active;
    }

    snapshot["edges"].push_back(
        {{"edge_index", edge_index},
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
         {"messages_enqueued", messages_enqueued},
         {"messages_dequeued", messages_dequeued},
         {"messages_rejected", messages_rejected},
         {"backpressure_events", backpressure_events},
         {"peak_queue_depth", peak_queue_depth},
         {"initialized", initialized},
         {"started", started},
         {"thread_active", thread_active},
         {"activity_state", activity_state},
         {"activity_color", ActivityColor(activity_state)}});
  }

  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    const auto &totals = node_totals[node_index];
    const auto activity_state =
        ActivityState(totals.thread_active, totals.rejected_messages,
                      totals.backpressure_events, totals.outbound_messages,
                      totals.inbound_messages, totals.connected_edges > 0,
                      totals.thread_active);
    snapshot["nodes"].push_back(
        {{"node_index", node_index},
         {"name", NodeName(nodes[node_index], node_index)},
         {"type", NodeType(nodes[node_index])},
         {"inbound_messages", totals.inbound_messages},
         {"outbound_messages", totals.outbound_messages},
         {"rejected_messages", totals.rejected_messages},
         {"backpressure_events", totals.backpressure_events},
         {"peak_queue_depth", totals.peak_queue_depth},
         {"connected_edges", totals.connected_edges},
         {"diagnostics_available",
          static_cast<bool>(TryGetDiagnosable(nodes[node_index]))},
         {"activity_state", activity_state},
         {"activity_color", ActivityColor(activity_state)}});
  }

  return snapshot;
}

nlohmann::json GraphSnapshotCollector::GetEdgeMetricsSnapshot() const {
  const auto metrics_snapshot = GetMetricsSnapshot();
  return nlohmann::json{
      {"schema", "graphx.dashboard.edge_metrics.v1"},
      {"active_generation", metrics_snapshot.value("active_generation", 0u)},
      {"active_config_revision",
       metrics_snapshot.value("active_config_revision", 0u)},
      {"active_config_etag",
       metrics_snapshot.value("active_config_etag", std::string{})},
      {"edges", metrics_snapshot.at("edges")}};
}

nlohmann::json GraphSnapshotCollector::GetDiagnosticsSnapshot() const {
  nlohmann::json snapshot{{"schema", "graphx.dashboard.diagnostics.v1"},
                          {"active_generation", 0},
                          {"active_config_revision", 0},
                          {"active_config_etag", ""},
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
  snapshot["active_config_revision"] = generation.config_revision;
  snapshot["active_config_etag"] = generation.config_etag;

  const auto graph_manager = generation.graph_manager;
  if (!graph_manager) {
    return snapshot;
  }

  const auto &nodes = graph_manager->GetNodes();
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index) {
    const auto diagnosable = TryGetDiagnosable(nodes[node_index]);
    if (!diagnosable) {
      continue;
    }
    snapshot["nodes"].push_back(
        {{"node_index", node_index},
         {"name", NodeName(nodes[node_index], node_index)},
         {"type", NodeType(nodes[node_index])},
         {"diagnostics", diagnosable->GetDiagnostics().Raw()}});
  }

  return snapshot;
}

} // namespace graph::dashboard
