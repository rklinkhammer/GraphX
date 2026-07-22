#!/usr/bin/env python3
"""External Phase 1-8 operator for the loopback-only GraphX FHSS dashboard."""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import copy
import hashlib
import http.client
import importlib.metadata
import json
import os
import platform
import re
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import traceback
import zlib
from datetime import datetime, timezone
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1] / "api"
sys.path.insert(0, str(API_DIR))
from schema_subset import load_registry, validate_instance, validate_schema  # noqa: E402
try:
    import jsonschema
    from jsonschema import Draft202012Validator, FormatChecker
    from referencing import Registry, Resource
except ImportError as error:
    raise SystemExit("install ../api/requirements-contracts.lock for authoritative live validation") from error

PHASE = 6
OWNED_MARKER = ".graphx-fhss-dashboard-operator"
MIN_SCREENSHOT_WIDTH = 640
MIN_SCREENSHOT_HEIGHT = 360
GENERATOR_TIMEOUT_SECONDS = 60
# WebDriver BiDi carries browser-automation results, not dashboard protocol
# traffic.  The reviewed axe-core 4.12.1 result observed during Phase 8 was
# 1,295,262 bytes, so retain a finite 4 MiB qualification-client allowance.
FIREFOX_BIDI_REVIEWED_AXE_FRAME_BYTES = 1_295_262
FIREFOX_BIDI_RECEIVE_MAX_BYTES = 4 * 1024 * 1024
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


def firefox_bidi_message_too_big(error: BaseException) -> bool:
    """Return whether a WebSocket close reports the bounded BiDi cap."""
    return any(getattr(frame, "code", None) == 1009 for frame in
               (getattr(error, "rcvd", None), getattr(error, "sent", None)))


def masked_websocket_text_frame(payload: bytes, mask: bytes) -> bytes:
    """Encode one RFC 6455 FIN/text client frame with canonical length form."""
    if len(mask) != 4:
        raise ValueError("WebSocket mask must contain exactly four bytes")
    length = len(payload)
    if length <= 125:
        header = bytes([0x81, 0x80 | length])
    elif length <= 0xffff:
        header = bytes([0x81, 0x80 | 126]) + length.to_bytes(2, "big")
    else:
        header = bytes([0x81, 0x80 | 127]) + length.to_bytes(8, "big")
    masked = bytes(value ^ mask[index % 4]
                   for index, value in enumerate(payload))
    return header + mask + masked


def phase6_document_type(document: object) -> str:
    if not isinstance(document, dict):
        return "non_object"
    if isinstance(document.get("frame_type"), str):
        return f"frame:{document['frame_type']}"
    if isinstance(document.get("schema"), str):
        return str(document["schema"])
    if isinstance(document.get("action"), str):
        return f"action:{document['action']}"
    return "object"


def phase6_document_is_frame(document: object) -> bool:
    return not (isinstance(document, dict) and
                document.get("frame_type") in
                ("http_handshake", "transport_eof"))


def build_phase6_lossless_stream(
        stream_id: str, scenario: str, direction: str, client_id: str,
        documents: list[object], expected_document_count: int,
        expected_frame_count: int) -> dict[str, object]:
    """Encode every ordered protocol document into bounded chained chunks."""
    max_raw = 524288
    if not documents or expected_document_count <= 0:
        raise RuntimeError(f"empty Phase6 lossless stream: {stream_id}")
    chunks: list[dict[str, object]] = []
    pending: list[bytes] = []
    pending_bytes = 0
    previous: str | None = None

    def commit_chunk() -> None:
        nonlocal pending, pending_bytes, previous
        raw = b"".join(pending)
        compressed = zlib.compress(raw, level=9)
        if not raw or len(raw) > max_raw or len(compressed) > 524288:
            raise RuntimeError(f"Phase6 lossless chunk bound exceeded: {stream_id}")
        chained = hashlib.sha256(
            (previous or "").encode("ascii") + raw).hexdigest()
        chunks.append({
            "index": len(chunks), "document_count": len(pending),
            "uncompressed_bytes": len(raw), "encoded_bytes": len(compressed),
            "sha256": chained, "previous_sha256": previous,
            "data_base64": base64.b64encode(compressed).decode("ascii"),
        })
        previous = chained
        pending = []
        pending_bytes = 0

    type_counts: dict[str, int] = {}
    sequences: list[int] = []
    for document in documents:
        line = (json.dumps(document, sort_keys=True,
                           separators=(",", ":")) + "\n").encode()
        if len(line) > max_raw:
            raise RuntimeError(
                f"Phase6 protocol document exceeds chunk bound: {stream_id}")
        if pending and pending_bytes + len(line) > max_raw:
            commit_chunk()
        pending.append(line)
        pending_bytes += len(line)
        kind = phase6_document_type(document)
        type_counts[kind] = type_counts.get(kind, 0) + 1
        if (isinstance(document, dict) and
                isinstance(document.get("sequence"), int) and
                not isinstance(document.get("sequence"), bool)):
            sequences.append(int(document["sequence"]))
    if pending:
        commit_chunk()
    if len(chunks) > 64:
        raise RuntimeError(f"Phase6 lossless chunk count exceeded: {stream_id}")
    return {
        "stream_id": stream_id, "scenario": scenario,
        "transport": "websocket", "direction": direction,
        "client_id": client_id,
        "expected_document_count": expected_document_count,
        "recorded_document_count": len(documents),
        "expected_frame_count": expected_frame_count,
        "recorded_frame_count": sum(phase6_document_is_frame(document)
                                    for document in documents),
        "first_sequence": sequences[0] if sequences else None,
        "last_sequence": sequences[-1] if sequences else None,
        "type_counts": type_counts, "chunk_count": len(chunks),
        "terminal_chain_sha256": previous, "chunks": chunks,
    }


def decode_phase6_lossless_stream(
        stream: dict[str, object]) -> list[object] | None:
    """Boundedly reconstruct and authenticate one lossless protocol stream."""
    try:
        chunks = list(stream["chunks"])
        if (not chunks or len(chunks) > 64 or
                int(stream["chunk_count"]) != len(chunks) or
                [chunk.get("index") for chunk in chunks] !=
                list(range(len(chunks)))):
            return None
        documents: list[object] = []
        previous: str | None = None
        for chunk in chunks:
            if chunk.get("previous_sha256") != previous:
                return None
            compressed = base64.b64decode(
                str(chunk["data_base64"]), validate=True)
            if (len(compressed) != int(chunk["encoded_bytes"]) or
                len(compressed) > 524288):
                return None
            inflater = zlib.decompressobj()
            raw = inflater.decompress(compressed, 524289)
            if (len(raw) > 524288 or inflater.unconsumed_tail or
                    not inflater.eof or inflater.unused_data):
                return None
            raw += inflater.flush(max(1, 524289 - len(raw)))
            if (len(raw) != int(chunk["uncompressed_bytes"]) or
                    not raw or len(raw) > 524288):
                return None
            chained = hashlib.sha256(
                (previous or "").encode("ascii") + raw).hexdigest()
            if chunk.get("sha256") != chained:
                return None
            lines = raw.splitlines(keepends=True)
            if len(lines) != int(chunk["document_count"]):
                return None
            for line in lines:
                if not line.endswith(b"\n"):
                    return None
                document = json.loads(line)
                canonical = (json.dumps(
                    document, sort_keys=True,
                    separators=(",", ":")) + "\n").encode()
                if canonical != line:
                    return None
                documents.append(document)
            previous = chained
        if stream.get("terminal_chain_sha256") != previous:
            return None
        return documents
    except (KeyError, TypeError, ValueError, zlib.error,
            json.JSONDecodeError, base64.binascii.Error):
        return None


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


def locate_firefox() -> Path:
    configured = os.environ.get("GRAPHX_FIREFOX_BINARY", "")
    candidates = [Path(configured)] if configured else []
    discovered = shutil.which("firefox")
    if discovered:
        candidates.append(Path(discovered))
    candidates.append(Path("/Applications/Firefox.app/Contents/MacOS/firefox"))
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate.resolve()
    raise RuntimeError(
        "Phase6 browser qualification requires maintained Firefox; set "
        "GRAPHX_FIREFOX_BINARY")


class FirefoxBidiSession:
    """Small dependency-bounded WebDriver BiDi client for evidence capture."""

    def __init__(self, output: Path):
        from websockets.sync.client import connect as websocket_connect

        self._messages: list[dict[str, object]] = []
        self._next_id = 0
        self._process: subprocess.Popen[str] | None = None
        self._socket = None
        self.context = ""
        self.session_id = ""
        self.capabilities: dict[str, object] = {}
        self._profile: Path | None = None
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(("127.0.0.1", 0))
        port = int(probe.getsockname()[1])
        probe.close()
        profile = output / "phase6-firefox-profile"
        if profile.exists():
            shutil.rmtree(profile)
        profile.mkdir(parents=True)
        (profile / "user.js").write_text(
            'user_pref("ui.prefersReducedMotion", 1);\n', encoding="utf-8")
        self._profile = profile
        command = [str(locate_firefox()), "--headless", "--no-remote",
                   "--profile", str(profile), "--remote-debugging-port",
                   str(port), "about:blank"]
        self._process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self._process.poll() is not None:
                break
            try:
                self._socket = websocket_connect(
                    f"ws://127.0.0.1:{port}/session", open_timeout=1,
                    close_timeout=1, compression=None,
                    max_size=FIREFOX_BIDI_RECEIVE_MAX_BYTES)
                break
            except Exception:
                time.sleep(0.1)
        if self._socket is None:
            self.close()
            raise RuntimeError("Firefox did not expose its WebDriver BiDi session")
        created = self.call("session.new", {"capabilities": {}})
        self.session_id = str(created["sessionId"])
        self.capabilities = dict(created["capabilities"])
        self.call("session.subscribe", {"events": ["log.entryAdded"]})
        self.context = str(self.call(
            "browsingContext.create", {"type": "tab"})["context"])
        try:
            self.call("browsingContext.setViewport", {
                "context": self.context,
                "viewport": {"width": 1440, "height": 1000},
                "devicePixelRatio": 1})
        except RuntimeError:
            pass

    def call(self, method: str, params: dict[str, object]) -> dict[str, object]:
        if self._socket is None:
            raise RuntimeError("Firefox BiDi session is closed")
        self._next_id += 1
        identifier = self._next_id
        self._socket.send(json.dumps(
            {"id": identifier, "method": method, "params": params}))
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                received = self._socket.recv(timeout=5)
            except Exception as error:
                if firefox_bidi_message_too_big(error):
                    raise RuntimeError(
                        "Firefox BiDi response exceeded the finite "
                        f"{FIREFOX_BIDI_RECEIVE_MAX_BYTES}-byte evidence-client "
                        "receive limit") from error
                raise
            response = json.loads(received)
            if response.get("type") == "event":
                if response.get("method") == "log.entryAdded":
                    entry = dict(response.get("params", {}).get("entry", {}))
                    self._messages.append({
                        "level": str(entry.get("level", "info")),
                        "text": str(entry.get("text", "")),
                        "timestamp": entry.get("timestamp"),
                        "source": entry.get("source", {}),
                    })
                continue
            if response.get("id") != identifier:
                continue
            if response.get("type") != "success":
                raise RuntimeError(
                    f"Firefox BiDi {method} failed: {json.dumps(response)}")
            return dict(response.get("result", {}))
        raise RuntimeError(f"Firefox BiDi {method} timed out")

    def navigate(self, url: str) -> None:
        self.call("browsingContext.navigate", {
            "context": self.context, "url": url, "wait": "complete"})

    def evaluate(self, expression: str) -> object:
        response = self.call("script.evaluate", {
            "expression": expression, "target": {"context": self.context},
            "awaitPromise": True, "resultOwnership": "none"})
        if response.get("type") != "success":
            raise RuntimeError(f"Firefox script evaluation failed: {response}")
        remote = dict(response.get("result", {}))
        return remote.get("value")

    def wait_for(self, expression: str, timeout: float = 10) -> object:
        deadline = time.monotonic() + timeout
        last: object = None
        while time.monotonic() < deadline:
            last = self.evaluate(expression)
            if last:
                return last
            time.sleep(0.1)
        raise RuntimeError(
            f"Firefox browser condition timed out: {expression}; last={last}")

    def screenshot(self, destination: Path) -> None:
        encoded = self.call("browsingContext.captureScreenshot", {
            "context": self.context, "origin": "viewport"})["data"]
        destination.write_bytes(base64.b64decode(str(encoded), validate=True))

    def key(self, value: str) -> None:
        """Send one genuine WebDriver BiDi keyboard key press."""
        self.call("input.performActions", {
            "context": self.context,
            "actions": [{"type":"key", "id":"phase8-keyboard", "actions":[
                {"type":"keyDown", "value":value},
                {"type":"keyUp", "value":value}]}]})

    @property
    def messages(self) -> list[dict[str, object]]:
        # Drain queued log events with a harmless evaluation before copying.
        self.evaluate("true")
        return copy.deepcopy(self._messages)

    def close(self) -> None:
        if self._socket is not None:
            try:
                self.call("session.end", {})
            except Exception:
                pass
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None
        if self._process is not None:
            stop(self._process)
            self._process = None
        if self._profile is not None and self._profile.is_dir():
            shutil.rmtree(self._profile)
        self._profile = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def browser_console_document(browser: FirefoxBidiSession, url: str,
                             state_path: Path, screenshot_path: Path,
                             observed_states: list[str]) -> dict[str, object]:
    return {
        "schema": "graphx.dashboard.browser_console.v1",
        "url": url,
        "captured_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "acquisition": {
            "mechanism": "webdriver_bidi",
            "session_id": browser.session_id,
            "context_id": browser.context,
        },
        "browser": {
            "name": browser.capabilities.get("browserName"),
            "version": browser.capabilities.get("browserVersion"),
            "user_agent": browser.capabilities.get("userAgent"),
            "headless": browser.capabilities.get("moz:headless") is True,
        },
        "served_state_sha256": sha256(state_path),
        "screenshot_sha256": sha256(screenshot_path),
        "observed_states": observed_states,
        "messages": browser.messages,
    }


def validate_browser_console(console: dict[str, object], state: dict[str, object],
                             state_path: Path, screenshot_path: Path) -> bool:
    try:
        captured = datetime.fromisoformat(
            str(console["captured_at"]).replace("Z", "+00:00"))
        acquisition = dict(console["acquisition"])
        browser = dict(console["browser"])
        messages = list(console["messages"])
        observed_states = list(console["observed_states"])
        now = datetime.now(timezone.utc)
        expected_state = {
            "live": "live WebSocket sequence",
            "replay": "live — replayed through sequence",
            "resync": "resync snapshot at sequence",
        }[str(state["case"])]
        return (
            console.get("schema") == "graphx.dashboard.browser_console.v1" and
            console.get("url") == state.get("url") and
            captured.tzinfo is not None and
            -5 <= (now - captured).total_seconds() <= 3600 and
            acquisition.get("mechanism") == "webdriver_bidi" and
            re.fullmatch(r"[0-9a-f-]{36}",
                         str(acquisition.get("session_id", ""))) is not None and
            re.fullmatch(r"[0-9a-f-]{36}",
                         str(acquisition.get("context_id", ""))) is not None and
            str(browser.get("name", "")).lower() == "firefox" and
            bool(browser.get("version")) and
            "Firefox/" in str(browser.get("user_agent", "")) and
            browser.get("headless") is True and
            console.get("served_state_sha256") == sha256(state_path) and
            console.get("screenshot_sha256") == sha256(screenshot_path) and
            any(expected_state in str(item) for item in observed_states) and
            all(isinstance(message, dict) and
                str(message.get("level", "")).lower() not in
                ("warning", "warn", "error") for message in messages))
    except (KeyError, TypeError, ValueError, OSError):
        return False


def validate_phase6_wire_transcript(document: dict[str, object]) -> bool:
    """Independently validate bounded Phase6 wire evidence and its proof."""
    try:
        schema_path = (Path(__file__).resolve().parent / "schemas" /
                       "phase6-wire-transcript.schema.json")
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        validate_schema(schema, schema_path.name)
        validate_instance(document, schema)
        encoded = json.dumps(document, sort_keys=True,
                             separators=(",", ":")).encode()
        limits = dict(document["limits"])
        records = list(document["records"])
        streams = list(document["lossless_streams"])
        proof = dict(document["proof"])
        if (len(encoded) > int(limits["max_encoded_bytes"]) or
                len(records) > int(limits["max_records"]) or
                len(streams) > int(limits["max_streams"]) or
                [record.get("index") for record in records] !=
                list(range(len(records)))):
            return False
        for record in records:
            payload_size = len(json.dumps(
                record.get("payload"), sort_keys=True,
                separators=(",", ":")).encode())
            if payload_size > int(limits["max_payload_bytes"]):
                return False
        scenarios = {str(record.get("scenario")) for record in records}
        required = set(proof["required_scenarios"])
        if not required.issubset(scenarios):
            return False
        stream_ids = [str(stream["stream_id"]) for stream in streams]
        if (not streams or len(stream_ids) != len(set(stream_ids)) or
                stream_ids != sorted(stream_ids) or
                stream_ids != list(proof["lossless_stream_ids"]) or
                proof.get("lossless_streams_complete") is not True):
            return False
        required_stream_scenarios = {
            "two_client", "within_retention_replay", "heartbeat",
            "stalled_websocket", "slow_client_overflow", "cross_origin",
            "forged_host", "missing_origin", "duplicate_origin",
            "extension_offer", "subprotocol_offer", "bad_version",
            "invalid_key", "duplicate_key", "unmasked", "oversized",
            "fragments", "utf8", "continuation", "control",
            "malformed_resume", "sequence_ahead", "idle_no_pong",
            "bounded_shutdown", "publisher_restart"}
        if not required_stream_scenarios.issubset(
                {str(stream["scenario"]) for stream in streams}):
            return False
        stream_directions = {
            scenario: {str(stream["direction"]) for stream in streams
                       if stream.get("scenario") == scenario}
            for scenario in required_stream_scenarios}
        if any(directions != {"client_to_server", "server_to_client"}
               for directions in stream_directions.values()):
            return False
        direct_stream_ids = {
            f"{record['scenario']}|{record.get('client_id') or 'none'}|"
            f"{record['direction']}"
            for record in records if record.get("transport") == "websocket"}
        if not direct_stream_ids.issubset(set(stream_ids)):
            return False
        decoded_by_id: dict[str, list[object]] = {}
        for stream in streams:
            decoded = decode_phase6_lossless_stream(stream)
            if decoded is None:
                return False
            decoded_by_id[str(stream["stream_id"])] = decoded
            recorded_frames = sum(phase6_document_is_frame(item)
                                  for item in decoded)
            if (int(stream["expected_document_count"]) != len(decoded) or
                    int(stream["recorded_document_count"]) != len(decoded) or
                    int(stream["expected_frame_count"]) != recorded_frames or
                    int(stream["recorded_frame_count"]) != recorded_frames):
                return False
            type_counts: dict[str, int] = {}
            sequences: list[int] = []
            for item in decoded:
                kind = phase6_document_type(item)
                type_counts[kind] = type_counts.get(kind, 0) + 1
                if (isinstance(item, dict) and
                        isinstance(item.get("sequence"), int) and
                        not isinstance(item.get("sequence"), bool)):
                    sequences.append(int(item["sequence"]))
            if (type_counts != dict(stream["type_counts"]) or
                    stream.get("first_sequence") !=
                        (sequences[0] if sequences else None) or
                    stream.get("last_sequence") !=
                        (sequences[-1] if sequences else None) or
                    any(current <= previous for previous, current in
                        zip(sequences, sequences[1:]))):
                return False
        required_high_volume = {
            "stalled_websocket|phase6-stalled-ws|server_to_client",
            "stalled_websocket|phase6-stalled-ws|client_to_server",
            "slow_client_overflow|phase6-healthy|server_to_client",
            "slow_client_overflow|phase6-healthy|client_to_server",
        }
        if not required_high_volume.issubset(decoded_by_id):
            return False
        healthy_received = decoded_by_id[
            "slow_client_overflow|phase6-healthy|server_to_client"]
        stalled_received = decoded_by_id[
            "stalled_websocket|phase6-stalled-ws|server_to_client"]
        if (len(healthy_received) < 1000 or
                not any(isinstance(item, dict) and
                        item.get("schema") ==
                        "graphx.dashboard.websocket_resync_required.v1"
                        for item in stalled_received) or
                not any(isinstance(item, dict) and
                        item.get("frame_type") == "close" and
                        item.get("close_code") == 1000
                        for item in stalled_received)):
            return False
        replayed = [int(value) for value in proof["replayed_sequences"]]
        resume = int(proof["resume_from_sequence"])
        if replayed != list(range(resume + 1, resume + 1 + len(replayed))):
            return False
        two_client_events = [
            record for record in records
            if record.get("scenario") == "two_client" and
            record.get("kind") == "event"]
        if (len(two_client_events) != 2 or
                {record.get("client_id") for record in two_client_events} !=
                {"phase6-first", "phase6-second"} or
                {int(record["payload"]["sequence"])
                 for record in two_client_events} !=
                {int(proof["two_client_sequence"])}):
            return False
        event_records = [record for record in records
                         if record.get("kind") == "event"]
        required_event_identity = {
            "sequence", "publisher_epoch", "generation", "run_epoch",
            "config_revision", "config_etag", "controller_epoch", "job_id",
            "correlation_id"}
        if (not event_records or any(
                not isinstance(record.get("payload"), dict) or
                not required_event_identity.issubset(record["payload"])
                for record in event_records)):
            return False
        per_client_sequences: dict[str, list[int]] = {}
        for record in event_records:
            client_id = str(record.get("client_id", ""))
            per_client_sequences.setdefault(client_id, []).append(
                int(record["payload"]["sequence"]))
        if any(any(current <= previous for previous, current in
                   zip(sequences, sequences[1:]))
               for sequences in per_client_sequences.values()):
            return False
        terminal_records = [record for record in records
                            if record.get("kind") == "job_terminal"]
        if (len(terminal_records) != 1 or
                not {"job_id", "controller_epoch", "run_epoch",
                     "config_revision", "config_etag",
                     "scenario_correlation_id"}.issubset(
                         terminal_records[0]["payload"])):
            return False
        required_close_codes = dict(proof["negative_close_codes"])
        observed_close_codes = {
            str(record["scenario"]): int(record["close_code"])
            for record in records if record.get("kind") == "close" and
            "close_code" in record}
        return (all(observed_close_codes.get(name) == int(code)
                    for name, code in required_close_codes.items()) and
                proof["old_publisher_epoch"] != proof["new_publisher_epoch"] and
                proof["publisher_epoch_changed"] is True and
                proof["replay_contiguous"] is True)
    except (KeyError, TypeError, ValueError, OSError, json.JSONDecodeError):
        return False


def validate_phase6_wire_transcript_negative_corpus(
        document: dict[str, object]) -> bool:
    """Prove duplicate, gap/order, and transcript-content tampering fail."""
    mutations: list[dict[str, object]] = []

    duplicate = copy.deepcopy(document)
    duplicate_record = copy.deepcopy(next(
        record for record in duplicate["records"]
        if record.get("scenario") == "two_client" and
        record.get("client_id") == "phase6-first" and
        record.get("kind") == "event"))
    duplicate_record["index"] = len(duplicate["records"])
    duplicate["records"].append(duplicate_record)
    mutations.append(duplicate)

    gap = copy.deepcopy(document)
    gap["proof"]["replayed_sequences"][0] += 1
    mutations.append(gap)

    reordered = copy.deepcopy(document)
    reordered["records"][0], reordered["records"][1] = (
        reordered["records"][1], reordered["records"][0])
    mutations.append(reordered)

    payload_tamper = copy.deepcopy(document)
    event = next(record for record in payload_tamper["records"]
                 if record.get("kind") == "event")
    event["payload"]["sequence"] = (1 << 64)
    mutations.append(payload_tamper)

    omission = copy.deepcopy(document)
    high_stream = next(
        stream for stream in omission["lossless_streams"]
        if stream["stream_id"] ==
        "slow_client_overflow|phase6-healthy|server_to_client")
    high_stream["expected_document_count"] += 1
    mutations.append(omission)

    duplicate_chunk = copy.deepcopy(document)
    high_stream = next(
        stream for stream in duplicate_chunk["lossless_streams"]
        if stream["stream_id"] ==
        "slow_client_overflow|phase6-healthy|server_to_client")
    duplicate = copy.deepcopy(high_stream["chunks"][-1])
    duplicate["index"] = len(high_stream["chunks"])
    high_stream["chunks"].append(duplicate)
    high_stream["chunk_count"] += 1
    mutations.append(duplicate_chunk)

    reordered_chunk = copy.deepcopy(document)
    high_stream = next(
        stream for stream in reordered_chunk["lossless_streams"]
        if stream["stream_id"] ==
        "slow_client_overflow|phase6-healthy|server_to_client")
    if len(high_stream["chunks"]) < 2:
        return False
    high_stream["chunks"][0], high_stream["chunks"][1] = (
        high_stream["chunks"][1], high_stream["chunks"][0])
    mutations.append(reordered_chunk)

    chunk_tamper = copy.deepcopy(document)
    high_stream = next(
        stream for stream in chunk_tamper["lossless_streams"]
        if stream["stream_id"] ==
        "slow_client_overflow|phase6-healthy|server_to_client")
    encoded = str(high_stream["chunks"][0]["data_base64"])
    high_stream["chunks"][0]["data_base64"] = (
        ("A" if encoded[0] != "A" else "B") + encoded[1:])
    mutations.append(chunk_tamper)

    inventory_tamper = copy.deepcopy(document)
    inventory_tamper["proof"]["lossless_stream_ids"].pop()
    mutations.append(inventory_tamper)

    return all(not validate_phase6_wire_transcript(item) for item in mutations)


