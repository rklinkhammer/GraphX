#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


DEFAULT_VALIDATE_TOOL = Path("tools/sarpy/validate_crsd.py")


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as out:
        json.dump(value, out, indent=2)
        out.write("\n")


def sarpy_installed() -> bool:
    return importlib.util.find_spec("sarpy") is not None


def probe_environment() -> dict[str, Any]:
    enabled = os.getenv("GRAPHX_SAR_BASELINE_RUNNER_ENABLE") == "1"
    crsd_path = os.getenv("GRAPHX_SARPY_CRSD_FILE", "")

    return {
        "schema": "graphx.sar.local_baseline_runner.probe.v1",
        "selected_baseline": "SarPy",
        "local_only": True,
        "ci_safe": False,
        "requires_opt_in_env": True,
        "opt_in_env": "GRAPHX_SAR_BASELINE_RUNNER_ENABLE",
        "dataset_env": "GRAPHX_SARPY_CRSD_FILE",
        "enabled": enabled,
        "packages": {
            "sarpy": {
                "installed": sarpy_installed(),
            }
        },
        "dataset": {
            "crsd_path": crsd_path,
            "provided": bool(crsd_path),
        },
        "notes": [
            "Local-only baseline runner. Not a GraphX runtime dependency.",
            "Default CI must not require external baseline packages or datasets.",
        ],
    }


def local_smoke_result(status: str, reason: str, details: dict[str, Any] | None = None) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "schema": "graphx.sar.local_baseline_runner.smoke.v1",
        "selected_baseline": "SarPy",
        "local_only": True,
        "ci_safe": False,
        "status": status,
        "reason": reason,
    }
    if details is not None:
        payload["details"] = details
    return payload


def run_local_smoke(validate_tool: Path) -> tuple[int, dict[str, Any]]:
    if os.getenv("GRAPHX_SAR_BASELINE_RUNNER_ENABLE") != "1":
        return 0, local_smoke_result(
            "skipped",
            "local_opt_in_not_enabled",
            {
                "opt_in_env": "GRAPHX_SAR_BASELINE_RUNNER_ENABLE",
                "expected_value": "1",
            },
        )

    if not sarpy_installed():
        return 0, local_smoke_result("skipped", "sarpy_not_installed")

    crsd_path_value = os.getenv("GRAPHX_SARPY_CRSD_FILE", "")
    if not crsd_path_value:
        return 0, local_smoke_result(
            "skipped",
            "crsd_path_not_set",
            {"dataset_env": "GRAPHX_SARPY_CRSD_FILE"},
        )

    crsd_path = Path(crsd_path_value)
    if not crsd_path.exists():
        return 0, local_smoke_result(
            "skipped",
            "crsd_path_missing",
            {"crsd_path": str(crsd_path)},
        )

    if not validate_tool.exists():
        return 0, local_smoke_result(
            "skipped",
            "validate_tool_missing",
            {"validate_tool": str(validate_tool)},
        )

    temp_dir = Path(os.getenv("TMPDIR", "/tmp")) / "graphx_sar_baseline_runner"
    temp_dir.mkdir(parents=True, exist_ok=True)
    report_path = temp_dir / "sarpy_validate_report.json"

    command = [
        sys.executable,
        str(validate_tool),
        "validate",
        "--input-crsd",
        str(crsd_path),
        "--output-json",
        str(report_path),
    ]
    result = subprocess.run(command, capture_output=True, text=True)

    if result.returncode != 0:
        return 0, local_smoke_result(
            "skipped",
            "validate_failed",
            {
                "return_code": result.returncode,
                "stderr": result.stderr.strip(),
                "stdout": result.stdout.strip(),
            },
        )

    return 0, local_smoke_result(
        "ran",
        "ok",
        {
            "validate_report": str(report_path),
            "validate_tool": str(validate_tool),
        },
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SAR baseline runner")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report local baseline runner environment status")
    probe.add_argument("--output-json", required=True)

    smoke = subparsers.add_parser(
        "run-local-smoke",
        help="Run local-only baseline smoke path with CI-safe skip behavior",
    )
    smoke.add_argument("--output-json", required=True)
    smoke.add_argument("--validate-tool", default=str(DEFAULT_VALIDATE_TOOL))

    return parser


def main() -> int:
    args = build_parser().parse_args()
    output_path = Path(args.output_json).resolve()

    if args.command_name == "probe-environment":
        write_json(output_path, probe_environment())
        print(output_path)
        return 0

    if args.command_name == "run-local-smoke":
        exit_code, payload = run_local_smoke(Path(args.validate_tool).resolve())
        write_json(output_path, payload)
        print(output_path)
        return exit_code

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
