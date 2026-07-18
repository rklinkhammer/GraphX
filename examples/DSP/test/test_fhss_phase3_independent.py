#!/usr/bin/env python3
"""Independent analytical tests for the Phase 3 waveform/channel harness."""

from __future__ import annotations

import importlib.util
import json
import math
import os
import random
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "tools/fhss_phase3_independent.py"
SPEC = importlib.util.spec_from_file_location("fhss_phase3_independent", TOOL)
assert SPEC and SPEC.loader
p3 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = p3
SPEC.loader.exec_module(p3)


def one_message(word: int = 0xAAAAAAAA, start: int = 17, phase: float = 0.0):
    return p3.Message(1, 1, start, phase, 1.0, (p3.Pulse(24, word, "body"),))


def conformant_pulses(body_word: int = 0x13579BDF):
    pulses = [{"frequency_index": p3.ACTIVE[i % 4],
               "word": p3.PREAMBLE_WORDS[i % 4], "role": "preamble"}
              for i in range(p3.PREAMBLE_PULSES)]
    pulses.append({"frequency_index": 24, "word": body_word, "role": "body"})
    return pulses


def conformant_scenario(**overrides):
    result = {"schema": "graphx.fhss.phase3-scenario.v2", "scenario_id": "unit",
              "seed": 42, "active_frequency_indices": list(p3.ACTIVE),
              "sample_format": "cf32_le", "messages": [{"message_id": 1,
                  "transmitter_id": 1, "transmit_start_sample": 1000,
                  "initial_phase_rad": 0.0, "pulses": conformant_pulses()}],
              "channel": {"awgn": {"eb_n0_db": 60.0}}}
    result.update(overrides)
    return result


def minimal_profile():
    matrix = [{"id": "clean", "family": "clean", "eb_n0_db": 60.0}]
    partitions = {"development": [1], "validation": [2],
                  "evaluation": [3], "gate_extension": [4]}
    frozen = {"matching_tolerance_samples": 160.0,
              "frequency_match": "exact frequency_index",
              "impairment_order": list(p3.IMPAIRMENT_ORDER),
              "burst_epoch_range_samples": [1000, 2000],
              "receiver_executor_timeout_seconds": 120,
              "receiver_process_timeout_seconds": 150,
              "matrix_sha256": p3.digest_json(matrix),
              "seed_partitions_sha256": p3.digest_json(partitions),
              "gates": [{"id": "pd", "point_id": "clean", "metric": "pd",
                         "statistic": "lower_ci95", "comparison": ">=",
                         "limit": 0.5, "units": "probability"}]}
    return {"schema": "graphx.fhss.phase3-validation-profile.v6", "version": 6,
            "claim_level": "test", "prohibited_claims": [], "waveform": {},
            "statistics": {"seed_partitions": partitions,
                           "gate_extension_point_ids": ["clean"],
                           "confidence": "Wilson 95%", "stopping_rule": "fixed",
                           "failure_policy": "fail"},
            "matrix": matrix, "frozen": frozen,
            "freeze": {"status": "frozen_before_held_out_evaluation",
                       "sha256": p3.digest_json(frozen)},
            "limitations": [], "engineering_rationale": {}}


