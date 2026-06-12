#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import math
import struct
import sys
from pathlib import Path
from typing import Any

from rrp1_scenario_to_run import load_json, write_json


SCHEMA_VERSION = "graphx.sar.image_comparison_report.v1"


def resolve_artifact_path(contract_path: Path, raw_path_text: str) -> Path:
    raw_path = Path(raw_path_text)
    if raw_path.is_absolute():
        return raw_path.resolve()
    return (contract_path.parent / raw_path).resolve()


def read_float32_raster(path: Path) -> list[float]:
    raw_bytes = path.read_bytes()
    if len(raw_bytes) % 4 != 0:
        raise ValueError(f"raw raster size must be a multiple of 4 bytes: {path}")
    return [value[0] for value in struct.iter_unpack("<f", raw_bytes)]


def hash_raw_bytes(path: Path) -> str:
    return hashlib.blake2b(path.read_bytes(), digest_size=8).hexdigest()


def compare_pixels(graphx_pixels: list[float], reference_pixels: list[float]) -> dict[str, float]:
    if len(graphx_pixels) != len(reference_pixels):
        raise ValueError("pixel counts must match to compute deterministic metrics")
    if not graphx_pixels:
        raise ValueError("pixel counts must be non-zero")

    l_inf = 0.0
    sum_sq = 0.0
    expected_sum_sq = 0.0
    for actual, expected in zip(graphx_pixels, reference_pixels):
        diff = float(actual) - float(expected)
        abs_diff = abs(diff)
        if abs_diff > l_inf:
            l_inf = abs_diff
        sum_sq += diff * diff
        expected_sum_sq += float(expected) * float(expected)

    count = float(len(reference_pixels))
    rms = math.sqrt(sum_sq / count)
    relative_l2 = 0.0 if expected_sum_sq == 0.0 else math.sqrt(sum_sq / expected_sum_sq)
    return {
        "l_inf": l_inf,
        "rms": rms,
        "relative_l2": relative_l2,
    }


def build_artifact_contract(contract_path: Path, contract: dict[str, Any]) -> dict[str, Any]:
    raw_path = resolve_artifact_path(contract_path, contract["raw_path"])
    pixels = read_float32_raster(raw_path)
    image_hash = hash_raw_bytes(raw_path)
    return {
        "source_tool": contract.get("source_tool", "unknown"),
        "provenance_class": contract["provenance_class"],
        "scenario_id": contract["scenario_id"],
        "format": contract["format"],
        "layout": contract["layout"],
        "artifact_kind": contract["artifact_kind"],
        "dtype": contract["dtype"],
        "width": contract["width"],
        "height": contract["height"],
        "byte_count": contract["byte_count"],
        "raw_path": str(raw_path),
        "image_hash": image_hash,
        "pixel_count": len(pixels),
        "pixels": pixels,
    }


def compare_contracts(graphx_contract_path: Path, reference_contract_path: Path) -> dict[str, Any]:
    graphx_contract = load_json(graphx_contract_path)
    reference_contract = load_json(reference_contract_path)

    graphx = build_artifact_contract(graphx_contract_path, graphx_contract)
    reference = build_artifact_contract(reference_contract_path, reference_contract)

    checks: list[dict[str, Any]] = []
    reasons: list[str] = []

    def add_check(name: str, passed: bool, detail: str) -> None:
        checks.append({"name": name, "passed": passed, "detail": detail})
        if not passed:
            reasons.append(f"{name}: {detail}")

    add_check(
        "graphx_source_tool",
        graphx["source_tool"] == "graphx",
        f"graphx={graphx['source_tool']}",
    )
    add_check(
        "graphx_provenance_class",
        graphx["provenance_class"] == "graphx_runtime",
        f"graphx={graphx['provenance_class']}",
    )
    add_check(
        "reference_provenance_class_allowed",
        reference["provenance_class"] in {"external_baseline", "deterministic_internal_reference"},
        f"reference={reference['provenance_class']}",
    )
    add_check(
        "reference_source_tool_and_provenance_alignment",
        (
            (reference["provenance_class"] == "external_baseline" and reference["source_tool"] != "graphx")
            or (
                reference["provenance_class"] == "deterministic_internal_reference"
                and reference["source_tool"] in {"deterministic-reference", "graphx-deterministic-reference"}
            )
        ),
        f"graphx={graphx['source_tool']}, reference={reference['source_tool']}",
    )
    add_check(
        "scenario_id_match",
        graphx["scenario_id"] == reference["scenario_id"],
        f"graphx={graphx['scenario_id']}, reference={reference['scenario_id']}",
    )
    add_check(
        "format_match",
        graphx["format"] == reference["format"],
        f"graphx={graphx['format']}, reference={reference['format']}",
    )
    add_check(
        "layout_match",
        graphx["layout"] == reference["layout"],
        f"graphx={graphx['layout']}, reference={reference['layout']}",
    )
    add_check(
        "artifact_kind_match",
        graphx["artifact_kind"] == reference["artifact_kind"],
        f"graphx={graphx['artifact_kind']}, reference={reference['artifact_kind']}",
    )
    add_check(
        "dtype_match",
        graphx["dtype"] == reference["dtype"],
        f"graphx={graphx['dtype']}, reference={reference['dtype']}",
    )
    add_check(
        "dimensions_match",
        graphx["width"] == reference["width"] and graphx["height"] == reference["height"],
        f"graphx={graphx['width']}x{graphx['height']}, reference={reference['width']}x{reference['height']}",
    )
    add_check(
        "byte_count_match",
        graphx["byte_count"] == reference["byte_count"],
        f"graphx={graphx['byte_count']}, reference={reference['byte_count']}",
    )
    add_check(
        "pixel_count_match",
        graphx["pixel_count"] == reference["pixel_count"],
        f"graphx={graphx['pixel_count']}, reference={reference['pixel_count']}",
    )

    metrics: dict[str, Any]
    if graphx["pixel_count"] == reference["pixel_count"] and graphx["pixel_count"] > 0:
        metrics = compare_pixels(graphx["pixels"], reference["pixels"])
        add_check("pixel_metrics_zero", metrics["l_inf"] == 0.0 and metrics["rms"] == 0.0 and metrics["relative_l2"] == 0.0,
                  f"l_inf={metrics['l_inf']}, rms={metrics['rms']}, relative_l2={metrics['relative_l2']}")
    else:
        metrics = {"l_inf": None, "rms": None, "relative_l2": None}
        add_check(
            "pixel_metrics_zero",
            False,
            "pixel metrics are unavailable because the raster sizes differ or are empty",
        )

    passed = all(check["passed"] for check in checks)
    return {
        "schema_version": SCHEMA_VERSION,
        "comparator": {
            "name": "rrp4_image_comparator",
            "version": 1,
        },
        "scenario_id": graphx["scenario_id"],
        "verdict": "pass" if passed else "fail",
        "passed": passed,
        "graphx": graphx | {"pixels": None},
        "reference": reference | {"pixels": None},
        "metrics": metrics,
        "checks": checks,
        "reasons": reasons,
    }


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare GraphX and gotcha-back image outputs")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    compare = subparsers.add_parser("compare", help="Compare two normalized image contracts and emit a report")
    compare.add_argument("--graphx-contract", required=True)
    compare.add_argument("--reference-contract", required=True)
    compare.add_argument("--report-json", required=True)

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "compare":
        report = compare_contracts(Path(args.graphx_contract).resolve(), Path(args.reference_contract).resolve())
        report_path = Path(args.report_json).resolve()
        write_json(report_path, report)
        print(report_path)
        return 0 if report["passed"] else 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())