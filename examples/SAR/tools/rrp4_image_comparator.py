#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path
from typing import Any

from rrp1_scenario_to_run import load_json, write_json


SCHEMA_VERSION = "graphx.sar.image_comparison_report.v1"


def require_field(contract: dict[str, Any], field: str, contract_name: str) -> Any:
    if field not in contract:
        raise KeyError(f"{contract_name}.{field} is required")
    return contract[field]


def require_string(contract: dict[str, Any], field: str, contract_name: str) -> str:
    value = require_field(contract, field, contract_name)
    if not isinstance(value, str):
        raise TypeError(f"{contract_name}.{field} must be a string")
    return value


def require_int(contract: dict[str, Any], field: str, contract_name: str) -> int:
    value = require_field(contract, field, contract_name)
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"{contract_name}.{field} must be an integer")
    return value


def require_float(contract: dict[str, Any], field: str, contract_name: str) -> float:
    value = require_field(contract, field, contract_name)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise TypeError(f"{contract_name}.{field} must be numeric")
    return float(value)


def optional_float(contract: dict[str, Any], field: str, contract_name: str) -> float | None:
    if field not in contract or contract[field] is None:
        return None
    return require_float(contract, field, contract_name)


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


def default_thresholds(strict_mode: bool) -> dict[str, Any]:
    if strict_mode:
        return {
            "mode": "strict",
            "peak_window_radius_pixels": 1,
            "rms_max": 0.0,
            "relative_l2_max": 0.0,
            "max_abs_error_max": 0.0,
            "peak_coordinate_delta_pixels_max": 0.0,
            "pslr_db_min": None,
            "islr_db_min": None,
        }
    return {
        "mode": "default",
        "peak_window_radius_pixels": 1,
        "rms_max": 1.0e-6,
        "relative_l2_max": 1.0e-6,
        "max_abs_error_max": 1.0e-6,
        "peak_coordinate_delta_pixels_max": 0.0,
        "pslr_db_min": None,
        "islr_db_min": None,
    }


def load_thresholds(path: Path) -> dict[str, Any]:
    thresholds = load_json(path)
    contract_name = "thresholds"
    output: dict[str, Any] = {
        "mode": "configured",
        "peak_window_radius_pixels": require_int(thresholds, "peak_window_radius_pixels", contract_name),
        "rms_max": require_float(thresholds, "rms_max", contract_name),
        "relative_l2_max": require_float(thresholds, "relative_l2_max", contract_name),
        "max_abs_error_max": require_float(thresholds, "max_abs_error_max", contract_name),
        "peak_coordinate_delta_pixels_max": require_float(
            thresholds, "peak_coordinate_delta_pixels_max", contract_name
        ),
        "pslr_db_min": optional_float(thresholds, "pslr_db_min", contract_name),
        "islr_db_min": optional_float(thresholds, "islr_db_min", contract_name),
    }

    if output["peak_window_radius_pixels"] < 0:
        raise ValueError("thresholds.peak_window_radius_pixels must be non-negative")
    return output


def find_peak_index(pixels: list[float]) -> int:
    if not pixels:
        raise ValueError("pixel counts must be non-zero")
    peak_index = 0
    peak_value = pixels[0]
    for index, value in enumerate(pixels[1:], start=1):
        if value > peak_value:
            peak_index = index
            peak_value = value
    return peak_index


def compute_peak_metrics(
    graphx_pixels: list[float], reference_pixels: list[float], width: int, height: int
) -> dict[str, float]:
    if width * height != len(graphx_pixels) or width * height != len(reference_pixels):
        raise ValueError("image dimensions must match pixel counts to compute peak metrics")

    graphx_peak = find_peak_index(graphx_pixels)
    reference_peak = find_peak_index(reference_pixels)

    graphx_peak_x = graphx_peak % width
    graphx_peak_y = graphx_peak // width
    reference_peak_x = reference_peak % width
    reference_peak_y = reference_peak // width

    dx = float(graphx_peak_x - reference_peak_x)
    dy = float(graphx_peak_y - reference_peak_y)
    return {
        "peak_coordinate_delta_x_pixels": abs(dx),
        "peak_coordinate_delta_y_pixels": abs(dy),
        "peak_coordinate_delta_pixels": math.hypot(dx, dy),
    }