def browser_websocket_outage_predicates(
        result: dict[str, object]) -> dict[str, object]:
    """Expose every browser outage acceptance predicate for evidence/tests."""
    outage_http = dict(result.get("outage_http", {}))
    coherent = dict(result.get("coherent_return", {}))
    behavioral = dict(result.get("behavioral", {}))
    reconnect = dict(result.get("stable_reconnect_reset", {}))
    messages = list(result.get("messages", []))
    return {
        "reset_status_200": outage_http.get("reset_status") == 200,
        "health_status_200": outage_http.get("health_status") == 200,
        "snapshot_status_200": outage_http.get("snapshot_status") == 200,
        "coherent_return_agrees": coherent.get("agrees") is True,
        "behavioral": {str(name): value is True
                       for name, value in behavioral.items()},
        "behavioral_nonempty": bool(behavioral),
        "reconnect_attempt_observed": (
            isinstance(reconnect.get("before"), int) and
            reconnect["before"] > 0),
        "stable_reconnect_reset": reconnect.get("after") == 0,
        "console_clean": not any(
            str(message.get("level", "")).lower() in
            ("warning", "warn", "error")
            for message in messages if isinstance(message, dict)),
    }


def browser_websocket_outage_passes(predicates: dict[str, object]) -> bool:
    """Evaluate a fully materialized browser outage predicate record."""
    behavioral = predicates.get("behavioral")
    return (isinstance(behavioral, dict) and bool(behavioral) and
            all(value is True for value in behavioral.values()) and
            all(value is True for name, value in predicates.items()
                if name != "behavioral"))


def qualify_browser_websocket_outage(url: str, port: int,
                                     output: Path) -> dict[str, object]:
    """Drive the production page through real WS loss, polling, and restore."""
    gate = output / "phase6-websocket-disabled.flag"
    diagnostic_path = output / "phase6-browser-transport-diagnostic.json"
    gate.unlink(missing_ok=True)
    diagnostic_path.unlink(missing_ok=True)
    transitions: list[dict[str, object]] = []
    stage = "launch_browser"
    try:
        with FirefoxBidiSession(output) as browser:
            stage = "navigate"
            browser.navigate(url + "/?transport_test=1")
            browser.wait_for(
                "document.getElementById('event-transport').textContent.length > 0")
            live_deadline = time.monotonic() + 10
            live = ""
            while time.monotonic() < live_deadline:
                request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                        {"Content-Type": "application/json"})
                value = browser.evaluate(
                    "document.getElementById('event-transport').textContent")
                if "live WebSocket sequence" in str(value):
                    live = str(value)
                    break
                time.sleep(0.1)
            if not live:
                raise RuntimeError(
                    "Firefox did not reach live WebSocket after bounded resets")
            transitions.append({"state": "live", "dom": live,
                                "at": datetime.now(timezone.utc).isoformat()})

            stage = "observe_polling_fallback"
            gate.write_text("websocket-disabled\n", encoding="utf-8")
            fallback = str(browser.wait_for(
                "document.getElementById('event-transport').textContent.includes("
                "'bounded polling fallback') && "
                "document.getElementById('event-transport').textContent", 10))
            transitions.append({"state": "websocket_unavailable", "dom": fallback,
                                "at": datetime.now(timezone.utc).isoformat()})
            reset_status, _, _ = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            polling = str(browser.wait_for(
                "document.getElementById('event-transport').textContent.includes("
                "'bounded polling sequence') && "
                "document.getElementById('event-transport').textContent", 10))
            health_status, _, _ = request(port, "GET", "/healthz")
            snapshot_status, _, snapshot_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            outage_snapshot = json.loads(snapshot_body)
            transitions.append({"state": "polling", "dom": polling,
                                "at": datetime.now(timezone.utc).isoformat()})

            stage = "restore_websocket"
            gate.unlink(missing_ok=True)
            browser.wait_for(
                "document.getElementById('event-transport').textContent.includes("
                "'WebSocket connected') || "
                "document.getElementById('event-transport').textContent.includes("
                "'live WebSocket sequence')", 15)
            request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                    {"Content-Type": "application/json"})
            restored = str(browser.wait_for(
                "document.getElementById('event-transport').textContent.includes("
                "'live WebSocket sequence') && "
                "document.getElementById('event-transport').textContent", 10))
            coherent = browser.evaluate("""
              (async () => {
                const snapshot = await fetch('/api/v1/fhss/snapshot',
                  {cache: 'no-store'}).then((response) => response.json());
                const identity = document.getElementById('config-identity').textContent;
                return JSON.stringify({
                  snapshot_revision: snapshot.config_revision,
                  snapshot_etag: snapshot.config_etag,
                  snapshot_sequence: snapshot.latest_sequence,
                  publisher_epoch: snapshot.publisher_epoch,
                  identity,
                  agrees: identity.includes(`revision=${snapshot.config_revision}`) &&
                    identity.includes(`etag=${snapshot.config_etag}`)
                });
              })()
            """)
            reconnect_before_stable = int(browser.evaluate(
                "window.__graphxDashboardTransportTest.state().reconnect_attempt"))
            # The page resets this counter only after the restored connection's
            # stable timer fires. Observe that semantic transition directly;
            # a fixed sleep races browser scheduling under qualification load.
            stable_state = json.loads(str(browser.wait_for("""
              (() => {
                const state = window.__graphxDashboardTransportTest.state();
                return state.reconnect_attempt === 0 ? JSON.stringify(state) : '';
              })()
            """, 10)))
            reconnect_after_stable = int(stable_state["reconnect_attempt"])
            behavioral = browser.evaluate("""
              (() => {
                const hooks = window.__graphxDashboardTransportTest;
                const contract = hooks.contract;
                const limits = {frame_bytes:65536,message_bytes:262144,
                  fragments_per_message:32,commands_per_second:16,
                  events_per_second:256,replay_events:256,replay_bytes:2097152,
                  queue_events:128,queue_bytes:2097152,idle_timeout_ms:1200,
                  max_lifetime_ms:3600000};
                const hello = {schema:'graphx.dashboard.websocket_hello.v1',
                  api_version:'v1',publisher_epoch:'epoch-a',latest_sequence:9,
                  oldest_available_sequence:1,heartbeat_interval_ms:200,limits};
                const heartbeat = {schema:'graphx.dashboard.websocket_heartbeat.v1',
                  publisher_epoch:'epoch-a',timestamp:'2026-07-20T01:02:03Z'};
                const counters = {dropped_events:0,dropped_events_total:0,
                  coalesced_events_total:0,reconnects_total:0};
                const event = {schema:'graphx.dashboard.event.v1',api_version:'v1',
                  publisher_epoch:'epoch-a',sequence:8,event_type:'metrics',
                  timestamp:'2026-07-20T01:02:03Z',payload:{ok:true}};
                const batch = {schema:'graphx.dashboard.events_batch.v1',
                  stream:'/api/v1/fhss/events',client_id:'fhss-browser-poll',
                  publisher_epoch:'epoch-a',latest_sequence:8,
                  oldest_available_sequence:1,newest_available_sequence:8,
                  events:[event],resync_required:false,reason:'none',
                  truncated:false,counters};
                const resync = {schema:'graphx.dashboard.websocket_resync_required.v1',
                  publisher_epoch:'epoch-a',snapshot_url:'/api/v1/fhss/snapshot',
                  reason:'retention_gap',latest_sequence:8};
                let attempt = 0;
                for (let index=0; index<12; ++index)
                  attempt = contract.nextReconnect(attempt).attempt;
                const fake = {schema:'graphx.dashboard.fhss_snapshot.v1',
                  publisher_epoch:'atomic-epoch',latest_sequence:77,
                  captured_at:'2026-07-20T01:02:03Z',config_revision:4242,
                  config_etag:'atomic-etag',generation:77,run_epoch:88,
                  configuration:{atomic_sentinel:true},
                  graph:{graph:{nodes:[],edges:[]}},
                  runtime:{rebuild_allowed:false,atomic_runtime:true},
                  metrics:{graph:{},nodes:[],edges:[]},diagnostics:{nodes:[]}};
                hooks.renderCoherentSnapshotForTesting(fake);
                const atomic = {
                  identity:document.getElementById('config-identity').textContent,
                  config:document.getElementById('config-effective').textContent,
                  runtime:document.getElementById('runtime-status').textContent,
                  meta:document.getElementById('meta').textContent};
                return JSON.stringify({
                  malformed_hello:!contract.validateHello({...hello,
                    heartbeat_interval_ms:'200'}),
                  malformed_heartbeat:!contract.validateHeartbeat({...heartbeat,
                    timestamp:'now'},'epoch-a'),
                  malformed_batch:!contract.validateBatch({...batch,
                    latest_sequence:9},'fhss-browser-poll'),
                  malformed_resync:!contract.validateResync({...resync,
                    snapshot_url:'https://attacker.invalid'},
                    '/api/v1/fhss/snapshot'),
                  duplicate:contract.classifyEvent(event,'epoch-a',8)==='duplicate',
                  gap:contract.classifyEvent({...event,sequence:10},
                    'epoch-a',7)==='resync',
                  epoch:contract.classifyEvent({...event,publisher_epoch:'epoch-b'},
                    'epoch-a',7)==='resync',
                  retry_exhausted:!contract.nextReconnect(attempt).allowed,
                  atomic_replacement:atomic.identity.includes('revision=4242') &&
                    atomic.identity.includes('etag=atomic-etag') &&
                    atomic.config.includes('atomic_sentinel') &&
                    atomic.runtime.includes('atomic_runtime') &&
                    atomic.meta.includes('generation=77 run=88')});
              })()
            """)
            transitions.append({"state": "websocket_restored", "dom": restored,
                                "at": datetime.now(timezone.utc).isoformat()})
            messages = browser.messages
            result = {
                "schema": "graphx.dashboard.browser_transport_qualification.v1",
                "url": url,
                "captured_at": datetime.now(timezone.utc).isoformat().replace(
                    "+00:00", "Z"),
                "acquisition": {"mechanism": "webdriver_bidi",
                                "session_id": browser.session_id,
                                "context_id": browser.context},
                "browser": {"name": browser.capabilities.get("browserName"),
                            "version": browser.capabilities.get("browserVersion"),
                            "user_agent": browser.capabilities.get("userAgent"),
                            "headless": browser.capabilities.get("moz:headless") is True},
                "transitions": transitions,
                "outage_http": {"reset_status": reset_status,
                                "health_status": health_status,
                                "snapshot_status": snapshot_status,
                                "snapshot_sequence": outage_snapshot.get(
                                    "latest_sequence")},
                "coherent_return": json.loads(str(coherent)),
                "behavioral": json.loads(str(behavioral)),
                "stable_reconnect_reset": {
                    "before": reconnect_before_stable,
                    "after": reconnect_after_stable},
                "messages": messages,
            }
            predicates = browser_websocket_outage_predicates(result)
            result["predicates"] = predicates
            result["result"] = (
                "PASS" if browser_websocket_outage_passes(predicates) else "FAIL")
            diagnostic_path.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8")
            if result["result"] != "PASS":
                failed = [name for name, value in predicates.items()
                          if (not all(value.values()) if isinstance(value, dict)
                              else value is not True)]
                raise RuntimeError(
                    "headless browser outage qualification failed; "
                    f"failed={failed}; evidence={diagnostic_path}; "
                    f"predicates={json.dumps(predicates, sort_keys=True)}")
            return result
    except Exception as error:
        if not diagnostic_path.exists():
            diagnostic_path.write_text(json.dumps({
                "schema":"graphx.dashboard.browser_transport_diagnostic.v1",
                "result":"FAIL", "stage":stage,
                "captured_at":datetime.now(timezone.utc).isoformat().replace(
                    "+00:00", "Z"),
                "transitions":transitions,
                "error":{"type":type(error).__name__, "message":str(error)},
            }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if str(diagnostic_path) in str(error):
            raise
        raise RuntimeError(
            f"browser outage qualification failed at {stage}; "
            f"evidence={diagnostic_path}; cause={error}") from error
    finally:
        gate.unlink(missing_ok=True)


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
    if args.phase >= 6:
        websocket_gate = args.output_dir.resolve() / "phase6-websocket-disabled.flag"
        websocket_gate.unlink(missing_ok=True)
        command.extend(["--dashboard-websocket-heartbeat-ms", "200",
                        "--dashboard-websocket-idle-ms", "1200",
                        "--dashboard-websocket-gate-file", str(websocket_gate)])
    if getattr(args, "investigation_qualification", False):
        command.append("--dashboard-investigation-qualification")
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
        (output / "screenshots").mkdir(exist_ok=True)
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
            port = int(url.rsplit(":", 1)[1])
            ready_deadline = time.monotonic() + 3
            while time.monotonic() < ready_deadline:
                if process.poll() is not None:
                    break
                try:
                    if request(port, "GET", "/healthz", timeout=0.5)[0] == 200:
                        return process, url, port, command
                except OSError:
                    time.sleep(0.02)
            break
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


def start_served_phase6_case(port: int, case: str, output: Path,
                             url: str) -> None:
    from websockets.sync.client import connect as websocket_connect
    websocket_url = f"ws://127.0.0.1:{port}/api/v1/fhss/events/stream"

    def connect(client_id: str, epoch: str = "", sequence: int = 0):
        client = websocket_connect(websocket_url, origin=url, open_timeout=5,
                                   close_timeout=2, max_size=256 * 1024,
                                   compression=None)
        hello = json.loads(client.recv(timeout=5))
        client.send(json.dumps({"action": "subscribe", "client_id": client_id,
                                "publisher_epoch": epoch,
                                "last_sequence": sequence}))
        return client, hello

    client, hello = connect(f"phase6-{case}")
    event: dict[str, object]
    if case == "resync":
        client.close()
        # Drive the public production publisher beyond the bounded 4096-event
        # retention window. A fresh browser (empty epoch, last_sequence=0)
        # must then receive resync_required; the condition remains true for
        # the subsequent manual browser capture.
        for _ in range(4100):
            reset_code, _, _ = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            if reset_code != 200:
                raise RuntimeError(
                    f"failed to prepare resync retention gap: HTTP {reset_code}")
        client, hello = connect("phase6-resync-expired", "", 0)
        event = json.loads(client.recv(timeout=5))
        if event.get("schema") != \
                "graphx.dashboard.websocket_resync_required.v1":
            raise RuntimeError("fresh browser did not receive resync_required")
        transport_state = "resync_required"
    else:
        request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
        event = json.loads(client.recv(timeout=5))
        if case == "replay":
            sequence = int(event.get("sequence", 0))
            client.close()
            request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                    {"Content-Type": "application/json"})
            client, hello = connect("phase6-replay-resume",
                                    hello["publisher_epoch"], sequence)
            event = json.loads(client.recv(timeout=5))
            transport_state = "contiguous_replay"
        else:
            transport_state = "live"
    client.close()
    status_code, _, status_body = request(port, "GET", "/api/v1/fhss/status")
    status = json.loads(status_body) if status_code == 200 else {}
    sequence = int(event.get("sequence", event.get("latest_sequence", 0)))
    state = {
        "schema": "graphx.dashboard.phase6.served_state.v1", "case": case,
        "url": url, "transport_state": transport_state,
        "publisher_epoch": event.get("publisher_epoch", hello.get("publisher_epoch")),
        "sequence": sequence,
        "generation": int(status.get("active_generation", 0)),
        "run_epoch": int(status.get("active_run_epoch", 0)),
        "observation_id": f"event-{case}-{sequence}",
        "observation_sha256": hashlib.sha256(
            json.dumps(event, sort_keys=True).encode()).hexdigest(),
        "synthetic_data_only": True, "hwil_available": False,
    }
    (output / f"phase6-{case}-served-state.json").write_text(
        json.dumps(state, indent=2) + "\n", encoding="utf-8")
    if case == "live":
        def publish_live_events() -> None:
            while True:
                time.sleep(1)
                try:
                    request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                            {"Content-Type": "application/json"})
                except OSError:
                    return
        threading.Thread(target=publish_live_events, daemon=True).start()


