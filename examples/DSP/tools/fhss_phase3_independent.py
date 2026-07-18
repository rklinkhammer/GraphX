#!/usr/bin/env python3
"""Independent FHSS/CPSM waveform, channel, and held-out evaluator.

This module deliberately imports no GraphX or libdsp code.  Its only receiver
interface is a binary IQ file and the public graph executable.  The equations
are written from docs/dsp/fhss_architecture.md and are tested with literal
vectors rather than production-generator parity alone.
"""

from __future__ import annotations

import argparse
import cmath
import hashlib
import json
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

VERSION = "graphx.fhss.phase3-independent.v4"
SAMPLE_RATE_HZ = 500_000_000.0
BIT_RATE_HZ = 5_000_000.0
SAMPLES_PER_SYMBOL = 100
BITS_PER_PULSE = 32
PULSE_SAMPLES = 3_200
GAP_SAMPLES = 3_300
PERIOD_SAMPLES = 6_500
PREAMBLE_PULSES = 16
MAX_PULSES = 256
H = 0.5
IQ_FIRST_HZ = -236_250_000.0
IQ_SPACING_HZ = 7_500_000.0
RF_BASE_HZ = 1_000_000_000.0
RF_SPACING_HZ = 8_000_000.0
ACTIVE = (24, 28, 32, 36)
PREAMBLE_WORDS = (0xAAAAAAAA, 0x77777777, 0x12121212, 0x62626262)
IMPAIRMENT_ORDER = (
    "fractional_timing_and_sample_clock",
    "multipath_and_fading",
    "doppler_cfo_and_phase",
    "blockers_and_collisions",
    "iq_imbalance_and_dc",
    "agc",
    "awgn",
    "clipping",
    "quantization",
)


def _exact_keys(value: dict[str, Any], allowed: set[str], context: str) -> None:
    unknown = set(value) - allowed
    if unknown:
        raise ValueError(f"{context} has unknown properties: {sorted(unknown)}")


