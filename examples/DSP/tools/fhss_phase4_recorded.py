#!/usr/bin/env python3
"""Governed recorded-IQ ingestion and truth-isolated FHSS replay.

This is infrastructure, not evidence that hardware validation happened.  The
tool never generates IQ and deliberately refuses a held-out run unless an
external, hash-bound corpus and frozen profile are supplied.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import struct
import subprocess
import tempfile
import time
from pathlib import Path
from statistics import NormalDist
from typing import Any

VERSION = "graphx.fhss.phase4-recorded.v1"
FORMATS = {"cf32_le": ("<ff", 8), "cf32_be": (">ff", 8),
           "cf64_le": ("<dd", 16), "cf64_be": (">dd", 16)}
CAPTURE_CLASSES = {"conducted", "channel_emulator", "ota", "negative_control", "synthetic_dry_run"}
PHYSICAL_CAPTURE_CLASSES = {"conducted", "channel_emulator", "ota", "negative_control"}
PARTITIONS = {"development", "validation", "held_out"}
FORBIDDEN_KEYS = {"messages", "transmitter_events", "expected_words", "truth_manifest",
                  "waveform_generator", "transmitted_frequency_indices",
                  "hidden_burst_epochs", "generator_truth"}
FORBIDDEN_VALUES = {"FHSSSyntheticIqSourceNode"}


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode()


def digest_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def digest_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.hexdigest()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(), parse_constant=lambda x: (_ for _ in ()).throw(
        ValueError(f"non-finite JSON number {x}")))


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
            stream.flush(); os.fsync(stream.fileno())
        os.replace(name, path)
    except BaseException:
        try: os.unlink(name)
        except FileNotFoundError: pass
        raise


def exact(obj: Any, required: set[str], optional: set[str], where: str) -> None:
    if not isinstance(obj, dict):
        raise ValueError(f"{where} must be an object")
    missing, unknown = required - obj.keys(), obj.keys() - required - optional
    if missing: raise ValueError(f"{where} missing {sorted(missing)}")
    if unknown: raise ValueError(f"{where} has unknown properties {sorted(unknown)}")


def finite(value: Any, where: str, low: float | None = None,
           high: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{where} must be numeric")
    value = float(value)
    if not math.isfinite(value): raise ValueError(f"{where} must be finite")
    if low is not None and value < low: raise ValueError(f"{where} below minimum")
    if high is not None and value > high: raise ValueError(f"{where} above maximum")
    return value


def identifier(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 128 or any(
            c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for c in value):
        raise ValueError(f"{where} is not a stable identifier")
    return value


def safe_relative(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value or Path(value).is_absolute() or ".." in Path(value).parts:
        raise ValueError(f"{where} must be a safe non-empty relative path")
    return value


def validate_profile(profile: Any) -> None:
    exact(profile, {"schema", "version", "status", "claim_level", "extends_phase3",
                    "waveform", "capture_program", "statistics", "replay",
                    "correlation", "partitions", "freeze", "limitations"}, set(), "profile")
    if profile["schema"] != "graphx.fhss.phase4-validation-profile.v1" or profile["version"] != 1:
        raise ValueError("unsupported Phase 4 profile")
    if profile["status"] not in {"synthetic_only_infrastructure_physical_validation_unavailable", "frozen_for_heldout"}:
        raise ValueError("invalid profile status")
    exact(profile["extends_phase3"], {"profile_path", "profile_sha256", "raw_sha256",
          "report_sha256", "freeze_manifest_sha256", "inventory_sha256"}, set(), "extends_phase3")
    for key, value in profile["extends_phase3"].items():
        if key.endswith("sha256") and (not isinstance(value, str) or len(value) != 64):
            raise ValueError(f"invalid {key}")
    exact(profile["waveform"], {"sample_rate_hz", "bit_rate_hz", "sample_formats",
          "canonical_replay_format", "architecture"}, set(), "waveform")
    finite(profile["waveform"]["sample_rate_hz"], "sample rate", 1.0)
    finite(profile["waveform"]["bit_rate_hz"], "bit rate", 1.0)
    if set(profile["waveform"]["sample_formats"]) - FORMATS.keys():
        raise ValueError("unsupported sample format in profile")
    if profile["waveform"]["canonical_replay_format"] not in profile["waveform"]["sample_formats"]:
        raise ValueError("canonical replay format is not supported")
    exact(profile["capture_program"], {"required_classes", "minimum_independent_sessions_per_class",
          "minimum_captures_per_class", "minimum_searched_seconds_per_class", "conducted",
          "channel_emulator", "ota", "maximum_transmitter_messages_per_capture"}, set(), "capture_program")
    if profile["capture_program"]["maximum_transmitter_messages_per_capture"] != 1:
        raise ValueError("current receiver evidence sink supports at most one message per capture")
    for key in ("minimum_independent_sessions_per_class", "minimum_captures_per_class"):
        value = profile["capture_program"][key]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ValueError(f"invalid {key}")
    finite(profile["capture_program"]["minimum_searched_seconds_per_class"], "minimum exposure", 1e-12)
    if set(profile["capture_program"]["required_classes"]) != {"conducted", "channel_emulator", "ota"}:
        raise ValueError("all three physical capture classes are required")
    conducted = profile["capture_program"]["conducted"]
    exact(conducted, {"wanted_power_dbm_range", "attenuation_db_range", "required_cases"}, set(), "conducted program")
    for key in ("wanted_power_dbm_range", "attenuation_db_range"):
        bounds = conducted[key]
        if not isinstance(bounds, list) or len(bounds) != 2 or finite(bounds[0], key) > finite(bounds[1], key):
            raise ValueError(f"invalid {key}")
    exact(profile["capture_program"]["channel_emulator"], {"required_cases"}, set(), "emulator program")
    exact(profile["capture_program"]["ota"], {"required_cases", "authorization_required",
          "preferred_order"}, set(), "OTA program")
    if profile["capture_program"]["ota"]["authorization_required"] is not True:
        raise ValueError("OTA authorization must be required")
    for capture_class in ("conducted", "channel_emulator", "ota"):
        cases = profile["capture_program"][capture_class]["required_cases"]
        if not isinstance(cases, list) or not cases or len(set(cases)) != len(cases) or not all(isinstance(x, str) and x for x in cases):
            raise ValueError(f"invalid {capture_class} scenario inventory")
    exact(profile["statistics"], {"confidence_method", "confidence_level", "cluster_unit",
          "metrics", "failed_run_policy", "matching", "thresholds"}, set(), "statistics")
    confidence = finite(profile["statistics"]["confidence_level"], "confidence level", .5, .999999)
    if profile["statistics"]["confidence_method"] != "session-clustered deterministic percentile bootstrap enveloped by Wilson score bounds":
        raise ValueError("unsupported confidence method")
    if not isinstance(profile["statistics"]["metrics"], list) or len(set(profile["statistics"]["metrics"])) != len(profile["statistics"]["metrics"]):
        raise ValueError("metrics must be a unique array")
    exact(profile["statistics"]["matching"], {"method", "nominal_tolerance_samples",
          "timing_uncertainty_policy", "duplicate_policy"}, set(), "matching")
    tolerance = profile["statistics"]["matching"]["nominal_tolerance_samples"]
    if isinstance(tolerance, bool) or not isinstance(tolerance, int) or not 0 <= tolerance <= 1_000_000:
        raise ValueError("invalid matching tolerance")
    thresholds = profile["statistics"]["thresholds"]
    exact(thresholds, {"status", "origin", "threshold_set_sha256",
          "detection_probability_lower_bound", "conditional_ber_upper_bound",
          "message_per_upper_bound", "false_alarms_per_second_upper_bound"}, set(), "thresholds")
    for key in ("detection_probability_lower_bound", "conditional_ber_upper_bound", "message_per_upper_bound"):
        finite(profile["statistics"]["thresholds"][key], key, 0.0, 1.0)
    finite(profile["statistics"]["thresholds"]["false_alarms_per_second_upper_bound"], "FAR threshold", 0.0)
    threshold_contract = {key: thresholds[key] for key in ("origin", "detection_probability_lower_bound",
        "conditional_ber_upper_bound", "message_per_upper_bound", "false_alarms_per_second_upper_bound")}
    if profile["statistics"]["cluster_unit"] != "capture_session":
        raise ValueError("hardware trials must be clustered by capture session")
    if profile["statistics"]["failed_run_policy"] != "retain_and_fail_no_retry_or_substitution":
        raise ValueError("unsafe failed-run policy")
    exact(profile["replay"], {"receiver_graph", "receiver_graph_sha256", "allowed_patches",
          "forbidden_receiver_fields", "executor_timeout_seconds", "process_timeout_seconds",
          "ci_tier", "offline_tier"}, set(), "replay")
    if profile["replay"]["allowed_patches"] != ["binary_iq_file_path", "receiver_output_paths"]:
        raise ValueError("replay patch allowlist changed")
    if not FORBIDDEN_KEYS.issubset(set(profile["replay"]["forbidden_receiver_fields"])):
        raise ValueError("replay forbidden-field list incomplete")
    for key in ("executor_timeout_seconds", "process_timeout_seconds"):
        value = profile["replay"][key]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ValueError(f"invalid {key}")
    if profile["replay"]["process_timeout_seconds"] <= profile["replay"]["executor_timeout_seconds"]:
        raise ValueError("process timeout must exceed executor timeout")
    exact(profile["correlation"], {"paired_metrics", "agreement_criteria",
          "reference_plane_policy", "uncertainty_policy"}, set(), "correlation")
    criteria = profile["correlation"]["agreement_criteria"]
    exact(criteria, {"status", "decision_rule", "metrics"}, set(), "agreement criteria")
    if criteria["status"] not in {"provisional_not_frozen", "frozen_before_held_out"}:
        raise ValueError("invalid agreement criteria status")
    if not isinstance(criteria["metrics"], dict) or not criteria["metrics"]:
        raise ValueError("agreement metrics missing")
    for name, criterion in criteria["metrics"].items():
        identifier(name, "agreement metric")
        exact(criterion, {"unit", "absolute_margin"}, set(), "agreement metric criterion")
        finite(criterion["absolute_margin"], "agreement margin", 0.0)
    if set(profile["correlation"]["paired_metrics"]) != set(criteria["metrics"]):
        raise ValueError("paired metrics and agreement criteria differ")
    exact(profile["partitions"], {"development_session_ids", "validation_session_ids",
          "held_out_session_ids", "unit", "adjacent_slice_partitioning"}, set(), "partitions")
    partition_sets = [set(profile["partitions"][key]) for key in ("development_session_ids",
                      "validation_session_ids", "held_out_session_ids")]
    if any(partition_sets[a] & partition_sets[b] for a in range(3) for b in range(a+1, 3)):
        raise ValueError("profile session partitions overlap")
    if profile["partitions"]["unit"] != "physical_capture_session" or profile["partitions"]["adjacent_slice_partitioning"] != "prohibited":
        raise ValueError("unsafe profile partitioning")
    exact(profile["freeze"], {"held_out_status", "reason", "profile_sha256",
          "dataset_manifest_sha256", "receiver_executable_sha256", "plugin_manifest_sha256",
          "independent_tool_sha256", "freeze_manifest_path", "freeze_manifest_sha256"},
          set(), "freeze")
    if profile["status"] == "synthetic_only_infrastructure_physical_validation_unavailable":
        if thresholds["status"] != "provisional_engineering_not_frozen" or thresholds["threshold_set_sha256"] is not None or criteria["status"] != "provisional_not_frozen":
            raise ValueError("synthetic-only profile must retain provisional thresholds and correlation criteria")
        if profile["freeze"]["held_out_status"] != "not_frozen_physical_validation_unavailable":
            raise ValueError("infrastructure profile cannot claim a held-out freeze")
        if any(profile["freeze"][k] is not None for k in ("dataset_manifest_sha256",
                "receiver_executable_sha256", "plugin_manifest_sha256", "profile_sha256",
                "freeze_manifest_path", "freeze_manifest_sha256")):
            raise ValueError("absent evidence hashes must be null")
        if not isinstance(profile["freeze"]["independent_tool_sha256"], str) or len(profile["freeze"]["independent_tool_sha256"]) != 64:
            raise ValueError("tool hash must bind the infrastructure profile")
    else:
        if thresholds["status"] != "frozen_before_held_out" or not isinstance(thresholds["threshold_set_sha256"], str) or thresholds["threshold_set_sha256"] != digest_bytes(canonical(threshold_contract)):
            raise ValueError("frozen profile requires a correctly hashed threshold set frozen before held-out evidence")
        if criteria["status"] != "frozen_before_held_out":
            raise ValueError("frozen profile requires correlation criteria frozen before held-out evidence")
        if profile["freeze"]["held_out_status"] != "frozen_before_held_out_replay":
            raise ValueError("held-out profile has invalid freeze status")
        for key in ("profile_sha256", "dataset_manifest_sha256", "receiver_executable_sha256",
                    "plugin_manifest_sha256", "independent_tool_sha256", "freeze_manifest_sha256"):
            value = profile["freeze"][key]
            if not isinstance(value, str) or len(value) != 64:
                raise ValueError(f"frozen profile missing {key}")
        if not isinstance(profile["freeze"]["freeze_manifest_path"], str) or not profile["freeze"]["freeze_manifest_path"]:
            raise ValueError("frozen profile missing freeze manifest path")
        safe_relative(profile["freeze"]["freeze_manifest_path"], "freeze manifest path")


def validate_equipment(e: Any) -> None:
    exact(e, {"equipment_id", "category", "manufacturer", "model", "firmware",
              "identifier_policy", "calibration_record_ids"}, {"notes"}, "equipment")
    identifier(e["equipment_id"], "equipment_id")
    if e["identifier_policy"] not in {"anonymized_stable", "approved_serial"}:
        raise ValueError("unsafe equipment identifier policy")
    if not all(isinstance(x, str) for x in e["calibration_record_ids"]):
        raise ValueError("calibration ids must be strings")
    for key in ("category", "manufacturer", "model", "firmware"):
        if not isinstance(e[key], str) or not e[key]: raise ValueError(f"equipment {key} required")


def validate_calibration(c: Any) -> None:
    exact(c, {"calibration_id", "method", "date_utc", "reference_plane",
              "uncertainty_db", "artifact_relative_path", "artifact_sha256", "equipment_ids"},
          {"expires_utc", "notes"}, "calibration")
    identifier(c["calibration_id"], "calibration_id")
    safe_relative(c["artifact_relative_path"], "calibration artifact path")
    for key in ("method", "date_utc", "reference_plane"):
        if not isinstance(c[key], str) or not c[key]: raise ValueError(f"calibration {key} required")
    finite(c["uncertainty_db"], "calibration uncertainty", 0.0, 100.0)
    if not isinstance(c["artifact_sha256"], str) or len(c["artifact_sha256"]) != 64:
        raise ValueError("invalid calibration artifact hash")
    if not isinstance(c["equipment_ids"], list) or not all(isinstance(x, str) and x for x in c["equipment_ids"]):
        raise ValueError("calibration equipment_ids must be strings")


def validate_capture(c: Any) -> None:
    req = {"capture_id", "session_id", "capture_class", "partition", "relative_iq_path",
           "iq_sha256", "byte_length", "sample_format", "sample_rate_hz",
           "center_frequency_hz", "start_time_utc", "time_source_quality",
           "waveform_profile_id", "transmitter_event_log", "transmitter_event_log_sha256",
           "equipment_ids", "calibration_record_ids", "reference_plane",
           "wanted_power", "blocker_power", "cfo_hz", "sample_clock_offset_ppm",
           "agc_gain", "clipping_observed", "rf_path", "receiver_settings", "noise_floor",
           "channel_emulator", "ota",
           "environment", "operator_tool_versions", "rights", "parents",
           "derived_artifacts", "failure_or_exclusion", "scenario_tags", "provenance"}
    exact(c, req, {"sigmf_meta_path", "sigmf_meta_sha256", "notes"}, "capture")
    identifier(c["capture_id"], "capture_id"); identifier(c["session_id"], "session_id")
    if c["capture_class"] not in CAPTURE_CLASSES or c["partition"] not in PARTITIONS:
        raise ValueError("invalid capture class or partition")
    exact(c["provenance"], {"origin", "producer", "hardware_involved"}, set(), "capture provenance")
    physical = c["capture_class"] in PHYSICAL_CAPTURE_CLASSES
    if physical and (c["provenance"]["origin"] != "independently_recorded_physical" or c["provenance"]["hardware_involved"] is not True):
        raise ValueError("synthetic provenance cannot be promoted to a physical capture class")
    if c["capture_class"] == "synthetic_dry_run" and (c["provenance"]["origin"] != "synthetic_dry_run" or c["provenance"]["hardware_involved"] is not False):
        raise ValueError("synthetic dry-run provenance is invalid")
    if c["capture_class"] == "synthetic_dry_run" and c["partition"] != "development":
        raise ValueError("synthetic dry-run captures are development-only and cannot be held_out")
    if not isinstance(c["provenance"]["producer"], str) or not c["provenance"]["producer"]:
        raise ValueError("capture provenance producer is required")
    if not isinstance(c["scenario_tags"], list) or not c["scenario_tags"] or not all(isinstance(x, str) and x for x in c["scenario_tags"]):
        raise ValueError("capture requires non-empty scenario tags")
    for key in ("start_time_utc", "waveform_profile_id"):
        if not isinstance(c[key], str) or not c[key]: raise ValueError(f"capture {key} required")
    for key in ("equipment_ids", "calibration_record_ids", "parents"):
        if not isinstance(c[key], list) or not all(isinstance(x, str) and x for x in c[key]):
            raise ValueError(f"capture {key} must be an array of strings")
    safe_relative(c["relative_iq_path"], "IQ path")
    if c["transmitter_event_log"] is not None: safe_relative(c["transmitter_event_log"], "event log path")
    if c.get("sigmf_meta_path") is not None: safe_relative(c["sigmf_meta_path"], "SigMF path")
    if not isinstance(c["iq_sha256"], str) or len(c["iq_sha256"]) != 64:
        raise ValueError("invalid IQ hash")
    if isinstance(c["byte_length"], bool) or not isinstance(c["byte_length"], int) or c["byte_length"] <= 0:
        raise ValueError("byte length must be positive integer")
    if c["sample_format"] not in FORMATS: raise ValueError("unsupported IQ format")
    if c["byte_length"] % FORMATS[c["sample_format"]][1]:
        raise ValueError("partial complex sample")
    finite(c["sample_rate_hz"], "sample rate", 1.0)
    finite(c["center_frequency_hz"], "center frequency", 0.0)
    if not isinstance(c["reference_plane"], str) or not c["reference_plane"]:
        raise ValueError("reference_plane must be non-empty")
    if not isinstance(c["clipping_observed"], bool): raise ValueError("clipping_observed must be boolean")
    if c["agc_gain"] is not None: finite(c["agc_gain"], "agc_gain")
    exact(c["time_source_quality"], {"kind", "uncertainty_seconds"}, set(), "time_source_quality")
    if not isinstance(c["time_source_quality"]["kind"], str) or not c["time_source_quality"]["kind"]:
        raise ValueError("time source kind is required")
    if c["time_source_quality"]["uncertainty_seconds"] is not None:
        finite(c["time_source_quality"]["uncertainty_seconds"], "time uncertainty", 0.0)
    for field in ("cfo_hz", "sample_clock_offset_ppm"):
        if c[field] is not None: finite(c[field], field)
    for power_name in ("wanted_power", "blocker_power"):
        p = c[power_name]
        exact(p, {"commanded_dbm", "measured_dbm", "uncertainty_db", "measurement_kind"}, set(), power_name)
        for key in ("commanded_dbm", "measured_dbm", "uncertainty_db"):
            if p[key] is not None: finite(p[key], f"{power_name}.{key}")
        if p["measurement_kind"] not in {"not_applicable", "commanded_only", "calibrated_inference", "measured"}:
            raise ValueError("invalid power measurement kind")
        if p["measurement_kind"] in {"calibrated_inference", "measured"} and (p["measured_dbm"] is None or p["uncertainty_db"] is None):
            raise ValueError("measured power requires value and uncertainty")
        if p["measurement_kind"] == "commanded_only" and p["measured_dbm"] is not None:
            raise ValueError("commanded power mislabeled as measured")
    if not isinstance(c["rf_path"], list): raise ValueError("rf_path must be an array")
    for component in c["rf_path"]:
        exact(component, {"equipment_id", "kind", "gain_loss_db", "uncertainty_db",
              "reference_plane"}, set(), "rf_path component")
        finite(component["gain_loss_db"], "RF path gain/loss")
        finite(component["uncertainty_db"], "RF path uncertainty", 0.0)
        for key in ("equipment_id", "kind", "reference_plane"):
            if not isinstance(component[key], str) or not component[key]: raise ValueError("RF path identifiers required")
    exact(c["receiver_settings"], {"gain_mode", "gain_db", "agc_enabled",
          "overload_observed"}, set(), "receiver_settings")
    if not isinstance(c["receiver_settings"]["gain_mode"], str) or not c["receiver_settings"]["gain_mode"]:
        raise ValueError("receiver gain mode required")
    if not isinstance(c["receiver_settings"]["agc_enabled"], bool) or not isinstance(c["receiver_settings"]["overload_observed"], bool):
        raise ValueError("receiver boolean settings invalid")
    if c["receiver_settings"]["gain_db"] is not None: finite(c["receiver_settings"]["gain_db"], "receiver gain")
    exact(c["noise_floor"], {"measured_dbm_hz", "uncertainty_db", "method"}, set(), "noise_floor")
    for key in ("measured_dbm_hz", "uncertainty_db"):
        if c["noise_floor"][key] is not None: finite(c["noise_floor"][key], f"noise_floor.{key}")
    if not isinstance(c["noise_floor"]["method"], str) or not c["noise_floor"]["method"]:
        raise ValueError("noise-floor method required")
    exact(c["rights"], {"license", "privacy", "export_control", "retention",
                        "redistribution"}, set(), "rights")
    if not all(isinstance(x, str) and x for x in c["rights"].values()):
        raise ValueError("all rights classifications are required")
    exact(c["operator_tool_versions"], {"capture_tool", "capture_tool_version",
          "operator_id_policy"}, set(), "operator_tool_versions")
    exact(c["environment"], {"temperature_c", "humidity_percent", "motion",
          "scenario_notes"}, set(), "environment")
    if not all(isinstance(c["operator_tool_versions"][key], str) and c["operator_tool_versions"][key]
               for key in c["operator_tool_versions"]): raise ValueError("operator tool metadata required")
    if not all(isinstance(c["environment"][key], str) for key in ("motion", "scenario_notes")):
        raise ValueError("environment text metadata invalid")
    for key in ("temperature_c", "humidity_percent"):
        if c["environment"][key] is not None: finite(c["environment"][key], key)
    if (c["transmitter_event_log"] is None) != (c["transmitter_event_log_sha256"] is None):
        raise ValueError("event log path and hash must be paired")
    if c["transmitter_event_log_sha256"] is not None and (not isinstance(c["transmitter_event_log_sha256"], str) or len(c["transmitter_event_log_sha256"]) != 64):
        raise ValueError("invalid event-log hash")
    if (c.get("sigmf_meta_path") is None) != (c.get("sigmf_meta_sha256") is None):
        raise ValueError("SigMF path and hash must be paired")
    for artifact in c["derived_artifacts"]:
        exact(artifact, {"kind", "relative_path", "sha256", "provenance_relative_path",
              "provenance_sha256"}, set(), "derived artifact")
        safe_relative(artifact["relative_path"], "derived artifact path")
        safe_relative(artifact["provenance_relative_path"], "derived provenance path")
        if not isinstance(artifact["kind"], str) or not artifact["kind"]:
            raise ValueError("derived artifact kind required")
        for key in ("sha256", "provenance_sha256"):
            if not isinstance(artifact[key], str) or len(artifact[key]) != 64:
                raise ValueError("invalid derived artifact hash")
    emulator = c["channel_emulator"]
    if c["capture_class"] == "channel_emulator":
        exact(emulator, {"equipment_id", "model", "firmware", "configuration_relative_path", "configuration_sha256",
              "normalization", "reference_plane", "seed_repeat_behavior"}, set(), "channel_emulator")
        safe_relative(emulator["configuration_relative_path"], "channel-emulator configuration path")
        if not all(isinstance(emulator[key], str) and emulator[key] for key in
                   ("equipment_id", "model", "firmware", "normalization", "reference_plane", "seed_repeat_behavior")):
            raise ValueError("channel-emulator metadata required")
        if not isinstance(emulator["configuration_sha256"], str) or len(emulator["configuration_sha256"]) != 64:
            raise ValueError("invalid channel-emulator configuration hash")
    elif emulator is not None:
        raise ValueError("non-emulator capture cannot claim emulator metadata")
    ota = c["ota"]
    if c["capture_class"] == "ota":
        exact(ota, {"authorization_id", "geometry", "antenna_configuration",
              "environment_description", "motion_profile", "facility_boundary"}, set(), "ota")
        if not isinstance(ota["geometry"], dict) or not isinstance(ota["antenna_configuration"], dict) or not all(
                isinstance(ota[key], str) and ota[key] for key in
                ("authorization_id", "environment_description", "motion_profile", "facility_boundary")):
            raise ValueError("OTA metadata invalid")
    elif ota is not None:
        raise ValueError("non-OTA capture cannot claim OTA metadata")
    if c["partition"] == "held_out" and not c["calibration_record_ids"]:
        raise ValueError("held-out capture requires calibration")
    if c["partition"] == "held_out" and c["transmitter_event_log"] is None:
        raise ValueError("held-out physical capture requires transmitter event log, including empty negative-control logs")


def validate_collection(collection: Any) -> None:
    exact(collection, {"schema", "version", "dataset_id", "description", "status",
          "external_storage", "equipment", "calibrations", "sessions", "captures",
          "partition_policy", "duplicate_policy", "rights"}, set(), "collection")
    if collection["schema"] != "graphx.fhss.phase4-dataset-collection.v1" or collection["version"] != 1:
        raise ValueError("unsupported collection schema")
    identifier(collection["dataset_id"], "dataset_id")
    if not isinstance(collection["description"], str) or not collection["description"] or collection["status"] not in {"development", "frozen_held_out"}:
        raise ValueError("invalid collection description/status")
    exact(collection["external_storage"], {"resolver_kind", "location_not_committed",
          "large_iq_in_git_prohibited"}, set(), "external_storage")
    if collection["external_storage"]["large_iq_in_git_prohibited"] is not True:
        raise ValueError("large IQ must remain external")
    if collection["external_storage"]["location_not_committed"] is not True or not isinstance(collection["external_storage"]["resolver_kind"], str) or not collection["external_storage"]["resolver_kind"]:
        raise ValueError("external storage declaration invalid")
    exact(collection["partition_policy"], {"unit", "adjacent_slice_partitioning"}, set(), "partition_policy")
    if collection["partition_policy"] != {"unit": "session", "adjacent_slice_partitioning": "prohibited"}:
        raise ValueError("unsafe collection partition policy")
    exact(collection["duplicate_policy"], {"cross_partition_iq_reuse",
          "same_partition_iq_reuse"}, set(), "duplicate_policy")
    if collection["duplicate_policy"]["cross_partition_iq_reuse"] != "prohibited" or collection["duplicate_policy"]["same_partition_iq_reuse"] != "requires_explicit_parent_relationship":
        raise ValueError("unsafe duplicate IQ policy")
    exact(collection["rights"], {"license", "privacy", "export_control", "retention",
          "redistribution"}, set(), "collection rights")
    if not all(isinstance(x, str) and x for x in collection["rights"].values()):
        raise ValueError("collection rights must be non-empty")
    equipment = collection["equipment"]; calibrations = collection["calibrations"]
    if not isinstance(equipment, list) or not isinstance(calibrations, list):
        raise ValueError("equipment/calibrations must be arrays")
    for e in equipment: validate_equipment(e)
    for c in calibrations: validate_calibration(c)
    equipment_ids = [e["equipment_id"] for e in equipment]
    calibration_ids = [c["calibration_id"] for c in calibrations]
    if len(set(equipment_ids)) != len(equipment_ids) or len(set(calibration_ids)) != len(calibration_ids):
        raise ValueError("duplicate equipment or calibration id")
    for e in equipment:
        if set(e["calibration_record_ids"]) - set(calibration_ids):
            raise ValueError("equipment references unknown calibration")
    for c in calibrations:
        if set(c["equipment_ids"]) - set(equipment_ids):
            raise ValueError("calibration references unknown equipment")
    sessions = collection["sessions"]
    if not isinstance(sessions, list): raise ValueError("sessions must be array")
    session_ids, session_partitions = [], {}
    for s in sessions:
        exact(s, {"session_id", "capture_class", "partition", "independence_unit",
                  "start_time_utc", "end_time_utc", "equipment_reset", "authorization",
                  "capture_ids"}, {"notes"}, "session")
        sid = identifier(s["session_id"], "session_id"); session_ids.append(sid)
        if s["capture_class"] not in CAPTURE_CLASSES or s["partition"] not in PARTITIONS:
            raise ValueError("invalid session class or partition")
        if not all(isinstance(s[key], str) and s[key] for key in ("start_time_utc", "end_time_utc")) or not isinstance(s["equipment_reset"], bool):
            raise ValueError("invalid session time/reset metadata")
        if not isinstance(s["capture_ids"], list) or not all(isinstance(x, str) and x for x in s["capture_ids"]):
            raise ValueError("session capture_ids must be strings")
        if s["independence_unit"] != "physical_capture_session":
            raise ValueError("session is not a physical independence unit")
        exact(s["authorization"], {"status", "scope", "artifact_sha256",
              "artifact_relative_path", "facility_boundary"}, set(), "authorization")
        if s["authorization"]["artifact_relative_path"] is not None:
            safe_relative(s["authorization"]["artifact_relative_path"], "authorization artifact path")
        if s["capture_class"] == "ota" and s["authorization"]["status"] != "approved":
            raise ValueError("OTA session requires approved authorization")
        if s["authorization"]["status"] not in {"approved", "not_applicable"} or not all(
                isinstance(s["authorization"][key], str) and s["authorization"][key]
                for key in ("scope", "facility_boundary")):
            raise ValueError("invalid authorization metadata")
        if s["authorization"]["artifact_sha256"] is not None and (not isinstance(s["authorization"]["artifact_sha256"], str) or len(s["authorization"]["artifact_sha256"]) != 64):
            raise ValueError("invalid authorization artifact hash")
        if (s["authorization"]["artifact_sha256"] is None) != (s["authorization"]["artifact_relative_path"] is None):
            raise ValueError("authorization artifact path/hash must be paired")
        session_partitions[sid] = s["partition"]
    if len(set(session_ids)) != len(session_ids): raise ValueError("duplicate session id")
    captures = collection["captures"]
    if not isinstance(captures, list): raise ValueError("captures must be array")
    capture_ids = []
    for c in captures:
        validate_capture(c); capture_ids.append(c["capture_id"])
        if c["session_id"] not in session_partitions or session_partitions[c["session_id"]] != c["partition"]:
            raise ValueError("capture/session partition mismatch")
        session = next(s for s in sessions if s["session_id"] == c["session_id"])
        if session["capture_class"] != c["capture_class"]:
            raise ValueError("capture/session class mismatch")
        if set(c["equipment_ids"]) - set(equipment_ids) or set(c["calibration_record_ids"]) - set(calibration_ids):
            raise ValueError("capture references unknown equipment/calibration")
        if {x["equipment_id"] for x in c["rf_path"]} - set(equipment_ids):
            raise ValueError("RF path references unknown equipment")
    if len(set(capture_ids)) != len(capture_ids): raise ValueError("duplicate capture id")
    declared_list = [x for s in sessions for x in s["capture_ids"]]
    if len(declared_list) != len(set(declared_list)) or set(declared_list) != set(capture_ids):
        raise ValueError("session capture inventory mismatch")
    # A physical session belongs to exactly one partition; derivation cannot cross into held-out.
    by_id = {c["capture_id"]: c for c in captures}
    for c in captures:
        if set(c["parents"]) - by_id.keys():
            raise ValueError("capture references unknown parent")
        if c["partition"] == "held_out":
            for parent in c["parents"]:
                if parent in by_id and by_id[parent]["partition"] != "held_out":
                    raise ValueError("held-out capture derives from development/validation recording")
    for i, left in enumerate(captures):
        for right in captures[i+1:]:
            reused = (left["relative_iq_path"] == right["relative_iq_path"] or
                      left["iq_sha256"] == right["iq_sha256"])
            if reused and left["partition"] != right["partition"]:
                raise ValueError("IQ path/hash reused across partitions")
            if reused and left["partition"] == right["partition"] and not (
                    left["capture_id"] in right["parents"] or right["capture_id"] in left["parents"]):
                raise ValueError("same-partition IQ reuse lacks explicit parent relationship")


def resolve_and_verify(capture: dict[str, Any], corpus_root: Path) -> Path:
    root = corpus_root.resolve(); path = (root / capture["relative_iq_path"]).resolve()
    if root != path and root not in path.parents: raise ValueError("capture escapes corpus root")
    if not path.is_file(): raise FileNotFoundError(f"missing external IQ: {capture['capture_id']}")
    if path.stat().st_size != capture["byte_length"]: raise ValueError("IQ byte length mismatch")
    if digest_file(path) != capture["iq_sha256"]: raise ValueError("IQ hash mismatch")
    if path.stat().st_size % FORMATS[capture["sample_format"]][1]: raise ValueError("partial complex sample")
    if capture.get("sigmf_meta_path"):
        meta = resolve_hash_artifact(root, capture["sigmf_meta_path"], capture["sigmf_meta_sha256"], "SigMF metadata")
        sigmf = load_json(meta)
        if not isinstance(sigmf, dict) or "global" not in sigmf or "core:datatype" not in sigmf["global"]:
            raise ValueError("invalid SigMF metadata")
        global_meta = sigmf["global"]
        if global_meta["core:datatype"] != capture["sample_format"]:
            raise ValueError("SigMF datatype differs from capture metadata")
        if finite(global_meta.get("core:sample_rate"), "SigMF sample rate", 1.0) != float(capture["sample_rate_hz"]):
            raise ValueError("SigMF sample rate differs from capture metadata")
        if global_meta.get("core:num_channels", 1) != 1:
            raise ValueError("only one-channel complex SigMF is supported")
    return path


def resolve_hash_artifact(root: Path, relative: str, expected_hash: str, where: str) -> Path:
    if Path(relative).is_absolute() or ".." in Path(relative).parts:
        raise ValueError(f"unsafe {where} path")
    base = root.resolve(); path = (base / relative).resolve()
    if base != path and base not in path.parents: raise ValueError(f"unsafe {where} path")
    if not path.is_file(): raise FileNotFoundError(f"missing {where}")
    if digest_file(path) != expected_hash: raise ValueError(f"{where} hash mismatch")
    return path


def convert_iq(source: Path, destination: Path, source_format: str,
               destination_format: str, expected_sha256: str | None = None) -> dict[str, Any]:
    if source_format not in FORMATS or destination_format not in {"cf32_le", "cf64_le"}:
        raise ValueError("unsupported conversion")
    if source.resolve() == destination.resolve(): raise ValueError("source and destination alias")
    if destination.exists(): raise FileExistsError("destination already exists; conversion is no-clobber")
    if expected_sha256 and digest_file(source) != expected_sha256:
        raise ValueError("source hash mismatch")
    source_struct, source_size = FORMATS[source_format]
    dest_struct, _ = FORMATS[destination_format]
    if source.stat().st_size % source_size: raise ValueError("partial complex sample")
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
    count = 0
    try:
        with source.open("rb") as inp, os.fdopen(fd, "wb") as out:
            while raw := inp.read(source_size):
                i, q = struct.unpack(source_struct, raw)
                if not math.isfinite(i) or not math.isfinite(q): raise ValueError("non-finite IQ sample")
                out.write(struct.pack(dest_struct, i, q)); count += 1
            out.flush(); os.fsync(out.fileno())
        os.link(name, destination)
        os.unlink(name)
    except BaseException:
        try: os.unlink(name)
        except FileNotFoundError: pass
        raise
    return {"schema": "graphx.fhss.phase4-conversion-provenance.v1",
            "tool_version": VERSION, "source_path": str(source),
            "source_format": source_format, "source_sha256": digest_file(source),
            "source_byte_length": source.stat().st_size,
            "destination_path": str(destination), "destination_format": destination_format,
            "destination_sha256": digest_file(destination),
            "destination_byte_length": destination.stat().st_size, "complex_samples": count}


def convert_with_provenance(source: Path, destination: Path, source_format: str,
                            destination_format: str, expected_sha256: str | None,
                            provenance_path: Path) -> dict[str, Any]:
    if provenance_path.exists(): raise FileExistsError("provenance destination already exists")
    if provenance_path.resolve() in {source.resolve(), destination.resolve()}:
        raise ValueError("provenance path aliases IQ source/destination")
    result = convert_iq(source, destination, source_format, destination_format, expected_sha256)
    try:
        atomic_json(provenance_path, result)
    except BaseException:
        if destination.is_file(): destination.unlink()
        raise
    return result


def scan_truth_free_graph(graph: Any) -> None:
    def visit(value: Any, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if key in FORBIDDEN_KEYS: raise ValueError(f"forbidden receiver key {path}.{key}")
                lowered = key.lower()
                if any(alias in lowered for alias in ("synthetic", "generator", "truth", "expected_word", "transmit_schedule")):
                    raise ValueError(f"forbidden receiver config alias {path}.{key}")
                visit(child, f"{path}.{key}")
        elif isinstance(value, list):
            for i, child in enumerate(value): visit(child, f"{path}[{i}]")
        elif isinstance(value, str) and value in FORBIDDEN_VALUES:
            raise ValueError(f"forbidden receiver component {value}")
    visit(graph, "graph")
    sources = [n for n in graph.get("nodes", []) if n.get("type") == "FHSSBinaryIqFileSourceNode"]
    if len(sources) != 1: raise ValueError("receiver must have exactly one binary IQ source")
    for node in graph.get("nodes", []):
        node_type = str(node.get("type", "")); lowered = node_type.lower()
        if node_type != "FHSSBinaryIqFileSourceNode" and any(alias in lowered for alias in ("source", "generator", "synthetic")):
            raise ValueError(f"receiver contains additional source/generator node {node_type}")


def patch_graph(base: Any, iq_path: Path, output_paths: dict[str, str] | None = None) -> Any:
    graph = json.loads(json.dumps(base)); scan_truth_free_graph(graph)
    source = next(n for n in graph["nodes"] if n["type"] == "FHSSBinaryIqFileSourceNode")
    source["node_config"]["file_path"] = str(iq_path)
    if output_paths:
        for node in graph["nodes"]:
            if node.get("id") in output_paths:
                if "output_path" not in node.get("node_config", {}):
                    raise ValueError("attempt to patch undeclared receiver output")
                node["node_config"]["output_path"] = output_paths[node["id"]]
    scan_truth_free_graph(graph); return graph


def match_events(events: list[dict[str, Any]], detections: list[dict[str, Any]],
                 tolerance_samples: int) -> dict[str, Any]:
    """Maximum-cardinality, then minimum-time-error one-to-one assignment."""
    if tolerance_samples < 0: raise ValueError("negative matching tolerance")
    for seq, name in ((events, "event"), (detections, "detection")):
        ids = set()
        for item in seq:
            required = {f"{name}_id", "start_sample", "frequency_index", "word"}
            if name == "event": required |= {"message_id"}
            exact(item, required, {"collision"}, name)
            iid = identifier(item[f"{name}_id"], f"{name}_id")
            if iid in ids: raise ValueError(f"duplicate {name} id")
            ids.add(iid)
            if not isinstance(item["start_sample"], int) or item["start_sample"] < 0:
                raise ValueError("invalid event time")
            if isinstance(item["frequency_index"], bool) or not isinstance(item["frequency_index"], int) or not 0 <= item["frequency_index"] <= 63:
                raise ValueError("frequency index must be in [0,63]")
            if isinstance(item["word"], bool) or not isinstance(item["word"], int) or not 0 <= item["word"] <= 0xFFFF_FFFF:
                raise ValueError("word must be uint32")
            if name == "event": identifier(item["message_id"], "message_id")
    ordered_events = sorted(enumerate(events), key=lambda x: (x[1]["start_sample"], x[1]["event_id"]))
    ordered_detections = sorted(enumerate(detections), key=lambda x: (x[1]["start_sample"], x[1]["detection_id"]))
    n, m = len(ordered_events), len(ordered_detections)
    # For points on a line and absolute-distance cost, an optimal assignment is
    # noncrossing. Dynamic programming therefore gives maximum cardinality and,
    # secondarily, minimum total timing error without a greedy counterexample.
    dp = [[(0, 0) for _ in range(m+1)] for _ in range(n+1)]
    action = [["" for _ in range(m+1)] for _ in range(n+1)]
    for i in range(1, n+1): action[i][0] = "skip_e"
    for j in range(1, m+1): action[0][j] = "skip_d"
    for i in range(1, n+1):
        for j in range(1, m+1):
            choices = [(dp[i-1][j], 1, "skip_e"), (dp[i][j-1], 2, "skip_d")]
            delta = abs(ordered_events[i-1][1]["start_sample"] - ordered_detections[j-1][1]["start_sample"])
            if delta <= tolerance_samples:
                previous = dp[i-1][j-1]
                choices.append(((previous[0]+1, previous[1]-delta), 3, "match"))
            best = max(choices, key=lambda x: (x[0][0], x[0][1], x[1]))
            dp[i][j], action[i][j] = best[0], best[2]
    pairs, i, j = [], n, m
    while i or j:
        selected = action[i][j]
        if selected == "match":
            pairs.append((ordered_events[i-1][0], ordered_detections[j-1][0])); i -= 1; j -= 1
        elif selected == "skip_e": i -= 1
        else: j -= 1
    pairs.reverse(); used_e = {x for x, _ in pairs}; used_d = {x for _, x in pairs}
    matched = []
    for ei, di in pairs:
        e, d = events[ei], detections[di]
        word_errors = (e["word"] ^ d["word"]).bit_count()
        matched.append({"event_id": e["event_id"], "detection_id": d["detection_id"],
                        "timing_error_samples": d["start_sample"] - e["start_sample"],
                        "frequency_correct": e["frequency_index"] == d["frequency_index"],
                        "bit_errors": word_errors, "word_error": word_errors != 0,
                        "collision": bool(e.get("collision", False))})
    candidate_events = {ei for ei, event in enumerate(events) for detection in detections
                        if abs(detection["start_sample"]-event["start_sample"]) <= tolerance_samples}
    candidate_detections = {di for di, detection in enumerate(detections) for event in events
                            if abs(detection["start_sample"]-event["start_sample"]) <= tolerance_samples}
    unmatched_e = [i for i in range(len(events)) if i not in used_e]
    unmatched_d = [i for i in range(len(detections)) if i not in used_d]
    return {"matched": matched,
            "miss_event_ids": [events[i]["event_id"] for i in unmatched_e if i not in candidate_events],
            "association_failure_event_ids": [events[i]["event_id"] for i in unmatched_e if i in candidate_events],
            "false_detection_ids": [detections[i]["detection_id"] for i in unmatched_d],
            "duplicate_detection_ids": [detections[i]["detection_id"] for i in unmatched_d if i in candidate_detections],
            "collision_truth_count": sum(bool(e.get("collision", False)) for e in events),
            "collision_matched_count": sum(bool(x["collision"]) for x in matched),
            "event_count": len(events), "detection_count": len(detections),
            "matched_count": len(matched)}


def is_negative_control(capture: dict[str, Any]) -> bool:
    return capture["capture_class"] == "negative_control" or "noise_only" in capture.get("scenario_tags", [])


def validate_event_document(event_doc: Any, capture: dict[str, Any]) -> None:
    exact(event_doc, {"schema", "session_id", "capture_id", "time_reference",
          "timing_uncertainty_samples", "events"}, set(), "event log")
    if event_doc["schema"] != "graphx.fhss.phase4-transmitter-events.v1":
        raise ValueError("unsupported event-log schema")
    if event_doc["capture_id"] != capture["capture_id"] or event_doc["session_id"] != capture["session_id"]:
        raise ValueError("event-log identity mismatch")
    uncertainty = event_doc["timing_uncertainty_samples"]
    if isinstance(uncertainty, bool) or not isinstance(uncertainty, int) or uncertainty < 0:
        raise ValueError("invalid event timing uncertainty")
    if not isinstance(event_doc["time_reference"], str) or not event_doc["time_reference"]:
        raise ValueError("event time reference required")
    match_events(event_doc["events"], [], 0)  # strict event contract validation
    message_ids = {event["message_id"] for event in event_doc["events"]}
    if is_negative_control(capture):
        if event_doc["events"]: raise ValueError("negative/noise control must contain zero transmitter events")
    elif not event_doc["events"] or len(message_ids) != 1:
        raise ValueError("v1 signal-bearing capture requires nonempty events for exactly one message_id")


def cluster_interval(values: dict[str, list[float]], confidence: float = .95,
                     replicates: int = 4000) -> dict[str, Any]:
    """Cluster bootstrap enveloped by a session-conservative Wilson bound."""
    if not values: return {"estimate": None, "interval": [None, None], "session_count": 0}
    sessions = sorted(values)
    flattened = [x for s in sessions for x in values[s]]
    if not flattened or any(not math.isfinite(x) for x in flattened):
        raise ValueError("metric values must be finite and non-empty")
    estimate = sum(flattened) / len(flattened)
    # Local fixed LCG avoids platform/version-dependent random sampling.
    state, samples = 0x50483431, []
    for _ in range(replicates):
        chosen = []
        for _ in sessions:
            state = (1664525 * state + 1013904223) & 0xFFFFFFFF
            chosen.extend(values[sessions[state % len(sessions)]])
        samples.append(sum(chosen) / len(chosen))
    samples.sort(); alpha = (1.0 - confidence) / 2.0
    lo = samples[int(alpha * replicates)]
    hi = samples[min(replicates - 1, int((1.0 - alpha) * replicates))]
    # Treating the number of independent sessions as the effective sample size
    # is conservative under arbitrary within-session dependence.  Enveloping
    # the bootstrap prevents degenerate [1,1]/[0,0] boundary claims.
    z = NormalDist().inv_cdf(.5 + confidence / 2.0); n = len(sessions)
    denominator = 1.0 + z*z/n
    center = (estimate + z*z/(2.0*n)) / denominator
    half = z * math.sqrt(estimate*(1.0-estimate)/n + z*z/(4.0*n*n)) / denominator
    wilson = [max(0.0, center-half), min(1.0, center+half)]
    lo, hi = min(lo, wilson[0]), max(hi, wilson[1])
    return {"estimate": estimate, "interval": [lo, hi], "confidence": confidence,
            "method": "deterministic percentile cluster bootstrap enveloped by session-effective Wilson score bounds",
            "cluster_unit": "capture_session", "session_count": len(sessions),
            "observation_count": len(flattened), "replicates": replicates,
            "finite_sample_envelope": "Wilson score using independent-session effective sample size"}


def poisson_count_upper(count: int, exposure: float, confidence: float) -> float:
    """Exact one-sided Garwood-equivalent upper rate by Poisson-CDF inversion."""
    if isinstance(count, bool) or not isinstance(count, int) or count < 0:
        raise ValueError("Poisson count must be non-negative integer")
    finite(exposure, "Poisson exposure", 1e-300)
    finite(confidence, "Poisson confidence", .5, .999999999)
    alpha = 1.0-confidence
    def log_cdf(mean: float) -> float:
        logs = [-mean]
        for k in range(1, count+1): logs.append(logs[-1] + math.log(mean) - math.log(k))
        maximum = max(logs)
        return maximum + math.log(sum(math.exp(x-maximum) for x in logs))
    target = math.log(alpha); low, high = 0.0, max(1.0, count+1.0)
    while log_cdf(high) > target: high *= 2.0
    for _ in range(100):
        middle = (low+high)/2.0
        if log_cdf(middle) > target: low = middle
        else: high = middle
    return high/exposure


def plugin_manifest(plugin_dir: Path) -> dict[str, Any]:
    if not plugin_dir.is_dir(): raise FileNotFoundError("plugin directory is missing")
    files = [{"name": str(path.relative_to(plugin_dir)), "sha256": digest_file(path),
              "byte_length": path.stat().st_size}
             for path in sorted(plugin_dir.rglob("*")) if path.is_file()]
    return {"files": files, "sha256": digest_bytes(canonical(files))}


def allocation_from_summary(summary: dict[str, Any]) -> dict[str, Any]:
    values = []
    for node in summary.get("diagnostics_snapshot", []):
        value = node.get("diagnostics", {}).get("allocation_high_water_bytes")
        if isinstance(value, int) and value >= 0: values.append(value)
    return {"reported_node_count": len(values), "sum_high_water_bytes": sum(values),
            "max_high_water_bytes": max(values, default=0)}


def validate_replay_result(result: Any) -> None:
    required = {"schema", "capture_id", "session_id", "attempt_number", "status", "command",
                "return_status", "receiver_completed", "completed", "elapsed_seconds", "hashes",
                "searched_samples", "searched_exposure_known", "matching", "allocation", "failure"}
    exact(result, required, set(), "replay result")
    if result["schema"] != "graphx.fhss.phase4-replay-result.v1" or result["attempt_number"] != 1:
        raise ValueError("invalid replay result schema/attempt")
    if result["status"] not in {"attempting", "completed", "failed", "timed_out"}:
        raise ValueError("invalid replay status")
    if not isinstance(result["completed"], bool) or not isinstance(result["receiver_completed"], bool):
        raise ValueError("replay completion flags must be boolean")
    if result["return_status"] is not None and (isinstance(result["return_status"], bool) or not isinstance(result["return_status"], int)):
        raise ValueError("return_status must be integer or null")
    if result["completed"] != (result["status"] == "completed"):
        raise ValueError("replay completion/status mismatch")
    if result["status"] == "attempting":
        if result["failure"] is not None or result["return_status"] is not None or result["receiver_completed"]:
            raise ValueError("attempting replay has terminal state")
    elif result["status"] == "completed":
        if result["failure"] is not None or result["return_status"] != 0 or not result["receiver_completed"]:
            raise ValueError("completed replay has inconsistent execution state")
    elif result["failure"] is None:
        raise ValueError("failed/timed-out replay lacks failure")
    elif result["status"] == "timed_out" and (result["return_status"] is not None or result["receiver_completed"]):
        raise ValueError("timed-out replay has inconsistent execution state")
    elif result["status"] == "failed" and ((result["receiver_completed"] and result["return_status"] != 0) or
            (not result["receiver_completed"] and result["return_status"] == 0)):
        raise ValueError("failed replay has inconsistent execution state")
    if not isinstance(result["command"], list) or not all(isinstance(x, str) for x in result["command"]):
        raise ValueError("invalid replay command")
    finite(result["elapsed_seconds"], "replay elapsed time", 0.0)
    if isinstance(result["searched_samples"], bool) or not isinstance(result["searched_samples"], int) or result["searched_samples"] < 0:
        raise ValueError("invalid searched sample count")
    if not isinstance(result["searched_exposure_known"], bool):
        raise ValueError("searched_exposure_known must be boolean")
    if not result["searched_exposure_known"] and result["searched_samples"] != 0:
        raise ValueError("unknown searched exposure must not claim samples")
    exact(result["hashes"], {"iq_sha256", "receiver_sha256", "base_graph_sha256",
          "effective_graph_sha256", "effective_config_sha256", "plugin_manifest_sha256",
          "stdout_sha256", "stderr_sha256"}, set(), "replay hashes")
    for value in result["hashes"].values():
        if value is not None and (not isinstance(value, str) or len(value) != 64):
            raise ValueError("invalid replay provenance hash")
    exact(result["allocation"], {"reported_node_count", "sum_high_water_bytes",
          "max_high_water_bytes"}, set(), "allocation")
    for value in result["allocation"].values():
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError("invalid allocation count")
    if result["failure"] is not None:
        exact(result["failure"], {"stage", "kind", "message"}, {"receiver_completed"}, "replay failure")
        if result["failure"]["stage"] not in {"pre_execution", "receiver_execution",
                "post_receiver_evaluator", "interrupted_attempt"}:
            raise ValueError("invalid failure stage")
        if not all(isinstance(result["failure"][key], str) and result["failure"][key]
                   for key in ("kind", "message")):
            raise ValueError("invalid failure description")
        if "receiver_completed" in result["failure"] and not isinstance(result["failure"]["receiver_completed"], bool):
            raise ValueError("failure receiver_completed must be boolean")
    matching = result["matching"]
    if matching is not None:
        exact(matching, {"matched", "miss_event_ids", "association_failure_event_ids",
              "false_detection_ids", "duplicate_detection_ids", "collision_truth_count",
              "collision_matched_count", "event_count", "detection_count", "matched_count"},
              set(), "matching")
        if matching["matched_count"] != len(matching["matched"]):
            raise ValueError("matching count mismatch")
        for name in ("miss_event_ids", "association_failure_event_ids", "false_detection_ids", "duplicate_detection_ids"):
            if not isinstance(matching[name], list) or not all(isinstance(x, str) and x for x in matching[name]):
                raise ValueError("matching identifiers must be non-empty strings")
        for name in ("collision_truth_count", "collision_matched_count", "event_count", "detection_count", "matched_count"):
            if isinstance(matching[name], bool) or not isinstance(matching[name], int) or matching[name] < 0:
                raise ValueError("matching counts must be non-negative integers")
        if not isinstance(matching["matched"], list): raise ValueError("matched must be an array")
        for match in matching["matched"]:
            exact(match, {"event_id", "detection_id", "timing_error_samples", "frequency_correct",
                  "bit_errors", "word_error", "collision"}, set(), "match")
            if not isinstance(match["event_id"], str) or not match["event_id"] or not isinstance(match["detection_id"], str) or not match["detection_id"]:
                raise ValueError("match identifiers required")
            if isinstance(match["timing_error_samples"], bool) or not isinstance(match["timing_error_samples"], int):
                raise ValueError("timing error must be integer")
            if isinstance(match["bit_errors"], bool) or not isinstance(match["bit_errors"], int) or not 0 <= match["bit_errors"] <= 32:
                raise ValueError("bit error count invalid")
            if not all(isinstance(match[key], bool) for key in ("frequency_correct", "word_error", "collision")):
                raise ValueError("match flags must be boolean")
            if match["word_error"] != (match["bit_errors"] != 0):
                raise ValueError("word-error flag contradicts bit-error count")
        matched_event_ids = [x["event_id"] for x in matching["matched"]]
        matched_detection_ids = [x["detection_id"] for x in matching["matched"]]
        miss_ids = matching["miss_event_ids"]; association_ids = matching["association_failure_event_ids"]
        false_ids = matching["false_detection_ids"]; duplicate_ids = matching["duplicate_detection_ids"]
        event_groups = (matched_event_ids, miss_ids, association_ids)
        if any(len(group) != len(set(group)) for group in event_groups) or any(
                set(event_groups[i]) & set(event_groups[j]) for i in range(3) for j in range(i + 1, 3)):
            raise ValueError("event outcomes must be unique and disjoint")
        if len(matched_detection_ids) != len(set(matched_detection_ids)) or len(false_ids) != len(set(false_ids)) or set(matched_detection_ids) & set(false_ids):
            raise ValueError("detection outcomes must be unique and disjoint")
        if len(duplicate_ids) != len(set(duplicate_ids)) or not set(duplicate_ids) <= set(false_ids):
            raise ValueError("duplicate detections must be a unique subset of false detections")
        if matching["event_count"] != len(matched_event_ids) + len(miss_ids) + len(association_ids):
            raise ValueError("event count contradicts declared outcomes")
        if matching["detection_count"] != len(matched_detection_ids) + len(false_ids):
            raise ValueError("detection count contradicts declared outcomes")
        matched_collisions = sum(x["collision"] for x in matching["matched"])
        if matching["collision_matched_count"] != matched_collisions or not 0 <= matched_collisions <= matching["collision_truth_count"] <= matching["event_count"]:
            raise ValueError("collision counts contradict declared outcomes")


def validate_correlation_report(report: Any) -> None:
    exact(report, {"schema", "status", "profile_sha256", "dataset_manifest_sha256",
          "paired_points", "metrics", "discrepancies", "limitations"}, set(), "correlation report")
    if report["schema"] != "graphx.fhss.phase4-correlation-report.v1" or report["status"] not in {"PASS", "FAIL", "BLOCKED", "UNAVAILABLE_DEFERRED"}:
        raise ValueError("invalid correlation report identity/status")
    for key in ("profile_sha256", "dataset_manifest_sha256"):
        value = report[key]
        if value is not None and (not isinstance(value, str) or len(value) != 64):
            raise ValueError("invalid correlation binding hash")
    if not isinstance(report["paired_points"], list) or not isinstance(report["metrics"], list) or not isinstance(report["discrepancies"], list):
        raise ValueError("invalid correlation arrays")
    for row in report["paired_points"]:
        exact(row, {"pair_id", "scenario_id", "metric", "unit", "reference_plane",
              "simulation_value", "hardware_value", "residual", "combined_standard_uncertainty",
              "expanded_uncertainty_k2", "absolute_margin", "pass", "discrepancy_classification",
              "simulation_artifact_sha256", "hardware_artifact_sha256", "simulation_sample_count",
              "hardware_sample_count", "hardware_session_count"}, set(), "paired point")
        for key in ("simulation_value", "hardware_value", "residual", "combined_standard_uncertainty",
                    "expanded_uncertainty_k2", "absolute_margin"):
            finite(row[key], f"paired point {key}")
    if report["status"] in {"BLOCKED", "UNAVAILABLE_DEFERRED"}:
        if report["paired_points"] or report["metrics"] or report["dataset_manifest_sha256"] is not None:
            raise ValueError("blocked correlation report contains evidence")
        for item in report["discrepancies"]:
            exact(item, {"classification", "reason"}, set(), "blocked discrepancy")
            if item["classification"] != "insufficient_evidence": raise ValueError("invalid blocked discrepancy")
    else:
        if report["dataset_manifest_sha256"] is None or not report["paired_points"]:
            raise ValueError("correlation decision lacks paired evidence")
        for item in report["discrepancies"]:
            exact(item, {"pair_id", "classification", "residual", "margin"}, set(), "discrepancy")


def replay_capture(profile: dict[str, Any], capture: dict[str, Any], iq_path: Path,
                   receiver: Path, graph_path: Path, plugins: Path,
                   corpus_root: Path, work: Path) -> dict[str, Any]:
    """Run receiver first, then and only then open evaluator truth/event data."""
    if capture["sample_format"] != profile["waveform"]["canonical_replay_format"]:
        raise ValueError("capture must be provenance-converted to frozen canonical format")
    base = load_json(graph_path); patched = patch_graph(base, iq_path)
    work.mkdir(parents=True, exist_ok=False)
    graph = work / "receiver.json"; summary_path = work / "summary.json"
    effective = work / "effective.json"
    atomic_json(graph, patched)
    command = [str(receiver), "--graph-config", str(graph), "--plugin-dir", str(plugins),
               "--summary-json", str(summary_path), "--effective-config-json", str(effective),
               "--executor-timeout-s", str(profile["replay"]["executor_timeout_seconds"])]
    started = time.perf_counter(); timed_out = False
    try:
        completed = subprocess.run(command, capture_output=True, text=True,
                                   timeout=profile["replay"]["process_timeout_seconds"])
        status, stdout, stderr = completed.returncode, completed.stdout, completed.stderr
    except subprocess.TimeoutExpired as error:
        timed_out, status = True, None
        stdout, stderr = error.stdout or "", error.stderr or ""
        if isinstance(stdout, bytes): stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes): stderr = stderr.decode(errors="replace")
    elapsed = time.perf_counter() - started
    process_completed = status == 0
    sample_count = capture["byte_length"] // FORMATS[capture["sample_format"]][1]
    manifest = plugin_manifest(plugins)
    result = {"schema": "graphx.fhss.phase4-replay-result.v1",
            "capture_id": capture["capture_id"], "session_id": capture["session_id"],
            "attempt_number": 1,
            "status": "timed_out" if timed_out else "failed",
            "command": command, "return_status": status, "receiver_completed": process_completed,
            "completed": False,
            "elapsed_seconds": elapsed,
            "hashes": {"iq_sha256": digest_file(iq_path), "receiver_sha256": digest_file(receiver),
                       "base_graph_sha256": digest_file(graph_path),
                       "effective_graph_sha256": digest_bytes(canonical(patched)),
                       "effective_config_sha256": digest_file(effective) if effective.is_file() else None,
                       "plugin_manifest_sha256": manifest["sha256"],
                       "stdout_sha256": digest_bytes(stdout.encode()),
                       "stderr_sha256": digest_bytes(stderr.encode())},
            "searched_samples": 0, "searched_exposure_known": False, "matching": None,
            "allocation": allocation_from_summary({}),
            "failure": None if process_completed else {"stage": "receiver_execution",
                "kind": "Timeout" if timed_out else "NonzeroOrMissingSummary",
                "message": "receiver timed out" if timed_out else "receiver failed or summary missing",
                "receiver_completed": process_completed}}
    # Evaluator-only access is intentionally after the subprocess terminates.
    try:
        if process_completed:
            result["searched_samples"] = sample_count; result["searched_exposure_known"] = True
            if not summary_path.is_file() or not effective.is_file():
                raise ValueError("receiver summary/effective output missing")
            summary = load_json(summary_path)
            effective_doc = load_json(effective)
            if not isinstance(summary, dict) or not isinstance(effective_doc, dict):
                raise ValueError("receiver summary/effective output must be JSON objects")
            result["allocation"] = allocation_from_summary(summary)
            result["status"] = "completed"; result["completed"] = True; result["failure"] = None
        else:
            summary = None
            if summary_path.is_file():
                try:
                    partial = load_json(summary_path)
                    progress = partial.get("fhss_replay_progress", {}) if isinstance(partial, dict) else {}
                    processed = progress.get("processed_samples")
                    if (not isinstance(processed, bool) and isinstance(processed, int) and
                            0 <= processed <= sample_count):
                        result["searched_samples"] = processed
                        result["searched_exposure_known"] = True
                except (OSError, ValueError, json.JSONDecodeError):
                    pass
        if capture["transmitter_event_log"] is not None:
            event_path = resolve_hash_artifact(corpus_root, capture["transmitter_event_log"], capture["transmitter_event_log_sha256"], "event log")
            event_doc = load_json(event_path)
            validate_event_document(event_doc, capture)
            diagnostics = (summary or {}).get("fhss_diagnostics", {})
            if process_completed and diagnostics.get("schema") != "graphx.fhss.message_sink.diagnostics.v1":
                raise ValueError("unsupported receiver diagnostics schema")
            decoded = diagnostics.get("decoded_pulses", []) if process_completed else []
            detections = [{"detection_id": f"d{i}", "start_sample": int(x["global_start_sample"]),
                           "frequency_index": int(x["frequency_index"]), "word": int(x["decoded_value"])}
                          for i, x in enumerate(decoded)]
            tolerance = (profile["statistics"]["matching"]["nominal_tolerance_samples"] +
                         int(event_doc["timing_uncertainty_samples"]))
            result["matching"] = match_events(event_doc["events"], detections, tolerance)
    except (ValueError, OSError, json.JSONDecodeError, KeyError, TypeError) as error:
        result["status"] = "failed"; result["completed"] = False
        result["failure"] = {"stage": "post_receiver_evaluator", "kind": type(error).__name__,
                             "message": str(error), "receiver_completed": process_completed}
    validate_replay_result(result)
    return result


def aggregate_journal(profile: dict[str, Any], collection: dict[str, Any],
                      journal: dict[str, Any], expected_bindings: dict[str, Any] | None = None,
                      correlation_report: dict[str, Any] | None = None,
                      corpus_root: Path | None = None) -> dict[str, Any]:
    validate_profile(profile); validate_collection(collection)
    by_capture = {c["capture_id"]: c for c in collection["captures"]}
    results = journal.get("results")
    if not isinstance(results, list): raise ValueError("journal results missing")
    ids = [r.get("capture_id") for r in results]
    if len(ids) != len(set(ids)) or set(ids) - by_capture.keys():
        raise ValueError("journal contains duplicate or unknown capture")
    result_captures = [by_capture[x] for x in ids]
    has_synthetic = any(c["capture_class"] == "synthetic_dry_run" for c in result_captures)
    has_physical = any(c["capture_class"] in PHYSICAL_CAPTURE_CLASSES for c in result_captures)
    if has_synthetic and has_physical: raise ValueError("synthetic and physical results cannot be aggregated together")
    if has_physical and any(c["partition"] != "held_out" or c["capture_class"] not in PHYSICAL_CAPTURE_CLASSES for c in result_captures):
        raise ValueError("physical aggregation accepts physical held-out classes only")
    if has_physical:
        if profile["status"] != "frozen_for_heldout" or expected_bindings is None:
            raise ValueError("physical aggregation requires a frozen profile and external artifact bindings")
        selected = [c for c in collection["captures"] if c["partition"] == "held_out" and c["capture_class"] in PHYSICAL_CAPTURE_CLASSES]
        validate_journal(journal, collection, selected, expected_bindings, True)
    clustered = {name: {} for name in ("detection", "bit_correct", "word_correct", "message_correct")}
    totals = {"attempted_captures": 0, "completed_captures": 0, "failed_captures": 0,
              "truth_events": 0, "matched_events": 0, "misses": 0,
              "association_failures": 0, "false_detections": 0, "duplicates": 0,
              "collision_truth_events": 0, "collision_matched_events": 0,
              "frequency_confusions": 0, "conditional_bits": 0, "bit_errors": 0,
              "conditional_words": 0, "word_errors": 0, "messages": 0,
              "message_errors": 0, "searched_samples": 0, "unknown_exposure_captures": 0}
    unresolved_metrics: set[str] = set()
    sample_rates = set()
    for result in results:
        validate_replay_result(result)
        capture = by_capture[result["capture_id"]]; session = capture["session_id"]
        totals["attempted_captures"] += 1
        if result.get("completed"): totals["completed_captures"] += 1
        else: totals["failed_captures"] += 1
        totals["searched_samples"] += int(result.get("searched_samples", 0))
        if not result["searched_exposure_known"]:
            totals["unknown_exposure_captures"] += 1
            if has_physical: unresolved_metrics.add("false_alarms_per_second")
        sample_rates.add(float(capture["sample_rate_hz"]))
        matching = result.get("matching")
        if matching is None:
            if has_physical:
                if corpus_root is None:
                    unresolved_metrics.update(("detection_probability", "conditional_ber", "word_error_rate", "message_per"))
                else:
                    try:
                        event_path = resolve_hash_artifact(corpus_root, capture["transmitter_event_log"],
                            capture["transmitter_event_log_sha256"], "transmitter event log")
                        event_doc = load_json(event_path); validate_event_document(event_doc, capture)
                        event_count = len(event_doc["events"])
                        totals["truth_events"] += event_count; totals["misses"] += event_count
                        clustered["detection"].setdefault(session, []).extend([0.0] * event_count)
                        if event_count:
                            totals["messages"] += 1; totals["message_errors"] += 1
                            clustered["message_correct"].setdefault(session, []).append(0.0)
                    except (ValueError, OSError, json.JSONDecodeError, TypeError):
                        unresolved_metrics.update(("detection_probability", "conditional_ber", "word_error_rate", "message_per"))
                continue
            # A signal-bearing capture without evaluable truth is a failed message,
            # never silently omitted from the message denominator.
            if capture["capture_class"] != "negative_control":
                totals["messages"] += 1; totals["message_errors"] += 1
                clustered["message_correct"].setdefault(session, []).append(0.0)
            continue
        events = int(matching["event_count"]); matched = matching["matched"]
        if is_negative_control(capture) and events != 0:
            raise ValueError("negative/noise control result contains transmitter events")
        if not is_negative_control(capture) and events == 0:
            raise ValueError("signal-bearing result lacks its single-message events")
        false = len(matching["false_detection_ids"]); misses = len(matching["miss_event_ids"])
        associations = len(matching["association_failure_event_ids"])
        duplicates = len(matching["duplicate_detection_ids"])
        totals["truth_events"] += events; totals["matched_events"] += len(matched)
        totals["misses"] += misses; totals["association_failures"] += associations
        totals["false_detections"] += false; totals["duplicates"] += duplicates
        totals["collision_truth_events"] += matching["collision_truth_count"]
        totals["collision_matched_events"] += matching["collision_matched_count"]
        clustered["detection"].setdefault(session, []).extend(
            [1.0] * len(matched) + [0.0] * (misses + associations))
        capture_message_error = misses > 0 or associations > 0
        for item in matched:
            errors = int(item["bit_errors"]); totals["conditional_bits"] += 32
            totals["bit_errors"] += errors; totals["conditional_words"] += 1
            totals["word_errors"] += int(item["word_error"])
            totals["frequency_confusions"] += int(not item["frequency_correct"])
            capture_message_error |= bool(item["word_error"]) or not item["frequency_correct"]
            clustered["bit_correct"].setdefault(session, []).extend(
                [1.0] * (32-errors) + [0.0] * errors)
            clustered["word_correct"].setdefault(session, []).append(0.0 if item["word_error"] else 1.0)
        if events:
            totals["messages"] += 1; totals["message_errors"] += int(capture_message_error)
            clustered["message_correct"].setdefault(session, []).append(0.0 if capture_message_error else 1.0)
    confidence = profile["statistics"]["confidence_level"]
    def error_metric(correct_name: str, errors: int, denominator: int) -> dict[str, Any]:
        interval = cluster_interval(clustered[correct_name], confidence)
        return {"numerator": errors, "denominator": denominator,
                "estimate": errors/denominator if denominator else None,
                "confidence_interval": ([1.0-interval["interval"][1], 1.0-interval["interval"][0]]
                                        if denominator else [None, None]),
                "confidence_method": interval.get("method"), "confidence_level": confidence,
                "cluster_unit": "capture_session", "session_count": interval["session_count"]}
    detection = cluster_interval(clustered["detection"], confidence)
    exposure_seconds = sum(int(r.get("searched_samples", 0)) /
                           by_capture[r["capture_id"]]["sample_rate_hz"] for r in results)
    false_count = totals["false_detections"]
    far_upper = poisson_count_upper(false_count, exposure_seconds, confidence) if exposure_seconds else None
    metrics = {
        "detection_probability": {"numerator": totals["matched_events"],
          "denominator": totals["truth_events"], "estimate": detection["estimate"],
          "confidence_interval": detection["interval"], "confidence_method": detection.get("method"),
          "confidence_level": confidence, "cluster_unit": "capture_session", "session_count": detection["session_count"]},
        "conditional_ber": error_metric("bit_correct", totals["bit_errors"], totals["conditional_bits"]),
        "word_error_rate": error_metric("word_correct", totals["word_errors"], totals["conditional_words"]),
        "message_per": error_metric("message_correct", totals["message_errors"], totals["messages"]),
        "false_alarms_per_second": {"numerator": false_count, "denominator_seconds": exposure_seconds,
          "estimate": false_count/exposure_seconds if exposure_seconds else None,
          "upper_confidence_bound": far_upper,
          "confidence_method": "conservative Poisson count bound including zero-event case",
          "confidence_level": confidence, "session_count": len({by_capture[r["capture_id"]]["session_id"] for r in results})}}
    gates = {"execution_completion": totals["failed_captures"] == 0,
             "detection_probability": bool(metrics["detection_probability"]["confidence_interval"][0] is not None and
             metrics["detection_probability"]["confidence_interval"][0] >= profile["statistics"]["thresholds"]["detection_probability_lower_bound"]),
             "conditional_ber": bool(metrics["conditional_ber"]["confidence_interval"][1] is not None and
             metrics["conditional_ber"]["confidence_interval"][1] <= profile["statistics"]["thresholds"]["conditional_ber_upper_bound"]),
             "message_per": bool(metrics["message_per"]["confidence_interval"][1] is not None and
             metrics["message_per"]["confidence_interval"][1] <= profile["statistics"]["thresholds"]["message_per_upper_bound"]),
             "false_alarms_per_second": bool(metrics["false_alarms_per_second"]["upper_confidence_bound"] is not None and
             metrics["false_alarms_per_second"]["upper_confidence_bound"] <= profile["statistics"]["thresholds"]["false_alarms_per_second_upper_bound"]),
             "correlation": bool(correlation_report is not None and correlation_report.get("status") == "PASS")}
    for metric in unresolved_metrics: gates[metric] = False
    if has_physical and correlation_report is not None:
        validate_correlation_report(correlation_report)
        if correlation_report["profile_sha256"] != digest_bytes(canonical(profile)) or correlation_report["dataset_manifest_sha256"] != expected_bindings["dataset_manifest_sha256"]:
            raise ValueError("correlation report is not bound to the physical aggregate inputs")
    status = "UNAVAILABLE_DEFERRED" if has_synthetic else ("PASS" if all(gates.values()) else "FAIL")
    return {"schema": "graphx.fhss.phase4-aggregate-report.v1",
            "scope": "synthetic_dry_run_infrastructure_only" if has_synthetic else "physical_held_out",
            "status": status,
            "claim": "synthetic dry-run infrastructure only; no physical claim" if has_synthetic else "recorded-IQ/HIL engineering validation only if a frozen physical corpus passes",
            "profile_sha256": digest_bytes(canonical(profile)),
            "collection_sha256": expected_bindings["dataset_manifest_sha256"] if has_physical else digest_bytes(canonical(collection)),
            "journal_results_sha256": digest_bytes(canonical(results)),
            "totals": totals, "metrics": metrics, "gates": gates,
            "all_executions_completed": totals["failed_captures"] == 0,
            "failed_runs_retained": totals["failed_captures"],
            "unresolved_metrics": sorted(unresolved_metrics),
            "limitations": ["Metrics are valid only for the governed collection and declared reference planes."]}


def record_capture_attempt(journal: dict[str, Any], journal_path: Path,
                           profile: dict[str, Any], capture: dict[str, Any], corpus_root: Path,
                           receiver: Path, graph: Path, plugins: Path, work_root: Path) -> None:
    """Durably record exactly one attempt, including every pre-execution failure."""
    if any(x.get("capture_id") == capture["capture_id"] for x in journal["results"]):
        return
    result = {"schema": "graphx.fhss.phase4-replay-result.v1",
              "capture_id": capture["capture_id"], "session_id": capture["session_id"],
              "status": "attempting", "attempt_number": 1, "command": [],
              "return_status": None, "receiver_completed": False, "completed": False,
              "elapsed_seconds": 0.0,
              "hashes": {"iq_sha256": capture["iq_sha256"],
                "receiver_sha256": digest_file(receiver), "base_graph_sha256": digest_file(graph),
                "effective_graph_sha256": None, "effective_config_sha256": None,
                "plugin_manifest_sha256": plugin_manifest(plugins)["sha256"],
                "stdout_sha256": None, "stderr_sha256": None},
              "searched_samples": 0, "searched_exposure_known": False, "matching": None,
              "allocation": {"reported_node_count": 0, "sum_high_water_bytes": 0,
                             "max_high_water_bytes": 0}, "failure": None}
    journal["results"].append(result); atomic_json(journal_path, journal)
    result_index = len(journal["results"])-1
    try:
        iq = resolve_and_verify(capture, corpus_root)
        result = replay_capture(profile, capture, iq, receiver, graph, plugins,
                                corpus_root, work_root / capture["capture_id"])
    except Exception as error:
        result = journal["results"][result_index]
        result["status"] = "failed"; result["completed"] = False
        result["failure"] = {"stage": "pre_execution", "kind": type(error).__name__,
                             "message": str(error)}
    result["attempt_number"] = 1
    validate_replay_result(result)
    journal["results"][result_index] = result; atomic_json(journal_path, journal)


def finalize_interrupted_attempts(journal: dict[str, Any], journal_path: Path) -> None:
    changed = False
    for result in journal["results"]:
        if result.get("status") == "attempting":
            result["status"] = "failed"; result["completed"] = False
            result["failure"] = {"stage": "interrupted_attempt", "kind": "InterruptedAttempt",
                                 "message": "prior durable attempt did not reach a terminal result"}
            validate_replay_result(result); changed = True
    if changed: atomic_json(journal_path, journal)


def journal_bindings(profile_path: Path, collection_path: Path, receiver: Path,
                     graph: Path, plugins: Path) -> dict[str, str]:
    return {"profile_file_sha256": digest_file(profile_path),
            "dataset_manifest_sha256": digest_file(collection_path),
            "tool_sha256": digest_file(Path(__file__)),
            "receiver_sha256": digest_file(receiver), "graph_sha256": digest_file(graph),
            "plugin_manifest_sha256": plugin_manifest(plugins)["sha256"]}


def validate_journal(journal: Any, collection: dict[str, Any], selected: list[dict[str, Any]],
                     bindings: dict[str, str], require_selected_set: bool) -> None:
    exact(journal, {"schema", "bindings", "results"},
          {"results_sha256", "all_executions_completed"}, "replay journal")
    if journal["schema"] != "graphx.fhss.phase4-replay-journal.v2":
        raise ValueError("unsupported replay journal schema")
    exact(journal["bindings"], set(bindings), set(), "journal bindings")
    if journal["bindings"] != bindings: raise ValueError("journal immutable binding mismatch")
    if not isinstance(journal["results"], list): raise ValueError("journal results must be array")
    selected_by_id = {c["capture_id"]: c for c in selected}
    if len(selected_by_id) != len(selected): raise ValueError("duplicate selected capture id")
    ids = []
    for result in journal["results"]:
        validate_replay_result(result); capture_id = result["capture_id"]; ids.append(capture_id)
        if capture_id not in selected_by_id: raise ValueError("journal result is not a selected physical held-out capture")
        capture = selected_by_id[capture_id]
        if result["session_id"] != capture["session_id"] or capture["partition"] != "held_out" or capture["capture_class"] not in PHYSICAL_CAPTURE_CLASSES:
            raise ValueError("journal capture/session/partition/class binding mismatch")
        expected_hashes = {"iq_sha256": capture["iq_sha256"],
                           "receiver_sha256": bindings["receiver_sha256"],
                           "base_graph_sha256": bindings["graph_sha256"],
                           "plugin_manifest_sha256": bindings["plugin_manifest_sha256"]}
        if any(result["hashes"][key] != value for key, value in expected_hashes.items()):
            raise ValueError("journal result artifact binding mismatch")
        if result["status"] == "completed" and any(result["hashes"][key] is None for key in
                ("effective_graph_sha256", "effective_config_sha256", "stdout_sha256", "stderr_sha256")):
            raise ValueError("completed journal result lacks execution provenance")
    if len(ids) != len(set(ids)): raise ValueError("duplicate journal capture/attempt")
    if require_selected_set and set(ids) != set(selected_by_id):
        raise ValueError("journal selected capture set is incomplete")
    if "results_sha256" in journal and journal["results_sha256"] != digest_bytes(canonical(journal["results"])):
        raise ValueError("journal results hash mismatch")
    if journal.get("all_executions_completed") is True and (set(ids) != set(selected_by_id) or
            not all(x["status"] == "completed" for x in journal["results"])):
        raise ValueError("journal falsely claims all executions completed")


def validate_freeze_manifest(profile: dict[str, Any], freeze_manifest: Any) -> None:
    exact(freeze_manifest, {"schema", "profile_contract_sha256",
          "dataset_manifest_sha256", "receiver_executable_sha256", "plugin_manifest_sha256",
          "independent_tool_sha256", "partition_sha256"}, set(), "freeze manifest")
    if freeze_manifest["schema"] != "graphx.fhss.phase4-freeze-manifest.v1":
        raise ValueError("freeze manifest schema mismatch")
    contract = json.loads(json.dumps(profile)); contract["freeze"]["profile_sha256"] = None
    contract["freeze"]["freeze_manifest_sha256"] = None
    bindings = {"profile_contract_sha256": digest_bytes(canonical(contract)),
                "dataset_manifest_sha256": profile["freeze"]["dataset_manifest_sha256"],
                "receiver_executable_sha256": profile["freeze"]["receiver_executable_sha256"],
                "plugin_manifest_sha256": profile["freeze"]["plugin_manifest_sha256"],
                "independent_tool_sha256": profile["freeze"]["independent_tool_sha256"],
                "partition_sha256": digest_bytes(canonical(profile["partitions"]))}
    if profile["freeze"]["profile_sha256"] != bindings["profile_contract_sha256"] or any(
            freeze_manifest[k] != v for k, v in bindings.items()):
        raise ValueError("freeze manifest binding mismatch")


def replay_collection(profile_path: Path, collection_path: Path, corpus_root: Path,
                      receiver: Path, graph: Path, plugins: Path, journal_path: Path,
                      work_root: Path) -> dict[str, Any]:
    profile = load_json(profile_path); validate_profile(profile)
    if profile["status"] != "frozen_for_heldout":
        raise ValueError("held-out replay refuses a profile that was not frozen before evidence")
    collection = load_json(collection_path); validate_collection(collection)
    if digest_file(Path(__file__)) != profile["freeze"]["independent_tool_sha256"]:
        raise ValueError("replay tool differs from frozen profile")
    freeze_manifest_path = resolve_hash_artifact(profile_path.parent,
        profile["freeze"]["freeze_manifest_path"], profile["freeze"]["freeze_manifest_sha256"], "freeze manifest")
    freeze_manifest = load_json(freeze_manifest_path)
    validate_freeze_manifest(profile, freeze_manifest)
    readiness_result = readiness(profile, collection, corpus_root)
    if readiness_result["status"] != "READY_FOR_FROZEN_HELD_OUT_REPLAY":
        raise ValueError("held-out replay prerequisites failed: " + "; ".join(readiness_result["failures"]))
    if digest_file(graph) != profile["replay"]["receiver_graph_sha256"]:
        raise ValueError("receiver graph differs from frozen profile")
    if digest_file(receiver) != profile["freeze"]["receiver_executable_sha256"]:
        raise ValueError("receiver executable differs from frozen profile")
    if plugin_manifest(plugins)["sha256"] != profile["freeze"]["plugin_manifest_sha256"]:
        raise ValueError("plugin set differs from frozen profile")
    if digest_file(collection_path) != profile["freeze"]["dataset_manifest_sha256"]:
        raise ValueError("dataset manifest differs from frozen profile")
    bindings = journal_bindings(profile_path, collection_path, receiver, graph, plugins)
    journal = load_json(journal_path) if journal_path.is_file() else {
        "schema": "graphx.fhss.phase4-replay-journal.v2", "bindings": bindings, "results": []}
    selected = [c for c in collection["captures"] if c["partition"] == "held_out" and c["capture_class"] in PHYSICAL_CAPTURE_CLASSES]
    validate_journal(journal, collection, selected, bindings, False)
    finalize_interrupted_attempts(journal, journal_path)
    validate_journal(journal, collection, selected, bindings, False)
    prior = {x["capture_id"] for x in journal["results"]}
    if not selected: raise ValueError("frozen collection contains no held-out captures")
    for capture in selected:
        if capture["capture_id"] in prior:
            continue  # immutable resume, never retry or replace an attempted capture
        record_capture_attempt(journal, journal_path, profile, capture, corpus_root,
                               receiver, graph, plugins, work_root)
    journal["results_sha256"] = digest_bytes(canonical(journal["results"]))
    journal["all_executions_completed"] = all(x["completed"] for x in journal["results"])
    validate_journal(journal, collection, selected, bindings, True)
    atomic_json(journal_path, journal); return journal


def readiness(profile: dict[str, Any], collection: dict[str, Any] | None,
              corpus_root: Path | None) -> dict[str, Any]:
    classes = ("conducted", "channel_emulator", "ota")
    counts = {c: {"sessions": 0, "captures": 0, "searched_seconds": 0.0} for c in classes}
    failures = []
    if collection is None:
        failures.append("no independently recorded dataset manifest is available")
    else:
        validate_collection(collection)
        if profile["status"] == "frozen_for_heldout":
            if collection["status"] != "frozen_held_out": failures.append("collection is not frozen_held_out")
            declared = {"development": set(profile["partitions"]["development_session_ids"]),
                        "validation": set(profile["partitions"]["validation_session_ids"]),
                        "held_out": set(profile["partitions"]["held_out_session_ids"])}
            actual = {p: {s["session_id"] for s in collection["sessions"] if s["partition"] == p} for p in PARTITIONS}
            if declared != actual: failures.append("profile and collection session partitions differ")
        for c in classes:
            counts[c]["sessions"] = len({s["session_id"] for s in collection["sessions"] if s["capture_class"] == c and s["partition"] == "held_out"})
            selected = [x for x in collection["captures"] if x["capture_class"] == c and x["partition"] == "held_out"]
            counts[c]["captures"] = len(selected)
            counts[c]["searched_seconds"] = sum(x["byte_length"] / FORMATS[x["sample_format"]][1] / x["sample_rate_hz"] for x in selected)
        if corpus_root is None: failures.append("external corpus root was not supplied")
        else:
            for capture in collection["captures"]:
                try:
                    resolve_and_verify(capture, corpus_root)
                    if capture["transmitter_event_log"] is not None:
                        event_path = resolve_hash_artifact(corpus_root, capture["transmitter_event_log"], capture["transmitter_event_log_sha256"], "transmitter event log")
                        event_doc = load_json(event_path)
                        validate_event_document(event_doc, capture)
                    for artifact in capture["derived_artifacts"]:
                        resolve_hash_artifact(corpus_root, artifact["relative_path"], artifact["sha256"], "derived artifact")
                        resolve_hash_artifact(corpus_root, artifact["provenance_relative_path"], artifact["provenance_sha256"], "derived provenance")
                    if capture["channel_emulator"] is not None:
                        emulator = capture["channel_emulator"]
                        resolve_hash_artifact(corpus_root, emulator["configuration_relative_path"], emulator["configuration_sha256"], "channel-emulator configuration")
                except (ValueError, FileNotFoundError) as error: failures.append(str(error))
            for calibration in collection["calibrations"]:
                try: resolve_hash_artifact(corpus_root, calibration["artifact_relative_path"], calibration["artifact_sha256"], "calibration artifact")
                except (ValueError, FileNotFoundError) as error: failures.append(str(error))
            for session in collection["sessions"]:
                authorization = session["authorization"]
                if authorization["status"] == "approved":
                    try: resolve_hash_artifact(corpus_root, authorization["artifact_relative_path"], authorization["artifact_sha256"], "authorization artifact")
                    except (ValueError, FileNotFoundError) as error: failures.append(str(error))
    minimums = profile["capture_program"]
    for c in classes:
        if counts[c]["sessions"] < minimums["minimum_independent_sessions_per_class"]:
            failures.append(f"{c}: insufficient independent sessions")
        if counts[c]["captures"] < minimums["minimum_captures_per_class"]:
            failures.append(f"{c}: insufficient captures")
        if counts[c]["searched_seconds"] < minimums["minimum_searched_seconds_per_class"]:
            failures.append(f"{c}: insufficient searched exposure")
        required = set(minimums[c]["required_cases"])
        present = {tag for capture in (collection or {}).get("captures", [])
                   if capture["capture_class"] == c and capture["partition"] == "held_out"
                   for tag in capture["scenario_tags"]}
        missing_cases = sorted(required-present)
        if missing_cases: failures.append(f"{c}: missing required scenarios {missing_cases}")
    return {"schema": "graphx.fhss.phase4-readiness.v1", "tool_version": VERSION,
            "claim": "infrastructure readiness only; not recorded-IQ/HIL validation",
            "status": "UNAVAILABLE_DEFERRED" if profile["status"] == "synthetic_only_infrastructure_physical_validation_unavailable" else ("BLOCKED" if failures else "READY_FOR_FROZEN_HELD_OUT_REPLAY"),
            "counts": counts, "held_out_replay_count": 0,
            "simulation_hardware_paired_point_count": 0,
            "failures": sorted(set(failures)),
            "criteria": {"A": "PASS", "B": "UNAVAILABLE_DEFERRED",
                         "C": "UNAVAILABLE_DEFERRED", "D": "UNAVAILABLE_DEFERRED",
                         "E": "UNAVAILABLE_DEFERRED", "F": "UNAVAILABLE_DEFERRED",
                         "G": "UNAVAILABLE_DEFERRED", "H": "PASS", "I": "PASS"}}


def validate_external(profile_path: Path, collection_path: Path | None,
                      corpus_root: Path | None) -> dict[str, Any]:
    profile = load_json(profile_path); validate_profile(profile)
    collection = load_json(collection_path) if collection_path else None
    return readiness(profile, collection, corpus_root)


def verify_infrastructure_artifacts(profile_path: Path, readiness_path: Path,
                                    correlation_path: Path, repo_root: Path) -> None:
    profile = load_json(profile_path); validate_profile(profile)
    if digest_file(Path(__file__)) != profile["freeze"]["independent_tool_sha256"]:
        raise ValueError("profile tool hash differs from verifier")
    expected = readiness(profile, None, None)
    expected_bytes = (json.dumps(expected, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()
    if readiness_path.read_bytes() != expected_bytes:
        raise ValueError("readiness report is not byte-identical to deterministic regeneration")
    correlation = load_json(correlation_path)
    validate_correlation_report(correlation)
    exact(correlation, {"schema", "status", "profile_sha256", "dataset_manifest_sha256",
          "paired_points", "metrics", "discrepancies", "limitations"}, set(), "correlation report")
    if correlation["schema"] != "graphx.fhss.phase4-correlation-report.v1" or correlation["status"] != "UNAVAILABLE_DEFERRED":
        raise ValueError("correlation report makes an unsupported claim")
    if correlation["profile_sha256"] != digest_file(profile_path) or correlation["dataset_manifest_sha256"] is not None:
        raise ValueError("correlation report binding mismatch")
    if correlation["paired_points"] or correlation["metrics"]:
        raise ValueError("correlation report invents paired evidence")
    phase3 = profile["extends_phase3"]
    paths = {"profile_sha256": "libdsp/config/fhss_phase3_validation_profile_v7.json",
             "raw_sha256": "libdsp/config/fhss_phase3_evaluation_raw_v7.json",
             "report_sha256": "libdsp/config/fhss_phase3_characterization_report_v7.json",
             "freeze_manifest_sha256": "libdsp/config/fhss_phase3_freeze_manifest_v7.json",
             "inventory_sha256": "libdsp/config/fhss_phase3_validation_inventory_v7.json"}
    for key, relative in paths.items():
        if digest_file(repo_root / relative) != phase3[key]:
            raise ValueError(f"Phase 3 immutable artifact changed: {relative}")


def correlate(profile: dict[str, Any], paired: dict[str, Any],
              collection: dict[str, Any] | None = None,
              freeze_manifest: dict[str, Any] | None = None,
              journal: dict[str, Any] | None = None,
              aggregate: dict[str, Any] | None = None,
              evidence_hashes: dict[str, str] | None = None) -> dict[str, Any]:
    validate_profile(profile)
    if profile["status"] != "frozen_for_heldout":
        raise ValueError("synthetic-only/unfrozen profile cannot produce hardware correlation")
    criteria = profile["correlation"]["agreement_criteria"]
    if criteria["status"] != "frozen_before_held_out":
        raise ValueError("correlation refuses provisional/unfrozen agreement criteria")
    if collection is None or freeze_manifest is None or journal is None or aggregate is None or evidence_hashes is None:
        raise ValueError("correlation requires governed physical collection, freeze, journal, and aggregate artifacts")
    validate_collection(collection)
    validate_freeze_manifest(profile, freeze_manifest)
    exact(evidence_hashes, {"profile_file_sha256", "dataset_manifest_sha256", "freeze_manifest_sha256",
          "journal_sha256", "aggregate_sha256"}, set(), "correlation artifact hashes")
    if any(not isinstance(value, str) or len(value) != 64 for value in evidence_hashes.values()):
        raise ValueError("correlation artifact hashes must be SHA-256 strings")
    exact(paired, {"schema", "dataset_manifest_sha256", "freeze_manifest_sha256",
          "replay_evidence", "pairs"}, set(), "paired correlation input")
    if paired["schema"] != "graphx.fhss.phase4-correlation-pairs.v1" or not isinstance(paired["pairs"], list) or not paired["pairs"]:
        raise ValueError("nonempty correlation pair input required")
    if not isinstance(paired["dataset_manifest_sha256"], str) or len(paired["dataset_manifest_sha256"]) != 64:
        raise ValueError("correlation pairs require dataset manifest hash")
    dataset_hash = evidence_hashes["dataset_manifest_sha256"]
    freeze_hash = evidence_hashes["freeze_manifest_sha256"]
    if paired["dataset_manifest_sha256"] != dataset_hash or profile["freeze"]["dataset_manifest_sha256"] != dataset_hash:
        raise ValueError("correlation dataset binding mismatch")
    if paired["freeze_manifest_sha256"] != freeze_hash or profile["freeze"]["freeze_manifest_sha256"] != freeze_hash:
        raise ValueError("correlation freeze-manifest binding mismatch")
    exact(paired["replay_evidence"], {"journal_sha256", "aggregate_sha256", "all_executions_completed",
          "profile_contract_sha256", "dataset_manifest_sha256", "physical_capture_ids"}, set(), "replay evidence")
    replay = paired["replay_evidence"]
    if replay["journal_sha256"] != evidence_hashes["journal_sha256"] or replay["aggregate_sha256"] != evidence_hashes["aggregate_sha256"] or replay["all_executions_completed"] is not True or replay["profile_contract_sha256"] != profile["freeze"]["profile_sha256"] or replay["dataset_manifest_sha256"] != dataset_hash:
        raise ValueError("correlation replay evidence binding/completion mismatch")
    physical = {c["capture_id"]: c for c in collection["captures"]
                if c["partition"] == "held_out" and c["capture_class"] in PHYSICAL_CAPTURE_CLASSES}
    if set(replay["physical_capture_ids"]) != set(physical):
        raise ValueError("correlation replay capture set mismatch")
    bindings = {"profile_file_sha256": evidence_hashes["profile_file_sha256"],
                "dataset_manifest_sha256": dataset_hash,
                "tool_sha256": profile["freeze"]["independent_tool_sha256"],
                "receiver_sha256": profile["freeze"]["receiver_executable_sha256"],
                "graph_sha256": profile["replay"]["receiver_graph_sha256"],
                "plugin_manifest_sha256": profile["freeze"]["plugin_manifest_sha256"]}
    validate_journal(journal, collection, list(physical.values()), bindings, True)
    if journal.get("all_executions_completed") is not True:
        raise ValueError("correlation journal is not complete")
    expected_aggregate = aggregate_journal(profile, collection, journal, bindings)
    if aggregate != expected_aggregate:
        raise ValueError("correlation aggregate artifact is not reproducible from the bound journal")
    derived_sample_count = sum(x["searched_samples"] for x in journal["results"])
    derived_session_count = len({physical[x]["session_id"] for x in physical})
    rows, discrepancies = [], []
    allowed_classes = {"simulator_model_deficiency", "calibration_reference_plane_uncertainty",
                       "hardware_nonideality", "receiver_defect", "insufficient_evidence",
                       "unsupported_region"}
    ids = set()
    for pair in paired["pairs"]:
        exact(pair, {"pair_id", "scenario_id", "metric", "unit", "reference_plane",
              "simulation", "hardware", "discrepancy_classification"}, set(), "correlation pair")
        pair_id = identifier(pair["pair_id"], "pair_id")
        if pair_id in ids: raise ValueError("duplicate correlation pair id")
        ids.add(pair_id); identifier(pair["scenario_id"], "scenario_id")
        metric = pair["metric"]
        if metric not in criteria["metrics"]: raise ValueError("pair metric lacks frozen criterion")
        criterion = criteria["metrics"][metric]
        if pair["unit"] != criterion["unit"] or not pair["reference_plane"]:
            raise ValueError("paired point has incompatible unit/reference plane")
        values = []
        for side_name in ("simulation", "hardware"):
            side = pair[side_name]
            required = {"value", "standard_uncertainty", "artifact_sha256", "unit",
                        "reference_plane", "sample_count", "session_count", "provenance", "capture_ids"}
            exact(side, required, set(), side_name)
            if side["unit"] != pair["unit"] or side["reference_plane"] != pair["reference_plane"]:
                raise ValueError("simulation/hardware units or reference planes differ")
            values.append(finite(side["value"], f"{side_name} value"))
            finite(side["standard_uncertainty"], f"{side_name} uncertainty", 0.0)
            if not isinstance(side["artifact_sha256"], str) or len(side["artifact_sha256"]) != 64:
                raise ValueError("invalid paired artifact hash")
            for count_name in ("sample_count", "session_count"):
                if isinstance(side[count_name], bool) or not isinstance(side[count_name], int) or side[count_name] <= 0:
                    raise ValueError("paired evidence counts must be positive")
            if side_name == "hardware":
                if side["provenance"] != "independently_recorded_physical" or set(side["capture_ids"]) != set(physical):
                    raise ValueError("hardware correlation side is synthetic or not governed held-out evidence")
                if side["sample_count"] != derived_sample_count or side["session_count"] != derived_session_count:
                    raise ValueError("hardware correlation evidence counts are not derived from the bound journal")
            elif side["provenance"] != "independent_simulation":
                raise ValueError("simulation correlation provenance invalid")
        residual = values[1]-values[0]
        combined = math.hypot(pair["simulation"]["standard_uncertainty"], pair["hardware"]["standard_uncertainty"])
        margin = criterion["absolute_margin"]; passed = abs(residual) <= margin
        classification = pair["discrepancy_classification"]
        if passed and classification is not None: raise ValueError("passing point cannot claim discrepancy")
        if not passed and classification not in allowed_classes: raise ValueError("failed point requires discrepancy classification")
        row = {"pair_id": pair_id, "scenario_id": pair["scenario_id"], "metric": metric,
               "unit": pair["unit"], "reference_plane": pair["reference_plane"],
               "simulation_value": values[0], "hardware_value": values[1],
               "residual": residual, "combined_standard_uncertainty": combined,
               "expanded_uncertainty_k2": 2.0*combined, "absolute_margin": margin,
               "pass": passed, "discrepancy_classification": classification,
               "simulation_artifact_sha256": pair["simulation"]["artifact_sha256"],
               "hardware_artifact_sha256": pair["hardware"]["artifact_sha256"],
               "simulation_sample_count": pair["simulation"]["sample_count"],
               "hardware_sample_count": pair["hardware"]["sample_count"],
               "hardware_session_count": pair["hardware"]["session_count"]}
        rows.append(row)
        if not passed: discrepancies.append({"pair_id": pair_id, "classification": classification,
                                              "residual": residual, "margin": margin})
    rows.sort(key=lambda x: x["pair_id"]); discrepancies.sort(key=lambda x: x["pair_id"])
    report = {"schema": "graphx.fhss.phase4-correlation-report.v1",
            "status": "PASS" if not discrepancies else "FAIL",
            "profile_sha256": digest_bytes(canonical(profile)),
            "dataset_manifest_sha256": paired["dataset_manifest_sha256"],
            "paired_points": rows, "metrics": sorted({x["metric"] for x in rows}),
            "discrepancies": discrepancies,
            "limitations": ["Engineering correlation only for declared compatible paired points."]}
    validate_correlation_report(report)
    return report


def correlate_files(profile_path: Path, paired_path: Path, collection_path: Path,
                    freeze_manifest_path: Path, journal_path: Path,
                    aggregate_path: Path) -> dict[str, Any]:
    """Load immutable evidence and bind hashes to the exact serialized file bytes."""
    evidence_hashes = {"profile_file_sha256": digest_file(profile_path),
        "dataset_manifest_sha256": digest_file(collection_path),
        "freeze_manifest_sha256": digest_file(freeze_manifest_path),
        "journal_sha256": digest_file(journal_path), "aggregate_sha256": digest_file(aggregate_path)}
    return correlate(load_json(profile_path), load_json(paired_path), load_json(collection_path),
        load_json(freeze_manifest_path), load_json(journal_path), load_json(aggregate_path), evidence_hashes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    val = sub.add_parser("validate")
    val.add_argument("--profile", type=Path, required=True)
    val.add_argument("--collection", type=Path)
    val.add_argument("--corpus-root", type=Path)
    val.add_argument("--output", type=Path)
    conv = sub.add_parser("convert")
    conv.add_argument("--input", type=Path, required=True); conv.add_argument("--output", type=Path, required=True)
    conv.add_argument("--input-format", choices=FORMATS, required=True)
    conv.add_argument("--output-format", choices=("cf32_le", "cf64_le"), required=True)
    conv.add_argument("--input-sha256"); conv.add_argument("--provenance", type=Path, required=True)
    scan = sub.add_parser("scan-graph"); scan.add_argument("--graph", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--profile", type=Path, required=True)
    verify.add_argument("--readiness", type=Path, required=True)
    verify.add_argument("--correlation", type=Path, required=True)
    verify.add_argument("--repo-root", type=Path, required=True)
    replay = sub.add_parser("replay")
    for name in ("profile", "collection", "corpus-root", "receiver", "graph", "plugins", "journal", "work-root"):
        replay.add_argument(f"--{name}", type=Path, required=True)
    agg = sub.add_parser("aggregate")
    agg.add_argument("--profile", type=Path, required=True); agg.add_argument("--collection", type=Path, required=True)
    agg.add_argument("--journal", type=Path, required=True); agg.add_argument("--output", type=Path, required=True)
    agg.add_argument("--correlation-report", type=Path)
    agg.add_argument("--corpus-root", type=Path)
    corr = sub.add_parser("correlate")
    corr.add_argument("--profile", type=Path, required=True); corr.add_argument("--pairs", type=Path, required=True)
    corr.add_argument("--collection", type=Path, required=True); corr.add_argument("--freeze-manifest", type=Path, required=True)
    corr.add_argument("--journal", type=Path, required=True); corr.add_argument("--aggregate", type=Path, required=True)
    corr.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "validate":
            result = validate_external(args.profile, args.collection, args.corpus_root)
            if args.output: atomic_json(args.output, result)
            print(json.dumps(result, sort_keys=True))
            return 3 if result["status"] in {"BLOCKED", "UNAVAILABLE_DEFERRED"} else 0
        if args.command == "convert":
            result = convert_with_provenance(args.input, args.output, args.input_format,
                                             args.output_format, args.input_sha256,
                                             args.provenance)
            print(json.dumps(result, sort_keys=True)); return 0
        if args.command == "replay":
            result = replay_collection(args.profile, args.collection, args.corpus_root,
                                       args.receiver, args.graph, args.plugins,
                                       args.journal, args.work_root)
            print(json.dumps(result, sort_keys=True)); return 0 if result["all_executions_completed"] else 4
        if args.command == "verify":
            verify_infrastructure_artifacts(args.profile, args.readiness,
                                            args.correlation, args.repo_root)
            print("Phase 4 infrastructure report verification: PASS"); return 0
        if args.command == "aggregate":
            profile = load_json(args.profile)
            bindings = {"profile_file_sha256": digest_file(args.profile),
                "dataset_manifest_sha256": digest_file(args.collection), "tool_sha256": digest_file(Path(__file__)),
                "receiver_sha256": profile["freeze"]["receiver_executable_sha256"],
                "graph_sha256": profile["replay"]["receiver_graph_sha256"],
                "plugin_manifest_sha256": profile["freeze"]["plugin_manifest_sha256"]}
            result = aggregate_journal(profile, load_json(args.collection), load_json(args.journal), bindings,
                load_json(args.correlation_report) if args.correlation_report else None, args.corpus_root)
            atomic_json(args.output, result); print(json.dumps(result, sort_keys=True)); return 0
        if args.command == "correlate":
            result = correlate_files(args.profile, args.pairs, args.collection, args.freeze_manifest,
                                     args.journal, args.aggregate)
            atomic_json(args.output, result); print(json.dumps(result, sort_keys=True)); return 0 if result["status"] == "PASS" else 5
        scan_truth_free_graph(load_json(args.graph)); print("truth-isolation scan: PASS"); return 0
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=os.sys.stderr); return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
