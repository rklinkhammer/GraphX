#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any

from sar_graphx_vs_baseline_harness import compare_with_settings


REPORT_SCHEMA = "graphx.sar.baseline_substitution_experiment.v1"
OPT_IN_ENV = "GRAPHX_SAR_BASELINE_SUBSTITUTION_ENABLE"


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as out:
        json.dump(value, out, indent=2)
        out.write("\n")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def run_substitution(
    graphx_stage_contract: Path,
    baseline_reference_contract: Path,
    *,
    strict: bool,
    thresholds_json: Path | None,
) -> tuple[int, dict[str, Any]]:
    common = {
        "schema": REPORT_SCHEMA,
        "mode": "local_stage_substitution",
        "local_only": True,
        "ci_safe": False,
        "selected_baseline": "SarPy",
        "substituted_stage": "image_formation",
        "graphx_replacement": "CrsdFocusedImageTransformNode",
        "canonical_sar_gpu_path_changed": False,
        "production_sar_claim": False,
    }

    if os.getenv(OPT_IN_ENV) != "1":
        return 0, {
            **common,
            "status": "skipped",
            "reason": "local_opt_in_not_enabled",
            "opt_in_env": OPT_IN_ENV,
        }

    if not graphx_stage_contract.exists() or not baseline_reference_contract.exists():
        return 0, {
            **common,
            "status": "skipped",
            "reason": "missing_contract_inputs",
            "graphx_stage_contract": str(graphx_stage_contract),
            "baseline_reference_contract": str(baseline_reference_contract),
        }

    graphx_contract = load_json(graphx_stage_contract)
    baseline_contract = load_json(baseline_reference_contract)
    if graphx_contract.get("source_tool") != "graphx":
        return 2, {
            **common,
            "status": "invalid",
            "reason": "graphx_stage_contract_must_be_graphx_output",
        }
    if baseline_contract.get("provenance_class") != "external_baseline":
        return 2, {
            **common,
            "status": "invalid",
            "reason": "baseline_reference_must_be_external_baseline",
        }

    comparison = compare_with_settings(
        graphx_stage_contract,
        baseline_reference_contract,
        strict=strict,
        thresholds_json=thresholds_json,
    )
    passed = bool(comparison.get("passed", False))
    return (0 if passed else 1), {
        **common,
        "status": "pass" if passed else "fail",
        "graphx_stage_contract": str(graphx_stage_contract),
        "baseline_reference_contract": str(baseline_reference_contract),
        "comparison_report": comparison,
        "notes": [
            "The experiment substitutes GraphX image formation output at a contract boundary.",
            "It does not modify SarPy, GraphX runtime architecture, or the canonical SAR GPU path.",
            "Comparison metrics are validation aids and not production SAR claims.",
        ],
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Local-only GraphX SAR baseline stage substitution experiment"
    )
    parser.add_argument("run-local-substitution", nargs="?")
    parser.add_argument("--graphx-stage-contract", required=True)
    parser.add_argument("--baseline-reference-contract", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--thresholds-json")
    parser.add_argument("--strict", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    exit_code, result = run_substitution(
        Path(args.graphx_stage_contract).resolve(),
        Path(args.baseline_reference_contract).resolve(),
        strict=args.strict,
        thresholds_json=(
            Path(args.thresholds_json).resolve() if args.thresholds_json else None
        ),
    )
    output = Path(args.output_json).resolve()
    write_json(output, result)
    print(output)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
