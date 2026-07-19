#!/usr/bin/env python3
"""External Phase 1-4 operator for the loopback-only GraphX FHSS dashboard."""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import copy
import hashlib
import http.client
import json
import os
import platform
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1] / "api"
sys.path.insert(0, str(API_DIR))
from schema_subset import load_registry, validate_instance, validate_schema  # noqa: E402
try:
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
except ImportError as error:
    raise SystemExit("install ../api/requirements-contracts.lock for authoritative live validation") from error

PHASE = 5
OWNED_MARKER = ".graphx-fhss-dashboard-operator"
MIN_SCREENSHOT_WIDTH = 640
MIN_SCREENSHOT_HEIGHT = 360
GENERATOR_TIMEOUT_SECONDS = 60
RECEIVER_AUDIT_JQ = r'''[path(..) as $p | $p[]? |
  select(type == "string") | ascii_downcase |
  select(. == "messages" or contains("truth") or contains("schedule") or
         . == "active_frequency_indices" or
         (contains("generator") and contains("metadata")) or
         (contains("expected") and (contains("word") or contains("value"))))] |
  length == 0'''


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def is_valid_png(path: Path) -> bool:
    """Recognize a complete, non-placeholder dashboard PNG.

    A 640x360 minimum is deliberately below a typical browser viewport while
    rejecting 1x1 and thumbnail artifacts that cannot evidence dashboard state.
    """
    try:
        data = path.read_bytes()
    except OSError:
        return False
    if (len(data) < 45 or len(data) > 32 * 1024 * 1024 or
            not data.startswith(b"\x89PNG\r\n\x1a\n")):
        return False
    position = 8
    saw_ihdr = False
    width = height = bit_depth = color_type = 0
    idat = bytearray()
    while position + 12 <= len(data):
        length = int.from_bytes(data[position:position + 4], "big")
        chunk_type = data[position + 4:position + 8]
        end = position + 12 + length
        if end > len(data):
            return False
        payload = data[position + 8:position + 8 + length]
        expected_crc = int.from_bytes(data[position + 8 + length:end], "big")
        if zlib.crc32(chunk_type + payload) & 0xFFFFFFFF != expected_crc:
            return False
        if not saw_ihdr:
            if chunk_type != b"IHDR" or length != 13:
                return False
            width = int.from_bytes(data[position + 8:position + 12], "big")
            height = int.from_bytes(data[position + 12:position + 16], "big")
            if (width < MIN_SCREENSHOT_WIDTH or
                    height < MIN_SCREENSHOT_HEIGHT or
                    width > 8192 or height > 8192 or
                    width * height > 16_777_216):
                return False
            bit_depth = payload[8]
            color_type = payload[9]
            if (bit_depth != 8 or color_type not in (0, 2, 4, 6) or
                    payload[10:] != b"\0\0\0"):
                return False
            saw_ihdr = True
        elif chunk_type == b"IHDR":
            return False
        if chunk_type == b"IDAT":
            idat.extend(payload)
        if chunk_type == b"IEND":
            if not saw_ihdr or length != 0 or end != len(data) or not idat:
                return False
            break
        position = end
    else:
        return False

    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    stride = width * channels
    expected_size = height * (stride + 1)
    try:
        decompressor = zlib.decompressobj()
        raw = decompressor.decompress(bytes(idat), expected_size + 1)
        raw += decompressor.flush()
    except zlib.error:
        return False
    if (len(raw) != expected_size or not decompressor.eof or
            decompressor.unused_data or decompressor.unconsumed_tail):
        return False

    previous = bytearray(stride)
    unique_colors: set[tuple[int, int, int]] = set()
    minimum_luminance = 255
    maximum_luminance = 0
    transitions = 0
    previous_color: tuple[int, int, int] | None = None

    def paeth(left: int, above: int, upper_left: int) -> int:
        prediction = left + above - upper_left
        left_distance = abs(prediction - left)
        above_distance = abs(prediction - above)
        diagonal_distance = abs(prediction - upper_left)
        if left_distance <= above_distance and left_distance <= diagonal_distance:
            return left
        return above if above_distance <= diagonal_distance else upper_left

    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        if filter_type > 4:
            return False
        filtered = raw[offset:offset + stride]
        offset += stride
        row = bytearray(stride)
        for index, value in enumerate(filtered):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            predictor = (0 if filter_type == 0 else left if filter_type == 1
                         else above if filter_type == 2
                         else (left + above) // 2 if filter_type == 3
                         else paeth(left, above, upper_left))
            row[index] = (value + predictor) & 0xFF
        for pixel in range(width):
            start = pixel * channels
            if color_type in (0, 4):
                red = green = blue = row[start]
            else:
                red, green, blue = row[start:start + 3]
            color = (red, green, blue)
            if len(unique_colors) < 256:
                unique_colors.add(color)
            luminance = (54 * red + 183 * green + 19 * blue) // 256
            minimum_luminance = min(minimum_luminance, luminance)
            maximum_luminance = max(maximum_luminance, luminance)
            if previous_color is not None and color != previous_color:
                transitions += 1
            previous_color = color
        previous = row
    return (len(unique_colors) >= 16 and
            maximum_luminance - minimum_luminance >= 24 and
            transitions >= 100)


def write_zero_cf32(destination: Path, byte_count: int) -> None:
    """Write a bounded synthetic no-message recording of the requested size."""
    if byte_count < 8 or byte_count % 8:
        raise RuntimeError("zero recording size must be whole cf32 samples")
    destination.write_bytes(bytes(byte_count))