def _finite(value: Any, context: str, minimum: float | None = None,
            maximum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{context} must be a finite number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{context} must be finite")
    if minimum is not None and result < minimum:
        raise ValueError(f"{context} is below its minimum")
    if maximum is not None and result > maximum:
        raise ValueError(f"{context} exceeds its maximum")
    return result


def validate_scenario(raw: dict[str, Any]) -> None:
    """Strict internal validation; JSON Schema tooling is not required."""
    if not isinstance(raw, dict):
        raise ValueError("scenario must be an object")
    _exact_keys(raw, {"schema", "scenario_id", "seed", "sample_format",
                      "active_frequency_indices", "allow_overlap", "tail_samples",
                      "messages", "channel"}, "scenario")
    if raw.get("schema") not in ("graphx.fhss.phase3-scenario.v1",
                                  "graphx.fhss.phase3-scenario.v2"):
        raise ValueError("unsupported scenario schema")
    if not isinstance(raw.get("scenario_id"), str) or not raw["scenario_id"] or len(raw["scenario_id"]) > 128:
        raise ValueError("scenario_id must be a non-empty bounded string")
    if isinstance(raw.get("seed"), bool) or not isinstance(raw.get("seed"), int) or not 0 <= raw["seed"] <= 0x7FFF_FFFF:
        raise ValueError("seed must be a non-negative int32")
    if raw.get("sample_format", "cf32_le") not in ("cf32_le", "cf64_le"):
        raise ValueError("unsupported sample format")
    if raw.get("active_frequency_indices", list(ACTIVE)) != list(ACTIVE):
        raise ValueError(f"active frequencies must be exactly {ACTIVE}")
    if not isinstance(raw.get("allow_overlap", False), bool):
        raise ValueError("allow_overlap must be boolean")
    if isinstance(raw.get("tail_samples", PERIOD_SAMPLES), bool) or not isinstance(raw.get("tail_samples", PERIOD_SAMPLES), int) or not 0 <= raw.get("tail_samples", PERIOD_SAMPLES) <= 1_000_000:
        raise ValueError("tail_samples must be a bounded non-negative integer")
    messages = raw.get("messages")
    if not isinstance(messages, list) or not 1 <= len(messages) <= 16:
        raise ValueError("messages must contain 1..16 entries")
    message_ids: set[int] = set()
    for mi, message in enumerate(messages):
        if not isinstance(message, dict):
            raise ValueError("message must be an object")
        _exact_keys(message, {"message_id", "transmitter_id", "transmit_start_sample",
                              "initial_phase_rad", "randomize_initial_phase", "amplitude",
                              "pulses"}, f"message[{mi}]")
        for key in ("message_id", "transmitter_id", "transmit_start_sample"):
            if isinstance(message.get(key, 1 if key == "transmitter_id" else None), bool) or not isinstance(message.get(key, 1 if key == "transmitter_id" else None), int):
                raise ValueError(f"message[{mi}].{key} must be an integer")
        if message["message_id"] in message_ids or message["message_id"] < 0:
            raise ValueError("message_id must be unique and non-negative")
        message_ids.add(message["message_id"])
        if message.get("transmitter_id", 1) < 0 or message["transmit_start_sample"] < 0:
            raise ValueError("transmitter and start identifiers must be non-negative")
        _finite(message.get("initial_phase_rad", 0.0), "initial_phase_rad")
        _finite(message.get("amplitude", 1.0), "amplitude", 1e-12, 1e6)
        if not isinstance(message.get("randomize_initial_phase", False), bool):
            raise ValueError("randomize_initial_phase must be boolean")
        pulses = message.get("pulses")
        if not isinstance(pulses, list) or not PREAMBLE_PULSES <= len(pulses) <= MAX_PULSES:
            raise ValueError("each conformant message requires 16 preamble pulses and at most 240 body pulses")
        for pi, pulse in enumerate(pulses):
            if not isinstance(pulse, dict):
                raise ValueError("pulse must be an object")
            _exact_keys(pulse, {"frequency_index", "word", "role"}, f"pulse[{pi}]")
            if isinstance(pulse.get("frequency_index"), bool) or not isinstance(pulse.get("frequency_index"), int) or pulse["frequency_index"] not in ACTIVE:
                raise ValueError("pulse frequency must be active")
            if isinstance(pulse.get("word"), bool) or not isinstance(pulse.get("word"), int) or not 0 <= pulse["word"] <= 0xFFFF_FFFF:
                raise ValueError("pulse word must be uint32")
            expected_role = "preamble" if pi < PREAMBLE_PULSES else "body"
            if pulse.get("role") != expected_role:
                raise ValueError("pulse roles must be 16 preamble followed by body")
            if pi < PREAMBLE_PULSES:
                expected_word = PREAMBLE_WORDS[ACTIVE.index(pulse["frequency_index"])]
                if pulse["word"] != expected_word:
                    raise ValueError("repeated preamble hop must use its architecture word")
    channel = raw.get("channel", {})
    if not isinstance(channel, dict):
        raise ValueError("channel must be an object")
    _exact_keys(channel, {"timing", "multipath", "fading", "carrier", "blockers",
                          "awgn", "hardware"}, "channel")
    timing = channel.get("timing", {})
    _exact_keys(timing, {"fractional_delay_samples", "sample_clock_offset_ppm"}, "timing")
    _finite(timing.get("fractional_delay_samples", 0.0), "fractional delay", 0.0, 10_000.0)
    _finite(timing.get("sample_clock_offset_ppm", 0.0), "sample clock offset", -10_000.0, 10_000.0)
    multipath = channel.get("multipath", {})
    _exact_keys(multipath, {"taps"}, "multipath")
    taps = multipath.get("taps", [{"delay_samples": 0.0, "power_linear": 1.0}])
    if not isinstance(taps, list) or not 1 <= len(taps) <= 64:
        raise ValueError("multipath taps must contain 1..64 entries")
    total_power = 0.0
    for tap in taps:
        if not isinstance(tap, dict):
            raise ValueError("TDL tap must be an object")
        _exact_keys(tap, {"delay_samples", "power_linear", "phase_rad"}, "TDL tap")
        _finite(tap.get("delay_samples"), "TDL delay", 0.0, 100_000.0)
        total_power += _finite(tap.get("power_linear"), "TDL power", 0.0, 1e6)
        _finite(tap.get("phase_rad", 0.0), "TDL phase")
    if total_power <= 0.0:
        raise ValueError("TDL total power must be positive")
    fading = channel.get("fading", {})
    _exact_keys(fading, {"kind", "k_factor_db", "max_doppler_hz", "oscillators"}, "fading")
    if fading.get("kind", "none") not in ("none", "rayleigh", "rician", "sum_of_sinusoids_rayleigh"):
        raise ValueError("unsupported fading kind")
    _finite(fading.get("k_factor_db", 6.0), "K factor", -100.0, 100.0)
    _finite(fading.get("max_doppler_hz", 1_000.0), "maximum Doppler", 0.0, SAMPLE_RATE_HZ/2)
    oscillators = fading.get("oscillators", 16)
    if isinstance(oscillators, bool) or not isinstance(oscillators, int) or not 4 <= oscillators <= 256:
        raise ValueError("oscillators must be in [4,256]")
    carrier = channel.get("carrier", {})
    _exact_keys(carrier, {"cfo_hz", "initial_phase_rad", "doppler_hz",
                          "phase_noise_step_std_rad"}, "carrier")
    for key in ("cfo_hz", "initial_phase_rad", "doppler_hz"):
        _finite(carrier.get(key, 0.0), key, -SAMPLE_RATE_HZ/2 if key != "initial_phase_rad" else None,
                SAMPLE_RATE_HZ/2 if key != "initial_phase_rad" else None)
    _finite(carrier.get("phase_noise_step_std_rad", 0.0), "phase noise", 0.0, 10.0)
    blockers = channel.get("blockers", [])
    if not isinstance(blockers, list) or len(blockers) > 16:
        raise ValueError("blockers must be a bounded array")
    for blocker in blockers:
        if not isinstance(blocker, dict):
            raise ValueError("blocker must be an object")
        _exact_keys(blocker, {"frequency_hz", "sir_db", "phase_rad"}, "blocker")
        _finite(blocker.get("frequency_hz"), "blocker frequency", -SAMPLE_RATE_HZ/2, SAMPLE_RATE_HZ/2)
        _finite(blocker.get("sir_db"), "blocker SIR", -200.0, 200.0)
        _finite(blocker.get("phase_rad", 0.0), "blocker phase")
    awgn = channel.get("awgn", {})
    _exact_keys(awgn, {"eb_n0_db"}, "awgn")
    _finite(awgn.get("eb_n0_db", 300.0), "Eb/N0", -100.0, 300.0)
    hardware_cfg = channel.get("hardware", {})
    _exact_keys(hardware_cfg, {"iq_gain_imbalance_db", "iq_phase_imbalance_deg",
                               "dc_i", "dc_q", "agc_transient_samples",
                               "clip_magnitude", "quantization_bits"}, "hardware")
    for key in ("iq_gain_imbalance_db", "iq_phase_imbalance_deg", "dc_i", "dc_q"):
        _finite(hardware_cfg.get(key, 0.0), key, -100.0, 100.0)
    _finite(hardware_cfg.get("clip_magnitude", 0.0), "clip magnitude", 0.0, 1e6)
    for key, low, high in (("agc_transient_samples", 0, 10_000_000), ("quantization_bits", 0, 24)):
        value = hardware_cfg.get(key, 0)
        if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high or (key == "quantization_bits" and value == 1):
            raise ValueError(f"invalid {key}")


def validate_profile(profile: dict[str, Any]) -> None:
    if not isinstance(profile, dict):
        raise ValueError("profile must be an object")
    _exact_keys(profile, {"schema", "version", "claim_level", "prohibited_claims",
                          "waveform", "statistics", "matrix", "frozen", "freeze",
                          "limitations", "engineering_rationale"}, "profile")
    if profile.get("schema") != "graphx.fhss.phase3-validation-profile.v6" or profile.get("version") != 6:
        raise ValueError("only the post-v5-timeout v6 profile is accepted")
    matrix = profile.get("matrix")
    if not isinstance(matrix, list) or not matrix:
        raise ValueError("profile matrix must be non-empty")
    allowed = {"id", "family", "eb_n0_db", "cfo_hz", "sample_clock_offset_ppm",
               "fractional_delay_samples", "taps", "fading", "k_factor_db",
               "max_doppler_hz", "doppler_hz", "blockers", "collision",
               "sir_db", "relative_timing_samples", "phase_noise_step_std_rad",
               "hardware", "payload_pulses", "fixed_burst_epoch_samples"}
    ids: set[str] = set()
    for point in matrix:
        if not isinstance(point, dict):
            raise ValueError("matrix point must be an object")
        _exact_keys(point, allowed, "matrix point")
        if not isinstance(point.get("id"), str) or not point["id"] or point["id"] in ids:
            raise ValueError("matrix point ids must be unique non-empty strings")
        ids.add(point["id"])
        if not isinstance(point.get("family"), str) or not point["family"]:
            raise ValueError("matrix family must be non-empty")
        if "eb_n0_db" in point:
            _finite(point["eb_n0_db"], "matrix Eb/N0", -100.0, 300.0)
        if "collision" in point:
            collision = point["collision"]
            if not isinstance(collision, dict):
                raise ValueError("collision must be an object")
            _exact_keys(collision, {"relative_start_samples", "relative_power_db",
                                    "same_hops"}, "collision")
            if (isinstance(collision.get("relative_start_samples"), bool) or
                    not isinstance(collision.get("relative_start_samples"), int)):
                raise ValueError("collision relative start must be integer")
            _finite(collision.get("relative_power_db"), "collision power", -100.0, 100.0)
            if not isinstance(collision.get("same_hops"), bool):
                raise ValueError("collision same_hops must be boolean")
            if "sir_db" in point and not math.isclose(
                    float(point["sir_db"]), -float(collision["relative_power_db"]),
                    abs_tol=1e-12):
                raise ValueError("collision SIR must be desired transmitter power over interferer power")
            if "relative_timing_samples" in point and int(point["relative_timing_samples"]) != int(collision["relative_start_samples"]):
                raise ValueError("collision relative timing axis mismatch")
    statistics = profile.get("statistics")
    if not isinstance(statistics, dict):
        raise ValueError("statistics must be an object")
    _exact_keys(statistics, {"seed_partitions", "gate_extension_point_ids",
                             "confidence", "stopping_rule", "failure_policy"},
                "statistics")
    partitions = statistics.get("seed_partitions")
    if not isinstance(partitions, dict) or set(partitions) != {
            "development", "validation", "evaluation", "gate_extension"}:
        raise ValueError("profile requires four explicit seed partitions")
    for name, seeds in partitions.items():
        if (not isinstance(seeds, list) or not seeds or
                any(isinstance(seed, bool) or not isinstance(seed, int) or seed < 0
                    for seed in seeds) or len(seeds) != len(set(seeds))):
            raise ValueError(f"invalid {name} seed partition")
    frozen = profile.get("frozen")
    if not isinstance(frozen, dict):
        raise ValueError("frozen profile section must be an object")
    _exact_keys(frozen, {"matching_tolerance_samples", "frequency_match",
                         "impairment_order", "burst_epoch_range_samples",
                         "receiver_executor_timeout_seconds",
                         "receiver_process_timeout_seconds", "matrix_sha256",
                         "seed_partitions_sha256", "gates"}, "frozen")
    if tuple(frozen.get("impairment_order", [])) != IMPAIRMENT_ORDER:
        raise ValueError("frozen impairment order differs from executable order")
    epoch_range = frozen.get("burst_epoch_range_samples")
    if (not isinstance(epoch_range, list) or len(epoch_range) != 2 or
            any(isinstance(value, bool) or not isinstance(value, int) for value in epoch_range) or
            not 0 <= epoch_range[0] <= epoch_range[1]):
        raise ValueError("invalid burst epoch range")
    executor_timeout = frozen.get("receiver_executor_timeout_seconds")
    process_timeout = frozen.get("receiver_process_timeout_seconds")
    if (isinstance(executor_timeout, bool) or not isinstance(executor_timeout, int) or
            not 1 <= executor_timeout <= 3_600):
        raise ValueError("invalid receiver executor timeout")
    if (isinstance(process_timeout, bool) or not isinstance(process_timeout, int) or
            not executor_timeout < process_timeout <= 3_600):
        raise ValueError("receiver process timeout must exceed executor timeout")
    gates = frozen.get("gates")
    if not isinstance(gates, list) or not gates:
        raise ValueError("frozen gates must be non-empty")
    for gate in gates:
        _exact_keys(gate, {"id", "point_id", "metric", "statistic", "comparison",
                           "limit", "units"}, "gate")
        if gate.get("point_id") not in ids or gate.get("comparison") not in (">=", "<="):
            raise ValueError("invalid gate target/comparison")
        _finite(gate.get("limit"), "gate limit")
    freeze = profile.get("freeze")
    if not isinstance(freeze, dict):
        raise ValueError("profile freeze record must be an object")
    _exact_keys(freeze, {"status", "sha256"}, "freeze")
    if freeze.get("status") not in ("pilot_not_frozen",
                                    "frozen_before_held_out_evaluation"):
        raise ValueError("v6 profile freeze status is invalid")
    if (not isinstance(freeze.get("sha256"), str) or
            len(freeze["sha256"]) != 64):
        raise ValueError("v6 frozen-section hash is malformed")


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def digest_json(value: Any) -> str:
    return digest_bytes(canonical_bytes(value))


def digest_file(path: Path) -> str:
    return digest_bytes(path.read_bytes())


def write_json_atomic(path: Path, value: Any) -> None:
    """Durably replace a progress artifact without exposing partial JSON."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def iq_offset(index: int) -> float:
    return IQ_FIRST_HZ + IQ_SPACING_HZ * index


def rf_frequency(index: int) -> float:
    return RF_BASE_HZ + RF_SPACING_HZ * index


@dataclass(frozen=True)
class Pulse:
    frequency_index: int
    word: int
    role: str


@dataclass(frozen=True)
class Message:
    message_id: int
    transmitter_id: int
    start_sample: int
    initial_phase_rad: float
    amplitude: float
    pulses: tuple[Pulse, ...]


def validate_message(message: Message) -> None:
    if not 1 <= len(message.pulses) <= MAX_PULSES:
        raise ValueError("message pulse count must be in [1,256]")
    if message.start_sample < 0 or not math.isfinite(message.initial_phase_rad):
        raise ValueError("message timing and phase must be finite/non-negative")
    if not math.isfinite(message.amplitude) or message.amplitude <= 0.0:
        raise ValueError("message amplitude must be finite and positive")
    for pulse in message.pulses:
        if pulse.frequency_index not in ACTIVE:
            raise ValueError("pulse frequency must be one of four active frequencies")
        if not 0 <= pulse.word <= 0xFFFFFFFF:
            raise ValueError("pulse word is not uint32")


def parse_messages(raw: dict[str, Any]) -> list[Message]:
    validate_scenario(raw)
    active = tuple(raw.get("active_frequency_indices", ACTIVE))
    if len(active) != 4 or len(set(active)) != 4 or any(i < 1 or i > 62 for i in active):
        raise ValueError("exactly four distinct selectable active frequencies required")
    if active != ACTIVE:
        raise ValueError(f"v1 receiver profile requires active frequencies {ACTIVE}")
    result: list[Message] = []
    for item in raw["messages"]:
        phase = item.get("initial_phase_rad", 0.0)
        if item.get("randomize_initial_phase", False):
            phase = random.Random(int(raw["seed"]) ^ int(item["message_id"])).uniform(-math.pi, math.pi)
        pulses = tuple(Pulse(int(p["frequency_index"]), int(p["word"]), str(p["role"])) for p in item["pulses"])
        message = Message(int(item["message_id"]), int(item.get("transmitter_id", 1)),
                          int(item["transmit_start_sample"]), float(phase),
                          float(item.get("amplitude", 1.0)), pulses)
        validate_message(message)
        result.append(message)
    if not result:
        raise ValueError("at least one message required")
    if not raw.get("allow_overlap", False):
        spans = sorted((m.start_sample, m.start_sample + (len(m.pulses)-1)*PERIOD_SAMPLES + PULSE_SAMPLES) for m in result)
        if any(spans[i][0] < spans[i-1][1] for i in range(1, len(spans))):
            raise ValueError("overlapping messages require allow_overlap=true")
    return result


def pulse_sample(word: int, local_sample: int, global_sample: int,
                 offset_hz: float, initial_phase: float = 0.0,
                 amplitude: float = 1.0) -> complex:
    """Literal full-response binary CPSM, h=1/2, MSB first."""
    symbol_index, within = divmod(local_sample, SAMPLES_PER_SYMBOL)
    completed = 0.0
    for prior in range(symbol_index):
        bit = (word >> (31 - prior)) & 1
        completed += H if bit == 0 else -H
    bit = (word >> (31 - symbol_index)) & 1
    symbol = 1.0 if bit == 0 else -1.0
    q = H * within / SAMPLES_PER_SYMBOL
    cpsm_phase = math.pi * (completed + symbol * q)
    hop_phase = 2.0 * math.pi * offset_hz * global_sample / SAMPLE_RATE_HZ
    return amplitude * cmath.exp(1j * (initial_phase + cpsm_phase + hop_phase))


def synthesize(messages: list[Message], tail_samples: int = PERIOD_SAMPLES) -> tuple[list[complex], list[dict[str, Any]]]:
    count = max(m.start_sample + (len(m.pulses)-1)*PERIOD_SAMPLES + PULSE_SAMPLES for m in messages) + tail_samples
    if count > 4_194_304:
        raise ValueError("independent generator capture exceeds bounded v1 limit")
    samples = [0j] * count
    truth: list[dict[str, Any]] = []
    for message in messages:
        for ordinal, pulse in enumerate(message.pulses):
            start = message.start_sample + ordinal * PERIOD_SAMPLES
            offset = iq_offset(pulse.frequency_index)
            for local in range(PULSE_SAMPLES):
                samples[start + local] += pulse_sample(
                    pulse.word, local, start + local, offset,
                    message.initial_phase_rad, message.amplitude)
            truth.append({
                "message_id": message.message_id,
                "transmitter_id": message.transmitter_id,
                "pulse_ordinal": ordinal,
                "nominal_transmit_start_sample": start,
                "received_start_sample": float(start),
                "received_end_sample": float(start + PULSE_SAMPLES),
                "frequency_index": pulse.frequency_index,
                "rf_frequency_hz": rf_frequency(pulse.frequency_index),
                "iq_offset_frequency_hz": offset,
                "word": pulse.word,
                "role": pulse.role,
                "amplitude_linear": message.amplitude,
                "signal_power_linear": message.amplitude ** 2,
                "dropped": False,
                "collided": False,
                "temporal_overlap": False,
                "same_hop_overlap": False,
            })
    for left in range(len(truth)):
        for right in range(left + 1, len(truth)):
            a, b = truth[left], truth[right]
            overlaps = a["nominal_transmit_start_sample"] < b["nominal_transmit_start_sample"] + PULSE_SAMPLES and b["nominal_transmit_start_sample"] < a["nominal_transmit_start_sample"] + PULSE_SAMPLES
            if overlaps:
                a["collided"] = True
                b["collided"] = True
                a["temporal_overlap"] = True
                b["temporal_overlap"] = True
                if a["frequency_index"] == b["frequency_index"]:
                    a["same_hop_overlap"] = True
                    b["same_hop_overlap"] = True
    return samples, truth


def signal_power(samples: Iterable[complex]) -> float:
    values = list(samples)
    return sum(abs(x) ** 2 for x in values) / len(values) if values else 0.0


def active_signal_power(samples: Iterable[complex], threshold: float = 1e-15) -> float:
    active = [value for value in samples if abs(value) > threshold]
    return signal_power(active)


def sinc_resample(samples: list[complex], fractional_delay: float,
                  clock_ppm: float, half_width: int = 12) -> list[complex]:
    if not math.isfinite(fractional_delay) or not math.isfinite(clock_ppm) or abs(clock_ppm) > 10_000:
        raise ValueError("invalid timing impairment")
    ratio = 1.0 + clock_ppm * 1e-6
    out_count = max(0, int(math.ceil(len(samples) * ratio + max(0.0, fractional_delay))))
    output: list[complex] = []
    for out_index in range(out_count):
        position = out_index / ratio - fractional_delay
        center = math.floor(position)
        value = 0j
        for index in range(center - half_width + 1, center + half_width + 1):
            if 0 <= index < len(samples):
                distance = position - index
                sinc = 1.0 if distance == 0 else math.sin(math.pi * distance) / (math.pi * distance)
                window = 0.5 + 0.5 * math.cos(math.pi * distance / half_width) if abs(distance) < half_width else 0.0
                weight = sinc * window
                value += samples[index] * weight
        # Samples outside the finite capture are zero.  Renormalizing only the
        # in-range weights would create a noncausal edge gain transient.
        output.append(value)
    return output


def fractional_tdl(samples: list[complex], taps: list[dict[str, Any]]) -> list[complex]:
    if not taps:
        return samples[:]
    powers = [float(t["power_linear"]) for t in taps]
    if any(not math.isfinite(p) or p < 0 for p in powers) or sum(powers) <= 0:
        raise ValueError("TDL powers must be finite, non-negative, and nonzero")
    normalization = math.sqrt(sum(powers))
    max_delay = max(float(t["delay_samples"]) for t in taps)
    output = [0j] * (len(samples) + math.ceil(max_delay) + 1)
    for tap, power in zip(taps, powers):
        delay = float(tap["delay_samples"])
        coefficient = cmath.rect(math.sqrt(power) / normalization, float(tap.get("phase_rad", 0.0)))
        delayed = sinc_resample(samples, delay, 0.0, 12)
        for index, value in enumerate(delayed[:len(output)]):
            output[index] += coefficient * value
    return output


def fading_coefficient(rng: random.Random, kind: str, k_factor_db: float = 6.0) -> complex:
    diffuse = complex(rng.gauss(0, 1 / math.sqrt(2)), rng.gauss(0, 1 / math.sqrt(2)))
    if kind == "rayleigh":
        return diffuse
    if kind == "rician":
        k = 10 ** (k_factor_db / 10)
        return math.sqrt(k / (k + 1)) + math.sqrt(1 / (k + 1)) * diffuse
    if kind == "none":
        return 1 + 0j
    raise ValueError("fading must be none, rayleigh, or rician")


def sum_of_sinusoids_components(max_doppler_hz: float, rng: random.Random,
                                oscillators: int = 16) -> list[tuple[float, float]]:
    """Return the declared tone frequencies and seeded initial phases."""
    if oscillators < 4 or not math.isfinite(max_doppler_hz) or max_doppler_hz <= 0:
        raise ValueError("invalid time-varying fading parameters")
    return [(max_doppler_hz * math.cos(2 * math.pi * (k + 0.5) / oscillators),
             rng.uniform(-math.pi, math.pi)) for k in range(oscillators)]


def sum_of_sinusoids_fading(count: int, sample_rate_hz: float,
                            max_doppler_hz: float, rng: random.Random,
                            oscillators: int = 16) -> list[complex]:
    """Reproducible unit-power engineering Rayleigh fading process."""
    if count < 0:
        raise ValueError("invalid time-varying fading parameters")
    components = sum_of_sinusoids_components(max_doppler_hz, rng, oscillators)
    scale = 1/math.sqrt(oscillators)
    return [scale*sum(cmath.exp(1j*(2*math.pi*frequency*n/sample_rate_hz + phase))
                      for frequency, phase in components) for n in range(count)]


def apply_phase_frequency(samples: list[complex], cfo_hz: float,
                          phase_rad: float, doppler_hz: float,
                          phase_noise_std_rad: float, rng: random.Random) -> list[complex]:
    if (not all(math.isfinite(v) for v in (cfo_hz, phase_rad, doppler_hz,
                                           phase_noise_std_rad)) or
            phase_noise_std_rad < 0.0):
        raise ValueError("non-finite carrier impairment")
    phase_noise = 0.0
    result = []
    for index, sample in enumerate(samples):
        phase_noise += rng.gauss(0.0, phase_noise_std_rad)
        phase = phase_rad + 2 * math.pi * (cfo_hz + doppler_hz) * index / SAMPLE_RATE_HZ + phase_noise
        result.append(sample * cmath.exp(1j * phase))
    return result


def add_tone(samples: list[complex], frequency_hz: float, sir_db: float,
             phase_rad: float = 0.0,
             reference_power: float | None = None) -> list[complex]:
    wanted = max(active_signal_power(samples) if reference_power is None else reference_power,
                 1e-30)
    blocker_amplitude = math.sqrt(wanted / (10 ** (sir_db / 10)))
    return [sample + blocker_amplitude * cmath.exp(1j * (phase_rad + 2*math.pi*frequency_hz*i/SAMPLE_RATE_HZ))
            for i, sample in enumerate(samples)]


def apply_iq_imbalance_dc(samples: list[complex], config: dict[str, Any]) -> list[complex]:
    gain = float(config.get("iq_gain_imbalance_db", 0.0))
    phase = math.radians(float(config.get("iq_phase_imbalance_deg", 0.0)))
    dc = complex(float(config.get("dc_i", 0.0)), float(config.get("dc_q", 0.0)))
    g_i, g_q = 10 ** (gain / 40), 10 ** (-gain / 40)
    output: list[complex] = []
    for sample in samples:
        q_rot = sample.imag * math.cos(phase) + sample.real * math.sin(phase)
        output.append(complex(g_i * sample.real, g_q * q_rot) + dc)
    return output


def apply_agc(samples: list[complex], transient_samples: int) -> list[complex]:
    if transient_samples < 0:
        raise ValueError("AGC transient must be non-negative")
    if transient_samples == 0:
        return samples[:]
    return [sample * (1.0 - math.exp(-(index + 1) / transient_samples))
            for index, sample in enumerate(samples)]


def apply_clipping(samples: list[complex], magnitude: float) -> list[complex]:
    if not math.isfinite(magnitude) or magnitude < 0.0:
        raise ValueError("clip magnitude must be finite and non-negative")
    if magnitude == 0.0:
        return samples[:]
    return [sample if abs(sample) <= magnitude else sample * magnitude / abs(sample)
            for sample in samples]


def apply_quantization(samples: list[complex], bits: int,
                       full_scale: float) -> list[complex]:
    if bits == 0:
        return samples[:]
    if not 2 <= bits <= 24 or not math.isfinite(full_scale) or full_scale <= 0.0:
        raise ValueError("quantization requires 2..24 bits and positive full scale")
    scale = (2 ** (bits - 1) - 1) / full_scale
    limit = 2 ** (bits - 1) - 1
    def quantize(value: float) -> float:
        code = max(-limit, min(limit, round(value * scale)))
        return code / scale
    return [complex(quantize(sample.real), quantize(sample.imag)) for sample in samples]


def hardware(samples: list[complex], config: dict[str, Any]) -> list[complex]:
    """Compatibility composition in the recorded execution order."""
    output = apply_iq_imbalance_dc(samples, config)
    output = apply_agc(output, int(config.get("agc_transient_samples", 0)))
    output = apply_clipping(output, float(config.get("clip_magnitude", 0.0)))
    return apply_quantization(output, int(config.get("quantization_bits", 0)),
                              max(float(config.get("clip_magnitude", 0.0)), 1.0))


def add_awgn(samples: list[complex], snr_db: float, rng: random.Random,
             reference_power: float | None = None) -> tuple[list[complex], float]:
    power = signal_power(samples) if reference_power is None else reference_power
    noise_power = power / (10 ** (snr_db / 10))
    sigma = math.sqrt(noise_power / 2)
    return [x + complex(rng.gauss(0, sigma), rng.gauss(0, sigma)) for x in samples], noise_power


def noise_calibration(active_power_w: float, eb_n0_db: float) -> dict[str, float]:
    """Complex-baseband calibration for one bit per CPSM symbol.

    E|n[k]|^2 = N0*Fs, Eb=Es=P/Rb, and sample-SNR=P/E|n[k]|^2.
    """
    if not math.isfinite(active_power_w) or active_power_w <= 0.0:
        raise ValueError("active wanted power must be finite and positive")
    if not math.isfinite(eb_n0_db):
        raise ValueError("Eb/N0 must be finite")
    eb_n0 = 10 ** (eb_n0_db / 10)
    bit_energy_j = active_power_w / BIT_RATE_HZ
    n0_w_per_hz = bit_energy_j / eb_n0
    complex_noise_variance_w = n0_w_per_hz * SAMPLE_RATE_HZ
    sample_snr_db = 10 * math.log10(active_power_w / complex_noise_variance_w)
    return {"desired_active_power_w": active_power_w,
            "bit_energy_j": bit_energy_j,
            "symbol_energy_j": bit_energy_j,
            "n0_w_per_hz": n0_w_per_hz,
            "complex_noise_variance_w": complex_noise_variance_w,
            "sample_snr_db": sample_snr_db,
            "es_n0_db": eb_n0_db,
            "eb_n0_db": eb_n0_db}


def apply_post_blocker_stages(samples: list[complex], hardware_config: dict[str, Any],
                              eb_n0_db: float, desired_active_power: float,
                              rng: random.Random) -> tuple[list[complex], dict[str, float]]:
    """Execute IQ/DC -> AGC -> AWGN -> clipping -> quantization exactly."""
    output = apply_iq_imbalance_dc(samples, hardware_config)
    output = apply_agc(output, int(hardware_config.get("agc_transient_samples", 0)))
    calibration = noise_calibration(desired_active_power, eb_n0_db)
    if eb_n0_db < 250:
        sigma = math.sqrt(calibration["complex_noise_variance_w"] / 2)
        output = [value + complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
                  for value in output]
    output = apply_clipping(output, float(hardware_config.get("clip_magnitude", 0.0)))
    output = apply_quantization(output, int(hardware_config.get("quantization_bits", 0)),
                                max(float(hardware_config.get("clip_magnitude", 0.0)), 1.0))
    return output, calibration


def apply_channel(samples: list[complex], config: dict[str, Any], seed: int,
                  desired_samples: list[complex] | None = None) -> tuple[list[complex], dict[str, Any]]:
    rng = random.Random(seed)
    timing = config.get("timing", {})
    fractional_delay = float(timing.get("fractional_delay_samples", 0.0))
    clock_ppm = float(timing.get("sample_clock_offset_ppm", 0.0))
    output = sinc_resample(samples, fractional_delay, clock_ppm)
    desired = sinc_resample(desired_samples if desired_samples is not None else samples,
                            fractional_delay, clock_ppm)
    tdl = config.get("multipath", {}).get("taps", [{"delay_samples": 0.0, "power_linear": 1.0}])
    output = fractional_tdl(output, tdl)
    desired = fractional_tdl(desired, tdl)
    fade = config.get("fading", {})
    fading_kind = fade.get("kind", "none")
    if fading_kind == "sum_of_sinusoids_rayleigh":
        coefficients = sum_of_sinusoids_fading(
            len(output), SAMPLE_RATE_HZ,
            float(fade.get("max_doppler_hz", 1_000.0)), rng,
            int(fade.get("oscillators", 16)))
        output = [c*x for c, x in zip(coefficients, output)]
        desired = [c*x for c, x in zip(coefficients, desired)]
        coefficient = complex(math.sqrt(signal_power(coefficients)), 0.0)
    else:
        coefficient = fading_coefficient(rng, fading_kind, float(fade.get("k_factor_db", 6.0)))
        output = [coefficient * x for x in output]
        desired = [coefficient * x for x in desired]
    carrier = config.get("carrier", {})
    output = apply_phase_frequency(output, float(carrier.get("cfo_hz", 0.0)), float(carrier.get("initial_phase_rad", 0.0)),
                                   float(carrier.get("doppler_hz", 0.0)), float(carrier.get("phase_noise_step_std_rad", 0.0)), rng)
    desired_active_power = active_signal_power(desired)
    if desired_active_power <= 0.0:
        raise ValueError("wanted waveform has no active power")
    for blocker in config.get("blockers", []):
        output = add_tone(output, float(blocker["frequency_hz"]), float(blocker["sir_db"]),
                          float(blocker.get("phase_rad", 0.0)), desired_active_power)
    hardware_config = config.get("hardware", {})
    eb_n0_db = float(config.get("awgn", {}).get("eb_n0_db", 300.0))
    output, calibration = apply_post_blocker_stages(
        output, hardware_config, eb_n0_db, desired_active_power, rng)
    return output, {
        "seed": seed,
        "impairment_order": list(IMPAIRMENT_ORDER),
        **calibration,
        "noise_power_linear": calibration["complex_noise_variance_w"],
        "snr_db": calibration["sample_snr_db"],
        "cfo_hz": float(carrier.get("cfo_hz", 0.0)),
        "sample_clock_offset_ppm": float(timing.get("sample_clock_offset_ppm", 0.0)),
        "fractional_delay_samples": float(timing.get("fractional_delay_samples", 0.0)),
        "doppler_hz": float(carrier.get("doppler_hz", 0.0)),
        "fading_coefficient": [coefficient.real, coefficient.imag],
        "fading_kind": fading_kind,
        "channel_taps": tdl,
    }


def encode_iq(samples: list[complex], fmt: str) -> bytes:
    code = "<ff" if fmt == "cf32_le" else "<dd" if fmt == "cf64_le" else None
    if code is None:
        raise ValueError("format must be cf32_le or cf64_le")
    return b"".join(struct.pack(code, x.real, x.imag) for x in samples)


def decode_iq(data: bytes, fmt: str) -> list[complex]:
    code, width = ("<ff", 8) if fmt == "cf32_le" else ("<dd", 16) if fmt == "cf64_le" else (None, 0)
    if code is None or len(data) % width:
        raise ValueError("invalid IQ format or byte count")
    return [complex(*struct.unpack_from(code, data, offset)) for offset in range(0, len(data), width)]


def parity_metrics(independent: bytes, canonical: bytes, fmt: str) -> dict[str, Any]:
    left, right = decode_iq(independent, fmt), decode_iq(canonical, fmt)
    count = min(len(left), len(right))
    if count == 0:
        raise ValueError("cannot compare empty IQ")
    errors = [abs(left[i] - right[i]) for i in range(count)]
    phase_errors = [abs(cmath.phase(left[i] * right[i].conjugate()))
                    for i in range(count) if abs(left[i]) > 1e-12 and abs(right[i]) > 1e-12]
    def estimated_frequency(values: list[complex]) -> float:
        increments = [values[i] * values[i-1].conjugate() for i in range(1, len(values))
                      if abs(values[i]) > 1e-12 and abs(values[i-1]) > 1e-12]
        return (sum(cmath.phase(x) for x in increments) / len(increments) * SAMPLE_RATE_HZ / (2*math.pi)) if increments else 0.0
    return {
        "schema": "graphx.fhss.phase3-clean-parity.v1",
        "compared_samples": count,
        "independent_samples": len(left),
        "canonical_samples": len(right),
        "maximum_complex_error": max(errors),
        "rms_complex_error": math.sqrt(sum(x*x for x in errors)/count),
        "maximum_phase_error_rad": max(phase_errors, default=0.0),
        "frequency_error_hz": estimated_frequency(left[:count]) - estimated_frequency(right[:count]),
        "timing_placement_error_samples": 0 if [abs(x)>1e-12 for x in left[:count]] == [abs(x)>1e-12 for x in right[:count]] else None,
        "byte_equal_over_common_length": independent[:count*(8 if fmt == "cf32_le" else 16)] == canonical[:count*(8 if fmt == "cf32_le" else 16)],
        "interpretation": "agreement evidence only, not proof; independent analytical vectors are the correctness oracle",
    }


def write_capture(scenario: dict[str, Any], iq_path: Path, truth_path: Path) -> dict[str, Any]:
    messages = parse_messages(scenario)
    if iq_path == truth_path or iq_path.exists() or truth_path.exists():
        raise FileExistsError("IQ and truth outputs must be distinct and must not already exist")
    if not iq_path.parent.is_dir() or not truth_path.parent.is_dir():
        raise FileNotFoundError("output parent directory does not exist")
    tail = int(scenario.get("tail_samples", PERIOD_SAMPLES))
    clean, truth = synthesize(messages, tail)
    desired, _ = synthesize([messages[0]], tail)
    impaired, channel_truth = apply_channel(clean, scenario.get("channel", {}),
                                             int(scenario["seed"]), desired)
    fmt = scenario.get("sample_format", "cf32_le")
    encoded = encode_iq(impaired, fmt)
    for event in truth:
        ratio = 1.0 + channel_truth["sample_clock_offset_ppm"] * 1e-6
        fractional = channel_truth["fractional_delay_samples"]
        tdl_delay = min(float(t["delay_samples"]) for t in channel_truth["channel_taps"])
        event["received_start_sample"] = ratio * (
            event["nominal_transmit_start_sample"] + fractional) + tdl_delay
        event["received_end_sample"] = ratio * (
            event["nominal_transmit_start_sample"] + PULSE_SAMPLES + fractional) + tdl_delay
        event["sample_clock_ratio"] = ratio
        event["accumulated_clock_drift_samples"] = (
            event["nominal_transmit_start_sample"] * (ratio - 1.0))
        event.update({k: channel_truth[k] for k in (
            "noise_power_linear", "snr_db", "sample_snr_db", "es_n0_db",
            "eb_n0_db", "n0_w_per_hz", "bit_energy_j", "symbol_energy_j",
            "desired_active_power_w", "cfo_hz", "sample_clock_offset_ppm",
            "doppler_hz", "channel_taps", "impairment_order")})
    manifest = {
        "schema": "graphx.fhss.phase3-truth.v1",
        "generator": VERSION,
        "scenario_id": scenario["scenario_id"],
        "seed": int(scenario["seed"]),
        "sample_rate_hz": SAMPLE_RATE_HZ,
        "sample_format": fmt,
        "sample_count": len(impaired),
        "events": truth,
        "channel": channel_truth,
        "scenario_sha256": digest_json(scenario),
        "iq_sha256": digest_bytes(encoded),
    }
    truth_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    iq_temp: Path | None = None
    truth_temp: Path | None = None
    iq_committed = False
    try:
        with tempfile.NamedTemporaryFile(dir=iq_path.parent, prefix=f".{iq_path.name}.",
                                         delete=False) as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
            iq_temp = Path(handle.name)
        with tempfile.NamedTemporaryFile(dir=truth_path.parent, prefix=f".{truth_path.name}.",
                                         delete=False) as handle:
            handle.write(truth_bytes)
            handle.flush()
            os.fsync(handle.fileno())
            truth_temp = Path(handle.name)
        if iq_path.exists() or truth_path.exists():
            raise FileExistsError("output appeared during transactional generation")
        os.link(iq_temp, iq_path)
        iq_temp.unlink()
        iq_temp = None
        iq_committed = True
        os.link(truth_temp, truth_path)
        truth_temp.unlink()
        truth_temp = None
    except Exception:
        if iq_committed and iq_path.exists() and not truth_path.exists():
            iq_path.unlink()
        for temporary in (iq_temp, truth_temp):
            if temporary is not None and temporary.exists():
                temporary.unlink()
        raise
    return manifest


def match_events(truth: list[dict[str, Any]], detections: list[dict[str, Any]],
                 timing_tolerance: float, require_word: bool = False) -> dict[str, Any]:
    """Greedy minimum-time one-to-one assignment within frequency/timing gates."""
    candidates: list[tuple[float, int, int, int]] = []
    ambiguous_truth: set[int] = set()
    for left, event in enumerate(truth):
        for right in range(left + 1, len(truth)):
            other = truth[right]
            if (int(event["frequency_index"]) == int(other["frequency_index"]) and
                    float(event["received_start_sample"]) == float(other["received_start_sample"]) and
                    int(event["word"]) == int(other["word"]) and
                    int(event.get("transmitter_id", 0)) != int(other.get("transmitter_id", 0))):
                ambiguous_truth.update((left, right))
    for ti, event in enumerate(truth):
        for di, detection in enumerate(detections):
            if int(event["frequency_index"]) != int(detection["frequency_index"]):
                continue
            error = abs(float(event["received_start_sample"]) - float(detection["global_start_sample"]))
            if error <= timing_tolerance:
                word_distance = ((int(event["word"]) ^
                                  int(detection.get("decoded_value", 0))).bit_count()
                                 if require_word else 0)
                candidates.append((error, word_distance, ti, di))
    used_truth: set[int] = set()
    used_detections: set[int] = set()
    matches = []
    for error, _, ti, di in sorted(candidates):
        if ti in used_truth or di in used_detections:
            continue
        used_truth.add(ti); used_detections.add(di)
        bit_errors = (int(truth[ti]["word"]) ^ int(detections[di].get("decoded_value", 0))).bit_count()
        expected_cfo = float(truth[ti].get("cfo_hz", 0.0)) + float(truth[ti].get("doppler_hz", 0.0))
        matches.append({"truth_index": ti, "detection_index": di, "timing_error_samples": error,
                        "truth_word": int(truth[ti]["word"]),
                        "decoded_word": int(detections[di].get("decoded_value", 0)),
                        "confidence": float(detections[di].get("confidence", 0.0)),
                        "estimated_cfo_hz": float(detections[di].get("cfo_hz", 0.0)),
                        "cfo_error_hz": float(detections[di].get("cfo_hz", 0.0)) - expected_cfo,
                        "bit_errors": bit_errors, "word_error": bit_errors != 0,
                        "association_ambiguous": ti in ambiguous_truth})
    duplicate_count = 0
    for di, detection in enumerate(detections):
        if di in used_detections:
            continue
        if any(int(event["frequency_index"]) == int(detection["frequency_index"]) and
               abs(float(event["received_start_sample"]) - float(detection["global_start_sample"])) <= timing_tolerance
               for event in truth):
            duplicate_count += 1
    timing_confusion: dict[str, int] = {}
    available = set(range(len(detections)))
    for event in truth:
        candidates_by_time = [(abs(float(event["received_start_sample"]) - float(detections[di]["global_start_sample"])), di)
                              for di in available]
        if not candidates_by_time:
            continue
        error, di = min(candidates_by_time)
        if error <= timing_tolerance:
            available.remove(di)
            key = f"{int(event['frequency_index'])}->{int(detections[di]['frequency_index'])}"
            timing_confusion[key] = timing_confusion.get(key, 0) + 1
    matched_by_truth = {match["truth_index"]: match for match in matches}
    message_groups: dict[tuple[int, int], list[int]] = {}
    for index, event in enumerate(truth):
        key = (int(event.get("message_id", 0)), int(event.get("transmitter_id", 0)))
        message_groups.setdefault(key, []).append(index)
    message_results = []
    for (message_id, transmitter_id), indices in sorted(message_groups.items()):
        matched_indices = [index for index in indices if index in matched_by_truth and
                           not matched_by_truth[index]["association_ambiguous"]]
        word_errors = sum(bool(matched_by_truth[index]["word_error"])
                          for index in matched_indices)
        success = len(matched_indices) == len(indices) and word_errors == 0
        message_results.append({"message_id": message_id,
                                "transmitter_id": transmitter_id,
                                "truth_events": len(indices),
                                "matched_events": len(matched_indices),
                                "word_errors": word_errors,
                                "success": success,
                                "capture_probability": len(matched_indices) / len(indices)})
    collision_truth = sum(bool(event.get("collided", False)) for event in truth)
    collision_matches = sum(bool(truth[m["truth_index"]].get("collided", False)) for m in matches)
    temporal_overlap_truth = sum(bool(event.get("temporal_overlap", False)) for event in truth)
    same_hop_overlap_truth = sum(bool(event.get("same_hop_overlap", False)) for event in truth)
    return {
        "matches": matches,
        "truth_events": len(truth),
        "detections": len(detections),
        "matched_events": len(matches),
        "misses": len(truth) - len(matches),
        "false_detections": len(detections) - len(matches) - duplicate_count,
        "duplicates": duplicate_count,
        "frequency_confusion": timing_confusion,
        "collision_truth_events": collision_truth,
        "collision_matched_events": collision_matches,
        "temporal_overlap_truth_events": temporal_overlap_truth,
        "same_hop_overlap_truth_events": same_hop_overlap_truth,
        "bit_errors": sum(m["bit_errors"] for m in matches),
        "bits": len(matches) * BITS_PER_PULSE,
        "word_errors": sum(m["word_error"] for m in matches),
        "words": len(matches),
        "association_errors": len(truth) - len(matches),
        "ambiguous_associations": sum(match["association_ambiguous"] for match in matches),
        "message_results": message_results,
        "message_errors": sum(not result["success"] for result in message_results),
        "messages": len(message_results),
        "message_error": any(not result["success"] for result in message_results),
    }


def wilson(successes: int, trials: int, z: float = 1.959963984540054) -> list[float]:
    if trials <= 0 or not 0 <= successes <= trials:
        raise ValueError("invalid Wilson counts")
    p = successes/trials
    den = 1 + z*z/trials
    center = (p + z*z/(2*trials))/den
    radius = z*math.sqrt((p*(1-p)+z*z/(4*trials))/trials)/den
    lower = center-radius
    upper = center+radius
    return [0.0 if abs(lower) < 1e-15 else max(0.0, lower),
            1.0 if abs(1.0-upper) < 1e-15 else min(1.0, upper)]


def gate_statistic_value(row: dict[str, Any], gate: dict[str, Any]) -> tuple[float | None, float | None]:
    """Return the point estimate and the predeclared gate statistic."""
    point_estimate = row.get(gate["metric"])
    statistic = gate.get("statistic", "point_estimate")
    interval_key = {"pd": "pd_ci95", "conditional_ber": "conditional_ber_ci95",
                    "conditional_wer": "conditional_wer_ci95", "per": "per_ci95",
                    "false_alarms_per_sample": "false_alarm_ci95"}.get(gate["metric"])
    if statistic == "point_estimate":
        return point_estimate, point_estimate
    if statistic in ("lower_ci95", "upper_ci95") and interval_key:
        return point_estimate, row[interval_key][0 if statistic == "lower_ci95" else 1]
    raise ValueError(f"unsupported gate statistic: {statistic}")


def percentile(values: list[float], probability: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = probability * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def validate_raw_binding(profile: dict[str, Any], raw: dict[str, Any]) -> None:
    if raw.get("profile_sha256") != digest_json(profile):
        raise ValueError("raw profile hash mismatch")
    if raw.get("frozen_sha256") != profile["freeze"]["sha256"]:
        raise ValueError("raw frozen hash mismatch")
    cases = raw.get("cases")
    if not isinstance(cases, list) or raw.get("cases_sha256") != digest_json(cases):
        raise ValueError("raw cases integrity hash mismatch")
    for case in cases:
        provenance = case.get("provenance", {})
        execution_status = case.get("execution_status")
        if execution_status not in ("completed", "failed"):
            raise ValueError("raw execution status is invalid")
        if execution_status == "completed" and provenance.get("return_status") != 0:
            raise ValueError("completed raw case has nonzero receiver status")
        if execution_status == "failed":
            failure = case.get("failure")
            if (not isinstance(failure, dict) or
                    failure.get("kind") not in ("timeout", "nonzero_receiver_status") or
                    provenance.get("return_status") == 0):
                raise ValueError("failed raw case lacks explicit failure evidence")
        if provenance.get("profile_sha256") != raw["profile_sha256"]:
            raise ValueError("case profile provenance mismatch")
        if provenance.get("scenario_sha256") != case.get("scenario_sha256"):
            raise ValueError("case scenario provenance mismatch")
        if provenance.get("iq_sha256") != case.get("iq_sha256"):
            raise ValueError("case IQ provenance mismatch")
        manifest = provenance.get("plugin_manifest")
        if not isinstance(manifest, list) or provenance.get("plugin_set_sha256") != digest_json(manifest):
            raise ValueError("plugin provenance mismatch")
        for plugin in manifest:
            if (not isinstance(plugin, dict) or set(plugin) != {"path", "sha256"} or
                    not isinstance(plugin["path"], str) or
                    not isinstance(plugin["sha256"], str) or len(plugin["sha256"]) != 64):
                raise ValueError("malformed plugin provenance entry")
        for field in ("receiver_executable_sha256", "receiver_graph_sha256",
                      "effective_receiver_config_sha256",
                      "independent_tool_sha256"):
            value = provenance.get(field)
            if not isinstance(value, str) or len(value) != 64:
                raise ValueError(f"missing provenance field: {field}")
        if provenance.get("cxx_mode") != "c++26":
            raise ValueError("receiver was not bound to a verified C++26 build")
        if (provenance.get("executor_timeout_seconds") !=
                profile["frozen"]["receiver_executor_timeout_seconds"] or
                provenance.get("process_timeout_seconds") !=
                profile["frozen"]["receiver_process_timeout_seconds"]):
            raise ValueError("receiver timeout provenance differs from frozen profile")
        if (execution_status == "completed" and
                (not isinstance(provenance.get("effective_config_sha256"), str) or
                 len(provenance["effective_config_sha256"]) != 64)):
            raise ValueError("completed case lacks effective config provenance")
        if (not isinstance(provenance.get("command"), list) or
                len(provenance["command"]) < 2 or
                not all(isinstance(value, str) for value in provenance["command"])):
            raise ValueError("receiver command provenance is missing")
        for field in ("compiler_version", "git_head", "git_status_sha256"):
            if not isinstance(provenance.get(field), str) or not provenance[field]:
                raise ValueError(f"missing build/git provenance: {field}")


def scenario_from_profile(profile: dict[str, Any], point: dict[str, Any], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    epoch_range = profile["frozen"].get("burst_epoch_range_samples", [20_000, 80_000])
    burst_epoch = int(point.get("fixed_burst_epoch_samples",
                                rng.randint(int(epoch_range[0]), int(epoch_range[1]))))
    pulses = []
    for index in range(PREAMBLE_PULSES):
        pulses.append({"frequency_index": ACTIVE[index % 4], "word": PREAMBLE_WORDS[index % 4], "role": "preamble"})
    for _ in range(int(point.get("payload_pulses", 1))):
        pulses.append({"frequency_index": rng.choice(ACTIVE), "word": rng.getrandbits(32), "role": "body"})
    messages = [{"message_id": seed, "transmitter_id": 1,
                 "transmit_start_sample": burst_epoch,
                 "initial_phase_rad": rng.uniform(-math.pi, math.pi), "pulses": pulses}]
    if "collision" in point:
        collision = point["collision"]
        same_hops = bool(collision.get("same_hops", False))
        collision_pulses = []
        for i in range(len(pulses)):
            frequency = pulses[i]["frequency_index"] if same_hops else rng.choice(ACTIVE)
            collision_pulses.append({"frequency_index": frequency,
                                     "word": (PREAMBLE_WORDS[ACTIVE.index(frequency)]
                                              if i < PREAMBLE_PULSES else rng.getrandbits(32)),
                                     "role": "preamble" if i < PREAMBLE_PULSES else "body"})
        messages.append({"message_id": seed + 1_000_000, "transmitter_id": 2,
                         "transmit_start_sample": burst_epoch + int(collision["relative_start_samples"]),
                         "initial_phase_rad": rng.uniform(-math.pi, math.pi),
                         "amplitude": 10 ** (float(collision["relative_power_db"])/20), "pulses": collision_pulses})
    return {
        "schema": "graphx.fhss.phase3-scenario.v2", "scenario_id": point["id"], "seed": seed,
        "sample_format": "cf32_le", "active_frequency_indices": list(ACTIVE), "allow_overlap": "collision" in point,
        "tail_samples": PERIOD_SAMPLES * 2,
        "messages": messages,
        "channel": {
            "timing": {"fractional_delay_samples": point.get("fractional_delay_samples", 0.0), "sample_clock_offset_ppm": point.get("sample_clock_offset_ppm", 0.0)},
            "multipath": {"taps": point.get("taps", [{"delay_samples": 0.0, "power_linear": 1.0}])},
            "fading": {"kind": point.get("fading", "none"), "k_factor_db": point.get("k_factor_db", 6.0),
                       "max_doppler_hz": point.get("max_doppler_hz", 1000.0)},
            "carrier": {"cfo_hz": point.get("cfo_hz", 0.0), "doppler_hz": point.get("doppler_hz", 0.0), "initial_phase_rad": rng.uniform(-math.pi, math.pi),
                        "phase_noise_step_std_rad": point.get("phase_noise_step_std_rad", 0.0)},
            "blockers": point.get("blockers", []), "awgn": {"eb_n0_db": point.get("eb_n0_db", 60.0)},
            "hardware": point.get("hardware", {}),
        },
    }


def receiver_environment(executable: Path, base_config: Path,
                         plugins: Path) -> dict[str, Any]:
    plugin_files = sorted(path for path in plugins.rglob("*") if path.is_file())
    plugin_manifest = [{"path": str(path.relative_to(plugins)),
                        "sha256": digest_file(path)} for path in plugin_files]
    compile_commands = next((parent / "compile_commands.json"
                             for parent in executable.parents
                             if (parent / "compile_commands.json").is_file()), None)
    compile_text = compile_commands.read_text() if compile_commands else ""
    compiler = subprocess.run(["c++", "--version"], text=True, capture_output=True,
                              timeout=10)
    git_head = subprocess.run(["git", "rev-parse", "HEAD"], text=True,
                              capture_output=True, timeout=10)
    git_status = subprocess.run(["git", "status", "--porcelain=v1"], text=True,
                                capture_output=True, timeout=10)
    return {"receiver_executable_sha256": digest_file(executable),
            "receiver_graph_sha256": digest_file(base_config),
            "plugin_manifest": plugin_manifest,
            "plugin_set_sha256": digest_json(plugin_manifest),
            "compiler_version": compiler.stdout.splitlines()[0] if compiler.returncode == 0 else "unavailable",
            "compile_commands_sha256": digest_file(compile_commands) if compile_commands else None,
            "cxx_mode": "c++26" if "-std=c++2c" in compile_text or "-std=gnu++2c" in compile_text else "unverified",
            "git_head": git_head.stdout.strip() if git_head.returncode == 0 else "unavailable",
            "git_status_sha256": digest_bytes(git_status.stdout.encode()),
            "git_dirty": bool(git_status.stdout),
            "independent_tool_sha256": digest_file(Path(__file__))}


def allocation_measurement(summary: dict[str, Any]) -> dict[str, Any]:
    expected = {"graphx.fhss.production_channelizer.diagnostics.v1",
                "graphx.fhss.acquisition_detector.diagnostics.v1"}
    values: list[int] = []
    present: set[str] = set()
    for node in summary.get("diagnostics_snapshot", []):
        diagnostics = node.get("diagnostics", {})
        schema = diagnostics.get("schema")
        if schema in expected and "allocation_high_water_bytes" in diagnostics:
            present.add(schema)
            values.append(int(diagnostics["allocation_high_water_bytes"]))
    return {"supported": present == expected,
            "reported_node_count": len(values),
            "sum_node_allocation_high_water_bytes": sum(values),
            "max_node_allocation_high_water_bytes": max(values, default=0)}


def run_receiver(executable: Path, base_config: Path, plugins: Path,
                 iq_path: Path, work: Path,
                 static_environment: dict[str, Any] | None = None,
                 executor_timeout_seconds: int = 120,
                 process_timeout_seconds: int = 150,
                 ) -> tuple[dict[str, Any] | None, dict[str, Any]]:
    config = json.loads(base_config.read_text())
    rendered = json.dumps(config)
    forbidden = ('"messages"', "FHSSSyntheticIqSourceNode", "generator_truth", "transmit_start_sample", "expected_words")
    if any(term in rendered for term in forbidden):
        raise RuntimeError("receiver topology contains forbidden transmitter/truth field")
    config["nodes"][0]["node_config"]["file_path"] = str(iq_path)
    patched = work / "receiver.json"; summary = work / "summary.json"; effective = work / "effective.json"
    patched.write_text(json.dumps(config, indent=2) + "\n")
    command = [str(executable), "--graph-config", str(patched), "--plugin-dir", str(plugins),
               "--summary-json", str(summary), "--effective-config-json", str(effective),
               "--executor-timeout-s", str(executor_timeout_seconds)]
    timed_out = False
    try:
        result = subprocess.run(command, text=True, capture_output=True,
                                timeout=process_timeout_seconds)
        return_status: int | None = result.returncode
        stdout = result.stdout
        stderr = result.stderr
    except subprocess.TimeoutExpired as error:
        timed_out = True
        return_status = None
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
    summary_json = json.loads(summary.read_text()) if summary.is_file() else None
    provenance = dict(static_environment or receiver_environment(executable, base_config, plugins))
    provenance.update({"command": command,
                       "return_status": return_status,
                       "timed_out": timed_out,
                       "executor_timeout_seconds": executor_timeout_seconds,
                       "process_timeout_seconds": process_timeout_seconds,
                       "stdout_sha256": digest_bytes(stdout.encode()),
                       "stderr_sha256": digest_bytes(stderr.encode()),
                       "stdout_tail": stdout[-4000:],
                       "stderr_tail": stderr[-4000:],
                       "effective_config_sha256": digest_file(effective) if effective.is_file() else None,
                       "effective_receiver_config_sha256": digest_json(config),
                       "iq_sha256": digest_file(iq_path)})
    return summary_json, provenance


def evaluate(profile: dict[str, Any], executable: Path, base_config: Path,
             plugins: Path, partition: str, limit: int | None = None,
             seed_limit: int | None = None,
             raw_path: Path | None = None) -> dict[str, Any]:
    validate_profile(profile)
    stats = profile["statistics"]
    partitions = {name: set(values) for name, values in stats["seed_partitions"].items()}
    if any(partitions[a] & partitions[b] for a in partitions for b in partitions if a < b):
        raise ValueError("seed partitions overlap")
    if partition not in ("validation", "evaluation"):
        raise ValueError("held-out evaluation refuses development partition")
    if (partition == "evaluation" and profile["freeze"]["status"] !=
            "frozen_before_held_out_evaluation"):
        raise ValueError("held-out evaluation requires a frozen v6 profile")
    if digest_json(profile["matrix"]) != profile["frozen"]["matrix_sha256"]:
        raise ValueError("frozen matrix hash mismatch")
    if digest_json(stats["seed_partitions"]) != profile["frozen"]["seed_partitions_sha256"]:
        raise ValueError("frozen seed-partition hash mismatch")
    if digest_json(profile["frozen"]) != profile["freeze"]["sha256"]:
        raise ValueError("frozen profile hash mismatch")
    seeds = list(stats["seed_partitions"][partition]); points = list(profile["matrix"])
    if seed_limit is not None:
        if seed_limit <= 0:
            raise ValueError("seed_limit must be positive")
        seeds = seeds[:seed_limit]
    extension_seeds = list(stats.get("seed_partitions", {}).get("gate_extension", []))
    extension_points = set(stats.get("gate_extension_point_ids", []))
    if limit: points = points[:limit]
    raw = []
    static_environment = receiver_environment(executable, base_config, plugins)
    profile_sha256 = digest_json(profile)
    def raw_document() -> dict[str, Any]:
        return {"schema": "graphx.fhss.phase3-raw-evaluation.v4",
                "partition": partition, "profile_sha256": profile_sha256,
                "frozen_sha256": profile["freeze"]["sha256"],
                "cases_sha256": digest_json(raw), "cases": raw}

    def retain_progress() -> None:
        if raw_path is not None:
            write_json_atomic(raw_path, raw_document())

    retain_progress()
    with tempfile.TemporaryDirectory(prefix="graphx_fhss_phase3_") as directory:
        root = Path(directory)
        for point in points:
            point_seeds = seeds + (extension_seeds if partition == "evaluation" and
                                   point["id"] in extension_points else [])
            for seed in point_seeds:
                print(f"phase3 actual graph: {point['id']} seed={seed}",
                      file=sys.stderr, flush=True)
                scenario = scenario_from_profile(profile, point, seed)
                case = root / f"{point['id']}_{seed}"; case.mkdir()
                iq, truth_path = case/"input.cf32", case/"truth.json"
                truth = write_capture(scenario, iq, truth_path)
                started = time.perf_counter()
                summary, provenance = run_receiver(
                    executable, base_config, plugins, iq, case,
                    static_environment,
                    profile["frozen"]["receiver_executor_timeout_seconds"],
                    profile["frozen"]["receiver_process_timeout_seconds"])
                elapsed = time.perf_counter() - started
                provenance.update({"profile_sha256": profile_sha256,
                                   "scenario_sha256": truth["scenario_sha256"],
                                   "independent_tool_sha256": static_environment["independent_tool_sha256"]})
                common = {"point_id": point["id"], "seed": seed,
                          "scenario_sha256": truth["scenario_sha256"],
                          "iq_sha256": truth["iq_sha256"],
                          "graph_elapsed_seconds": elapsed,
                          "provenance": provenance}
                if provenance["return_status"] != 0 or summary is None:
                    raw.append({**common, "execution_status": "failed",
                                "failure": {
                                    "kind": "timeout" if provenance["timed_out"] else "nonzero_receiver_status",
                                    "return_status": provenance["return_status"],
                                    "summary_available": summary is not None,
                                    "receiver_summary": summary,
                                    "scenario": scenario,
                                    "truth_manifest": truth}})
                    retain_progress()
                    continue
                detections = summary["fhss_diagnostics"].get("decoded_pulses", [])
                matching = match_events(truth["events"], detections, profile["frozen"]["matching_tolerance_samples"], True)
                searched_per_channel = max(0, (truth["sample_count"] - 241) // 10 + 1)
                allocation = allocation_measurement(summary)
                raw.append({**common, "execution_status": "completed",
                            "searched_input_samples": truth["sample_count"],
                            "searched_channel_samples": searched_per_channel * 64,
                            "searched_seconds_sum_across_channels": searched_per_channel * 64 / 50_000_000.0,
                            "eb_n0_db": truth["channel"]["eb_n0_db"],
                            "es_n0_db": truth["channel"]["es_n0_db"],
                            "sample_snr_db": truth["channel"]["sample_snr_db"],
                            "desired_active_power_w": truth["channel"]["desired_active_power_w"],
                            "burst_epoch_samples": min(event["nominal_transmit_start_sample"]
                                                       for event in truth["events"]),
                            "allocation": allocation,
                            "preamble_lock": bool(summary["fhss_diagnostics"].get("preamble_lock", False)), **matching})
                retain_progress()
    return raw_document()


def aggregate(profile: dict[str, Any], raw: dict[str, Any]) -> dict[str, Any]:
    validate_profile(profile)
    validate_raw_binding(profile, raw)
    rows = []
    for point in profile["matrix"]:
        attempted_cases = [c for c in raw["cases"] if c["point_id"] == point["id"]]
        cases = [c for c in attempted_cases
                 if c.get("execution_status") == "completed"]
        if not cases: continue
        truth = sum(c["truth_events"] for c in cases); matched = sum(c["matched_events"] for c in cases)
        false = sum(c["false_detections"] for c in cases); searched = sum(c["searched_channel_samples"] for c in cases)
        bits = sum(c["bits"] for c in cases); bit_errors = sum(c["bit_errors"] for c in cases)
        words = sum(c["words"] for c in cases); word_errors = sum(c["word_errors"] for c in cases)
        msg_errors = sum(c["message_errors"] for c in cases)
        messages = sum(c["messages"] for c in cases)
        timing = [m["timing_error_samples"] for c in cases for m in c["matches"]]
        cfo_errors = [m["cfo_error_hz"] for c in cases for m in c["matches"]]
        confidences = [m["confidence"] for c in cases for m in c["matches"]]
        confusion: dict[str, int] = {}
        transmitter_capture: dict[str, dict[str, int | float]] = {}
        for case in cases:
            for key, value in case["frequency_confusion"].items():
                confusion[key] = confusion.get(key, 0) + value
            for result in case["message_results"]:
                key = str(result["transmitter_id"])
                stats = transmitter_capture.setdefault(
                    key, {"truth_events": 0, "matched_events": 0,
                          "messages": 0, "successful_messages": 0})
                stats["truth_events"] += result["truth_events"]
                stats["matched_events"] += result["matched_events"]
                stats["messages"] += 1
                stats["successful_messages"] += int(result["success"])
        for stats in transmitter_capture.values():
            stats["pulse_capture_probability"] = stats["matched_events"] / stats["truth_events"]
            stats["message_capture_probability"] = stats["successful_messages"] / stats["messages"]
        allocation_supported = all(c["allocation"]["supported"] for c in cases)
        rows.append({"point_id": point["id"], "classification": "measured_actual_graph",
                     "family": point["family"],
                     "eb_n0_db": point.get("eb_n0_db"),
                     "sample_snr_db": cases[0]["sample_snr_db"],
                     "sir_db": point.get("sir_db"),
                     "relative_timing_samples": point.get("relative_timing_samples"),
                     "trials": len(cases), "attempted_trials": len(attempted_cases),
                     "execution_failures": len(attempted_cases) - len(cases),
                     "truth_events": truth, "matched_events": matched,
                     "pd": matched/truth, "pd_ci95": wilson(matched, truth),
                     "false_detections": false, "searched_samples": searched,
                     "false_alarms_per_sample": false/searched, "false_alarm_ci95": wilson(false, searched),
                     "bit_errors": bit_errors, "decoded_bits": bits,
                     "conditional_ber": bit_errors/bits if bits else None,
                     "conditional_ber_ci95": wilson(bit_errors, bits) if bits else None,
                     "word_errors": word_errors, "decoded_words": words,
                     "conditional_wer": word_errors/words if words else None,
                     "conditional_wer_ci95": wilson(word_errors, words) if words else None,
                     "association_errors": sum(c["association_errors"] for c in cases),
                     "ambiguous_associations": sum(c["ambiguous_associations"] for c in cases),
                     "message_errors": msg_errors, "messages": messages,
                     "per": msg_errors/messages, "per_ci95": wilson(msg_errors, messages),
                     "timing_rmse_samples": math.sqrt(sum(x*x for x in timing)/len(timing)) if timing else None,
                     "timing_absolute_error_p50_samples": percentile(timing, 0.50),
                     "timing_absolute_error_p95_samples": percentile(timing, 0.95),
                     "timing_absolute_error_p99_samples": percentile(timing, 0.99),
                     "timing_absolute_error_max_samples": max(timing, default=None),
                     "cfo_rmse_hz": math.sqrt(sum(x*x for x in cfo_errors)/len(cfo_errors)) if cfo_errors else None,
                     "mean_matched_confidence": sum(confidences)/len(confidences) if confidences else None,
                     "confidence_calibration_absolute_error": abs(sum(confidences)/len(confidences) - matched/truth) if confidences else None,
                     "frequency_index_confusion": confusion,
                     "collision_truth_events": sum(c["collision_truth_events"] for c in cases),
                     "collision_matched_events": sum(c["collision_matched_events"] for c in cases),
                     "temporal_overlap_truth_events": sum(c["temporal_overlap_truth_events"] for c in cases),
                     "same_hop_overlap_truth_events": sum(c["same_hop_overlap_truth_events"] for c in cases),
                     "transmitter_capture": transmitter_capture,
                     "allocation_supported": allocation_supported,
                     "allocation_sum_high_water_bytes": max(
                         c["allocation"]["sum_node_allocation_high_water_bytes"] for c in cases),
                     "allocation_max_node_high_water_bytes": max(
                         c["allocation"]["max_node_allocation_high_water_bytes"] for c in cases),
                     "burst_epoch_min_samples": min(c["burst_epoch_samples"] for c in cases),
                     "burst_epoch_max_samples": max(c["burst_epoch_samples"] for c in cases),
                     "runtime_seconds": sum(c["graph_elapsed_seconds"] for c in cases)})
    acceptance = {}
    by_id = {row["point_id"]: row for row in rows}
    for gate in profile["frozen"]["gates"]:
        row = by_id.get(gate["point_id"])
        if row is None:
            continue
        point_estimate, measured = gate_statistic_value(row, gate)
        passed = measured is not None and (measured >= gate["limit"] if gate["comparison"] == ">=" else measured <= gate["limit"])
        acceptance[gate["id"]] = {"expected": gate, "point_estimate": point_estimate,
                                  "gate_statistic_value": measured, "pass": passed,
                                  "source": "raw held-out actual-graph cases"}
    executed_ids = {case["point_id"] for case in raw["cases"]}
    expected_ids = {point["id"] for point in profile["matrix"]}
    matrix_complete = executed_ids == expected_ids
    allocation_pass = bool(rows) and all(row["allocation_supported"] for row in rows)
    failed_cases = [case for case in raw["cases"]
                    if case.get("execution_status") != "completed"]
    execution_pass = not failed_cases
    machine_acceptance = {"actual_allocation_high_water_available": {
        "pass": allocation_pass,
        "policy": "machine FAIL when either production channelizer or acquisition detector omits allocation diagnostics"},
        "all_receiver_executions_completed": {
            "pass": execution_pass,
            "failed_case_count": len(failed_cases),
            "failed_cases": [{"point_id": case.get("point_id"),
                              "seed": case.get("seed"),
                              "failure": case.get("failure")}
                             for case in failed_cases],
            "policy": "every nonzero status or timeout is retained and is machine FAIL"}}
    return {"schema": "graphx.fhss.phase3-report.v4", "claim_level": profile["claim_level"],
            "profile_sha256": digest_json(profile), "frozen_sha256": profile["freeze"]["sha256"],
            "raw_cases_sha256": raw["cases_sha256"],
            "independent_tool_sha256": digest_bytes(Path(__file__).read_bytes()),
            "partition": raw["partition"], "rows": rows, "acceptance": acceptance,
            "machine_acceptance": machine_acceptance,
            "matrix_complete": matrix_complete,
            "overall_pass": matrix_complete and allocation_pass and execution_pass and
                            len(acceptance) == len(profile["frozen"]["gates"]) and
                            all(x["pass"] for x in acceptance.values()),
            "limitations": profile["limitations"]}


def verify(profile: dict[str, Any], raw: dict[str, Any], report: dict[str, Any]) -> None:
    if raw["partition"] != "evaluation" or report["partition"] != "evaluation":
        raise ValueError("checked report must use held-out evaluation")
    regenerated = aggregate(profile, raw)
    if canonical_bytes(regenerated) != canonical_bytes(report):
        raise ValueError("report does not reproduce byte-for-byte from raw results")
    if report["profile_sha256"] != digest_json(profile):
        raise ValueError("report profile hash mismatch")


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate"); gen.add_argument("--scenario", type=Path, required=True); gen.add_argument("--iq", type=Path, required=True); gen.add_argument("--truth", type=Path, required=True)
    compare = sub.add_parser("compare"); compare.add_argument("--independent", type=Path, required=True); compare.add_argument("--canonical", type=Path, required=True); compare.add_argument("--sample-format", choices=("cf32_le", "cf64_le"), required=True); compare.add_argument("--report", type=Path, required=True)
    ev = sub.add_parser("evaluate"); ev.add_argument("--profile", type=Path, required=True); ev.add_argument("--receiver", type=Path, required=True); ev.add_argument("--graph", type=Path, required=True); ev.add_argument("--plugins", type=Path, required=True); ev.add_argument("--partition", choices=("validation", "evaluation"), required=True); ev.add_argument("--limit", type=int); ev.add_argument("--seed-limit", type=int); ev.add_argument("--smoke", action="store_true"); ev.add_argument("--raw", type=Path, required=True); ev.add_argument("--report", type=Path, required=True)
    ver = sub.add_parser("verify"); ver.add_argument("--profile", type=Path, required=True); ver.add_argument("--raw", type=Path, required=True); ver.add_argument("--report", type=Path, required=True)
    agg = sub.add_parser("aggregate"); agg.add_argument("--profile", type=Path, required=True); agg.add_argument("--raw", type=Path, required=True); agg.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "generate":
        manifest = write_capture(json.loads(args.scenario.read_text()), args.iq, args.truth); print(json.dumps(manifest, sort_keys=True)); return 0
    if args.command == "compare":
        metrics = parity_metrics(args.independent.read_bytes(), args.canonical.read_bytes(), args.sample_format)
        args.report.write_text(json.dumps(metrics, indent=2, sort_keys=True)+"\n")
        return 0
    profile = json.loads(args.profile.read_text())
    if args.command == "evaluate":
        raw = evaluate(profile, args.receiver, args.graph, args.plugins, args.partition,
                       args.limit, args.seed_limit, args.raw)
        report = aggregate(profile, raw)
        write_json_atomic(args.raw, raw)
        write_json_atomic(args.report, report)
        if args.smoke:
            if args.partition != "validation":
                raise ValueError("smoke mode is validation-only")
            return 0 if (raw["cases"] and
                         all(case.get("execution_status") == "completed" and
                             case["allocation"]["supported"]
                             for case in raw["cases"])) else 2
        return 0 if report["overall_pass"] else 2
    raw = json.loads(args.raw.read_text())
    if args.command == "aggregate":
        args.report.write_text(json.dumps(aggregate(profile, raw), indent=2, sort_keys=True)+"\n"); return 0
    report = json.loads(args.report.read_text()); verify(profile, raw, report); print("Phase 3 report verification passed"); return 0


if __name__ == "__main__":
    raise SystemExit(main())
