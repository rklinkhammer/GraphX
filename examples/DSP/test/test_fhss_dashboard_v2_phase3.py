#!/usr/bin/env python3
"""Independent Phase 3 identity and schema contract checks."""

from __future__ import annotations

import json
import os
from pathlib import Path

import jsonschema
from referencing import Registry, Resource


ROOT = Path(os.environ.get("GRAPHX_SOURCE_ROOT", Path(__file__).parents[3]))
SCHEMAS = ROOT / "examples/DSP/dashboard/api/schemas"


def load(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> None:
    documents = {path: load(path) for path in SCHEMAS.glob("*.schema.json")}
    registry = Registry()
    for document in documents.values():
        if isinstance(document, dict) and "$id" in document:
            registry = registry.with_resource(
                str(document["$id"]), Resource.from_contents(document)
            )
    for name in ("metrics", "edge-metrics", "diagnostics", "fhss-snapshot", "event"):
        jsonschema.Draft202012Validator.check_schema(
            documents[SCHEMAS / f"{name}.schema.json"]
        )

    graph = load(ROOT / "libdsp/config/fhss_phase2_binary_iq_receiver.json")
    assert isinstance(graph, dict)
    nodes = graph["nodes"]
    edges = graph["edges"]
    assert len(nodes) == 75
    assert len(edges) == 137
    node_ids = [node["id"] for node in nodes]
    assert len(set(node_ids)) == 75
    edge_ids = [
        f'{edge["source_node_id"]}:{edge["source_port"]}->'
        f'{edge["target_node_id"]}:{edge["target_port"]}'
        for edge in edges
    ]
    assert len(set(edge_ids)) == 137
    for channel, merge_input in ((0, 1), (31, 32), (63, 64)):
        assert f"detector_{channel}:0->merge:{merge_input}" in edge_ids

    metric_specs = [
        ("total_items_processed", "graph", "counter", "item"),
        ("total_items_rejected", "graph", "counter", "item"),
        ("total_messages_processed", "graph", "counter", "message"),
        ("graph_total_enqueued", "graph", "counter", "message"),
        ("graph_total_dequeued", "graph", "counter", "message"),
        ("backpressure_events", "graph", "counter", "event"),
        ("peak_queue_depth", "graph", "gauge", "message"),
        ("peak_active_threads", "graph", "gauge", "thread"),
        ("inbound_messages", "node", "counter", "message"),
        ("outbound_messages", "node", "counter", "message"),
        ("rejected_messages", "node", "counter", "message"),
        ("backpressure_events", "node", "counter", "event"),
        ("peak_queue_depth", "node", "gauge", "message"),
        ("connected_edges", "node", "gauge", "edge"),
        ("diagnostics_available", "node", "state", "boolean"),
        ("activity_state", "node", "state", "state"),
        ("messages_enqueued", "edge", "counter", "message"),
        ("messages_dequeued", "edge", "counter", "message"),
        ("messages_rejected", "edge", "counter", "message"),
        ("backpressure_events", "edge", "counter", "event"),
        ("current_queue_depth", "edge", "gauge", "message"),
        ("peak_queue_depth", "edge", "gauge", "message"),
        ("transfer_service_duration", "edge", "distribution", "nanosecond"),
        ("initialized", "edge", "state", "boolean"),
        ("started", "edge", "state", "boolean"),
        ("thread_active", "edge", "state", "boolean"),
        ("activity_state", "edge", "state", "state"),
        ("queue_residence_duration", "edge", "distribution", "nanosecond"),
        ("node_processing_duration", "edge", "distribution", "nanosecond"),
        ("end_to_end_duration", "edge", "distribution", "nanosecond"),
        ("dashboard_delivery_duration", "edge", "distribution", "nanosecond"),
    ]

    def definition(name, scope, kind, unit):
        is_uncollected = name in {
            "queue_residence_duration",
            "node_processing_duration",
            "end_to_end_duration",
            "dashboard_delivery_duration",
        }
        is_state = kind == "state"
        if kind == "counter":
            aggregation = (
                "runtime manager aggregate loaded atomically; never re-summed by dashboard"
                if scope == "graph" else
                "checked sum of incident canonical edge counters in one collection"
                if scope == "node" else
                "direct atomic edge counter; never re-summed by dashboard")
        elif name in {"peak_queue_depth", "peak_active_threads"}:
            aggregation = "maximum retained by the active runtime manager"
        elif kind == "gauge":
            aggregation = "no cross-sample aggregation"
        elif kind == "state":
            aggregation = "one state per canonical record"
        else:
            aggregation = {
                "transfer_service_duration":
                    "cumulative successful transfer-call duration and count",
                "queue_residence_duration":
                    "explicitly unavailable; GraphX does not timestamp queue entry and exit",
                "node_processing_duration":
                    "explicitly unavailable; node service intervals are not collected here",
                "end_to_end_duration":
                    "explicitly unavailable; messages are not end-to-end correlated",
                "dashboard_delivery_duration":
                    "explicitly unavailable; browser delivery is outside the runtime metric boundary",
            }[name]
        return {
        "name": name,
        "field": f"/{scope}{'' if scope == 'graph' else 's/*'}/{name}",
        "scope": scope,
        "kind": kind,
        "unit": unit,
        "monotonic": kind == "counter",
        "availability": "explicit",
        "capture": "independent hand-authored contract fixture",
        "reset": (
            "not_collected" if is_uncollected
            else "sample_replaced" if is_state or name in {
                "connected_edges", "current_queue_depth"
            }
            else "new_runtime_manager"
        ),
        "aggregation": aggregation,
        "overflow": (
            "not_applicable" if is_state
            else "unavailable_above_javascript_safe_integer"
        ),
        "numeric_representation": (
            "boolean" if unit == "boolean"
            else "enumerated_string" if is_state
            else "structured_duration" if kind == "distribution"
            else "non_negative_javascript_safe_integer"
        ),
    }

    metric_definitions = [definition(*spec) for spec in metric_specs]
    availability = {"state": "available", "reason": None}
    transfer = {
        "availability": "unavailable",
        "reason": "no successful transfer has been observed",
        "clock": None,
        "start_event": None,
        "end_event": None,
        "unit": "nanosecond",
        "count": None,
        "cumulative_total": None,
    }
    edge = {
        "edge_id": "detector_31:0->merge:32",
        "source_node_id": "detector_31",
        "source_port": 0,
        "destination_node_id": "merge",
        "destination_port": 32,
        "availability": "available",
        "unavailable_reason": None,
        "edge_index": 0,
        "source_node_index": 0,
        "source_node_name": "runtime-name-is-noncanonical",
        "source_port_index": 0,
        "destination_node_index": 1,
        "destination_node_name": "another-runtime-name",
        "destination_port_index": 32,
        "message_type": "message",
        "messages_enqueued": 0,
        "messages_dequeued": 0,
        "messages_rejected": 0,
        "backpressure_events": 0,
        "current_queue_depth": 0,
        "current_queue_depth_availability": availability,
        "peak_queue_depth": 0,
        "transfer_service_duration": transfer,
        "queue_residence_duration": transfer,
        "node_processing_duration": transfer,
        "end_to_end_duration": transfer,
        "dashboard_delivery_duration": transfer,
        "initialized": True,
        "started": False,
        "thread_active": False,
        "activity_state": "initialized",
    }
    metrics = {
        "schema": "graphx.dashboard.metrics.v1",
        "active_generation": 4,
        "active_run_epoch": 2,
        "active_config_revision": 9,
        "active_config_etag": '"graphx-config-9"',
        "capture_id": "g4-r2-c9-m100",
        "sampled_at_monotonic_ms": 100,
        "collection_interval": {
            "state": "unavailable",
            "reason": "no compatible previous sample",
            "clock": "steady_clock",
            "duration_ms": None,
        },
        "rate_availability": {
            "state": "unavailable",
            "reason": "same-run pair unavailable",
        },
        "qualified_rates": [],
        "identity_availability": availability,
        "metric_definitions": metric_definitions,
        "graph": {
            "availability": "available",
            "unavailable_reason": None,
            "total_items_processed": 0,
            "total_items_rejected": 0,
            "total_messages_processed": 0,
            "graph_total_enqueued": 0,
            "graph_total_dequeued": 0,
            "backpressure_events": 0,
            "peak_queue_depth": 0,
            "peak_active_threads": 0,
        },
        "nodes": [],
        "edges": [edge],
    }
    validator = jsonschema.Draft202012Validator(
        documents[SCHEMAS / "metrics.schema.json"], registry=registry
    )
    validator.validate(metrics)

    malformed = json.loads(json.dumps(metrics))
    malformed["edges"][0]["edge_id"] = "position:0"
    # JSON Schema validates shape; canonical equality is deliberately enforced
    # by the producer and typed frontend parser, never inferred by schema.
    validator.validate(malformed)
    malformed["edges"][0]["current_queue_depth"] = -1
    assert list(validator.iter_errors(malformed))

    frontend = (
        ROOT / "examples/DSP/dashboard/frontend/src/api.ts"
    ).read_text(encoding="utf-8")
    assert "edge_id disagrees with its canonical endpoints" in frontend
    assert ".find((record) => record.node_id === node.id)" in (
        ROOT / "examples/DSP/dashboard/frontend/src/Inspector.tsx"
    ).read_text(encoding="utf-8")
    inspector = (
        ROOT / "examples/DSP/dashboard/frontend/src/Inspector.tsx"
    ).read_text(encoding="utf-8").lower().replace("no animated", "")
    assert "no generic latency or animated activity is inferred." in inspector

    print("Phase 3 identity/schema checks: PASS (75 nodes, 137 exact-port edges)")


if __name__ == "__main__":
    main()