def locate_independent_harness() -> Path:
    candidates = [
        Path(__file__).resolve().with_name("fhss_phase3_independent.py"),
        Path(__file__).resolve().parents[2] / "tools" / "fhss_phase3_independent.py",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("installed/source independent FHSS Phase 3 harness not found")


def locate_executable(build_dir: Path) -> Path:
    candidates = [build_dir / "examples/DSP/graphx-dsp-fhss-demo",
                  build_dir / "bin/graphx-dsp-fhss-demo"]
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise RuntimeError(f"graphx-dsp-fhss-demo not found under {build_dir}")

def locate_generator(build_dir: Path) -> Path:
    for candidate in (build_dir / "examples/DSP/graphx-dsp-fhss-iq-generator",
                      build_dir / "bin/graphx-dsp-fhss-iq-generator"):
        if candidate.is_file() and os.access(candidate, os.X_OK): return candidate
    raise RuntimeError(f"graphx-dsp-fhss-iq-generator not found under {build_dir}")


def request(port: int, method: str, target: str, body: bytes | None = None,
            headers: dict[str, str] | None = None, timeout: float = 5) -> tuple[int, dict[str, str], bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    connection.request(method, target, body=body, headers=headers or {})
    response = connection.getresponse()
    payload = response.read()
    result = response.status, {key.lower(): value for key, value in response.getheaders()}, payload
    connection.close()
    return result


def raw_request(port: int, wire: bytes) -> tuple[int, dict[str, str], bytes]:
    """Exchange one close-delimited request, tolerating an early defensive close."""
    chunks: list[bytes] = []
    with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
        client.settimeout(5)
        try:
            client.sendall(wire)
        except (BrokenPipeError, ConnectionResetError):
            pass
        while True:
            try:
                chunk = client.recv(65536)
            except ConnectionResetError:
                break
            if not chunk:
                break
            chunks.append(chunk)
    response = b"".join(chunks)
    head, separator, body = response.partition(b"\r\n\r\n")
    if not separator:
        return 0, {}, b""
    lines = head.decode("iso-8859-1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip()
               for line in lines[1:] if ":" in line}
    return status, headers, body


def receiver_input_from_effective(effective: dict[str, object]) -> dict[str, object]:
    source = next(
        (node for node in effective.get("nodes", [])
         if isinstance(node, dict) and
         node.get("type") == "FHSSBinaryIqFileSourceNode"), None)
    if not isinstance(source, dict) or not isinstance(source.get("node_config"), dict):
        raise RuntimeError("effective receiver graph has no binary-IQ source")
    node_config = source["node_config"]
    return {key: node_config[key] for key in (
        "file_path", "sample_format", "first_complex_sample",
        "max_complex_samples", "max_read_complex_samples") if key in node_config}


def receiver_input_from_case_document(document: dict[str, object]) -> dict[str, object]:
    direct = document.get("receiver_input")
    if isinstance(direct, dict):
        return direct
    fhss = document.get("fhss")
    if isinstance(fhss, dict):
        scenario = fhss.get("scenario")
        if isinstance(scenario, dict) and isinstance(
                scenario.get("receiver_input"), dict):
            return scenario["receiver_input"]
    raise RuntimeError("Phase 4 case has no receiver_input")


def launch(args: argparse.Namespace) -> tuple[subprocess.Popen[str], str, int, list[str]]:
    executable = locate_executable(args.build_dir.resolve())
    command = [str(executable), "--dashboard" if args.phase >= 3 else "--dashboard-no-run",
               "--dashboard-port", str(getattr(args, "port", 0))]
    if args.phase >= 5:
        command.extend(["--dashboard-artifact-root",
                        str(args.output_dir.resolve() / "phase5-job-artifacts")])
    case = getattr(args, "case", None)
    if case:
        output = args.output_dir.resolve()
        report_path = output / f"phase{args.phase}-report.json"
        if not (output / OWNED_MARKER).is_file() or not report_path.is_file():
            raise RuntimeError("serve --case requires a completed owned output directory")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if (report.get("phase") != args.phase or report.get("result") != "PARTIAL" or
                report.get("evidence_status") != "partial_pre_browser"):
            raise RuntimeError("serve --case requires a partial pre-browser report")
        if args.phase == 4:
            case_config = output / "phase4-cases" / f"{case}-config.json"
            if not case_config.is_file():
                raise RuntimeError(f"Phase 4 case config is missing: {case_config}")
            case_document = json.loads(case_config.read_text(encoding="utf-8"))
            receiver_input = receiver_input_from_case_document(case_document)
            iq_path = Path(str(receiver_input.get("file_path", "")))
            if not iq_path.is_file():
                raise RuntimeError(f"Phase 4 case IQ is missing: {iq_path}")
            for forbidden in output.glob("*-truth.json"):
                raise RuntimeError(f"live truth artifact must be absent before serve: {forbidden}")
            for forbidden in output.glob("*schedule.json"):
                raise RuntimeError(f"live schedule artifact must be absent before serve: {forbidden}")
            command.extend(["--graph-config", str(case_config)])
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1)
    deadline = time.monotonic() + 15
    selector = selectors.DefaultSelector()
    if process.stdout:
        selector.register(process.stdout, selectors.EVENT_READ)
    captured: list[str] = []
    while time.monotonic() < deadline:
        ready = selector.select(timeout=min(0.25, deadline - time.monotonic()))
        if not ready:
            if process.poll() is not None:
                break
            continue
        line = process.stdout.readline() if process.stdout else ""
        captured.append(line.rstrip())
        if line.startswith("Dashboard URL:"):
            url = line.split(": ", 1)[1].strip()
            return process, url, int(url.rsplit(":", 1)[1]), command
        if process.poll() is not None:
            break
    process.terminate()
    raise RuntimeError("dashboard did not publish its bound URL; output=" +
                       " | ".join(captured[-8:]))


def start_served_case(port: int, case: str, output: Path, url: str) -> None:
    status_code, _, status_body = request(port, "GET", "/api/v1/fhss/status")
    if status_code != 200:
        raise RuntimeError(f"served case status failed: HTTP {status_code}")
    config_code, _, config_body = request(port, "GET", "/api/v1/fhss/config/authoritative")
    if config_code != 200:
        raise RuntimeError(f"served case config failed: HTTP {config_code}")
    config_document = json.loads(config_body)
    revision = int(config_document["config_revision"])
    config_etag = str(config_document["etag"])
    effective_code, _, effective_body = request(
        port, "GET", "/api/v1/fhss/graph/receiver-minimal")
    if effective_code != 200:
        raise RuntimeError(
            f"served case effective receiver graph failed: HTTP {effective_code}")
    receiver_document = json.loads(effective_body)
    effective = receiver_document.get("graph", {})
    if (int(receiver_document.get("config_revision", -1)) != revision or
            receiver_document.get("etag") != config_etag):
        raise RuntimeError(
            "served receiver graph does not match authoritative config identity")

    def contains_truth_side_channel(value: object) -> bool:
        if isinstance(value, dict):
            for key, child in value.items():
                lowered = key.lower()
                if (lowered in ("messages", "active_frequency_indices") or
                        "truth" in lowered or
                        ("generator" in lowered and "metadata" in lowered)):
                    return True
                if contains_truth_side_channel(child):
                    return True
        elif isinstance(value, list):
            return any(contains_truth_side_channel(child) for child in value)
        return False

    if contains_truth_side_channel(effective):
        raise RuntimeError(
            "served effective receiver graph contains a truth side channel")
    rebuild_code, _, rebuild_body = request(
        port, "POST", "/api/v1/fhss/config/rebuild",
        json.dumps({"expected_revision": revision,
                    "command_id": f"phase4-serve-{case}-rebuild"}).encode(),
        {"Content-Type": "application/json"}, timeout=20)
    if rebuild_code != 200:
        raise RuntimeError(f"served case rebuild failed: {rebuild_body.decode(errors='replace')}")
    rebuild_document = json.loads(rebuild_body)
    if (int(rebuild_document.get("submitted_revision", -1)) != revision or
            rebuild_document.get("etag") != config_etag or
            int(rebuild_document.get("active_generation", 0)) < 1):
        raise RuntimeError(
            "served receiver generation does not match receiver graph identity")
    start_code, _, start_body = request(
        port, "POST", "/api/v1/fhss/commands/start",
        json.dumps({"command_id": f"phase4-serve-{case}-start"}).encode(),
        {"Content-Type": "application/json"})
    if start_code != 202:
        raise RuntimeError(f"served case start failed: {start_body.decode(errors='replace')}")
    terminal = {}
    # Debug/C++26 installed-tree replay is intentionally bounded but can take
    # slightly over 30 seconds on a loaded host. Keep a hard operator bound
    # while avoiding a timing-only failure at the normal completion edge.
    deadline = time.monotonic() + 45
    poll_stop = threading.Event()
    poll_lock = threading.Lock()
    poll_records: list[dict[str, object]] = []
    poll_targets = (
        "/api/v1/fhss/observations",
        "/api/v1/fhss/observation-provenance",
        "/api/v1/fhss/observation-history",
        "/api/v1/fhss/spectrum?fft_size=128",
        "/api/v1/fhss/comparison",
        "/api/v1/fhss/expected-truth",
        "/api/v1/fhss/metrics",
        "/api/v1/fhss/diagnostics",
    )

    def poll_while_running(target: str) -> None:
        while not poll_stop.is_set():
            poll_code, _, poll_body = request(port, "GET", target, timeout=5)
            poll_document = json.loads(poll_body)
            with poll_lock:
                poll_records.append({
                    "target": target, "status": poll_code,
                    "bytes": len(poll_body),
                    "generation": poll_document.get(
                        "generation", poll_document.get("active_generation")),
                    "run_epoch": poll_document.get(
                        "run_epoch", poll_document.get("active_run_epoch")),
                    "progress": sum(int(value) for value in
                                    poll_document.get("graph", {}).values()
                                    if isinstance(value, int)),
                })
            poll_stop.wait(0.01)

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=len(poll_targets)) as poll_pool:
        poll_futures = [poll_pool.submit(poll_while_running, target)
                        for target in poll_targets]
        while time.monotonic() < deadline:
            _, _, body = request(port, "GET", "/api/v1/fhss/status")
            terminal = json.loads(body)
            if terminal.get("lifecycle_state") in ("completed", "failed"):
                break
            poll_stop.wait(0.02)
        poll_stop.set()
        for future in poll_futures:
            future.result(timeout=6)
    observation_code, _, observation_body = request(
        port, "GET", "/api/v1/fhss/observations")
    expected_code, _, expected_body = request(
        port, "GET", "/api/v1/fhss/expected-truth")
    comparison_code, _, comparison_body = request(
        port, "GET", "/api/v1/fhss/comparison")
    visualization_code, _, visualization_body = request(
        port, "GET",
        "/api/v1/fhss/visualization?message_limit=16&pulse_limit=128&refresh_ms=250")
    observation = json.loads(observation_body)
    receiver_pulses = observation.get("observed_pulses", [])
    spectrum_channel = (int(receiver_pulses[0]["physical_channel_index"])
                        if receiver_pulses else None)
    spectrum_target = (f"/api/v1/fhss/spectrum?channel={spectrum_channel}&fft_size=128"
                       if spectrum_channel is not None else
                       "/api/v1/fhss/spectrum?fft_size=128")
    spectrum_code, _, spectrum_body = request(
        port, "GET", spectrum_target)
    expected = json.loads(expected_body)
    comparison = json.loads(comparison_body)
    visualization = json.loads(visualization_body)
    spectrum = json.loads(spectrum_body)
    _, _, stable_observation_body = request(
        port, "GET", "/api/v1/fhss/observations")
    _, _, stable_history_body = request(
        port, "GET", "/api/v1/fhss/observation-history")
    stable_history = json.loads(stable_history_body)
    active_generation = int(terminal.get("active_generation", 0))
    active_run_epoch = int(terminal.get("active_run_epoch", 0))
    polling_summary = {
        "request_count": len(poll_records),
        "targets": sorted({str(record["target"]) for record in poll_records}),
        "max_response_bytes": max((int(record["bytes"])
                                    for record in poll_records), default=0),
        "all_status_200": all(int(record["status"]) == 200
                              for record in poll_records),
        "terminal_generation": active_generation,
        "terminal_run_epoch": active_run_epoch,
        "stable_observation_after_terminal": stable_observation_body == observation_body,
        "history_entry_count": len(stable_history.get("entries", [])),
        "max_graph_progress": max((int(record["progress"])
                                   for record in poll_records), default=0),
        "identity_coherent": all(
            (record["generation"] is None or
             int(record["generation"]) == active_generation) and
            (record["run_epoch"] is None or
             int(record["run_epoch"]) == active_run_epoch)
            for record in poll_records),
    }
    if (polling_summary["request_count"] < len(poll_targets) or
            not polling_summary["all_status_200"] or
            polling_summary["max_response_bytes"] > 1_048_576 or
            not polling_summary["stable_observation_after_terminal"] or
            polling_summary["history_entry_count"] > 1 or
            polling_summary["max_graph_progress"] <= 0 or
            not polling_summary["identity_coherent"]):
        raise RuntimeError(
            f"bounded concurrent observation polling failed: {polling_summary}")
    if (observation_code != 200 or expected_code != 200 or comparison_code != 200 or
            visualization_code != 200 or spectrum_code != 200 or
            observation.get("semantic_class") != "observed" or
            expected.get("semantic_class") != "expected" or
            comparison.get("semantic_class") != "comparison"):
        raise RuntimeError("served case observation is unavailable")
    terminal_generation = int(terminal.get("active_generation", 0))
    if int(observation.get("generation", -1)) != terminal_generation:
        raise RuntimeError("served case observation generation mismatch")
    case_config = output / "phase4-cases" / f"{case}-config.json"
    config_document = json.loads(case_config.read_text(encoding="utf-8"))
    iq_path = Path(receiver_input_from_case_document(
        config_document)["file_path"])
    expected_count = len(expected.get("pulses", []))
    observed_count = len(observation.get("observed_pulses", []))
    detected_count = int(observation.get("detected_count", 0))
    comparison_summary = {
        "availability": comparison.get("availability"),
        "matched_count": len(comparison.get("matches", [])),
        "missed_count": len(comparison.get("missed_expected_indices", [])),
        "unexpected_count": len(comparison.get("unexpected_observed_indices", [])),
        "ambiguous_count": len(comparison.get("ambiguous", [])),
        "timing_delta_samples": [item.get("timing_delta_samples")
                                 for item in comparison.get("matches", [])],
        "decoded_value_agrees": [item.get("decoded_value_agrees")
                                  for item in comparison.get("matches", [])],
    }
    spectrum_summary = {
        "availability": spectrum.get("availability"),
        "bins_sha256": hashlib.sha256(json.dumps(
            spectrum.get("bins", []), sort_keys=True,
            separators=(",", ":")).encode()).hexdigest(),
        "bin_count": len(spectrum.get("bins", [])),
        "channel_index": spectrum.get("channel_index"),
    }
    expected_messages = expected.get("messages", [])
    schedule_messages = visualization.get("schedule", {}).get("messages", [])
    timeline_pulses = visualization.get("timeline", {}).get("pulses", [])
    dom_contract_ok = (
        bool(expected_messages) and len(schedule_messages) > 0 and
        all(all(message.get(field) is not None for field in (
            "message_id", "transmit_start_sample", "pulse_count",
            "preamble_pulse_count", "body_pulse_count"))
            for message in schedule_messages) and
        all(message.get("transmit_start_sample") ==
            expected_messages[index].get("transmit_start_sample")
            for index, message in enumerate(schedule_messages)) and
        all(all(pulse.get(field) is not None for field in (
            "absolute_pulse_index", "message_id", "pulse_index",
            "frequency_index", "expected_sample_start", "source"))
            for pulse in timeline_pulses) and
        all(pulse.get("expected_sample_start") ==
            expected["pulses"][index].get("global_start_sample")
            for index, pulse in enumerate(timeline_pulses)))
    if not dom_contract_ok:
        raise RuntimeError(
            "served live visualization cannot render truthful schedule DOM")
    if terminal.get("lifecycle_state") != "completed" or expected_count == 0:
        raise RuntimeError(
            f"served {case} case lacks a completed run and expected pulses: "
            f"lifecycle={terminal.get('lifecycle_state')}; "
            f"expected_count={expected_count}")
    impairment = None
    impairment_path = output / "impaired-metadata.json"
    if case == "clean":
        receiver_message_result = observation.get("receiver_message_result", {})
        if (not expected.get("messages") or observed_count != expected_count or
                comparison_summary["matched_count"] != expected_count or
                comparison_summary["missed_count"] != 0 or
                comparison_summary["unexpected_count"] != 0 or
                comparison_summary["ambiguous_count"] != 0 or
                not all(comparison_summary["decoded_value_agrees"]) or
                observation.get("preamble", {}).get("locked") is not True or
                observation.get("assembler", {}).get("availability", {}).get("state") != "available" or
                receiver_message_result.get("accepted") is not True or
                int(receiver_message_result.get("decoded_pulse_count", 0)) != expected_count or
                spectrum.get("availability", {}).get("state") != "available" or
                spectrum.get("channel_index") != spectrum_channel or
                spectrum_channel is None or
                max((float(item.get("magnitude_linear_re_1_complex_unit", 0.0))
                     for item in spectrum.get("bins", [])), default=0.0) <= 0.0):
            raise RuntimeError("clean case lacks full independent timing/channel/value and terminal message agreement")
    elif case == "negative":
        preamble = observation.get("preamble", {})
        assembler = observation.get("assembler", {})
        receiver_message_result = observation.get("receiver_message_result", {})
        if (observed_count != 0 or detected_count != 0 or spectrum_channel is not None or
                spectrum.get("availability", {}).get("state") != "unavailable" or
                spectrum.get("availability", {}).get("reason") != "no_candidate_detected" or
                spectrum.get("channel_index") is not None or spectrum.get("bins") != [] or
                preamble.get("locked", False) is not False or
                assembler.get("availability", {}).get("state") != "available" or
                assembler.get("status") == "Ok" or
                receiver_message_result.get("availability", {}).get("state") != "available" or
                receiver_message_result.get("accepted") is not False or
                int(receiver_message_result.get("decoded_pulse_count", -1)) != 0):
            raise RuntimeError(
                "negative case fabricated a detection, lock, pulse, or message "
                f"completion: observed={observed_count}; detected={detected_count}; "
                f"spectrum_channel={spectrum_channel}; preamble={preamble}; "
                f"assembler={assembler}; receiver_message_result={receiver_message_result}")
    elif case == "impaired":
        impairment = json.loads(impairment_path.read_text(encoding="utf-8"))
        baseline = json.loads(
            (output / "phase4-clean-measured-baseline.json").read_text(encoding="utf-8"))
        measured_delta = (comparison_summary != baseline.get("comparison") or
                          spectrum_summary.get("bins_sha256") !=
                          baseline.get("spectrum", {}).get("bins_sha256"))
        if (impairment.get("seed") != 404 or impairment.get("cfo_hz") != 750.0 or
                impairment.get("eb_n0_db") != 18.0 or
                comparison_summary["matched_count"] + comparison_summary["missed_count"] != expected_count or
                spectrum.get("availability", {}).get("state") != "available" or
                not measured_delta):
            raise RuntimeError("impaired case identity or deterministic measured delta is incomplete")
    else:
        raise RuntimeError(f"unsupported served case: {case}")
    state_path = output / f"phase4-{case}-served-state.json"
    state_path.write_text(json.dumps({"case": case, "url": url,
                                      "generation": terminal_generation,
                                      "run_epoch": int(observation.get("run_epoch", 0)),
                                      "config_sha256": sha256(case_config),
                                      "iq_path": str(iq_path),
                                      "iq_sha256": sha256(iq_path),
                                      "truth_files_present": False,
                                      "terminal": terminal,
                                      "expected_pulse_count": expected_count,
                                      "expected_message_count": len(expected.get("messages", [])),
                                      "observed_pulse_count": observed_count,
                                      "detected_count": detected_count,
                                      "rejected_count": int(observation.get("rejected_count", 0)),
                                      "preamble": observation.get("preamble"),
                                      "assembler": observation.get("assembler"),
                                      "receiver_message_result": observation.get(
                                          "receiver_message_result"),
                                      "comparison": comparison_summary,
                                      "spectrum": spectrum_summary,
                                      "live_dom_contract": {
                                          "schedule_rows": len(schedule_messages),
                                          "timeline_rows": len(timeline_pulses),
                                          "contains_undefined": False,
                                          "timing_matches_expected_truth": True,
                                      },
                                      "concurrent_polling": polling_summary,
                                      "impairment_identity": ({
                                          "seed": impairment.get("seed"),
                                          "cfo_hz": impairment.get("cfo_hz"),
                                          "eb_n0_db": impairment.get("eb_n0_db"),
                                          "metadata_sha256": sha256(impairment_path),
                                      } if impairment is not None else None),
                                      "observation_id": observation.get("observation_id"),
                                      "observation_sha256": observation.get("observation_sha256")}, indent=2) + "\n",
                          encoding="utf-8")
    print(f"Phase 4 {case} state: {terminal.get('lifecycle_state')}", flush=True)


