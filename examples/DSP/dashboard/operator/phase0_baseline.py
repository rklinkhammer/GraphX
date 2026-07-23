#!/usr/bin/env python3
"""Generate and verify the concise deterministic Dashboard Phase 0 baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from frontend_asset_inventory import compare_frontend_inventories, inventory_frontend

CANONICALIZATION = "json-sort-keys-compact-utf8-lf-v1"


def canonical_bytes(document: object) -> bytes:
    return (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def binding(root: Path, path: Path) -> dict[str, str]:
    return {"path":path.relative_to(root).as_posix(), "sha256":sha256(path)}


def topology_identity_document(graph: dict[str, object], graph_sha256: str
                               ) -> dict[str, object]:
    nodes_in = graph.get("nodes")
    edges_in = graph.get("edges")
    if not isinstance(nodes_in, list) or not isinstance(edges_in, list):
        raise ValueError("topology graph requires node and edge arrays")
    node_ids = [str(node["id"]) for node in nodes_in]
    if len(node_ids) != len(set(node_ids)):
        raise ValueError("configuration node IDs are not unique")
    nodes = [{"configuration_node_id":node_id,
              "future_visual_node_id":node_id} for node_id in sorted(node_ids)]
    edge_ids: set[str] = set()
    edges: list[dict[str, object]] = []
    for edge in edges_in:
        source = str(edge["source_node_id"])
        target = str(edge["target_node_id"])
        source_port = int(edge["source_port"])
        target_port = int(edge["target_port"])
        if source not in node_ids or target not in node_ids or \
                source_port < 0 or target_port < 0:
            raise ValueError("topology edge is invalid")
        edge_id = f"{source}:{source_port}->{target}:{target_port}"
        if edge_id in edge_ids:
            raise ValueError(f"duplicate canonical edge identity: {edge_id}")
        edge_ids.add(edge_id)
        edges.append({"configuration_edge_id":edge_id,
                      "future_visual_edge_id":edge_id,
                      "source_node_id":source, "source_port":source_port,
                      "target_node_id":target, "target_port":target_port})
    edges.sort(key=lambda item: str(item["configuration_edge_id"]))
    detector_merge = [item for item in edges
                      if str(item["source_node_id"]).startswith("detector_")
                      and item["target_node_id"] == "merge"]
    if len(nodes) != 75 or len(edges) != 137 or len(detector_merge) != 64:
        raise ValueError("Phase 2 graph cardinality contract changed")
    by_source = {str(item["source_node_id"]):item for item in detector_merge}
    for detector in range(64):
        item = by_source.get(f"detector_{detector}")
        if item is None or item["source_port"] != 0 or \
                item["target_port"] != detector + 1:
            raise ValueError(f"detector {detector} merge-port contract changed")
    return {
        "schema":"graphx.fhss.dashboard.topology_identity.v1",
        "source_graph":{"logical_name":"fhss_phase2_binary_iq_receiver",
                        "sha256":graph_sha256},
        "identity_rule":"source-node-id:source-port->target-node-id:target-port",
        "array_positions_are_identities":False,
        "runtime_overlays_enabled":False,
        "deferred_until_phase3":["runtime_node_to_configuration",
                                 "metric_to_configuration",
                                 "diagnostic_to_configuration"],
        "node_count":len(nodes), "edge_count":len(edges),
        "nodes":nodes, "edges":edges,
        "detector_merge_contract":{"count":64, "first_target_port":1,
                                   "last_target_port":64}}


def topology_identity_from_path(graph_path: Path) -> dict[str, object]:
    raw = graph_path.read_bytes()
    first = json.loads(raw)
    reconstructed = json.loads(canonical_bytes(first))
    if first != reconstructed:
        raise ValueError("graph parse/serialize/parse changed semantics")
    identity = topology_identity_document(first, hashlib.sha256(raw).hexdigest())
    equivalent = {"nodes":list(reversed(reconstructed["nodes"])),
                  "edges":list(reversed(reconstructed["edges"]))}
    if topology_identity_document(equivalent, hashlib.sha256(raw).hexdigest()) != identity:
        raise ValueError("equivalent graph reconstruction changed stable identities")
    return identity


def build(root: Path) -> tuple[dict[str, object], dict[str, object]]:
    dashboard = root / "examples/DSP/dashboard"
    api = dashboard / "api/openapi.json"
    openapi = json.loads(api.read_text(encoding="utf-8"))
    paths = set(openapi.get("paths", {}))
    application_paths = {path for path in paths if path.startswith("/api/") and
                         path != "/api/v1/version"}
    if (any(path.startswith(("/api/v2", "/legacy", "/v2")) for path in paths) or
            any(not path.startswith("/api/v1/fhss")
                for path in application_paths)):
        raise ValueError("/api/v1/fhss is not the sole dashboard API namespace")
    schema_paths = sorted((dashboard / "api/schemas").glob("*.json"))
    graph_path = root / "libdsp/config/fhss_phase2_binary_iq_receiver.json"
    identity = topology_identity_from_path(graph_path)
    identity_path = dashboard / "baseline/topology-identity.json"
    frontend = inventory_frontend(dashboard)
    manifest = {
        "schema":"graphx.fhss.dashboard.phase0_baseline.v1",
        "canonicalization":CANONICALIZATION,
        "authoritative_contracts":{
            "openapi":binding(root, api),
            "application_namespace":"/api/v1/fhss",
            "root_entrypoint":"/",
            "schemas":[binding(root, path) for path in schema_paths]},
        "frontend_assets":{"source":frontend, "installed_expected":frontend,
                           "exact_release_agreement_required":True},
        "installed_layout":{
            "frontend_root":"share/graphx/fhss-dashboard",
            "contract_root":"share/graphx/fhss-dashboard/api",
            "receiver_graph":"share/graphx/config/fhss_phase2_binary_iq_receiver.json",
            "frontend_files":[str(entry["path"]) for entry in frontend["entries"]]},
        "topology_identity":{
            "path":identity_path.relative_to(root).as_posix(),
            "sha256":hashlib.sha256(canonical_bytes(identity)).hexdigest()},
        "stable_repository_inputs":[
            binding(root, dashboard / "frontend/package-lock.json"),
            binding(root, dashboard / "frontend/toolchain.json"),
            binding(root, dashboard / "frontend/THIRD_PARTY_NOTICES.md"),
            binding(root, root / "docs/dsp/fhss_dashboard_frontend_policy.md"),
            binding(root, root / "docs/dsp/adr/0001-fhss-dashboard-react-flow-elk.md")],
        "feature_expectations":{
            "configuration":"validate, stage, atomically apply, and preserve strong identity",
            "runtime_lifecycle":"rebuild, start, stop, and report truthful state",
            "message_jobs":"bounded synthetic job control without receiver truth injection",
            "observations":"receiver-derived and separate from expected truth",
            "comparison":"explicit expected-versus-observed evaluation",
            "spectrum":"bounded receiver-side spectrum or explicit unavailability",
            "investigations":"export, validate, and replay linked synthetic artifacts",
            "event_recovery":"ordered envelopes, replay, gap detection, and coherent snapshot resync",
            "shutdown":"bounded stop, join, and port release"},
        "qualification":{
            "dashboard_scope":"FHSS-specific", "synthetic_data_only":True,
            "hwil_available":False, "conducted_rf_available":False,
            "ota_available":False, "live_rf_available":False,
            "production_rf_qualified":False}}
    return manifest, identity


def write(root: Path) -> None:
    manifest, identity = build(root)
    baseline = root / "examples/DSP/dashboard/baseline"
    baseline.mkdir(parents=True, exist_ok=True)
    (baseline / "topology-identity.json").write_bytes(canonical_bytes(identity))
    (baseline / "phase0-baseline.json").write_bytes(canonical_bytes(manifest))


def verify(root: Path, installed_root: Path | None = None) -> None:
    expected_manifest, expected_identity = build(root)
    baseline = root / "examples/DSP/dashboard/baseline"
    manifest_path = baseline / "phase0-baseline.json"
    identity_path = baseline / "topology-identity.json"
    actual_manifest = json.loads(manifest_path.read_text())
    actual_identity = json.loads(identity_path.read_text())
    if actual_manifest != expected_manifest or actual_identity != expected_identity:
        raise ValueError("Phase 0 baseline is stale; regenerate it deliberately")
    if manifest_path.read_bytes() != canonical_bytes(actual_manifest) or \
            identity_path.read_bytes() != canonical_bytes(actual_identity):
        raise ValueError("Phase 0 baseline is not canonical")
    if installed_root is None:
        return
    installed_assets = installed_root / "share/graphx/fhss-dashboard"
    compare_frontend_inventories(
        actual_manifest["frontend_assets"]["source"],
        inventory_frontend(installed_assets))
    installed_html = sorted(path.relative_to(installed_assets).as_posix()
                            for path in installed_assets.rglob("*.html"))
    if installed_html != ["index.html"]:
        raise ValueError("installed dashboard has an alternate HTML entrypoint")
    installed_graph = installed_root / str(
        actual_manifest["installed_layout"]["receiver_graph"])
    if topology_identity_from_path(installed_graph) != actual_identity:
        raise ValueError("installed receiver topology differs from source")
    source_api = root / "examples/DSP/dashboard/api"
    installed_api = installed_assets / "api"
    source_contracts = {path.relative_to(source_api).as_posix():sha256(path)
                        for path in source_api.rglob("*.json")}
    installed_contracts = {path.relative_to(installed_api).as_posix():sha256(path)
                           for path in installed_api.rglob("*.json")}
    if source_contracts != installed_contracts:
        raise ValueError("installed API contracts differ from source")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("generate", "verify"))
    parser.add_argument("--source-root", type=Path,
                        default=Path(__file__).resolve().parents[4])
    parser.add_argument("--installed-root", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    if args.command == "generate":
        write(root)
    else:
        verify(root, args.installed_root.resolve() if args.installed_root else None)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
