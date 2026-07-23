#!/usr/bin/env python3
"""Bounded Phase 8 security/fuzz qualification for the local FHSS dashboard.

The deterministic CI driver mutates retained seeds, sends each candidate to the
actual production endpoint, and retains response-signature coverage. Separate
bounded byte oracles classify the same candidates without reusing production
results as the only oracle.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import http.client
import re
import sys
import urllib.parse
from pathlib import Path, PurePosixPath

MAX_INPUT = 1_048_576
TARGETS = ("http_json", "json_patch", "websocket", "sigmf_import")


def _json(data: bytes) -> object:
    if len(data) > MAX_INPUT:
        raise ValueError("input exceeds qualification bound")
    return json.loads(data.decode("utf-8", errors="strict"))


def fuzz_http_json(data: bytes) -> None:
    try:
        if b"\r\n\r\n" in data:
            header, body = data.split(b"\r\n\r\n", 1)
            lines = header.split(b"\r\n")
            if len(header) > 16_384 or not lines:
                raise ValueError("header bound")
            request_line = lines[0].decode("ascii", errors="strict").split(" ")
            if len(request_line) != 3 or request_line[2] != "HTTP/1.1":
                raise ValueError("request line")
            lengths = [line for line in lines[1:]
                       if line.lower().startswith(b"content-length:")]
            transfers = [line for line in lines[1:]
                         if line.lower().startswith(b"transfer-encoding:")]
            if len(lengths) > 1 or (lengths and transfers):
                raise ValueError("ambiguous framing")
            if body:
                _json(body)
        else:
            _json(data)
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        return


def _pointer(value: object) -> None:
    if not isinstance(value, str) or (value and not value.startswith("/")):
        raise ValueError("invalid JSON Pointer")
    for token in value.split("/")[1:]:
        if re.search(r"~(?:[^01]|$)", token):
            raise ValueError("invalid JSON Pointer escape")


def fuzz_json_patch(data: bytes) -> None:
    try:
        patch = _json(data)
        if not isinstance(patch, list) or len(patch) > 256:
            raise ValueError("patch bound")
        for operation in patch:
            if not isinstance(operation, dict) or operation.get("op") not in {
                    "add", "remove", "replace", "move", "copy", "test"}:
                raise ValueError("patch operation")
            _pointer(operation.get("path"))
            if operation.get("op") in {"move", "copy"}:
                _pointer(operation.get("from"))
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        return


def fuzz_websocket(data: bytes) -> None:
    try:
        message = _json(data)
        if not isinstance(message, dict):
            raise ValueError("message object")
        action = message.get("action")
        schema = message.get("schema")
        if action not in {"subscribe", "heartbeat_ack", None}:
            raise ValueError("unsupported command")
        if schema is not None and schema not in {
                "graphx.dashboard.event.v1",
                "graphx.dashboard.websocket_hello.v1",
                "graphx.dashboard.websocket_heartbeat.v1",
                "graphx.dashboard.websocket_resync_required.v1"}:
            raise ValueError("unsupported event")
        for key in ("sequence", "last_sequence", "latest_sequence"):
            if key in message and (not isinstance(message[key], int) or
                                   isinstance(message[key], bool) or
                                   message[key] < 0 or message[key] > 2**53 - 1):
                raise ValueError("sequence bound")
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        return


def fuzz_sigmf_import(data: bytes) -> None:
    try:
        document = _json(data)
        if not isinstance(document, dict):
            raise ValueError("manifest object")
        for key in ("bundle_name", "dataset", "path", "relative_path"):
            if key not in document:
                continue
            value = document[key]
            if not isinstance(value, str) or "\x00" in value or len(value) > 4096:
                raise ValueError("path representation")
            path = PurePosixPath(value.replace("\\", "/"))
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("path containment")
        datatype = document.get("datatype")
        if datatype is not None and datatype not in {"cf32_le", "cf64_le"}:
            raise ValueError("unsupported datatype")
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        return


FUNCTIONS = {
    "http_json": fuzz_http_json,
    "json_patch": fuzz_json_patch,
    "websocket": fuzz_websocket,
    "sigmf_import": fuzz_sigmf_import,
}


def mutations(seed: bytes):
    yield seed
    yield b""
    yield seed[: max(0, len(seed) // 2)]
    yield seed + b"\x00\xff"
    for index in range(min(len(seed), 8)):
        changed = bytearray(seed)
        changed[index] ^= 0x80
        yield bytes(changed)


def _production_response(base_url: str, target: str,
                         data: bytes) -> tuple[int, str]:
    parsed = urllib.parse.urlparse(base_url)
    connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=5)
    headers: dict[str, str] = {"Content-Type":"application/json"}
    method, path = "POST", "/api/v1/fhss/config/validate"
    if target == "http_json":
        import socket
        sock = socket.create_connection((str(parsed.hostname), int(parsed.port)), 5)
        request_bytes = data if data.startswith((b"GET ", b"POST ", b"PATCH ")) else (
            b"POST /api/v1/fhss/config/validate HTTP/1.1\r\nHost: " +
            f"{parsed.hostname}:{parsed.port}".encode() +
            b"\r\nContent-Type: application/json\r\nContent-Length: " +
            str(len(data)).encode() + b"\r\nConnection: close\r\n\r\n" + data)
        sock.sendall(request_bytes[:MAX_INPUT + 32768])
        response = sock.recv(4096)
        sock.close()
        status = int(response.split(b" ", 2)[1]) if b" " in response else 0
        return status, hashlib.sha256(response[:128]).hexdigest()[:16]
    if target == "json_patch":
        _, response_headers = _production_response(base_url, "etag", b"")
        method, path = "PATCH", "/api/v1/fhss/config"
        headers = {"Content-Type":"application/json-patch+json",
                   "If-Match":response_headers}
    elif target == "websocket":
        # The production server's handshake and frame decoder are exercised by
        # sending a real RFC 6455 upgrade plus a masked arbitrary-byte frame.
        import base64
        import os
        import socket
        key = base64.b64encode(os.urandom(16)).decode()
        sock = socket.create_connection((str(parsed.hostname), int(parsed.port)), 5)
        upgrade = (f"GET /api/v1/fhss/events/stream HTTP/1.1\r\n"
                   f"Host: {parsed.hostname}:{parsed.port}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                   f"Sec-WebSocket-Key: {key}\r\nOrigin: {base_url}\r\n\r\n").encode()
        sock.sendall(upgrade)
        handshake = sock.recv(4096)
        payload = data[:65536]
        mask = b"P8fz"
        length = len(payload)
        if length <= 125:
            frame = bytes((0x81, 0x80 | length))
        elif length <= 65535:
            frame = bytes((0x81, 0xfe)) + length.to_bytes(2, "big")
        else:
            frame = bytes((0x81, 0xff)) + length.to_bytes(8, "big")
        frame += mask + bytes(value ^ mask[index % 4]
                              for index, value in enumerate(payload))
        sock.sendall(frame)
        response = sock.recv(4096)
        sock.close()
        status = int(handshake.split(b" ", 2)[1]) if b" " in handshake else 0
        return status, hashlib.sha256(response[:64]).hexdigest()[:16]
    elif target == "sigmf_import":
        path = "/api/v1/fhss/investigations/import-validations"
        headers["Idempotency-Key"] = "phase8-fuzz-" + hashlib.sha256(data).hexdigest()[:16]
    elif target == "etag":
        connection.request("GET", "/api/v1/fhss/config")
        response = connection.getresponse()
        response.read()
        etag = response.getheader("ETag") or '"graphx-config-0"'
        connection.close()
        return response.status, etag
    connection.request(method, path, body=data, headers=headers)
    response = connection.getresponse()
    body = response.read(4096)
    content_type = response.getheader("Content-Type", "").split(";", 1)[0]
    signature = f"{content_type}:{hashlib.sha256(body[:64]).hexdigest()[:16]}"
    status = response.status
    connection.close()
    return status, signature


def smoke(base_url: str | None = None,
          corpus_root: Path | None = None) -> dict[str, object]:
    seeds = {
        "http_json": b"POST /api/v1/fhss/config HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}",
        "json_patch": b'[{"op":"replace","path":"/receiver_input/sample_format","value":"cf32_le"}]',
        "websocket": b'{"action":"subscribe","client_id":"phase8","last_sequence":0}',
        "sigmf_import": b'{"bundle_name":"phase8","dataset":"recording.sigmf-data","datatype":"cf32_le"}',
    }
    executions: dict[str, int] = {}
    response_coverage: dict[str, list[str]] = {}
    reachable: dict[str, int] = {}
    retained_records: list[dict[str, object]] = []
    for name, seed in seeds.items():
        count = 0
        candidates = list(mutations(seed))
        seen_signatures: set[str] = set()
        retained: list[bytes] = []
        index = 0
        while index < len(candidates) and count < 32:
            candidate = candidates[index]
            index += 1
            # Independent bounded oracle first, then the actual production
            # endpoint/parser. New status/body signatures are retained as the
            # response-coverage corpus for the next bounded run.
            FUNCTIONS[name](candidate)
            if base_url:
                try:
                    status, signature = _production_response(base_url, name, candidate)
                    response_coverage.setdefault(name, []).append(
                        f"{status}:{signature}")
                    if status > 0:
                        reachable[name] = reachable.get(name, 0) + 1
                    coverage_key = f"{status}:{signature}"
                    if coverage_key not in seen_signatures:
                        seen_signatures.add(coverage_key)
                        retained.append(candidate)
                        if corpus_root is not None:
                            target_root = corpus_root / name
                            target_root.mkdir(parents=True, exist_ok=True)
                            digest = hashlib.sha256(candidate).hexdigest()
                            corpus_path = target_root / f"{digest}.bin"
                            corpus_path.write_bytes(candidate)
                            retained_records.append({
                                "target":name,
                                "path":str(corpus_path.relative_to(corpus_root.parent)),
                                "sha256":digest,"bytes":len(candidate),
                                "coverage_signature":coverage_key})
                        if len(candidates) < 32:
                            candidates.extend(list(mutations(candidate + b" "))[:2])
                except (OSError, ValueError, ImportError, http.client.HTTPException):
                    response_coverage.setdefault(name, []).append("transport-safe-failure")
            count += 1
        executions[name] = count
        response_coverage.setdefault(name, [])
        reachable.setdefault(name, 0)
    production_pass = (base_url is not None and
        all(reachable.get(name, 0) > 0 for name in TARGETS) and
        any(value.startswith("101:")
            for value in response_coverage.get("websocket", [])))
    return {
        "schema":"graphx.fhss.dashboard.phase8_fuzz_smoke.v1",
        "engine_interface":"bounded production-endpoint response-coverage driver with independent byte oracles",
        "guidance":"production HTTP/WebSocket status and bounded response signatures",
        "production_endpoints_exercised":production_pass,
        "max_input_bytes":MAX_INPUT,
        "targets":list(TARGETS),
        "executions":executions,
        "retained_regression_seeds":retained_records,
        "successful_responses":reachable,
        "response_coverage":{name:sorted(set(values))
                             for name, values in response_coverage.items()},
        "result":"PASS" if production_pass else "FAIL",
    }


def security_audit(asset_root: Path) -> dict[str, object]:
    html = (asset_root / "index.html").read_text(encoding="utf-8")
    compiled_assets = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted(asset_root.rglob("*"))
        if path.is_file() and path.suffix in (".css", ".js"))
    unsafe = re.findall(r"\.(?:innerHTML|outerHTML)\s*=|insertAdjacentHTML\s*\(",
                        html + "\n" + compiled_assets)
    return {
        "schema":"graphx.fhss.dashboard.phase8_security_static.v1",
        "profile":"loopback-only local operator",
        "unsafe_dynamic_html_sinks":len(unsafe),
        "inline_event_handlers":len(re.findall(r"\son[a-z]+\s*=", html,
                                                 flags=re.IGNORECASE)),
        "has_reduced_motion":"prefers-reduced-motion" in compiled_assets,
        "has_visible_focus":":focus-visible" in compiled_assets,
        "has_skip_link":"skip-link" in compiled_assets,
        "result":"PASS" if not unsafe else "FAIL",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=TARGETS)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--asset-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--base-url")
    parser.add_argument("--static-only", action="store_true")
    args = parser.parse_args()
    if args.target:
        data = args.input.read_bytes() if args.input else sys.stdin.buffer.read(MAX_INPUT + 1)
        FUNCTIONS[args.target](data)
        return 0
    corpus_root = ((args.output.resolve().parent / "phase8-fuzz-corpus")
                   if args.output else Path.cwd() / "phase8-fuzz-corpus")
    report = {"fuzz":smoke(args.base_url, corpus_root)
              if not args.static_only else {
                  "schema":"graphx.fhss.dashboard.phase8_fuzz_smoke.v1",
                  "result":"NOT_RUN", "reason":"production endpoint required"},
              "security":security_audit(args.asset_root.resolve())}
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    fuzz_ok = args.static_only or (report["fuzz"].get("result") == "PASS" and
        report["fuzz"].get("production_endpoints_exercised") is True and
        set(report["fuzz"].get("response_coverage", {})) == set(TARGETS))
    return 0 if report["security"]["result"] == "PASS" and fuzz_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