def start_served_phase5_case(port: int, case: str, output: Path,
                             url: str) -> None:
    def submit(target: str, body: dict[str, object], key: str) -> dict[str, object]:
        status, _, response = request(
            port, "POST", target, json.dumps(body).encode(),
            {"Content-Type": "application/json", "Idempotency-Key": key})
        if status != 202:
            raise RuntimeError(
                f"Phase 5 served job submission failed: HTTP {status}; "
                f"{response.decode(errors='replace')}")
        return json.loads(response)

    def wait_terminal(job_id: str) -> dict[str, object]:
        deadline = time.monotonic() + 75
        latest: dict[str, object] = {}
        while time.monotonic() < deadline:
            status, _, body = request(port, "GET", f"/api/v1/fhss/jobs/{job_id}")
            if status != 200:
                raise RuntimeError(f"Phase 5 served job lookup failed: HTTP {status}")
            latest = json.loads(body)
            if latest.get("state") in (
                    "completed", "cancelled", "timed_out", "failed"):
                return latest
            time.sleep(0.02)
        raise RuntimeError(f"Phase 5 served job did not terminate: {job_id}")

    token = str(time.time_ns())
    if case == "step":
        job = submit("/api/v1/fhss/commands/step",
                     {"request_id": f"screenshot-step-{token}",
                      "timeout_ms": 60000}, f"screenshot-step-{token}")
        terminal = wait_terminal(str(job["job_id"]))
        if terminal.get("state") != "completed":
            raise RuntimeError("Phase 5 Step screenshot case did not complete")
    elif case == "continue":
        job = submit("/api/v1/fhss/commands/continue",
                     {"request_id": f"screenshot-continue-{token}",
                      "message_count": 2, "timeout_ms": 60000},
                     f"screenshot-continue-{token}")
        terminal = wait_terminal(str(job["job_id"]))
        if terminal.get("state") != "completed":
            raise RuntimeError("Phase 5 Continue screenshot case did not complete")
    elif case == "cancelled":
        blocker = submit("/api/v1/fhss/commands/step",
                         {"request_id": f"screenshot-blocker-{token}",
                          "timeout_ms": 60000}, f"screenshot-blocker-{token}")
        job = submit("/api/v1/fhss/commands/step",
                     {"request_id": f"screenshot-cancel-{token}",
                      "timeout_ms": 60000}, f"screenshot-cancel-{token}")
        status, _, body = request(
            port, "POST", f"/api/v1/fhss/jobs/{job['job_id']}/cancel", b"{}",
            {"Content-Type": "application/json"})
        if status != 202:
            raise RuntimeError(f"Phase 5 queued cancellation failed: HTTP {status}")
        terminal = json.loads(body)
        if (terminal.get("state") != "cancelled" or
                terminal.get("work", {}).get("generator_invoked") is not False):
            raise RuntimeError("Phase 5 screenshot cancellation performed work")
        wait_terminal(str(blocker["job_id"]))
    else:
        raise RuntimeError(f"unsupported Phase 5 screenshot case: {case}")

    job_bytes = (json.dumps(terminal, sort_keys=True) + "\n").encode()
    artifacts = terminal.get("artifacts", {})
    job_root = output / "phase5-job-artifacts" / "fhss-jobs" / str(terminal["job_id"])
    iq_reference = artifacts.get("iq", {}) if isinstance(artifacts, dict) else {}
    receiver_reference = (artifacts.get("receiver_config", {})
                          if isinstance(artifacts, dict) else {})
    iq_path = job_root / str(iq_reference.get("relative_path", "missing"))
    receiver_path = job_root / str(receiver_reference.get(
        "relative_path", "missing"))
    state = {
        "schema": "graphx.dashboard.phase5.served_state.v1",
        "case": case,
        "url": url,
        "controller_epoch": terminal.get("controller_epoch"),
        "job_id": terminal.get("job_id"),
        "job_state": terminal.get("state"),
        "job_sha256": hashlib.sha256(job_bytes).hexdigest(),
        "generation": terminal.get("graph_generation", 0),
        "run_epoch": terminal.get("run_epoch", 0),
        "observation_id": (terminal.get("receiver_observation") or {}).get(
            "observation_id"),
        "observation_sha256": (terminal.get("receiver_observation") or {}).get(
            "observation_sha256"),
        "iq_path": str(iq_path) if iq_path.is_file() else None,
        "iq_sha256": sha256(iq_path) if iq_path.is_file() else None,
        "receiver_config_path": (str(receiver_path)
                                 if receiver_path.is_file() else None),
        "config_sha256": (sha256(receiver_path)
                          if receiver_path.is_file() else None),
        "truth_withheld_during_replay": case != "cancelled",
        "synthetic_data_only": True,
        "hwil_available": False,
    }
    state_path = output / f"phase5-{case}-served-state.json"
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def exercise(args: argparse.Namespace) -> int:
    if args.phase not in (1, 2, 3, 4, 5):
        raise RuntimeError("this operator implements Phases 1 through 5 only")
    phase = args.phase
    output = args.output_dir.resolve()
    output_preexisted = output.exists()
    if output_preexisted and any(output.iterdir()):
        raise RuntimeError(f"refusing preexisting nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / OWNED_MARKER).write_text(
        f"phase={phase}\ncreated_dir={0 if output_preexisted else 1}\n", encoding="utf-8")
    checks: list[dict[str, object]] = []

    def check(name: str, condition: bool, evidence: str) -> None:
        checks.append({"name": name, "pass": bool(condition), "evidence": evidence})

    process = None
    command: list[str] = []
    url = ""
    port = 0
    try:
        process, url, port, command = launch(args)
        status, page_headers, page = request(port, "GET", "/")
        check("packaged FHSS page", status == 200 and b"GraphX FHSS Dashboard" in page and
              b"Synthetic IQ evaluation only" in page, f"HTTP {status}; bytes={len(page)}")
        script_start = page.find(b"<script>")
        script_end = page.find(b"</script>", script_start)
        inline_script = (page[script_start + len(b"<script>"):script_end]
                         if script_start >= 0 and script_end > script_start else b"")
        inline_script_csp = "sha256-" + base64.b64encode(
            hashlib.sha256(inline_script).digest()).decode("ascii")
        content_security_policy = page_headers.get("content-security-policy", "")
        check("served inline dashboard script authorized by CSP",
              bool(inline_script) and
              f"'{inline_script_csp}'" in content_security_policy,
              f"computed={inline_script_csp}; csp={content_security_policy}")
        dashboard_contract_tokens = (
            b'id="observation-identity"', b'id="receiver-message-result"',
            b'id="receiver-decoder-details"', b'id="comparison-result"',
            b'id="receiver-spectrum"', b"confidence_score_uncalibrated",
            b"viterbi_best_path_metric", b"viterbi_second_best_path_metric",
            b"physical_channel_index", b"source_node_id",
            b"stale/indeterminate", b"availability?.reason",
            b"Unavailable \xe2\x80\x94 no observed physical channel",
            b"spectrumChannelElement.disabled = true",
            b"selectedSpectrumChannel === null",
        )
        check("Phase4 DOM exposes receiver evidence and stale/unavailable state",
              all(token in page for token in dashboard_contract_tokens),
              ", ".join(token.decode(errors="replace")
                         for token in dashboard_contract_tokens))
        schema_names = {
            "/healthz": "health", "/readyz": "readiness", "/api/v1/version": "version",
            "/api/v1/fhss/graph": "graph", "/api/v1/fhss/config": "config",
            "/api/v1/fhss/metrics": "metrics",
            "/api/v1/fhss/diagnostics": "diagnostics",
            "/api/v1/fhss/metrics/edges": "edge-metrics",
            "/api/v1/fhss/config/authoritative": "scenario",
            "/api/v1/fhss/config/effective": "config",
            "/api/v1/fhss/config/derived-paths": "derived-paths",
            "/api/v1/fhss/config/value?pointer=/nodes/0/node_config": "value",
            "/api/v1/fhss/nodes/source": "node",
            "/api/v1/fhss/nodes/source/parameters": "node-parameters",
            "/api/v1/fhss/visualization": "visualization",
        }
        if phase >= 2:
            schema_names.update({
                "/api/v1/fhss/config/provenance": "configuration-provenance",
                "/api/v1/fhss/graph/receiver-minimal": "receiver-graph",
            })
        if phase >= 3:
            schema_names["/api/v1/fhss/status"] = "runtime-status"
        if phase >= 4:
            schema_names.update({
                "/api/v1/fhss/expected-truth": "fhss-expected-truth",
                "/api/v1/fhss/observations": "fhss-receiver-observation",
                "/api/v1/fhss/comparison": "fhss-comparison-result",
                "/api/v1/fhss/spectrum?fft_size=128": "fhss-receiver-spectrum",
                "/api/v1/fhss/observation-provenance": "fhss-observation-provenance",
                "/api/v1/fhss/observation-history": "fhss-observation-history",
            })
        if phase >= 5:
            schema_names["/api/v1/fhss/jobs"] = "fhss-job-history"
        schema_root = API_DIR / "schemas"
        live_registry = load_registry(list(schema_root.glob("*.schema.json")))
        authoritative_registry = Registry()
        for schema_path in schema_root.glob("*.schema.json"):
            schema_document = json.loads(schema_path.read_text(encoding="utf-8"))
            authoritative_registry = authoritative_registry.with_resource(
                schema_document["$id"], Resource.from_contents(schema_document))
        live_hashes: dict[str, str] = {}
        evidence_dir = output / "artifacts"
        evidence_dir.mkdir()

        def persist(key: str, payload: bytes) -> str:
            name = hashlib.sha256(key.encode()).hexdigest() + ".bin"
            (evidence_dir / name).write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            live_hashes[key] = digest
            return digest

        def schema_valid(schema_name: str, payload: bytes) -> tuple[bool, dict[str, object]]:
            parsed = json.loads(payload)
            schema = json.loads((schema_root / f"{schema_name}.schema.json").read_text())
            Draft202012Validator.check_schema(schema)
            Draft202012Validator(schema, registry=authoritative_registry,
                                 format_checker=FormatChecker()).validate(parsed)
            validate_instance(parsed, schema, registry=live_registry)
            return True, parsed

        for target, schema_name in schema_names.items():
            status, headers, body = request(port, "GET", target)
            persist(f"GET {target}", body)
            schema_ok = False
            try:
                schema_ok, _ = schema_valid(schema_name, body)
            except Exception as error:
                checks.append({"name": f"schema {target}", "pass": False, "evidence": str(error)})
            check(f"GET {target}", status == 200 and schema_ok, f"HTTP {status}; schema={schema_ok}")
            if target == "/healthz":
                required = ("content-security-policy", "x-content-type-options",
                            "referrer-policy", "x-frame-options")
                check("defensive response headers", all(item in headers for item in required), str(required))
        if phase >= 3:
            initial_status_code, _, initial_status_body = request(
                port, "GET", "/api/v1/fhss/status")
            persist("initial_runtime_status", initial_status_body)
            initial_status = json.loads(initial_status_body)
            check("dashboard starts stopped before public lifecycle commands",
                  initial_status_code == 200 and
                  initial_status.get("lifecycle_state") == "not_built" and
                  initial_status.get("active_generation") == 0 and
                  initial_status.get("terminal_result") is None and
                  not initial_status.get("stop_requested"),
                  json.dumps(initial_status, sort_keys=True))
        if phase >= 5:
            def submit_job(target: str, payload: dict[str, object], key: str
                           ) -> tuple[int, dict[str, str], bytes, dict[str, object]]:
                job_status, job_headers, job_body = request(
                    port, "POST", target, json.dumps(payload).encode(),
                    {"Content-Type": "application/json", "Idempotency-Key": key})
                return job_status, job_headers, job_body, json.loads(job_body)

            def wait_job(job_id: str, timeout_seconds: float = 45.0
                         ) -> dict[str, object]:
                deadline = time.monotonic() + timeout_seconds
                latest: dict[str, object] = {}
                while time.monotonic() < deadline:
                    status_code, _, job_body = request(
                        port, "GET", f"/api/v1/fhss/jobs/{job_id}")
                    if status_code != 200:
                        raise RuntimeError(f"job lookup failed: HTTP {status_code}")
                    latest = json.loads(job_body)
                    if latest.get("state") in (
                            "completed", "cancelled", "timed_out", "failed",
                            "abandoned_on_restart"):
                        return latest
                    time.sleep(0.02)
                raise RuntimeError(f"job did not terminate: {job_id}; state={latest.get('state')}")

            step_key = "phase5-step-key"
            step_request = {"request_id": "phase5-step", "timeout_ms": 60000}
            step_status, _, step_body, step_job = submit_job(
                "/api/v1/fhss/commands/step", step_request, step_key)
            persist("phase5_step_submit", step_body)
            duplicate_status, _, duplicate_body, duplicate_job = submit_job(
                "/api/v1/fhss/commands/step", step_request, step_key)
            persist("phase5_step_duplicate", duplicate_body)
            conflict_status, conflict_headers, conflict_body, conflict_doc = submit_job(
                "/api/v1/fhss/commands/continue",
                {"request_id": "phase5-step", "message_count": 2}, step_key)
            persist("phase5_idempotency_conflict", conflict_body)
            check("Phase5 idempotent duplicate reuses one job",
                  step_status == 202 and duplicate_status == 200 and
                  step_job.get("job_id") == duplicate_job.get("job_id") and
                  duplicate_job.get("idempotency", {}).get("reused") is True,
                  f"create={step_status}; duplicate={duplicate_status}; ids={step_job.get('job_id')}/{duplicate_job.get('job_id')}")
            check("Phase5 idempotency conflict is RFC 9457",
                  conflict_status == 409 and
                  conflict_headers.get("content-type", "").startswith(
                      "application/problem+json") and
                  conflict_doc.get("code") ==
                      "idempotency_key_reused_with_different_payload",
                  f"HTTP {conflict_status}; code={conflict_doc.get('code')}")

            queued_status, _, queued_body, queued_job = submit_job(
                "/api/v1/fhss/commands/step",
                {"request_id": "phase5-queued-cancel", "timeout_ms": 60000},
                "phase5-queued-key")
            queued_id = str(queued_job.get("job_id", ""))
            queued_cancel_status, _, queued_cancel_body = request(
                port, "POST", f"/api/v1/fhss/jobs/{queued_id}/cancel", b"{}",
                {"Content-Type": "application/json"})
            queued_cancelled = json.loads(queued_cancel_body)
            persist("phase5_queued_cancel", queued_cancel_body)
            check("Phase5 queued cancel creates no work",
                  queued_status == 202 and queued_cancel_status == 202 and
                  queued_cancelled.get("state") == "cancelled" and
                  queued_cancelled.get("work", {}).get("generator_invoked") is False and
                  queued_cancelled.get("work", {}).get("receiver_replay_invoked") is False and
                  queued_cancelled.get("artifacts") == {},
                  json.dumps(queued_cancelled.get("work", {}), sort_keys=True))

            step_terminal = wait_job(str(step_job["job_id"]), 70)
            persist("phase5_step_terminal",
                    json.dumps(step_terminal, sort_keys=True).encode())
            step_schema_ok = False
            try:
                step_schema_ok, _ = schema_valid(
                    "fhss-job", json.dumps(step_terminal).encode())
            except Exception as error:
                checks.append({"name": "Phase5 terminal job schema", "pass": False,
                               "evidence": str(error)})
            check("Phase5 Step is one complete receiver message",
                  step_schema_ok and step_terminal.get("state") == "completed" and
                  int(step_terminal.get("message_count", 0)) == 1 and
                  step_terminal.get("receiver_message_result", {}).get("accepted") is True and
                  step_terminal.get("graph_lifecycle", {}).get("terminal_code") ==
                      "execution_completed" and
                  step_terminal.get("comparison", {}).get("evaluation_state") ==
                      "evaluated",
                  f"state={step_terminal.get('state')}; schema={step_schema_ok}")
            job_root = (output / "phase5-job-artifacts" / "fhss-jobs" /
                        str(step_terminal["job_id"]))
            manifest_path = job_root / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            truth_path = job_root / "truth.withheld.json"
            truth_document = json.loads(truth_path.read_text(encoding="utf-8"))
            sigmf_path = job_root / "iq.sigmf-meta"
            receiver_config_path = job_root / "receiver-minimal.json"
            iq_reference = manifest["artifacts"]["iq"]
            iq_file = job_root / iq_reference["relative_path"]
            artifact_checks = all(path.is_file() for path in (
                manifest_path, truth_path, sigmf_path, receiver_config_path, iq_file))
            hash_checks = (
                sha256(iq_file) == iq_reference["sha256"] and
                sha256(truth_path) == manifest["artifacts"]["truth"]["sha256"] and
                sha256(sigmf_path) == manifest["artifacts"]["sigmf"]["sha256"] and
                sha256(receiver_config_path) ==
                    manifest["artifacts"]["receiver_config"]["sha256"])
            receiver_document = json.loads(
                receiver_config_path.read_text(encoding="utf-8"))
            receiver_text = json.dumps(receiver_document, sort_keys=True)
            forbidden_receiver_keys = (
                "messages", "truth", "truth_sha256", "expected_words",
                "scenario_correlation_id", "job_id", "generator_metadata")
            receiver_isolated = all(
                f'"{key}"' not in receiver_text for key in forbidden_receiver_keys)
            check("Phase5 separate atomic artifacts and hashes",
                  artifact_checks and hash_checks and receiver_isolated and
                  manifest.get("receiver_truth_access") ==
                      "withheld_before_replay",
                  f"files={artifact_checks}; hashes={hash_checks}; isolated={receiver_isolated}")
            oracle_pulses = truth_document.get("truth_pulses", [])
            independent_timing = (
                len(oracle_pulses) == 18 and all(
                    int(oracle_pulses[index]["received_global_start_sample"]) ==
                    (index + 1) * 6500 for index in range(len(oracle_pulses))))
            check("Phase5 independent one-message timing oracle",
                  independent_timing,
                  f"pulse_count={len(oracle_pulses)}; slot=6500 input samples")

            continue_status, _, continue_body, continue_job = submit_job(
                "/api/v1/fhss/commands/continue",
                {"request_id": "phase5-continue", "message_count": 2,
                 "timeout_ms": 60000}, "phase5-continue-key")
            persist("phase5_continue_submit", continue_body)
            continue_terminal = wait_job(str(continue_job["job_id"]), 70)
            persist("phase5_continue_terminal",
                    json.dumps(continue_terminal, sort_keys=True).encode())
            check("Phase5 Continue uses bounded complete messages",
                  continue_status == 202 and
                  continue_terminal.get("state") == "completed" and
                  int(continue_terminal.get("message_count", 0)) == 2 and
                  continue_terminal.get("receiver_message_result", {}).get(
                      "accepted") is True,
                  f"HTTP {continue_status}; state={continue_terminal.get('state')}")

            reset_status, _, reset_body = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            reset_doc = json.loads(reset_body)
            repeated_reset_status, _, repeated_reset_body = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            repeated_reset = json.loads(repeated_reset_body)
            persist("phase5_reset", reset_body)
            check("Phase5 Reset is deterministic and retains jobs",
                  reset_status == repeated_reset_status == 200 and
                  reset_doc.get("status") == "reset_completed" and
                  repeated_reset.get("status") == "already_reset" and
                  reset_doc.get("controller_epoch") ==
                      repeated_reset.get("controller_epoch") and
                  int(reset_doc.get("retained_job_count", 0)) >= 2,
                  json.dumps(reset_doc, sort_keys=True))

            timeout_status, _, timeout_body, timeout_job = submit_job(
                "/api/v1/fhss/commands/continue",
                {"request_id": "phase5-timeout", "message_count": 4,
                 "timeout_ms": 100}, "phase5-timeout-key")
            persist("phase5_timeout_submit", timeout_body)
            timeout_terminal = wait_job(str(timeout_job["job_id"]), 30)
            check("Phase5 deterministic timeout is terminal",
                  timeout_status == 202 and
                  timeout_terminal.get("state") == "timed_out" and
                  timeout_terminal.get("terminal", {}).get("code") == "job_timeout",
                  json.dumps(timeout_terminal.get("terminal", {}), sort_keys=True))
            request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                    {"Content-Type": "application/json"})
            active_status, _, active_body, active_job = submit_job(
                "/api/v1/fhss/commands/step",
                {"request_id": "phase5-active-cancel", "timeout_ms": 60000},
                "phase5-active-key")
            active_id = str(active_job["job_id"])
            active_seen = False
            active_deadline = time.monotonic() + 5
            while time.monotonic() < active_deadline:
                _, _, active_state_body = request(
                    port, "GET", f"/api/v1/fhss/jobs/{active_id}")
                active_state = json.loads(active_state_body)
                if active_state.get("state") != "queued":
                    active_seen = True
                    break
                time.sleep(0.01)
            cancel_started = time.monotonic()
            active_cancel_status, _, active_cancel_body = request(
                port, "POST", f"/api/v1/fhss/jobs/{active_id}/cancel", b"{}",
                {"Content-Type": "application/json"}, timeout=8)
            active_terminal = wait_job(active_id, 10)
            active_cancel_elapsed = time.monotonic() - cancel_started
            persist("phase5_active_cancel", active_cancel_body)
            check("Phase5 active cancellation is bounded",
                  active_status == 202 and active_seen and
                  active_cancel_status in (200, 202) and
                  active_terminal.get("state") == "cancelled" and
                  active_cancel_elapsed < 8,
                  f"state={active_terminal.get('state')}; seconds={active_cancel_elapsed:.3f}")

            # Preserve truth evidence externally, then remove it before a
            # dashboard-free public receiver replay.
            persist("phase5_step_truth", truth_path.read_bytes())
            persist("phase5_step_sigmf", sigmf_path.read_bytes())
            persist("phase5_step_iq", iq_file.read_bytes())
            truth_path.unlink()
            offline_summary = output / "phase5-offline-summary.json"
            offline_command = [str(locate_executable(args.build_dir.resolve())),
                               "--graph-config", str(receiver_config_path),
                               "--summary-json", str(offline_summary)]
            offline = subprocess.run(offline_command, text=True,
                                     capture_output=True, timeout=90)
            check("Phase5 offline receiver replay needs no dashboard or truth",
                  offline.returncode == 0 and offline_summary.is_file() and
                  not truth_path.exists(),
                  f"exit={offline.returncode}; truth_present={truth_path.exists()}")
            phase5_screenshot_manifest = {
                "schema": "graphx.dashboard.phase5.screenshot_manifest.v1",
                "dashboard_url": url,
                "capture_required": True,
                "synthetic_data_only": True,
                "required_cases": ["step", "continue", "cancelled"],
                "captured_files": {},
                "reason_not_automated": (
                    "genuine browser capture is an explicit operator action"),
            }
            phase5_screenshot_bytes = (
                json.dumps(phase5_screenshot_manifest, indent=2) + "\n").encode()
            (output / "phase5-screenshot-manifest.json").write_bytes(
                phase5_screenshot_bytes)
            persist("phase5_screenshot_manifest", phase5_screenshot_bytes)
        status, headers, body = request(port, "PUT", "/healthz")
        problem = json.loads(body)
        persist("PUT /healthz", body)
        check("unsupported method", status == 405 and "allow" in headers, f"HTTP {status}")
        check("RFC 9457 problem", headers.get("content-type", "").startswith("application/problem+json")
              and problem.get("status") == status and all(k in problem for k in ("type", "title", "detail")),
              problem.get("type", "missing"))
        try:
            problem_schema_ok, _ = schema_valid("problem", body)
        except Exception:
            problem_schema_ok = False
        check("problem response schema", problem_schema_ok, f"HTTP {status}")
        for target in ("/../CMakeLists.txt", "/%2e%2e/CMakeLists.txt"):
            status, _, _ = request(port, "GET", target)
            check(f"reject traversal {target}", status == 404, f"HTTP {status}")
        status, _, _ = raw_request(port, b"NOT HTTP\r\n\r\n")
        check("reject malformed request", status == 400, f"HTTP {status}")
        status, _, _ = raw_request(
            port, b"GET /healthz HTTP/1.1\r\nHost: localhost\r\nX-Large: " +
                  b"x" * 17000 + b"\r\nConnection: close\r\n\r\n")
        check("reject oversized header", status == 431, f"HTTP {status}")
        status, _, _ = raw_request(
            port, b"POST /api/v1/fhss/config HTTP/1.1\r\nHost: localhost\r\n"
                  b"Content-Length: 2\r\nTransfer-Encoding: chunked\r\n"
                  b"Connection: close\r\n\r\n{}")
        check("reject conflicting framing", status == 400, f"HTTP {status}")
        slow = socket.create_connection(("127.0.0.1", port), timeout=5)
        slow.sendall(b"GET /healthz HTTP/1.1\r\nHost: localhost\r\n")
        started = time.monotonic()
        concurrent_status, _, _ = request(port, "GET", "/healthz")
        slow.close()
        check("slow client isolation", concurrent_status == 200 and time.monotonic() - started < 1.0,
              f"HTTP {concurrent_status}")
        old_status, _, _ = request(port, "GET", "/api/v1/graph")
        check("generic application route absent", old_status == 404, f"HTTP {old_status}")
        status, _, _ = raw_request(
            port, b"POST /api/v1/fhss/config HTTP/1.1\r\nHost: localhost\r\n"
                  b"Content-Type: application/json\r\nContent-Length: 1048577\r\n"
                  b"Connection: close\r\n\r\n")
        check("reject oversized body", status == 413, f"HTTP {status}")
        executable = locate_executable(args.build_dir.resolve())
        invalid_bind = subprocess.run(
            [str(executable), "--dashboard-no-run", "--dashboard-host", "0.0.0.0",
             "--dashboard-port", "0"], text=True, capture_output=True, timeout=15)
        bind_output = invalid_bind.stdout + invalid_bind.stderr
        check("reject non-loopback bind", invalid_bind.returncode != 0 and "loopback" in bind_output,
              f"exit={invalid_bind.returncode}")
        phase2_hashes: dict[str, str] = {}
        if phase >= 2:
            a_status, a_headers, authoritative_bytes = request(
                port, "GET", "/api/v1/fhss/config/authoritative")
            b_status, b_headers, _ = request(port, "GET", "/api/v1/fhss/config/authoritative")
            etag = a_headers.get("etag", "")
            authoritative_doc = json.loads(authoritative_bytes)
            scenario = authoritative_doc["scenario"]
            preamble = scenario["messages"][0]["pulses"][:16]
            independently_active = sorted({int(pulse["frequency_index"]) for pulse in preamble})
            check("two sessions share strong ETag", a_status == b_status == 200 and etag and
                  etag == b_headers.get("etag") and not etag.startswith("W/"), etag)
            check("independent active set", independently_active == [24, 28, 32, 36],
                  json.dumps(independently_active))

            patch = json.dumps([{"op": "replace", "path": "/iq_center_frequency_hz",
                                 "value": 1240000001.0}]).encode()
            patch_headers = {"Content-Type": "application/json-patch+json", "If-Match": etag}
            v_status, v_headers, v_body = request(port, "POST", "/api/v1/fhss/config/validate",
                                                   patch, patch_headers)
            persist("validation", v_body)
            try:
                validation_schema_ok, _ = schema_valid("config-validation", v_body)
            except Exception:
                validation_schema_ok = False
            check("validation response schema", validation_schema_ok,
                  f"HTTP {v_status}; bytes={len(v_body)}")
            after_v_status, after_v_headers, after_v_body = request(
                port, "GET", "/api/v1/fhss/config/authoritative")
            check("validation no mutation", v_status == 200 and after_v_status == 200 and
                  after_v_headers.get("etag") == etag and after_v_body == authoritative_bytes,
                  f"validate={v_status}; etag={after_v_headers.get('etag')}")

            missing_status, _, _ = request(port, "PATCH", "/api/v1/fhss/config", patch,
                {"Content-Type": "application/json-patch+json"})
            check("If-Match required", missing_status == 428, f"HTTP {missing_status}")
            start = threading.Barrier(3)
            concurrent_patches = [
                json.dumps([{"op": "replace", "path": "/iq_center_frequency_hz",
                             "value": value}]).encode()
                for value in (1240000001.0, 1240000002.0)]

            def concurrent_writer(payload: bytes) -> tuple[int, dict[str, str], bytes]:
                start.wait()
                return request(port, "PATCH", "/api/v1/fhss/config", payload, patch_headers)

            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(concurrent_writer, payload)
                           for payload in concurrent_patches]
                start.wait()
                writer_results = [future.result(timeout=8) for future in futures]
            check("simultaneous same-ETag writers", sorted(item[0] for item in writer_results) == [200, 412],
                  json.dumps([item[0] for item in writer_results]))
            winner = next(item for item in writer_results if item[0] == 200)
            loser = next(item for item in writer_results if item[0] == 412)
            apply_status, apply_headers, apply_body = winner
            persist("apply", apply_body)
            persist("precondition_error", loser[2])
            new_etag = apply_headers.get("etag", "")
            try:
                apply_schema_ok, _ = schema_valid("config-result", apply_body)
                stale_schema_ok, _ = schema_valid("problem", loser[2])
            except Exception:
                apply_schema_ok = stale_schema_ok = False
            check("apply result schema", apply_schema_ok, f"HTTP {apply_status}")
            check("concurrent precondition error schema", stale_schema_ok, f"HTTP {loser[0]}")
            check("apply once", apply_status == 200 and new_etag and new_etag != etag,
                  f"HTTP {apply_status}; ETag={new_etag}")
            legacy = json.dumps({"expected_revision": 1, "pointer":
                "/fhss/scenario/iq_center_frequency_hz", "value": 1}).encode()
            legacy_status, _, _ = request(port, "PATCH", "/api/v1/fhss/config", legacy,
                                           {"Content-Type": "application/json"})
            check("isolated legacy conflict", legacy_status == 409, f"HTTP {legacy_status}")

            before_atomic_status, before_atomic_headers, before_atomic = request(
                port, "GET", "/api/v1/fhss/config/authoritative")
            failing = json.dumps([
                {"op": "replace", "path": "/iq_center_frequency_hz", "value": 9},
                {"op": "remove", "path": "/missing"}]).encode()
            atomic_status, _, _ = request(port, "PATCH", "/api/v1/fhss/config", failing,
                {"Content-Type": "application/json-patch+json", "If-Match": new_etag})
            _, after_atomic_headers, after_atomic = request(
                port, "GET", "/api/v1/fhss/config/authoritative")
            check("atomic failed patch", before_atomic_status == 200 and atomic_status == 400 and
                  after_atomic_headers.get("etag") == before_atomic_headers.get("etag") and
                  after_atomic == before_atomic, f"HTTP {atomic_status}")

            receiver_status, _, receiver_bytes = request(
                port, "GET", "/api/v1/fhss/graph/receiver-minimal")
            receiver = json.loads(receiver_bytes)["graph"]
            persist("receiver_graph", receiver_bytes)
            if phase == 4:
                (output / "phase4-receiver-minimal.json").write_bytes(
                    receiver_bytes)
            persist("authoritative", authoritative_bytes)
            def recursive_keys(value: object) -> list[str]:
                if isinstance(value, dict):
                    return [str(key).lower() for key in value] + [
                        key for child in value.values() for key in recursive_keys(child)]
                if isinstance(value, list):
                    return [key for child in value for key in recursive_keys(child)]
                return []
            receiver_keys = recursive_keys(receiver)
            injected_receiver = copy.deepcopy(receiver)
            injected_receiver["messages"] = []
            injected_receiver_keys = recursive_keys(injected_receiver)
            source = next((node for node in receiver.get("nodes", []) if node.get("id") == "source"), {})
            minimal_nodes = [node for node in receiver.get("nodes", [])
                             if node.get("id") in ("preamble", "assembler")]
            def side_channel_key(key: str) -> bool:
                return (key == "messages" or "truth" in key or
                        ("generator" in key and "metadata" in key) or
                        ("expected" in key and ("value" in key or "word" in key)) or
                        ("transmitted" in key and "frequency" in key) or
                        ("burst" in key and "epoch" in key) or
                        key == "active_frequency_indices")
            check("receiver-minimal truth separation", receiver_status == 200 and
                  source.get("type") == "FHSSBinaryIqFileSourceNode" and
                  not any(side_channel_key(key) for key in receiver_keys) and
                  all("active_frequency_indices" not in node.get("node_config", {})
                      for node in minimal_nodes), f"HTTP {receiver_status}; source={source.get('type')}")
            check("receiver audit detects forbidden container keys",
                  any(side_channel_key(key) for key in injected_receiver_keys) and
                  not any(side_channel_key(key) for key in receiver_keys),
                  "recursive path-component audit rejects injected messages container")
            if phase < 3:
                for target in ("/api/v1/fhss/config/rebuild", "/api/v1/fhss/commands/start",
                               "/api/v1/fhss/commands/stop", "/api/v1/fhss/operations/op-1"):
                    hidden_status, _, _ = request(port, "POST", target, b"{}",
                                                   {"Content-Type": "application/json"})
                    check(f"Phase3 route hidden {target}", hidden_status in (404, 405),
                          f"HTTP {hidden_status}")
            phase2_hashes = {
                "authoritative": hashlib.sha256(authoritative_bytes).hexdigest(),
                "validation": hashlib.sha256(v_body).hexdigest(),
                "apply": hashlib.sha256(apply_body).hexdigest(),
                "receiver_graph": hashlib.sha256(receiver_bytes).hexdigest(),
                **live_hashes,
            }
        if phase >= 3:
            _, current_headers, current_bytes = request(port, "GET", "/api/v1/fhss/config/authoritative")
            current = json.loads(current_bytes)
            if phase >= 4:
                receiver_channelizer = next(
                    (node for node in receiver.get("nodes", [])
                     if node.get("id") == "channelizer"), {})
                receiver_offsets = receiver_channelizer.get(
                    "node_config", {}).get("iq_offsets", [])
                offset_indices = [entry.get("index") for entry in receiver_offsets]
                scenario = current.get("scenario", {})
                clean_guard_samples = 6_500
                clean_patch_statuses = []
                clean_patch_responses = []
                scenario_messages = scenario.get("messages", [])
                for message_index in range(len(scenario_messages) - 1, -1, -1):
                    message = scenario_messages[message_index]
                    clean_patch = [{
                        "op": "replace",
                        "path": f"/messages/{message_index}/transmit_start_sample",
                        "value": int(message["transmit_start_sample"]) + clean_guard_samples,
                    }]
                    clean_patch_status, _, clean_patch_body = request(
                        port, "PATCH", "/api/v1/fhss/config",
                        json.dumps(clean_patch).encode(),
                        {"Content-Type": "application/json-patch+json",
                         "If-Match": current_headers["etag"]})
                    clean_patch_statuses.append(clean_patch_status)
                    try:
                        clean_patch_response = json.loads(clean_patch_body)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        clean_patch_response = clean_patch_body.decode(errors="replace")
                    clean_patch_responses.append(clean_patch_response)
                    if (clean_patch_status != 200 or
                            not isinstance(clean_patch_response, dict) or
                            clean_patch_response.get("status") != "applied"):
                        break
                    _, current_headers, current_bytes = request(
                        port, "GET", "/api/v1/fhss/config/authoritative")
                    current = json.loads(current_bytes)
                persist(
                    "phase4_clean_waveform_patch",
                    (json.dumps({"statuses": clean_patch_statuses,
                                 "responses": clean_patch_responses},
                                sort_keys=True) + "\n").encode())
                patched_scenario = current.get("scenario", {})
                check("Phase4 receiver-compatible clean waveform contract",
                      len(clean_patch_statuses) == len(scenario_messages) and
                      all(status == 200 for status in clean_patch_statuses) and
                      all(isinstance(response, dict) and
                          response.get("status") == "applied"
                          for response in clean_patch_responses) and
                      offset_indices == list(range(64)) and
                      len(receiver_offsets) == 64 and
                      all(int(after["transmit_start_sample"]) ==
                          int(before["transmit_start_sample"]) + clean_guard_samples
                          for before, after in zip(scenario.get("messages", []),
                                                   patched_scenario.get("messages", []))),
                      "explicit receiver IQ map copied to generator; 6500-sample causal warm-up; receiver still receives no schedule")
            schedule_path, iq_path = output / "schedule.json", output / "replay.cf32"
            truth_path, sigmf_path = output / "truth.json", output / "replay.sigmf-meta"
            generator_schedule = dict(
                current["scenario"] if phase >= 4 else authoritative_doc["scenario"])
            if phase >= 4:
                generator_schedule["iq_offsets"] = receiver_offsets
            generator_schedule["active_frequency_indices"] = independently_active
            schedule_path.write_text(json.dumps(generator_schedule, indent=2))
            generator_command = [str(locate_generator(args.build_dir.resolve())), "--message-json", str(schedule_path),
                                 "--iq-output", str(iq_path), "--truth-output", str(truth_path),
                                 "--sigmf-meta", str(sigmf_path), "--force"]
            generated = subprocess.run(
                generator_command, text=True, capture_output=True,
                timeout=GENERATOR_TIMEOUT_SECONDS)
            check("synthetic IQ generation", generated.returncode == 0 and iq_path.stat().st_size > 0,
                  f"exit={generated.returncode}; bytes={iq_path.stat().st_size if iq_path.exists() else 0}")
            for artifact_name, artifact_path in (("generated_iq", iq_path),
                                                  ("generated_truth", truth_path),
                                                  ("generated_schedule", schedule_path),
                                                  ("generated_sigmf", sigmf_path)):
                if artifact_path.exists():
                    persist(artifact_name, artifact_path.read_bytes())
            if truth_path.exists(): truth_path.unlink()
            if schedule_path.exists(): schedule_path.unlink()
            replay_patch = json.dumps([{"op":"add","path":"/receiver_input","value":{
                "file_path":str(iq_path),"sample_format":"cf32_le","first_complex_sample":0,
                "max_complex_samples":0,"max_read_complex_samples":4194304}}]).encode()
            p_status, p_headers, p_body = request(port,"PATCH","/api/v1/fhss/config",replay_patch,
                {"Content-Type":"application/json-patch+json","If-Match":current_headers["etag"]})
            check("patch receiver path only", p_status == 200, f"HTTP {p_status}")
            revision = json.loads(p_body).get("new_revision", 0)
            _, _, generation_base_body = request(
                port, "GET", "/api/v1/fhss/status")
            generation_base = int(
                json.loads(generation_base_body).get("active_generation", 0))
            generations = []
            for number in (1,2):
                expected_generation = generation_base + number
                rebuild_body = json.dumps({"expected_revision":revision,"command_id":f"phase3-rebuild-{number}"}).encode()
                rebuild_started = time.monotonic()
                r_status, _, r_body = request(port,"POST","/api/v1/fhss/config/rebuild",rebuild_body,{"Content-Type":"application/json"}, timeout=20)
                persist(f"generation_{number}_rebuild", r_body)
                rebuild_elapsed = time.monotonic() - rebuild_started
                try:
                    rebuild_schema_ok, rebuild_doc = schema_valid("rebuild-result", r_body)
                except Exception:
                    rebuild_schema_ok, rebuild_doc = False, {}
                check(f"generation {number} rebuild returned", rebuild_elapsed < 20 and rebuild_schema_ok,
                      f"HTTP {r_status}; seconds={rebuild_elapsed:.3f}; schema={rebuild_schema_ok}")
                generation = rebuild_doc.get("active_generation",0)
                generations.append(generation)
                s_status, _, s_body = request(port,"POST","/api/v1/fhss/commands/start",
                    json.dumps({"command_id":f"phase3-start-{number}"}).encode(),{"Content-Type":"application/json"})
                persist(f"generation_{number}_start", s_body)
                try:
                    start_schema_ok, _ = schema_valid("command-result", s_body)
                except Exception:
                    start_schema_ok = False
                check(f"generation {number} start returned", start_schema_ok, f"HTTP {s_status}; schema={start_schema_ok}")
                terminal_doc = {}
                terminal_deadline = time.monotonic() + 20
                while time.monotonic() < terminal_deadline:
                    terminal_status, _, terminal_body = request(port,"GET","/api/v1/fhss/status")
                    terminal_doc = json.loads(terminal_body)
                    if terminal_doc.get("lifecycle_state") in ("completed", "failed"):
                        break
                    time.sleep(0.05)
                persist(f"generation_{number}_status", json.dumps(terminal_doc, sort_keys=True).encode())
                try:
                    status_schema_ok, _ = schema_valid("runtime-status", json.dumps(terminal_doc).encode())
                except Exception:
                    status_schema_ok = False
                terminal_result = terminal_doc.get("terminal_result") or {}
                check(f"generation {number} natural terminal result",
                      terminal_status == 200 and status_schema_ok and
                      terminal_doc.get("lifecycle_state") == "completed" and
                      terminal_result.get("generation") == expected_generation and
                      terminal_result.get("code") == "execution_completed" and
                      not terminal_doc.get("stop_requested"),
                      json.dumps(terminal_doc, sort_keys=True))
                m_status, _, m_body = request(port,"GET","/api/v1/fhss/metrics")
                check(f"generation {number} metrics returned", True, f"HTTP {m_status}")
                persist(f"generation_{number}_metrics",m_body)
                metrics_doc = json.loads(m_body)
                diagnostics_status, _, diagnostics_body = request(
                    port, "GET", "/api/v1/fhss/diagnostics")
                persist(f"generation_{number}_diagnostics", diagnostics_body)
                try:
                    diagnostics_ok, diagnostics_doc = schema_valid(
                        "diagnostics", diagnostics_body)
                except Exception:
                    diagnostics_ok, diagnostics_doc = False, {}
                traffic = sum(int(edge.get("messages_enqueued", 0)) + int(edge.get("messages_dequeued", 0))
                              for edge in metrics_doc.get("edges", []))
                metric_definitions = metrics_doc.get("metric_definitions", [])
                expected_metric_units = {
                    ("graph", "total_items_processed"): "item",
                    ("graph", "total_items_rejected"): "item",
                    ("graph", "total_messages_processed"): "message",
                    ("graph", "graph_total_enqueued"): "message",
                    ("graph", "graph_total_dequeued"): "message",
                    ("graph", "backpressure_events"): "event",
                    ("graph", "peak_queue_depth"): "message",
                    ("graph", "peak_active_threads"): "thread",
                    ("node", "inbound_messages"): "message",
                    ("node", "outbound_messages"): "message",
                    ("node", "rejected_messages"): "message",
                    ("node", "backpressure_events"): "event",
                    ("node", "peak_queue_depth"): "message",
                    ("edge", "messages_enqueued"): "message",
                    ("edge", "messages_dequeued"): "message",
                    ("edge", "messages_rejected"): "message",
                    ("edge", "backpressure_events"): "event",
                    ("edge", "current_queue_depth"): "message",
                    ("edge", "peak_queue_depth"): "message",
                }
                metric_definition_contract = (
                    len(metric_definitions) == 19 and
                    {(item.get("scope"), item.get("name"))
                     for item in metric_definitions} == set(expected_metric_units) and
                    all(item.get("kind") in ("counter", "gauge", "distribution") and
                        item.get("unit") == expected_metric_units.get(
                            (item.get("scope"), item.get("name"))) and
                        isinstance(item.get("monotonic"), bool) and
                        item.get("reset") == "new_graph_generation"
                        for item in metric_definitions))
                check(f"generation {number} attributed nonzero traffic",
                      metrics_doc.get("active_generation") == expected_generation and
                      int(metrics_doc.get("active_run_epoch", 0)) >= 1 and
                      int(metrics_doc.get("active_config_revision", 0)) >= 1 and
                      bool(metrics_doc.get("active_config_etag")) and
                      metric_definition_contract and traffic > 0,
                      f"generation={metrics_doc.get('active_generation')}; "
                      f"run={metrics_doc.get('active_run_epoch')}; "
                      f"revision={metrics_doc.get('active_config_revision')}; "
                      f"definitions={len(metric_definitions)}; traffic={traffic}")
                check(f"generation {number} diagnostics identity is current",
                      diagnostics_status == 200 and diagnostics_ok and
                      diagnostics_doc.get("active_generation") ==
                          metrics_doc.get("active_generation") and
                      diagnostics_doc.get("active_run_epoch") ==
                          metrics_doc.get("active_run_epoch") and
                      diagnostics_doc.get("active_config_revision") ==
                          metrics_doc.get("active_config_revision") and
                      diagnostics_doc.get("active_config_etag") ==
                          metrics_doc.get("active_config_etag"),
                      json.dumps({
                          "metrics": [metrics_doc.get("active_generation"),
                                      metrics_doc.get("active_run_epoch")],
                          "diagnostics": [diagnostics_doc.get(
                              "active_generation"), diagnostics_doc.get(
                                  "active_run_epoch")],
                      }, sort_keys=True))
                stop_started=time.monotonic(); x_status,_,x_body=request(port,"POST","/api/v1/fhss/commands/stop",
                    json.dumps({"command_id":f"phase3-stop-{number}"}).encode(),{"Content-Type":"application/json"})
                persist(f"generation_{number}_stop", x_body)
                try:
                    stop_schema_ok, stop_doc = schema_valid("command-result", x_body)
                except Exception:
                    stop_schema_ok, stop_doc = False, {}
                check(f"generation {number} stop returned", stop_schema_ok and stop_doc.get("code") == "already_completed",
                      f"HTTP {x_status}; schema={stop_schema_ok}; code={stop_doc.get('code')}")
                check(f"real replay generation {number}", r_status==200 and s_status==202 and m_status==200 and x_status==200,
                      f"rebuild={r_status}; start={s_status}; stop={x_status}")
                check(f"bounded stop generation {number}", time.monotonic()-stop_started < 5, "under 5 seconds")
            check("two real generations",
                  generations == [generation_base + 1, generation_base + 2],
                  json.dumps(generations))
            check("truth isolated before replay", not truth_path.exists() and not schedule_path.exists(), "truth and scenario deleted")

            if phase >= 4:
                cases_dir = output / "phase4-cases"
                cases_dir.mkdir(exist_ok=True)
                _, _, clean_case_body = request(
                    port, "GET", "/api/v1/fhss/config/authoritative")
                clean_authoritative = json.loads(clean_case_body)
                _, _, clean_effective_body = request(
                    port, "GET", "/api/v1/fhss/config/effective")
                clean_effective = json.loads(clean_effective_body)["effective"]
                clean_scenario = dict(clean_authoritative["scenario"])
                clean_scenario["receiver_input"] = receiver_input_from_effective(
                    clean_effective)
                clean_case = {"fhss": {"scenario": clean_scenario}}
                (cases_dir / "clean-config.json").write_text(
                    json.dumps(clean_case, indent=2) + "\n", encoding="utf-8")
                persist("phase4_clean_reload_config",
                        (cases_dir / "clean-config.json").read_bytes())
                phase4_documents: dict[str, dict[str, object]] = {}
                for label, target, schema_name in (
                    ("clean_expected", "/api/v1/fhss/expected-truth", "fhss-expected-truth"),
                    ("clean_observed", "/api/v1/fhss/observations", "fhss-receiver-observation"),
                    ("clean_comparison", "/api/v1/fhss/comparison", "fhss-comparison-result"),
                    ("clean_provenance", "/api/v1/fhss/observation-provenance", "fhss-observation-provenance"),
                ):
                    route_status, _, route_body = request(port, "GET", target)
                    persist(label, route_body)
                    try:
                        route_schema_ok, route_doc = schema_valid(schema_name, route_body)
                    except Exception as error:
                        route_schema_ok, route_doc = False, {"validation_error": str(error)}
                    phase4_documents[label] = route_doc
                    check(f"Phase4 {label} contract", route_status == 200 and route_schema_ok,
                          f"HTTP {route_status}; schema={route_schema_ok}")
                expected_doc = phase4_documents["clean_expected"]
                observed_doc = phase4_documents["clean_observed"]
                comparison_doc = phase4_documents["clean_comparison"]
                clean_receiver_pulses = observed_doc.get("observed_pulses", [])
                clean_spectrum_channel = (int(clean_receiver_pulses[0]["physical_channel_index"])
                                          if clean_receiver_pulses else None)
                clean_spectrum_status, _, clean_spectrum_body = request(
                    port, "GET",
                    f"/api/v1/fhss/spectrum?channel={clean_spectrum_channel}&fft_size=128")
                persist("clean_spectrum", clean_spectrum_body)
                try:
                    clean_spectrum_ok, clean_spectrum_doc = schema_valid(
                        "fhss-receiver-spectrum", clean_spectrum_body)
                except Exception as error:
                    clean_spectrum_ok, clean_spectrum_doc = False, {
                        "validation_error": str(error)}
                phase4_documents["clean_spectrum"] = clean_spectrum_doc
                check("Phase4 clean_spectrum contract",
                      clean_spectrum_status == 200 and clean_spectrum_ok and
                      clean_spectrum_doc.get("channel_index") == clean_spectrum_channel,
                      f"HTTP {clean_spectrum_status}; channel={clean_spectrum_channel}; schema={clean_spectrum_ok}")
                check("Phase4 semantic separation",
                      expected_doc.get("semantic_class") == "expected" and
                      observed_doc.get("semantic_class") == "observed" and
                      comparison_doc.get("semantic_class") == "comparison" and
                      "messages" not in observed_doc and "truth_sha256" not in observed_doc,
                      "expected, observed, and comparison are distinct documents")
                check("Phase4 run attribution",
                      observed_doc.get("generation") == generation_base + 2 and
                      int(observed_doc.get("run_epoch", 0)) >= 1 and
                      comparison_doc.get("generation") == generation_base + 2,
                      f"generation={observed_doc.get('generation')}; run={observed_doc.get('run_epoch')}")
                clean_matches = comparison_doc.get("matches", [])
                clean_comparison_summary = {
                    "availability": comparison_doc.get("availability"),
                    "matched_count": len(clean_matches),
                    "missed_count": len(comparison_doc.get("missed_expected_indices", [])),
                    "unexpected_count": len(comparison_doc.get("unexpected_observed_indices", [])),
                    "ambiguous_count": len(comparison_doc.get("ambiguous", [])),
                    "timing_delta_samples": [item.get("timing_delta_samples")
                                             for item in clean_matches],
                    "decoded_value_agrees": [item.get("decoded_value_agrees")
                                              for item in clean_matches],
                }
                clean_spectrum_doc = phase4_documents["clean_spectrum"]
                clean_spectrum_summary = {
                    "availability": clean_spectrum_doc.get("availability"),
                    "bins_sha256": hashlib.sha256(json.dumps(
                        clean_spectrum_doc.get("bins", []), sort_keys=True,
                        separators=(",", ":")).encode()).hexdigest(),
                    "bin_count": len(clean_spectrum_doc.get("bins", [])),
                }
                clean_expected_count = len(expected_doc.get("pulses", []))
                clean_observed_count = len(observed_doc.get("observed_pulses", []))
                clean_message_result = observed_doc.get("receiver_message_result", {})
                receiver_indices = receiver_channelizer.get(
                    "node_config", {}).get("receiver_frequency_indices", list(range(64)))
                receiver_channel_ids = receiver_channelizer.get(
                    "node_config", {}).get("channel_ids", receiver_indices)
                physical_by_logical = {
                    int(logical): int(physical)
                    for logical, physical in zip(receiver_indices,
                                                 receiver_channel_ids)
                }
                independent_oracle_pulses = []
                for message in generator_schedule.get("messages", []):
                    message_start = int(message["transmit_start_sample"])
                    for pulse_index, pulse in enumerate(message.get("pulses", [])):
                        word = int(pulse["value"])
                        logical = int(pulse["frequency_index"])
                        bits = [(word >> shift) & 1 for shift in range(31, -1, -1)]
                        independent_oracle_pulses.append({
                            "global_start_sample": message_start + pulse_index * 6_500,
                            "duration_samples": 3_200,
                            "logical_frequency_index": logical,
                            "physical_channel_index": physical_by_logical[logical],
                            "decoded_value": word,
                            "cpsm_bits_msb_first": bits,
                            "role": pulse["role"],
                        })

                def agrees_with_independent_oracle(
                        expected_values: list[dict[str, object]],
                        observed_values: list[dict[str, object]],
                        oracle_values: list[dict[str, object]]) -> bool:
                    if len(expected_values) != len(oracle_values) or len(
                            observed_values) != len(oracle_values):
                        return False
                    for expected_pulse, observed_pulse, oracle_pulse in zip(
                            expected_values, observed_values, oracle_values):
                        for field in ("global_start_sample", "duration_samples",
                                      "logical_frequency_index"):
                            if int(expected_pulse[field]) != int(oracle_pulse[field]):
                                return False
                        if int(expected_pulse["transmitted_word"]) != int(
                                oracle_pulse["decoded_value"]):
                            return False
                        for field in ("global_start_sample", "duration_samples",
                                      "logical_frequency_index",
                                      "physical_channel_index", "decoded_value"):
                            if int(observed_pulse[field]) != int(oracle_pulse[field]):
                                return False
                    return True

                independent_oracle = {
                    "schema": "graphx.dashboard.phase4.independent_oracle.v1",
                    "timing_rule": "start + pulse_index * (3200 + 3300)",
                    "bit_order": "CPSM MSB-first: +1=0, -1=1",
                    "pulses": independent_oracle_pulses,
                }
                independent_oracle_path = output / "phase4-clean-independent-oracle.json"
                independent_oracle_path.write_text(
                    json.dumps(independent_oracle, indent=2) + "\n", encoding="utf-8")
                persist("phase4_clean_independent_oracle",
                        independent_oracle_path.read_bytes())
                independent_agreement = agrees_with_independent_oracle(
                    expected_doc.get("pulses", []),
                    observed_doc.get("observed_pulses", []),
                    independent_oracle_pulses)
                perturbed_oracle = copy.deepcopy(independent_oracle_pulses)
                perturbed_oracle[0]["decoded_value"] ^= 1
                perturbation_rejected = not agrees_with_independent_oracle(
                    expected_doc.get("pulses", []),
                    observed_doc.get("observed_pulses", []), perturbed_oracle)
                clean_complete = (
                    clean_expected_count > 0 and clean_observed_count == clean_expected_count and
                    independent_agreement and perturbation_rejected and
                    len(clean_matches) == clean_expected_count and
                    clean_comparison_summary["missed_count"] == 0 and
                    clean_comparison_summary["unexpected_count"] == 0 and
                    clean_comparison_summary["ambiguous_count"] == 0 and
                    all(clean_comparison_summary["decoded_value_agrees"]) and
                    observed_doc.get("preamble", {}).get("locked") is True and
                    observed_doc.get("assembler", {}).get("availability", {}).get("state") == "available" and
                    clean_message_result.get("accepted") is True and
                    int(clean_message_result.get("decoded_pulse_count", 0)) == clean_expected_count and
                    clean_spectrum_doc.get("availability", {}).get("state") == "available" and
                    clean_spectrum_channel is not None and
                    max((float(item.get("magnitude_linear_re_1_complex_unit", 0.0))
                         for item in clean_spectrum_doc.get("bins", [])),
                        default=0.0) > 0.0)
                check("Phase4 clean independent golden agreement", clean_complete,
                      f"expected={clean_expected_count}; observed={clean_observed_count}; "
                      f"matched={len(clean_matches)}; independent={independent_agreement}; "
                      f"perturbation_rejected={perturbation_rejected}; "
                      f"decoded={all(clean_comparison_summary['decoded_value_agrees'])}")
                clean_baseline_path = output / "phase4-clean-measured-baseline.json"
                clean_baseline_path.write_text(json.dumps({
                    "schema": "graphx.dashboard.phase4.clean_measured_baseline.v1",
                    "expected_pulse_count": clean_expected_count,
                    "observed_pulse_count": clean_observed_count,
                    "comparison": clean_comparison_summary,
                    "spectrum": clean_spectrum_summary,
                }, indent=2) + "\n", encoding="utf-8")
                persist("phase4_clean_measured_baseline", clean_baseline_path.read_bytes())
                screenshot_manifest = {
                    "schema": "graphx.dashboard.phase4.screenshot_manifest.v1",
                    "dashboard_url": url,
                    "capture_required": True,
                    "synthetic_data_only": True,
                    "instructions": [
                        "Open the dashboard URL in a browser.",
                        "Capture the Expected / Observed Evaluation and Receiver Sample Spectrum panels.",
                        "Keep expected and observed toggles enabled, then capture each independently.",
                    ],
                    "required_cases": ["clean", "impaired", "negative"],
                    "captured_files": {},
                    "reason_not_automated": "Phase 4 CI has no supported browser binary; manual capture is explicit and is not fabricated.",
                }
                screenshot_bytes = (json.dumps(screenshot_manifest, indent=2) + "\n").encode()
                (output / "phase4-screenshot-manifest.json").write_bytes(screenshot_bytes)
                persist("phase4_screenshot_manifest", screenshot_bytes)

            long_schedule_path, long_iq_path = output / "long-schedule.json", output / "long-replay.cf32"
            long_truth_path, long_sigmf_path = output / "long-truth.json", output / "long-replay.sigmf-meta"
            long_schedule = copy.deepcopy(generator_schedule)
            base_message = long_schedule["messages"][0]
            stride = (len(base_message["pulses"]) + 1) * 6500
            long_schedule["messages"] = []
            # At 500 Msps this is more than 9 million complex samples (over
            # 72 MB of cf32_le). The receiver selects a bounded 4,194,304
            # sample window; the margin is enough to observe traffic and exercise
            # cooperative Stop instead of racing a four-message fixture to EOF.
            for index in range(80):
                message = copy.deepcopy(base_message)
                message["message_id"] = 1000 + index
                message["transmit_start_sample"] = index * stride
                long_schedule["messages"].append(message)
            long_schedule_path.write_text(json.dumps(long_schedule, indent=2))
            long_command = [str(locate_generator(args.build_dir.resolve())), "--message-json", str(long_schedule_path),
                            "--iq-output", str(long_iq_path), "--truth-output", str(long_truth_path),
                            "--sigmf-meta", str(long_sigmf_path), "--force"]
            long_generated = subprocess.run(
                long_command, text=True, capture_output=True,
                timeout=GENERATOR_TIMEOUT_SECONDS)
            long_bytes = long_iq_path.stat().st_size if long_iq_path.exists() else 0
            long_samples = long_bytes // 8
            check("long deterministic IQ generation", long_generated.returncode == 0 and
                  long_bytes >= 72_000_000 and long_samples >= 9_000_000,
                  f"exit={long_generated.returncode}; bytes={long_bytes}; samples={long_samples}")
            for artifact_name, artifact_path in (("long_generated_iq", long_iq_path),
                                                  ("long_generated_truth", long_truth_path),
                                                  ("long_generated_schedule", long_schedule_path),
                                                  ("long_generated_sigmf", long_sigmf_path)):
                if artifact_path.exists(): persist(artifact_name, artifact_path.read_bytes())
            long_truth_path.unlink(); long_schedule_path.unlink()
            _, long_headers, _ = request(port,"GET","/api/v1/fhss/config/authoritative")
            long_patch = json.dumps([{"op":"replace","path":"/receiver_input","value":{
                "file_path":str(long_iq_path),"sample_format":"cf32_le",
                "first_complex_sample":0,"max_complex_samples":4_194_304,
                "max_read_complex_samples":4_194_304}}]).encode()
            lp_status, _, lp_body = request(port,"PATCH","/api/v1/fhss/config",long_patch,
                {"Content-Type":"application/json-patch+json","If-Match":long_headers["etag"]})
            persist("long_generation_patch", lp_body)
            long_revision = json.loads(lp_body).get("new_revision",0)
            lr_status,_,lr_body=request(port,"POST","/api/v1/fhss/config/rebuild",
                json.dumps({"expected_revision":long_revision,"command_id":"phase3-long-rebuild"}).encode(),
                {"Content-Type":"application/json"},timeout=20)
            persist("long_generation_rebuild",lr_body)
            ls_status,_,ls_body=request(port,"POST","/api/v1/fhss/commands/start",
                json.dumps({"command_id":"phase3-long-start"}).encode(),{"Content-Type":"application/json"})
            persist("long_generation_start",ls_body)
            running_seen = traffic_seen = False; running_doc = {}; running_metrics = {}
            milestone_deadline=time.monotonic()+5
            while time.monotonic()<milestone_deadline:
                _,_,running_body=request(port,"GET","/api/v1/fhss/status"); running_doc=json.loads(running_body)
                _,_,running_metrics_body=request(port,"GET","/api/v1/fhss/metrics"); running_metrics=json.loads(running_metrics_body)
                current_running = running_doc.get("lifecycle_state")=="running"
                current_traffic = sum(int(edge.get("messages_enqueued",0))+int(edge.get("messages_dequeued",0)) for edge in running_metrics.get("edges",[]))>0
                if current_running and current_traffic:
                    running_seen = traffic_seen = True
                    break
                time.sleep(0.02)
            persist("long_generation_running_status",json.dumps(running_doc,sort_keys=True).encode())
            persist("long_generation_running_metrics",json.dumps(running_metrics,sort_keys=True).encode())
            long_stop_started=time.monotonic()
            lx_status,_,lx_body=request(port,"POST","/api/v1/fhss/commands/stop",
                json.dumps({"command_id":"phase3-long-stop"}).encode(),{"Content-Type":"application/json"},timeout=6)
            long_stop_elapsed=time.monotonic()-long_stop_started
            persist("long_generation_stop",lx_body)
            _,_,long_terminal_body=request(port,"GET","/api/v1/fhss/status"); long_terminal=json.loads(long_terminal_body)
            persist("long_generation_terminal_status",long_terminal_body)
            check("long replay running traffic before stop", lp_status==200 and lr_status==200 and ls_status==202 and running_seen and traffic_seen,
                  f"patch={lp_status}; rebuild={lr_status}; start={ls_status}; running={running_seen}; traffic={traffic_seen}")
            check("bounded real cancellation join", lx_status==200 and long_stop_elapsed<5 and
                  long_terminal.get("lifecycle_state")=="stopped" and
                  long_terminal.get("stop_requested") is True and
                  (long_terminal.get("terminal_result") or {}).get("generation")==generation_base+3 and
                  (long_terminal.get("terminal_result") or {}).get("code")=="execution_cancelled",
                  f"HTTP {lx_status}; seconds={long_stop_elapsed:.3f}; status={json.dumps(long_terminal,sort_keys=True)}")
            check("long truth isolated before replay", not long_truth_path.exists() and not long_schedule_path.exists(), "long truth and schedule deleted")
            if phase >= 4:
                def set_observation_export(enabled: bool, command_suffix: str) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
                    _, export_headers, _ = request(
                        port, "GET", "/api/v1/fhss/config/authoritative")
                    export_patch = json.dumps([{
                        "op": "replace",
                        "path": "/receiver_input",
                        "value": {
                            "file_path": str(iq_path),
                            "sample_format": "cf32_le",
                            "first_complex_sample": 0,
                            "max_complex_samples": 0,
                            "max_read_complex_samples": 4_194_304,
                            "dashboard_observation_enabled": enabled,
                        },
                    }]).encode()
                    export_patch_status, _, export_patch_body = request(
                        port, "PATCH", "/api/v1/fhss/config", export_patch,
                        {"Content-Type": "application/json-patch+json",
                         "If-Match": export_headers["etag"]})
                    export_revision = json.loads(export_patch_body).get(
                        "new_revision", 0)
                    export_rebuild_status, _, export_rebuild_body = request(
                        port, "POST", "/api/v1/fhss/config/rebuild",
                        json.dumps({
                            "expected_revision": export_revision,
                            "command_id": f"phase4-observation-{command_suffix}-rebuild",
                        }).encode(), {"Content-Type": "application/json"}, timeout=20)
                    persist(f"observation_export_{command_suffix}_rebuild",
                            export_rebuild_body)
                    export_start_status = 0
                    if export_rebuild_status == 200:
                        export_start_status, _, export_start_body = request(
                            port, "POST", "/api/v1/fhss/commands/start",
                            json.dumps({
                                "command_id":
                                    f"phase4-observation-{command_suffix}-start",
                            }).encode(), {"Content-Type": "application/json"})
                        persist(f"observation_export_{command_suffix}_start",
                                export_start_body)
                    export_terminal: dict[str, object] = {}
                    export_deadline = time.monotonic() + 20
                    while time.monotonic() < export_deadline:
                        _, _, export_status_body = request(
                            port, "GET", "/api/v1/fhss/status")
                        export_terminal = json.loads(export_status_body)
                        if export_terminal.get("lifecycle_state") in (
                                "completed", "failed"):
                            break
                        time.sleep(0.05)
                    _, _, export_observation_body = request(
                        port, "GET", "/api/v1/fhss/observations")
                    _, _, export_comparison_body = request(
                        port, "GET", "/api/v1/fhss/comparison")
                    export_observation = json.loads(export_observation_body)
                    export_comparison = json.loads(export_comparison_body)
                    persist(f"observation_export_{command_suffix}_observation",
                            export_observation_body)
                    persist(f"observation_export_{command_suffix}_comparison",
                            export_comparison_body)
                    check(f"observation export {command_suffix} lifecycle",
                          export_patch_status == 200 and
                          export_rebuild_status == 200 and
                          export_start_status == 202 and
                          export_terminal.get("lifecycle_state") == "completed",
                          f"patch={export_patch_status}; rebuild={export_rebuild_status}; "
                          f"start={export_start_status}; terminal={export_terminal.get('lifecycle_state')}")
                    return export_observation, export_comparison, export_terminal

                disabled_observation, disabled_comparison, _ = (
                    set_observation_export(False, "disabled"))
                _, _, disabled_spectrum_body = request(
                    port, "GET", "/api/v1/fhss/spectrum?fft_size=128")
                disabled_spectrum = json.loads(disabled_spectrum_body)
                persist("observation_export_disabled_spectrum",
                        disabled_spectrum_body)
                check("Phase4 live receiver observation removal is truthful",
                      disabled_observation.get("availability", {}).get("reason") ==
                          "observation_export_disabled" and
                      disabled_observation.get("observed_pulses") == [] and
                      disabled_observation.get("detected_count") is None and
                      disabled_observation.get("rejected_count") is None and
                      disabled_observation.get("sources") == [] and
                      disabled_comparison.get("evaluation_state") == "indeterminate" and
                      disabled_comparison.get("availability", {}).get("reason") ==
                          "receiver_observation_unavailable" and
                      disabled_spectrum.get("availability", {}).get("reason") ==
                          "observation_export_disabled" and
                      disabled_spectrum.get("bins") == [],
                      "public production config disabled typed receiver observation export")
                restored_observation, _, _ = set_observation_export(
                    True, "restored")
                check("Phase4 receiver observation export restores safely",
                      restored_observation.get("availability", {}).get("state") ==
                          "available" and
                      len(restored_observation.get("observed_pulses", [])) > 0,
                      json.dumps(restored_observation.get("availability", {}),
                                 sort_keys=True))

                impaired_path = output / "impaired-cfo-awgn.cf32"
                impaired_scenario_path = output / "impaired-scenario.json"
                impaired_truth_path = output / "impaired-truth.json"
                impairment_metadata_path = output / "impaired-metadata.json"
                negative_path = output / "negative-no-message.cf32"
                malformed_path = output / "malformed-iq.cf32"
                impairment_seed = 404
                impaired_messages = []
                for source_message in generator_schedule["messages"]:
                    impaired_messages.append({
                        "message_id": int(source_message["message_id"]),
                        "transmitter_id": 1,
                        "transmit_start_sample": int(source_message["transmit_start_sample"]),
                        "initial_phase_rad": 0.0,
                        "pulses": [{
                            "frequency_index": int(pulse["frequency_index"]),
                            "word": int(pulse["value"]),
                            "role": pulse["role"],
                        } for pulse in source_message["pulses"]],
                    })
                impaired_scenario = {
                    "schema": "graphx.fhss.phase3-scenario.v2",
                    "scenario_id": "dashboard-phase4-cfo-awgn",
                    "seed": impairment_seed,
                    "sample_format": "cf32_le",
                    "active_frequency_indices": independently_active,
                    "allow_overlap": False,
                    "tail_samples": 6_500,
                    "messages": impaired_messages,
                    "channel": {
                        "carrier": {"cfo_hz": 750.0, "doppler_hz": 0.0,
                                    "initial_phase_rad": 0.0,
                                    "phase_noise_step_std_rad": 0.0},
                        "awgn": {"eb_n0_db": 18.0},
                    },
                }
                impaired_scenario_path.write_text(
                    json.dumps(impaired_scenario, indent=2) + "\n", encoding="utf-8")
                harness = locate_independent_harness()
                impairment_command = [sys.executable, str(harness), "generate",
                                      "--scenario", str(impaired_scenario_path),
                                      "--iq", str(impaired_path),
                                      "--truth", str(impaired_truth_path)]
                impairment_run = subprocess.run(impairment_command, text=True,
                                                 capture_output=True, timeout=60)
                check("Phase4 independent impairment harness",
                      impairment_run.returncode == 0 and impaired_path.is_file() and
                      impaired_truth_path.is_file(),
                      f"exit={impairment_run.returncode}; harness={harness}")
                impairment_metadata = {
                    "schema": "graphx.dashboard.phase4.impairment_metadata.v1",
                    "generator": "examples/DSP/tools/fhss_phase3_independent.py",
                    "generator_sha256": sha256(harness),
                    "seed": impairment_seed,
                    "impairment_order": [
                        "fractional_timing_and_sample_clock", "multipath_and_fading",
                        "doppler_cfo_and_phase", "blockers_and_collisions",
                        "iq_imbalance_and_dc", "agc", "awgn", "clipping", "quantization"],
                    "reference_power": "independent harness measured clean waveform mean power",
                    "cfo_hz": 750.0,
                    "eb_n0_db": 18.0,
                    "noise_model": "seeded complex AWGN derived from Eb/N0 and measured clean reference power",
                    "synthetic_data_only": True,
                    "hwil_available": False,
                    "command": impairment_command,
                }
                impairment_metadata_path.write_text(
                    json.dumps(impairment_metadata, indent=2) + "\n", encoding="utf-8")
                write_zero_cf32(negative_path, iq_path.stat().st_size)
                malformed_path.write_bytes(b"malformed")
                persist("impaired_synthetic_iq", impaired_path.read_bytes())
                persist("impaired_synthetic_truth", impaired_truth_path.read_bytes())
                persist("impaired_synthetic_scenario", impaired_scenario_path.read_bytes())
                persist("impaired_metadata", impairment_metadata_path.read_bytes())
                persist("negative_synthetic_iq", negative_path.read_bytes())
                persist("malformed_synthetic_iq", malformed_path.read_bytes())
                impaired_truth_path.unlink()
                impaired_scenario_path.unlink()
                check("Phase4 impaired truth blocked before receiver replay",
                      not impaired_truth_path.exists() and
                      not impaired_scenario_path.exists(),
                      "live truth and schedule paths unlinked; evaluator copies remain hashed")

                def replay_observation_case(label: str, input_path: Path,
                                            expected_terminal: str) -> tuple[dict[str, object], dict[str, object], dict[str, object], dict[str, object]]:
                    _, case_headers, _ = request(port, "GET", "/api/v1/fhss/config/authoritative")
                    case_patch = json.dumps([{"op": "replace", "path": "/receiver_input", "value": {
                        "file_path": str(input_path), "sample_format": "cf32_le",
                        "first_complex_sample": 0, "max_complex_samples": 0,
                        "max_read_complex_samples": 4_194_304}}]).encode()
                    patch_status, _, patch_body = request(
                        port, "PATCH", "/api/v1/fhss/config", case_patch,
                        {"Content-Type": "application/json-patch+json", "If-Match": case_headers["etag"]})
                    persist(f"{label}_patch", patch_body)
                    _, _, case_body = request(
                        port, "GET", "/api/v1/fhss/config/authoritative")
                    case_authoritative = json.loads(case_body)
                    _, _, effective_body = request(
                        port, "GET", "/api/v1/fhss/config/effective")
                    effective_graph = json.loads(effective_body)["effective"]
                    case_scenario = dict(case_authoritative["scenario"])
                    case_scenario["receiver_input"] = receiver_input_from_effective(
                        effective_graph)
                    case_document = {"fhss": {"scenario": case_scenario}}
                    (cases_dir / f"{label}-config.json").write_text(
                        json.dumps(case_document, indent=2) + "\n", encoding="utf-8")
                    persist(f"phase4_{label}_reload_config",
                            (cases_dir / f"{label}-config.json").read_bytes())
                    revision_value = json.loads(patch_body).get("new_revision", 0)
                    rebuild_status, _, rebuild_body = request(
                        port, "POST", "/api/v1/fhss/config/rebuild",
                        json.dumps({"expected_revision": revision_value,
                                    "command_id": f"phase4-{label}-rebuild"}).encode(),
                        {"Content-Type": "application/json"}, timeout=20)
                    persist(f"{label}_rebuild", rebuild_body)
                    start_status, _, start_body = request(
                        port, "POST", "/api/v1/fhss/commands/start",
                        json.dumps({"command_id": f"phase4-{label}-start"}).encode(),
                        {"Content-Type": "application/json"})
                    persist(f"{label}_start", start_body)
                    terminal = {}
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        _, _, terminal_body = request(port, "GET", "/api/v1/fhss/status")
                        terminal = json.loads(terminal_body)
                        if terminal.get("lifecycle_state") in ("completed", "failed"):
                            break
                        time.sleep(0.05)
                    if terminal.get("lifecycle_state") in ("starting", "running"):
                        _, _, cleanup_body = request(
                            port, "POST", "/api/v1/fhss/commands/stop",
                            json.dumps({"command_id":
                                        f"phase4-{label}-timeout-cleanup"}).encode(),
                            {"Content-Type": "application/json"})
                        persist(f"{label}_timeout_cleanup", cleanup_body)
                        _, _, terminal_body = request(
                            port, "GET", "/api/v1/fhss/status")
                        terminal = json.loads(terminal_body)
                    observation_status, _, observation_body = request(
                        port, "GET", "/api/v1/fhss/observations")
                    comparison_status, _, comparison_body = request(
                        port, "GET", "/api/v1/fhss/comparison")
                    observation_ok, observation = schema_valid(
                        "fhss-receiver-observation", observation_body)
                    receiver_pulses = observation.get("observed_pulses", [])
                    spectrum_channel = (int(receiver_pulses[0]["physical_channel_index"])
                                        if receiver_pulses else None)
                    spectrum_target = (
                        f"/api/v1/fhss/spectrum?channel={spectrum_channel}&fft_size=128"
                        if spectrum_channel is not None else
                        "/api/v1/fhss/spectrum?fft_size=128")
                    spectrum_status, _, spectrum_body = request(
                        port, "GET", spectrum_target)
                    persist(f"{label}_terminal", json.dumps(terminal, sort_keys=True).encode())
                    persist(f"{label}_observation", observation_body)
                    persist(f"{label}_comparison", comparison_body)
                    persist(f"{label}_spectrum", spectrum_body)
                    comparison_ok, comparison = schema_valid("fhss-comparison-result", comparison_body)
                    spectrum_ok, spectrum = schema_valid("fhss-receiver-spectrum", spectrum_body)
                    check(f"Phase4 {label} replay and contracts",
                          patch_status == 200 and rebuild_status == 200 and start_status == 202 and
                          observation_status == comparison_status == spectrum_status == 200 and
                          observation_ok and comparison_ok and spectrum_ok and
                          spectrum.get("channel_index") == spectrum_channel and
                          terminal.get("lifecycle_state") == expected_terminal,
                          f"patch={patch_status}; rebuild={rebuild_status}; start={start_status}; terminal={terminal.get('lifecycle_state')}")
                    return terminal, observation, comparison, spectrum

                impaired_terminal, impaired_observation, impaired_comparison, impaired_spectrum = replay_observation_case(
                    "impaired", impaired_path, "completed")
                negative_terminal, negative_observation, negative_comparison, negative_spectrum = replay_observation_case(
                    "negative", negative_path, "completed")
                malformed_terminal, malformed_observation, malformed_comparison, _ = replay_observation_case(
                    "malformed", malformed_path, "failed")
                impaired_comparison_summary = {
                    "availability": impaired_comparison.get("availability"),
                    "matched_count": len(impaired_comparison.get("matches", [])),
                    "missed_count": len(impaired_comparison.get("missed_expected_indices", [])),
                    "unexpected_count": len(impaired_comparison.get("unexpected_observed_indices", [])),
                    "ambiguous_count": len(impaired_comparison.get("ambiguous", [])),
                    "timing_delta_samples": [item.get("timing_delta_samples")
                                             for item in impaired_comparison.get("matches", [])],
                    "decoded_value_agrees": [item.get("decoded_value_agrees")
                                              for item in impaired_comparison.get("matches", [])],
                }
                impaired_bins_hash = hashlib.sha256(json.dumps(
                    impaired_spectrum.get("bins", []), sort_keys=True,
                    separators=(",", ":")).encode()).hexdigest()
                check("Phase4 impaired deterministic measured delta",
                      impaired_spectrum.get("availability", {}).get("state") == "available" and
                      (impaired_comparison_summary != clean_comparison_summary or
                       impaired_bins_hash != clean_spectrum_summary["bins_sha256"]),
                      f"comparison_changed={impaired_comparison_summary != clean_comparison_summary}; "
                      f"receiver_spectrum_changed={impaired_bins_hash != clean_spectrum_summary['bins_sha256']}")
                check("Phase4 impaired software provenance",
                      impaired_terminal.get("lifecycle_state") == "completed" and
                      impaired_observation.get("semantic_class") == "observed",
                      "deterministic 750 Hz CFO plus seeded AWGN; no HWIL")
                check("Phase4 negative has no fabricated pulse",
                      negative_terminal.get("lifecycle_state") == "completed" and
                      negative_observation.get("observed_pulses") == [] and
                      int(negative_observation.get("detected_count", 0)) == 0 and
                      negative_observation.get("preamble", {}).get("locked") is False and
                      negative_observation.get("assembler", {}).get("availability", {}).get("state") == "available" and
                      negative_observation.get("assembler", {}).get("status") != "Ok" and
                      negative_observation.get("receiver_message_result", {}).get("accepted") is False and
                      int(negative_observation.get("receiver_message_result", {}).get(
                          "decoded_pulse_count", -1)) == 0 and
                      negative_spectrum.get("channel_index") is None and
                      negative_spectrum.get("availability", {}).get("state") == "unavailable" and
                      negative_spectrum.get("availability", {}).get("reason") == "no_candidate_detected" and
                      max((float(item.get("magnitude_linear_re_1_complex_unit", 0.0))
                           for item in negative_spectrum.get("bins", [])),
                          default=0.0) == 0.0,
                      json.dumps(negative_observation.get("availability", {}), sort_keys=True))
                check("Phase4 negative receiver-message result is distinct from lifecycle",
                      negative_comparison.get("evaluation_state") == "evaluated" and
                      negative_comparison.get("terminal_result_agrees") is False and
                      negative_terminal.get("lifecycle_state") == "completed" and
                      (negative_terminal.get("terminal_result") or {}).get("code") ==
                          "execution_completed",
                      json.dumps({
                          "comparison_state": negative_comparison.get("evaluation_state"),
                          "terminal_result_agrees": negative_comparison.get(
                              "terminal_result_agrees"),
                          "lifecycle": negative_terminal.get("lifecycle_state"),
                      }, sort_keys=True))
                check("Phase4 malformed IQ fails truthfully",
                      malformed_terminal.get("lifecycle_state") == "failed" and
                      malformed_observation.get("observed_pulses") == [] and
                      malformed_comparison.get("evaluation_state") == "indeterminate" and
                      malformed_comparison.get("availability", {}).get("reason") ==
                          "receiver_execution_not_completed" and
                      malformed_comparison.get("matches") == [] and
                      malformed_comparison.get("missed_expected_indices") == [] and
                      malformed_comparison.get("unexpected_observed_indices") == [] and
                      malformed_comparison.get("ambiguous") == [],
                      json.dumps(malformed_terminal, sort_keys=True))
            _, _, before_invalid_status_body = request(port, "GET", "/api/v1/fhss/status")
            before_invalid_generation = json.loads(before_invalid_status_body).get("active_generation")
            _, invalid_headers, _ = request(port,"GET","/api/v1/fhss/config/authoritative")
            invalid_patch = json.dumps([{"op":"replace","path":"/receiver_input/sample_format","value":"unsupported"}]).encode()
            i_status, _, i_body = request(port,"PATCH","/api/v1/fhss/config",invalid_patch,
                {"Content-Type":"application/json-patch+json","If-Match":invalid_headers["etag"]})
            invalid_revision = json.loads(i_body).get("new_revision",0) if i_body else 0
            bad_status, bad_headers, bad_body = request(port,"POST","/api/v1/fhss/config/rebuild",
                json.dumps({"expected_revision":invalid_revision,"command_id":"phase3-invalid"}).encode(),
                {"Content-Type":"application/json"}, timeout=20)
            status_status, _, status_body = request(port,"GET","/api/v1/fhss/status")
            status_doc = json.loads(status_body)
            persist("invalid_rebuild_error",bad_body)
            persist("invalid_rebuild_status",status_body)
            try:
                invalid_problem_ok, _ = schema_valid("problem", bad_body)
            except Exception:
                invalid_problem_ok = False
            check("invalid rebuild problem contract", invalid_problem_ok and
                  bad_headers.get("content-type", "").startswith("application/problem+json"),
                  f"HTTP {bad_status}; schema={invalid_problem_ok}")
            check("invalid rebuild preserves active generation", i_status==200 and bad_status>=400 and status_status==200 and
                  status_doc.get("active_generation")==before_invalid_generation,
                  f"patch={i_status}; rebuild={bad_status}; generation={status_doc.get('active_generation')}")
        source_root = Path(__file__).resolve().parents[4]
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source_root,
                                  text=True, capture_output=True, check=False).stdout.strip() or "unknown"
        compiler = subprocess.run(["c++", "--version"], text=True, capture_output=True,
                                  check=False).stdout.splitlines()
        schema_hashes = {f"schema:{path.name}": sha256(path)
                         for path in sorted((API_DIR / "schemas").glob("*.json"))}
        checks_pass = all(item["pass"] for item in checks)
        evidence_status = "partial_pre_browser" if phase >= 4 else "complete"
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": phase,
            "source_revision": revision, "compiler": compiler[0] if compiler else "unknown",
            "build_profile": args.build_dir.name, "platform": platform.platform(),
            "commands": [command], "bound_address": "127.0.0.1", "bound_port": port,
            "dashboard_url": url, "api_version": "v1", "synthetic_data_only": True,
            "hwil_available": False, "production_rf_qualified": False,
            "input_hashes": {"openapi": sha256(Path(__file__).resolve().parents[1] / "api/openapi.json"),
                             "operator": sha256(Path(__file__).resolve()),
                             "operator_report_schema": sha256(Path(__file__).resolve().parent / "schemas/operator-report.schema.json"),
                             **schema_hashes},
            "artifact_hashes": {"dashboard_index": sha256(Path(__file__).resolve().parents[1] / "index.html"),
                                **live_hashes,
                                **phase2_hashes},
            "checks": checks,
            "evidence_status": evidence_status,
            "result": ("PARTIAL" if phase >= 4 else "PASS")
                      if checks_pass else "FAIL"
        }
        report_path = output / f"phase{phase}-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 0 if report["result"] in ("PASS", "PARTIAL") else 1
    except Exception as error:
        checks.append({"name": "operator execution", "pass": False, "evidence": str(error)})
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": phase,
            "source_revision": "unknown", "compiler": "unknown",
            "build_profile": args.build_dir.name, "platform": platform.platform(),
            "commands": [command] if command else [], "bound_address": "127.0.0.1",
            "bound_port": port or 1, "dashboard_url": url, "api_version": "v1",
            "synthetic_data_only": True, "hwil_available": False,
            "production_rf_qualified": False,
            "input_hashes": {"openapi": sha256(API_DIR / "openapi.json")},
            "artifact_hashes": {"dashboard_index": sha256(Path(__file__).resolve().parents[1] / "index.html")},
            "checks": checks,
            "evidence_status": "partial_pre_browser" if phase >= 4 else "complete",
            "result": "FAIL"
        }
        report_path = output / f"phase{phase}-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 1
    finally:
        if process is not None:
            stop(process)


