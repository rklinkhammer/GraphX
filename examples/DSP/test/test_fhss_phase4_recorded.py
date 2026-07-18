#!/usr/bin/env python3
import copy
import importlib.util
import json
import math
import os
import struct
import tempfile
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "examples/DSP/tools/fhss_phase4_recorded.py"
SPEC = importlib.util.spec_from_file_location("phase4", TOOL)
phase4 = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(phase4)
PROFILE_PATH = ROOT / "libdsp/config/fhss_phase4_validation_profile_v1.json"
GRAPH_PATH = ROOT / "libdsp/config/fhss_phase2_binary_iq_receiver.json"


def power(kind="not_applicable"):
    return {"commanded_dbm": None, "measured_dbm": None,
            "uncertainty_db": None, "measurement_kind": kind}


def capture(raw: bytes):
    return {
        "capture_id": "cap-1", "session_id": "session-1", "capture_class": "synthetic_dry_run",
        "partition": "development", "relative_iq_path": "cap-1.cf32",
        "iq_sha256": phase4.digest_bytes(raw), "byte_length": len(raw),
        "sample_format": "cf32_le", "sample_rate_hz": 500000000.0,
        "center_frequency_hz": 1240000000.0, "start_time_utc": "2026-01-01T00:00:00Z",
        "time_source_quality": {"kind": "unverified_test_fixture", "uncertainty_seconds": None},
        "waveform_profile_id": "architecture-current", "transmitter_event_log": None,
        "transmitter_event_log_sha256": None, "equipment_ids": [],
        "calibration_record_ids": [], "reference_plane": "not_applicable_test_fixture",
        "wanted_power": power(), "blocker_power": power(), "cfo_hz": None,
        "sample_clock_offset_ppm": None, "agc_gain": None, "clipping_observed": False,
        "rf_path": [], "receiver_settings": {"gain_mode": "not_applicable", "gain_db": None,
          "agc_enabled": False, "overload_observed": False},
        "noise_floor": {"measured_dbm_hz": None, "uncertainty_db": None, "method": "not_applicable"},
        "channel_emulator": None, "ota": None, "environment": {"temperature_c": None,
          "humidity_percent": None, "motion": "none", "scenario_notes": "literal fixture"},
        "operator_tool_versions": {"capture_tool": "literal-fixture",
          "capture_tool_version": "1", "operator_id_policy": "none"},
        "rights": {"license": "test-only", "privacy": "none", "export_control": "none",
                   "retention": "ephemeral", "redistribution": "repository-test-only"},
        "parents": [], "derived_artifacts": [], "failure_or_exclusion": None
        ,"scenario_tags": ["literal_test_fixture"]
        ,"provenance": {"origin": "synthetic_dry_run", "producer": "literal-test-fixture", "hardware_involved": False}
    }


def collection(raw: bytes):
    return {"schema": "graphx.fhss.phase4-dataset-collection.v1", "version": 1,
            "dataset_id": "literal-test-fixture", "description": "literal bytes; not recorded evidence",
            "status": "development", "external_storage": {"resolver_kind": "explicit_root",
              "location_not_committed": True, "large_iq_in_git_prohibited": True},
            "equipment": [], "calibrations": [],
            "sessions": [{"session_id": "session-1", "capture_class": "synthetic_dry_run",
              "partition": "development", "independence_unit": "physical_capture_session",
              "start_time_utc": "2026-01-01T00:00:00Z", "end_time_utc": "2026-01-01T00:00:01Z",
              "equipment_reset": False, "authorization": {"status": "not_applicable",
                "scope": "no RF literal fixture", "artifact_sha256": None,
                "artifact_relative_path": None,
                "facility_boundary": "not applicable"},
              "capture_ids": ["cap-1"]}], "captures": [capture(raw)],
            "partition_policy": {"unit": "session", "adjacent_slice_partitioning": "prohibited"},
            "duplicate_policy": {"cross_partition_iq_reuse": "prohibited",
              "same_partition_iq_reuse": "requires_explicit_parent_relationship"},
            "rights": {"license": "test-only", "privacy": "none", "export_control": "none",
                       "retention": "ephemeral", "redistribution": "repository-test-only"}}


def replay_result(matching, completed=True, searched_samples=500000000):
    return {"schema": "graphx.fhss.phase4-replay-result.v1", "capture_id": "cap-1",
            "session_id": "session-1", "attempt_number": 1,
            "status": "completed" if completed else "failed", "command": [],
            "return_status": 0 if completed else None, "receiver_completed": completed,
            "completed": completed, "elapsed_seconds": 0.0,
            "hashes": {k: None for k in ("iq_sha256", "receiver_sha256", "base_graph_sha256",
              "effective_graph_sha256", "effective_config_sha256", "plugin_manifest_sha256",
              "stdout_sha256", "stderr_sha256")}, "searched_samples": searched_samples,
            "searched_exposure_known": True,
            "matching": matching, "allocation": {"reported_node_count": 0,
              "sum_high_water_bytes": 0, "max_high_water_bytes": 0},
            "failure": None if completed else {"stage": "pre_execution", "kind": "TestFailure", "message": "test"}}


def freeze_thresholds(profile):
    thresholds = profile["statistics"]["thresholds"]
    thresholds["status"] = "frozen_before_held_out"
    contract = {key: thresholds[key] for key in ("origin", "detection_probability_lower_bound",
      "conditional_ber_upper_bound", "message_per_upper_bound", "false_alarms_per_second_upper_bound")}
    thresholds["threshold_set_sha256"] = phase4.digest_bytes(phase4.canonical(contract))


