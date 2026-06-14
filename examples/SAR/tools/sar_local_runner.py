#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from sar_scenario_to_run import (
    build_graphx_config,
    ensure_layout,
    load_json,
    repo_root_from_script,
    scenario_id_from_path,
    validate_scenario_manifest,
    write_json,
    write_text,
)
from gotcha_back_adapter import build_invocation_spec, build_run_script


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare the local-only GOTCHA reproduction layout from a frozen scenario"
    )
    parser.add_argument("--scenario", required=True, help="Path to a scenario manifest JSON file")
    parser.add_argument("--output-dir", required=True, help="Directory where the runner should scaffold artifacts")
    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    script_path = Path(__file__).resolve()
    repo_root = repo_root_from_script(script_path)
    scenario_path = Path(args.scenario).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not scenario_path.exists():
        print(f"error: scenario file not found: {scenario_path}", file=sys.stderr)
        return 2

    scenario = load_json(scenario_path)
    errors = validate_scenario_manifest(scenario)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 3

    manual_template_path = repo_root / "examples" / "SAR" / "config" / "sar_gotcha_external_manual.json"
    manual_template = load_json(manual_template_path)
    graphx_config = build_graphx_config(scenario, manual_template)

    layout = ensure_layout(output_dir)
    manifest_copy_path = layout["manifest"] / scenario_path.name
    shutil.copyfile(scenario_path, manifest_copy_path)

    graphx_config_path = layout["graphx"] / "graphx_config.json"
    write_json(graphx_config_path, graphx_config)

    graphx_script_path = layout["graphx"] / "run_graphx.sh"
    write_text(
        graphx_script_path,
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n\n"
        "export GRAPHX_SAR_ALLOW_EXTERNAL_DATA=1\n"
        "echo 'Set fixture_path in graphx_config.json before running GraphX.'\n"
        "echo './build-ninja/ninja-debug-metal-native/examples/SAR/sar_example "
        + str(graphx_config_path)
        + " ./build-ninja/ninja-debug-metal-native/examples/SAR/plugins'\n",
    )
    graphx_script_path.chmod(0o755)

    reference_invocation = build_invocation_spec(scenario_path, scenario, layout["reference"])
    reference_invocation_path = layout["reference"] / "gotcha_back_invocation.json"
    write_json(reference_invocation_path, reference_invocation)

    reference_contract_path = layout["reference"] / "reference_output_contract.json"
    write_json(reference_contract_path, reference_invocation["expected_output"])

    reference_script_path = layout["reference"] / "run_gotcha_back.sh"
    write_text(
        reference_script_path,
        build_run_script(reference_invocation, reference_invocation_path, scenario_path),
    )
    reference_script_path.chmod(0o755)

    orchestration_plan_path = layout["reports"] / "orchestration_plan.json"
    write_json(
        orchestration_plan_path,
        {
            "layout_version": 1,
            "scenario_id": scenario_id_from_path(scenario_path),
            "status": "prepared",
            "requires_external_data": False,
            "requires_external_reference_binary": False,
            "graphx": {
                "config": str(graphx_config_path),
                "command": str(graphx_script_path),
            },
            "reference": {
                "command": str(reference_script_path),
                "invocation": str(reference_invocation_path),
                "output_contract": str(reference_contract_path),
            },
            "notes": [
                "The local runner prepares boundaries only and does not execute GraphX or gotcha-back.",
                "Populate fixture_path in graphx_config.json before manual local runs.",
            ],
        },
    )

    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