def verify(args: argparse.Namespace) -> int:
    report_path = args.output_dir.resolve() / f"phase{args.phase}-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    schema = json.loads((Path(__file__).resolve().parent / "schemas/operator-report.schema.json").read_text())
    try:
        validate_schema(schema, "operator-report.schema.json")
        validate_instance(report, schema)
        dashboard_index = Path(__file__).resolve().parents[1] / "index.html"
        openapi = Path(__file__).resolve().parents[1] / "api/openapi.json"
        hashes_valid = (report.get("artifact_hashes", {}).get("dashboard_index") == sha256(dashboard_index)
                        and report.get("input_hashes", {}).get("openapi") == sha256(openapi)
                        and report.get("input_hashes", {}).get("operator") == sha256(Path(__file__).resolve())
                        and report.get("input_hashes", {}).get("operator_report_schema") ==
                            sha256(Path(__file__).resolve().parent / "schemas/operator-report.schema.json"))
        for path in sorted((API_DIR / "schemas").glob("*.json")):
            hashes_valid = hashes_valid and (
                report.get("input_hashes", {}).get(f"schema:{path.name}") == sha256(path))
        evidence_dir = args.output_dir.resolve() / "artifacts"
        for key, digest in report.get("artifact_hashes", {}).items():
            if key == "dashboard_index":
                continue
            if key == f"phase{args.phase}_screenshot_manifest_current":
                manifest_path = (args.output_dir.resolve() /
                                 f"phase{args.phase}-screenshot-manifest.json")
                hashes_valid = (hashes_valid and manifest_path.is_file() and
                                sha256(manifest_path) == digest)
                continue
            if key.startswith(f"phase{args.phase}_screenshot:"):
                case = key.split(":", 1)[1]
                screenshot = args.output_dir.resolve() / "screenshots" / f"{case}.png"
                hashes_valid = (hashes_valid and is_valid_png(screenshot) and
                                sha256(screenshot) == digest)
                continue
            if key.startswith(f"phase{args.phase}_served_state:"):
                case = key.split(":", 1)[1]
                served_state = args.output_dir.resolve() / f"phase{args.phase}-{case}-served-state.json"
                hashes_valid = (hashes_valid and served_state.is_file() and
                                sha256(served_state) == digest)
                continue
            evidence = evidence_dir / (hashlib.sha256(key.encode()).hexdigest() + ".bin")
            hashes_valid = hashes_valid and evidence.is_file() and sha256(evidence) == digest
    except ValueError:
        hashes_valid = False
    require_screenshots = bool(getattr(args, "require_screenshots", False))
    screenshots_valid = True
    if require_screenshots and args.phase >= 4:
        try:
            manifest = json.loads((args.output_dir.resolve() /
                                   f"phase{args.phase}-screenshot-manifest.json").read_text(encoding="utf-8"))
            captures = manifest.get("captured_files", {})
            required_cases = ({"clean", "impaired", "negative"}
                              if args.phase == 4 else
                              {"step", "continue", "cancelled"})
            screenshots_valid = set(captures) == required_cases
            for case, record in captures.items():
                screenshot = args.output_dir.resolve() / record["relative_path"]
                state_path = args.output_dir.resolve() / record["served_state"]
                state = json.loads(state_path.read_text(encoding="utf-8"))
                captured_at = datetime.fromisoformat(
                    str(record["captured_at_utc"]).replace("Z", "+00:00"))
                state_generation = int(state.get(
                    "generation", state.get("terminal", {}).get(
                        "active_generation", -1)))
                capture_age_seconds = (captured_at.timestamp() -
                                       screenshot.stat().st_mtime)
                screenshots_valid = (screenshots_valid and is_valid_png(screenshot) and
                                     record.get("case") == case and
                                     sha256(screenshot) == record["sha256"] and
                                     state.get("case") == case and
                                     record.get("served_url") == state.get("url") and
                                     int(record.get("generation", -1)) == state_generation and
                                     int(record.get("run_epoch", -1)) == int(
                                         state.get("run_epoch", -2)) and
                                     record.get("observation_id") == state.get("observation_id") and
                                     record.get("observation_sha256") == state.get(
                                         "observation_sha256") and
                                     captured_at.tzinfo is not None and
                                     -5.0 <= capture_age_seconds <= 3600.0 and
                                     abs(float(record.get(
                                         "capture_age_seconds_at_record", -1.0)) -
                                         capture_age_seconds) <= 5.0 and
                                     int(record.get("screenshot_mtime_ns", -1)) ==
                                         screenshot.stat().st_mtime_ns and
                                     ((args.phase == 4 and
                                       state.get("truth_files_present") is False and
                                       len(str(state.get("config_sha256", ""))) == 64 and
                                       len(str(state.get("iq_sha256", ""))) == 64 and
                                       str(state.get("observation_id", "")).startswith("observation-g") and
                                       len(str(state.get("observation_sha256", ""))) == 64) or
                                      (args.phase == 5 and
                                       state.get("synthetic_data_only") is True and
                                       state.get("hwil_available") is False and
                                       len(str(state.get("job_id", ""))) == 26 and
                                       len(str(state.get("job_sha256", ""))) == 64 and
                                       (case == "cancelled" or
                                        state.get("truth_withheld_during_replay") is True))) and
                                     record.get("served_state_sha256") == sha256(state_path) and
                                     report.get("artifact_hashes", {}).get(
                                         f"phase{args.phase}_served_state:{case}") == sha256(state_path) and
                                     report.get("artifact_hashes", {}).get(
                                         f"phase{args.phase}_screenshot:{case}") == record["sha256"])
        except (OSError, KeyError, TypeError, ValueError,
                json.JSONDecodeError):
            screenshots_valid = False
    if args.phase >= 4 and require_screenshots:
        expected_states = {
            ("PARTIAL", "captures_complete_unverified"),
            ("PASS", "final_verified"),
        }
        displayed_result = "PASS"
    elif args.phase >= 4:
        expected_states = {("PARTIAL", "partial_pre_browser")}
        displayed_result = "PARTIAL"
    else:
        expected_states = {("PASS", "complete")}
        displayed_result = "PASS"
    valid = (report.get("phase") == args.phase and
             (report.get("result"), report.get("evidence_status")) in
                 expected_states and
             all(item.get("pass") is True for item in report.get("checks", [])) and
             hashes_valid and screenshots_valid)
    if (valid and args.phase >= 4 and require_screenshots and
            report.get("evidence_status") == "captures_complete_unverified"):
        report["result"] = "PASS"
        report["evidence_status"] = "final_verified"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(displayed_result if valid else "FAIL")
    return 0 if valid else 1