class Phase4RecordedTest(unittest.TestCase):
    def setUp(self):
        self.raw = struct.pack("<ffff", 1.0, -2.0, 0.25, 3.5)
        self.profile = phase4.load_json(PROFILE_PATH)

    def test_profile_is_valid_and_not_frozen(self):
        phase4.validate_profile(self.profile)
        self.assertEqual(self.profile["freeze"]["held_out_status"], "not_frozen_physical_validation_unavailable")

    def test_frozen_profile_requires_complete_freeze_contract(self):
        profile = copy.deepcopy(self.profile); profile["status"] = "frozen_for_heldout"
        freeze_thresholds(profile); profile["correlation"]["agreement_criteria"]["status"] = "frozen_before_held_out"
        with self.assertRaisesRegex(ValueError, "freeze status"):
            phase4.validate_profile(profile)

    def test_frozen_and_synthetic_profiles_cannot_forge_policy_freeze(self):
        for policy in ("thresholds", "correlation"):
            profile = copy.deepcopy(self.profile); profile["status"] = "frozen_for_heldout"
            if policy == "thresholds": profile["correlation"]["agreement_criteria"]["status"] = "frozen_before_held_out"
            else: freeze_thresholds(profile)
            with self.assertRaisesRegex(ValueError, "threshold|correlation"):
                phase4.validate_profile(profile)
        synthetic = copy.deepcopy(self.profile); freeze_thresholds(synthetic)
        with self.assertRaisesRegex(ValueError, "provisional"):
            phase4.validate_profile(synthetic)

    def test_nested_profile_confidence_threshold_and_program_ranges_are_strict(self):
        for mutate in (
          lambda p: p["statistics"].update(confidence_level=1.0),
          lambda p: p["statistics"]["thresholds"].update(message_per_upper_bound=1.1),
          lambda p: p["capture_program"]["conducted"].update(wanted_power_dbm_range=[0, -1]),
          lambda p: p["replay"].update(process_timeout_seconds=100)):
            profile = copy.deepcopy(self.profile); mutate(profile)
            with self.assertRaises(ValueError): phase4.validate_profile(profile)

    def test_phase3_hashes_are_current(self):
        paths = {"profile_sha256": "fhss_phase3_validation_profile_v7.json",
                 "raw_sha256": "fhss_phase3_evaluation_raw_v7.json",
                 "report_sha256": "fhss_phase3_characterization_report_v7.json",
                 "freeze_manifest_sha256": "fhss_phase3_freeze_manifest_v7.json",
                 "inventory_sha256": "fhss_phase3_validation_inventory_v7.json"}
        for key, name in paths.items():
            self.assertEqual(phase4.digest_file(ROOT / "libdsp/config" / name),
                             self.profile["extends_phase3"][key])

    def test_committed_reports_reproduce_and_bind(self):
        phase4.verify_infrastructure_artifacts(
            PROFILE_PATH, ROOT/"libdsp/config/fhss_phase4_readiness_report_v1.json",
            ROOT/"libdsp/config/fhss_phase4_correlation_report_v1.json", ROOT)

    def test_missing_collection_is_blocked(self):
        report = phase4.readiness(self.profile, None, None)
        self.assertEqual(report["status"], "UNAVAILABLE_DEFERRED")
        self.assertTrue(all(x["captures"] == 0 for x in report["counts"].values()))

    def test_literal_fixture_resolves_but_program_remains_blocked(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); (root / "cap-1.cf32").write_bytes(self.raw)
            report = phase4.readiness(self.profile, collection(self.raw), root)
            self.assertEqual(report["status"], "UNAVAILABLE_DEFERRED")
            self.assertEqual(report["counts"]["conducted"]["captures"], 0)

    def test_readiness_rejects_count_complete_but_unverifiable_artifact_graph(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); doc = collection(self.raw); profile = copy.deepcopy(self.profile)
            profile["capture_program"]["minimum_independent_sessions_per_class"] = 1
            profile["capture_program"]["minimum_captures_per_class"] = 1
            profile["capture_program"]["minimum_searched_seconds_per_class"] = 1e-12
            doc["equipment"] = [{"equipment_id": "eq", "category": "receiver", "manufacturer": "test",
              "model": "test", "firmware": "1", "identifier_policy": "anonymized_stable",
              "calibration_record_ids": ["cal"]}]
            doc["calibrations"] = [{"calibration_id": "cal", "method": "test", "date_utc": "2026-01-01Z",
              "reference_plane": "input", "uncertainty_db": 1.0, "artifact_relative_path": "missing-cal.pdf",
              "artifact_sha256": "a"*64, "equipment_ids": ["eq"]}]
            doc["sessions"] = []; doc["captures"] = []
            for index, capture_class in enumerate(("conducted", "channel_emulator", "ota")):
                raw = struct.pack("<ff", float(index+1), 0.0); path = f"{capture_class}.cf32"; (root/path).write_bytes(raw)
                item = capture(raw); item.update({"capture_id": f"cap-{index}", "session_id": f"session-{index}",
                  "capture_class": capture_class, "partition": "held_out", "relative_iq_path": path,
                  "provenance": {"origin": "independently_recorded_physical", "producer": "test-metadata-only", "hardware_involved": True},
                  "equipment_ids": ["eq"], "calibration_record_ids": ["cal"],
                  "transmitter_event_log": f"missing-{index}.events.json", "transmitter_event_log_sha256": "b"*64,
                  "scenario_tags": profile["capture_program"][capture_class]["required_cases"]})
                if capture_class == "channel_emulator": item["channel_emulator"] = {"equipment_id": "eq", "model": "test", "firmware": "1", "configuration_relative_path": "emulator.json", "configuration_sha256": "c"*64, "normalization": "declared", "reference_plane": "input", "seed_repeat_behavior": "recorded"}
                if capture_class == "ota": item["ota"] = {"authorization_id": "auth", "geometry": {}, "antenna_configuration": {}, "environment_description": "test", "motion_profile": "static", "facility_boundary": "test"}
                authorization = {"status": "approved" if capture_class == "ota" else "not_applicable",
                  "scope": "test", "artifact_relative_path": "missing-auth.pdf" if capture_class == "ota" else None,
                  "artifact_sha256": "d"*64 if capture_class == "ota" else None, "facility_boundary": "test"}
                doc["captures"].append(item); doc["sessions"].append({"session_id": f"session-{index}",
                  "capture_class": capture_class, "partition": "held_out", "independence_unit": "physical_capture_session",
                  "start_time_utc": "2026-01-01Z", "end_time_utc": "2026-01-01Z", "equipment_reset": True,
                  "authorization": authorization, "capture_ids": [f"cap-{index}"]})
            result = phase4.readiness(profile, doc, root)
            self.assertEqual(result["status"], "UNAVAILABLE_DEFERRED")
            self.assertTrue(any("event log" in x for x in result["failures"]))
            self.assertTrue(any("calibration artifact" in x for x in result["failures"]))
            self.assertTrue(any("authorization artifact" in x for x in result["failures"]))

    def test_unknown_property_rejected(self):
        doc = collection(self.raw); doc["surprise"] = 1
        with self.assertRaisesRegex(ValueError, "unknown"):
            phase4.validate_collection(doc)

    def test_unknown_nested_metadata_rejected(self):
        doc = collection(self.raw); doc["captures"][0]["environment"]["untracked"] = 1
        with self.assertRaisesRegex(ValueError, "unknown"):
            phase4.validate_collection(doc)

    def test_duplicate_capture_rejected(self):
        doc = collection(self.raw); doc["captures"].append(copy.deepcopy(doc["captures"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate capture"):
            phase4.validate_collection(doc)

    def test_partial_sample_rejected(self):
        doc = collection(self.raw); doc["captures"][0]["byte_length"] = 9
        with self.assertRaisesRegex(ValueError, "partial"):
            phase4.validate_collection(doc)

    def test_nonfinite_rejected(self):
        doc = collection(self.raw); doc["captures"][0]["sample_rate_hz"] = math.inf
        with self.assertRaisesRegex(ValueError, "finite"):
            phase4.validate_collection(doc)

    def test_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); (root / "cap-1.cf32").write_bytes(self.raw + b"x")
            with self.assertRaisesRegex(ValueError, "byte length"):
                phase4.resolve_and_verify(capture(self.raw), root)

    def test_path_escape_rejected(self):
        item = capture(self.raw); item["relative_iq_path"] = "../outside"
        with self.assertRaisesRegex(ValueError, "safe non-empty relative"):
            phase4.validate_capture(item)

    def test_commanded_is_not_measured(self):
        item = capture(self.raw); item["wanted_power"] = {"commanded_dbm": -50.0,
            "measured_dbm": -50.0, "uncertainty_db": None, "measurement_kind": "commanded_only"}
        with self.assertRaisesRegex(ValueError, "mislabeled"):
            phase4.validate_capture(item)

    def test_runtime_measurement_types_and_reference_plane_are_strict(self):
        for field, value, pattern in (("clipping_observed", "yes", "boolean"),
                                      ("agc_gain", "high", "numeric"),
                                      ("reference_plane", "", "non-empty")):
            item = capture(self.raw); item[field] = value
            with self.assertRaisesRegex(ValueError, pattern): phase4.validate_capture(item)

    def test_synthetic_capture_is_development_only(self):
        item = capture(self.raw); item["partition"] = "held_out"
        with self.assertRaisesRegex(ValueError, "development-only"):
            phase4.validate_capture(item)

    def test_held_out_requires_calibration(self):
        item = capture(self.raw); item["partition"] = "held_out"; item["capture_class"] = "conducted"
        item["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        with self.assertRaisesRegex(ValueError, "requires calibration"):
            phase4.validate_capture(item)

    def test_event_log_path_and_hash_must_be_paired(self):
        item = capture(self.raw); item["transmitter_event_log"] = "events.json"
        with self.assertRaisesRegex(ValueError, "paired"):
            phase4.validate_capture(item)

    def test_emulator_capture_requires_strict_emulator_metadata(self):
        item = capture(self.raw); item["capture_class"] = "channel_emulator"
        item["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        with self.assertRaisesRegex(ValueError, "channel_emulator"):
            phase4.validate_capture(item)

    def test_ota_session_requires_approval(self):
        doc = collection(self.raw); doc["sessions"][0]["capture_class"] = "ota"
        doc["captures"][0]["capture_class"] = "ota"
        doc["captures"][0]["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        doc["captures"][0]["ota"] = {"authorization_id": "blocked", "geometry": {},
          "antenna_configuration": {}, "environment_description": "test", "motion_profile": "none",
          "facility_boundary": "none"}
        with self.assertRaisesRegex(ValueError, "approved"):
            phase4.validate_collection(doc)

    def test_synthetic_provenance_cannot_be_promoted_to_physical_class(self):
        item = capture(self.raw); item["capture_class"] = "conducted"
        with self.assertRaisesRegex(ValueError, "cannot be promoted"):
            phase4.validate_capture(item)

    def test_capture_and_session_partitions_must_match(self):
        doc = collection(self.raw); doc["captures"][0]["partition"] = "validation"
        doc["captures"][0]["capture_class"] = "conducted"
        doc["captures"][0]["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        doc["sessions"][0]["capture_class"] = "conducted"
        with self.assertRaisesRegex(ValueError, "partition mismatch"):
            phase4.validate_collection(doc)

    def test_capture_and_session_classes_must_match(self):
        doc = collection(self.raw); doc["sessions"][0]["capture_class"] = "negative_control"
        with self.assertRaisesRegex(ValueError, "class mismatch"):
            phase4.validate_collection(doc)

    def test_top_level_rights_are_strict(self):
        doc = collection(self.raw); doc["rights"]["privacy"] = ""
        with self.assertRaisesRegex(ValueError, "rights"):
            phase4.validate_collection(doc)

    def test_cross_partition_iq_reuse_is_rejected(self):
        doc = collection(self.raw)
        doc["captures"][0]["capture_class"] = "conducted"
        doc["captures"][0]["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        doc["sessions"][0]["capture_class"] = "conducted"
        second = copy.deepcopy(doc["captures"][0])
        second.update({"capture_id": "cap-2", "session_id": "session-2", "partition": "validation"})
        session = copy.deepcopy(doc["sessions"][0]); session.update({"session_id": "session-2",
          "partition": "validation", "capture_ids": ["cap-2"]})
        doc["captures"].append(second); doc["sessions"].append(session)
        with self.assertRaisesRegex(ValueError, "reused across partitions"):
            phase4.validate_collection(doc)

    def test_calibration_equipment_references_are_bidirectional(self):
        doc = collection(self.raw)
        doc["calibrations"] = [{"calibration_id": "cal-1", "method": "test", "date_utc": "2026-01-01Z",
          "reference_plane": "input", "uncertainty_db": 1.0, "artifact_relative_path": "cal.pdf",
          "artifact_sha256": "0"*64, "equipment_ids": ["missing"]}]
        with self.assertRaisesRegex(ValueError, "unknown equipment"):
            phase4.validate_collection(doc)

    def test_cf32_big_endian_golden_conversion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); src = root/"in"; out = root/"out"
            src.write_bytes(struct.pack(">ffff", 1.0, -2.0, .25, 3.5))
            provenance = phase4.convert_iq(src, out, "cf32_be", "cf32_le")
            self.assertEqual(out.read_bytes(), self.raw)
            self.assertEqual(provenance["complex_samples"], 2)

    def test_cf64_little_endian_golden_conversion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); src = root/"in"; out = root/"out"
            src.write_bytes(struct.pack("<dddd", 1.0, -2.0, .25, 3.5))
            phase4.convert_iq(src, out, "cf64_le", "cf32_le")
            self.assertEqual(out.read_bytes(), self.raw)

    def test_conversion_is_transactional_on_nan(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); src = root/"in"; out = root/"out"
            src.write_bytes(struct.pack("<ff", math.nan, 0.0))
            with self.assertRaisesRegex(ValueError, "non-finite"):
                phase4.convert_iq(src, out, "cf32_le", "cf32_le")
            self.assertFalse(out.exists())

    def test_conversion_rejects_inplace_alias_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); src = root/"iq"; src.write_bytes(self.raw)
            with self.assertRaisesRegex(ValueError, "alias"):
                phase4.convert_iq(src, src, "cf32_le", "cf32_le")
            alias = root/"alias"; alias.symlink_to(src)
            with self.assertRaisesRegex(ValueError, "alias"):
                phase4.convert_iq(src, alias, "cf32_le", "cf32_le")
            destination = root/"existing"; destination.write_bytes(b"keep")
            with self.assertRaises(FileExistsError): phase4.convert_iq(src, destination, "cf32_le", "cf32_le")
            self.assertEqual(destination.read_bytes(), b"keep"); self.assertEqual(src.read_bytes(), self.raw)

    def test_provenance_failure_rolls_back_new_conversion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); src = root/"iq"; out = root/"out"; provenance = root/"provenance"
            src.write_bytes(self.raw); original = phase4.atomic_json
            try:
                phase4.atomic_json = lambda *_: (_ for _ in ()).throw(OSError("injected"))
                with self.assertRaisesRegex(OSError, "injected"):
                    phase4.convert_with_provenance(src, out, "cf32_le", "cf32_le", None, provenance)
            finally: phase4.atomic_json = original
            self.assertFalse(out.exists()); self.assertEqual(src.read_bytes(), self.raw)

    def test_optional_artifact_paths_reject_absolute_parent_and_symlink_escape(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); outside = root.parent/(root.name+"-outside"); outside.write_bytes(b"x")
            try:
                for relative in ("../escape", str(outside)):
                    with self.assertRaises(ValueError): phase4.resolve_hash_artifact(root, relative, phase4.digest_file(outside), "artifact")
                link = root/"link"; link.symlink_to(outside)
                with self.assertRaises(ValueError): phase4.resolve_hash_artifact(root, "link", phase4.digest_file(outside), "artifact")
                item = capture(self.raw); (root/"cap-1.cf32").write_bytes(self.raw); item["sigmf_meta_path"] = "link"; item["sigmf_meta_sha256"] = phase4.digest_file(outside)
                with self.assertRaises(ValueError): phase4.resolve_and_verify(item, root)
            finally: outside.unlink(missing_ok=True)

    def test_production_graph_is_truth_free(self):
        phase4.scan_truth_free_graph(phase4.load_json(GRAPH_PATH))

    def test_forbidden_truth_key_rejected_recursively(self):
        graph = phase4.load_json(GRAPH_PATH); graph["nodes"][1]["node_config"]["messages"] = []
        with self.assertRaisesRegex(ValueError, "forbidden"):
            phase4.scan_truth_free_graph(graph)

    def test_additional_synthetic_source_and_generator_aliases_are_rejected(self):
        graph = phase4.load_json(GRAPH_PATH)
        graph["nodes"].append({"id": "bypass", "type": "OtherSyntheticIqSourceNode", "node_config": {}})
        with self.assertRaisesRegex(ValueError, "source/generator"):
            phase4.scan_truth_free_graph(graph)
        graph = phase4.load_json(GRAPH_PATH); graph["nodes"][1]["node_config"]["generator_config"] = {}
        with self.assertRaisesRegex(ValueError, "config alias"):
            phase4.scan_truth_free_graph(graph)

    def test_patch_graph_changes_only_file_path(self):
        graph = phase4.load_json(GRAPH_PATH); patched = phase4.patch_graph(graph, Path("/tmp/input.cf32"))
        before = copy.deepcopy(graph); after = copy.deepcopy(patched)
        next(n for n in before["nodes"] if n["type"] == "FHSSBinaryIqFileSourceNode")["node_config"]["file_path"] = "/tmp/input.cf32"
        self.assertEqual(before, after)

    def test_matching_is_one_to_one_and_separates_false_and_miss(self):
        events = [{"event_id": "e1", "message_id": "m1", "start_sample": 100, "frequency_index": 24, "word": 1},
                  {"event_id": "e2", "message_id": "m1", "start_sample": 500, "frequency_index": 28, "word": 2}]
        detections = [{"detection_id": "d1", "start_sample": 101, "frequency_index": 24, "word": 1},
                      {"detection_id": "d2", "start_sample": 102, "frequency_index": 24, "word": 1}]
        result = phase4.match_events(events, detections, 10)
        self.assertEqual(result["matched_count"], 1)
        self.assertEqual(result["miss_event_ids"], ["e2"])
        self.assertEqual(result["false_detection_ids"], ["d2"])
        self.assertEqual(result["duplicate_detection_ids"], ["d2"])

    def test_matching_maximizes_cardinality_before_timing_cost(self):
        events = [{"event_id": "e0", "message_id": "m", "start_sample": 0, "frequency_index": 24, "word": 1},
                  {"event_id": "e5", "message_id": "m", "start_sample": 5, "frequency_index": 24, "word": 1}]
        detections = [{"detection_id": "d4", "start_sample": 4, "frequency_index": 24, "word": 1},
                      {"detection_id": "d8", "start_sample": 8, "frequency_index": 24, "word": 1}]
        result = phase4.match_events(events, detections, 4)
        self.assertEqual(result["matched_count"], 2)
        self.assertEqual({(x["event_id"], x["detection_id"]) for x in result["matched"]},
                         {("e0", "d4"), ("e5", "d8")})

    def test_invalid_event_frequency_word_and_message_id_fail(self):
        base = {"event_id": "e", "message_id": "m", "start_sample": 0, "frequency_index": 64, "word": 0}
        with self.assertRaisesRegex(ValueError, "frequency"):
            phase4.match_events([base], [], 1)
        base["frequency_index"] = 1; base["word"] = 1 << 32
        with self.assertRaisesRegex(ValueError, "uint32"):
            phase4.match_events([base], [], 1)
        base["word"] = 1; base["message_id"] = "bad id"
        with self.assertRaisesRegex(ValueError, "message_id"):
            phase4.match_events([base], [], 1)

    def test_v1_event_contract_rejects_empty_and_two_message_signal_capture(self):
        item = capture(self.raw)
        base = {"schema": "graphx.fhss.phase4-transmitter-events.v1", "session_id": "session-1",
                "capture_id": "cap-1", "time_reference": "sample-zero", "timing_uncertainty_samples": 0,
                "events": []}
        with self.assertRaisesRegex(ValueError, "nonempty"):
            phase4.validate_event_document(base, item)
        base["events"] = [
          {"event_id": "e1", "message_id": "m1", "start_sample": 0, "frequency_index": 24, "word": 1},
          {"event_id": "e2", "message_id": "m2", "start_sample": 10, "frequency_index": 24, "word": 1}]
        with self.assertRaisesRegex(ValueError, "one message_id"):
            phase4.validate_event_document(base, item)

    def test_negative_control_requires_and_accepts_zero_events(self):
        item = capture(self.raw); item["capture_class"] = "negative_control"
        item["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        document = {"schema": "graphx.fhss.phase4-transmitter-events.v1", "session_id": "session-1",
                    "capture_id": "cap-1", "time_reference": "sample-zero", "timing_uncertainty_samples": 0,
                    "events": []}
        phase4.validate_event_document(document, item)
        document["events"] = [{"event_id": "e", "message_id": "m", "start_sample": 0,
                               "frequency_index": 24, "word": 1}]
        with self.assertRaisesRegex(ValueError, "zero"):
            phase4.validate_event_document(document, item)

    def test_runtime_replay_schema_identity_is_enforced(self):
        result = replay_result(None); result["schema"] = "wrong"
        with self.assertRaisesRegex(ValueError, "schema"):
            phase4.validate_replay_result(result)

    def test_replay_result_rejects_forged_types_and_inconsistent_states(self):
        mutations = (
          lambda r: r.update(receiver_completed="yes"),
          lambda r: r.update(return_status="zero"),
          lambda r: r.update(status="completed", completed=True, failure={"stage": "pre_execution", "kind": "x", "message": "x"}),
          lambda r: r["allocation"].update(reported_node_count=-1))
        for mutate in mutations:
            result = replay_result(None); mutate(result)
            with self.assertRaises(ValueError): phase4.validate_replay_result(result)

    def test_replay_result_rejects_contradictory_matching_outcomes(self):
        matching = {"matched": [], "miss_event_ids": [], "association_failure_event_ids": [],
          "false_detection_ids": [], "duplicate_detection_ids": [], "collision_truth_count": 0,
          "collision_matched_count": 0, "event_count": 100, "detection_count": 100, "matched_count": 0}
        result = replay_result(matching)
        with self.assertRaisesRegex(ValueError, "event count"):
            phase4.validate_replay_result(result)
        matching.update(event_count=0, detection_count=1, false_detection_ids=["d1"], duplicate_detection_ids=["d2"])
        with self.assertRaisesRegex(ValueError, "subset"):
            phase4.validate_replay_result(result)

    def test_journal_resume_rejects_forgery_duplicates_wrong_bindings_and_missing_set(self):
        item = capture(self.raw); item.update(capture_class="conducted", partition="held_out")
        item["provenance"] = {"origin": "independently_recorded_physical", "producer": "test", "hardware_involved": True}
        bindings = {"profile_file_sha256": "1"*64, "dataset_manifest_sha256": "2"*64,
          "tool_sha256": "3"*64, "receiver_sha256": "4"*64, "graph_sha256": "5"*64,
          "plugin_manifest_sha256": "6"*64}
        result = replay_result(None); result["hashes"].update(iq_sha256=item["iq_sha256"],
          receiver_sha256=bindings["receiver_sha256"], base_graph_sha256=bindings["graph_sha256"],
          plugin_manifest_sha256=bindings["plugin_manifest_sha256"], effective_graph_sha256="7"*64,
          effective_config_sha256="8"*64, stdout_sha256="9"*64, stderr_sha256="a"*64)
        journal = {"schema": "graphx.fhss.phase4-replay-journal.v2", "bindings": bindings,
                   "results": [result], "results_sha256": phase4.digest_bytes(phase4.canonical([result])),
                   "all_executions_completed": True}
        phase4.validate_journal(journal, {"captures": [item]}, [item], bindings, True)
        forged = copy.deepcopy(journal); forged["results"] = [{"capture_id": "cap-1", "completed": True}]
        with self.assertRaises(ValueError): phase4.validate_journal(forged, {"captures": [item]}, [item], bindings, True)
        duplicate = copy.deepcopy(journal); duplicate.pop("results_sha256"); duplicate.pop("all_executions_completed"); duplicate["results"].append(copy.deepcopy(result))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            phase4.validate_journal(duplicate, {"captures": [item]}, [item], bindings, True)
        wrong_session = copy.deepcopy(journal); wrong_session.pop("results_sha256"); wrong_session["results"][0]["session_id"] = "other"
        with self.assertRaisesRegex(ValueError, "binding"):
            phase4.validate_journal(wrong_session, {"captures": [item]}, [item], bindings, True)
        wrong_binding = copy.deepcopy(journal); wrong_binding["bindings"]["tool_sha256"] = "f"*64
        with self.assertRaisesRegex(ValueError, "binding"):
            phase4.validate_journal(wrong_binding, {"captures": [item]}, [item], bindings, True)
        contradictory = copy.deepcopy(journal); contradictory.pop("results_sha256")
        contradictory["results"][0]["matching"] = {"matched": [], "miss_event_ids": [],
          "association_failure_event_ids": [], "false_detection_ids": [], "duplicate_detection_ids": [],
          "collision_truth_count": 0, "collision_matched_count": 0, "event_count": 100,
          "detection_count": 100, "matched_count": 0}
        with self.assertRaisesRegex(ValueError, "event count"):
            phase4.validate_journal(contradictory, {"captures": [item]}, [item], bindings, True)
        missing = {"schema": "graphx.fhss.phase4-replay-journal.v2", "bindings": bindings,
                   "results": [], "results_sha256": phase4.digest_bytes(phase4.canonical([])),
                   "all_executions_completed": True}
        with self.assertRaises(ValueError): phase4.validate_journal(missing, {"captures": [item]}, [item], bindings, True)
        contaminated = copy.deepcopy(item); contaminated["partition"] = "development"
        with self.assertRaisesRegex(ValueError, "partition"):
            phase4.validate_journal(journal, {"captures": [contaminated]}, [contaminated], bindings, True)

    def test_cluster_interval_uses_sessions(self):
        result = phase4.cluster_interval({"s1": [0.0] * 10, "s2": [1.0]})
        self.assertEqual(result["session_count"], 2)
        self.assertEqual(result["cluster_unit"], "capture_session")
        self.assertEqual(result["observation_count"], 11)
        self.assertLess(result["interval"][0], result["estimate"])
        self.assertGreater(result["interval"][1], result["estimate"])

    def test_cluster_interval_has_nontrivial_finite_sample_boundary_bounds(self):
        successes = phase4.cluster_interval({f"s{i}": [1.0] * 10 for i in range(3)})
        failures = phase4.cluster_interval({f"s{i}": [0.0] * 10 for i in range(3)})
        self.assertEqual(successes["estimate"], 1.0); self.assertLess(successes["interval"][0], 1.0)
        self.assertEqual(failures["estimate"], 0.0); self.assertGreater(failures["interval"][1], 0.0)
        self.assertIn("Wilson", successes["method"])

    def test_exact_poisson_upper_bound_handles_zero_and_positive_counts(self):
        zero = phase4.poisson_count_upper(0, 1.0, .95)
        one = phase4.poisson_count_upper(1, 1.0, .95)
        self.assertAlmostEqual(zero, -math.log(.05), places=10)
        self.assertGreater(one, zero)

    def test_aggregate_separates_conditional_errors_and_zero_far_bound(self):
        doc = collection(self.raw)
        matching = phase4.match_events(
          [{"event_id": "e1", "message_id": "m1", "start_sample": 100, "frequency_index": 24, "word": 1}],
          [{"detection_id": "d1", "start_sample": 100, "frequency_index": 24, "word": 3}], 1)
        journal = {"results": [replay_result(matching)]}
        report = phase4.aggregate_journal(self.profile, doc, journal)
        self.assertEqual(report["metrics"]["conditional_ber"]["numerator"], 1)
        self.assertEqual(report["metrics"]["conditional_ber"]["denominator"], 32)
        self.assertEqual(report["metrics"]["message_per"]["numerator"], 1)
        self.assertGreater(report["metrics"]["false_alarms_per_second"]["upper_confidence_bound"], 0)

    def test_aggregate_uses_nondefault_frozen_confidence(self):
        profile = copy.deepcopy(self.profile); profile["statistics"]["confidence_level"] = .90
        doc = collection(self.raw)
        matching = phase4.match_events(
          [{"event_id": "e", "message_id": "m", "start_sample": 0, "frequency_index": 24, "word": 1}], [], 0)
        journal = {"results": [replay_result(matching)]}
        report = phase4.aggregate_journal(profile, doc, journal)
        self.assertEqual(report["metrics"]["detection_probability"]["confidence_level"], .90)
        self.assertEqual(report["metrics"]["false_alarms_per_second"]["confidence_level"], .90)

    def test_duplicate_subtype_remains_in_false_alarm_numerator(self):
        matching = phase4.match_events(
          [{"event_id": "e", "message_id": "m", "start_sample": 0, "frequency_index": 24, "word": 1}],
          [{"detection_id": "d0", "start_sample": 0, "frequency_index": 24, "word": 1},
           {"detection_id": "d1", "start_sample": 1, "frequency_index": 24, "word": 1}], 2)
        report = phase4.aggregate_journal(self.profile, collection(self.raw),
                                          {"results": [replay_result(matching)]})
        self.assertEqual(report["totals"]["duplicates"], 1)
        self.assertEqual(report["metrics"]["false_alarms_per_second"]["numerator"], 1)

    def test_preexecution_failures_are_durable_and_never_retried(self):
        cases = ("missing", "size", "hash", "partial", "format", "sigmf")
        for kind in cases:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as d:
                root = Path(d); plugins = root/"plugins"; plugins.mkdir(); work = root/"work"; work.mkdir()
                item = capture(self.raw)
                if kind != "missing": (root/"cap-1.cf32").write_bytes(self.raw)
                if kind == "size": item["byte_length"] += 8
                if kind == "hash": item["iq_sha256"] = "f"*64
                if kind == "partial":
                    (root/"cap-1.cf32").write_bytes(b"123456789"); item["byte_length"] = 9; item["iq_sha256"] = phase4.digest_file(root/"cap-1.cf32")
                if kind == "format": item["sample_format"] = "ci16_le"
                if kind == "sigmf":
                    meta = root/"bad.sigmf-meta"; meta.write_text(json.dumps({"global": {"core:datatype": "cf64_le", "core:sample_rate": 1}}))
                    item["sigmf_meta_path"] = meta.name; item["sigmf_meta_sha256"] = phase4.digest_file(meta)
                journal = {"schema": "journal", "results": []}; path = root/"journal.json"
                phase4.record_capture_attempt(journal, path, self.profile, item, root, GRAPH_PATH, GRAPH_PATH, plugins, work)
                phase4.record_capture_attempt(journal, path, self.profile, item, root, GRAPH_PATH, GRAPH_PATH, plugins, work)
                stored = json.loads(path.read_text())
                self.assertEqual(len(stored["results"]), 1)
                self.assertEqual(stored["results"][0]["failure"]["stage"], "pre_execution")

    def test_interrupted_durable_attempt_becomes_terminal_without_retry(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); result = replay_result(None, False, 0)
            result.update(status="attempting", failure=None)
            journal = {"results": [result]}; path = root/"journal.json"
            phase4.finalize_interrupted_attempts(journal, path)
            phase4.finalize_interrupted_attempts(journal, path)
            stored = json.loads(path.read_text())
            self.assertEqual(len(stored["results"]), 1)
            self.assertEqual(stored["results"][0]["failure"]["stage"], "interrupted_attempt")

    def test_postreceiver_evaluator_failure_preserves_receiver_provenance(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); iq = root/"cap.cf32"; iq.write_bytes(self.raw); plugins = root/"plugins"; plugins.mkdir()
            receiver = root/"receiver.py"
            receiver.write_text("#!/usr/bin/env python3\nimport json,sys\na=sys.argv\ns=a[a.index('--summary-json')+1]; e=a[a.index('--effective-config-json')+1]\njson.dump({'fhss_diagnostics':{'schema':'graphx.fhss.message_sink.diagnostics.v1','decoded_pulses':[]}},open(s,'w'))\njson.dump({'ok':True},open(e,'w'))\n")
            os.chmod(receiver, 0o755)
            item = capture(self.raw); item["relative_iq_path"] = iq.name
            item["transmitter_event_log"] = "missing-events.json"; item["transmitter_event_log_sha256"] = "0"*64
            result = phase4.replay_capture(self.profile, item, iq, receiver, GRAPH_PATH, plugins, root, root/"work")
            self.assertEqual(result["failure"]["stage"], "post_receiver_evaluator")
            self.assertTrue(result["receiver_completed"])
            self.assertEqual(result["return_status"], 0)
            self.assertTrue(result["command"])
            self.assertEqual(result["searched_samples"], 2)
            self.assertIsNotNone(result["hashes"]["stdout_sha256"])

    def test_malformed_receiver_outputs_are_post_execution_failures_with_provenance(self):
        for malformed in ("summary", "effective"):
            with self.subTest(malformed=malformed), tempfile.TemporaryDirectory() as d:
                root = Path(d); iq = root/"cap.cf32"; iq.write_bytes(self.raw); plugins = root/"plugins"; plugins.mkdir()
                receiver = root/"receiver.py"
                summary_text = "not-json" if malformed == "summary" else json.dumps({"fhss_diagnostics": {"schema": "graphx.fhss.message_sink.diagnostics.v1", "decoded_pulses": []}})
                effective_text = "not-json" if malformed == "effective" else "{}"
                receiver.write_text("#!/usr/bin/env python3\nimport sys\na=sys.argv\nopen(a[a.index('--summary-json')+1],'w').write("+repr(summary_text)+")\nopen(a[a.index('--effective-config-json')+1],'w').write("+repr(effective_text)+")\n")
                os.chmod(receiver, 0o755); item = capture(self.raw)
                result = phase4.replay_capture(self.profile, item, iq, receiver, GRAPH_PATH, plugins, root, root/"work")
                self.assertEqual(result["failure"]["stage"], "post_receiver_evaluator")
                self.assertTrue(result["receiver_completed"]); self.assertEqual(result["return_status"], 0)
                self.assertTrue(result["command"]); self.assertEqual(result["searched_samples"], 2)
                self.assertTrue(result["searched_exposure_known"])
                self.assertIsNotNone(result["hashes"]["effective_config_sha256"])

    def test_incomplete_receivers_never_claim_full_file_exposure(self):
        for partial in (False, True):
            with self.subTest(partial=partial), tempfile.TemporaryDirectory() as d:
                root = Path(d); iq = root/"cap.cf32"; iq.write_bytes(self.raw); plugins = root/"plugins"; plugins.mkdir()
                receiver = root/"receiver.py"
                body = ("import json,sys\na=sys.argv\nopen(a[a.index('--summary-json')+1],'w').write(json.dumps({"
                        "'fhss_replay_progress':{'processed_samples':1}}))\n") if partial else "import sys\n"
                receiver.write_text("#!/usr/bin/env python3\n"+body+"sys.exit(2)\n"); os.chmod(receiver, 0o755)
                result = phase4.replay_capture(self.profile, capture(self.raw), iq, receiver, GRAPH_PATH, plugins, root, root/"work")
                self.assertEqual(result["searched_samples"], 1 if partial else 0)
                self.assertEqual(result["searched_exposure_known"], partial)
                self.assertEqual(result["status"], "failed")
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); iq = root/"cap.cf32"; iq.write_bytes(self.raw); plugins = root/"plugins"; plugins.mkdir()
            receiver = root/"receiver"; receiver.write_text("fixture"); item = capture(self.raw)
            def timeout_run(command, **unused):
                summary = Path(command[command.index("--summary-json")+1])
                summary.write_text(json.dumps({"fhss_replay_progress": {"processed_samples": 1}}))
                raise phase4.subprocess.TimeoutExpired(command, 1)
            with mock.patch.object(phase4.subprocess, "run", side_effect=timeout_run):
                result = phase4.replay_capture(self.profile, item, iq, receiver, GRAPH_PATH, plugins, root, root/"work")
            self.assertEqual(result["status"], "timed_out"); self.assertEqual(result["searched_samples"], 1)
            self.assertTrue(result["searched_exposure_known"])

    def test_synthetic_profile_cannot_be_promoted_to_hardware_correlation(self):
        profile = copy.deepcopy(self.profile); profile["correlation"]["agreement_criteria"]["status"] = "frozen_before_held_out"
        side = {"value": .9, "standard_uncertainty": .01, "artifact_sha256": "1"*64,
                "unit": "probability", "reference_plane": "receiver-input", "sample_count": 100, "session_count": 3}
        pair = {"schema": "graphx.fhss.phase4-correlation-pairs.v1", "dataset_manifest_sha256": "3"*64,
          "pairs": [{"pair_id": "p1", "scenario_id": "s1",
          "metric": "detection_probability_difference", "unit": "probability", "reference_plane": "receiver-input",
          "simulation": side, "hardware": {**side, "value": .92, "artifact_sha256": "2"*64},
          "discrepancy_classification": None}]}
        with self.assertRaisesRegex(ValueError, "synthetic-only"):
            phase4.correlate(profile, pair)

    def test_fully_governed_future_physical_correlation_path(self):
        doc = collection(self.raw); item = doc["captures"][0]
        item.update({"capture_class": "conducted", "partition": "held_out",
          "provenance": {"origin": "independently_recorded_physical", "producer": "independent-recorder", "hardware_involved": True},
          "equipment_ids": ["eq"], "calibration_record_ids": ["cal"],
          "transmitter_event_log": "cap-1.events.json", "transmitter_event_log_sha256": "e"*64,
          "scenario_tags": ["nominal_signal"]})
        doc["status"] = "frozen_held_out"
        doc["equipment"] = [{"equipment_id": "eq", "category": "receiver", "manufacturer": "test",
          "model": "test", "firmware": "1", "identifier_policy": "anonymized_stable",
          "calibration_record_ids": ["cal"]}]
        doc["calibrations"] = [{"calibration_id": "cal", "method": "traceable-test", "date_utc": "2026-01-01Z",
          "reference_plane": "receiver-input", "uncertainty_db": 0.5,
          "artifact_relative_path": "calibration.pdf", "artifact_sha256": "c"*64,
          "equipment_ids": ["eq"]}]
        session = doc["sessions"][0]; session.update({"capture_class": "conducted", "partition": "held_out",
          "equipment_reset": True})
        profile = copy.deepcopy(self.profile); profile["status"] = "frozen_for_heldout"
        profile["statistics"]["thresholds"].update(detection_probability_lower_bound=0.0,
          conditional_ber_upper_bound=1.0, message_per_upper_bound=1.0,
          false_alarms_per_second_upper_bound=10.0)
        freeze_thresholds(profile)
        profile["correlation"]["agreement_criteria"]["status"] = "frozen_before_held_out"
        profile["partitions"].update({"development_session_ids": [], "validation_session_ids": [],
                                      "held_out_session_ids": ["session-1"]})
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); paths = {name: root/f"{name}.json" for name in
              ("profile", "collection", "freeze", "journal", "aggregate", "pairs")}
            event_doc = {"schema": "graphx.fhss.phase4-transmitter-events.v1", "session_id": "session-1",
              "capture_id": "cap-1", "time_reference": "sample-zero", "timing_uncertainty_samples": 0,
              "events": [{"event_id": "e1", "message_id": "m1", "start_sample": 0,
                          "frequency_index": 24, "word": 1}]}
            phase4.atomic_json(root/"cap-1.events.json", event_doc)
            item["transmitter_event_log_sha256"] = phase4.digest_file(root/"cap-1.events.json")
            phase4.atomic_json(paths["collection"], doc); dataset_hash = phase4.digest_file(paths["collection"])
            profile["freeze"].update({"held_out_status": "frozen_before_held_out_replay", "reason": "future governed fixture",
              "profile_sha256": None, "dataset_manifest_sha256": dataset_hash,
              "receiver_executable_sha256": "1"*64, "plugin_manifest_sha256": "2"*64,
              "independent_tool_sha256": phase4.digest_file(TOOL), "freeze_manifest_path": "freeze.json",
              "freeze_manifest_sha256": None})
            profile_hash = phase4.digest_bytes(phase4.canonical(copy.deepcopy(profile)))
            profile["freeze"]["profile_sha256"] = profile_hash
            freeze = {"schema": "graphx.fhss.phase4-freeze-manifest.v1", "profile_contract_sha256": profile_hash,
              "dataset_manifest_sha256": dataset_hash, "receiver_executable_sha256": "1"*64,
              "plugin_manifest_sha256": "2"*64, "independent_tool_sha256": phase4.digest_file(TOOL),
              "partition_sha256": phase4.digest_bytes(phase4.canonical(profile["partitions"]))}
            phase4.atomic_json(paths["freeze"], freeze); freeze_hash = phase4.digest_file(paths["freeze"])
            profile["freeze"]["freeze_manifest_sha256"] = freeze_hash; phase4.atomic_json(paths["profile"], profile)
            bindings = {"profile_file_sha256": phase4.digest_file(paths["profile"]),
              "dataset_manifest_sha256": dataset_hash, "tool_sha256": phase4.digest_file(TOOL),
              "receiver_sha256": "1"*64, "graph_sha256": profile["replay"]["receiver_graph_sha256"],
              "plugin_manifest_sha256": "2"*64}
            result = replay_result(phase4.match_events(
              [{"event_id": "e1", "message_id": "m1", "start_sample": 0, "frequency_index": 24, "word": 1}],
              [{"detection_id": "d1", "start_sample": 0, "frequency_index": 24, "word": 1}], 0)); result["hashes"].update(
              iq_sha256=item["iq_sha256"], receiver_sha256="1"*64,
              base_graph_sha256=profile["replay"]["receiver_graph_sha256"], plugin_manifest_sha256="2"*64,
              effective_graph_sha256="7"*64, effective_config_sha256="8"*64,
              stdout_sha256="9"*64, stderr_sha256="a"*64)
            journal = {"schema": "graphx.fhss.phase4-replay-journal.v2", "bindings": bindings,
              "results": [result], "results_sha256": phase4.digest_bytes(phase4.canonical([result])),
              "all_executions_completed": True}
            phase4.atomic_json(paths["journal"], journal)
            aggregate = phase4.aggregate_journal(profile, doc, journal, bindings)
            self.assertEqual(aggregate["status"], "FAIL")
            self.assertTrue(all(value for key, value in aggregate["gates"].items() if key != "correlation"))
            weak_profile = copy.deepcopy(profile)
            weak_profile["statistics"]["thresholds"]["conditional_ber_upper_bound"] = 0.0
            freeze_thresholds(weak_profile)
            weak_journal = copy.deepcopy(journal)
            weak_journal["results"][0]["matching"] = phase4.match_events(
              [{"event_id": "e1", "message_id": "m1", "start_sample": 0, "frequency_index": 24, "word": 1}],
              [{"detection_id": "d1", "start_sample": 0, "frequency_index": 24, "word": 3}], 0)
            weak_journal["results_sha256"] = phase4.digest_bytes(phase4.canonical(weak_journal["results"]))
            weak = phase4.aggregate_journal(weak_profile, doc, weak_journal, bindings)
            self.assertFalse(weak["gates"]["conditional_ber"]); self.assertEqual(weak["status"], "FAIL")
            expanded = copy.deepcopy(doc); second = copy.deepcopy(item)
            second.update(capture_id="cap-2", session_id="session-2", relative_iq_path="cap-2.cf32",
                          iq_sha256="b"*64, parents=[])
            expanded["captures"].append(second)
            second_session = copy.deepcopy(session); second_session.update(session_id="session-2", capture_ids=["cap-2"])
            expanded["sessions"].append(second_session)
            with self.assertRaisesRegex(ValueError, "incomplete"):
                phase4.aggregate_journal(profile, expanded, journal, bindings)
            phase4.atomic_json(paths["aggregate"], aggregate)
            side = {"value": .90, "standard_uncertainty": .01, "artifact_sha256": "4"*64,
                    "unit": "probability", "reference_plane": "receiver-input", "sample_count": 100,
                    "session_count": 1, "provenance": "independent_simulation", "capture_ids": []}
            paired = {"schema": "graphx.fhss.phase4-correlation-pairs.v1", "dataset_manifest_sha256": dataset_hash,
              "freeze_manifest_sha256": freeze_hash, "replay_evidence": {
              "journal_sha256": phase4.digest_file(paths["journal"]), "aggregate_sha256": phase4.digest_file(paths["aggregate"]),
              "all_executions_completed": True, "profile_contract_sha256": profile_hash,
              "dataset_manifest_sha256": dataset_hash, "physical_capture_ids": ["cap-1"]},
              "pairs": [{"pair_id": "p1", "scenario_id": "s1", "metric": "detection_probability_difference",
              "unit": "probability", "reference_plane": "receiver-input", "simulation": side,
              "hardware": {**side, "value": .92, "artifact_sha256": "6"*64, "sample_count": result["searched_samples"],
                           "provenance": "independently_recorded_physical", "capture_ids": ["cap-1"]},
              "discrepancy_classification": None}]}
            phase4.atomic_json(paths["pairs"], paired)
            report = phase4.correlate_files(paths["profile"], paths["pairs"], paths["collection"], paths["freeze"], paths["journal"], paths["aggregate"])
            self.assertEqual(report["status"], "PASS"); self.assertEqual(len(report["paired_points"]), 1)
            final_aggregate = phase4.aggregate_journal(profile, doc, journal, bindings, report)
            self.assertEqual(final_aggregate["status"], "PASS"); self.assertTrue(all(final_aggregate["gates"].values()))
            for timeout in (False, True):
                failed_result = copy.deepcopy(result)
                failed_result.update(status="timed_out" if timeout else "failed", completed=False,
                  receiver_completed=False, return_status=None if timeout else 2, searched_samples=0,
                  searched_exposure_known=False, matching=None,
                  failure={"stage": "receiver_execution" if timeout else "pre_execution",
                           "kind": "Timeout" if timeout else "MissingIq", "message": "retained failure"})
                failed_journal = {"schema": "graphx.fhss.phase4-replay-journal.v2", "bindings": bindings,
                  "results": [failed_result], "results_sha256": phase4.digest_bytes(phase4.canonical([failed_result])),
                  "all_executions_completed": False}
                failed_report = phase4.aggregate_journal(profile, doc, failed_journal, bindings, corpus_root=root)
                self.assertEqual(failed_report["status"], "FAIL")
                self.assertFalse(failed_report["gates"]["execution_completion"])
                self.assertFalse(failed_report["gates"]["false_alarms_per_second"])
                self.assertEqual(failed_report["totals"]["truth_events"], 1)
                self.assertEqual(failed_report["totals"]["misses"], 1)
            bad_event_doc = copy.deepcopy(doc); bad_event_doc["captures"][0]["transmitter_event_log_sha256"] = "f"*64
            unresolved = phase4.aggregate_journal(profile, bad_event_doc, failed_journal, bindings, corpus_root=root)
            self.assertIn("detection_probability", unresolved["unresolved_metrics"])
            self.assertFalse(unresolved["gates"]["detection_probability"])
            contaminated = copy.deepcopy(paired); contaminated["pairs"][0]["hardware"]["provenance"] = "independent_simulation"
            hashes = {"profile_file_sha256": bindings["profile_file_sha256"], "dataset_manifest_sha256": dataset_hash,
              "freeze_manifest_sha256": freeze_hash, "journal_sha256": phase4.digest_file(paths["journal"]),
              "aggregate_sha256": phase4.digest_file(paths["aggregate"])}
            with self.assertRaisesRegex(ValueError, "synthetic|governed"):
                phase4.correlate(profile, contaminated, doc, freeze, journal, aggregate, hashes)
            forged = copy.deepcopy(paired); forged["replay_evidence"]["journal_sha256"] = "f"*64
            with self.assertRaisesRegex(ValueError, "binding"):
                phase4.correlate(profile, forged, doc, freeze, journal, aggregate, hashes)

    def test_aggregate_retains_failed_signal_capture_in_message_denominator(self):
        doc = collection(self.raw)
        journal = {"results": [replay_result(None, False, 0)]}
        report = phase4.aggregate_journal(self.profile, doc, journal)
        self.assertEqual(report["totals"]["failed_captures"], 1)
        self.assertEqual(report["metrics"]["message_per"]["numerator"], 1)
        self.assertEqual(report["metrics"]["message_per"]["denominator"], 1)

    def test_replay_refuses_infrastructure_profile_before_touching_corpus(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); coll = root/"collection.json"; coll.write_text(json.dumps(collection(self.raw)))
            with self.assertRaisesRegex(ValueError, "not frozen"):
                phase4.replay_collection(PROFILE_PATH, coll, root, root/"receiver", GRAPH_PATH,
                                         root/"plugins", root/"journal", root/"work")


if __name__ == "__main__":
    unittest.main()
