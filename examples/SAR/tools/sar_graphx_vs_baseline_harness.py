#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path
from typing import Any

from sar_image_comparator import compare_contracts, default_thresholds, load_thresholds


REPORT_SCHEMA = "graphx.sar.graphx_vs_baseline_harness.v1"


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as out:
        json.dump(value, out, indent=2)
        out.write("\n")


def write_float32_raster(path: Path, values: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as out:
        for value in values:
            out.write(struct.pack("<f", float(value)))


def make_contract(
    *,
    source_tool: str,
    provenance_class: str,
    scenario_id: str,
    raw_path: Path,
    width: int,
    height: int,
) -> dict[str, Any]:
    return {
        "source_tool": source_tool,
        "provenance_class": provenance_class,
        "scenario_id": scenario_id,
        "format": "float32_raster",
        "layout": "row_major",
        "artifact_kind": "materialized_image",
        "dtype": "float32",
        "width": width,
        "height": height,
        "byte_count": width * height * 4,
        "raw_path": str(raw_path),
    }


def compare_with_settings(
    graphx_contract: Path,
    reference_contract: Path,
    *,
    strict: bool,
    thresholds_json: Path | None,
) -> dict[str, Any]:
    if strict and thresholds_json is not None:
        raise ValueError("--strict and --thresholds-json are mutually exclusive")

    if thresholds_json is not None:
        thresholds = load_thresholds(thresholds_json.resolve())
    else:
        thresholds = default_thresholds(strict_mode=strict)

    return compare_contracts(
        graphx_contract.resolve(),
        reference_contract.resolve(),
        thresholds,
    )


def run_ci_tiny_fixture(output_dir: Path, strict: bool) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=True)

    pixels = [
        0.0, 0.1, 0.0, 0.0,
        0.2, 1.0, 0.2, 0.0,
        0.0, 0.2, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0,
    ]
    width = 4
    height = 4

    graphx_raw = output_dir / "graphx_tiny.bin"
    reference_raw = output_dir / "reference_tiny.bin"
    write_float32_raster(graphx_raw, pixels)
    write_float32_raster(reference_raw, pixels)

    graphx_contract_path = output_dir / "graphx_contract.json"
    reference_contract_path = output_dir / "reference_contract.json"
    write_json(
        graphx_contract_path,
        make_contract(
            source_tool="graphx",
            provenance_class="graphx_runtime",
            scenario_id="scenario_001",
            raw_path=graphx_raw,
            width=width,
            height=height,
        ),
    )
    write_json(
        reference_contract_path,
        make_contract(
            source_tool="cpu-reference-backprojection",
            provenance_class="deterministic_internal_reference",
            scenario_id="scenario_001",
            raw_path=reference_raw,
            width=width,
            height=height,
        ),
    )

    comparison = compare_with_settings(
        graphx_contract_path,
        reference_contract_path,
        strict=strict,
        thresholds_json=None,
    )
    comparison_path = output_dir / "comparison_report.json"
    write_json(comparison_path, comparison)

    return {
        "schema": REPORT_SCHEMA,
        "mode": "ci_tiny_fixture",
        "local_only": False,
        "ci_safe": True,
        "status": "pass" if comparison.get("passed", False) else "fail",
        "comparison_report": str(comparison_path),
        "notes": [
            "Deterministic tiny fixture comparison for CI-safe validation.",
            "Comparison metrics are validation aids and not production SAR claims.",
        ],
    }


def run_local_comparison(
    graphx_contract: Path,
    reference_contract: Path,
    *,
    strict: bool,
    thresholds_json: Path | None,
) -> tuple[int, dict[str, Any]]:
    if os.getenv("GRAPHX_SAR_BASELINE_RUNNER_ENABLE") != "1":
        return 0, {
            "schema": REPORT_SCHEMA,
            "mode": "local_baseline_comparison",
            "local_only": True,
            "ci_safe": False,
            "status": "skipped",
            "reason": "local_opt_in_not_enabled",
            "opt_in_env": "GRAPHX_SAR_BASELINE_RUNNER_ENABLE",
        }

    if not graphx_contract.exists() or not reference_contract.exists():
        return 0, {
            "schema": REPORT_SCHEMA,
            "mode": "local_baseline_comparison",
            "local_only": True,
            "ci_safe": False,
            "status": "skipped",
            "reason": "missing_contract_inputs",
            "graphx_contract": str(graphx_contract),
            "reference_contract": str(reference_contract),
        }

    report = compare_with_settings(
        graphx_contract,
        reference_contract,
        strict=strict,
        thresholds_json=thresholds_json,
    )

    return (0 if report.get("passed", False) else 1), {
        "schema": REPORT_SCHEMA,
        "mode": "local_baseline_comparison",
        "local_only": True,
        "ci_safe": False,
        "status": "pass" if report.get("passed", False) else "fail",
        "comparison_report": report,
        "notes": [
            "Local-only comparison path intended for manual baseline validation.",
            "Comparison metrics are validation aids and not production SAR claims.",
        ],
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="GraphX-vs-baseline SAR comparison harness")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    tiny = subparsers.add_parser(
        "run-ci-tiny-fixture",
        help="Generate deterministic tiny fixture contracts and compare them in a CI-safe path",
    )
    tiny.add_argument("--output-dir", required=True)
    tiny.add_argument("--output-json", required=True)
    tiny.add_argument("--strict", action="store_true")

    local = subparsers.add_parser(
        "run-local-comparison",
        help="Run local-only GraphX-vs-baseline comparison when explicitly enabled",
    )
    local.add_argument("--graphx-contract", required=True)
    local.add_argument("--reference-contract", required=True)
    local.add_argument("--output-json", required=True)
    local.add_argument("--thresholds-json")
    local.add_argument("--strict", action="store_true")

    return parser


def main() -> int:
    args = build_parser().parse_args()

    if args.command_name == "run-ci-tiny-fixture":
        result = run_ci_tiny_fixture(Path(args.output_dir).resolve(), strict=args.strict)
        output = Path(args.output_json).resolve()
        write_json(output, result)
        print(output)
        return 0 if result.get("status") == "pass" else 1

    if args.command_name == "run-local-comparison":
        exit_code, result = run_local_comparison(
            Path(args.graphx_contract).resolve(),
            Path(args.reference_contract).resolve(),
            strict=args.strict,
            thresholds_json=Path(args.thresholds_json).resolve() if args.thresholds_json else None,
        )
        output = Path(args.output_json).resolve()
        write_json(output, result)
        print(output)
        return exit_code

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
