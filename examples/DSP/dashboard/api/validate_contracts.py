#!/usr/bin/env python3
"""Authoritative Phase 1 OpenAPI 3.1 and JSON Schema contract validation."""

import json
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
    from openapi_spec_validator import validate as validate_openapi
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
    for path in schemas:
        schema = registry[path.name]
        assert schema.get("$schema") == DIALECT, path
        assert schema.get("$id"), f"{path}: missing $id"
        assert schema.get("type") == "object", f"{path}: top-level schema must be object"
        Draft202012Validator.check_schema(schema)

    # Independent representative instances exercise every pinned API schema;
    # production live instances are checked again by the external operator.
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
            "lifecycle_state": "ready", "ready": True, "rebuild_allowed": False,
            "rebuild_blocked": False, "active_generation": 0, "rebuild_attempts": 0,
            "successful_rebuilds": 0, "last_error": None},
        "metrics.schema.json": {"schema": "graphx.dashboard.metrics.v1", "graph": {},
                                "nodes": [], "edges": []},
        "edge-metrics.schema.json": {"schema": "graphx.dashboard.edge_metrics.v1", "edges": []},
        "diagnostics.schema.json": {"schema": "graphx.dashboard.diagnostics.v1", "nodes": []},
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
            "fixture_label": "synthetic", "schedule": {}, "heatmap": {}, "timeline": {}, "decoder": {},
            "selected_channel_preview": {}, "bounds": {}, "config_revision": 1},
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
        Draft202012Validator(registry[name]).validate(sample)
        validate_instance(sample, registry[name], registry=registry)

    # Receiver-facing contracts reject generator schedule/truth fields even if
    # future code accidentally serializes them into a node configuration.
    receiver_leaks = ("messages", "truth_from_fixture", "truth_path", "truth_file",
                      "generator_metadata", "transmitted_active_frequency_indices",
                      "transmitted_pulse_frequency_indices", "active_frequency_indices")
    for field in receiver_leaks:
        leaked = json.loads(json.dumps(samples["receiver-graph.schema.json"]))
        leaked["graph"]["nodes"] = [{"id": "assembler", "type": "FHSSAssemblerNode",
                                        "node_config": {field: []}}]
        assert not Draft202012Validator(registry["receiver-graph.schema.json"]).is_valid(leaked), field
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
        assert not Draft202012Validator(registry["receiver-graph.schema.json"]).is_valid(leaked), f"nested {field}"
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
        assert not Draft202012Validator(registry["scenario.schema.json"]).is_valid(leaked), field
    for mutation in ("missing_messages", "missing_preamble"):
        invalid = json.loads(json.dumps(samples["scenario.schema.json"]))
        if mutation == "missing_messages": invalid["scenario"].pop("messages")
        else: invalid["scenario"]["messages"][0]["pulses"] = invalid["scenario"]["messages"][0]["pulses"][:15]
        assert not Draft202012Validator(registry["scenario.schema.json"]).is_valid(invalid), mutation
    for mutation in ("missing_source", "missing_topology"):
        invalid = json.loads(json.dumps(samples["receiver-graph.schema.json"]))
        if mutation == "missing_source": invalid["graph"]["nodes"] = [n for n in invalid["graph"]["nodes"] if n.get("id") != "source"]
        else: invalid["graph"]["edges"] = []
        assert not Draft202012Validator(registry["receiver-graph.schema.json"]).is_valid(invalid), mutation

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
