#!/usr/bin/env python3
"""Gate FHSS Phase 2 using raw output from the compiled C++ candidates."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import subprocess
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / "libdsp/config/fhss_phase2_validation_profile_v1.json"
REPORT = ROOT / "libdsp/config/fhss_phase2_characterization_report_v1.json"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_threshold_set(profile: dict[str, Any]) -> dict[str, Any]:
    """Return the frozen inputs used to select or judge the operating point."""
    channelizer = profile["channelizer"]
    detector = profile["detector"]
    return {
        "profile_version": profile["version"],
        "sampled_band_model": profile["sampled_band_model"],
        "channelizer": {
            key: channelizer[key]
            for key in (
                "algorithm",
                "channel_centers_hz",
                "occupied_bandwidth_hz",
                "passband_edge_hz",
                "cutoff_frequency_hz",
                "transition_band_end_hz",
                "decimation_factor",
                "fir_tap_count",
                "group_delay_input_samples",
                "requirements",
            )
        },
        "detector": {
            key: detector[key]
            for key in (
                "noise_power_quantile",
                "threshold_above_noise_linear",
                "release_threshold_ratio",
                "candidate_min_absolute_power_linear",
                "minimum_symbol_coherence",
                "smoothing_window_channel_samples",
                "min_pulse_input_samples",
                "max_pulse_input_samples",
                "bridge_gap_input_samples",
                "duplicate_tolerance_input_samples",
                "nominal_bandwidth_hz",
                "supported_cfo_hz",
                "supported_amplitude_linear",
                "evaluation_snr_db",
                "requirements",
            )
        },
        "development_seeds": profile["statistics"]["development_seeds"],
        "threshold_provenance": profile["threshold_provenance"],
    }


def threshold_set_sha256(profile: dict[str, Any]) -> str:
    encoded = json.dumps(
        canonical_threshold_set(profile), sort_keys=True, separators=(",", ":")
    ).encode()
    return hashlib.sha256(encoded).hexdigest()


def wilson(successes: int, trials: int) -> list[float]:
    if trials <= 0 or successes < 0 or successes > trials:
        raise ValueError("invalid binomial counts")
    z = 1.959963984540054
    p = successes / trials
    denominator = 1.0 + z * z / trials
    center = (p + z * z / (2.0 * trials)) / denominator
    radius = z * math.sqrt(
        (p * (1.0 - p) + z * z / (4.0 * trials)) / trials
    ) / denominator
    return [max(0.0, center - radius), min(1.0, center + radius)]


def find_characterizer(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("GRAPHX_FHSS_PHASE2_CHARACTERIZER"):
        candidates.append(Path(os.environ["GRAPHX_FHSS_PHASE2_CHARACTERIZER"]))
    candidates.extend(
        [
            ROOT
            / "build-ninja/ninja-debug/examples/DSP/graphx-dsp-fhss-phase2-characterize",
            ROOT / "build/examples/DSP/graphx-dsp-fhss-phase2-characterize",
        ]
    )
    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_file() and os.access(resolved, os.X_OK):
            return resolved
    raise SystemExit(
        "compiled graphx-dsp-fhss-phase2-characterize not found; build the "
        "dsp_fhss_phase2_characterize target or pass --characterizer"
    )


def run_characterizer(
    executable: Path, trials: int, partition: str
) -> tuple[dict[str, Any], float]:
    command = [
        str(executable),
        "--profile",
        str(PROFILE),
        "--trials",
        str(trials),
        "--seed-partition",
        partition,
    ]
    started = time.perf_counter()
    process = subprocess.run(command, text=True, capture_output=True, check=False)
    elapsed = time.perf_counter() - started
    if process.returncode != 0:
        raise SystemExit(
            f"candidate characterizer failed ({process.returncode}): "
            f"{process.stderr.strip()}"
        )
    try:
        raw = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise SystemExit(f"candidate characterizer emitted invalid JSON: {error}") from error
    if raw.get("schema") != "graphx.fhss.phase2-raw-candidate-measurements":
        raise SystemExit("candidate characterizer emitted an unsupported schema")
    return raw, elapsed


def upper_gate(measured: float, limit: float, units: str) -> dict[str, Any]:
    return {
        "expected": {"comparison": "<=", "limit": limit, "units": units},
        "measured": measured,
        "delta_to_limit": limit - measured,
        "pass": measured <= limit,
    }


def lower_gate(measured: float, limit: float, units: str) -> dict[str, Any]:
    return {
        "expected": {"comparison": ">=", "limit": limit, "units": units},
        "measured": measured,
        "delta_to_limit": measured - limit,
        "pass": measured >= limit,
    }


def characterize(
    trials: int, partition: str, executable: Path
) -> dict[str, Any]:
    profile = json.loads(PROFILE.read_text())
    expected_threshold_hash = profile["threshold_freeze"]["threshold_set_sha256"]
    measured_threshold_hash = threshold_set_sha256(profile)
    if measured_threshold_hash != expected_threshold_hash:
        raise SystemExit(
            "frozen threshold set changed: profile records "
            f"{expected_threshold_hash}, calculated {measured_threshold_hash}"
        )
    raw, wall_seconds = run_characterizer(executable, trials, partition)
    if raw["seed_partition"] != partition or raw["trials_per_snr_point"] != trials:
        raise SystemExit("candidate characterizer did not honor trial arguments")

    channel = raw["channelizer"]
    detector = raw["detector"]
    channel_requirements = profile["channelizer"]["requirements"]
    detector_requirements = profile["detector"]["requirements"]
    expected_delay = profile["channelizer"]["group_delay_input_samples"]
    delay_error = abs(channel["measured_group_delay_input_samples"] - expected_delay)

    roc_by_snr = {float(point["snr_db"]): point for point in detector["roc"]}
    ten_db = roc_by_snr[10.0]
    pd_interval = wilson(ten_db["matched_events"], ten_db["truth_events"])
    pd_estimate = ten_db["matched_events"] / ten_db["truth_events"]
    false_interval = wilson(
        detector["false_detections"], detector["searched_channel_samples"]
    )
    false_rate = (
        detector["false_detections"] / detector["searched_channel_samples"]
    )
    duplicate_rate = (
        detector["duplicate_excess_detections"]
        / detector["same_channel_interferer_trials"]
    )

    acceptance: dict[str, dict[str, Any]] = {
        "channelizer_passband_ripple": upper_gate(
            channel["passband_ripple_db"],
            channel_requirements["maximum_passband_ripple_db"],
            "dB",
        ),
        "channelizer_stopband_attenuation": lower_gate(
            channel["stopband_attenuation_db"],
            channel_requirements["minimum_stopband_attenuation_db"],
            "dB",
        ),
        "channelizer_adjacent_rejection": lower_gate(
            channel["adjacent_channel_rejection_db"],
            channel_requirements["minimum_adjacent_channel_rejection_db"],
            "dB",
        ),
        "channelizer_alternate_rejection": lower_gate(
            channel["alternate_channel_rejection_db"],
            channel_requirements["minimum_alternate_channel_rejection_db"],
            "dB",
        ),
        "channelizer_alias_power": upper_gate(
            channel["alias_power_dbc"],
            channel_requirements["maximum_alias_power_dbc"],
            "dBc",
        ),
        "channelizer_integrated_alias_power": upper_gate(
            channel["integrated_alias_power_dbc"],
            channel_requirements["maximum_integrated_alias_power_dbc"],
            "dBc across all measured folding-band tones",
        ),
        "channelizer_alias_oracle_error": upper_gate(
            channel["maximum_alias_candidate_oracle_complex_error"],
            channel_requirements["maximum_alias_oracle_complex_error"],
            "complex magnitude",
        ),
        "channelizer_transition_width": upper_gate(
            channel["measured_transition_width_hz"],
            channel_requirements["maximum_transition_width_hz"],
            "Hz",
        ),
        "channelizer_enbw_minimum": lower_gate(
            channel["measured_equivalent_noise_bandwidth_hz"],
            channel_requirements["minimum_equivalent_noise_bandwidth_hz"],
            "Hz",
        ),
        "channelizer_enbw_maximum": upper_gate(
            channel["measured_equivalent_noise_bandwidth_hz"],
            channel_requirements["maximum_equivalent_noise_bandwidth_hz"],
            "Hz",
        ),
        "channelizer_runtime_allocation_high_water": upper_gate(
            channel["production_node_runtime_allocation_high_water_bytes"],
            channel_requirements["maximum_runtime_allocation_high_water_bytes"],
            "bytes observed by deterministic candidate allocation counter",
        ),
        "channelizer_group_delay_error": upper_gate(
            delay_error,
            channel_requirements["maximum_group_delay_error_input_samples"],
            "input samples",
        ),
        "channelizer_near_far_ratio": lower_gate(
            channel["maximum_supported_near_far_ratio_db"],
            channel_requirements["minimum_supported_near_far_ratio_db"],
            "dB",
        ),
        "channelizer_packetized_one_shot_error": upper_gate(
            channel["packetized_one_shot_max_error"],
            channel_requirements["maximum_packetized_one_shot_error"],
            "complex magnitude",
        ),
        "detector_probability_at_10_db": lower_gate(
            pd_interval[0],
            detector_requirements["minimum_probability_of_detection_at_10_db"],
            "Wilson 95% lower bound",
        ),
        "detector_false_alarm_rate": upper_gate(
            false_interval[1],
            detector_requirements["maximum_false_alarms_per_searched_sample"],
            "Wilson 95% upper bound per searched channel sample",
        ),
        "detector_timing_error_p95": upper_gate(
            detector["timing_error_input_samples_p95"],
            detector_requirements["maximum_timing_error_input_samples_p95"],
            "input samples",
        ),
        "detector_duplicate_detections": upper_gate(
            duplicate_rate,
            detector_requirements[
                "maximum_duplicate_detections_per_truth_pulse"
            ],
            "excess detections per same-channel overlap trial",
        ),
        "detector_runtime_allocation_high_water": upper_gate(
            detector["node_runtime_allocation_high_water_bytes"],
            detector_requirements["maximum_runtime_allocation_high_water_bytes"],
            "bytes observed by deterministic candidate allocation counter",
        ),
    }
    terminal_contract = detector["terminal_contract"]
    acceptance["detector_terminal_and_reset_contract"] = {
        "expected": {"comparison": "all_true", "limit": True},
        "measured": terminal_contract,
        "delta_to_limit": None,
        "pass": all(terminal_contract.values()),
    }
    confusion = detector["frequency_index_confusion_matrix"]
    diagonal = sum(confusion[index][index] for index in range(64))
    total_confusion = sum(sum(row) for row in confusion)
    acceptance["detector_frequency_identity"] = {
        "expected": {"comparison": "==", "limit": 64, "units": "correct nodes"},
        "measured": {"correct": diagonal, "total": total_confusion},
        "delta_to_limit": diagonal - 64,
        "pass": diagonal == total_confusion == 64,
    }

    executable_display = str(executable)
    try:
        executable_display = str(executable.relative_to(ROOT))
    except ValueError:
        pass
    result = {
        "schema": "graphx.fhss.phase2-characterization-report",
        "version": 2,
        "claim_level": profile["claim_level"],
        "profile_sha256": sha256(PROFILE),
        "threshold_set_sha256": measured_threshold_hash,
        "threshold_freeze_status": profile["threshold_freeze"]["status"],
        "threshold_provenance": profile["threshold_provenance"],
        "implementation_state": (
            "working-tree characterization; record git revision in release artifact"
        ),
        "compiler_language_mode": "C++26 (-std=c++2c)",
        "seed_partition": partition,
        "seeds": profile["statistics"][f"{partition}_seeds"],
        "trials_per_snr_point": trials,
        "candidate_measurement_provenance": {
            "executable": executable_display,
            "executable_sha256": sha256(executable),
            "measurement_source": (
                "compiled C++ candidate kernels and receiver nodes; Python only "
                "orchestrates execution, confidence intervals, and gates"
            ),
            "characterizer_wall_seconds": wall_seconds,
        },
        "raw_candidate_measurements": raw,
        "derived_statistics": {
            "probability_detection_at_10_db": pd_estimate,
            "probability_detection_at_10_db_wilson95": pd_interval,
            "false_detections_per_searched_sample": false_rate,
            "false_detection_probability_wilson95": false_interval,
            "group_delay_error_input_samples": delay_error,
            "duplicate_excess_rate": duplicate_rate,
        },
        "declared_resource_bounds": {
            "channelizer_candidate_max_input_samples_per_token": profile[
                "channelizer"
            ]["candidate_max_input_samples_per_token"],
            "detector_candidate_max_buffered_channel_samples": profile["detector"][
                "candidate_max_buffered_channel_samples"
            ],
            "detector_integration_override_max_buffered_channel_samples": profile[
                "detector"
            ]["integration_max_buffered_channel_samples"],
            "scope": "configuration bounds, not process-memory measurements",
        },
        "acceptance_results": acceptance,
        "all_provisional_targets_pass": all(item["pass"] for item in acceptance.values()),
        "known_limitations": profile["unsupported"],
    }
    return result


def without_runtime(value: Any) -> Any:
    """Runtime is measured but intentionally excluded from stale-report equality."""
    ignored = {
        "runtime_seconds",
        "mean_runtime_microseconds_per_capture",
        "characterizer_wall_seconds",
    }
    if isinstance(value, dict):
        return {
            key: without_runtime(item)
            for key, item in value.items()
            if key not in ignored
        }
    if isinstance(value, list):
        return [without_runtime(item) for item in value]
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trials", type=int, default=32)
    parser.add_argument(
        "--seed-partition",
        choices=("development", "evaluation"),
        default="evaluation",
    )
    parser.add_argument("--characterizer")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--print-threshold-hash", action="store_true")
    args = parser.parse_args()
    profile = json.loads(PROFILE.read_text())
    if args.print_threshold_hash:
        print(threshold_set_sha256(profile))
        return 0
    if args.trials <= 0:
        parser.error("--trials must be positive")
    executable = find_characterizer(args.characterizer)
    generated = characterize(args.trials, args.seed_partition, executable)
    rendered = json.dumps(generated, indent=2, sort_keys=True) + "\n"
    if args.write:
        REPORT.write_text(rendered)
    if args.verify:
        if not REPORT.exists():
            raise SystemExit("Phase 2 report is stale; regenerate with --write")
        checked = json.loads(REPORT.read_text())
        if without_runtime(checked) != without_runtime(generated):
            raise SystemExit("Phase 2 report is stale; regenerate with --write")
    if not args.write and not args.verify:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