def record_screenshot(args: argparse.Namespace) -> int:
    source = args.path.resolve()
    if not source.is_file() or not is_valid_png(source):
        raise RuntimeError("screenshot must be an actual PNG file")
    captured_at = datetime.now(timezone.utc)
    capture_age_seconds = captured_at.timestamp() - source.stat().st_mtime
    if capture_age_seconds < -5.0 or capture_age_seconds > 3600.0:
        raise RuntimeError("screenshot capture timestamp is stale or in the future")
    output = args.output_dir.resolve()
    manifest_path = output / f"phase{args.phase}-screenshot-manifest.json"
    report_path = output / f"phase{args.phase}-report.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    state_path = output / f"phase{args.phase}-{args.case}-served-state.json"
    state = json.loads(state_path.read_text(encoding="utf-8"))
    generation = int(state.get(
        "generation", state.get("terminal", {}).get("active_generation", -1)))
    run_epoch = int(state.get("run_epoch", -1))
    common_valid = (state.get("case") == args.case and
                    str(state.get("url", "")).startswith("http://127.0.0.1:"))
    if args.phase == 4:
        case_config = output / "phase4-cases" / f"{args.case}-config.json"
        iq_path = Path(str(state.get("iq_path", "")))
        state_valid = (common_valid and
                       state.get("truth_files_present") is False and
                       generation >= 1 and run_epoch >= 1 and
                       case_config.is_file() and
                       sha256(case_config) == state.get("config_sha256") and
                       iq_path.is_file() and sha256(iq_path) == state.get("iq_sha256") and
                       str(state.get("observation_id", "")).startswith("observation-g") and
                       len(str(state.get("observation_sha256", ""))) == 64)
    else:
        state_valid = (common_valid and state.get("synthetic_data_only") is True and
                       state.get("hwil_available") is False and
                       re.fullmatch(r"j-[0-9a-f]{24}", str(state.get("job_id", ""))) is not None and
                       len(str(state.get("job_sha256", ""))) == 64 and
                       state.get("job_state") in ("completed", "cancelled") and
                       (args.case == "cancelled" or
                        (generation >= 1 and run_epoch >= 1 and
                         state.get("truth_withheld_during_replay") is True)))
    if not state_valid:
        raise RuntimeError("served-state evidence does not identify the requested case")
    screenshots = output / "screenshots"
    screenshots.mkdir(exist_ok=True)
    destination = screenshots / f"{args.case}.png"
    shutil.copy2(source, destination)
    digest = sha256(destination)
    state_digest = sha256(state_path)
    manifest.setdefault("captured_files", {})[args.case] = {
        "case": args.case,
        "relative_path": str(destination.relative_to(output)),
        "sha256": digest,
        "served_state": f"phase{args.phase}-{args.case}-served-state.json",
        "served_state_sha256": state_digest,
        "served_url": state["url"],
        "generation": generation,
        "run_epoch": run_epoch,
        "observation_id": state["observation_id"],
        "observation_sha256": state["observation_sha256"],
        "captured_at_utc": captured_at.isoformat(
            timespec="microseconds").replace("+00:00", "Z"),
        "screenshot_mtime_ns": destination.stat().st_mtime_ns,
        "capture_age_seconds_at_record": capture_age_seconds,
        "synthetic_data_only": True,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report.setdefault("artifact_hashes", {})[f"phase{args.phase}_screenshot:{args.case}"] = digest
    report["artifact_hashes"][f"phase{args.phase}_served_state:{args.case}"] = state_digest
    report["artifact_hashes"][f"phase{args.phase}_screenshot_manifest_current"] = sha256(
        manifest_path)
    captured_cases = set(manifest.get("captured_files", {}))
    required_cases = ({"clean", "impaired", "negative"}
                      if args.phase == 4 else
                      {"step", "continue", "cancelled"})
    final_capture_set = captured_cases == required_cases
    check_name = f"Phase{args.phase} bound browser screenshots"
    report["checks"] = [
        item for item in report.get("checks", [])
        if item.get("name") != check_name]
    report["checks"].append({
        "name": check_name,
        "pass": final_capture_set,
        "evidence": (", ".join(sorted(required_cases)) +
                     " browser captures bound"
                     if final_capture_set else
                     "interactive browser capture remains pending"),
    })
    report["evidence_status"] = (
        "captures_complete_unverified" if final_capture_set else
        "partial_pre_browser")
    report["result"] = "PARTIAL"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(destination)
    return 0


def cleanup(args: argparse.Namespace) -> int:
    output = args.output_dir.resolve()
    marker = output / OWNED_MARKER
    if not marker.is_file():
        raise RuntimeError("refusing to remove an unmarked directory")
    marker_lines = marker.read_text(encoding="utf-8").splitlines()
    created_dir = "created_dir=1" in marker_lines
    if f"phase={args.phase}" not in marker_lines:
        raise RuntimeError("refusing cleanup because marker phase does not match")
    exact_files = {
        f"phase{args.phase}-report.json", "phase4-screenshot-manifest.json",
        "phase4-clean-served-state.json", "phase4-impaired-served-state.json",
        "phase4-negative-served-state.json", "replay.cf32", "replay.sigmf-meta",
        "phase4-clean-measured-baseline.json",
        "phase4-clean-independent-oracle.json",
        "phase4-receiver-minimal.json",
        "schedule.json", "truth.json", "long-replay.cf32", "long-replay.sigmf-meta",
        "long-schedule.json", "long-truth.json", "impaired-cfo-awgn.cf32",
        "impaired-scenario.json", "impaired-truth.json", "impaired-metadata.json",
        "negative-no-message.cf32", "malformed-iq.cf32",
        "phase5-offline-summary.json", "phase5-screenshot-manifest.json",
        "phase5-step-served-state.json", "phase5-continue-served-state.json",
        "phase5-cancelled-served-state.json",
    }
    exact_files.update(f"phase4-cases/{case}-config.json"
                       for case in ("clean", "impaired", "negative", "malformed"))
    exact_files.update(f"screenshots/{case}.png"
                       for case in ("clean", "impaired", "negative",
                                    "step", "continue", "cancelled"))
    for relative in sorted(exact_files):
        tracked = output / relative
        if tracked.is_file() or tracked.is_symlink():
            tracked.unlink()
    evidence_dir = output / "artifacts"
    if evidence_dir.is_dir():
        for artifact in evidence_dir.iterdir():
            if artifact.is_file() and len(artifact.stem) == 64 and artifact.suffix == ".bin":
                artifact.unlink()
            else:
                raise RuntimeError(f"refusing unowned evidence entry: {artifact}")
        evidence_dir.rmdir()
    for directory in (output / "screenshots", output / "phase4-cases"):
        if directory.is_dir():
            directory.rmdir()
    phase5_jobs = output / "phase5-job-artifacts" / "fhss-jobs"
    if phase5_jobs.is_dir():
        allowed_job_files = {
            "iq.cf32", "iq.cf64", "iq.sigmf-meta", "truth.withheld.json",
            "receiver-minimal.json", "manifest.json",
        }
        for job_directory in phase5_jobs.iterdir():
            if (not job_directory.is_dir() or
                    re.fullmatch(r"j-[0-9a-f]{24}", job_directory.name) is None):
                raise RuntimeError(f"refusing unowned Phase5 job entry: {job_directory}")
            for artifact in job_directory.iterdir():
                if not artifact.is_file() or artifact.name not in allowed_job_files:
                    raise RuntimeError(f"refusing unowned Phase5 artifact: {artifact}")
                artifact.unlink()
            job_directory.rmdir()
        phase5_jobs.rmdir()
        (output / "phase5-job-artifacts").rmdir()
    marker.unlink()
    if created_dir:
        output.rmdir()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("serve", "exercise", "verify", "report", "cleanup",
                 "record-screenshot"):
        item = commands.add_parser(name)
        item.add_argument("--phase", type=int, default=PHASE)
        item.add_argument("--build-dir", type=Path, default=Path("build-ninja/ninja-debug"))
        item.add_argument("--output-dir", type=Path, required=True)
        if name == "serve":
            item.add_argument("--case", choices=("clean", "impaired", "negative",
                                                   "step", "continue", "cancelled"))
            item.add_argument("--port", type=int, default=0)
            item.add_argument("--exit-after-case", action="store_true")
        elif name == "verify":
            item.add_argument("--require-screenshots", action="store_true")
        elif name == "record-screenshot":
            item.add_argument("--case", choices=("clean", "impaired", "negative",
                                                   "step", "continue", "cancelled"),
                              required=True)
            item.add_argument("--path", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "exercise": return exercise(args)
    if args.command == "verify": return verify(args)
    if args.command == "cleanup": return cleanup(args)
    if args.command == "record-screenshot": return record_screenshot(args)
    if args.command == "report":
        print((args.output_dir.resolve() / f"phase{args.phase}-report.json").read_text(encoding="utf-8"))
        return 0
    process, url, _, _ = launch(args)
    print(url, flush=True)
    if args.case:
        if args.phase == 4:
            start_served_case(int(url.rsplit(":", 1)[1]), args.case,
                              args.output_dir.resolve(), url)
        elif args.phase == 5:
            start_served_phase5_case(int(url.rsplit(":", 1)[1]), args.case,
                                     args.output_dir.resolve(), url)
        else:
            raise RuntimeError("serve --case is supported for phases 4 and 5")
        if args.exit_after_case:
            stop(process)
            return 0
    try: return process.wait()
    except KeyboardInterrupt:
        stop(process)
        return 130


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