def exercise(args: argparse.Namespace) -> int:
    if args.phase not in (1, 2, 3, 4, 5, 6, 7, 8):
        raise RuntimeError("this operator implements Phases 1 through 8 only")
    phase = args.phase
    output = args.output_dir.resolve()
    output_preexisted = output.exists()
    if output_preexisted and any(output.iterdir()):
        raise RuntimeError(f"refusing preexisting nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / OWNED_MARKER).write_text(
        f"phase={phase}\ncreated_dir={0 if output_preexisted else 1}\n", encoding="utf-8")
    checks: list[dict[str, object]] = []
    phase6_records: list[dict[str, object]] = []
    phase6_lossless_overrides: dict[str, dict[str, object]] = {}

    def transcript(scenario: str, transport: str, direction: str, kind: str,
                   payload: object, client_id: str | None = None,
                   **fields: object) -> None:
        if phase < 6:
            return
        if len(phase6_records) >= 1024:
            raise RuntimeError("Phase6 wire transcript record bound exceeded")
        if len(json.dumps(payload, sort_keys=True,
                          separators=(",", ":")).encode()) > 262144:
            raise RuntimeError("Phase6 wire transcript payload bound exceeded")
        phase6_records.append({
            "index": len(phase6_records), "scenario": scenario,
            "transport": transport, "direction": direction, "kind": kind,
            "client_id": client_id, "payload": copy.deepcopy(payload),
            **fields,
        })

    def check(name: str, condition: bool, evidence: str) -> None:
        checks.append({"name": name, "pass": bool(condition), "evidence": evidence})

    def stream_id(scenario: str, client_id: str | None,
                  direction: str) -> str:
        return f"{scenario}|{client_id or 'none'}|{direction}"

    def record_lossless_override(
            scenario: str, direction: str, client_id: str,
            documents: list[object], expected_documents: int,
            expected_frames: int) -> None:
        identifier = stream_id(scenario, client_id, direction)
        phase6_lossless_overrides[identifier] = build_phase6_lossless_stream(
            identifier, scenario, direction, client_id, documents,
            expected_documents, expected_frames)

    process = None
    command: list[str] = []
    additional_commands: list[list[str]] = []
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
        transport_status, transport_headers, transport_script = request(
            port, "GET", "/fhss_transport_state.js")
        check("production browser transport module is served and CSP-authorized",
              transport_status == 200 and
              "javascript" in transport_headers.get("content-type", "") and
              b"validateHello" in transport_script and
              b"validateBatch" in transport_script and
              "script-src 'self'" in content_security_policy,
              f"HTTP {transport_status}; bytes={len(transport_script)}; "
              f"type={transport_headers.get('content-type')}")
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
        check("event payload cannot shadow browser document",
              b"let eventDocument;" in page and
              b"let document;" not in page and
              b"Event transport: live \xe2\x80\x94 replayed through sequence" in page and
              b"transportContract.classifyEvent" in page and
              b"transportContract.validateBatch" in page and
              b"value.sequence === sequence" in transport_script and
              b"graphx.dashboard.events_batch.v1" in transport_script and
              b"coherentResyncRequired" in page and
              b"maxReconnectDelayMs" in page,
              "strict envelope/duplicate/gap/resync/event-poll/backoff browser state machine")
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
        if phase >= 6:
            schema_names["/api/v1/fhss/snapshot"] = "fhss-snapshot"
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
            step_observation_status, _, step_observation_body = request(
                port, "GET", "/api/v1/fhss/observations")
            step_observation = json.loads(step_observation_body)
            persist("phase5_step_observation", step_observation_body)
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
            oracle_cycle = [
                (24, 2863311530, -56250000.0),
                (28, 2004318071, -26250000.0),
                (32, 303174162, 3750000.0),
                (36, 1650614882, 33750000.0),
            ]
            oracle_expected = oracle_cycle * 4 + [
                (24, 16909060, -56250000.0),
                (28, 2779096538, -26250000.0),
            ]
            independent_timing = (
                len(oracle_pulses) == 18 and all(
                    int(oracle_pulses[index]["received_global_start_sample"]) ==
                    (index + 1) * 6500 for index in range(len(oracle_pulses))))
            check("Phase5 independent one-message timing oracle",
                  independent_timing,
                  f"pulse_count={len(oracle_pulses)}; slot=6500 input samples")
            independent_channel_words = (
                len(oracle_pulses) == len(oracle_expected) and all(
                    (int(pulse.get("frequency_index", -1)),
                     int(pulse.get("word", -1)),
                     float(pulse.get("iq_offset_frequency_hz", float("nan")))) ==
                    oracle_expected[index]
                    for index, pulse in enumerate(oracle_pulses)))
            check("Phase5 independent channel offset and decoded-word oracle",
                  independent_channel_words,
                  "hard-coded architecture sequence: logical=physical "
                  "24/28/32/36; offsets=-56.25/-26.25/3.75/33.75 MHz")

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
            old_controller_epoch = int(step_terminal["controller_epoch"])
            stop(process)
            process = None
            offline_summary = output / "phase5-offline-summary.json"
            offline_command = [str(locate_executable(args.build_dir.resolve())),
                               "--graph-config", str(receiver_config_path),
                               "--summary-json", str(offline_summary)]
            offline = subprocess.run(offline_command, text=True,
                                     capture_output=True, timeout=90)
            offline_document = (json.loads(offline_summary.read_text(
                encoding="utf-8")) if offline_summary.is_file() else {})
            offline_diagnostics = offline_document.get("fhss_diagnostics", {})
            offline_receiver_result = {
                "availability": {"state": "available", "reason": None},
                "status": offline_diagnostics.get("message_status"),
                "accepted": offline_diagnostics.get("message_status") == "Ok",
                "decoded_pulse_count": offline_diagnostics.get("pulse_count"),
            }
            live_receiver_result = step_terminal.get("receiver_message_result", {})
            live_pulses = [{
                "global_start_sample": pulse.get("global_start_sample"),
                "frequency_index": pulse.get("logical_frequency_index"),
                "physical_channel_index": pulse.get("physical_channel_index"),
                "decoded_value": pulse.get("decoded_value"),
                "iq_offset_frequency_hz": pulse.get("iq_offset_frequency_hz"),
            } for pulse in step_observation.get("observed_pulses", [])]
            offline_pulses = [{
                "global_start_sample": pulse.get("global_start_sample"),
                "frequency_index": pulse.get("frequency_index"),
                "physical_channel_index": pulse.get("channel_id"),
                "decoded_value": pulse.get("decoded_value"),
                "iq_offset_frequency_hz": pulse.get("iq_offset_frequency_hz"),
            } for pulse in offline_diagnostics.get("decoded_pulses", [])]
            canonical_hash = lambda value: hashlib.sha256(
                json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest()
            check("Phase5 offline receiver replay needs no dashboard or truth",
                  offline.returncode == 0 and offline_summary.is_file() and
                  not truth_path.exists(),
                  f"exit={offline.returncode}; truth_present={truth_path.exists()}")
            check("Phase5 offline receiver result exactly matches dashboard evidence",
                  step_observation_status == 200 and
                  offline_receiver_result == live_receiver_result and
                  offline_pulses == live_pulses and
                  canonical_hash(offline_receiver_result) ==
                      canonical_hash(live_receiver_result) and
                  canonical_hash(offline_pulses) == canonical_hash(live_pulses),
                  f"result_hash={canonical_hash(offline_receiver_result)}; "
                  f"pulse_hash={canonical_hash(offline_pulses)}")

            process, url, port, restart_command = launch(args)
            command.extend(["--restart-command", *restart_command])
            restart_status, _, restart_body = request(
                port, "GET", "/api/v1/fhss/jobs")
            restart_history = json.loads(restart_body)
            restart_reset_status, _, restart_reset_body = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            restart_reset = json.loads(restart_reset_body)
            persist("phase5_restart_history", restart_body)
            persist("phase5_restart_reset", restart_reset_body)
            check("Phase5 real process restart resets memory and preserves bounded artifacts",
                  restart_status == 200 and restart_reset_status == 200 and
                  restart_history.get("entries") == [] and
                  int(restart_history.get("controller_epoch", 0)) !=
                      old_controller_epoch and
                  restart_reset.get("idempotency_entries_retained") == 0 and
                  restart_reset.get("status") == "already_reset" and
                  manifest_path.is_file() and iq_file.is_file(),
                  f"old_epoch={old_controller_epoch}; "
                  f"new_epoch={restart_history.get('controller_epoch')}; "
                  f"entries={len(restart_history.get('entries', []))}")
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
        if phase >= 6:
            try:
                from websockets.sync.client import connect as websocket_connect
            except ImportError as error:
                raise RuntimeError(
                    "Phase 6 requires the pinned maintained websockets client") from error

            websocket_url = f"ws://127.0.0.1:{port}/api/v1/fhss/events/stream"

            def open_event_client(client_id: str, publisher_epoch: str = "",
                                  last_sequence: int = 0,
                                  scenario: str = "connection"):
                client = websocket_connect(websocket_url, origin=url,
                                           open_timeout=5, close_timeout=2,
                                           max_size=256 * 1024,
                                           compression=None)
                transcript(scenario, "websocket", "client_to_server",
                           "handshake_request",
                           {"url": websocket_url, "origin": url,
                            "compression": None}, client_id)
                transcript(scenario, "websocket", "server_to_client",
                           "handshake_response",
                           {"http_status": 101,
                            "negotiated_extensions": []}, client_id,
                           http_status=101)
                hello = json.loads(client.recv(timeout=5))
                subscribe_command = {"action": "subscribe",
                                     "client_id": client_id,
                                     "publisher_epoch": publisher_epoch,
                                     "last_sequence": last_sequence}
                transcript(scenario, "websocket", "server_to_client", "hello",
                           hello, client_id)
                transcript(scenario, "websocket", "client_to_server",
                           "subscribe", subscribe_command, client_id)
                client.send(json.dumps(subscribe_command))
                return client, hello

            def close_event_client(client, scenario: str | None = None,
                                   client_id: str | None = None) -> bool:
                try:
                    client.close()
                    if scenario is not None:
                        transcript(scenario, "websocket", "client_to_server",
                                   "close", {"frame_type": "close",
                                             "close_code": 1000,
                                             "initiator": "operator"},
                                   client_id, close_code=1000)
                    return True
                except Exception:
                    # Exact graceful/error close semantics are asserted by the
                    # raw protocol matrix and C++ behavioral lane. Cleanup must
                    # tolerate a peer that has already completed its close.
                    return False

            first, first_hello = open_event_client(
                "phase6-first", scenario="two_client")
            second, second_hello = open_event_client(
                "phase6-second", scenario="two_client")
            check("WebSocket maintained-client hello",
                  first_hello.get("schema") == "graphx.dashboard.websocket_hello.v1" and
                  first_hello.get("publisher_epoch") == second_hello.get("publisher_epoch") and
                  first_hello.get("limits", {}).get("queue_bytes", 0) > 0,
                  json.dumps(first_hello, sort_keys=True))
            reset_status, _, reset_event_body = request(
                port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                {"Content-Type": "application/json"})
            first_event = json.loads(first.recv(timeout=5))
            second_event = json.loads(second.recv(timeout=5))
            transcript("two_client", "websocket", "server_to_client", "event",
                       first_event, "phase6-first")
            transcript("two_client", "websocket", "server_to_client", "event",
                       second_event, "phase6-second")
            persist("phase6_first_event", json.dumps(first_event).encode())
            check("two independent live clients",
                  reset_status == 200 and
                  first_event.get("schema") == "graphx.dashboard.event.v1" and
                  second_event.get("sequence") == first_event.get("sequence") and
                  first_event.get("publisher_epoch") == first_hello.get("publisher_epoch"),
                  f"HTTP {reset_status}; sequence={first_event.get('sequence')}")

            resume_sequence = int(first_event.get("sequence", 0))
            close_event_client(first, "two_client", "phase6-first")
            request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                    {"Content-Type": "application/json"})
            replay, replay_hello = open_event_client(
                "phase6-replay", first_hello["publisher_epoch"], resume_sequence,
                "within_retention_replay")
            replay_event = json.loads(replay.recv(timeout=5))
            transcript("within_retention_replay", "websocket", "server_to_client",
                       "event", replay_event, "phase6-replay")
            check("contiguous replay after reconnect",
                  replay_hello.get("publisher_epoch") == first_hello.get("publisher_epoch") and
                  replay_event.get("sequence") == resume_sequence + 1,
                  json.dumps(replay_event, sort_keys=True))
            close_event_client(replay, "within_retention_replay",
                               "phase6-replay")
            close_event_client(second, "two_client", "phase6-second")

            heartbeat, _ = open_event_client(
                "phase6-heartbeat", first_hello["publisher_epoch"],
                int(replay_event.get("sequence", 0)), "heartbeat")
            heartbeat_deadline = time.monotonic() + 1.5
            while time.monotonic() < heartbeat_deadline:
                heartbeat_message = json.loads(heartbeat.recv(timeout=1))
                if heartbeat_message.get("schema") == \
                        "graphx.dashboard.websocket_heartbeat.v1":
                    heartbeat_ack = {
                        "action": "heartbeat_ack",
                        "publisher_epoch": heartbeat_message["publisher_epoch"],
                    }
                    transcript("heartbeat", "websocket", "server_to_client",
                               "heartbeat", heartbeat_message, "phase6-heartbeat")
                    transcript("heartbeat", "websocket", "client_to_server",
                               "heartbeat_ack", heartbeat_ack, "phase6-heartbeat")
                    heartbeat.send(json.dumps(heartbeat_ack))
            request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                    {"Content-Type": "application/json"})
            while True:
                heartbeat_event = json.loads(heartbeat.recv(timeout=5))
                if heartbeat_event.get("schema") == \
                        "graphx.dashboard.websocket_heartbeat.v1":
                    heartbeat_ack = {
                        "action": "heartbeat_ack",
                        "publisher_epoch": heartbeat_event["publisher_epoch"],
                    }
                    transcript("heartbeat", "websocket", "server_to_client",
                               "heartbeat", heartbeat_event, "phase6-heartbeat")
                    transcript("heartbeat", "websocket", "client_to_server",
                               "heartbeat_ack", heartbeat_ack, "phase6-heartbeat")
                    heartbeat.send(json.dumps(heartbeat_ack))
                    continue
                break
            transcript("heartbeat", "websocket", "server_to_client", "event",
                       heartbeat_event, "phase6-heartbeat")
            heartbeat_snapshot_status, _, heartbeat_snapshot_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            heartbeat_snapshot = json.loads(heartbeat_snapshot_body)
            transcript("heartbeat", "http", "server_to_client",
                       "metric_snapshot", heartbeat_snapshot, None,
                       http_status=heartbeat_snapshot_status)
            check("ping/pong keeps maintained client alive",
                  heartbeat_event.get("schema") == "graphx.dashboard.event.v1" and
                  heartbeat_snapshot_status == 200 and
                  int(heartbeat_snapshot.get("transport", {}).get(
                      "pongs_received", 0)) > 0,
                  f"event={heartbeat_event.get('sequence')}; "
                  f"pongs={heartbeat_snapshot.get('transport', {}).get('pongs_received')}")
            heartbeat_sequence = int(heartbeat_event.get("sequence", 0))

            # A production RFC6455 client that deliberately stops reading.
            # Its tiny receive buffer stalls only that session's synchronous
            # writer. The publisher then overflows that client's bounded app
            # queue while a maintained client drains concurrently.
            _, _, before_stall_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            before_stall_snapshot = json.loads(before_stall_body)
            before_stall_overflows = int(before_stall_snapshot.get(
                "transport", {}).get("queue_overflows", 0))
            stalled_websocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            stalled_websocket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            stalled_websocket.settimeout(5)
            stalled_websocket.connect(("127.0.0.1", port))
            stalled_websocket.sendall((
                "GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                f"Origin: {url}\r\n\r\n").encode())
            stalled_handshake = b""
            while b"\r\n\r\n" not in stalled_handshake:
                stalled_handshake += stalled_websocket.recv(4096)
            stalled_wire = stalled_handshake.split(b"\r\n\r\n", 1)[1]
            stalled_received_documents: list[object] = [{
                "frame_type": "http_handshake", "http_status": 101,
                "headers": stalled_handshake.split(
                    b"\r\n\r\n", 1)[0].decode("iso-8859-1"),
            }]
            stalled_received_expected = 1
            stalled_received_frames_expected = 0
            stalled_command = json.dumps({
                "action": "subscribe", "client_id": "phase6-stalled-ws",
                "publisher_epoch": first_hello["publisher_epoch"],
                "last_sequence": heartbeat_sequence}).encode()
            stalled_mask = b"\x12\x34\x56\x78"
            stalled_websocket.sendall(masked_websocket_text_frame(
                stalled_command, stalled_mask))
            stalled_sent_documents: list[object] = [{
                "frame_type": "http_handshake", "method": "GET",
                "url": websocket_url, "origin": url,
            }, json.loads(stalled_command)]
            stalled_sent_expected = 2
            stalled_sent_frames_expected = 1
            transcript("stalled_websocket", "websocket", "server_to_client",
                       "handshake_response",
                       stalled_handshake.split(b"\r\n\r\n", 1)[0].decode(
                           "iso-8859-1"), "phase6-stalled-ws", http_status=101)
            transcript("stalled_websocket", "websocket", "client_to_server",
                       "subscribe", json.loads(stalled_command),
                       "phase6-stalled-ws")
            stalled_keepalive = json.dumps({
                "action": "heartbeat_ack",
                "publisher_epoch": first_hello["publisher_epoch"],
            }).encode()
            stalled_keepalive_frame = masked_websocket_text_frame(
                stalled_keepalive, b"\x31\x41\x59\x26")
            stalled_keepalive_stop = threading.Event()
            stalled_keepalive_errors: list[str] = []

            def keep_stalled_websocket_subscribed() -> None:
                nonlocal stalled_sent_expected, stalled_sent_frames_expected
                try:
                    while not stalled_keepalive_stop.wait(0.2):
                        # Sending a valid bounded acknowledgement does not read
                        # or drain any server bytes; receive backpressure remains.
                        stalled_websocket.sendall(stalled_keepalive_frame)
                        stalled_sent_expected += 1
                        stalled_sent_frames_expected += 1
                        stalled_sent_documents.append(
                            json.loads(stalled_keepalive))
                except (BrokenPipeError, ConnectionResetError, OSError) as error:
                    if not stalled_keepalive_stop.is_set():
                        stalled_keepalive_errors.append(
                            f"{type(error).__name__}: {error}")

            stalled_keepalive_thread = threading.Thread(
                target=keep_stalled_websocket_subscribed,
                name="phase6-stalled-websocket-keepalive", daemon=True)
            stalled_keepalive_thread.start()
            transcript("stalled_websocket", "websocket", "client_to_server",
                       "heartbeat_ack", json.loads(stalled_keepalive),
                       "phase6-stalled-ws")

            healthy, healthy_hello = open_event_client(
                "phase6-healthy", first_hello["publisher_epoch"],
                heartbeat_sequence, "slow_client_overflow")
            healthy_messages: list[dict[str, object]] = []
            healthy_received_documents: list[object] = [{
                "frame_type": "http_handshake", "http_status": 101,
                "negotiated_extensions": [],
            }, healthy_hello]
            healthy_received_expected = 2
            healthy_received_frames_expected = 1
            healthy_sent_documents: list[object] = [{
                "frame_type": "http_handshake", "method": "GET",
                "url": websocket_url, "origin": url,
            }, {
                "action": "subscribe", "client_id": "phase6-healthy",
                "publisher_epoch": first_hello["publisher_epoch"],
                "last_sequence": heartbeat_sequence,
            }]
            healthy_sent_expected = 2
            healthy_sent_frames_expected = 1
            healthy_errors: list[str] = []
            healthy_stop = threading.Event()

            def drain_healthy_websocket() -> None:
                nonlocal healthy_received_expected, healthy_received_frames_expected
                nonlocal healthy_sent_expected, healthy_sent_frames_expected
                try:
                    while not healthy_stop.is_set():
                        try:
                            message = json.loads(healthy.recv(timeout=0.25))
                        except TimeoutError:
                            continue
                        healthy_received_expected += 1
                        healthy_received_frames_expected += 1
                        healthy_received_documents.append(message)
                        if message.get("schema") == \
                                "graphx.dashboard.websocket_heartbeat.v1":
                            acknowledgement = {
                                "action": "heartbeat_ack",
                                "publisher_epoch": message["publisher_epoch"],
                            }
                            healthy.send(json.dumps(acknowledgement))
                            healthy_sent_expected += 1
                            healthy_sent_frames_expected += 1
                            healthy_sent_documents.append(acknowledgement)
                        else:
                            healthy_messages.append(message)
                except Exception as error:  # recorded below, never hidden
                    if not healthy_stop.is_set():
                        healthy_errors.append(
                            f"{type(error).__name__}: {error}")

            healthy_thread = threading.Thread(
                target=drain_healthy_websocket,
                name="phase6-maintained-websocket", daemon=True)
            healthy_thread.start()
            # Keep publication below the 256 events/s ingress budget so this
            # does not manufacture a global publisher overflow. The aggregate
            # payload is intentionally larger than ordinary TCP buffering.
            for _ in range(1600):
                request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                        {"Content-Type": "application/json"})
                time.sleep(0.0045)

            stall_deadline = time.monotonic() + 5
            after_stall_snapshot = before_stall_snapshot
            while time.monotonic() < stall_deadline:
                _, _, after_stall_body = request(
                    port, "GET", "/api/v1/fhss/snapshot")
                after_stall_snapshot = json.loads(after_stall_body)
                if int(after_stall_snapshot.get("transport", {}).get(
                        "queue_overflows", 0)) > before_stall_overflows:
                    break
                time.sleep(0.02)
            after_stall_overflows = int(after_stall_snapshot.get(
                "transport", {}).get("queue_overflows", 0))

            # Release the socket only after overflow is observed. The stalled
            # session must then emit an explicit resync and bounded close.
            stalled_websocket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF,
                                         256 * 1024)
            stalled_websocket.settimeout(0.25)
            stalled_resync: dict[str, object] | None = None
            stalled_close_code: int | None = None
            stalled_close_hex = ""
            drain_deadline = time.monotonic() + 3
            while time.monotonic() < drain_deadline and stalled_close_code is None:
                cursor = 0
                while cursor + 2 <= len(stalled_wire):
                    opcode = stalled_wire[cursor] & 0x0f
                    length = stalled_wire[cursor + 1] & 0x7f
                    header = 2
                    if length == 126:
                        if cursor + 4 > len(stalled_wire):
                            break
                        length = int.from_bytes(
                            stalled_wire[cursor + 2:cursor + 4], "big")
                        header = 4
                    elif length == 127:
                        if cursor + 10 > len(stalled_wire):
                            break
                        length = int.from_bytes(
                            stalled_wire[cursor + 2:cursor + 10], "big")
                        header = 10
                    frame_end = cursor + header + length
                    if frame_end > len(stalled_wire):
                        break
                    frame = stalled_wire[cursor:frame_end]
                    payload = stalled_wire[cursor + header:frame_end]
                    if opcode == 0x1:
                        parsed = json.loads(payload.decode("utf-8"))
                        stalled_received_frames_expected += 1
                        stalled_received_expected += 1
                        stalled_received_documents.append(parsed)
                        if parsed.get("schema") == \
                                "graphx.dashboard.websocket_resync_required.v1":
                            stalled_resync = parsed
                    elif opcode == 0x8:
                        stalled_close_code = (
                            int.from_bytes(payload[:2], "big")
                            if len(payload) >= 2 else 1005)
                        stalled_close_hex = frame.hex()
                        stalled_received_frames_expected += 1
                        stalled_received_expected += 1
                        stalled_received_documents.append({
                            "frame_type": "close",
                            "close_code": stalled_close_code,
                            "frame_hex": stalled_close_hex,
                        })
                    cursor = frame_end
                stalled_wire = stalled_wire[cursor:]
                if stalled_close_code is None:
                    try:
                        chunk = stalled_websocket.recv(65536)
                        if not chunk:
                            break
                        stalled_wire += chunk
                    except socket.timeout:
                        continue
                    except (ConnectionResetError, OSError):
                        break
            stalled_keepalive_stop.set()
            stalled_keepalive_thread.join(timeout=1)

            healthy_deadline = time.monotonic() + 5
            while (time.monotonic() < healthy_deadline and
                   not any(message.get("schema") ==
                           "graphx.dashboard.event.v1"
                           for message in healthy_messages)):
                time.sleep(0.02)
            healthy_event = next(
                (message for message in healthy_messages
                 if message.get("schema") == "graphx.dashboard.event.v1"), {})

            # Exercise the schema-compatible HTTP queue independently after
            # the raw WebSocket proof so its counters cannot satisfy that proof.
            slow_status, _, _ = request(
                port, "GET", "/api/v1/fhss/events?client_id=phase6-slow-public")
            for _ in range(160):
                request(port, "POST", "/api/v1/fhss/commands/reset", b"{}",
                        {"Content-Type": "application/json"})
            slow_after_status, _, slow_after_body = request(
                port, "GET", "/api/v1/fhss/events?client_id=phase6-slow-public")
            slow_after = json.loads(slow_after_body)
            transcript("slow_client_overflow", "http", "server_to_client",
                       "poll_overflow", slow_after, "phase6-slow-public",
                       http_status=slow_after_status)
            transcript("slow_client_overflow", "websocket", "server_to_client",
                       "event", healthy_event, "phase6-healthy")
            if stalled_resync is not None:
                transcript("stalled_websocket", "websocket", "server_to_client",
                           "resync_required", stalled_resync,
                           "phase6-stalled-ws")
            transcript("stalled_websocket", "websocket", "server_to_client",
                       "close", {"reason": "bounded stalled-client recovery"},
                       "phase6-stalled-ws", close_code=stalled_close_code,
                       frame_hex=stalled_close_hex)
            overflow_job_status, _, _, overflow_job = submit_job(
                "/api/v1/fhss/commands/step",
                {"request_id": "phase6-after-overflow", "timeout_ms": 60000},
                "phase6-after-overflow")
            overflow_terminal = wait_job(str(overflow_job.get("job_id")))
            transcript("stalled_websocket", "http", "server_to_client",
                       "job_terminal", overflow_terminal, None,
                       http_status=200)
            transcript("stalled_websocket", "http", "server_to_client",
                       "metric_snapshot", after_stall_snapshot)
            check("slow consumer overflow isolates healthy client and job",
                  slow_status == slow_after_status == 200 and
                  slow_after.get("resync_required") is True and
                  int(slow_after.get("counters", {}).get("dropped_events", 0)) > 0 and
                  after_stall_overflows > before_stall_overflows and
                  stalled_resync is not None and
                  stalled_close_code == 1000 and
                  not stalled_keepalive_errors and
                  healthy_event.get("schema") == "graphx.dashboard.event.v1" and
                  not healthy_errors and
                  overflow_job_status == 202 and
                  overflow_terminal.get("state") == "completed",
                  f"dropped={slow_after.get('counters', {}).get('dropped_events')}; "
                  f"ws_overflows={before_stall_overflows}->{after_stall_overflows}; "
                  f"stalled_resync={stalled_resync is not None}; "
                  f"stalled_close={stalled_close_code}; "
                  f"stalled_keepalive_errors={stalled_keepalive_errors}; "
                  f"healthy={healthy_event.get('sequence')}; "
                  f"healthy_errors={healthy_errors}; "
                  f"job={overflow_terminal.get('state')}")
            persist("phase6_slow_overflow", slow_after_body)
            healthy_stop.set()
            healthy_thread.join(timeout=2)
            healthy_close_sent = close_event_client(healthy)
            if healthy_close_sent:
                healthy_sent_expected += 1
                healthy_sent_frames_expected += 1
                healthy_sent_documents.append({
                    "frame_type": "close", "close_code": 1000,
                    "initiator": "operator",
                })
            record_lossless_override(
                "stalled_websocket", "server_to_client", "phase6-stalled-ws",
                stalled_received_documents, stalled_received_expected,
                stalled_received_frames_expected)
            record_lossless_override(
                "stalled_websocket", "client_to_server", "phase6-stalled-ws",
                stalled_sent_documents, stalled_sent_expected,
                stalled_sent_frames_expected)
            record_lossless_override(
                "slow_client_overflow", "server_to_client", "phase6-healthy",
                healthy_received_documents, healthy_received_expected,
                healthy_received_frames_expected)
            record_lossless_override(
                "slow_client_overflow", "client_to_server", "phase6-healthy",
                healthy_sent_documents, healthy_sent_expected,
                healthy_sent_frames_expected)
            close_event_client(heartbeat, "heartbeat", "phase6-heartbeat")

            browser_transport = qualify_browser_websocket_outage(url, port, output)
            transcript("websocket_outage", "browser", "operator",
                       "outage_poll_restore", browser_transport)
            transition_states = [item.get("state")
                                 for item in browser_transport["transitions"]]
            restored_event = {
                "sequence": browser_transport["coherent_return"][
                    "snapshot_sequence"]}
            check("actual browser WebSocket outage polls and coherently restores",
                  transition_states == ["live", "websocket_unavailable",
                                        "polling", "websocket_restored"] and
                  browser_transport["outage_http"]["health_status"] == 200 and
                  browser_transport["outage_http"]["snapshot_status"] == 200 and
                  browser_transport["coherent_return"]["agrees"] is True and
                  browser_transport["acquisition"]["mechanism"] ==
                      "webdriver_bidi",
                  json.dumps(browser_transport, sort_keys=True))
            persist("phase6_transport_restore",
                    json.dumps(browser_transport, sort_keys=True).encode())

            negative = socket.create_connection(("127.0.0.1", port), timeout=5)
            bad_upgrade = (
                f"GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Origin: https://attacker.invalid\r\n\r\n").encode()
            negative.sendall(bad_upgrade)
            transcript("cross_origin", "websocket", "client_to_server",
                       "handshake_request",
                       bad_upgrade.decode("iso-8859-1"), None)
            negative_response = negative.recv(4096)
            negative.close()
            transcript("cross_origin", "websocket", "server_to_client",
                       "handshake_response",
                       negative_response.decode("iso-8859-1", errors="replace"),
                       None, http_status=403)
            check("cross-origin upgrade rejected",
                  b" 403 " in negative_response,
                  negative_response.split(b"\r\n", 1)[0].decode(errors="replace"))

            def raw_upgrade_status(scenario_name: str, host: str,
                                   origins: list[str],
                                   extra: str = "",
                                   key: str = "dGhlIHNhbXBsZSBub25jZQ==") -> bytes:
                probe = socket.create_connection(("127.0.0.1", port), timeout=5)
                headers = "".join(f"Origin: {origin}\r\n" for origin in origins)
                request_bytes = (
                    "GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                    f"Host: {host}\r\nUpgrade: websocket\r\n"
                    "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                    f"Sec-WebSocket-Key: {key}\r\n"
                    f"{headers}{extra}\r\n").encode()
                probe.sendall(request_bytes)
                transcript(scenario_name, "websocket", "client_to_server",
                           "handshake_request",
                           request_bytes.decode("iso-8859-1"), None)
                response = probe.recv(4096)
                probe.close()
                return response

            forged_host_status = raw_upgrade_status(
                "forged_host", "attacker.invalid",
                ["http://attacker.invalid"])
            missing_origin_status = raw_upgrade_status(
                "missing_origin", f"127.0.0.1:{port}", [])
            duplicate_origin_status = raw_upgrade_status(
                "duplicate_origin", f"127.0.0.1:{port}", [url, url])
            extension_status = raw_upgrade_status(
                "extension_offer", f"127.0.0.1:{port}", [url],
                "Sec-WebSocket-Extensions: permessage-deflate\r\n")
            subprotocol_status = raw_upgrade_status(
                "subprotocol_offer", f"127.0.0.1:{port}", [url],
                "Sec-WebSocket-Protocol: graphx.v1\r\n")
            for negative_name, response, status_code in (
                    ("forged_host", forged_host_status, 403),
                    ("missing_origin", missing_origin_status, 403),
                    ("duplicate_origin", duplicate_origin_status, 403),
                    ("extension_offer", extension_status, 101),
                    ("subprotocol_offer", subprotocol_status, 400)):
                transcript(negative_name, "websocket", "server_to_client",
                           "handshake_response",
                           response.decode("iso-8859-1", errors="replace"),
                           None, http_status=status_code)
            check("strict bound-origin and negotiation policy",
                  b" 403 " in forged_host_status and
                  b" 403 " in missing_origin_status and
                  b" 403 " in duplicate_origin_status and
                  b" 101 " in extension_status and
                  b"sec-websocket-extensions:" not in extension_status.lower() and
                  b" 400 " in subprotocol_status,
                  "; ".join(x.split(b"\r\n", 1)[0].decode(errors="replace") for x in
                            (forged_host_status, missing_origin_status,
                             duplicate_origin_status,
                             extension_status, subprotocol_status)))

            bad_version = socket.create_connection(("127.0.0.1", port), timeout=5)
            bad_version_request = (
                f"GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Version: 12\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                f"Origin: {url}\r\n\r\n").encode()
            bad_version.sendall(bad_version_request)
            transcript("bad_version", "websocket", "client_to_server",
                       "handshake_request",
                       bad_version_request.decode("iso-8859-1"), None)
            bad_version_response = bad_version.recv(4096)
            bad_version.close()
            bad_version_http = (426 if b" 426 " in bad_version_response else 400)
            transcript("bad_version", "websocket", "server_to_client",
                       "handshake_response",
                       bad_version_response.decode("iso-8859-1", errors="replace"),
                       None, http_status=bad_version_http)
            check("unsupported WebSocket version rejected",
                  b" 400 " in bad_version_response or b" 426 " in bad_version_response,
                  bad_version_response.split(b"\r\n", 1)[0].decode(errors="replace"))

            invalid_key = raw_upgrade_status(
                "invalid_key", f"127.0.0.1:{port}", [url], key="invalid-key")
            duplicate_key = raw_upgrade_status(
                "duplicate_key", f"127.0.0.1:{port}", [url],
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n")
            transcript("invalid_key", "websocket", "server_to_client",
                       "handshake_response",
                       invalid_key.decode("iso-8859-1", errors="replace"),
                       None, http_status=400)
            transcript("duplicate_key", "websocket", "server_to_client",
                       "handshake_response",
                       duplicate_key.decode("iso-8859-1", errors="replace"),
                       None, http_status=400)

            def masked_frame(opcode: int, payload: bytes, fin: bool = True,
                             masked: bool = True) -> bytes:
                first = (0x80 if fin else 0) | opcode
                mask_bit = 0x80 if masked else 0
                if len(payload) <= 125:
                    header = bytes((first, mask_bit | len(payload)))
                elif len(payload) <= 0xffff:
                    header = bytes((first, mask_bit | 126)) + len(payload).to_bytes(2, "big")
                else:
                    header = bytes((first, mask_bit | 127)) + len(payload).to_bytes(8, "big")
                mask = b"\x12\x34\x56\x78"
                encoded = bytes(value ^ mask[index % 4]
                                for index, value in enumerate(payload)) if masked else payload
                return header + (mask if masked else b"") + encoded

            def raw_close_code(scenario_name: str,
                               frames: list[bytes]) -> tuple[int | None, str]:
                probe = socket.create_connection(("127.0.0.1", port), timeout=5)
                probe.sendall((
                    "GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                    f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                    "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                    f"Origin: {url}\r\n\r\n").encode())
                buffered = b""
                while b"\r\n\r\n" not in buffered:
                    buffered += probe.recv(4096)
                handshake, buffered = buffered.split(b"\r\n\r\n", 1)
                received_documents: list[object] = [{
                    "frame_type": "http_handshake", "http_status": 101,
                    "headers": handshake.decode("iso-8859-1"),
                }]
                received_expected = 1
                received_frames_expected = 0
                sent_documents: list[object] = [{
                    "frame_type": "http_handshake", "method": "GET",
                    "url": websocket_url, "origin": url,
                }] + [{
                    "frame_type": "raw_client_frame",
                    "bytes": len(frame),
                    "sha256": hashlib.sha256(frame).hexdigest(),
                    "data_base64": base64.b64encode(frame).decode("ascii"),
                } for frame in frames]
                for frame in frames:
                    probe.sendall(frame)
                probe.settimeout(4)

                def finish(code: int | None, frame_hex: str) -> tuple[int | None, str]:
                    record_lossless_override(
                        scenario_name, "client_to_server", "operator-negative",
                        sent_documents, len(sent_documents), len(frames))
                    record_lossless_override(
                        scenario_name, "server_to_client", "operator-negative",
                        received_documents, received_expected,
                        received_frames_expected)
                    return code, frame_hex

                try:
                    while True:
                        cursor = 0
                        while cursor + 2 <= len(buffered):
                            opcode = buffered[cursor] & 0x0f
                            length = buffered[cursor + 1] & 0x7f
                            header = 2
                            if length == 126:
                                if cursor + 4 > len(buffered): break
                                length = int.from_bytes(buffered[cursor + 2:cursor + 4], "big")
                                header = 4
                            elif length == 127:
                                if cursor + 10 > len(buffered): break
                                length = int.from_bytes(buffered[cursor + 2:cursor + 10], "big")
                                header = 10
                            if cursor + header + length > len(buffered): break
                            frame_end = cursor + header + length
                            payload = buffered[cursor + header:frame_end]
                            if opcode == 1:
                                parsed = json.loads(payload.decode("utf-8"))
                                received_expected += 1
                                received_frames_expected += 1
                                received_documents.append(parsed)
                            if opcode == 8:
                                frame = buffered[cursor:cursor + header + length]
                                code = (int.from_bytes(payload[:2], "big")
                                        if length >= 2 else None)
                                received_expected += 1
                                received_frames_expected += 1
                                received_documents.append({
                                    "frame_type": "close", "close_code": code,
                                    "frame_hex": frame.hex(),
                                })
                                return finish(code, frame.hex())
                            cursor += header + length
                        buffered = buffered[cursor:] + probe.recv(4096)
                except (socket.timeout, OSError):
                    return finish(None, "")
                finally:
                    probe.close()

            subscribe = json.dumps({"action": "subscribe",
                                    "client_id": "operator-negative",
                                    "last_sequence": 0}).encode()
            negative_results = {
                "unmasked": raw_close_code(
                    "unmasked", [masked_frame(1, subscribe, masked=False)]),
                "oversized": raw_close_code(
                    "oversized", [masked_frame(1, b"x" * (257 * 1024))]),
                "fragments": raw_close_code(
                    "fragments",
                    [masked_frame(1, b"{", fin=False)] +
                    [masked_frame(0, b" ", fin=False) for _ in range(32)] +
                    [masked_frame(0, b"}", fin=True)]),
                "utf8": raw_close_code(
                    "utf8", [masked_frame(1, b"\xc3\x28")]),
                "continuation": raw_close_code(
                    "continuation", [masked_frame(0, b"{}")]),
                "control": raw_close_code(
                    "control", [masked_frame(9, b"x", fin=False)]),
            }
            negative_codes = {name: result[0]
                              for name, result in negative_results.items()}
            for negative_name, (close_code, frame_hex) in negative_results.items():
                transcript(negative_name, "websocket", "server_to_client",
                           "close", {"reason": "protocol_negative"},
                           "operator-negative", close_code=close_code,
                           frame_hex=frame_hex)
            malformed_resume, _ = open_event_client(
                "phase6-malformed-resume", first_hello["publisher_epoch"],
                int(restored_event.get("sequence", 0)), "malformed_resume")
            time.sleep(0.1)
            malformed_command = {"action": "heartbeat_ack",
                                 "publisher_epoch": 7}
            transcript("malformed_resume", "websocket", "client_to_server",
                       "heartbeat_ack", malformed_command,
                       "phase6-malformed-resume")
            malformed_resume.send(json.dumps(malformed_command))
            malformed_resume_code = None
            malformed_deadline = time.monotonic() + 3
            while time.monotonic() < malformed_deadline and \
                    malformed_resume_code is None:
                try:
                    malformed_observed = json.loads(
                        malformed_resume.recv(timeout=1))
                    malformed_kind = (
                        "heartbeat" if malformed_observed.get("schema") ==
                        "graphx.dashboard.websocket_heartbeat.v1" else
                        "event")
                    transcript("malformed_resume", "websocket",
                               "server_to_client", malformed_kind,
                               malformed_observed,
                               "phase6-malformed-resume")
                except Exception as error:
                    received_close = getattr(error, "rcvd", None)
                    malformed_resume_code = getattr(received_close, "code", None)
            close_event_client(malformed_resume)
            transcript("malformed_resume", "websocket", "server_to_client",
                       "close", {"reason": "malformed_resume"},
                       "phase6-malformed-resume",
                       close_code=malformed_resume_code, frame_hex="")
            ahead, ahead_hello = open_event_client(
                "phase6-sequence-ahead", first_hello["publisher_epoch"],
                (1 << 64) - 2, "sequence_ahead")
            ahead_message = json.loads(ahead.recv(timeout=5))
            transcript("sequence_ahead", "websocket", "server_to_client",
                       "resync_required", ahead_message,
                       "phase6-sequence-ahead")
            close_event_client(ahead, "sequence_ahead",
                               "phase6-sequence-ahead")
            check("complete malformed protocol and resume matrix",
                  b" 101 " not in invalid_key and
                  b" 400 " in duplicate_key and
                  negative_codes == {"unmasked": 1002, "oversized": 1009,
                                     "fragments": 1009, "utf8": 1007,
                                     "continuation": 1002, "control": 1002} and
                  malformed_resume_code == 1008 and
                  ahead_message.get("schema") ==
                      "graphx.dashboard.websocket_resync_required.v1" and
                  ahead_message.get("reason") == "sequence_ahead" and
                  ahead_message.get("publisher_epoch") ==
                      ahead_hello.get("publisher_epoch"),
                  json.dumps({"invalid_key_status":
                                  invalid_key.split(b"\r\n", 1)[0].decode(
                                      errors="replace"),
                              "duplicate_key_status":
                                  duplicate_key.split(b"\r\n", 1)[0].decode(
                                      errors="replace"),
                              "codes": negative_codes,
                              "malformed_resume": malformed_resume_code,
                              "ahead": ahead_message}, sort_keys=True))

            _, _, pre_idle_snapshot_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            pre_idle_closes = int(json.loads(pre_idle_snapshot_body).get(
                "transport", {}).get("idle_closes", 0))
            idle_socket = socket.create_connection(("127.0.0.1", port), timeout=5)
            idle_socket.sendall((
                f"GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                f"Origin: {url}\r\n\r\n").encode())
            wire = b""
            while b"\r\n\r\n" not in wire:
                wire += idle_socket.recv(4096)
            idle_handshake, idle_buffered = wire.split(b"\r\n\r\n", 1)
            idle_received_documents: list[object] = [{
                "frame_type": "http_handshake", "http_status": 101,
                "headers": idle_handshake.decode("iso-8859-1"),
            }]
            idle_received_expected = 1
            idle_received_frames_expected = 0
            transcript("idle_no_pong", "websocket", "server_to_client",
                       "handshake_response",
                       wire.split(b"\r\n\r\n", 1)[0].decode("iso-8859-1"),
                       "no-pong", http_status=101)
            subscribe_bytes = json.dumps(
                {"action": "subscribe", "client_id": "no-pong",
                 "publisher_epoch": first_hello["publisher_epoch"],
                 "last_sequence": int(restored_event.get("sequence", 0))}).encode()
            mask = b"\x12\x34\x56\x78"
            idle_socket.sendall(masked_websocket_text_frame(
                subscribe_bytes, mask))
            idle_sent_documents: list[object] = [{
                "frame_type": "http_handshake", "method": "GET",
                "url": websocket_url, "origin": url,
            }, json.loads(subscribe_bytes)]
            transcript("idle_no_pong", "websocket", "client_to_server",
                       "subscribe", json.loads(subscribe_bytes), "no-pong")
            idle_socket.settimeout(4)
            idle_started = time.monotonic()
            close_seen = False
            idle_close_code: int | None = None
            idle_close_hex = ""
            buffered = idle_buffered
            while time.monotonic() - idle_started < 3.5 and not close_seen:
                if buffered:
                    cursor = 0
                    while cursor + 2 <= len(buffered):
                        opcode = buffered[cursor] & 0x0f
                        length = buffered[cursor + 1] & 0x7f
                        header = 2
                        if length == 126 and cursor + 4 <= len(buffered):
                            length = int.from_bytes(buffered[cursor + 2:cursor + 4], "big")
                            header = 4
                        elif length == 127 and cursor + 10 <= len(buffered):
                            length = int.from_bytes(buffered[cursor + 2:cursor + 10], "big")
                            header = 10
                        if cursor + header + length > len(buffered):
                            break
                        frame_end = cursor + header + length
                        frame = buffered[cursor:frame_end]
                        frame_payload = buffered[cursor + header:frame_end]
                        if opcode == 0x1:
                            idle_received_expected += 1
                            idle_received_frames_expected += 1
                            idle_received_documents.append(
                                json.loads(frame_payload.decode("utf-8")))
                        elif opcode in (0x9, 0xA):
                            idle_received_expected += 1
                            idle_received_frames_expected += 1
                            idle_received_documents.append({
                                "frame_type": "ping" if opcode == 0x9 else "pong",
                                "frame_hex": frame.hex(),
                            })
                        if opcode == 0x8:
                            close_seen = True
                            close_payload = frame_payload
                            idle_close_code = (
                                int.from_bytes(close_payload[:2], "big")
                                if len(close_payload) >= 2 else 1005)
                            idle_close_hex = frame.hex()
                            idle_received_expected += 1
                            idle_received_frames_expected += 1
                            idle_received_documents.append({
                                "frame_type": "close",
                                "close_code": idle_close_code,
                                "frame_hex": idle_close_hex,
                            })
                            break
                        cursor += header + length
                    buffered = buffered[cursor:]
                if not close_seen:
                    try:
                        buffered += idle_socket.recv(4096)
                    except socket.timeout:
                        break
            idle_socket.close()
            _, _, post_idle_snapshot_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            post_idle_closes = int(json.loads(post_idle_snapshot_body).get(
                "transport", {}).get("idle_closes", 0))
            transcript("idle_no_pong", "websocket", "server_to_client",
                       "close", {"reason": "client idle timeout"}, "no-pong",
                       close_code=idle_close_code, frame_hex=idle_close_hex)
            transcript("idle_no_pong", "http", "server_to_client",
                       "metric_snapshot", json.loads(post_idle_snapshot_body),
                       None, http_status=200)
            record_lossless_override(
                "idle_no_pong", "server_to_client", "no-pong",
                idle_received_documents, idle_received_expected,
                idle_received_frames_expected)
            record_lossless_override(
                "idle_no_pong", "client_to_server", "no-pong",
                idle_sent_documents, len(idle_sent_documents),
                1)
            check("missing pong closes idle client within configured bound",
                  post_idle_closes > pre_idle_closes and
                  idle_close_code == 1008 and
                  time.monotonic() - idle_started < 3.5,
                  f"close_seen={close_seen}; idle_closes={pre_idle_closes}->"
                  f"{post_idle_closes}; close_code={idle_close_code}; "
                  f"elapsed={time.monotonic() - idle_started:.3f}s")

            old_epoch = first_hello["publisher_epoch"]
            old_sequence = int(healthy_event.get("sequence", heartbeat_sequence))
            shutdown_stalled = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            shutdown_stalled.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            shutdown_stalled.settimeout(5)
            shutdown_stalled.connect(("127.0.0.1", port))
            shutdown_stalled.sendall((
                "GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                f"Host: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                f"Origin: {url}\r\n\r\n").encode())
            shutdown_stalled_handshake = b""
            while b"\r\n\r\n" not in shutdown_stalled_handshake:
                shutdown_stalled_handshake += shutdown_stalled.recv(4096)
            shutdown_headers, shutdown_wire = shutdown_stalled_handshake.split(
                b"\r\n\r\n", 1)
            shutdown_stalled_received: list[object] = [{
                "frame_type": "http_handshake", "http_status": 101,
                "headers": shutdown_headers.decode("iso-8859-1"),
            }]
            shutdown_stalled_received_expected = 1
            shutdown_stalled_received_frames = 0
            shutdown_stalled_command = json.dumps({
                "action": "subscribe",
                "client_id": "phase6-shutdown-stalled",
                "publisher_epoch": old_epoch,
                "last_sequence": old_sequence}).encode()
            shutdown_mask = b"\x89\xab\xcd\xef"
            shutdown_stalled.sendall(masked_websocket_text_frame(
                shutdown_stalled_command, shutdown_mask))
            shutdown_stalled_sent: list[object] = [{
                "frame_type": "http_handshake", "method": "GET",
                "url": websocket_url, "origin": url,
            }, json.loads(shutdown_stalled_command)]
            transcript("bounded_shutdown", "websocket", "server_to_client",
                       "handshake_response",
                       shutdown_stalled_handshake.split(b"\r\n\r\n", 1)[0].decode(
                           "iso-8859-1"), "phase6-shutdown-stalled",
                       http_status=101)
            transcript("bounded_shutdown", "websocket", "client_to_server",
                       "subscribe", json.loads(shutdown_stalled_command),
                       "phase6-shutdown-stalled")
            shutdown_healthy, shutdown_hello = open_event_client(
                "phase6-shutdown-healthy", old_epoch, old_sequence,
                "bounded_shutdown")
            _, _, pre_shutdown_body = request(
                port, "GET", "/api/v1/fhss/snapshot")
            pre_shutdown_snapshot = json.loads(pre_shutdown_body)
            transcript("bounded_shutdown", "http", "server_to_client",
                       "metric_snapshot", pre_shutdown_snapshot)
            shutdown_started = time.monotonic()
            stop(process)
            shutdown_elapsed_ms = int(
                (time.monotonic() - shutdown_started) * 1000)
            # The peer may have queued hello/event frames before shutdown. Drain
            # those bounded bytes until EOF/reset instead of mistaking the first
            # buffered byte for a still-open connection.
            stalled_closed = False
            close_deadline = time.monotonic() + 1.0
            while time.monotonic() < close_deadline and not stalled_closed:
                try:
                    shutdown_stalled.settimeout(
                        min(0.1, close_deadline - time.monotonic()))
                    shutdown_chunk = shutdown_stalled.recv(4096)
                    stalled_closed = shutdown_chunk == b""
                    shutdown_wire += shutdown_chunk
                except socket.timeout:
                    continue
                except (ConnectionResetError, OSError):
                    stalled_closed = True
            shutdown_stalled.close()
            stalled_websocket.close()
            close_event_client(shutdown_healthy)
            cursor = 0
            while cursor + 2 <= len(shutdown_wire):
                opcode = shutdown_wire[cursor] & 0x0f
                length = shutdown_wire[cursor + 1] & 0x7f
                header = 2
                if length == 126:
                    if cursor + 4 > len(shutdown_wire):
                        break
                    length = int.from_bytes(
                        shutdown_wire[cursor + 2:cursor + 4], "big")
                    header = 4
                elif length == 127:
                    if cursor + 10 > len(shutdown_wire):
                        break
                    length = int.from_bytes(
                        shutdown_wire[cursor + 2:cursor + 10], "big")
                    header = 10
                frame_end = cursor + header + length
                if frame_end > len(shutdown_wire):
                    break
                frame = shutdown_wire[cursor:frame_end]
                payload = shutdown_wire[cursor + header:frame_end]
                if opcode == 0x1:
                    shutdown_stalled_received.append(
                        json.loads(payload.decode("utf-8")))
                elif opcode == 0x8:
                    shutdown_stalled_received.append({
                        "frame_type": "close",
                        "close_code": (int.from_bytes(payload[:2], "big")
                                       if len(payload) >= 2 else 1005),
                        "frame_hex": frame.hex(),
                    })
                else:
                    shutdown_stalled_received.append({
                        "frame_type": f"opcode_{opcode}",
                        "frame_hex": frame.hex(),
                    })
                shutdown_stalled_received_expected += 1
                shutdown_stalled_received_frames += 1
                cursor = frame_end
            shutdown_stalled_received.append({
                "frame_type": "transport_eof",
                "buffered_unparsed_bytes": len(shutdown_wire) - cursor,
                "socket_closed": stalled_closed,
            })
            shutdown_stalled_received_expected += 1
            record_lossless_override(
                "bounded_shutdown", "server_to_client",
                "phase6-shutdown-stalled", shutdown_stalled_received,
                shutdown_stalled_received_expected,
                shutdown_stalled_received_frames)
            record_lossless_override(
                "bounded_shutdown", "client_to_server",
                "phase6-shutdown-stalled", shutdown_stalled_sent,
                len(shutdown_stalled_sent), 1)
            transcript("bounded_shutdown", "websocket", "server_to_client",
                       "transport_eof", {
                           "frame_type": "transport_eof",
                           "client_id": "phase6-shutdown-healthy",
                           "process_exit": process.returncode,
                       }, "phase6-shutdown-healthy")
            transcript("bounded_shutdown", "process", "operator",
                       "shutdown", {
                           "elapsed_ms": shutdown_elapsed_ms,
                           "active_clients_before": pre_shutdown_snapshot.get(
                               "transport", {}).get("active_websocket_clients"),
                           "healthy_client": "phase6-shutdown-healthy",
                           "stalled_client": "phase6-stalled-ws",
                           "stalled_socket_closed": stalled_closed,
                           "process_exit": process.returncode})
            process, url, port, restarted_command = launch(args)
            websocket_url = (
                f"ws://127.0.0.1:{port}/api/v1/fhss/events/stream")
            command.extend(["--operator-restart--", *restarted_command])
            restarted, restarted_hello = open_event_client(
                "phase6-old-epoch", old_epoch, old_sequence,
                "publisher_restart")
            restart_resync = json.loads(restarted.recv(timeout=5))
            transcript("publisher_restart", "websocket", "server_to_client",
                       "resync_required", restart_resync, "phase6-old-epoch")
            snapshot_status, _, snapshot_body = request(
                port, "GET", str(restart_resync.get("snapshot_url", "")))
            snapshot = json.loads(snapshot_body)
            transcript("publisher_restart", "http", "server_to_client",
                       "coherent_snapshot", snapshot, None,
                       http_status=snapshot_status)
            check("bounded shutdown with healthy and stalled WebSockets",
                  shutdown_elapsed_ms < 8000 and stalled_closed and
                  int(pre_shutdown_snapshot.get("transport", {}).get(
                      "active_websocket_clients", 0)) >= 2,
                  f"elapsed_ms={shutdown_elapsed_ms}; "
                  f"active_before={pre_shutdown_snapshot.get('transport', {}).get('active_websocket_clients')}; "
                  f"stalled_closed={stalled_closed}")
            check("publisher restart requires coherent resync snapshot",
                  restart_resync.get("schema") ==
                      "graphx.dashboard.websocket_resync_required.v1" and
                  restarted_hello.get("publisher_epoch") != old_epoch and
                  snapshot_status == 200 and
                  snapshot.get("schema") == "graphx.dashboard.fhss_snapshot.v1" and
                  snapshot.get("publisher_epoch") == restarted_hello.get("publisher_epoch") and
                  snapshot.get("latest_sequence") == restart_resync.get("latest_sequence") and
                  snapshot.get("config_revision") ==
                      snapshot.get("configuration", {}).get("config_revision") and
                  snapshot.get("generation") ==
                      snapshot.get("runtime", {}).get("active_generation"),
                  f"old={old_epoch}; new={restarted_hello.get('publisher_epoch')}; "
                  f"snapshot_http={snapshot_status}")
            persist("phase6_restart_resync", json.dumps(restart_resync).encode())
            persist("phase6_coherent_snapshot", snapshot_body)
            close_event_client(restarted, "publisher_restart",
                               "phase6-old-epoch")
            persist("phase6_reset_response", reset_event_body)
            negative_proof = {**negative_codes,
                              "malformed_resume": malformed_resume_code}
            direct_stream_documents: dict[str, list[object]] = {}
            direct_stream_metadata: dict[str, tuple[str, str, str]] = {}
            for record in phase6_records:
                if record.get("transport") != "websocket":
                    continue
                scenario_name = str(record["scenario"])
                direction_name = str(record["direction"])
                client_name = str(record.get("client_id") or "none")
                identifier = stream_id(
                    scenario_name, record.get("client_id"), direction_name)
                if identifier in phase6_lossless_overrides:
                    continue
                kind = str(record["kind"])
                if kind in ("handshake_response", "handshake_request"):
                    document: object = {
                        "frame_type": "http_handshake",
                        "http_status": record.get("http_status"),
                        "headers": record.get("payload"),
                    }
                elif kind == "close":
                    document = {
                        "frame_type": "close",
                        "close_code": record.get("close_code"),
                        "frame_hex": record.get("frame_hex", ""),
                        "payload": record.get("payload"),
                    }
                elif isinstance(record.get("payload"), dict):
                    document = copy.deepcopy(record["payload"])
                else:
                    document = {"frame_type": kind,
                                "payload": copy.deepcopy(record.get("payload"))}
                direct_stream_documents.setdefault(identifier, []).append(
                    document)
                direct_stream_metadata[identifier] = (
                    scenario_name, direction_name, client_name)
            for identifier, documents in direct_stream_documents.items():
                scenario_name, direction_name, client_name = (
                    direct_stream_metadata[identifier])
                expected_frames = sum(phase6_document_is_frame(document)
                                      for document in documents)
                phase6_lossless_overrides[identifier] = (
                    build_phase6_lossless_stream(
                        identifier, scenario_name, direction_name, client_name,
                        documents, len(documents), expected_frames))
            lossless_streams = sorted(
                phase6_lossless_overrides.values(),
                key=lambda item: str(item["stream_id"]))
            transcript_document = {
                "schema": "graphx.dashboard.phase6-wire-transcript.v1",
                "captured_at": datetime.now(timezone.utc).isoformat().replace(
                    "+00:00", "Z"),
                "limits": {"max_records": 1024,
                           "max_encoded_bytes": 16777216,
                           "max_payload_bytes": 262144,
                           "max_streams": 128,
                           "max_chunks_per_stream": 64,
                           "max_chunk_uncompressed_bytes": 524288,
                           "max_chunk_encoded_bytes": 524288},
                "redaction_policy": (
                    "synthetic loopback protocol evidence only; no cookies, "
                    "authorization headers, IQ, truth, or user data"),
                "records": phase6_records,
                "lossless_streams": lossless_streams,
                "proof": {
                    "two_client_sequence": int(first_event["sequence"]),
                    "resume_from_sequence": resume_sequence,
                    "replayed_sequences": [int(replay_event["sequence"])],
                    "replay_contiguous": int(replay_event["sequence"]) ==
                        resume_sequence + 1,
                    "old_publisher_epoch": old_epoch,
                    "new_publisher_epoch": restarted_hello["publisher_epoch"],
                    "publisher_epoch_changed":
                        restarted_hello["publisher_epoch"] != old_epoch,
                    "negative_close_codes": negative_proof,
                    "required_scenarios": [
                        "two_client", "within_retention_replay", "heartbeat",
                        "stalled_websocket", "slow_client_overflow",
                        "websocket_outage", "cross_origin", "missing_origin",
                        "malformed_resume", "sequence_ahead", "idle_no_pong",
                        "bounded_shutdown", "publisher_restart"],
                    "lossless_stream_ids": [
                        str(item["stream_id"]) for item in lossless_streams],
                    "lossless_streams_complete": True,
                },
            }
            transcript_bytes = (json.dumps(
                transcript_document, indent=2, sort_keys=True) + "\n").encode()
            transcript_path = output / "phase6-wire-transcript.json"
            transcript_path.write_bytes(transcript_bytes)
            transcript_valid = validate_phase6_wire_transcript(
                transcript_document)
            transcript_negative_corpus_valid = (
                validate_phase6_wire_transcript_negative_corpus(
                    transcript_document))
            check("bounded complete Phase6 wire transcript",
                  transcript_valid and transcript_negative_corpus_valid and
                  len(transcript_bytes) <= 16777216,
                  f"records={len(phase6_records)}; streams={len(lossless_streams)}; "
                  f"bytes={len(transcript_bytes)}; "
                  f"schema={transcript_valid}; negative_corpus="
                  f"{transcript_negative_corpus_valid}")
            if not transcript_valid or not transcript_negative_corpus_valid:
                raise RuntimeError("Phase6 wire transcript failed validation")
            persist("phase6_wire_transcript", transcript_bytes)
            phase6_screenshot_manifest = {
                "schema": "graphx.dashboard.phase6.screenshot_manifest.v1",
                "dashboard_url": url, "capture_required": True,
                "synthetic_data_only": True,
                "required_cases": ["live", "replay", "resync"],
                "captured_files": {},
                "capture_mechanism":
                    "direct Firefox WebDriver BiDi screenshot and console acquisition",
            }
            phase6_manifest_bytes = (
                json.dumps(phase6_screenshot_manifest, indent=2) + "\n").encode()
            (output / "phase6-screenshot-manifest.json").write_bytes(
                phase6_manifest_bytes)
            persist("phase6_screenshot_manifest", phase6_manifest_bytes)
        if phase >= 7:
            phase7_stage_durations: dict[str, dict[str, object]] = {}
            phase7_timing_path = output / "phase7-stage-durations.json"

            def record_phase7_duration(name: str, started: float,
                                       state: object) -> None:
                phase7_stage_durations[name] = {
                    "elapsed_seconds":time.monotonic() - started,
                    "terminal_state":state,
                    "recorded_at":datetime.now(timezone.utc).isoformat().replace(
                        "+00:00", "Z")}
                phase7_timing_path.write_text(json.dumps({
                    "schema":"graphx.dashboard.phase7.stage_durations.v1",
                    "stages":phase7_stage_durations}, indent=2) + "\n",
                    encoding="utf-8")

            def submit_investigation(target: str, payload: dict[str, object],
                                     key: str) -> tuple[int, dict[str, str], dict[str, object]]:
                code, response_headers, response_body = request(
                    port, "POST", target, json.dumps(payload).encode(),
                    {"Content-Type":"application/json", "Idempotency-Key":key},
                    timeout=10)
                return code, response_headers, json.loads(response_body)

            def wait_investigation(operation_id: str,
                                   timeout_seconds: float = 90) -> dict[str, object]:
                deadline = time.monotonic() + timeout_seconds
                latest: dict[str, object] = {}
                while time.monotonic() < deadline:
                    code, _, payload = request(
                        port, "GET",
                        f"/api/v1/fhss/investigations/operations/{operation_id}")
                    if code != 200:
                        raise RuntimeError(f"investigation lookup failed: HTTP {code}")
                    latest = json.loads(payload)
                    if latest.get("state") in (
                            "completed", "cancelled", "failed", "timed_out"):
                        return latest
                    time.sleep(0.02)
                raise RuntimeError(
                    f"investigation did not terminate: {operation_id}; {latest}")

            p7_source_started = time.monotonic()
            p7_job_status, _, p7_job_body, p7_job = submit_job(
                "/api/v1/fhss/commands/step",
                {"request_id":"phase7-source-job", "timeout_ms":60000},
                "phase7-source-job-key")
            persist("phase7_source_job_submit", p7_job_body)
            p7_terminal = wait_job(str(p7_job["job_id"]), 70)
            record_phase7_duration(
                "primary_source_job", p7_source_started,
                p7_terminal.get("state"))
            persist("phase7_source_job_terminal",
                    json.dumps(p7_terminal, sort_keys=True).encode())
            check("Phase7 committed source job",
                  p7_job_status == 202 and p7_terminal.get("state") == "completed",
                  json.dumps(p7_terminal.get("terminal"), sort_keys=True))
            job_id = str(p7_terminal["job_id"])
            estimated_bytes = int(p7_terminal["artifacts"]["iq"]["bytes"])

            unconfirmed_status, unconfirmed_headers, unconfirmed = submit_investigation(
                "/api/v1/fhss/investigations/exports",
                {"request_id":"phase7-copy-estimate","bundle_name":"phase7-unconfirmed",
                 "job_id":job_id,"iq_mode":"copy"}, "phase7-copy-estimate-key")
            check("Phase7 copy requires confirmation with byte estimate",
                  unconfirmed_status == 428 and
                  unconfirmed_headers.get("content-type", "").startswith(
                      "application/problem+json") and
                  int(unconfirmed.get("estimated_iq_bytes", -1)) == estimated_bytes,
                  json.dumps(unconfirmed, sort_keys=True))

            def export_bundle(name: str, mode: str) -> dict[str, object]:
                stage_started = time.monotonic()
                payload: dict[str, object] = {
                    "request_id":f"phase7-export-{mode}", "bundle_name":name,
                    "job_id":job_id,"iq_mode":mode,"timeout_ms":120000}
                if mode == "copy": payload["confirm_copy"] = True
                code, _, submitted = submit_investigation(
                    "/api/v1/fhss/investigations/exports", payload,
                    f"phase7-export-{mode}-key")
                if code != 202: raise RuntimeError(f"{mode} export HTTP {code}: {submitted}")
                try:
                    terminal = wait_investigation(
                        str(submitted["operation_id"]), 120)
                except Exception:
                    record_phase7_duration(
                        f"{name}_{mode}_export", stage_started,
                        "operator_wait_failed")
                    raise
                record_phase7_duration(
                    f"{name}_{mode}_export", stage_started,
                    terminal.get("state"))
                return terminal

            def operation(name: str, kind: str, qualifier: str = "") -> dict[str, object]:
                stage_started = time.monotonic()
                route = ("import-validations" if kind == "validate" else "replays")
                identity = f"{kind}-{name}{qualifier}"
                code, _, submitted = submit_investigation(
                    f"/api/v1/fhss/investigations/{route}",
                    {"request_id":f"phase7-{identity}","bundle_name":name,
                     "timeout_ms":120000}, f"phase7-{identity}-key")
                if code != 202: raise RuntimeError(f"{kind} HTTP {code}: {submitted}")
                try:
                    terminal = wait_investigation(
                        str(submitted["operation_id"]), 120)
                except Exception:
                    record_phase7_duration(
                        f"{name}_{kind}{qualifier}", stage_started,
                        "operator_wait_failed")
                    raise
                record_phase7_duration(
                    f"{name}_{kind}{qualifier}", stage_started,
                    terminal.get("state"))
                return terminal

            reference_export = export_bundle("phase7-reference", "reference")
            reference_validation = operation("phase7-reference", "validate")
            reference_replay = operation("phase7-reference", "replay")
            copied_export = export_bundle("phase7-copied", "copy")
            copied_validation = operation("phase7-copied", "validate")
            copied_replay = operation("phase7-copied", "replay")
            browser_evidence: dict[str, object] = {}
            browser_console: list[dict[str, object]] = []

            def capture_investigation_states(
                    cases: tuple[tuple[str, dict[str, object]], ...]) -> None:
                with FirefoxBidiSession(output) as browser:
                    browser.navigate(url)
                    browser.evaluate(
                        "document.getElementById('tab-graph').click(); true")
                    for label, selected_operation in cases:
                        operation_id = str(selected_operation["operation_id"])
                        browser.evaluate(
                            f"selectedInvestigationOperationId={json.dumps(operation_id)}; "
                            "refreshInvestigations(); true")
                        browser.wait_for(
                            "document.getElementById('investigation-identity').textContent"
                            f".includes({json.dumps(operation_id)})")
                        browser.evaluate("""
                          (() => {
                            const target=document.getElementById('investigation-identity');
                            target.scrollIntoView({block:'start',inline:'nearest'});
                            return true;
                          })()
                        """)
                        served = json.loads(str(browser.evaluate("""
                          (() => {
                            const panel=document.getElementById('investigation-controls');
                            const rect=panel.getBoundingClientRect();
                            const identity=document.getElementById('investigation-identity');
                            const identityRect=identity.getBoundingClientRect();
                            return JSON.stringify({
                              identity:identity.textContent,
                              state:document.getElementById('investigation-state').textContent,
                              panel_text:panel.innerText,
                              panel_rect:{top:rect.top,bottom:rect.bottom,left:rect.left,
                                          right:rect.right,width:rect.width,height:rect.height},
                              identity_rect:{top:identityRect.top,bottom:identityRect.bottom,
                                             left:identityRect.left,right:identityRect.right,
                                             width:identityRect.width,height:identityRect.height},
                              viewport:{width:innerWidth,height:innerHeight},
                              panel_visible:rect.width>0 && rect.height>0 && rect.bottom>0 &&
                                            rect.top<innerHeight && rect.right>0 && rect.left<innerWidth,
                              visible:identityRect.width>0 && identityRect.height>0 &&
                                      identityRect.bottom>0 && identityRect.top<innerHeight &&
                                      identityRect.right>0 && identityRect.left<innerWidth
                            });
                          })()
                        """)))
                        if (not served.get("visible") or
                                operation_id not in str(served.get("panel_text", ""))):
                            raise RuntimeError(
                                f"Phase7 investigation state is not visibly rendered: {served}")
                        served.update({
                            "operation_id": operation_id,
                            "dashboard_url": url,
                            "browser_session_id": browser.session_id,
                            "browser_context_id": browser.context,
                            "browser_name": browser.capabilities.get("browserName"),
                            "browser_version": browser.capabilities.get("browserVersion"),
                            "captured_at": datetime.now(timezone.utc).isoformat().replace(
                                "+00:00", "Z"),
                        })
                        screenshot = output / f"phase7-investigation-{label}.png"
                        browser.screenshot(screenshot)
                        served["screenshot_sha256"] = sha256(screenshot)
                        evidence_path = output / f"phase7-investigation-{label}.json"
                        evidence_path.write_text(json.dumps(served, indent=2) + "\n")
                        persist(f"phase7_browser_screenshot:{label}",
                                screenshot.read_bytes())
                        persist(f"phase7_browser_state:{label}",
                                evidence_path.read_bytes())
                        browser_evidence[label] = served
                    browser_console.extend(browser.messages)

            # Capture these terminal operations before the bounded history is
            # intentionally filled by the adversarial import matrix.
            capture_investigation_states((
                ("reference-completed", reference_export),
                ("copy-completed", copied_export),
                ("replay-success", copied_replay),
            ))
            for label, document in (
                    ("reference_export", reference_export),
                    ("reference_validation", reference_validation),
                    ("reference_replay", reference_replay),
                    ("copied_export", copied_export),
                    ("copied_validation", copied_validation),
                    ("copied_replay", copied_replay)):
                persist("phase7_" + label,
                        json.dumps(document, sort_keys=True).encode())
                check("Phase7 " + label.replace("_", " "),
                      document.get("state") == "completed",
                      json.dumps(document.get("terminal"), sort_keys=True))
            check("Phase7 deterministic replay hashes agree",
                  reference_replay.get("result", {}).get(
                      "semantic_receiver_result_sha256") ==
                  copied_replay.get("result", {}).get(
                      "semantic_receiver_result_sha256") and
                  reference_replay.get("result", {}).get("matches_expected") is True and
                  copied_replay.get("result", {}).get("matches_expected") is True,
                  json.dumps({"reference":reference_replay.get("result"),
                              "copy":copied_replay.get("result")}, sort_keys=True))

            bundle_root = output / "phase5-job-artifacts" / "fhss-investigations"
            official_schema = json.loads((Path(__file__).resolve().parents[1] /
                "sigmf/official-v1.2.6/sigmf-schema.json").read_text())
            official_validator = Draft202012Validator(
                official_schema, format_checker=FormatChecker())
            bundle_schema_documents = [json.loads(path.read_text())
                for path in sorted((API_DIR / "schemas").glob("fhss-*.schema.json"))]
            bundle_registry = Registry()
            for schema_document in bundle_schema_documents:
                if "$id" in schema_document:
                    bundle_registry = bundle_registry.with_resource(
                        schema_document["$id"], Resource.from_contents(schema_document))
            bundle_schemas = {document["$id"]: document
                              for document in bundle_schema_documents
                              if "$id" in document}
            bundle_schema_by_identity = {
                "graphx.dashboard.fhss_investigation_manifest.v1":
                    "urn:graphx:dashboard:fhss-investigation-manifest:v1",
                "graphx.fhss.iq-truth.v1": "urn:graphx:fhss:iq-truth:v1",
                "graphx.dashboard.fhss_receiver_observation.v1":
                    "urn:graphx:dashboard:fhss-receiver-observation:v1",
                "graphx.dashboard.fhss_comparison_result.v1":
                    "urn:graphx:dashboard:fhss-comparison-result:v1",
                "graphx.dashboard.receiver_graph.v1":
                    "urn:graphx:dashboard:fhss-receiver-graph:v1",
                "graphx.dashboard.fhss_receiver_result.v1":
                    "urn:graphx:dashboard:fhss-receiver-result:v1",
                "graphx.dashboard.fhss_investigation_provenance.v1":
                    "urn:graphx:dashboard:fhss-investigation-provenance:v1",
                "graphx.dashboard.fhss_operator_actions.v1":
                    "urn:graphx:dashboard:fhss-operator-actions:v1",
                "graphx.dashboard.fhss_external_iq_reference.v1":
                    "urn:graphx:dashboard:fhss-external-iq-reference:v1",
                "graphx.dashboard.fhss_build_api_manifest.v1":
                    "urn:graphx:dashboard:fhss-build-api-manifest:v1",
            }

            def validate_bundle_document(document: dict[str, object]) -> None:
                schema_id = bundle_schema_by_identity.get(str(document.get("schema", "")))
                if schema_id is None or schema_id not in bundle_schemas:
                    raise RuntimeError(
                        f"no pinned bundle schema for {document.get('schema')}")
                Draft202012Validator(
                    bundle_schemas[schema_id], registry=bundle_registry,
                    format_checker=FormatChecker()).validate(document)
            for name, mode in (("phase7-reference", "reference"),
                               ("phase7-copied", "copy")):
                directory = bundle_root / name
                manifest_bytes = (directory / "manifest.json").read_bytes()
                manifest = json.loads(manifest_bytes)
                validate_bundle_document(manifest)
                detached = (directory / "manifest.sha256").read_text().strip()
                sigmf = json.loads((directory / "recording.sigmf-meta").read_text())
                official_validator.validate(sigmf)
                for artifact in manifest["artifacts"]:
                    if artifact["media_type"] != "application/json" or \
                            artifact["path"] == "recording.sigmf-meta":
                        continue
                    validate_bundle_document(json.loads(
                        (directory / artifact["path"]).read_text()))
                artifact_paths = {entry["path"] for entry in manifest["artifacts"]}
                check(f"Phase7 {mode} canonical manifest and detached hash",
                      manifest_bytes == (json.dumps(manifest, sort_keys=True,
                          separators=(",", ":")) + "\n").encode() and
                      detached == hashlib.sha256(manifest_bytes).hexdigest(), detached)
                check(f"Phase7 {mode} inventory and SigMF identity",
                      manifest["iq_mode"] == mode and
                      manifest["self_contained"] is (mode == "copy") and
                      manifest["receiver_truth_access"] == "none" and
                      "truth.json" in artifact_paths and
                      "observation.json" in artifact_paths and
                      "comparison.json" in artifact_paths and
                      "receiver-config.json" in artifact_paths and
                      (("recording.sigmf-data" in artifact_paths) is
                       (mode == "copy")) and
                      sigmf["global"]["core:sha512"] == manifest["iq_sha512"],
                      json.dumps(sorted(artifact_paths)))
                persist(f"phase7_{mode}_manifest", manifest_bytes)
                persist(f"phase7_{mode}_sigmf",
                        (directory / "recording.sigmf-meta").read_bytes())

            copied_iq = bundle_root / "phase7-copied" / "recording.sigmf-data"
            with copied_iq.open("r+b", buffering=0) as stream:
                original_first = stream.read(1)
                if len(original_first) != 1:
                    raise RuntimeError("copied IQ is unexpectedly empty")
                stream.seek(0)
                stream.write(bytes([original_first[0] ^ 1]))
            tamper = operation("phase7-copied", "validate", "-tampered")
            with copied_iq.open("r+b", buffering=0) as stream:
                stream.write(original_first)
            check("Phase7 tampered IQ rejected before replay",
                  tamper.get("state") == "failed" and
                  tamper.get("terminal", {}).get("code") ==
                      "investigation_validation_failed",
                  json.dumps(tamper.get("terminal"), sort_keys=True))

            copied_directory = bundle_root / "phase7-copied"
            canonical = lambda value: (json.dumps(
                value, sort_keys=True, separators=(",", ":")) + "\n").encode()

            def expect_validation_failure(
                    label: str, check_label: str | None = None) -> dict[str, object]:
                result = operation("phase7-copied", "validate", "-" + label)
                check("Phase7 rejects " + (check_label or label.replace("-", " ")),
                      result.get("state") == "failed" and
                      result.get("terminal", {}).get("code") ==
                          "investigation_validation_failed",
                      json.dumps(result.get("terminal"), sort_keys=True))
                return result

            # Raw byte tampering proves every distinct artifact digest is
            # enforced. Restore exact bytes after each operation.
            for artifact_name, label in (
                    ("recording.sigmf-meta", "modified-metadata"),
                    ("truth.json", "modified-truth"),
                    ("observation.json", "modified-observation"),
                    ("comparison.json", "modified-comparison"),
                    ("receiver-config.json", "modified-receiver-config"),
                    ("receiver-result.json", "modified-receiver-result"),
                    ("provenance.json", "modified-build-provenance"),
                    ("actions.json", "modified-action-log"),
                    ("build-api.json", "modified-build-api-manifest")):
                path = copied_directory / artifact_name
                original = path.read_bytes()
                path.write_bytes(original + b" ")
                expect_validation_failure(label)
                path.write_bytes(original)

            manifest_path = copied_directory / "manifest.json"
            detached_path = copied_directory / "manifest.sha256"
            metadata_path = copied_directory / "recording.sigmf-meta"
            original_manifest_bytes = manifest_path.read_bytes()
            original_detached_bytes = detached_path.read_bytes()
            original_metadata_bytes = metadata_path.read_bytes()

            def publish_modified_manifest(manifest: dict[str, object]) -> None:
                encoded = canonical(manifest)
                manifest_path.write_bytes(encoded)
                detached_path.write_text(hashlib.sha256(encoded).hexdigest() + "\n")

            def publish_modified_metadata(mutator) -> None:
                metadata = json.loads(original_metadata_bytes)
                mutator(metadata)
                metadata_bytes = canonical(metadata)
                metadata_path.write_bytes(metadata_bytes)
                manifest = json.loads(original_manifest_bytes)
                entry = next(item for item in manifest["artifacts"]
                             if item["path"] == "recording.sigmf-meta")
                entry["bytes"] = len(metadata_bytes)
                entry["sha256"] = hashlib.sha256(metadata_bytes).hexdigest()
                entry["sha512"] = hashlib.sha512(metadata_bytes).hexdigest()
                publish_modified_manifest(manifest)

            def restore_semantic_files() -> None:
                manifest_path.write_bytes(original_manifest_bytes)
                detached_path.write_bytes(original_detached_bytes)
                metadata_path.write_bytes(original_metadata_bytes)

            semantic_metadata_cases = (
                ("official-schema-extra-property", lambda value:
                    value.update({"unexpected":True})),
                ("unsupported-datatype", lambda value:
                    value["global"].update({"core:datatype":"ci16_le"})),
                ("nonpositive-sample-rate", lambda value:
                    value["global"].update({"core:sample_rate":0})),
                ("negative-center-frequency", lambda value:
                    value["captures"][0].update({"core:frequency":-1})),
                ("capture-out-of-range", lambda value:
                    value["captures"][0].update({"core:sample_start":999999999})),
                ("annotation-out-of-range", lambda value:
                    value["annotations"][0].update({"core:sample_count":999999999})),
                ("dataset-sha512-mismatch", lambda value:
                    value["global"].update({"core:sha512":"0" * 128})),
                ("unsupported-sigmf-version", lambda value:
                    value["global"].update({"core:version":"9.9.9"})),
                ("metadata-only-masquerade", lambda value:
                    value["global"].update({"core:metadata_only":True})),
            )
            for label, mutator in semantic_metadata_cases:
                publish_modified_metadata(mutator)
                expect_validation_failure(label)
                restore_semantic_files()

            provenance_path = copied_directory / "provenance.json"
            original_provenance_bytes = provenance_path.read_bytes()
            committed_identity_cases = (
                ("job-id-rehash", "rehashed source job id disagreement",
                 "source_job_id",
                 "j-000000000000000000000008"),
                ("request-id-rehash", "rehashed source job request id disagreement",
                 "source_job_request_id",
                 "operator-substituted-request"),
                ("epoch-rehash", "rehashed controller epoch disagreement",
                 "controller_epoch",
                 int(json.loads(original_provenance_bytes)["controller_epoch"]) + 1),
                ("scenario-id-rehash", "rehashed scenario correlation id disagreement",
                 "scenario_correlation_id",
                 "s-000000000000000000000008"),
            )
            for label, check_label, field, replacement in committed_identity_cases:
                inconsistent_provenance = json.loads(original_provenance_bytes)
                inconsistent_provenance[field] = replacement
                inconsistent_bytes = canonical(inconsistent_provenance)
                provenance_path.write_bytes(inconsistent_bytes)
                inconsistent_manifest = json.loads(original_manifest_bytes)
                inconsistent_entry = next(
                    item for item in inconsistent_manifest["artifacts"]
                    if item["path"] == "provenance.json")
                inconsistent_entry.update({
                    "bytes": len(inconsistent_bytes),
                    "sha256": hashlib.sha256(inconsistent_bytes).hexdigest(),
                    "sha512": hashlib.sha512(inconsistent_bytes).hexdigest()})
                publish_modified_manifest(inconsistent_manifest)
                expect_validation_failure(label, check_label)
                provenance_path.write_bytes(original_provenance_bytes)
                restore_semantic_files()

            for label, mutate_manifest in (
                    ("sample-count-mismatch", lambda value:
                        value.update({"sample_count":int(value["sample_count"]) + 1})),
                    ("unsupported-bundle-version", lambda value:
                        value.update({"bundle_format_version":2})),
                    ("unsupported-manifest-schema", lambda value:
                        value.update({"schema":"graphx.dashboard.fhss_investigation_manifest.v999"})),
                    ("absolute-artifact-path", lambda value:
                        value["artifacts"][0].update({"path":"/tmp/truth.json"})),
                    ("sibling-prefix-artifact-path", lambda value:
                        value["artifacts"][0].update({"path":"../fhss-investigations-evil/truth.json"})),
                    ("duplicate-manifest-entry", lambda value:
                        value["artifacts"].append(copy.deepcopy(value["artifacts"][0])))):
                modified = json.loads(original_manifest_bytes)
                mutate_manifest(modified)
                publish_modified_manifest(modified)
                expect_validation_failure(label)
                restore_semantic_files()

            missing_path = copied_directory / "truth.json"
            missing_backup = output / "phase7-truth-backup.json"
            shutil.copyfile(missing_path, missing_backup)
            missing_path.unlink()
            expect_validation_failure("missing-declared-file")
            shutil.copyfile(missing_backup, missing_path)
            missing_backup.unlink()
            extra_path = copied_directory / "unmanifested.json"
            extra_path.write_text("{}\n")
            expect_validation_failure("extra-unmanifested-file")
            extra_path.unlink()

            regular_path = copied_directory / "truth.json"
            regular_backup = output / "phase7-regular-backup.json"
            shutil.copyfile(regular_path, regular_backup)
            regular_path.unlink()
            os.link(regular_backup, regular_path)
            expect_validation_failure("hardlink-substitution")
            regular_path.unlink(); shutil.copyfile(regular_backup, regular_path)
            regular_path.unlink(); regular_path.symlink_to(regular_backup)
            expect_validation_failure("symlink-file-substitution")
            regular_path.unlink(); shutil.copyfile(regular_backup, regular_path)
            regular_path.unlink(); os.mkfifo(regular_path)
            expect_validation_failure("fifo-special-file-substitution")
            regular_path.unlink(); shutil.copyfile(regular_backup, regular_path)
            regular_backup.unlink()

            # Rebind the reference record to unsafe components while keeping
            # all outer hashes consistent, proving containment is independent
            # of manifest integrity.
            reference_directory = bundle_root / "phase7-reference"
            reference_record_path = reference_directory / "external-iq-reference.json"
            reference_manifest_path = reference_directory / "manifest.json"
            reference_detached_path = reference_directory / "manifest.sha256"
            reference_record_original = reference_record_path.read_bytes()
            reference_manifest_original = reference_manifest_path.read_bytes()
            reference_detached_original = reference_detached_path.read_bytes()
            unsafe_reference = json.loads(reference_record_original)
            unsafe_reference["relative_components"] = ["..", "outside.cf32"]
            unsafe_reference_bytes = canonical(unsafe_reference)
            reference_record_path.write_bytes(unsafe_reference_bytes)
            reference_manifest = json.loads(reference_manifest_original)
            reference_entry = next(item for item in reference_manifest["artifacts"]
                                   if item["path"] == "external-iq-reference.json")
            reference_entry.update({
                "bytes":len(unsafe_reference_bytes),
                "sha256":hashlib.sha256(unsafe_reference_bytes).hexdigest(),
                "sha512":hashlib.sha512(unsafe_reference_bytes).hexdigest()})
            encoded_reference_manifest = canonical(reference_manifest)
            reference_manifest_path.write_bytes(encoded_reference_manifest)
            reference_detached_path.write_text(
                hashlib.sha256(encoded_reference_manifest).hexdigest() + "\n")
            unsafe_reference_result = operation(
                "phase7-reference", "validate", "-out-of-root")
            check("Phase7 rejects out-of-root external IQ reference",
                  unsafe_reference_result.get("state") == "failed",
                  json.dumps(unsafe_reference_result.get("terminal"), sort_keys=True))
            reference_record_path.write_bytes(reference_record_original)
            reference_manifest_path.write_bytes(reference_manifest_original)
            reference_detached_path.write_bytes(reference_detached_original)

            collision_code, _, collision_submit = submit_investigation(
                "/api/v1/fhss/investigations/exports",
                {"request_id":"phase7-collision","bundle_name":"phase7-copied",
                 "job_id":job_id,"iq_mode":"reference","timeout_ms":30000},
                "phase7-collision-key")
            collision_result = (wait_investigation(str(collision_submit["operation_id"]), 30)
                                if collision_code == 202 else collision_submit)
            check("Phase7 no-overwrite collision policy",
                  collision_code == 202 and collision_result.get("state") == "failed",
                  json.dumps(collision_result.get("terminal"), sort_keys=True))
            restored_validation = operation(
                "phase7-copied", "validate", "-restored-after-failures")
            check("Phase7 prior committed bundle survives later failures",
                  restored_validation.get("state") == "completed",
                  json.dumps(restored_validation.get("terminal"), sort_keys=True))
            traversal_status, _, traversal = submit_investigation(
                "/api/v1/fhss/investigations/import-validations",
                {"request_id":"phase7-traversal","bundle_name":"../outside"},
                "phase7-traversal-key")
            check("Phase7 traversal rejected synchronously",
                  traversal_status == 400, json.dumps(traversal, sort_keys=True))
            outside = output / "phase7-outside"
            outside.mkdir(exist_ok=True)
            symlink = bundle_root / "phase7-symlink"
            symlink.symlink_to(outside, target_is_directory=True)
            symlink_rejection = operation("phase7-symlink", "validate")
            check("Phase7 symlink bundle rejected",
                  symlink_rejection.get("state") == "failed",
                  json.dumps(symlink_rejection.get("terminal"), sort_keys=True))
            symlink.unlink()

            # A separate public executable instance enables a fixed,
            # startup-only qualification sequence. No fault is selected by an
            # HTTP request, and production startup never enables this profile.
            qualification_output = output / "phase7-qualification"
            qualification_output.mkdir()
            qualification_args = copy.copy(args)
            qualification_args.output_dir = qualification_output
            qualification_args.investigation_qualification = True
            qualification_process = None
            qualification_port = 0
            try:
                qualification_process, _, qualification_port, qualification_command = (
                    launch(qualification_args))
                additional_commands.append(qualification_command)

                def q_request(method: str, target: str,
                              payload: dict[str, object] | None = None,
                              key: str | None = None) -> tuple[int, dict[str, object]]:
                    headers = {"Content-Type":"application/json"}
                    if key is not None:
                        headers["Idempotency-Key"] = key
                    code, _, body = request(
                        qualification_port, method, target,
                        None if payload is None else json.dumps(payload).encode(),
                        headers if payload is not None else None, timeout=10)
                    return code, json.loads(body)

                q_source_started = time.monotonic()
                q_job_code, q_job_submit = q_request(
                    "POST", "/api/v1/fhss/commands/step",
                    {"request_id":"phase7-qualification-source",
                     "timeout_ms":60000}, "phase7-qualification-source-key")
                q_job_id = str(q_job_submit["job_id"])
                q_deadline = time.monotonic() + 70
                q_job_terminal: dict[str, object] = {}
                while time.monotonic() < q_deadline:
                    q_code, q_job_terminal = q_request(
                        "GET", f"/api/v1/fhss/jobs/{q_job_id}")
                    if q_code == 200 and q_job_terminal.get("state") in (
                            "completed", "cancelled", "failed", "timed_out"):
                        break
                    time.sleep(0.02)
                check("Phase7 qualification source job completed",
                      q_job_code == 202 and q_job_terminal.get("state") == "completed",
                      json.dumps(q_job_terminal.get("terminal"), sort_keys=True))
                record_phase7_duration(
                    "qualification_source_job", q_source_started,
                    q_job_terminal.get("state"))

                q_bundle_root = (qualification_output /
                                 "phase5-job-artifacts" /
                                 "fhss-investigations")

                def q_submit_export(sequence: int, mode: str,
                                    timeout_ms: int = 30000) -> dict[str, object]:
                    name = f"qualification-{sequence}"
                    payload: dict[str, object] = {
                        "request_id":name, "bundle_name":name,
                        "job_id":q_job_id, "iq_mode":mode,
                        "timeout_ms":timeout_ms}
                    if mode == "copy":
                        payload["confirm_copy"] = True
                    code, submitted = q_request(
                        "POST", "/api/v1/fhss/investigations/exports",
                        payload, f"{name}-key")
                    if code != 202:
                        raise RuntimeError(
                            f"qualification export {sequence} rejected: {code} {submitted}")
                    return submitted

                def q_wait(operation_id: str, terminal: bool = True,
                           state: str | None = None,
                           bound: float = 8) -> dict[str, object]:
                    deadline = time.monotonic() + bound
                    latest: dict[str, object] = {}
                    while time.monotonic() < deadline:
                        code, latest = q_request(
                            "GET", "/api/v1/fhss/investigations/operations/" +
                            operation_id)
                        if code != 200:
                            raise RuntimeError("qualification operation lookup failed")
                        if ((terminal and latest.get("state") in
                             ("completed", "cancelled", "failed", "timed_out")) or
                                (state is not None and latest.get("state") == state)):
                            return latest
                        time.sleep(0.005)
                    raise RuntimeError(
                        f"qualification operation did not reach target: {latest}")

                def q_cleanup(sequence: int) -> bool:
                    name = f"qualification-{sequence}"
                    return (not (q_bundle_root / name).exists() and
                            not any(q_bundle_root.glob(".tmp-*")))

                for sequence, mode, expected_code, label in (
                        (1, "reference", "investigation_quota_exceeded",
                         "quota exceeded"),
                        (2, "copy", "investigation_quota_exceeded",
                         "copied-IQ size limit"),
                        (3, "copy", "artifact_enospc", "deterministic ENOSPC")):
                    qualification_stage_started = time.monotonic()
                    submitted = q_submit_export(sequence, mode)
                    terminal = q_wait(str(submitted["operation_id"]))
                    record_phase7_duration(
                        f"qualification_export_{sequence}",
                        qualification_stage_started, terminal.get("state"))
                    persist(f"phase7_qualification_{sequence}_{label}",
                            json.dumps(terminal, sort_keys=True).encode())
                    check(f"Phase7 external {label} transition and cleanup",
                          terminal.get("state") == "failed" and
                          terminal.get("terminal", {}).get("code") == expected_code and
                          q_cleanup(sequence),
                          json.dumps(terminal.get("terminal"), sort_keys=True))

                for sequence, mode, active_state, label in (
                        (4, "reference", "hashing", "cancel during hashing"),
                        (5, "copy", "copying", "cancel during copying"),
                        (6, "reference", "publishing", "cancel before rename")):
                    qualification_stage_started = time.monotonic()
                    submitted = q_submit_export(sequence, mode)
                    operation_id = str(submitted["operation_id"])
                    observed = q_wait(operation_id, terminal=False,
                                      state=active_state)
                    cancel_code, accepted = q_request(
                        "POST", "/api/v1/fhss/investigations/operations/" +
                        operation_id + "/cancel", {})
                    terminal = q_wait(operation_id)
                    record_phase7_duration(
                        f"qualification_export_{sequence}",
                        qualification_stage_started, terminal.get("state"))
                    persist(f"phase7_qualification_{sequence}_{label}",
                            json.dumps({"observed":observed, "accepted":accepted,
                                        "terminal":terminal},
                                       sort_keys=True).encode())
                    check(f"Phase7 external {label} transition and cleanup",
                          cancel_code == 202 and terminal.get("state") == "cancelled" and
                          q_cleanup(sequence),
                          json.dumps(terminal.get("terminal"), sort_keys=True))

                qualification_stage_started = time.monotonic()
                timeout_submit = q_submit_export(7, "reference", 100)
                timeout_terminal = q_wait(str(timeout_submit["operation_id"]))
                record_phase7_duration(
                    "qualification_export_7", qualification_stage_started,
                    timeout_terminal.get("state"))
                persist("phase7_qualification_timeout",
                        json.dumps(timeout_terminal, sort_keys=True).encode())
                check("Phase7 external deterministic timeout and cleanup",
                      timeout_terminal.get("state") == "timed_out" and
                      timeout_terminal.get("terminal", {}).get("code") ==
                          "operation_timeout" and q_cleanup(7),
                      json.dumps(timeout_terminal.get("terminal"), sort_keys=True))

                qualification_stage_started = time.monotonic()
                shutdown_submit = q_submit_export(8, "reference")
                q_wait(str(shutdown_submit["operation_id"]), terminal=False,
                       state="hashing")
                shutdown_started = time.monotonic()
                qualification_process.send_signal(signal.SIGTERM)
                qualification_process.wait(timeout=5)
                shutdown_elapsed = time.monotonic() - shutdown_started
                qualification_process = None
                persist("phase7_qualification_shutdown",
                        json.dumps({"elapsed_seconds":shutdown_elapsed,
                                    "operation_id":shutdown_submit["operation_id"],
                                    "destination_absent":q_cleanup(8)},
                                   sort_keys=True).encode())
                record_phase7_duration(
                    "qualification_export_8", qualification_stage_started,
                    "process_shutdown")
                check("Phase7 bounded active-export shutdown and cleanup",
                      shutdown_elapsed <= 5 and q_cleanup(8),
                      f"elapsed={shutdown_elapsed:.3f}s")
            finally:
                if qualification_process is not None:
                    stop(qualification_process)

            capture_investigation_states((("safe-failed", symlink_rejection),))
            persist("phase7_browser_console", json.dumps(browser_console).encode())
            browser_case_labels = ("reference-completed", "copy-completed",
                                   "replay-success", "safe-failed")
            completed_identities = [
                str(browser_evidence[label]["identity"])
                for label in ("reference-completed", "copy-completed")]
            check("Phase7 browser renders four genuine investigation states",
                  all(is_valid_png(output / f"phase7-investigation-{label}.png")
                      for label in browser_case_labels) and
                  all("pending" not in identity and "datatype=cf" in identity and
                      "samples=" in identity and "IQ-SHA512=" in identity and
                      "bytes=" in identity for identity in completed_identities) and
                  '"state": "completed"' in str(browser_evidence["replay-success"]["state"]) and
                  '"state": "failed"' in str(browser_evidence["safe-failed"]["state"]) and
                  all(
                      str(entry.get("level", "")).lower() not in
                      ("warning", "warn", "error") for entry in browser_console),
                  f"captures={len(browser_case_labels)}; "
                  f"console={len(browser_console)}")
            persist("phase7_stage_durations", phase7_timing_path.read_bytes())
            if phase == 8:
                with FirefoxBidiSession(output) as browser:
                    browser.navigate(url)
                    browser.wait_for(
                        "document.getElementById('event-transport').textContent.length > 0")
                    browser.evaluate("document.activeElement.blur(); true")
                    browser.key("\ue004")
                    skip_focused = browser.evaluate(
                        "document.activeElement.classList.contains('skip-link')")
                    browser.key("\ue007")
                    skip_activated = browser.evaluate(
                        "document.activeElement.id === 'dashboard-main'")
                    browser.evaluate("document.getElementById('tab-main').focus(); true")
                    browser.key("\ue014")
                    real_tab_right = browser.evaluate(
                        "document.activeElement.id === 'tab-graph' && "
                        "document.getElementById('panel-graph').classList.contains('active')")
                    browser.key("\ue011")
                    real_tab_home = browser.evaluate(
                        "document.activeElement.id === 'tab-main' && "
                        "document.getElementById('panel-main').classList.contains('active')")
                    browser.evaluate("document.getElementById('tab-graph').click(); "
                                     "document.getElementById('runtime-rebuild').focus(); true")
                    browser.key("\ue007")
                    browser.wait_for(
                        "!document.getElementById('runtime-status').textContent"
                        ".includes('unavailable')", 20)
                    lifecycle_rebuild_text = browser.evaluate(
                        "document.getElementById('runtime-status').textContent")
                    browser.evaluate("document.getElementById('runtime-start').focus(); true")
                    browser.key("\ue007")
                    time.sleep(0.2)
                    browser.evaluate("document.getElementById('runtime-stop').focus(); true")
                    browser.key("\ue007")
                    browser.wait_for(
                        "document.getElementById('runtime-status').textContent.length > 20", 20)
                    lifecycle_stop_text = browser.evaluate(
                        "document.getElementById('runtime-status').textContent")
                    browser.evaluate(
                        "document.getElementById('config-validate').focus(); true")
                    focus_before_refresh = browser.evaluate("document.activeElement.id")
                    time.sleep(1.2)
                    focus_after_refresh = browser.evaluate("document.activeElement.id")
                    browser.evaluate("selectedJob=" + json.dumps(p7_terminal) + "; "
                                     "document.getElementById('investigation-bundle-name')"
                                     ".value='phase8-keyboard-export'; "
                                     "document.getElementById('investigation-export').focus(); true")
                    export_identity_before = str(browser.evaluate(
                        "document.getElementById('investigation-identity').textContent"))
                    browser.key("\ue007")
                    browser.wait_for(
                        "document.getElementById('investigation-identity').textContent !== "
                        + json.dumps(export_identity_before), 20)
                    keyboard_export_identity = browser.evaluate(
                        "document.getElementById('investigation-identity').textContent")
                    keyboard_export_operation = str(browser.evaluate(
                        "selectedInvestigationOperationId"))
                    keyboard_export_terminal = wait_investigation(
                        keyboard_export_operation, 60)
                    validate_identity_before = str(browser.evaluate(
                        "document.getElementById('investigation-identity').textContent"))
                    browser.evaluate(
                        "document.getElementById('investigation-validate').focus(); true")
                    browser.key("\ue007")
                    browser.wait_for(
                        "document.getElementById('investigation-identity').textContent !== "
                        + json.dumps(validate_identity_before), 20)
                    keyboard_validate_identity = browser.evaluate(
                        "document.getElementById('investigation-identity').textContent")
                    keyboard_validate_operation = str(browser.evaluate(
                        "selectedInvestigationOperationId"))
                    keyboard_validate_terminal = wait_investigation(
                        keyboard_validate_operation, 60)
                    replay_identity_before = str(keyboard_validate_identity)
                    browser.evaluate(
                        "document.getElementById('investigation-replay').focus(); true")
                    browser.key("\ue007")
                    browser.wait_for(
                        "document.getElementById('investigation-identity').textContent !== "
                        + json.dumps(replay_identity_before), 20)
                    keyboard_replay_identity = browser.evaluate(
                        "document.getElementById('investigation-identity').textContent")
                    keyboard_replay_operation = str(browser.evaluate(
                        "selectedInvestigationOperationId"))
                    keyboard_replay_terminal = wait_investigation(
                        keyboard_replay_operation, 60)
                    accessibility = json.loads(str(browser.evaluate(r"""
                      (() => {
                        const tabs=[...document.querySelectorAll('[role=tab]')];
                        const controls=[...document.querySelectorAll(
                          'button,input,select,textarea,a[href]')];
                        const named=(element) => {
                          const id=element.id;
                          const label=id && document.querySelector(`label[for="${id}"]`);
                          const wrapped=element.closest('label');
                          return Boolean(element.getAttribute('aria-label') ||
                            element.getAttribute('aria-labelledby') || label || wrapped ||
                            element.textContent.trim() || element.title);
                        };
                        const main=tabs[0], graph=tabs[1];
                        const focus=getComputedStyle(main);
                        const rgb=(hex) => {
                          const value=hex.trim().replace('#','');
                          return [0,2,4].map(i=>parseInt(value.slice(i,i+2),16)/255);
                        };
                        const luminance=(hex) => rgb(hex).map(value=>
                          value<=0.04045 ? value/12.92 : ((value+0.055)/1.055)**2.4)
                          .reduce((sum,value,index)=>sum+value*[0.2126,0.7152,0.0722][index],0);
                        const contrast=(a,b) => {
                          const values=[luminance(a),luminance(b)].sort((x,y)=>y-x);
                          return (values[0]+0.05)/(values[1]+0.05);
                        };
                        const contrastRatios={
                          foreground:contrast('#111827','#ffffff'),
                          muted:contrast('#4b5563','#ffffff'),
                          accent:contrast('#0f766e','#ffffff'),
                          focus:contrast('#7c3aed','#ffffff')};
                        const status=[...document.querySelectorAll(
                          '[role=status],[aria-live]')].map(e=>({id:e.id,
                            role:e.getAttribute('role'),live:e.getAttribute('aria-live')}));
                        return JSON.stringify({
                          title:document.title,language:document.documentElement.lang,
                          main_landmark:Boolean(document.querySelector('main')),
                          navigation_landmark_count:document.querySelectorAll('nav').length,
                          skip_link:Boolean(document.querySelector('.skip-link')),
                          tablist_count:document.querySelectorAll('[role=tablist]').length,
                          tab_count:tabs.length,
                          tab_roving:tabs.filter(t=>t.tabIndex===0).length===1,
                          labelled_legend_group:Boolean(document.querySelector(
                            '.legend-row[role="group"][aria-label]')),
                          unnamed_controls:controls.filter(e=>!named(e)).map(e=>e.id),
                          focus_outline_style:focus.outlineStyle,
                          focus_outline_width:focus.outlineWidth,
                          contrast_ratios:contrastRatios,
                          live_regions:status,
                          required_live_regions:['event-transport','config-status','runtime-status',
                            'job-state','investigation-state'].every(id=>status.some(x=>x.id===id)),
                          unsafe_inline_handlers:document.querySelectorAll('[onclick]').length,
                          heading_one_count:document.querySelectorAll('h1').length,
                          visualization_text_alternative:[
                            document.getElementById('receiver-spectrum').textContent,
                            document.getElementById('timeline').innerText,
                            document.getElementById('heatmap-meta').textContent
                          ].every(text=>text.trim().length>10),
                          errors_are_text:Boolean(document.getElementById('config-status') &&
                            document.getElementById('runtime-status')),
                          non_color_status:Boolean(document.querySelector('.badge') ||
                            document.getElementById('event-transport')),
                          reduced_motion_matches:matchMedia('(prefers-reduced-motion: reduce)').matches,
                          reduced_motion_duration:getComputedStyle(document.body).animationDuration
                        });
                      })()
                    """)))
                    browser.call("browsingContext.setViewport", {
                        "context": browser.context,
                        "viewport": {"width": 320, "height": 800},
                        "devicePixelRatio": 1})
                    reflow = json.loads(str(browser.evaluate(r"""
                      (() => JSON.stringify({
                        viewport:innerWidth,
                        document_width:document.documentElement.scrollWidth,
                        body_width:document.body.scrollWidth,
                        active_panel_visible:Boolean(document.querySelector('.tab-panel.active')),
                        focused_id:document.activeElement && document.activeElement.id
                      }))()
                    """)))
                    browser.call("browsingContext.setViewport", {
                        "context": browser.context,
                        "viewport": {"width": 1440, "height": 1000},
                        "devicePixelRatio": 1})
                    screenshot = output / "phase8-accessibility.png"
                    browser.screenshot(screenshot)
                    accessibility.update({
                        "schema":"graphx.fhss.dashboard.phase8_browser_accessibility.v1",
                        "browser_name":browser.capabilities.get("browserName"),
                        "browser_version":browser.capabilities.get("browserVersion"),
                        "browser_session_id":browser.session_id,
                        "browser_context_id":browser.context,
                        "dashboard_url":url,
                        "real_keyboard_input":True,
                        "skip_focused":skip_focused,
                        "skip_activated":skip_activated,
                        "tab_right":real_tab_right,
                        "tab_home":real_tab_home,
                        "lifecycle_rebuild_text":lifecycle_rebuild_text,
                        "lifecycle_stop_text":lifecycle_stop_text,
                        "focus_before_refresh":focus_before_refresh,
                        "focus_after_refresh":focus_after_refresh,
                        "keyboard_export_identity":keyboard_export_identity,
                        "keyboard_validate_identity":keyboard_validate_identity,
                        "keyboard_replay_identity":keyboard_replay_identity,
                        "keyboard_operation_states":[
                            keyboard_export_terminal.get("state"),
                            keyboard_validate_terminal.get("state"),
                            keyboard_replay_terminal.get("state")],
                        "reflow_320_css_px":reflow,
                        "reduced_motion_rule_present":"prefers-reduced-motion" in
                            (Path(__file__).resolve().parents[1] /
                             "index.html").read_text(encoding="utf-8"),
                        "screenshot_sha256":sha256(screenshot),
                        "console":browser.messages,
                    })
                accessibility_path = output / "phase8-browser-accessibility.json"
                accessibility_path.write_text(
                    json.dumps(accessibility, indent=2) + "\n", encoding="utf-8")
                persist("phase8_browser_accessibility",
                        accessibility_path.read_bytes())
                persist("phase8_accessibility_screenshot", screenshot.read_bytes())
                check("Phase8 maintained-browser WCAG automation",
                      accessibility["language"] == "en" and
                      accessibility["main_landmark"] is True and
                      accessibility["navigation_landmark_count"] >= 1 and
                      accessibility["skip_link"] is True and
                      accessibility["skip_focused"] is True and
                      accessibility["skip_activated"] is True and
                      accessibility["real_keyboard_input"] is True and
                      accessibility["tablist_count"] == 1 and
                      accessibility["tab_count"] == 2 and
                      accessibility["tab_right"] is True and
                      accessibility["tab_home"] is True and
                      accessibility["tab_roving"] is True and
                      accessibility["labelled_legend_group"] is True and
                      accessibility["unnamed_controls"] == [] and
                      accessibility["unsafe_inline_handlers"] == 0 and
                      accessibility["heading_one_count"] == 1 and
                      accessibility["visualization_text_alternative"] is True and
                      accessibility["errors_are_text"] is True and
                      accessibility["required_live_regions"] is True and
                      accessibility["non_color_status"] is True and
                      accessibility["focus_before_refresh"] == "config-validate" and
                      accessibility["focus_after_refresh"] == "config-validate" and
                      len(accessibility["lifecycle_rebuild_text"]) > 20 and
                      len(accessibility["lifecycle_stop_text"]) > 20 and
                      "operation=" in accessibility["keyboard_export_identity"] and
                      "operation=" in accessibility["keyboard_validate_identity"] and
                      "operation=" in accessibility["keyboard_replay_identity"] and
                      len({accessibility["keyboard_export_identity"],
                           accessibility["keyboard_validate_identity"],
                           accessibility["keyboard_replay_identity"]}) == 3 and
                      accessibility["keyboard_operation_states"] ==
                          ["completed", "completed", "completed"] and
                      min(accessibility["contrast_ratios"].values()) >= 4.5 and
                      accessibility["reduced_motion_rule_present"] is True and
                      accessibility["reduced_motion_matches"] is True and
                      reflow["document_width"] <= 320 and
                      reflow["body_width"] <= 320 and
                      all(str(entry.get("level", "")).lower() not in
                          ("warning", "warn", "error")
                          for entry in accessibility["console"]),
                      json.dumps(accessibility, sort_keys=True))

                def process_resources() -> dict[str, object]:
                    stats: dict[str, object] = {
                        "rss_kib":None, "threads":None, "handles":None,
                        "rss_thread_probe":"unsupported",
                        "handle_probe":"unsupported"}
                    if sys.platform == "darwin":
                        try:
                            import ctypes
                            class ProcTaskInfo(ctypes.Structure):
                                _fields_ = [
                                    ("virtual_size", ctypes.c_uint64),
                                    ("resident_size", ctypes.c_uint64),
                                    ("total_user", ctypes.c_uint64),
                                    ("total_system", ctypes.c_uint64),
                                    ("threads_user", ctypes.c_uint64),
                                    ("threads_system", ctypes.c_uint64),
                                    ("policy", ctypes.c_int32),
                                    ("faults", ctypes.c_int32),
                                    ("pageins", ctypes.c_int32),
                                    ("cow_faults", ctypes.c_int32),
                                    ("messages_sent", ctypes.c_int32),
                                    ("messages_received", ctypes.c_int32),
                                    ("syscalls_mach", ctypes.c_int32),
                                    ("syscalls_unix", ctypes.c_int32),
                                    ("csw", ctypes.c_int32),
                                    ("threadnum", ctypes.c_int32),
                                    ("numrunning", ctypes.c_int32),
                                    ("priority", ctypes.c_int32)]
                            info = ProcTaskInfo()
                            libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
                            size = libproc.proc_pidinfo(
                                process.pid, 4, 0, ctypes.byref(info),
                                ctypes.sizeof(info))
                            if size == ctypes.sizeof(info):
                                stats["rss_kib"] = int(info.resident_size // 1024)
                                stats["threads"] = int(info.threadnum)
                                stats["rss_thread_probe"] = "libproc-PROC_PIDTASKINFO"
                        except (OSError, AttributeError, ValueError):
                            pass
                    if stats["rss_thread_probe"] == "unsupported":
                        ps = subprocess.run(
                            ["ps", "-o", "rss=", "-o", "thcount=", "-p",
                             str(process.pid)], text=True, capture_output=True,
                            check=False)
                        fields = ps.stdout.split()
                        if len(fields) >= 2:
                            stats["rss_kib"], stats["threads"] = map(int, fields[:2])
                            stats["rss_thread_probe"] = "ps"
                    lsof = shutil.which("lsof")
                    if lsof:
                        descriptors = subprocess.run(
                            [lsof, "-a", "-p", str(process.pid)], text=True,
                            capture_output=True, check=False)
                        stats["handles"] = max(0, len(descriptors.stdout.splitlines()) - 1)
                        stats["handle_probe"] = "lsof"
                    return stats

                before = process_resources()
                soak_started = time.monotonic()
                soak_statuses: list[int] = []
                for iteration in range(64):
                    route = "/healthz" if iteration % 2 == 0 else "/api/v1/fhss/status"
                    soak_statuses.append(request(port, "GET", route, timeout=5)[0])

                from websockets.sync.client import connect as websocket_connect
                reconnect_results: list[bool] = []
                for iteration in range(8):
                    client = websocket_connect(
                        f"ws://127.0.0.1:{port}/api/v1/fhss/events/stream",
                        origin=url, open_timeout=5, close_timeout=2,
                        max_size=256 * 1024, compression=None)
                    hello = json.loads(client.recv(timeout=5))
                    client.send(json.dumps({
                        "action":"subscribe", "client_id":f"phase8-soak-{iteration}",
                        "publisher_epoch":hello["publisher_epoch"],
                        "last_sequence":hello["latest_sequence"]}))
                    reconnect_results.append(
                        hello.get("schema") == "graphx.dashboard.websocket_hello.v1")
                    client.close()

                config_candidates = [
                    args.build_dir.resolve() / "share/graphx/config" /
                        "fhss_cpsm_channelized_fixture_500msps.json",
                    Path(__file__).resolve().parents[4] / "libdsp/config" /
                        "fhss_cpsm_channelized_fixture_500msps.json"]
                soak_schedule_source = next(
                    (candidate for candidate in config_candidates
                     if candidate.is_file()), None)
                if soak_schedule_source is None:
                    raise RuntimeError(
                        "Phase8 soak requires the canonical FHSS generator schedule")
                soak_graph = json.loads(
                    soak_schedule_source.read_text(encoding="utf-8"))
                soak_source_node = next(
                    (node for node in soak_graph.get("nodes", [])
                     if node.get("type") == "FHSSSyntheticIqSourceNode"), None)
                if (not isinstance(soak_source_node, dict) or
                        not isinstance(soak_source_node.get("node_config"), dict)):
                    raise RuntimeError(
                        "canonical FHSS graph has no synthetic-IQ generator config")
                soak_schedule = copy.deepcopy(soak_source_node["node_config"])
                soak_base_message = copy.deepcopy(soak_schedule["messages"][0])
                soak_stride = (len(soak_base_message["pulses"]) + 1) * 6500
                soak_schedule["messages"] = []
                for message_index in range(48):
                    soak_message = copy.deepcopy(soak_base_message)
                    soak_message["message_id"] = 8000 + message_index
                    soak_message["transmit_start_sample"] = (
                        message_index * soak_stride)
                    soak_schedule["messages"].append(soak_message)
                soak_long_schedule_path = output / "phase8-soak-long-schedule.json"
                soak_long_iq_path = output / "phase8-soak-long.cf32"
                soak_long_truth_path = output / "phase8-soak-long-truth.json"
                soak_long_sigmf_path = output / "phase8-soak-long.sigmf-meta"
                soak_long_schedule_path.write_text(
                    json.dumps(soak_schedule, indent=2), encoding="utf-8")
                soak_long_command = [
                    str(locate_generator(args.build_dir.resolve())),
                    "--message-json", str(soak_long_schedule_path),
                    "--iq-output", str(soak_long_iq_path),
                    "--truth-output", str(soak_long_truth_path),
                    "--sigmf-meta", str(soak_long_sigmf_path), "--force"]
                soak_long_generated = subprocess.run(
                    soak_long_command, text=True, capture_output=True,
                    timeout=GENERATOR_TIMEOUT_SECONDS)
                additional_commands.append(soak_long_command)
                if (soak_long_generated.returncode != 0 or
                        not soak_long_iq_path.is_file() or
                        soak_long_iq_path.stat().st_size < 32_000_000):
                    raise RuntimeError(
                        "Phase8 long soak IQ generation failed: " +
                        soak_long_generated.stderr)
                soak_long_schedule_path.unlink()
                soak_long_truth_path.unlink()

                shutdown_output = output / "phase8-soak-shutdown"
                shutdown_output.mkdir()
                shutdown_args = copy.copy(args)
                shutdown_args.output_dir = shutdown_output
                shutdown_process, _, shutdown_port, shutdown_command = launch(
                    shutdown_args)
                additional_commands.append(shutdown_command)
                _, shutdown_config_headers, _ = request(
                    shutdown_port, "GET", "/api/v1/fhss/config/authoritative")
                shutdown_long_patch = json.dumps([{
                    "op":"add", "path":"/receiver_input", "value":{
                        "file_path":str(soak_long_iq_path), "sample_format":"cf32_le",
                        "first_complex_sample":0,
                        "max_complex_samples":4_194_304,
                        "max_read_complex_samples":4_194_304,
                    }}]).encode()
                shutdown_patch_code, _, shutdown_patch_body = request(
                    shutdown_port, "PATCH", "/api/v1/fhss/config",
                    shutdown_long_patch,
                    {"Content-Type":"application/json-patch+json",
                     "If-Match":shutdown_config_headers["etag"]}, timeout=10)
                lifecycle_results: list[dict[str, object]] = []
                for iteration in range(2):
                    config_code, _, config_body = request(
                        shutdown_port, "GET", "/api/v1/fhss/config/authoritative")
                    revision = int(json.loads(config_body)["config_revision"])
                    rebuild_code, _, rebuild_body = request(
                        shutdown_port, "POST", "/api/v1/fhss/config/rebuild",
                        json.dumps({"expected_revision":revision,
                                    "command_id":f"phase8-soak-rebuild-{iteration}"}).encode(),
                        {"Content-Type":"application/json"}, timeout=30)
                    start_code, _, start_body = request(
                        shutdown_port, "POST", "/api/v1/fhss/commands/start",
                        json.dumps({"command_id":
                                    f"phase8-soak-start-{iteration}"}).encode(),
                        {"Content-Type":"application/json"}, timeout=30)
                    running_seen = False
                    running_deadline = time.monotonic() + 5
                    while time.monotonic() < running_deadline:
                        _, _, running_body = request(
                            shutdown_port, "GET", "/api/v1/fhss/status", timeout=5)
                        running_seen = (
                            json.loads(running_body).get("lifecycle_state") ==
                            "running")
                        if running_seen:
                            break
                        time.sleep(0.01)
                    stop_code, _, stop_body = request(
                        shutdown_port, "POST", "/api/v1/fhss/commands/stop",
                        json.dumps({"command_id":
                                    f"phase8-soak-stop-{iteration}"}).encode(),
                        {"Content-Type":"application/json"}, timeout=10)
                    deadline = time.monotonic() + 45
                    terminal: dict[str, object] = {}
                    while time.monotonic() < deadline:
                        _, _, terminal_body = request(
                            shutdown_port, "GET", "/api/v1/fhss/status", timeout=5)
                        terminal = json.loads(terminal_body)
                        if terminal.get("lifecycle_state") in (
                                "completed", "failed", "stopped"):
                            break
                        time.sleep(0.02)
                    lifecycle_results.append({
                        "config":config_code, "rebuild":rebuild_code,
                        "start":start_code, "stop":stop_code,
                        "running_seen":running_seen,
                        "rebuild_document":json.loads(rebuild_body),
                        "start_document":json.loads(start_body),
                        "terminal_state":terminal.get("lifecycle_state"),
                        "stop_document":json.loads(stop_body)})

                export_replay_results: list[dict[str, object]] = []
                for iteration in range(2):
                    bundle_name = f"phase8-soak-{iteration}"
                    code, _, submitted = submit_investigation(
                        "/api/v1/fhss/investigations/exports",
                        {"request_id":f"phase8-soak-export-{iteration}",
                         "bundle_name":bundle_name, "job_id":job_id,
                         "iq_mode":"reference", "timeout_ms":120000},
                        f"phase8-soak-export-key-{iteration}")
                    exported = wait_investigation(str(submitted["operation_id"]), 120)
                    validate_code, _, validate_submitted = submit_investigation(
                        "/api/v1/fhss/investigations/import-validations",
                        {"request_id":f"phase8-soak-validate-{iteration}",
                         "bundle_name":bundle_name,"timeout_ms":120000},
                        f"phase8-soak-validate-key-{iteration}")
                    validated = wait_investigation(
                        str(validate_submitted["operation_id"]), 120)
                    replay_code, _, replay_submitted = submit_investigation(
                        "/api/v1/fhss/investigations/replays",
                        {"request_id":f"phase8-soak-replay-{iteration}",
                         "bundle_name":bundle_name,"timeout_ms":120000},
                        f"phase8-soak-replay-key-{iteration}")
                    replayed = wait_investigation(
                        str(replay_submitted["operation_id"]), 120)
                    export_replay_results.append({
                        "submit_codes":[code, validate_code, replay_code],
                        "states":[exported.get("state"), validated.get("state"),
                                  replayed.get("state")],
                        "matches_expected":replayed.get("result", {}).get(
                            "matches_expected")})

                shutdown_started = time.monotonic()
                shutdown_process.send_signal(signal.SIGTERM)
                shutdown_process.wait(timeout=8)
                shutdown_elapsed = time.monotonic() - shutdown_started
                shutdown_exit = shutdown_process.returncode
                try:
                    request(shutdown_port, "GET", "/healthz", timeout=1)
                    shutdown_unreachable = False
                except OSError:
                    shutdown_unreachable = True
                soak_elapsed = time.monotonic() - soak_started
                after = process_resources()
                probe_support = {
                    "rss":isinstance(before["rss_kib"], int) and
                          isinstance(after["rss_kib"], int),
                    "threads":isinstance(before["threads"], int) and
                              isinstance(after["threads"], int),
                    "handles":isinstance(before["handles"], int) and
                              isinstance(after["handles"], int),
                }
                dimensions = {
                    "load":"PASS" if soak_statuses == [200] * 64 else "FAIL",
                    "reconnect":"PASS" if reconnect_results == [True] * 8 else "FAIL",
                    "lifecycle":"PASS" if all(
                        shutdown_patch_code == 200 and
                        item["config"] == 200 and item["rebuild"] == 200 and
                        item["start"] == 202 and item["stop"] == 200 and
                        item["running_seen"] is True and
                        item["terminal_state"] == "stopped"
                        for item in lifecycle_results) else "FAIL",
                    "export_replay":"PASS" if all(
                        item["submit_codes"] == [202, 202, 202] and
                        item["states"] == ["completed", "completed", "completed"] and
                        item["matches_expected"] is True
                        for item in export_replay_results) else "FAIL",
                    "shutdown":"PASS" if shutdown_exit == 0 and
                        shutdown_elapsed <= 8 and shutdown_unreachable else "FAIL",
                }
                resource_pass = (all(probe_support.values()) and
                    after["rss_kib"] - before["rss_kib"] <= 65536 and
                    after["threads"] - before["threads"] <= 8 and
                    after["handles"] - before["handles"] <= 16)
                soak_pass = (sys.platform == "darwin" and resource_pass and
                             soak_elapsed <= 240 and
                             all(value == "PASS" for value in dimensions.values()))
                soak = {
                    "schema":"graphx.fhss.dashboard.phase8_soak.v1",
                    "result":"PASS" if soak_pass else "FAIL",
                    "supported_profile":"macOS",
                    "iterations":{"load":64,"reconnect":8,"lifecycle":2,
                                  "export_replay":2,"shutdown":1},
                    "elapsed_seconds":soak_elapsed,
                    "bounds":{"max_seconds":240,"max_rss_growth_kib":65536,
                              "max_thread_growth":8,"max_handle_growth":16,
                              "max_shutdown_seconds":8},
                    "before":before,"after":after,
                    "probe_support":probe_support,
                    "dimensions":dimensions,
                    "receiver_fixture_patch":{
                        "status":shutdown_patch_code,
                        "document":json.loads(shutdown_patch_body)},
                    "reconnect_results":reconnect_results,
                    "lifecycle_results":lifecycle_results,
                    "export_replay_results":export_replay_results,
                    "shutdown":{"exit_code":shutdown_exit,
                                "elapsed_seconds":shutdown_elapsed,
                                "listener_unreachable":shutdown_unreachable,
                                "process_survived":shutdown_process.poll() is None},
                    "status_histogram":{str(code):soak_statuses.count(code)
                                        for code in sorted(set(soak_statuses))},
                }
                soak_path = output / "phase8-soak.json"
                soak_path.write_text(json.dumps(soak, indent=2) + "\n")
                persist("phase8_soak", soak_path.read_bytes())
                check("Phase8 bounded server resource soak",
                      soak_pass,
                      json.dumps(soak, sort_keys=True))
                qualification_path = output / "phase8-fuzz-security.json"
                qualification_command = [
                    sys.executable,
                    str(Path(__file__).resolve().parent /
                        "phase8_qualification.py"),
                    "--smoke", "--asset-root",
                    str(Path(__file__).resolve().parents[1]),
                    "--base-url", url,
                    "--output", str(qualification_path)]
                qualification_run = subprocess.run(
                    qualification_command, text=True, capture_output=True,
                    timeout=30, check=False)
                additional_commands.append(qualification_command)
                qualification_document = json.loads(
                    qualification_path.read_text(encoding="utf-8"))
                persist("phase8_fuzz_security", qualification_path.read_bytes())
                retained_fuzz_records = qualification_document.get(
                    "fuzz", {}).get("retained_regression_seeds", [])
                for record in retained_fuzz_records:
                    corpus_path = output / str(record["path"])
                    persist("phase8_fuzz_seed:" + str(record["target"]) + ":" +
                            str(record["sha256"]), corpus_path.read_bytes())
                check("Phase8 bounded fuzz and security qualification",
                      qualification_run.returncode == 0 and
                      qualification_document.get("fuzz", {}).get("result") == "PASS" and
                      qualification_document.get("security", {}).get("result") == "PASS" and
                      set(qualification_document.get("fuzz", {}).get("targets", [])) == {
                          "http_json", "json_patch", "websocket", "sigmf_import"} and
                      len(retained_fuzz_records) >= 4 and
                      all((output / str(record["path"])).is_file() and
                          sha256(output / str(record["path"])) == record["sha256"]
                          for record in retained_fuzz_records),
                      qualification_run.stdout + qualification_run.stderr)
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
            if phase == 3:
                lifecycle_guard_samples = 6_500
                lifecycle_scenario = current.get("scenario", {})
                lifecycle_messages = copy.deepcopy(
                    lifecycle_scenario.get("messages", []))
                if not lifecycle_messages:
                    raise RuntimeError(
                        "Phase3 lifecycle fixture requires one canonical message")
                canonical_lifecycle_message = copy.deepcopy(
                    lifecycle_messages[0])
                lifecycle_patch_operations = []
                for message_index in range(
                        len(lifecycle_messages) - 1, 0, -1):
                    lifecycle_patch_operations.append([{
                        "op": "remove",
                        "path": f"/messages/{message_index}",
                    }])
                lifecycle_patch_operations.append([{
                    "op": "replace",
                    "path": "/messages/0/transmit_start_sample",
                    "value": int(canonical_lifecycle_message[
                        "transmit_start_sample"]) + lifecycle_guard_samples,
                }])
                lifecycle_patch_statuses = []
                lifecycle_patch_responses = []
                for lifecycle_patch in lifecycle_patch_operations:
                    patch_status, _, patch_body = request(
                        port, "PATCH", "/api/v1/fhss/config",
                        json.dumps(lifecycle_patch).encode(),
                        {"Content-Type": "application/json-patch+json",
                         "If-Match": current_headers["etag"]})
                    lifecycle_patch_statuses.append(patch_status)
                    try:
                        patch_response = json.loads(patch_body)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        patch_response = patch_body.decode(errors="replace")
                    lifecycle_patch_responses.append(patch_response)
                    if (patch_status != 200 or
                            not isinstance(patch_response, dict) or
                            patch_response.get("status") != "applied"):
                        break
                    _, current_headers, current_bytes = request(
                        port, "GET", "/api/v1/fhss/config/authoritative")
                    current = json.loads(current_bytes)
                persist(
                    "phase3_lifecycle_waveform_patch",
                    (json.dumps({
                        "statuses": lifecycle_patch_statuses,
                        "operations": lifecycle_patch_operations,
                        "responses": lifecycle_patch_responses,
                    }, sort_keys=True) + "\n").encode())
                isolated_lifecycle_messages = current.get(
                    "scenario", {}).get("messages", [])
                expected_lifecycle_message = copy.deepcopy(
                    canonical_lifecycle_message)
                expected_lifecycle_message["transmit_start_sample"] = (
                    int(canonical_lifecycle_message["transmit_start_sample"]) +
                    lifecycle_guard_samples)
                check("Phase3 isolated one-message lifecycle fixture",
                      len(lifecycle_patch_statuses) ==
                          len(lifecycle_patch_operations) and
                      all(status == 200
                          for status in lifecycle_patch_statuses) and
                      all(isinstance(response, dict) and
                          response.get("status") == "applied"
                          for response in lifecycle_patch_responses) and
                      isolated_lifecycle_messages ==
                          [expected_lifecycle_message] and
                      isolated_lifecycle_messages[0].get("pulses") ==
                          canonical_lifecycle_message.get("pulses") and
                      len(isolated_lifecycle_messages[0].get("pulses", [])) > 0,
                      json.dumps({
                          "message_count": len(isolated_lifecycle_messages),
                          "message_id": (isolated_lifecycle_messages[0].get(
                              "message_id")
                              if isolated_lifecycle_messages else None),
                          "pulse_count": (len(isolated_lifecycle_messages[0].get(
                              "pulses", []))
                              if isolated_lifecycle_messages else 0),
                          "transmit_start_sample": (
                              isolated_lifecycle_messages[0].get(
                                  "transmit_start_sample")
                              if isolated_lifecycle_messages else None),
                      }, sort_keys=True))
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
                clean_patch_operations = []
                scenario_messages = copy.deepcopy(scenario.get("messages", []))
                if not scenario_messages:
                    raise RuntimeError(
                        "Phase4 isolated validation fixture requires one canonical message")
                canonical_message = copy.deepcopy(scenario_messages[0])
                for message_index in range(len(scenario_messages) - 1, 0, -1):
                    clean_patch_operations.append([{
                        "op": "remove",
                        "path": f"/messages/{message_index}",
                    }])
                clean_patch_operations.append([{
                    "op": "replace",
                    "path": "/messages/0/transmit_start_sample",
                    "value": int(canonical_message["transmit_start_sample"]) +
                             clean_guard_samples,
                }])
                for clean_patch in clean_patch_operations:
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
                                 "operations": clean_patch_operations,
                                 "responses": clean_patch_responses},
                                sort_keys=True) + "\n").encode())
                patched_scenario = current.get("scenario", {})
                isolated_messages = patched_scenario.get("messages", [])
                expected_message = copy.deepcopy(canonical_message)
                expected_message["transmit_start_sample"] = (
                    int(canonical_message["transmit_start_sample"]) +
                    clean_guard_samples)
                check("Phase4 receiver-compatible clean waveform contract",
                      len(clean_patch_statuses) == len(clean_patch_operations) and
                      all(status == 200 for status in clean_patch_statuses) and
                      all(isinstance(response, dict) and
                          response.get("status") == "applied"
                          for response in clean_patch_responses) and
                      offset_indices == list(range(64)) and
                      len(receiver_offsets) == 64 and
                      isolated_messages == [expected_message],
                      "explicit receiver IQ map; one complete canonical message; "
                      "6500-sample causal warm-up; receiver still receives no schedule")
                check("Phase4 isolated one-message validation fixture",
                      len(isolated_messages) == 1 and
                      isolated_messages[0] == expected_message and
                      isolated_messages[0].get("pulses") ==
                          canonical_message.get("pulses") and
                      len(isolated_messages[0].get("pulses", [])) > 0,
                      json.dumps({
                          "message_count": len(isolated_messages),
                          "message_id": (isolated_messages[0].get("message_id")
                                         if isolated_messages else None),
                          "pulse_count": (len(isolated_messages[0].get("pulses", []))
                                          if isolated_messages else 0),
                          "transmit_start_sample": (
                              isolated_messages[0].get("transmit_start_sample")
                              if isolated_messages else None),
                      }, sort_keys=True))
            schedule_path, iq_path = output / "schedule.json", output / "replay.cf32"
            truth_path, sigmf_path = output / "truth.json", output / "replay.sigmf-meta"
            generator_schedule = dict(current["scenario"])
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
                terminal_deadline = time.monotonic() + 120
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
                    json.dumps({"command_id":f"phase3-stop-{number}"}).encode(),
                    {"Content-Type":"application/json"}, timeout=30)
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
            long_truth_path.unlink(missing_ok=True)
            long_schedule_path.unlink(missing_ok=True)
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
                    # Debug/installed receiver execution can legitimately
                    # exceed one minute. Match the operator's established
                    # maximum bounded wait without weakening the requirement
                    # that the graph reaches a real terminal state.
                    export_deadline = time.monotonic() + 120
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
                    deadline = time.monotonic() + 120
                    while time.monotonic() < deadline:
                        _, _, terminal_body = request(port, "GET", "/api/v1/fhss/status")
                        terminal = json.loads(terminal_body)
                        if terminal.get("lifecycle_state") in ("completed", "failed"):
                            break
                        time.sleep(0.05)
                    if terminal.get("lifecycle_state") in ("starting", "running"):
                        cleanup_status, _, cleanup_body = request(
                            port, "POST", "/api/v1/fhss/commands/stop",
                            json.dumps({"command_id":
                                        f"phase4-{label}-timeout-cleanup"}).encode(),
                            {"Content-Type": "application/json"}, timeout=30)
                        persist(f"{label}_timeout_cleanup", cleanup_body)
                        _, _, terminal_body = request(
                            port, "GET", "/api/v1/fhss/status")
                        terminal = json.loads(terminal_body)
                        check(f"Phase4 {label} timeout cleanup is terminal",
                              cleanup_status == 200 and
                              terminal.get("lifecycle_state") not in
                                  ("starting", "running"),
                              f"stop={cleanup_status}; "
                              f"terminal={terminal.get('lifecycle_state')}")
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
        if phase == 8:
            dashboard_root = Path(__file__).resolve().parents[1]
            schema_paths = [
                *sorted((dashboard_root / "api" / "schemas").glob("*.schema.json")),
                *sorted((dashboard_root / "operator" / "schemas").glob(
                    "*.schema.json"))]
            schema_entries: list[dict[str, object]] = []
            for schema_path in schema_paths:
                schema_document = json.loads(schema_path.read_text(encoding="utf-8"))
                Draft202012Validator.check_schema(schema_document)
                relative = str(schema_path.relative_to(dashboard_root))
                schema_entries.append({"path":relative, "sha256":sha256(schema_path),
                                       "id":schema_document.get("$id")})
                schema_hashes[f"schema:{relative}"] = sha256(schema_path)
            schema_inventory = {
                "schema":"graphx.fhss.dashboard.phase8_schema_inventory.v1",
                "count":len(schema_entries), "metaschema_validation":"PASS",
                "entries":schema_entries}
            schema_inventory_path = output / "phase8-schema-inventory.json"
            schema_inventory_path.write_text(
                json.dumps(schema_inventory, indent=2) + "\n", encoding="utf-8")
            persist("phase8_schema_inventory", schema_inventory_path.read_bytes())
            packaged_paths = [
                dashboard_root / "index.html",
                dashboard_root / "fhss_transport_state.js",
                *sorted((dashboard_root / "api").rglob("*")),
                *sorted((dashboard_root / "operator").rglob("*")),
                *sorted((dashboard_root / "sigmf").rglob("*"))]
            package_entries = {
                str(path.relative_to(dashboard_root)):sha256(path)
                for path in packaged_paths
                if path.is_file() and "__pycache__" not in path.parts and
                   path.suffix != ".pyc"}
            independent_tool = (
                dashboard_root / "operator" / "fhss_phase3_independent.py")
            if not independent_tool.is_file():
                independent_tool = (Path(__file__).resolve().parents[2] /
                                    "tools" / "fhss_phase3_independent.py")
            if not independent_tool.is_file():
                raise RuntimeError("packaged independent FHSS oracle is missing")
            package_entries["operator/fhss_phase3_independent.py"] = sha256(
                independent_tool)
            required_phase8_docs = (
                "fhss_dashboard_phase8_manual_operator_test.md",
                "fhss_dashboard_phase8_security_support.md",
                "fhss_dashboard_phase8_architecture_recommendation.md")
            for document_name in required_phase8_docs:
                document_path = dashboard_root / "docs" / document_name
                if not document_path.is_file():
                    document_path = (Path(__file__).resolve().parents[4] /
                                     "docs" / "dsp" / document_name)
                if not document_path.is_file():
                    raise RuntimeError(
                        f"required packaged Phase8 document is missing: {document_name}")
                package_entries[f"docs/{document_name}"] = sha256(document_path)
            package_executables = {
                "graphx-dsp-fhss-demo":sha256(
                    locate_executable(args.build_dir.resolve())),
                "graphx-dsp-fhss-iq-generator":sha256(
                    locate_generator(args.build_dir.resolve()))}
            config_candidates = [
                args.build_dir.resolve() / "share/graphx/config" /
                    "fhss_cpsm_channelized_fixture_500msps.json",
                Path(__file__).resolve().parents[4] / "libdsp/config" /
                    "fhss_cpsm_channelized_fixture_500msps.json"]
            canonical_config = next(
                (candidate for candidate in config_candidates if candidate.is_file()), None)
            if canonical_config is None:
                raise RuntimeError("canonical packaged FHSS configuration missing")
            package_entries["config:fhss_cpsm_channelized_fixture_500msps.json"] = (
                sha256(canonical_config))
            package_manifest = {
                "schema":"graphx.fhss.dashboard.phase8_package_manifest.v1",
                "entry_count":len(package_entries),
                "entries":dict(sorted(package_entries.items())),
                "executables":dict(sorted(package_executables.items()))}
            package_manifest_path = output / "phase8-package-manifest.json"
            package_manifest_path.write_text(
                json.dumps(package_manifest, indent=2) + "\n", encoding="utf-8")
            persist("phase8_package_manifest", package_manifest_path.read_bytes())
        sigmf_root = (Path(__file__).resolve().parents[1] / "sigmf" /
                      "official-v1.2.6")
        sigmf_input_hashes = {
            "sigmf_schema": sha256(sigmf_root / "sigmf-schema.json"),
            "sigmf_provenance": sha256(sigmf_root / "provenance.json"),
            "sigmf_license": sha256(sigmf_root / "LICENSE.md"),
            "jsonschema_validator_module": sha256(Path(jsonschema.__file__).resolve()),
            "jsonschema_validator_version": importlib.metadata.version("jsonschema"),
        }
        checks_pass = all(item["pass"] for item in checks)
        evidence_status = ("final_verified" if phase >= 7 else
                           ("partial_pre_browser" if phase >= 4 else "complete"))
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": phase,
            "source_revision": revision, "compiler": compiler[0] if compiler else "unknown",
            "build_profile": args.build_dir.name, "platform": platform.platform(),
            "commands": [command, *additional_commands],
            "bound_address": "127.0.0.1", "bound_port": port,
            "dashboard_url": url, "api_version": "v1", "synthetic_data_only": True,
            "hwil_available": False, "production_rf_qualified": False,
            "input_hashes": {"openapi": sha256(Path(__file__).resolve().parents[1] / "api/openapi.json"),
                             "transport_state": sha256(Path(__file__).resolve().parents[1] / "fhss_transport_state.js"),
                             "operator": sha256(Path(__file__).resolve()),
                             "operator_report_schema": sha256(Path(__file__).resolve().parent / "schemas/operator-report.schema.json"),
                             "phase6_transcript_schema": sha256(Path(__file__).resolve().parent / "schemas/phase6-wire-transcript.schema.json"),
                             **sigmf_input_hashes,
                             **schema_hashes},
            "artifact_hashes": {"dashboard_index": sha256(Path(__file__).resolve().parents[1] / "index.html"),
                                **live_hashes,
                                **phase2_hashes},
            "checks": checks,
            **({"qualification": {
                "dashboard_scope":"FHSS-specific",
                "input_evidence":"synthetic IQ only",
                "hwil_conducted_ota_evidence":"unavailable and deferred",
                "receiver_truth_isolation":"PASS",
                "browser_automation":"PASS",
                "security_local_profile":"PASS",
                "operator_workflow":"PASS",
                "production_rf_qualification":"NOT QUALIFIED"
            }} if phase == 8 and checks_pass else {}),
            "evidence_status": evidence_status,
            "result": ("PASS" if phase >= 7 else
                       ("PARTIAL" if phase >= 4 else "PASS"))
                      if checks_pass else "FAIL"
        }
        report_path = output / f"phase{phase}-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 0 if report["result"] in ("PASS", "PARTIAL") else 1
    except Exception as error:
        checks.append({"name": "operator execution", "pass": False,
                       "evidence": traceback.format_exc()})
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
        transport_state = (Path(__file__).resolve().parents[1] /
                           "fhss_transport_state.js")
        transcript_schema = (Path(__file__).resolve().parent / "schemas" /
                             "phase6-wire-transcript.schema.json")
        sigmf_root = (Path(__file__).resolve().parents[1] / "sigmf" /
                      "official-v1.2.6")
        transport_hash_valid = (
            args.phase < 6 or
            report.get("input_hashes", {}).get("transport_state") ==
                sha256(transport_state))
        transcript_schema_hash_valid = (
            args.phase < 6 or
            report.get("input_hashes", {}).get("phase6_transcript_schema") ==
                sha256(transcript_schema))
        sigmf_hashes_valid = (
            args.phase < 7 or (
                report.get("input_hashes", {}).get("sigmf_schema") ==
                    sha256(sigmf_root / "sigmf-schema.json") and
                report.get("input_hashes", {}).get("sigmf_provenance") ==
                    sha256(sigmf_root / "provenance.json") and
                report.get("input_hashes", {}).get("sigmf_license") ==
                    sha256(sigmf_root / "LICENSE.md") and
                report.get("input_hashes", {}).get("jsonschema_validator_module") ==
                    sha256(Path(jsonschema.__file__).resolve()) and
                report.get("input_hashes", {}).get("jsonschema_validator_version") ==
                    importlib.metadata.version("jsonschema")))
        hashes_valid = (report.get("artifact_hashes", {}).get("dashboard_index") == sha256(dashboard_index)
                        and report.get("input_hashes", {}).get("openapi") == sha256(openapi)
                        and transport_hash_valid
                        and transcript_schema_hash_valid
                        and sigmf_hashes_valid
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
            if key == "phase6_wire_transcript":
                transcript_path = (args.output_dir.resolve() /
                                   "phase6-wire-transcript.json")
                transcript_document = json.loads(
                    transcript_path.read_text(encoding="utf-8"))
                evidence = evidence_dir / (
                    hashlib.sha256(key.encode()).hexdigest() + ".bin")
                hashes_valid = (
                    hashes_valid and transcript_path.is_file() and
                    sha256(transcript_path) == digest and
                    evidence.is_file() and sha256(evidence) == digest and
                    validate_phase6_wire_transcript(transcript_document) and
                    validate_phase6_wire_transcript_negative_corpus(
                        transcript_document) and
                    hashlib.sha256(transcript_path.read_bytes() + b" ").hexdigest()
                        != digest)
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
            if key.startswith(f"phase{args.phase}_browser_console:"):
                case = key.split(":", 1)[1]
                console_log = (args.output_dir.resolve() / "browser-console" /
                               f"{case}.json")
                hashes_valid = (hashes_valid and console_log.is_file() and
                                sha256(console_log) == digest)
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
                              ({"step", "continue", "cancelled"}
                               if args.phase == 5 else
                               {"live", "replay", "resync"}))
            screenshots_valid = set(captures) == required_cases
            for case, record in captures.items():
                screenshot = args.output_dir.resolve() / record["relative_path"]
                state_path = args.output_dir.resolve() / record["served_state"]
                state = json.loads(state_path.read_text(encoding="utf-8"))
                console_valid = True
                if args.phase == 6:
                    console_path = args.output_dir.resolve() / record["console_log"]
                    console = json.loads(console_path.read_text(encoding="utf-8"))
                    console_valid = (
                        console_path.is_file() and
                        record.get("console_sha256") == sha256(console_path) and
                        report.get("artifact_hashes", {}).get(
                            f"phase6_browser_console:{case}") == sha256(console_path) and
                        validate_browser_console(
                            console, state, state_path, screenshot))
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
                                     console_valid and
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
                                        state.get("truth_withheld_during_replay") is True)) or
                                      (args.phase == 6 and
                                       state.get("synthetic_data_only") is True and
                                       state.get("hwil_available") is False and
                                       state.get("transport_state") in
                                       ("live", "contiguous_replay", "resync_required") and
                                       len(str(state.get("publisher_epoch", ""))) == 32 and
                                       int(state.get("sequence", -1)) >= 0)) and
                                     record.get("served_state_sha256") == sha256(state_path) and
                                     report.get("artifact_hashes", {}).get(
                                         f"phase{args.phase}_served_state:{case}") == sha256(state_path) and
                                     report.get("artifact_hashes", {}).get(
                                         f"phase{args.phase}_screenshot:{case}") == record["sha256"])
        except (OSError, KeyError, TypeError, ValueError,
                json.JSONDecodeError):
            screenshots_valid = False
    if args.phase >= 7:
        screenshot_cases = ("reference-completed", "copy-completed",
                            "replay-success", "safe-failed")
        try:
            phase7_states = {
                case: json.loads((args.output_dir.resolve() /
                    f"phase7-investigation-{case}.json").read_text())
                for case in screenshot_cases}
            screenshots_valid = all(
                is_valid_png(args.output_dir.resolve() /
                             f"phase7-investigation-{case}.png") and
                report.get("artifact_hashes", {}).get(
                    f"phase7_browser_screenshot:{case}") == sha256(
                        args.output_dir.resolve() /
                        f"phase7-investigation-{case}.png") and
                report.get("artifact_hashes", {}).get(
                    f"phase7_browser_state:{case}") == sha256(
                        args.output_dir.resolve() /
                        f"phase7-investigation-{case}.json") and
                phase7_states[case].get("screenshot_sha256") == sha256(
                    args.output_dir.resolve() /
                    f"phase7-investigation-{case}.png") and
                phase7_states[case].get("visible") is True and
                phase7_states[case].get("operation_id") in
                    str(phase7_states[case].get("panel_text", "")) and
                phase7_states[case].get("operation_id") in
                    str(phase7_states[case].get("identity", "")) and
                phase7_states[case].get("dashboard_url") == report.get("dashboard_url") and
                bool(phase7_states[case].get("browser_session_id")) and
                bool(phase7_states[case].get("browser_context_id")) and
                phase7_states[case].get("panel_visible") is True and
                int(phase7_states[case].get("panel_rect", {}).get("bottom", 0)) > 0 and
                int(phase7_states[case].get("panel_rect", {}).get("top", -1)) <
                    int(phase7_states[case].get("viewport", {}).get("height", -1)) and
                int(phase7_states[case].get("identity_rect", {}).get("bottom", 0)) > 0 and
                int(phase7_states[case].get("identity_rect", {}).get("top", -1)) <
                    int(phase7_states[case].get("viewport", {}).get("height", -1))
                for case in screenshot_cases)
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
            screenshots_valid = False
        expected_states = {("PASS", "final_verified")}
        displayed_result = "PASS"
    elif args.phase >= 4 and require_screenshots:
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
    if args.phase == 8:
        qualification = report.get("qualification", {})
        valid = (valid and qualification == {
            "dashboard_scope":"FHSS-specific",
            "input_evidence":"synthetic IQ only",
            "hwil_conducted_ota_evidence":"unavailable and deferred",
            "receiver_truth_isolation":"PASS",
            "browser_automation":"PASS",
            "security_local_profile":"PASS",
            "operator_workflow":"PASS",
            "production_rf_qualification":"NOT QUALIFIED",
        })
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
    console_destination = None
    console_digest = None
    if args.phase == 6:
        if args.console_log is None:
            raise RuntimeError("--console-log is required for Phase6 captures")
        console_source = args.console_log.resolve()
        if not console_source.is_file():
            raise RuntimeError("Phase6 browser console evidence is required")
        console = json.loads(console_source.read_text(encoding="utf-8"))
        if not validate_browser_console(console, state, state_path, source):
            raise RuntimeError(
                "browser console evidence lacks valid direct BiDi provenance")
        console_directory = output / "browser-console"
        console_directory.mkdir(exist_ok=True)
        console_destination = console_directory / f"{args.case}.json"
        if console_source != console_destination.resolve():
            shutil.copy2(console_source, console_destination)
        console_digest = sha256(console_destination)
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
    elif args.phase == 5:
        state_valid = (common_valid and state.get("synthetic_data_only") is True and
                       state.get("hwil_available") is False and
                       re.fullmatch(r"j-[0-9a-f]{24}", str(state.get("job_id", ""))) is not None and
                       len(str(state.get("job_sha256", ""))) == 64 and
                       state.get("job_state") in ("completed", "cancelled") and
                       (args.case == "cancelled" or
                        (generation >= 1 and run_epoch >= 1 and
                         state.get("truth_withheld_during_replay") is True)))
    else:
        state_valid = (common_valid and state.get("synthetic_data_only") is True and
                       state.get("hwil_available") is False and
                       state.get("transport_state") in
                       ("live", "contiguous_replay", "resync_required") and
                       len(str(state.get("publisher_epoch", ""))) == 32 and
                       int(state.get("sequence", -1)) >= 0)
    if not state_valid:
        raise RuntimeError("served-state evidence does not identify the requested case")
    screenshots = output / "screenshots"
    screenshots.mkdir(exist_ok=True)
    destination = screenshots / f"{args.case}.png"
    if source.resolve() != destination.resolve():
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
    if args.phase == 6:
        manifest["captured_files"][args.case].update({
            "console_log": str(console_destination.relative_to(output)),
            "console_sha256": console_digest,
        })
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report.setdefault("artifact_hashes", {})[f"phase{args.phase}_screenshot:{args.case}"] = digest
    report["artifact_hashes"][f"phase{args.phase}_served_state:{args.case}"] = state_digest
    if args.phase == 6:
        report["artifact_hashes"][f"phase6_browser_console:{args.case}"] = console_digest
    report["artifact_hashes"][f"phase{args.phase}_screenshot_manifest_current"] = sha256(
        manifest_path)
    captured_cases = set(manifest.get("captured_files", {}))
    required_cases = ({"clean", "impaired", "negative"}
                      if args.phase == 4 else
                      ({"step", "continue", "cancelled"}
                       if args.phase == 5 else
                       {"live", "replay", "resync"}))
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