class IndependentWaveformTest(unittest.TestCase):
    def test_canonical_clean_parity_is_secondary_evidence(self):
        executable = os.environ.get("GRAPHX_FHSS_CANONICAL_GENERATOR")
        if not executable:
            self.skipTest("canonical generator path not supplied")
        fixtures = Path(__file__).resolve().parents[1] / "fixtures"
        scenario_path = fixtures / "fhss_phase3_clean_scenario_v1.json"
        schedule_path = fixtures / "fhss_phase3_canonical_parity_schedule_v1.json"
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); independent = root/"independent.cf64"; canonical = root/"canonical.cf64"
            p3.write_capture(json.loads(scenario_path.read_text()), independent, root/"independent_truth.json")
            completed = subprocess.run([executable, "--message-json", str(schedule_path), "--iq-output", str(canonical),
                                        "--truth-output", str(root/"canonical_truth.json"), "--sample-format", "cf64_le"],
                                       text=True, capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            metrics = p3.parity_metrics(independent.read_bytes(), canonical.read_bytes(), "cf64_le")
            self.assertLess(metrics["maximum_complex_error"], 2e-15)
            self.assertEqual(metrics["timing_placement_error_samples"], 0)
            self.assertIn("not proof", metrics["interpretation"])

    def test_literal_msb_first_cpsm_samples(self):
        positive = p3.pulse_sample(0x00000000, 50, 50, 0.0)
        negative = p3.pulse_sample(0x80000000, 50, 50, 0.0)
        self.assertAlmostEqual(positive.real, math.sqrt(0.5), places=13)
        self.assertAlmostEqual(positive.imag, math.sqrt(0.5), places=13)
        self.assertAlmostEqual(negative.real, math.sqrt(0.5), places=13)
        self.assertAlmostEqual(negative.imag, -math.sqrt(0.5), places=13)

    def test_symbol_boundary_phase_is_continuous(self):
        word = 0x80000000
        before = p3.pulse_sample(word, 99, 99, 0.0)
        boundary = p3.pulse_sample(word, 100, 100, 0.0)
        expected_increment = -math.pi * 0.5 / 100
        self.assertAlmostEqual(__import__("cmath").phase(boundary * before.conjugate()), expected_increment, places=13)

    def test_nonzero_global_origin_changes_only_hop_phase(self):
        a = p3.pulse_sample(0xAAAAAAAA, 41, 41, 12_500_000.0)
        b = p3.pulse_sample(0xAAAAAAAA, 41, 1041, 12_500_000.0)
        expected = 2*math.pi*12_500_000.0*1000/p3.SAMPLE_RATE_HZ
        self.assertAlmostEqual(__import__("cmath").phase(b*a.conjugate()), __import__("cmath").phase(complex(math.cos(expected), math.sin(expected))), places=12)

    def test_pulse_gap_and_later_pulse_placement(self):
        message = p3.Message(1, 1, 11, 0.0, 1.0,
                             (p3.Pulse(24, 0, "preamble"), p3.Pulse(28, 0xFFFFFFFF, "body")))
        samples, truth = p3.synthesize([message], 0)
        self.assertEqual(truth[0]["nominal_transmit_start_sample"], 11)
        self.assertEqual(truth[1]["nominal_transmit_start_sample"], 6511)
        self.assertTrue(all(samples[i] == 0j for i in range(3211, 6511)))
        self.assertNotEqual(samples[6511], 0j)

    def test_overlap_sums_independent_transmitters(self):
        first = one_message(start=0)
        second = p3.Message(2, 2, 0, 0.0, 0.5, first.pulses)
        combined, _ = p3.synthesize([first, second], 0)
        single, _ = p3.synthesize([first], 0)
        self.assertAlmostEqual(abs(combined[123]), 1.5*abs(single[123]), places=12)

    def test_cf32_cf64_literal_bytes_and_roundtrip(self):
        samples = [complex(1.0, -0.5), complex(-2.0, 4.0)]
        cf32 = p3.encode_iq(samples, "cf32_le")
        cf64 = p3.encode_iq(samples, "cf64_le")
        self.assertEqual(cf32.hex(), "0000803f000000bf000000c000008040")
        self.assertEqual(cf64.hex(), "000000000000f03f000000000000e0bf00000000000000c00000000000001040")
        self.assertEqual(p3.decode_iq(cf32, "cf32_le"), samples)
        self.assertEqual(p3.decode_iq(cf64, "cf64_le"), samples)

    def test_deterministic_capture_hash_and_separate_truth(self):
        scenario = conformant_scenario(scenario_id="hash")
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); a=root/"a.iq"; at=root/"a.json"; b=root/"b.iq"; bt=root/"b.json"
            ma = p3.write_capture(scenario, a, at); mb = p3.write_capture(scenario, b, bt)
            self.assertEqual(ma["iq_sha256"], mb["iq_sha256"])
            self.assertEqual(a.read_bytes(), b.read_bytes())
            self.assertNotIn(b"events", a.read_bytes())

    def test_strict_schema_rejects_unknown_nonfinite_and_bad_preamble(self):
        invalid = conformant_scenario(unknown=True)
        with self.assertRaisesRegex(ValueError, "unknown properties"):
            p3.validate_scenario(invalid)
        invalid = conformant_scenario()
        invalid["channel"]["carrier"] = {"phase_noise_step_std_rad": -0.1}
        with self.assertRaisesRegex(ValueError, "below its minimum"):
            p3.validate_scenario(invalid)
        invalid = conformant_scenario()
        invalid["messages"][0]["pulses"][4]["word"] ^= 1
        with self.assertRaisesRegex(ValueError, "architecture word"):
            p3.validate_scenario(invalid)
        invalid = conformant_scenario()
        invalid["channel"]["timing"] = {"fractional_delay_samples": math.nan}
        with self.assertRaisesRegex(ValueError, "finite"):
            p3.validate_scenario(invalid)

    def test_transactional_generation_no_clobber_and_rollback(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); iq = root/"capture.iq"; truth = root/"truth.json"
            iq.write_bytes(b"sentinel")
            with self.assertRaises(FileExistsError):
                p3.write_capture(conformant_scenario(), iq, truth)
            self.assertEqual(iq.read_bytes(), b"sentinel")
            iq.unlink()
            real_link = p3.os.link
            calls = 0
            def fail_second(source, destination):
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("injected truth commit failure")
                return real_link(source, destination)
            with mock.patch.object(p3.os, "link", side_effect=fail_second):
                with self.assertRaisesRegex(OSError, "injected"):
                    p3.write_capture(conformant_scenario(), iq, truth)
            self.assertFalse(iq.exists())
            self.assertFalse(truth.exists())


