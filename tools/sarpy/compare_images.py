#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import sys
from pathlib import Path
from typing import Any


def _import_optional(module_name: str):
    try:
        return importlib.import_module(module_name)
    except Exception:
        return None


def _package_status(module_name: str) -> dict[str, Any]:
    module = _import_optional(module_name)
    installed = module is not None
    version = None
    if installed:
        version = getattr(module, "__version__", "unknown")
    return {
        "installed": installed,
        "version": version,
    }


def probe_environment() -> dict[str, Any]:
    return {
        "tool": "compare_images",
        "local_only": True,
        "ci_safe": False,
        "notes": [
            "This comparator is optional and local-only by default.",
            "It compares artifact outputs and does not alter GraphX runtime contracts.",
        ],
        "packages": {
            "numpy": _package_status("numpy"),
            "matplotlib": _package_status("matplotlib"),
            "sarpy": _package_status("sarpy"),
        },
    }


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _load_optional_json(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def _normalized_string_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value]


def _extract_lineage(metadata: dict[str, Any], npy_path: Path) -> dict[str, Any]:
    ordered_inputs = _normalized_string_list(metadata.get("ordered_crsd_inputs"))
    per_segment_checksums = _normalized_string_list(
        metadata.get("ordered_crsd_input_checksums")
    )
    if not per_segment_checksums:
        per_segment_checksums = _normalized_string_list(metadata.get("per_segment_input_hashes"))

    ordered_set_checksum = metadata.get("ordered_set_hash")
    output_hash = metadata.get("focused_output_sha256")
    if output_hash is None:
        output_hash = metadata.get("output_hash")
    if output_hash is None:
        output_hash = hashlib.sha256(npy_path.read_bytes()).hexdigest()

    algorithm = metadata.get("algorithm")
    if algorithm is None:
        algorithm = metadata.get("reference_kind")
    if algorithm is None:
        algorithm = "unspecified"

    geometry_assumptions = metadata.get("geometry_assumptions")
    if not isinstance(geometry_assumptions, dict):
        geometry_assumptions = {}

    return {
        "ordered_crsd_inputs": ordered_inputs,
        "per_segment_crsd_input_checksums": per_segment_checksums,
        "ordered_set_checksum": ordered_set_checksum,
        "output_hash": str(output_hash),
        "algorithm": str(algorithm),
        "geometry_assumptions": geometry_assumptions,
    }


def _require_numpy_matplotlib():
    numpy = _import_optional("numpy")
    matplotlib = _import_optional("matplotlib")
    if numpy is None or matplotlib is None:
        missing = []
        if numpy is None:
            missing.append("numpy")
        if matplotlib is None:
            missing.append("matplotlib")
        raise RuntimeError("missing required package(s): " + ", ".join(missing))
    return numpy, matplotlib


def _wrapped_phase_difference(numpy, candidate, reference):
    raw = numpy.angle(candidate) - numpy.angle(reference)
    return (raw + numpy.pi) % (2.0 * numpy.pi) - numpy.pi


def _safe_correlation(numpy, a_flat, b_flat) -> float:
    a_mean = float(numpy.mean(a_flat))
    b_mean = float(numpy.mean(b_flat))
    a_centered = a_flat - a_mean
    b_centered = b_flat - b_mean
    a_norm = float(numpy.linalg.norm(a_centered))
    b_norm = float(numpy.linalg.norm(b_centered))

    if a_norm == 0.0 and b_norm == 0.0:
        return 1.0
    if a_norm == 0.0 or b_norm == 0.0:
        return 0.0
    return float(numpy.dot(a_centered, b_centered) / (a_norm * b_norm))


def _global_ssim(numpy, candidate_mag, reference_mag) -> float:
    candidate = candidate_mag.astype(numpy.float64)
    reference = reference_mag.astype(numpy.float64)
    dynamic_range = float(max(candidate.max(), reference.max()) - min(candidate.min(), reference.min()))
    if dynamic_range == 0.0:
        return 1.0 if numpy.array_equal(candidate, reference) else 0.0

    k1 = 0.01
    k2 = 0.03
    c1 = (k1 * dynamic_range) ** 2
    c2 = (k2 * dynamic_range) ** 2

    candidate_mean = float(numpy.mean(candidate))
    reference_mean = float(numpy.mean(reference))
    candidate_var = float(numpy.mean((candidate - candidate_mean) ** 2))
    reference_var = float(numpy.mean((reference - reference_mean) ** 2))
    covariance = float(numpy.mean((candidate - candidate_mean) * (reference - reference_mean)))

    numerator = (2.0 * candidate_mean * reference_mean + c1) * (2.0 * covariance + c2)
    denominator = (candidate_mean ** 2 + reference_mean ** 2 + c1) * (
        candidate_var + reference_var + c2
    )
    if denominator == 0.0:
        return 1.0 if numpy.array_equal(candidate, reference) else 0.0
    return float(numerator / denominator)