def capture_browser(args: argparse.Namespace) -> int:
    if args.phase != 6:
        raise RuntimeError("capture-browser is defined for Phase6 only")
    output = args.output_dir.resolve()
    state_path = output / f"phase6-{args.case}-served-state.json"
    state = json.loads(state_path.read_text(encoding="utf-8"))
    url = str(state.get("url", ""))
    if not url.startswith("http://127.0.0.1:"):
        raise RuntimeError("served-state URL is not a bound loopback origin")
    expected = {
        "live": "live WebSocket sequence",
        "replay": "live — replayed through sequence",
        "resync": "resync snapshot at sequence",
    }[args.case]
    capture_input = output / "capture-input"
    capture_input.mkdir(exist_ok=True)
    screenshot_path = capture_input / f"{args.case}.png"
    console_path = capture_input / f"{args.case}-console.json"
    with FirefoxBidiSession(output) as browser:
        browser.navigate(url)
        observed = str(browser.wait_for(
            "document.getElementById('event-transport').textContent", 10))
        browser.wait_for(
            f"document.getElementById('event-transport').textContent.includes({json.dumps(expected)})",
            10)
        final_state = str(browser.evaluate(
            "document.getElementById('event-transport').textContent"))
        browser.screenshot(screenshot_path)
        console_path.write_text(json.dumps(browser_console_document(
            browser, url, state_path, screenshot_path,
            [observed, final_state]), indent=2) + "\n", encoding="utf-8")
    result = record_screenshot(argparse.Namespace(
        phase=6, build_dir=args.build_dir, output_dir=output, case=args.case,
        path=screenshot_path, console_log=console_path))
    screenshot_path.unlink(missing_ok=True)
    console_path.unlink(missing_ok=True)
    capture_input.rmdir()
    return result


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
        "phase6-screenshot-manifest.json", "phase6-live-served-state.json",
        "phase6-replay-served-state.json", "phase6-resync-served-state.json",
        "phase6-wire-transcript.json",
    }
    exact_files.update(f"phase4-cases/{case}-config.json"
                       for case in ("clean", "impaired", "negative", "malformed"))
    exact_files.update(f"screenshots/{case}.png"
                       for case in ("clean", "impaired", "negative",
                                    "step", "continue", "cancelled"))
    exact_files.update(f"screenshots/{case}.png"
                       for case in ("live", "replay", "resync"))
    exact_files.update(f"browser-console/{case}.json"
                       for case in ("live", "replay", "resync"))
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
    for directory in (output / "screenshots", output / "browser-console",
                      output / "phase4-cases"):
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
                 "record-screenshot", "capture-browser"):
        item = commands.add_parser(name)
        item.add_argument("--phase", type=int, default=PHASE)
        item.add_argument("--build-dir", type=Path, default=Path("build-ninja/ninja-debug"))
        item.add_argument("--output-dir", type=Path, required=True)
        if name == "serve":
            item.add_argument("--case", choices=("clean", "impaired", "negative",
                                                   "step", "continue", "cancelled",
                                                   "live", "replay", "resync"))
            item.add_argument("--port", type=int, default=0)
            item.add_argument("--exit-after-case", action="store_true")
        elif name == "verify":
            item.add_argument("--require-screenshots", action="store_true")
        elif name in ("record-screenshot", "capture-browser"):
            item.add_argument("--case", choices=("clean", "impaired", "negative",
                                                   "step", "continue", "cancelled",
                                                   "live", "replay", "resync"),
                              required=True)
            if name == "record-screenshot":
                item.add_argument("--path", type=Path, required=True)
                item.add_argument("--console-log", type=Path)
    args = parser.parse_args()
    if args.command == "exercise": return exercise(args)
    if args.command == "verify": return verify(args)
    if args.command == "cleanup": return cleanup(args)
    if args.command == "record-screenshot": return record_screenshot(args)
    if args.command == "capture-browser": return capture_browser(args)
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
        elif args.phase == 6:
            start_served_phase6_case(int(url.rsplit(":", 1)[1]), args.case,
                                     args.output_dir.resolve(), url)
        else:
            raise RuntimeError("serve --case is supported for phases 4 through 6")
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