class IndependentChannelTest(unittest.TestCase):
    def test_awgn_mean_variance_and_circularity(self):
        rng = random.Random(77)
        noisy, noise_power = p3.add_awgn([1+0j]*100_000, 10.0, rng, 1.0)
        noise = [x-1 for x in noisy]
        mean_i = sum(x.real for x in noise)/len(noise)
        mean_q = sum(x.imag for x in noise)/len(noise)
        var_i = sum((x.real-mean_i)**2 for x in noise)/len(noise)
        var_q = sum((x.imag-mean_q)**2 for x in noise)/len(noise)
        self.assertAlmostEqual(noise_power, 0.1, places=14)
        self.assertLess(abs(mean_i), 0.002)
        self.assertLess(abs(mean_q), 0.002)
        self.assertLess(abs(var_i-0.05), 0.002)
        self.assertLess(abs(var_q-0.05), 0.002)

    def test_ebn0_calibration_is_dimensionally_consistent(self):
        calibration = p3.noise_calibration(2.0, 20.0)
        self.assertAlmostEqual(calibration["bit_energy_j"], 2.0/p3.BIT_RATE_HZ)
        self.assertAlmostEqual(calibration["complex_noise_variance_w"], 2.0, places=12)
        self.assertAlmostEqual(calibration["sample_snr_db"], 0.0, places=12)
        self.assertAlmostEqual(calibration["n0_w_per_hz"] * p3.SAMPLE_RATE_HZ,
                               calibration["complex_noise_variance_w"], places=12)

    def test_cfo_positive_and_negative_phase_slope(self):
        for frequency in (25_000.0, -25_000.0):
            values = p3.apply_phase_frequency([1+0j]*1000, frequency, 0, 0, 0, random.Random(1))
            slope = __import__("cmath").phase(values[1]*values[0].conjugate())
            self.assertAlmostEqual(slope*p3.SAMPLE_RATE_HZ/(2*math.pi), frequency, places=6)

    def test_fractional_delay_and_clock_length(self):
        impulse = [1+0j] + [0j]*99
        delayed = p3.sinc_resample(impulse, 2.5, 100.0)
        self.assertEqual(len(delayed), math.ceil(100*1.0001+2.5))
        self.assertIn(max(range(len(delayed)), key=lambda i: abs(delayed[i])), (2, 3))

    def test_normalized_integer_and_fractional_tdl(self):
        impulse = [1+0j] + [0j]*63
        output = p3.fractional_tdl(impulse, [{"delay_samples": 0, "power_linear": 1},
                                             {"delay_samples": 16.5, "power_linear": 1}])
        self.assertAlmostEqual(abs(output[0]), 1/math.sqrt(2), places=10)
        self.assertIn(max(range(1, len(output)), key=lambda i: abs(output[i])), (16, 17))

    def test_rayleigh_rician_seed_reproduction(self):
        for kind in ("rayleigh", "rician"):
            a = p3.fading_coefficient(random.Random(99), kind)
            b = p3.fading_coefficient(random.Random(99), kind)
            self.assertEqual(a, b)
        draws = [abs(p3.fading_coefficient(random.Random(i), "rayleigh"))**2 for i in range(5000)]
        self.assertLess(abs(sum(draws)/len(draws)-1.0), 0.05)

    def test_time_varying_fading_is_seeded_unit_power_and_doppler_bounded(self):
        a = p3.sum_of_sinusoids_fading(20_000, 100_000.0, 1_000.0, random.Random(7))
        b = p3.sum_of_sinusoids_fading(20_000, 100_000.0, 1_000.0, random.Random(7))
        self.assertEqual(a, b)
        self.assertLess(abs(p3.signal_power(a)-1.0), 0.20)
        components = p3.sum_of_sinusoids_components(1_000.0, random.Random(7))
        self.assertTrue(all(abs(frequency) <= 1_000.0 for frequency, _ in components))

        # Duplicate cosine frequencies combine coherently.  The long-record
        # autocorrelation must agree with the finite tone model; an
        # instantaneous composite phase derivative is intentionally not used
        # because it is unbounded near a fading null.
        grouped = {}
        for frequency, phase in components:
            key = round(frequency, 9)
            grouped[key] = grouped.get(key, 0j) + __import__("cmath").exp(1j*phase)/math.sqrt(len(components))
        model_power = sum(abs(weight)**2 for weight in grouped.values())
        lag = 25
        expected = sum(abs(weight)**2 * __import__("cmath").exp(1j*2*math.pi*frequency*lag/100_000)
                       for frequency, weight in grouped.items()) / model_power
        measured = sum(a[i+lag]*a[i].conjugate() for i in range(len(a)-lag)) / (len(a)-lag)
        measured /= p3.signal_power(a)
        self.assertLess(abs(measured-expected), 0.02)

    def test_blocker_sir_and_hardware_models(self):
        wanted = [1+0j]*1000
        mixed = p3.add_tone(wanted, 1_000_000.0, 20.0)
        blocker = [mixed[i]-wanted[i] for i in range(len(wanted))]
        self.assertAlmostEqual(p3.signal_power(blocker), 0.01, places=10)
        impaired = p3.hardware([2+2j], {"iq_gain_imbalance_db": 1, "iq_phase_imbalance_deg": 3,
                                                "dc_i": .1, "clip_magnitude": 1, "quantization_bits": 8})
        self.assertLessEqual(abs(impaired[0]), 1.01)

    def test_impairment_composition_order_and_fixed_noise_reference(self):
        samples = [1+0j] * 1000
        config = {"iq_gain_imbalance_db": 1.0, "dc_i": 0.1,
                  "agc_transient_samples": 10, "clip_magnitude": 0.8,
                  "quantization_bits": 8}
        composed = p3.apply_iq_imbalance_dc(samples, config)
        composed = p3.apply_agc(composed, 10)
        composed = p3.apply_clipping(composed, 0.8)
        composed = p3.apply_quantization(composed, 8, 1.0)
        self.assertEqual(p3.hardware(samples, config), composed)
        wanted_power = p3.active_signal_power(samples)
        mixed = p3.add_tone(samples, 1_000_000, 0.0, reference_power=wanted_power)
        blocker_power = p3.signal_power([mixed[i]-samples[i] for i in range(len(samples))])
        self.assertAlmostEqual(blocker_power/wanted_power, 1.0, places=12)
        self.assertEqual(p3.noise_calibration(wanted_power, 20.0),
                         p3.noise_calibration(wanted_power, 20.0))
        actual, _ = p3.apply_post_blocker_stages(samples, config, 30.0,
                                                  wanted_power, random.Random(88))
        expected = p3.apply_iq_imbalance_dc(samples, config)
        expected = p3.apply_agc(expected, 10)
        calibration = p3.noise_calibration(wanted_power, 30.0)
        rng = random.Random(88)
        sigma = math.sqrt(calibration["complex_noise_variance_w"] / 2)
        expected = [value + complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
                    for value in expected]
        expected = p3.apply_clipping(expected, 0.8)
        expected = p3.apply_quantization(expected, 8, 1.0)
        self.assertEqual(actual, expected)

    def test_channel_seed_and_impairment_order(self):
        config = {"timing": {"fractional_delay_samples": .25, "sample_clock_offset_ppm": 5},
                  "multipath": {"taps": [{"delay_samples": 0, "power_linear": 1}]},
                  "fading": {"kind": "rician"}, "carrier": {"cfo_hz": 100},
                  "awgn": {"eb_n0_db": 40}, "hardware": {"dc_i": .01}}
        a, ta = p3.apply_channel([1+0j]*100, config, 123)
        b, tb = p3.apply_channel([1+0j]*100, config, 123)
        self.assertEqual(a, b)
        self.assertEqual(ta["impairment_order"], list(p3.IMPAIRMENT_ORDER))

    def test_truth_clock_drift_accumulates_at_multiple_epochs(self):
        for epoch in (1_000, 101_000):
            scenario = conformant_scenario()
            scenario["messages"][0]["transmit_start_sample"] = epoch
            scenario["channel"] = {
                "timing": {"fractional_delay_samples": 0.25,
                           "sample_clock_offset_ppm": 100.0},
                "multipath": {"taps": [{"delay_samples": 2.5,
                                           "power_linear": 1.0}]},
                "awgn": {"eb_n0_db": 60.0}}
            with tempfile.TemporaryDirectory() as d:
                manifest = p3.write_capture(scenario, Path(d)/"a.iq", Path(d)/"a.json")
            event = manifest["events"][0]
            expected = 1.0001 * (epoch + 0.25) + 2.5
            self.assertAlmostEqual(event["received_start_sample"], expected, places=10)
            self.assertAlmostEqual(event["accumulated_clock_drift_samples"],
                                   epoch * 0.0001, places=10)


