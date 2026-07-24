#!/usr/bin/env python3
"""Authoritative Phase 1 OpenAPI 3.1 and JSON Schema contract validation."""

import copy
import json
from pathlib import Path

try:
    from jsonschema import Draft202012Validator, FormatChecker
    from openapi_spec_validator import validate as validate_openapi
    from referencing import Registry, Resource
except ImportError as error:
    raise SystemExit(
        "missing authoritative contract validators; install "
        "examples/DSP/dashboard/api/requirements-contracts.lock") from error

from schema_subset import DIALECT, load_registry, validate_instance, validate_schema

ROOT = Path(__file__).resolve().parent
HTTP_METHODS = {"get", "post", "patch", "delete", "put", "head", "options", "trace"}


def walk(value, visit):
    visit(value)
    if isinstance(value, dict):
        for child in value.values(): walk(child, visit)
    elif isinstance(value, list):
        for child in value: walk(child, visit)


def main() -> int:
    schemas = list((ROOT / "schemas").glob("*.schema.json"))
    assert schemas, "no JSON Schemas found"
    registry = load_registry(schemas)
    authoritative_registry = Registry()
    for path in schemas:
        schema = registry[path.name]
        assert schema.get("$schema") == DIALECT, path
        assert schema.get("$id"), f"{path}: missing $id"
        assert schema.get("type") == "object", f"{path}: top-level schema must be object"
        Draft202012Validator.check_schema(schema)
        authoritative_registry = authoritative_registry.with_resource(
            schema["$id"], Resource.from_contents(schema))

    def authoritative_validator(name: str) -> Draft202012Validator:
        return Draft202012Validator(registry[name], registry=authoritative_registry,
                                     format_checker=FormatChecker())

    # Independent representative instances exercise every pinned API schema;
    # production live instances are checked again by the external operator.
    metric_units = (
        (("total_items_processed", "graph"), "item"),
        (("total_items_rejected", "graph"), "item"),
        (("total_messages_processed", "graph"), "message"),
        (("graph_total_enqueued", "graph"), "message"),
        (("graph_total_dequeued", "graph"), "message"),
        (("backpressure_events", "graph"), "event"),
        (("peak_queue_depth", "graph"), "message"),
        (("peak_active_threads", "graph"), "thread"),
        (("inbound_messages", "node"), "message"),
        (("outbound_messages", "node"), "message"),
        (("rejected_messages", "node"), "message"),
        (("backpressure_events", "node"), "event"),
        (("peak_queue_depth", "node"), "message"),
        (("connected_edges", "node"), "edge"),
        (("diagnostics_available", "node"), "boolean"),
        (("activity_state", "node"), "state"),
        (("messages_enqueued", "edge"), "message"),
        (("messages_dequeued", "edge"), "message"),
        (("messages_rejected", "edge"), "message"),
        (("backpressure_events", "edge"), "event"),
        (("current_queue_depth", "edge"), "message"),
        (("peak_queue_depth", "edge"), "message"),
        (("transfer_service_duration", "edge"), "nanosecond"),
        (("initialized", "edge"), "boolean"),
        (("started", "edge"), "boolean"),
        (("thread_active", "edge"), "boolean"),
        (("activity_state", "edge"), "state"),
        (("queue_residence_duration", "edge"), "nanosecond"),
        (("node_processing_duration", "edge"), "nanosecond"),
        (("end_to_end_duration", "edge"), "nanosecond"),
        (("dashboard_delivery_duration", "edge"), "nanosecond"),
    )
    metric_definitions = [
        {"name": name,
         "field": f"/{scope}{'' if scope == 'graph' else 's/*'}/{name}",
         "scope": scope,
         "kind": ("distribution" if "duration" in name else
                  "state" if name in ("diagnostics_available", "activity_state",
                                      "initialized", "started", "thread_active") else
                  "counter" if name not in (
             "peak_queue_depth", "peak_active_threads",
             "current_queue_depth", "connected_edges") else "gauge"),
         "unit": unit,
         "monotonic": name not in (
             "peak_queue_depth", "peak_active_threads",
             "current_queue_depth"),
         "availability": "explicit",
         "capture": ("instantaneous read at server collection"
                     if name == "current_queue_depth"
                     else "atomic relaxed-load at server collection"),
         "reset": ("not_collected" if "duration" in name
                   and name != "transfer_service_duration" else
                   "sample_replaced" if name in (
                       "current_queue_depth", "connected_edges",
                       "diagnostics_available", "activity_state", "initialized",
                       "started", "thread_active") else "new_runtime_manager"),
         "aggregation": ("no cross-sample aggregation"
                         if name == "current_queue_depth"
                         else ("maximum only within one generation and run epoch"
                               if name in ("peak_queue_depth", "peak_active_threads")
                               else "sum only within one generation and run epoch")),
         "overflow": ("not_applicable" if name in (
             "diagnostics_available", "activity_state", "initialized",
             "started", "thread_active") else
             "unavailable_above_javascript_safe_integer"),
         "numeric_representation": (
             "structured_duration" if "duration" in name else
             "enumerated_string" if name == "activity_state" else
             "boolean" if name in (
                 "diagnostics_available", "initialized", "started",
                 "thread_active") else
             "non_negative_javascript_safe_integer")}
        for (name, scope), unit in metric_units
    ]
    # This independent contract table mirrors the normative wire contract
    # explicitly; it is intentionally not imported from the C++ producer.
    for definition in metric_definitions:
        scope = definition["scope"]
        name = definition["name"]
        if "duration" in name:
            definition.update(
                kind="distribution", monotonic=False,
                reset=("new_runtime_manager"
                       if name == "transfer_service_duration"
                       else "not_collected"))
        elif name in ("diagnostics_available", "activity_state",
                      "initialized", "started", "thread_active"):
            definition.update(kind="state", monotonic=False,
                              reset="sample_replaced")
        elif name in ("peak_queue_depth", "peak_active_threads",
                      "current_queue_depth", "connected_edges"):
            definition.update(
                kind="gauge", monotonic=False,
                reset=("new_runtime_manager"
                       if name in ("peak_queue_depth", "peak_active_threads")
                       else "sample_replaced"))
        else:
            definition.update(kind="counter", monotonic=True,
                              reset="new_runtime_manager")
        if definition["kind"] == "counter":
            definition["aggregation"] = (
                "runtime manager aggregate loaded atomically; never re-summed by dashboard"
                if scope == "graph" else
                "checked sum of incident canonical edge counters in one collection"
                if scope == "node" else
                "direct atomic edge counter; never re-summed by dashboard")
        elif name in ("peak_queue_depth", "peak_active_threads"):
            definition["aggregation"] = (
                "maximum retained by the active runtime manager")
        elif definition["kind"] == "gauge":
            definition["aggregation"] = "no cross-sample aggregation"
        elif definition["kind"] == "state":
            definition["aggregation"] = "one state per canonical record"
        else:
            definition["aggregation"] = {
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
    inactive_identity = {"state": "unavailable",
                         "reason": "no active runtime generation"}
    inactive_graph_metrics = {
        "availability": "unavailable",
        "unavailable_reason": "no active runtime generation",
        "total_items_processed": None, "total_items_rejected": None,
        "total_messages_processed": None, "graph_total_enqueued": None,
        "graph_total_dequeued": None, "backpressure_events": None,
        "peak_queue_depth": None, "peak_active_threads": None,
    }
    samples = {
        "health.schema.json": {"status": "ok"},
        "readiness.schema.json": {"ready": True, "state": "ready"},
        "version.schema.json": {"schema": "graphx.dashboard.version.v1", "api_version": "v1"},
        "graph.schema.json": {"schema": "graphx.dashboard.graph.v1", "owner": "receiver", "config_revision": 1,
                              "etag": '"graphx-config-1"',
                              "graph": {"nodes": [], "edges": []}},
        "config.schema.json": {"schema": "graphx.dashboard.config.v1", "owner": "receiver",
                               "config_revision": 1, "etag": '"graphx-config-1"',
                               "effective": {"nodes": [], "edges": []}, "derived_paths": []},
        "problem.schema.json": {"type": "about:blank", "title": "Bad Request", "status": 400,
                                "detail": "invalid request"},
        "runtime-status.schema.json": {"schema": "graphx.dashboard.runtime_status.v1",
            "lifecycle_state": "not_built", "ready": True, "rebuild_allowed": False,
            "rebuild_blocked": False, "active_generation": 0, "active_run_epoch": 0,
            "rebuild_attempts": 0,
            "successful_rebuilds": 0, "last_error": None, "active_config_revision":0,
            "active_config_etag":"", "config_revision":1, "etag":"\"graphx-config-1\"",
            "rebuild_required":True,"configuration_stale":True,"stop_requested":False,
            "started_at":None,"terminal_at":None,"terminal_result":None},
        "metrics.schema.json": {"schema": "graphx.dashboard.metrics.v1", "active_generation":0,
                                "active_run_epoch":0,"active_config_revision":0,"active_config_etag":"",
                                "capture_id":"inactive","sampled_at_monotonic_ms":1,
                                "collection_interval":{"state":"unavailable","reason":"no compatible previous sample","clock":"steady_clock","duration_ms":None},
                                "rate_availability":{"state":"unavailable","reason":"two samples required"},
                                "qualified_rates":[],"identity_availability":inactive_identity,
                                "metric_definitions":metric_definitions,"graph": inactive_graph_metrics,
                                "nodes": [], "edges": []},
        "edge-metrics.schema.json": {"schema": "graphx.dashboard.edge_metrics.v1", "active_generation":0,"active_run_epoch":0,"active_config_revision":0,"active_config_etag":"","capture_id":"inactive","sampled_at_monotonic_ms":1,"identity_availability":inactive_identity,"edges": []},
        "diagnostics.schema.json": {"schema": "graphx.dashboard.diagnostics.v1", "active_generation":0,"active_run_epoch":0,"active_config_revision":0,"active_config_etag":"","capture_id":"inactive","sampled_at_monotonic_ms":1,"identity_availability":inactive_identity,"nodes": []},
        "events.schema.json": {"schema": "graphx.dashboard.events_batch.v1",
            "stream": "/api/v1/fhss/events", "publisher_epoch": "a" * 32,
            "client_id": "validator", "resync_required": False,
            "reason": "none", "latest_sequence": 0,
            "oldest_available_sequence": 1, "newest_available_sequence": 0,
            "truncated": False, "events": [], "counters": {
                "dropped_events": 0, "dropped_events_total": 0,
                "coalesced_events_total": 0, "reconnects_total": 0}},
        "event.schema.json": {"schema": "graphx.dashboard.event.v1", "api_version": "v1",
            "publisher_epoch": "a" * 32, "sequence": 1, "event_type": "status",
            "timestamp": "2026-07-19T00:00:00.000Z", "generation": 1, "run_epoch": 2,
            "config_revision": 3, "config_etag": '"graphx-config-3"',
            "controller_epoch": None, "job_id": None, "correlation_id": None,
            "semantic_class": "runtime", "payload": {}},
        "websocket-hello.schema.json": {"schema": "graphx.dashboard.websocket_hello.v1",
            "api_version": "v1", "publisher_epoch": "a" * 32,
            "latest_sequence": 0, "oldest_available_sequence": 1,
            "heartbeat_interval_ms": 10000, "limits": {"frame_bytes": 65536,
                "message_bytes": 262144, "fragments_per_message": 32,
                "commands_per_second": 16, "events_per_second": 256,
                "replay_events": 256, "replay_bytes": 2097152,
                "queue_events": 128, "queue_bytes": 2097152,
                "idle_timeout_ms": 30000, "max_lifetime_ms": 3600000}},
        "websocket-heartbeat.schema.json": {
            "schema": "graphx.dashboard.websocket_heartbeat.v1",
            "publisher_epoch": "a" * 32,
            "timestamp": "2026-07-19T00:00:00.000Z"},
        "websocket-subscribe.schema.json": {
            "action": "subscribe", "client_id": "validator",
            "publisher_epoch": "a" * 32, "last_sequence": 0},
        "websocket-heartbeat-ack.schema.json": {
            "action": "heartbeat_ack", "publisher_epoch": "a" * 32},
        "websocket-resync-required.schema.json": {
            "schema": "graphx.dashboard.websocket_resync_required.v1",
            "publisher_epoch": "a" * 32, "latest_sequence": 0,
            "snapshot_url": "/api/v1/fhss/snapshot", "reason": "retention_gap"},
        "fhss-snapshot.schema.json": {"schema": "graphx.dashboard.fhss_snapshot.v1",
            "publisher_epoch": "a" * 32, "latest_sequence": 1,
            "captured_at": "2026-07-19T00:00:00.000Z", "config_revision": 1,
            "config_etag": '"graphx-config-1"', "generation": 1, "run_epoch": 1,
            "coherence":{"state":"coherent","metric_capture_id":"g1-r1-m1",
                         "diagnostic_capture_id":"g1-r1-d1"},
            "configuration": {}, "graph": {}, "runtime": {}, "metrics": {},
            "transport": {"counter_availability":{"state":"available","reason":None},
                "active_websocket_clients": 0, "pongs_received": 0,
                "idle_closes": 0, "protocol_failures": 0,
                "rejected_upgrades": 0, "replayed_events": 0,
                "resync_requests": 0, "queue_overflows": 0,
                "close_reasons": {"normal": 0, "protocol": 0,
                    "unsupported_data": 0, "invalid_utf8": 0, "too_big": 0,
                    "policy": 0, "going_away": 0, "internal": 0},
                "dropped_events_total": 0, "coalesced_events_total": 0},
            "diagnostics": {}},
        "scenario.schema.json": {"schema": "graphx.dashboard.scenario.v1", "owner": "receiver",
            "config_revision": 1, "scenario": {}, "derived_paths": [],
            "validation": {"valid": True, "levels": [], "errors": []}},
        "derived-paths.schema.json": {"schema": "graphx.dashboard.derived_paths.v1",
                                      "config_revision": 1, "paths": []},
        "value.schema.json": {"schema": "graphx.dashboard.value.v1", "pointer": "/fhss/scenario", "value": {}},
        "node.schema.json": {"schema": "graphx.dashboard.node.v1", "config_revision": 1, "node": {}},
        "node-parameters.schema.json": {"schema": "graphx.dashboard.node_parameters.v1",
            "config_revision": 1, "node_id": "source", "node": {}, "parameters": {}, "ports": {}},
        "visualization.schema.json": {"schema": "graphx.dashboard.fhss_visualization.v1",
            "fixture_label": "synthetic", "schedule": {}, "heatmap": {}, "timeline": {},
            "bounds": {}, "config_revision": 1},
        "configuration-provenance.schema.json": {
            "schema": "graphx.dashboard.configuration_provenance.v1",
            "config_revision": 1, "etag": '"graphx-config-1"', "provenance": [{
                "architecture_version": "docs/dsp/fhss_architecture.md",
                "rule_id": "sample-v1", "rule": "sample rule",
                "source_pointers": ["/messages/0"],
                "target_pointer": "/derived", "units": "complex_samples",
                "classification": {"source": "authoritative", "target": "generated",
                                   "mutability": "read-only"},
                "warnings": []}]},
        "receiver-graph.schema.json": {"schema": "graphx.dashboard.receiver_graph.v1",
            "config_revision": 1, "etag": '"graphx-config-1"',
            "graph": {"nodes": [], "edges": []}},
        "config-validation.schema.json": {"schema": "graphx.dashboard.config_validation.v1",
            "status": "validated", "config_revision": 1, "etag": '"graphx-config-1"',
            "validation": {"valid": True, "levels": [], "errors": []}},
        "config-result.schema.json": {"schema": "graphx.dashboard.config_result.v1",
            "status": "applied", "old_revision": 1, "new_revision": 2,
            "etag": '"graphx-config-2"',
            "validation": {"valid": True, "levels": [], "errors": []}},
        "rebuild-result.schema.json": {"schema":"graphx.dashboard.rebuild_result.v1","command_id":"r1","status":"succeeded","submitted_revision":1,"etag":"\"graphx-config-1\"","lifecycle_state":"stopped","active_generation":1,"warning":None},
        "command-result.schema.json": {"schema":"graphx.dashboard.command_result.v1","command_id":"s1","status":"accepted","active_generation":1,"code":"start_accepted","message":"accepted"},
        "fhss-expected-truth.schema.json": {
            "schema":"graphx.dashboard.fhss_expected_truth.v1","semantic_class":"expected",
            "scenario_id":"synthetic-1","config_revision":1,"config_etag":"\"graphx-config-1\"",
            "timing_basis":{"unit":"input_samples","sample_rate_hz":500000000.0,
                "bit_rate_hz":5000000.0,"bits_per_pulse":32,"pulse_gap_seconds":0.0000066,
                "pulse_duration_samples":3200,"inter_pulse_gap_samples":3300,
                "slot_samples":6500,"derivation":"dsp::fhss::DeriveTimingModel",
                "architecture_rule":"fhss-pr1-timing"},
            "messages":[],"pulses":[],
            "expected_receiver_message":{"accepted":False,"decoded_pulse_count":0,
                "status_class":"no_message_expected"},
            "synthetic_impairments":{"noise_enabled":False,"doppler_enabled":False,
                "multipath_enabled":False,"declared_only":True},
            "bounds":{"max_pulses":512,"max_messages":64,"original_message_count":0,
                "returned_message_count":0,"messages_truncated":False,
                "original_pulse_count":0,"returned_pulse_count":0,"pulses_truncated":False},
            "truth_sha256":"0" * 64},
        "fhss-receiver-observation.schema.json": {
            "schema":"graphx.dashboard.fhss_receiver_observation.v1","semantic_class":"observed",
            "generation":0,"run_epoch":0,"config_revision":0,"config_etag":"",
            "observation_id":"observation-g0-r0",
            "availability":{"state":"unavailable","reason":"generation_not_available"},
            "timing_basis":{"unit":"input_samples","global":True},
            "sample_rate":{"availability":{"state":"unavailable","reason":"no_receiver_samples"}},
            "observed_pulses":[],"detected_count":None,"rejected_count":None,
            "count_availability":{"state":"unavailable","reason":"source_not_diagnosable"},
            "count_semantics":{"detected":"unavailable",
                "rejected":"unavailable",
                "deduplication_rule":"terminal sink counts supersede upstream detector counts; source kinds are never added together"},
            "rejection_reason_codes":[],
            "preamble":{"availability":{"state":"unavailable","reason":"generation_not_available"}},
            "receiver_derived_active_frequencies":{"availability":{"state":"unavailable","reason":"generation_not_available"},"indices":[]},
            "assembler":{"availability":{"state":"unavailable","reason":"generation_not_available"}},
            "receiver_message_result":{"availability":{"state":"unavailable","reason":"generation_not_available"}},
            "terminal_result":{"availability":{"state":"unavailable","reason":"generation_not_available"}},
            "sources":[],"provenance":[],
            "truncation":{"truncated":False,"original_pulse_count":0,
                "returned_pulse_count":0,"max_pulses":512,"max_response_bytes":1048576},
            "observation_sha256":"1" * 64},
        "fhss-comparison-result.schema.json": {
            "schema":"graphx.dashboard.fhss_comparison_result.v1","semantic_class":"comparison",
            "evaluation_state":"indeterminate","expected_truth_sha256":"0" * 64,
            "receiver_observation_sha256":"1" * 64,"generation":0,"run_epoch":0,
            "config_identity":{"expected_config_revision":0,"expected_config_etag":"a",
                "observed_config_revision":1,"observed_config_etag":"b","agrees":False},
            "algorithm":{"name":"bounded_one_to_one_timing_channel_match",
                "version":"1.1.0","timing_tolerance_samples":64,
                "channel_rule":"logical_frequency_index_exact",
                "tie_rule":"equal_distance_is_ambiguous_no_assignment",
                "duplicate_rule":"each_expected_and_observed_used_at_most_once"},
            "availability":{"state":"unavailable","reason":"configuration_identity_mismatch"},"matches":[],
            "missed_expected_indices":[],"unexpected_observed_indices":[],"ambiguous":[],
            "terminal_result_agrees":None,
            "execution_lifecycle":{"completed":None,"observed_code":None,
                "correlated_separately_from_receiver_message":True},
            "comparison_sha256":"2" * 64},
        "fhss-receiver-spectrum.schema.json": {
            "schema":"graphx.dashboard.fhss_receiver_spectrum.v1","semantic_class":"unavailable",
            "generation":0,"run_epoch":0,"config_revision":0,"config_etag":"",
            "channel_index":None,"availability":{"state":"unavailable","reason":"generation_not_available"},"bins":[]},
        "fhss-observation-provenance.schema.json": {
            "schema":"graphx.dashboard.fhss_observation_provenance.v1","semantic_class":"observed",
            "generation":0,"run_epoch":0,"config_revision":0,"config_etag":"",
            "observation_id":"observation-g0-r0","records":[]},
        "fhss-observation-history.schema.json": {
            "schema":"graphx.dashboard.fhss_observation_history.v1","semantic_class":"observed",
            "retention":{"max_entries":1,"max_age_seconds":3600,"max_bytes":1048576,
            "policy":"active_generation_current_run_only"},"entries":[]},
        "fhss-job-request.schema.json": {
            "operation":"step", "request_id":"validator-step", "message_count":1,
            "sample_format":"cf32_le", "timeout_ms":30000},
        "fhss-job.schema.json": {
            "schema":"graphx.dashboard.fhss_job.v1", "controller_epoch":1,
            "job_id":"j-" + "1" * 24, "request_id":"validator-step",
            "idempotency_key_digest":"2" * 64,
            "scenario_correlation_id":"s-" + "3" * 24, "job_sequence":1,
            "operation":"step", "state":"queued", "message_cursor":0,
            "message_count":1, "sample_format":"cf32_le", "config_revision":1,
            "config_etag":"\"graphx-config-1\"", "graph_generation":0,
            "run_epoch":0, "created_at":"2026-01-01T00:00:00Z",
            "started_at":None, "terminal_at":None,
            "terminal":{"code":None,"detail":None},
            "work":{"generator_invoked":False,"receiver_replay_invoked":False},
            "artifacts":{},
            "generation_result":{"availability":{"state":"pending","reason":None},
                                 "terminal":None},
            "graph_lifecycle":None,
            "receiver_message_result":None,"receiver_observation":None,
            "comparison":None},
        "fhss-job-history.schema.json": {
            "schema":"graphx.dashboard.fhss_job_history.v1","controller_epoch":1,
            "entries":[],"bounds":{"max_entries":32,"max_metadata_bytes":2097152,
                "original_count":0,"returned_count":0,"truncated":False}},
        "fhss-job-reset.schema.json": {
            "schema":"graphx.dashboard.fhss_job_reset.v1","controller_epoch":2,
            "message_cursor":0,"retained_job_count":1,
            "idempotency_entries_retained":0,"status":"reset_completed"},
    }
    artifact = {"path":"truth.json","schema":"truth.v1",
        "media_type":"application/json","classification":"generator_truth",
        "retention_class":"bundle","receiver_visible":False,
        "evaluator_visible":True,"replay_use":False,"storage":"bundled",
        "bytes":2,"sha256":"0" * 64,"sha512":"1" * 128}
    artifacts = []
    for index, path in enumerate(("truth.json", "observation.json", "comparison.json",
            "receiver-config.json", "receiver-result.json", "recording.sigmf-meta",
            "provenance.json", "actions.json", "external-iq-reference.json")):
        entry = copy.deepcopy(artifact); entry["path"] = path
        entry["sha256"] = f"{index:x}" * 64
        artifacts.append(entry)
    samples.update({
        "fhss-investigation-export-request.schema.json": {
            "request_id":"export-1","bundle_name":"bundle-1",
            "job_id":"j-" + "1" * 24,"iq_mode":"reference","timeout_ms":30000},
        "fhss-investigation-validation-request.schema.json": {
            "request_id":"validate-1","bundle_name":"bundle-1","timeout_ms":30000},
        "fhss-investigation-replay-request.schema.json": {
            "request_id":"replay-1","bundle_name":"bundle-1","timeout_ms":30000},
        "fhss-investigation-operation.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_operation.v1",
            "operation_id":"op-" + "1" * 24,"operation":"export",
            "request_id":"export-1","bundle_name":"bundle-1","state":"queued",
            "created_at":"2026-01-01T00:00:00Z","started_at":None,"terminal_at":None,
            "terminal":{"code":None,"detail":None},"result":None,
            "bounds":{"timeout_ms":30000,"checkpoint_bound_ms":100}},
        "fhss-investigation-operations.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_operations.v1",
            "entries":[],"bounds":{"max_entries":32}},
        "fhss-investigation-artifact-entry.schema.json": artifact,
        "fhss-investigation-manifest.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_manifest.v1",
            "bundle_name":"bundle-1","bundle_format_version":1,
            "created_at":"2026-01-01T00:00:00Z","iq_mode":"reference",
            "self_contained":False,"synthetic_only":True,"receiver_truth_access":"none",
            "source_job_id":"j-" + "1" * 24,
            "source_job_request_id":"request-1","controller_epoch":1,
            "scenario_correlation_id":"scenario-1",
            "datatype":"cf32_le","iq_bytes":8,"sample_count":1,
            "iq_sha256":"0" * 64,"iq_sha512":"1" * 128,
            "expected_receiver_result_sha256":"2" * 64,"artifacts":artifacts,
            "manifest_integrity":{"algorithm":"sha256","detached_file":"manifest.sha256",
                "scope":"exact canonical manifest.json bytes"}},
        "fhss-external-iq-reference.schema.json": {
            "schema":"graphx.dashboard.fhss_external_iq_reference.v1",
            "approved_root_id":"fhss-jobs","relative_components":["j-" + "1" * 24,"iq.cf32"],
            "bytes":8,"sha256":"0" * 64,"sha512":"1" * 128,
            "identity":{"device":1,"inode":2}},
        "fhss-investigation-provenance.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_provenance.v1",
            "created_at":"2026-01-01T00:00:00Z","synthetic_only":True,
            "hwil":"unavailable","production_rf_qualification":"not_qualified",
            "source_job_id":"j-" + "1" * 24,"source_job_request_id":"step-1",
            "controller_epoch":1,"graph_generation":1,"run_epoch":1,
            "scenario_correlation_id":"s-" + "2" * 24,"source_config_revision":1,
            "source_config_etag":"etag","sample_format":"cf32_le","sample_count":1,
            "sample_rate_hz":500000000.0,"center_frequency_hz":1240000000.0,
            "producer":"graphx-dsp-fhss-demo","build":{"language_standard":"C++26",
                "compiler":"validator","platform":"local-host"},"api_version":"v1",
            "bundle_schema_version":1,"sigmf_core_version":"1.2.6",
            "publisher_epoch":None,"event_sequence_range":None},
        "fhss-operator-actions.schema.json": {
            "schema":"graphx.dashboard.fhss_operator_actions.v1","actions":[{
                "sequence":1,"action":"export","request_id":"export-1",
                "timestamp":"2026-01-01T00:00:00Z"}]},
        "fhss-investigation-quota.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_quota.v1",
            "active_operations":0,"retained_operations":0,
            "retained_bundle_bytes":0,"remaining_bundle_bytes":536870912,"limits":{
                "concurrent_operations":1,"operations":32,"bundles":32,
                "artifacts_per_bundle":64,"json_bytes":1048576,"copied_iq_bytes":67108864,
                "referenced_iq_bytes":536870912,"retained_bundle_bytes":536870912,
                "chunk_bytes":262144,"checkpoint_bound_ms":100}},
        "fhss-investigation-export-result.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_export_result.v1",
            "bundle_name":"bundle-1","iq_mode":"reference","self_contained":False,
            "manifest_sha256":"0" * 64,"iq_sha512":"1" * 128,"iq_bytes":8,
            "datatype":"cf32_le","sample_count":1},
        "fhss-investigation-validation-result.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_validation_result.v1",
            "bundle_name":"bundle-1","iq_mode":"reference","datatype":"cf32_le",
            "iq_bytes":8,"sample_count":1,"iq_sha512":"1" * 128,
            "manifest_sha256":"0" * 64,"receiver_truth_access":"none"},
        "fhss-investigation-replay-result.schema.json": {
            "schema":"graphx.dashboard.fhss_investigation_replay_result.v1",
            "bundle_name":"bundle-1","manifest_sha256":"0" * 64,
            "semantic_receiver_result_sha256":"2" * 64,"matches_expected":True,
            "receiver_truth_access":"none","receiver_message_result":{
                "accepted":True,"decoded_pulse_count":1,"status":"accepted"}},
        "fhss-receiver-result.schema.json": {
            "schema":"graphx.dashboard.fhss_receiver_result.v1","result":{
                "accepted":True,"decoded_pulse_count":1,"status":"accepted"},
            "semantic_sha256":"2" * 64},
        "fhss-iq-truth.schema.json": {
            "schema":"graphx.fhss.iq-truth.v1","generator":"generator",
            "generator_version":1,"input_sha256":"0" * 64,
            "iq_sha256":"1" * 64,"sample_count":1,"sample_format":"cf32_le",
            "sample_rate_hz":1.0,"truth_pulses":[]},
        "fhss-receiver-graph.schema.json": {
            "schema":"graphx.dashboard.receiver_graph.v1","config_revision":1,
            "etag":"etag","graph":{"nodes":[{}],"edges":[]}},
        "fhss-build-api-manifest.schema.json": {
            "schema":"graphx.dashboard.fhss_build_api_manifest.v1",
            "producer_executable":{"path":"/bin/demo","bytes":1,
                "sha256":"0" * 64,"device":1,"inode":2},
            "source_revision":"1" * 40,"source_dirty":True,
            "source_state_capture":"configure_time_git_status_porcelain",
            "language_standard":"C++26","platform":"test",
            "openapi":{"version":"7.0.0","sha256":"1" * 64},
            "pinned_sigmf":{"version":"1.2.6",
                "schema_id":"https://raw.githubusercontent.com/sigmf/SigMF/v1.2.6/sigmf-schema.json",
                "sha256":"2" * 64,"license":"Apache-2.0"},
            "schemas":[{"path":"schema.json","sha256":"3" * 64}]},
    })
    snapshot = samples["fhss-snapshot.schema.json"]
    snapshot["configuration"] = copy.deepcopy(samples["config.schema.json"])
    snapshot["graph"] = copy.deepcopy(samples["graph.schema.json"])
    snapshot_runtime = copy.deepcopy(samples["runtime-status.schema.json"])
    snapshot_runtime.update({
        "active_generation": 1, "active_run_epoch": 1,
        "active_config_revision": 1,
        "active_config_etag": '"graphx-config-1"',
        "rebuild_required": False, "configuration_stale": False,
    })
    snapshot["runtime"] = snapshot_runtime
    snapshot_metrics = copy.deepcopy(samples["metrics.schema.json"])
    snapshot_metrics.update({
        "active_generation": 1, "active_run_epoch": 1,
        "active_config_revision": 1,
        "active_config_etag": '"graphx-config-1"',
        "capture_id": "g1-r1-m1",
    })
    snapshot["metrics"] = snapshot_metrics
    snapshot_diagnostics = copy.deepcopy(samples["diagnostics.schema.json"])
    snapshot_diagnostics.update({
        "active_generation": 1, "active_run_epoch": 1,
        "active_config_revision": 1,
        "active_config_etag": '"graphx-config-1"',
        "capture_id": "g1-r1-d1",
    })
    snapshot["diagnostics"] = snapshot_diagnostics

    repository = ROOT.parents[3]
    generator_graph = json.loads((repository / "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json").read_text())
    generator_scenario = next(node["node_config"] for node in generator_graph["nodes"]
                              if node.get("id") == "source")
    generator_scenario.pop("active_frequency_indices", None)
    receiver_graph = json.loads((repository / "libdsp/config/fhss_phase2_binary_iq_receiver.json").read_text())
    samples["scenario.schema.json"]["scenario"] = generator_scenario
    samples["receiver-graph.schema.json"]["graph"] = receiver_graph
    assert set(samples) == {path.name for path in schemas}, "every schema needs a representative instance"
    for name, sample in samples.items():
        authoritative_validator(name).validate(sample)
        validate_instance(sample, registry[name], registry=registry)

    # Pinned upstream SigMF 1.2.6 is an independent oracle, not generated from
    # the production serializer. Both GraphX datatypes are exercised here.
    official_sigmf_path = ROOT.parent / "sigmf" / "official-v1.2.6" / "sigmf-schema.json"
    official_sigmf = json.loads(official_sigmf_path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(official_sigmf)
    official_sigmf_validator = Draft202012Validator(official_sigmf,
                                                     format_checker=FormatChecker())
    for datatype, stride in (("cf32_le", 8), ("cf64_le", 16)):
        metadata = {"global":{"core:datatype":datatype,"core:version":"1.2.6",
                    "core:sample_rate":500000000.0,"core:sha512":"0" * 128},
                    "captures":[{"core:sample_start":0,
                                 "core:frequency":1240000000.0}],
                    "annotations":[{"core:sample_start":0,"core:sample_count":1}]}
        official_sigmf_validator.validate(metadata)
        assert stride in (8, 16)
    malformed_sigmf = copy.deepcopy(metadata)
    del malformed_sigmf["global"]["core:version"]
    assert not official_sigmf_validator.is_valid(malformed_sigmf)

    def assert_contract_rejects(name: str, value: object, label: str) -> None:
        assert not authoritative_validator(name).is_valid(value), label
        try:
            validate_instance(value, registry[name], registry=registry)
        except ValueError:
            pass
        else:
            raise AssertionError(f"offline validator accepted {label}")

    # The 31 metric definitions are an exact set, not a generic bag of
    # plausible metadata. Exercise each semantic identity dimension against
    # two independent validators.
    duplicate_metric = copy.deepcopy(samples["metrics.schema.json"])
    duplicate_metric["metric_definitions"][-1] = copy.deepcopy(
        duplicate_metric["metric_definitions"][0])
    assert_contract_rejects("metrics.schema.json", duplicate_metric,
                            "duplicate metric definition")
    for field, replacement in (
            ("field", "/graph/total_items_rejected"),
            ("scope", "edge"), ("kind", "gauge"), ("unit", "event"),
            ("monotonic", False), ("reset", "sample_replaced"),
            ("aggregation", "no cross-sample aggregation")):
        conflicting = copy.deepcopy(samples["metrics.schema.json"])
        conflicting["metric_definitions"][0][field] = replacement
        assert_contract_rejects(
            "metrics.schema.json", conflicting,
            f"conflicting metric definition {field}")

    # Canonical graph/config structures are closed and bounded below the
    # dashboard transport boundary.
    for schema_name, graph_key in (
            ("graph.schema.json", "graph"),
            ("config.schema.json", "effective")):
        unknown_graph_field = copy.deepcopy(samples[schema_name])
        unknown_graph_field[graph_key]["unknown"] = True
        assert_contract_rejects(schema_name, unknown_graph_field,
                                f"{schema_name} nested unknown")

        unknown_node_field = copy.deepcopy(samples[schema_name])
        unknown_node_field[graph_key]["nodes"] = [
            {"id":"source","type":"Source","unexpected":True}]
        assert_contract_rejects(schema_name, unknown_node_field,
                                f"{schema_name} node unknown")

        missing_node_identity = copy.deepcopy(samples[schema_name])
        missing_node_identity[graph_key]["nodes"] = [{"type":"Source"}]
        assert_contract_rejects(schema_name, missing_node_identity,
                                f"{schema_name} missing node identity")

        malformed_edge = copy.deepcopy(samples[schema_name])
        malformed_edge[graph_key]["edges"] = [{
            "source_node_id":"source", "source_port":0,
            "target_node_id":"sink"}]
        assert_contract_rejects(schema_name, malformed_edge,
                                f"{schema_name} missing edge endpoint")

        extra_edge_field = copy.deepcopy(samples[schema_name])
        extra_edge_field[graph_key]["edges"] = [{
            "source_node_id":"source", "source_port":0,
            "target_node_id":"sink", "target_port":0, "weight":1}]
        assert_contract_rejects(schema_name, extra_edge_field,
                                f"{schema_name} edge unknown")

        negative_port = copy.deepcopy(samples[schema_name])
        negative_port[graph_key]["edges"] = [{
            "source_node_id":"source", "source_port":-1,
            "target_node_id":"sink", "target_port":0}]
        assert_contract_rejects(schema_name, negative_port,
                                f"{schema_name} negative port")

        too_many_nodes = copy.deepcopy(samples[schema_name])
        too_many_nodes[graph_key]["nodes"] = [
            {"id":f"node-{index}","type":"Node"} for index in range(257)]
        assert_contract_rejects(schema_name, too_many_nodes,
                                f"{schema_name} node bound")

        too_many_edges = copy.deepcopy(samples[schema_name])
        too_many_edges[graph_key]["edges"] = [{
            "source_node_id":"source", "source_port":index,
            "target_node_id":"sink", "target_port":index}
            for index in range(513)]
        assert_contract_rejects(schema_name, too_many_edges,
                                f"{schema_name} edge bound")

    # Independent Phase6 adversarial corpus. These values are constructed here,
    # not by the production server/generator, and both authoritative Draft
    # 2020-12 and the offline validator must reject every case.
    phase6_negatives = []
    def phase6_negative(schema_name: str, label: str, mutate) -> None:
        value = copy.deepcopy(samples[schema_name])
        mutate(value)
        phase6_negatives.append((schema_name, value, label))

    for schema_name in (
            "event.schema.json", "events.schema.json",
            "websocket-hello.schema.json", "websocket-heartbeat.schema.json",
            "websocket-heartbeat-ack.schema.json",
            "websocket-subscribe.schema.json",
            "websocket-resync-required.schema.json",
            "fhss-snapshot.schema.json"):
        phase6_negative(schema_name, f"{schema_name} unknown field",
                        lambda value: value.update(unknown=True))
    for schema_name, pointer in (
            ("event.schema.json", "sequence"),
            ("events.schema.json", "latest_sequence"),
            ("websocket-hello.schema.json", "latest_sequence"),
            ("websocket-subscribe.schema.json", "last_sequence"),
            ("websocket-resync-required.schema.json", "latest_sequence"),
            ("fhss-snapshot.schema.json", "latest_sequence")):
        phase6_negative(schema_name, f"{schema_name} negative sequence",
                        lambda value, key=pointer: value.update({key: -1}))
        phase6_negative(schema_name, f"{schema_name} uint64 overflow sequence",
                        lambda value, key=pointer: value.update({key: 1 << 64}))
        phase6_negative(schema_name, f"{schema_name} non-finite sequence",
                        lambda value, key=pointer: value.update({key: float("inf")}))
    for schema_name in (
            "event.schema.json", "events.schema.json",
            "websocket-hello.schema.json", "websocket-heartbeat.schema.json",
            "websocket-heartbeat-ack.schema.json",
            "websocket-subscribe.schema.json",
            "websocket-resync-required.schema.json",
            "fhss-snapshot.schema.json"):
        phase6_negative(schema_name, f"{schema_name} malformed publisher epoch",
                        lambda value: value.update(publisher_epoch="not-an-epoch"))
    phase6_negative("websocket-subscribe.schema.json", "empty client id",
                    lambda value: value.update(client_id=""))
    phase6_negative("websocket-subscribe.schema.json", "oversized client id",
                    lambda value: value.update(client_id="a" * 65))
    phase6_negative("websocket-subscribe.schema.json", "invalid client id alphabet",
                    lambda value: value.update(client_id="bad/client"))
    phase6_negative("websocket-heartbeat-ack.schema.json", "wrong heartbeat action",
                    lambda value: value.update(action="subscribe"))
    phase6_negative("websocket-resync-required.schema.json", "invalid resync reason",
                    lambda value: value.update(reason="attacker_reason"))
    phase6_negative("events.schema.json", "invalid batch reason",
                    lambda value: value.update(reason="attacker_reason"))
    phase6_negative("events.schema.json", "oversized event batch",
                    lambda value: value.update(events=[copy.deepcopy(
                        samples["event.schema.json"]) for _ in range(257)]))
    phase6_negative("events.schema.json", "uint64 counter overflow",
                    lambda value: value["counters"].update(
                        dropped_events_total=1 << 64))
    phase6_negative("events.schema.json", "negative counter",
                    lambda value: value["counters"].update(dropped_events=-1))
    phase6_negative("events.schema.json", "non-finite counter",
                    lambda value: value["counters"].update(
                        reconnects_total=float("nan")))
    phase6_negative("event.schema.json", "oversized event type",
                    lambda value: value.update(event_type="x" * 65))
    phase6_negative("event.schema.json", "invalid event payload type",
                    lambda value: value.update(payload=[]))
    phase6_negative("event.schema.json", "oversized event payload members",
                    lambda value: value.update(payload={
                        str(index): index for index in range(1025)}))
    phase6_negative("event.schema.json", "oversized semantic class",
                    lambda value: value.update(semantic_class="x" * 65))
    phase6_negative("websocket-hello.schema.json", "unsafe hello message limit",
                    lambda value: value["limits"].update(
                        message_bytes=262145))
    phase6_negative("websocket-hello.schema.json", "unsafe hello client count",
                    lambda value: value["limits"].update(queue_events=4097))
    phase6_negative("fhss-snapshot.schema.json", "unsafe active client count",
                    lambda value: value["transport"].update(
                        active_websocket_clients=9))
    for schema_name, value, label in phase6_negatives:
        assert_contract_rejects(schema_name, value, label)

    masquerading_expected = copy.deepcopy(samples["fhss-expected-truth.schema.json"])
    masquerading_expected["semantic_class"] = "observed"
    assert_contract_rejects("fhss-expected-truth.schema.json", masquerading_expected,
                            "expected document masquerading as observed")
    masquerading_observed = copy.deepcopy(samples["fhss-receiver-observation.schema.json"])
    masquerading_observed["semantic_class"] = "expected"
    assert_contract_rejects("fhss-receiver-observation.schema.json", masquerading_observed,
                            "observed document masquerading as expected")
    missing_unit = copy.deepcopy(samples["fhss-receiver-observation.schema.json"])
    missing_unit["provenance"] = [{"generation":0,"run_epoch":0,"node_id":"sink",
        "node_class":"FHSSMessageSinkNode","source_schema":"receiver.v1",
        "packet_field":"detected_count",
        "sample_interval_availability":{"state":"unavailable","reason":"not_carried_by_receiver_product"},
        "sample_interval":None,"capture_time_availability":{"state":"unavailable","reason":"not_carried_by_receiver_product"},
        "capture_time":None,"transformation":"copied"}]
    assert_contract_rejects("fhss-receiver-observation.schema.json", missing_unit,
                            "provenance missing unit")
    nonfinite = copy.deepcopy(samples["fhss-expected-truth.schema.json"])
    nonfinite["timing_basis"]["sample_rate_hz"] = float("inf")
    assert_contract_rejects("fhss-expected-truth.schema.json", nonfinite,
                            "non-finite evaluator timing")
    full_range_expected = copy.deepcopy(samples["fhss-expected-truth.schema.json"])
    full_range_expected["pulses"] = [{
        "expected_index":0, "message_id":1, "pulse_index":0,
        "global_start_sample":(1 << 64) - 1, "duration_samples":1,
        "logical_frequency_index":0, "transmitted_word":0, "role":"body"}]
    authoritative_validator("fhss-expected-truth.schema.json").validate(full_range_expected)
    validate_instance(full_range_expected, registry["fhss-expected-truth.schema.json"],
                      registry=registry)
    full_range_observed = copy.deepcopy(samples["fhss-receiver-observation.schema.json"])
    full_range_observed["observed_pulses"] = [{
        "observed_index":0, "global_start_sample":(1 << 64) - 1,
        "duration_samples":1, "logical_frequency_index":0,
        "physical_channel_index":0, "rf_frequency_hz":0.0,
        "iq_offset_frequency_hz":0.0, "estimated_center_frequency_hz":0.0,
        "detector_frequency_error_hz_unqualified":0.0,
        "confidence_score_uncalibrated":0.0, "viterbi_path_metric":0.0,
        "viterbi_second_best_path_metric":0.0, "decoded_value":0,
        "source_node_id":"decoder"}]
    authoritative_validator("fhss-receiver-observation.schema.json").validate(
        full_range_observed)
    validate_instance(full_range_observed,
                      registry["fhss-receiver-observation.schema.json"],
                      registry=registry)
    bounded_expected = copy.deepcopy(samples["fhss-expected-truth.schema.json"])
    bounded_expected["pulses"] = [{
        "expected_index":index, "message_id":1, "pulse_index":index,
        "global_start_sample":index, "duration_samples":1,
        "logical_frequency_index":index % 64, "transmitted_word":index,
        "role":"body"} for index in range(512)]
    bounded_expected["expected_receiver_message"].update(
        {"accepted":True, "decoded_pulse_count":512,
         "status_class":"accepted_message"})
    bounded_expected["bounds"].update(
        {"original_pulse_count":513, "returned_pulse_count":512,
         "pulses_truncated":True})
    authoritative_validator("fhss-expected-truth.schema.json").validate(bounded_expected)
    validate_instance(bounded_expected, registry["fhss-expected-truth.schema.json"],
                      registry=registry)
    oversized_decoded_count = copy.deepcopy(bounded_expected)
    oversized_decoded_count["expected_receiver_message"]["decoded_pulse_count"] = 513
    assert_contract_rejects("fhss-expected-truth.schema.json", oversized_decoded_count,
                            "unbounded expected decoded pulse count")
    inconsistent_availability = copy.deepcopy(samples["fhss-receiver-observation.schema.json"])
    inconsistent_availability["availability"] = {
        "state":"available", "reason":"generation_not_available"}
    assert_contract_rejects("fhss-receiver-observation.schema.json",
                            inconsistent_availability, "availability inconsistency")
    unknown_job_field = copy.deepcopy(samples["fhss-job-request.schema.json"])
    unknown_job_field["node_step"] = True
    assert_contract_rejects("fhss-job-request.schema.json", unknown_job_field,
                            "arbitrary node stepping field")
    illegal_job_state = copy.deepcopy(samples["fhss-job.schema.json"])
    illegal_job_state["state"] = "paused_mid_pulse"
    assert_contract_rejects("fhss-job.schema.json", illegal_job_state,
                            "illegal job state")

    # Receiver-facing contracts reject generator schedule/truth fields even if
    # future code accidentally serializes them into a node configuration.
    receiver_leaks = ("messages", "truth_from_fixture", "truth_path", "truth_file",
                      "generator_metadata", "transmitted_active_frequency_indices",
                      "transmitted_pulse_frequency_indices", "active_frequency_indices")
    for field in receiver_leaks:
        leaked = json.loads(json.dumps(samples["receiver-graph.schema.json"]))
        leaked["graph"]["nodes"] = [{"id": "assembler", "type": "FHSSAssemblerNode",
                                        "node_config": {field: []}}]
        assert not authoritative_validator("receiver-graph.schema.json").is_valid(leaked), field
        try:
            validate_instance(leaked, registry["receiver-graph.schema.json"], registry=registry)
        except ValueError:
            pass
        else:
            raise AssertionError(f"offline validator accepted receiver leak: {field}")
    for field in ("messages", "truth_from_fixture", "active_frequency_indices"):
        leaked = json.loads(json.dumps(samples["receiver-graph.schema.json"]))
        leaked["graph"]["nodes"][0]["node_config"]["nested"] = {
            "deeper": [{"configuration": {field: []}}]}
        assert not authoritative_validator("receiver-graph.schema.json").is_valid(leaked), f"nested {field}"
        try:
            validate_instance(leaked, registry["receiver-graph.schema.json"], registry=registry)
        except ValueError:
            pass
        else:
            raise AssertionError(f"offline validator accepted nested receiver leak: {field}")

    for field in ("active_frequency_indices", "preamble_pulses", "truth_from_fixture",
                  "truth_path", "generator_metadata", "transmitted_active_frequency_indices",
                  "transmitted_pulse_frequency_indices"):
        leaked = json.loads(json.dumps(samples["scenario.schema.json"]))
        leaked["scenario"][field] = []
        assert not authoritative_validator("scenario.schema.json").is_valid(leaked), field
    for mutation in ("missing_messages", "missing_preamble"):
        invalid = json.loads(json.dumps(samples["scenario.schema.json"]))
        if mutation == "missing_messages": invalid["scenario"].pop("messages")
        else: invalid["scenario"]["messages"][0]["pulses"] = invalid["scenario"]["messages"][0]["pulses"][:15]
        assert not authoritative_validator("scenario.schema.json").is_valid(invalid), mutation
    for mutation in ("missing_source", "missing_topology"):
        invalid = json.loads(json.dumps(samples["receiver-graph.schema.json"]))
        if mutation == "missing_source": invalid["graph"]["nodes"] = [n for n in invalid["graph"]["nodes"] if n.get("id") != "source"]
        else: invalid["graph"]["edges"] = []
        assert not authoritative_validator("receiver-graph.schema.json").is_valid(invalid), mutation

    document = json.loads((ROOT / "openapi.json").read_text(encoding="utf-8"))
    validate_openapi(document, base_uri=(ROOT / "openapi.json").resolve().as_uri())
    assert document.get("openapi") == "3.1.2"
    assert document.get("jsonSchemaDialect") == DIALECT
    assert document.get("info", {}).get("title") and document.get("info", {}).get("version")
    paths = document.get("paths")
    assert isinstance(paths, dict) and paths
    operation_ids = set()
    for route, path_item in paths.items():
        assert route in {"/healthz", "/readyz", "/api/v1/version"} or route.startswith("/api/v1/fhss/"), route
        for method, operation in path_item.items():
            if method not in HTTP_METHODS: continue
            operation_id = operation.get("operationId")
            assert operation_id and operation_id not in operation_ids, operation_id
            operation_ids.add(operation_id)
            responses = operation.get("responses")
            assert isinstance(responses, dict) and responses, f"{method} {route}: responses"
            for response_code, response in responses.items():
                if "$ref" not in response:
                    assert response.get("description"), f"{method} {route}: description"
                    content = response.get("content")
                    if response_code in {"101", "204"}:
                        assert content is None, f"{method} {route}: bodyless response"
                        continue
                    assert isinstance(content, dict) and content, f"{method} {route}: response content"
                    for media in content.values():
                        response_schema = media.get("schema")
                        assert isinstance(response_schema, dict), f"{method} {route}: response schema"
                        validate_schema(response_schema, f"{method} {route} response")

    def reference(node):
        if not isinstance(node, dict) or "$ref" not in node: return
        ref = node["$ref"]
        if ref.startswith("schemas/"):
            assert ref in registry, ref
        elif ref.startswith("#/"):
            current = document
            for token in ref[2:].split("/"): current = current[token]
        else:
            raise AssertionError(f"unsupported reference {ref}")
    walk(document, reference)
    print(f"OpenAPI 3.1.2 authoritative validation: PASS; schemas: {len(schemas)}; operations: {len(operation_ids)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
