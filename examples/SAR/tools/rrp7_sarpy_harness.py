#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path
from typing import Any

from rrp1_scenario_to_run import load_json, write_json


CONTRACT_SCHEMA = "graphx.sar.product_metadata_contract.v1"


def sarpy_environment_status() -> dict[str, Any]:
    spec = importlib.util.find_spec("sarpy")
    installed = spec is not None
    version = None
    if installed:
        try:
            import sarpy  # type: ignore

            version = getattr(sarpy, "__version__", "unknown")
        except Exception:
            version = "unknown"

    return {
        "tool": "SarPy",
        "installed": installed,
        "version": version,
        "local_only": True,
        "ci_safe": False,
        "requires_manual_dataset_path": True,
        "comparison_scope": "product_metadata_validation_only",
        "notes": [
            "SarPy is an optional local-only harness and is not required for normal CI.",
            "SarPy outputs are normalized as product/metadata contracts only.",
            "This harness does not prove GraphX phase-history image-formation correctness.",
        ],
    }


def require_string(data: dict[str, Any], field: str, object_name: str) -> str:
    if field not in data or not isinstance(data[field], str) or not data[field]:
        raise ValueError(f"{object_name}.{field} must be a non-empty string")
    return data[field]


def require_object(data: dict[str, Any], field: str, object_name: str) -> dict[str, Any]:
    if field not in data or not isinstance(data[field], dict):
        raise ValueError(f"{object_name}.{field} must be an object")
    return data[field]


def normalize_metadata_contract(scenario_path: Path, input_path: Path, output_path: Path) -> dict[str, Any]:
    scenario = load_json(scenario_path)
    source = load_json(input_path)

    scenario_id = require_string(scenario, "scenario_id", "scenario")
    source_tool = require_string(source, "source_tool", "sarpy_metadata")
    product_type = require_string(source, "product_type", "sarpy_metadata")
    source_product_path = require_string(source, "source_product_path", "sarpy_metadata")
    metadata_fields = require_object(source, "metadata_fields", "sarpy_metadata")

    contract = {
        "schema": CONTRACT_SCHEMA,
        "source_tool": source_tool,
        "provenance_class": "external_baseline",
        "scenario_id": scenario_id,
        "artifact_kind": "product_metadata_summary",
        "format": "json_metadata",
        "layout": "object",
        "dtype": "n/a",
        "comparison_scope": "product_metadata_validation_only",
        "local_only": True,
        "ci_safe": False,
        "product_type": product_type,
        "source_product_path": source_product_path,
        "metadata_fields": metadata_fields,
        "notes": [
            "Normalized SarPy-side metadata contract for local/manual validation only.",
            "Not proof of GraphX phase-history image-formation correctness.",
        ],
    }
    write_json(output_path, contract)
    return contract


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SarPy metadata harness")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report whether SarPy is installed locally")
    probe.add_argument("--output-json", required=True)

    normalize = subparsers.add_parser(
        "normalize-metadata",
        help="Normalize SarPy-side metadata JSON into a GraphX-compatible product metadata contract",
    )
    normalize.add_argument("--scenario", required=True)
    normalize.add_argument("--input-json", required=True)
    normalize.add_argument("--output-json", required=True)
    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "probe-environment":
        output_path = Path(args.output_json).resolve()
        write_json(output_path, sarpy_environment_status())
        print(output_path)
        return 0

    if args.command_name == "normalize-metadata":
        output_path = Path(args.output_json).resolve()
        normalize_metadata_contract(
            Path(args.scenario).resolve(),
            Path(args.input_json).resolve(),
            output_path,
        )
        print(output_path)
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())