def compute_pslr_islr(
    pixels: list[float], width: int, height: int, peak_window_radius_pixels: int
) -> dict[str, float | None]:
    if width * height != len(pixels):
        raise ValueError("image dimensions must match pixel counts to compute sidelobe metrics")
    if peak_window_radius_pixels < 0:
        raise ValueError("peak_window_radius_pixels must be non-negative")

    peak_index = find_peak_index(pixels)
    peak_x = peak_index % width
    peak_y = peak_index // width
    peak_value = abs(float(pixels[peak_index]))
    if peak_value == 0.0:
        return {"pslr_db": None, "islr_db": None}

    peak_energy = 0.0
    sidelobe_energy = 0.0
    max_sidelobe = 0.0
    for index, value in enumerate(pixels):
        x = index % width
        y = index // width
        magnitude = abs(float(value))
        in_mainlobe = abs(x - peak_x) <= peak_window_radius_pixels and abs(y - peak_y) <= peak_window_radius_pixels
        if in_mainlobe:
            peak_energy += magnitude * magnitude
            continue
        sidelobe_energy += magnitude * magnitude
        if magnitude > max_sidelobe:
            max_sidelobe = magnitude

    pslr_db = None if max_sidelobe == 0.0 else 20.0 * math.log10(peak_value / max_sidelobe)
    islr_db = None if peak_energy == 0.0 or sidelobe_energy == 0.0 else 10.0 * math.log10(sidelobe_energy / peak_energy)
    return {"pslr_db": pslr_db, "islr_db": islr_db}


def build_artifact_contract(contract_path: Path, contract: dict[str, Any], contract_name: str) -> dict[str, Any]:
    source_tool = require_string(contract, "source_tool", contract_name)
    provenance_class = require_string(contract, "provenance_class", contract_name)
    scenario_id = require_string(contract, "scenario_id", contract_name)
    artifact_format = require_string(contract, "format", contract_name)
    layout = require_string(contract, "layout", contract_name)
    artifact_kind = require_string(contract, "artifact_kind", contract_name)
    dtype = require_string(contract, "dtype", contract_name)
    width = require_int(contract, "width", contract_name)
    height = require_int(contract, "height", contract_name)
    byte_count = require_int(contract, "byte_count", contract_name)
    raw_path_text = require_string(contract, "raw_path", contract_name)

    raw_path = resolve_artifact_path(contract_path, raw_path_text)
    pixels = read_float32_raster(raw_path)
    image_hash = hash_raw_bytes(raw_path)
    return {
        "source_tool": source_tool,
        "provenance_class": provenance_class,
        "scenario_id": scenario_id,
        "format": artifact_format,
        "layout": layout,
        "artifact_kind": artifact_kind,
        "dtype": dtype,
        "width": width,
        "height": height,
        "byte_count": byte_count,
        "raw_path": str(raw_path),
        "image_hash": image_hash,
        "pixel_count": len(pixels),
        "pixels": pixels,
    }


