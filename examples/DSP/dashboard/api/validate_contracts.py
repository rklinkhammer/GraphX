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
        (("messages_enqueued", "edge"), "message"),
        (("messages_dequeued", "edge"), "message"),
        (("messages_rejected", "edge"), "message"),
        (("backpressure_events", "edge"), "event"),
        (("current_queue_depth", "edge"), "message"),
        (("peak_queue_depth", "edge"), "message"),
    )
    metric_definitions = [
        {"name": name, "scope": scope,
         "kind": "counter" if name not in (
             "peak_queue_depth", "peak_active_threads",
             "current_queue_depth") else "gauge",
         "unit": unit,
         "monotonic": name not in (
             "peak_queue_depth", "peak_active_threads",
             "current_queue_depth"),
         "reset": "new_graph_generation"}
        for (name, scope), unit in metric_units
    ]
    samples = {
        "health.schema.json": {"status": "ok"},
        "readiness.schema.json": {"ready": True, "state": "ready"},
        "version.schema.json": {"schema": "graphx.dashboard.version.v1", "api_version": "v1"},
        "graph.schema.json": {"schema": "graphx.dashboard.graph.v1", "owner": "receiver", "config_revision": 1,
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
                                "metric_definitions":metric_definitions,"graph": {},
                                "nodes": [], "edges": []},
        "edge-metrics.schema.json": {"schema": "graphx.dashboard.edge_metrics.v1", "active_generation":0,"active_run_epoch":0,"active_config_revision":0,"active_config_etag":"","edges": []},
        "diagnostics.schema.json": {"schema": "graphx.dashboard.diagnostics.v1", "active_generation":0,"active_run_epoch":0,"active_config_revision":0,"active_config_etag":"","nodes": []},
        "events.schema.json": {"schema": "graphx.dashboard.events_batch.v1",
            "stream": "/api/v1/fhss/events", "client_id": "validator", "resync_required": False,
            "latest_sequence": 0, "events": [], "counters": {}},
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

    def assert_contract_rejects(name: str, value: object, label: str) -> None:
        assert not authoritative_validator(name).is_valid(value), label
        try:
            validate_instance(value, registry[name], registry=registry)
        except ValueError:
            pass
        else:
            raise AssertionError(f"offline validator accepted {label}")

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
            for response in responses.values():
                if "$ref" not in response:
                    assert response.get("description"), f"{method} {route}: description"
                    content = response.get("content")
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