def compare_images(
    reference_npy: Path,
    candidate_npy: Path,
    output_report_json: Path,
    output_diff_magnitude_png: Path,
    output_phase_difference_png: Path,
    reference_metadata_json: Path | None,
    candidate_metadata_json: Path | None,
) -> dict[str, Any]:
    numpy, matplotlib = _require_numpy_matplotlib()
    matplotlib.use("Agg")
    pyplot = importlib.import_module("matplotlib.pyplot")

    reference = numpy.load(reference_npy)
    candidate = numpy.load(candidate_npy)
    if reference.shape != candidate.shape:
        raise ValueError("reference and candidate shapes must match")
    if reference.ndim != 2:
        raise ValueError("reference and candidate arrays must be 2D")

    reference_complex = reference.astype(numpy.complex64)
    candidate_complex = candidate.astype(numpy.complex64)

    reference_mag = numpy.abs(reference_complex).astype(numpy.float32)
    candidate_mag = numpy.abs(candidate_complex).astype(numpy.float32)
    magnitude_diff = numpy.abs(candidate_mag - reference_mag).astype(numpy.float32)

    phase_diff = _wrapped_phase_difference(numpy, candidate_complex, reference_complex).astype(numpy.float32)

    rmse = float(numpy.sqrt(numpy.mean((candidate_mag - reference_mag) ** 2)))
    phase_rmse = float(numpy.sqrt(numpy.mean(phase_diff ** 2)))
    peak_error = float(abs(float(numpy.max(candidate_mag)) - float(numpy.max(reference_mag))))
    correlation = _safe_correlation(numpy, candidate_mag.ravel(), reference_mag.ravel())
    ssim = _global_ssim(numpy, candidate_mag, reference_mag)

    output_diff_magnitude_png.parent.mkdir(parents=True, exist_ok=True)
    output_phase_difference_png.parent.mkdir(parents=True, exist_ok=True)
    output_report_json.parent.mkdir(parents=True, exist_ok=True)

    pyplot.imsave(output_diff_magnitude_png, magnitude_diff, cmap="magma")
    pyplot.imsave(output_phase_difference_png, phase_diff, cmap="twilight", vmin=-math.pi, vmax=math.pi)

    digest = hashlib.sha256(magnitude_diff.tobytes()).hexdigest()
    reference_metadata = _load_optional_json(reference_metadata_json)
    candidate_metadata = _load_optional_json(candidate_metadata_json)

    reference_lineage = _extract_lineage(reference_metadata, reference_npy)
    graphx_lineage = _extract_lineage(candidate_metadata, candidate_npy)
    ordered_set_match = (
        bool(graphx_lineage["ordered_set_checksum"]) and
        graphx_lineage["ordered_set_checksum"] == reference_lineage["ordered_set_checksum"]
    )

    report = {
        "schema": "graphx.sar.image_comparison_report.v3",
        "local_only": True,
        "reference_image": str(reference_npy),
        "candidate_image": str(candidate_npy),
        "difference_magnitude_png": str(output_diff_magnitude_png),
        "phase_difference_png": str(output_phase_difference_png),
        "shape": [int(reference.shape[0]), int(reference.shape[1])],
        "metrics": {
            "rmse_magnitude": rmse,
            "phase_rmse_radians": phase_rmse,
            "peak_error_magnitude": peak_error,
            "magnitude_correlation": correlation,
            "ssim_magnitude": ssim,
        },
        "lineage": {
            "ordered_crsd_inputs": {
                "graphx": graphx_lineage["ordered_crsd_inputs"],
                "reference": reference_lineage["ordered_crsd_inputs"],
            },
            "per_segment_crsd_input_checksums": {
                "graphx": graphx_lineage["per_segment_crsd_input_checksums"],
                "reference": reference_lineage["per_segment_crsd_input_checksums"],
            },
            "ordered_set_checksum": {
                "graphx": graphx_lineage["ordered_set_checksum"],
                "reference": reference_lineage["ordered_set_checksum"],
                "match": ordered_set_match,
            },
            "graphx_output_hash": graphx_lineage["output_hash"],
            "reference_output_hash": reference_lineage["output_hash"],
            "algorithm": {
                "graphx": graphx_lineage["algorithm"],
                "reference": reference_lineage["algorithm"],
            },
            "geometry_assumptions": {
                "graphx": graphx_lineage["geometry_assumptions"],
                "reference": reference_lineage["geometry_assumptions"],
            },
        },
        "difference_magnitude_sha256": digest,
    }
    _write_json(output_report_json, report)
    return report


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SAR image comparison utility")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report optional package availability")
    probe.add_argument("--output-json", required=True)

    compare = subparsers.add_parser("compare", help="Compare two 2D complex image .npy arrays")
    compare.add_argument("--reference-npy", required=True)
    compare.add_argument("--candidate-npy", required=True)
    compare.add_argument("--output-report-json", required=True)
    compare.add_argument("--output-diff-magnitude-png", required=True)
    compare.add_argument("--output-phase-difference-png", required=True)
    compare.add_argument("--reference-metadata-json", required=False)
    compare.add_argument("--candidate-metadata-json", required=False)

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "probe-environment":
        output_json = Path(args.output_json).resolve()
        _write_json(output_json, probe_environment())
        print(output_json)
        return 0

    if args.command_name == "compare":
        try:
            compare_images(
                Path(args.reference_npy).resolve(),
                Path(args.candidate_npy).resolve(),
                Path(args.output_report_json).resolve(),
                Path(args.output_diff_magnitude_png).resolve(),
                Path(args.output_phase_difference_png).resolve(),
                Path(args.reference_metadata_json).resolve() if args.reference_metadata_json else None,
                Path(args.candidate_metadata_json).resolve() if args.candidate_metadata_json else None,
            )
        except Exception as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2
        print(Path(args.output_report_json).resolve())
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