def compare_contracts(
    graphx_contract_path: Path,
    reference_contract_path: Path,
    thresholds_config: dict[str, Any],
) -> dict[str, Any]:
    graphx_contract = load_json(graphx_contract_path)
    reference_contract = load_json(reference_contract_path)

    graphx = build_artifact_contract(graphx_contract_path, graphx_contract, "graphx_contract")
    reference = build_artifact_contract(reference_contract_path, reference_contract, "reference_contract")

    if graphx["source_tool"] != "graphx":
        raise ValueError(f"graphx_contract.source_tool must be graphx, found {graphx['source_tool']}")
    if graphx["provenance_class"] != "graphx_runtime":
        raise ValueError(
            f"graphx_contract.provenance_class must be graphx_runtime, found {graphx['provenance_class']}"
        )
    if reference["scenario_id"] != graphx["scenario_id"]:
        raise ValueError(
            f"scenario_id mismatch: graphx={graphx['scenario_id']} reference={reference['scenario_id']}"
        )
    if reference["provenance_class"] not in {"deterministic_internal_reference", "external_baseline"}:
        raise ValueError(
            "reference_contract.provenance_class must be deterministic_internal_reference or external_baseline, "
            f"found {reference['provenance_class']}"
        )
    if reference["source_tool"] not in {
        "cpu-reference-backprojection",
        "deterministic-reference",
        "graphx-deterministic-reference",
        "gotcha-back",
    }:
        raise ValueError(
            "reference_contract.source_tool must identify a valid reference artifact tool, "
            f"found {reference['source_tool']}"
        )

    checks: list[dict[str, Any]] = []
    reasons: list[str] = []

    def add_check(name: str, passed: bool, detail: str) -> None:
        checks.append({"name": name, "passed": passed, "detail": detail})
        if not passed:
            reasons.append(f"{name}: {detail}")

    add_check("graphx_source_tool", graphx["source_tool"] == "graphx", f"graphx={graphx['source_tool']}")
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
                and reference["source_tool"]
                in {"cpu-reference-backprojection", "deterministic-reference", "graphx-deterministic-reference"}
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
    dimensions_match = graphx["width"] == reference["width"] and graphx["height"] == reference["height"]
    add_check(
        "dimensions_match",
        dimensions_match,
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

    thresholds = dict(thresholds_config)
    if not dimensions_match or graphx["pixel_count"] != reference["pixel_count"] or graphx["pixel_count"] == 0:
        metrics = {
            "l_inf": None,
            "rms": None,
            "relative_l2": None,
            "max_abs_error": None,
            "peak_coordinate_delta_x_pixels": None,
            "peak_coordinate_delta_y_pixels": None,
            "peak_coordinate_delta_pixels": None,
            "pslr_db": None,
            "islr_db": None,
        }
        add_check(
            "metric_inputs_comparable",
            False,
            "metrics are unavailable because artifact dimensions or pixel counts differ",
        )
    else:
        basic_metrics = compare_pixels(graphx["pixels"], reference["pixels"])
        peak_metrics = compute_peak_metrics(
            graphx["pixels"], reference["pixels"], graphx["width"], graphx["height"]
        )
        sidelobe_metrics = compute_pslr_islr(
            graphx["pixels"], graphx["width"], graphx["height"], thresholds_config["peak_window_radius_pixels"]
        )

        metrics = {
            "l_inf": basic_metrics["l_inf"],
            "rms": basic_metrics["rms"],
            "relative_l2": basic_metrics["relative_l2"],
            "max_abs_error": basic_metrics["l_inf"],
            "peak_coordinate_delta_x_pixels": peak_metrics["peak_coordinate_delta_x_pixels"],
            "peak_coordinate_delta_y_pixels": peak_metrics["peak_coordinate_delta_y_pixels"],
            "peak_coordinate_delta_pixels": peak_metrics["peak_coordinate_delta_pixels"],
            "pslr_db": sidelobe_metrics["pslr_db"],
            "islr_db": sidelobe_metrics["islr_db"],
        }

        threshold_checks = [
            ("rms_within_threshold", metrics["rms"] <= thresholds["rms_max"], f"rms={metrics['rms']}, threshold={thresholds['rms_max']}"),
            (
                "relative_l2_within_threshold",
                metrics["relative_l2"] <= thresholds["relative_l2_max"],
                f"relative_l2={metrics['relative_l2']}, threshold={thresholds['relative_l2_max']}",
            ),
            (
                "max_abs_error_within_threshold",
                metrics["max_abs_error"] <= thresholds["max_abs_error_max"],
                f"max_abs_error={metrics['max_abs_error']}, threshold={thresholds['max_abs_error_max']}",
            ),
            (
                "peak_coordinate_delta_within_threshold",
                metrics["peak_coordinate_delta_pixels"] <= thresholds["peak_coordinate_delta_pixels_max"],
                f"peak_coordinate_delta_pixels={metrics['peak_coordinate_delta_pixels']}, threshold={thresholds['peak_coordinate_delta_pixels_max']}",
            ),
        ]

        if thresholds.get("pslr_db_min") is not None:
            threshold_checks.append(
                (
                    "pslr_db_above_threshold",
                    metrics["pslr_db"] is not None and metrics["pslr_db"] >= thresholds["pslr_db_min"],
                    f"pslr_db={metrics['pslr_db']}, threshold={thresholds['pslr_db_min']}",
                )
            )
        if thresholds.get("islr_db_min") is not None:
            threshold_checks.append(
                (
                    "islr_db_above_threshold",
                    metrics["islr_db"] is not None and metrics["islr_db"] >= thresholds["islr_db_min"],
                    f"islr_db={metrics['islr_db']}, threshold={thresholds['islr_db_min']}",
                )
            )

        for name, passed, detail in threshold_checks:
            add_check(name, passed, detail)

    passed = all(check["passed"] for check in checks)
    return {
        "schema_version": SCHEMA_VERSION,
        "comparator": {
            "name": "rrp4_image_comparator",
            "version": 1,
        },
        "scenario_id": graphx["scenario_id"],
        "thresholds": thresholds,
        "verdict": "pass" if passed else "fail",
        "passed": passed,
        "graphx": graphx | {"pixels": None},
        "reference": reference | {"pixels": None},
        "metrics": metrics,
        "checks": checks,
        "reasons": reasons,
    }


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compare GraphX and reference image artifacts")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    compare = subparsers.add_parser("compare", help="Compare two normalized image contracts and emit a report")
    compare.add_argument("--graphx-contract", required=True)
    compare.add_argument("--reference-contract", required=True)
    compare.add_argument("--report-json", required=True)
    compare.add_argument("--thresholds-json")
    compare.add_argument("--strict", action="store_true")

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "compare":
        if args.thresholds_json and args.strict:
            raise ValueError("--thresholds-json and --strict are mutually exclusive")

        if args.thresholds_json:
            thresholds = load_thresholds(Path(args.thresholds_json).resolve())
        else:
            thresholds = default_thresholds(strict_mode=args.strict)

        report = compare_contracts(
            Path(args.graphx_contract).resolve(),
            Path(args.reference_contract).resolve(),
            thresholds,
        )
        report_path = Path(args.report_json).resolve()
        write_json(report_path, report)
        print(report_path)
        return 0 if report["passed"] else 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())