class MatchingStatisticsTest(unittest.TestCase):
    def test_one_to_one_matching_miss_false_duplicate_and_bits(self):
        truth = [{"message_id": 1, "transmitter_id": 1, "frequency_index": 24, "received_start_sample": 100, "word": 0xAAAAAAAA},
                 {"message_id": 1, "transmitter_id": 1, "frequency_index": 28, "received_start_sample": 200, "word": 0}]
        detections = [{"frequency_index": 24, "global_start_sample": 102, "decoded_value": 0xAAAAAAAB},
                      {"frequency_index": 24, "global_start_sample": 103, "decoded_value": 0xAAAAAAAA},
                      {"frequency_index": 32, "global_start_sample": 400, "decoded_value": 0}]
        result = p3.match_events(truth, detections, 5, True)
        self.assertEqual((result["matched_events"], result["misses"], result["duplicates"], result["false_detections"]), (1,1,1,1))
        self.assertEqual((result["bit_errors"], result["bits"]), (1, 32))
        self.assertEqual((result["word_errors"], result["words"]), (1, 1))
        self.assertEqual(result["association_errors"], 1)
        self.assertEqual(result["messages"], 1)
        self.assertTrue(result["message_error"])

    def test_per_message_transmitter_and_collision_classes_are_distinct(self):
        pulse = (p3.Pulse(24, 0xAAAAAAAA, "body"),)
        _, truth = p3.synthesize([
            p3.Message(10, 1, 100, 0.0, 1.0, pulse),
            p3.Message(20, 2, 200, 0.0, 1.0, pulse)], 0)
        detections = [{"frequency_index": 24, "global_start_sample": 100,
                       "decoded_value": 0xAAAAAAAA}]
        result = p3.match_events(truth, detections, 5, True)
        self.assertEqual((result["messages"], result["message_errors"]), (2, 1))
        self.assertEqual(result["temporal_overlap_truth_events"], 2)
        self.assertEqual(result["same_hop_overlap_truth_events"], 2)
        by_transmitter = {row["transmitter_id"]: row for row in result["message_results"]}
        self.assertEqual(by_transmitter[1]["capture_probability"], 1.0)
        self.assertEqual(by_transmitter[2]["capture_probability"], 0.0)

    def test_wilson_zero_event_is_bounded(self):
        interval = p3.wilson(0, 1000)
        self.assertEqual(interval[0], 0.0)
        self.assertGreater(interval[1], 0.0)

    def test_frozen_gates_use_declared_confidence_bound(self):
        row = {"pd": 1.0, "pd_ci95": [0.89, 1.0], "per": 0.0,
               "per_ci95": [0.0, 0.66]}
        self.assertEqual(
            p3.gate_statistic_value(row, {"metric": "pd", "statistic": "lower_ci95"}),
            (1.0, 0.89))
        self.assertEqual(
            p3.gate_statistic_value(row, {"metric": "per", "statistic": "upper_ci95"}),
            (0.0, 0.66))

    def test_profile_rejects_overlap_and_development_evaluation(self):
        profile = minimal_profile()
        profile["statistics"]["seed_partitions"]["evaluation"] = [2]
        with self.assertRaisesRegex(ValueError, "overlap"):
            p3.evaluate(profile, Path("x"), Path("x"), Path("x"), "evaluation")
        profile["statistics"]["seed_partitions"]["evaluation"] = [3]
        with self.assertRaisesRegex(ValueError, "refuses development"):
            p3.evaluate(profile, Path("x"), Path("x"), Path("x"), "development")
        profile = minimal_profile()
        profile["freeze"]["status"] = "pilot_not_frozen"
        with self.assertRaisesRegex(ValueError, "requires a frozen"):
            p3.evaluate(profile, Path("x"), Path("x"), Path("x"),
                        "evaluation")

    def test_raw_binding_accepts_explicit_failure_but_rejects_status_lie(self):
        profile = {"freeze": {"sha256": "frozen"},
                   "frozen": {"receiver_executor_timeout_seconds": 120,
                              "receiver_process_timeout_seconds": 150}}
        sha = "a" * 64
        provenance = {"return_status": 0, "profile_sha256": p3.digest_json(profile),
                      "scenario_sha256": sha, "iq_sha256": sha,
                      "plugin_manifest": [], "plugin_set_sha256": p3.digest_json([]),
                      "receiver_executable_sha256": sha, "receiver_graph_sha256": sha,
                      "effective_config_sha256": sha,
                      "effective_receiver_config_sha256": sha,
                      "independent_tool_sha256": sha, "cxx_mode": "c++26",
                      "executor_timeout_seconds": 120,
                      "process_timeout_seconds": 150,
                      "command": ["receiver", "--graph-config"],
                      "compiler_version": "clang test", "git_head": sha,
                      "git_status_sha256": sha}
        cases = [{"execution_status": "completed",
                  "scenario_sha256": sha, "iq_sha256": sha,
                  "provenance": provenance}]
        raw = {"profile_sha256": p3.digest_json(profile), "frozen_sha256": "frozen",
               "cases": cases, "cases_sha256": p3.digest_json(cases)}
        p3.validate_raw_binding(profile, raw)
        raw["cases"][0]["provenance"]["return_status"] = 9
        raw["cases_sha256"] = p3.digest_json(raw["cases"])
        with self.assertRaisesRegex(ValueError, "nonzero receiver status"):
            p3.validate_raw_binding(profile, raw)
        raw["cases"][0]["execution_status"] = "failed"
        raw["cases"][0]["failure"] = {
            "kind": "nonzero_receiver_status", "return_status": 9}
        raw["cases_sha256"] = p3.digest_json(raw["cases"])
        p3.validate_raw_binding(profile, raw)
        raw["cases"][0]["execution_status"] = "completed"
        raw["cases"][0]["iq_sha256"] = "b" * 64
        with self.assertRaisesRegex(ValueError, "integrity hash"):
            p3.validate_raw_binding(profile, raw)

    def test_evaluator_journals_failure_and_continues_next_case(self):
        profile = minimal_profile()
        sha = "a" * 64
        static = {"receiver_executable_sha256": sha,
                  "receiver_graph_sha256": sha,
                  "plugin_manifest": [],
                  "plugin_set_sha256": p3.digest_json([]),
                  "compiler_version": "clang test",
                  "compile_commands_sha256": sha,
                  "cxx_mode": "c++26", "git_head": sha,
                  "git_status_sha256": sha, "git_dirty": True,
                  "independent_tool_sha256": p3.digest_file(TOOL)}
        truth = {"scenario_sha256": sha, "iq_sha256": sha,
                 "sample_count": 10_000,
                 "channel": {"eb_n0_db": 60.0, "es_n0_db": 60.0,
                             "sample_snr_db": 40.0,
                             "desired_active_power_w": 1.0},
                 "events": [{"message_id": 1, "transmitter_id": 1,
                             "nominal_transmit_start_sample": 1000,
                             "received_start_sample": 1000.0,
                             "frequency_index": 24, "word": 0xAAAAAAAA}]}
        base_provenance = {**static, "command": ["receiver", "--graph-config"],
                           "timed_out": False,
                           "executor_timeout_seconds": 120,
                           "process_timeout_seconds": 150,
                           "stdout_sha256": sha, "stderr_sha256": sha,
                           "stdout_tail": "", "stderr_tail": "",
                           "effective_config_sha256": sha,
                           "effective_receiver_config_sha256": sha,
                           "iq_sha256": sha}
        failed_provenance = {**base_provenance, "return_status": 2}
        success_provenance = {**base_provenance, "return_status": 0}
        summary = {"completion_signaled": True,
                   "fhss_diagnostics": {"decoded_pulses": [],
                                         "preamble_lock": False},
                   "diagnostics_snapshot": [
                       {"diagnostics": {"schema": "graphx.fhss.production_channelizer.diagnostics.v1",
                                        "allocation_high_water_bytes": 10}},
                       {"diagnostics": {"schema": "graphx.fhss.acquisition_detector.diagnostics.v1",
                                        "allocation_high_water_bytes": 20}}]}
        with tempfile.TemporaryDirectory() as directory:
            raw_path = Path(directory) / "progress.json"
            with (mock.patch.object(p3, "receiver_environment", return_value=static),
                  mock.patch.object(p3, "write_capture", return_value=truth),
                  mock.patch.object(
                      p3, "run_receiver",
                      side_effect=[({**summary, "completion_signaled": False},
                                    failed_provenance),
                                   (summary, success_provenance)])):
                raw = p3.evaluate(profile, Path("receiver"), Path("graph"),
                                  Path("plugins"), "evaluation",
                                  raw_path=raw_path)
            self.assertEqual(len(raw["cases"]), 2)
            self.assertEqual(raw["cases"][0]["execution_status"], "failed")
            self.assertTrue(raw["cases"][0]["failure"]["summary_available"])
            self.assertFalse(raw["cases"][0]["failure"]
                             ["receiver_summary"]["completion_signaled"])
            self.assertEqual(raw["cases"][1]["execution_status"], "completed")
            persisted = json.loads(raw_path.read_text())
            self.assertEqual(persisted["cases_sha256"],
                             p3.digest_json(persisted["cases"]))
            self.assertEqual(len(persisted["cases"]), 2)
            report = p3.aggregate(profile, raw)
            self.assertFalse(report["machine_acceptance"]
                             ["all_receiver_executions_completed"]["pass"])
            self.assertFalse(report["overall_pass"])

    def test_parity_metrics_not_claimed_as_proof(self):
        data = p3.encode_iq([1+0j, 0+1j], "cf64_le")
        result = p3.parity_metrics(data, data, "cf64_le")
        self.assertEqual(result["maximum_complex_error"], 0.0)
        self.assertIn("not proof", result["interpretation"])


if __name__ == "__main__":
    unittest.main()
