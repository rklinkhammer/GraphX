#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from sar_scenario_to_run import load_json, scenario_id_from_path, write_json, write_text


PINNED_GOTCHA_BACK_PROFILES: dict[str, dict[str, Any]] = {
    "scenario_001": {
        "pass": 1,
        "first_az": 38,
        "last_az": 41,
        "gotcha_subdir": "GOTCHA-CP_Disc1",
        "executable_hint": "build-release/sarbp",
    }
}


def pinned_profile_for_scenario(scenario_id: str) -> dict[str, Any]:
    if scenario_id not in PINNED_GOTCHA_BACK_PROFILES:
        raise KeyError(f"no pinned gotcha-back profile for scenario: {scenario_id}")
    return PINNED_GOTCHA_BACK_PROFILES[scenario_id]


def build_invocation_spec(scenario_path: Path, scenario: dict[str, Any], reference_dir: Path) -> dict[str, Any]:
    scenario_id = scenario_id_from_path(scenario_path)
    profile = pinned_profile_for_scenario(scenario_id)
    raw_output_path = reference_dir / "gotcha_back_image.bin"
    normalized_output_path = reference_dir / "normalized_reference.json"

    image_grid = scenario["image_grid"]
    output = scenario["output"]
    command = [
        '${GOTCHA_BACK_BIN:-./build-release/sarbp}',
        '--pass', str(profile['pass']),
        '--first-az', str(profile['first_az']),
        '--last-az', str(profile['last_az']),
        '--output-file', str(raw_output_path),
        '${GOTCHA_DIR}',
    ]

    return {
        "tool": "gotcha-back",
        "scenario_id": scenario_id,
        "dataset": scenario["dataset"],
        "pinned_profile": profile,
        "command": command,
        "expected_output": {
            "source_tool": "gotcha-back",
            "provenance_class": "external_baseline",
            "scenario_id": scenario_id,
            "raw_path": str(raw_output_path),
            "normalized_path": str(normalized_output_path),
            "width": image_grid["width"],
            "height": image_grid["height"],
            "format": output["format"],
            "layout": output["layout"],
            "artifact_kind": output["artifact_kind"],
            "dtype": "float32",
        },
    }


def build_run_script(invocation_spec: dict[str, Any], output_json_path: Path, scenario_path: Path) -> str:
    command = " \\\n  ".join(invocation_spec["command"])
    return (
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n\n"
        "if [[ -z \"${GOTCHA_DIR:-}\" ]]; then\n"
        "  echo 'Set GOTCHA_DIR to the unpacked GOTCHA dataset directory.' >&2\n"
        "  exit 2\n"
        "fi\n"
        "if [[ -z \"${GOTCHA_BACK_BIN:-}\" ]]; then\n"
        "  echo 'Set GOTCHA_BACK_BIN to the gotcha-back sarbp executable (default shown below).' >&2\n"
        "fi\n\n"
        f"# Scenario: {scenario_id_from_path(scenario_path)}\n"
        f"# Invocation spec: {output_json_path}\n"
        f"{command}\n"
    )


def normalize_output(raw_path: Path, scenario_path: Path, output_json_path: Path) -> int:
    scenario = load_json(scenario_path)
    image_grid = scenario["image_grid"]
    output = scenario["output"]
    width = int(image_grid["width"])
    height = int(image_grid["height"])
    expected_bytes = width * height * 4

    if not raw_path.exists():
        print(f"error: raw gotcha-back output not found: {raw_path}", file=sys.stderr)
        return 2

    byte_count = raw_path.stat().st_size
    if byte_count != expected_bytes:
        print(
            f"error: raw output size mismatch, expected {expected_bytes} bytes but found {byte_count}",
            file=sys.stderr,
        )
        return 3

    normalized = {
        "format_version": 1,
        "source_tool": "gotcha-back",
        "provenance_class": "external_baseline",
        "scenario_id": scenario_id_from_path(scenario_path),
        "format": output["format"],
        "layout": output["layout"],
        "artifact_kind": output["artifact_kind"],
        "width": width,
        "height": height,
        "dtype": "float32",
        "byte_count": byte_count,
        "raw_path": str(raw_path),
    }
    write_json(output_json_path, normalized)
    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="gotcha-back scenario adapter")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    scaffold = subparsers.add_parser("scaffold-reference", help="Write pinned gotcha-back invocation artifacts")
    scaffold.add_argument("--scenario", required=True)
    scaffold.add_argument("--reference-dir", required=True)

    normalize = subparsers.add_parser("normalize-output", help="Normalize gotcha-back raw output to comparison format")
    normalize.add_argument("--scenario", required=True)
    normalize.add_argument("--input-raw", required=True)
    normalize.add_argument("--output-json", required=True)
    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "scaffold-reference":
        scenario_path = Path(args.scenario).resolve()
        reference_dir = Path(args.reference_dir).resolve()
        reference_dir.mkdir(parents=True, exist_ok=True)
        scenario = load_json(scenario_path)
        invocation_spec = build_invocation_spec(scenario_path, scenario, reference_dir)
        invocation_path = reference_dir / "gotcha_back_invocation.json"
        write_json(invocation_path, invocation_spec)
        run_script_path = reference_dir / "run_gotcha_back.sh"
        write_text(run_script_path, build_run_script(invocation_spec, invocation_path, scenario_path))
        run_script_path.chmod(0o755)
        contract_path = reference_dir / "reference_output_contract.json"
        write_json(contract_path, invocation_spec["expected_output"])
        print(reference_dir)
        return 0

    if args.command_name == "normalize-output":
        return normalize_output(
            Path(args.input_raw).resolve(),
            Path(args.scenario).resolve(),
            Path(args.output_json).resolve(),
        )

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
