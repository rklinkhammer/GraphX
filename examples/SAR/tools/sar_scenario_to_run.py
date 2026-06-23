from __future__ import annotations

import json
from pathlib import Path
from typing import Any


SUPPORTED_VERSION = "graphx.sar.scenario.v1"


def repo_root_from_script(script_path: Path) -> Path:
    return script_path.resolve().parents[3]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def validate_scenario_manifest(manifest: dict[str, Any]) -> list[str]:
    required_fields = [
        "version",
        "dataset",
        "pulse_range",
        "range_bins",
        "image_grid",
        "scene_center",
        "algorithm",
        "window",
        "range_compression",
        "output",
    ]
    errors: list[str] = []
    for field in required_fields:
        if field not in manifest:
            errors.append(f"missing required field: {field}")

    if errors:
        return errors

    if manifest.get("version") != SUPPORTED_VERSION:
        errors.append("unsupported scenario manifest version")

    return errors


def scenario_id_from_path(path: Path) -> str:
    return path.stem


def build_local_replay_graph_template() -> dict[str, Any]:
    return {
        "name": "sar_local_replay_scaffold",
        "execution_backend": "metal",
        "backend_fallback_policy": "allow_fallback",
        "resolver_diagnostics": True,
        "edge_contract": "accel-token",
        "resolver_mappings": [
            {
                "intent_type": "SarBackprojectionTransformAccelNode",
                "input_token_type": "SarAccelControlToken",
                "output_token_type": "SarAccelControlToken",
                "variants": [
                    {
                        "backend": backend,
                        "concrete_type": "SarBackprojectionTransformAccelNode",
                    }
                    for backend in ("metal", "stub", "cuda", "sycl")
                ],
            }
        ],
        "num_threads": 4,
        "nodes": [
            {
                "id": "src",
                "type": "GotchaReplaySourceNode",
                "node_config": {
                    "fixture_path": "REPLACE_WITH_LOCAL_EXTERNAL_FIXTURE_PATH",
                    "emit_watermark": False,
                    "allow_external_fixture": True,
                },
            },
            {
                "id": "compression",
                "type": "RangeCompressionNode",
                "node_config": {
                    "enabled": True,
                    "gain": 1.0,
                    "sample_rate_hz": 48_000.0,
                },
            },
            {
                "id": "split",
                "type": "AzimuthTileSplitNode",
                "node_config": {
                    "tile_count": 4,
                    "tile_id_offset": 0,
                    "backend_id": 0,
                },
            },
            {
                "id": "h2d",
                "type": "H2DAsyncAccelNode",
                "node_config": {
                    "override_backend": False,
                    "backend_id": 0,
                },
            },
            {
                "id": "bp",
                "type": "SarBackprojectionTransformAccelNode",
                "node_config": {
                    "image_width": 16,
                    "backend_id": 0,
                    "queue_id": 0,
                    "kernel_id": 3301,
                },
            },
            {
                "id": "d2h",
                "type": "D2HAsyncAccelNode",
                "node_config": {
                    "override_backend": False,
                    "backend_id": 0,
                },
            },
            {
                "id": "merge",
                "type": "ImageTileMergeNode",
                "node_config": {
                    "expected_tiles": 4,
                    "require_watermark_before_complete": False,
                    "backend_id": 0,
                    "backend": 1,
                },
            },
            {
                "id": "sink",
                "type": "SarDiagnosticsSinkNode",
                "node_config": {"completion_signal_enabled": True},
            },
        ],
        "edges": [
            {
                "source_node_id": source,
                "source_port": 0,
                "target_node_id": target,
                "target_port": 0,
            }
            for source, target in (
                ("src", "compression"),
                ("compression", "split"),
                ("split", "h2d"),
                ("h2d", "bp"),
                ("bp", "d2h"),
                ("d2h", "merge"),
                ("merge", "sink"),
            )
        ],
    }


def build_graphx_config(scenario: dict[str, Any], manual_template: dict[str, Any]) -> dict[str, Any]:
    config = json.loads(json.dumps(manual_template))
    config["name"] = f"sar_scenario_{scenario['dataset']['subset']}"

    image_grid = scenario["image_grid"]
    pulse_range = scenario["pulse_range"]
    range_bins = scenario["range_bins"]
    output = scenario["output"]

    for node in config.get("nodes", []):
        if node.get("id") == "src":
            node.setdefault("node_config", {})["emit_watermark"] = False
        if node.get("id") == "bp":
            node.setdefault("node_config", {})["image_width"] = image_grid["width"]
        if node.get("id") == "compression":
            node.setdefault("node_config", {})["enabled"] = bool(
                scenario["range_compression"].get("enabled", True)
            )

    if not any(node.get("id") == "materialize" for node in config.get("nodes", [])):
        materialize_node = {
            "id": "materialize",
            "type": "SarMaterializedImageSinkNode",
            "node_config": {
                "enabled": output.get("artifact_kind") == "materialized_image"
            },
        }

        new_nodes: list[dict[str, Any]] = []
        for node in config.get("nodes", []):
            if node.get("id") == "merge":
                new_nodes.append(materialize_node)
            new_nodes.append(node)
        config["nodes"] = new_nodes

        new_edges: list[dict[str, Any]] = []
        for edge in config.get("edges", []):
            if edge.get("source_node_id") == "d2h" and edge.get("target_node_id") == "merge":
                new_edges.append(
                    {
                        "source_node_id": "d2h",
                        "source_port": 0,
                        "target_node_id": "materialize",
                        "target_port": 0,
                    }
                )
                new_edges.append(
                    {
                        "source_node_id": "materialize",
                        "source_port": 0,
                        "target_node_id": "merge",
                        "target_port": 0,
                    }
                )
                continue
            new_edges.append(edge)
        config["edges"] = new_edges

    config.setdefault("metadata", {})
    config["metadata"]["scenario_id"] = scenario["dataset"]["subset"]
    config["metadata"]["scenario_version"] = scenario["version"]
    config["metadata"]["pulse_range"] = pulse_range
    config["metadata"]["range_bins"] = range_bins
    config["metadata"]["output"] = output
    return config


def ensure_layout(output_dir: Path) -> dict[str, Path]:
    layout = {
        "manifest": output_dir / "manifest",
        "graphx": output_dir / "graphx",
        "reference": output_dir / "reference",
        "reports": output_dir / "reports",
    }
    for path in layout.values():
        path.mkdir(parents=True, exist_ok=True)
    return layout


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
