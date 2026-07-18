#!/usr/bin/env python3
"""External Phase 1 operator for the loopback-only GraphX FHSS dashboard."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import platform
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1] / "api"
sys.path.insert(0, str(API_DIR))
from schema_subset import validate_instance, validate_schema  # noqa: E402
try:
    from jsonschema import Draft202012Validator
except ImportError as error:
    raise SystemExit("install ../api/requirements-contracts.lock for authoritative live validation") from error

PHASE = 1
OWNED_MARKER = ".graphx-fhss-dashboard-operator"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def locate_executable(build_dir: Path) -> Path:
    candidates = [build_dir / "examples/DSP/graphx-dsp-fhss-demo",
                  build_dir / "bin/graphx-dsp-fhss-demo"]
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise RuntimeError(f"graphx-dsp-fhss-demo not found under {build_dir}")


def request(port: int, method: str, target: str, body: bytes | None = None,
            headers: dict[str, str] | None = None) -> tuple[int, dict[str, str], bytes]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
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


def launch(args: argparse.Namespace) -> tuple[subprocess.Popen[str], str, int, list[str]]:
    executable = locate_executable(args.build_dir.resolve())
    command = [str(executable), "--dashboard-no-run", "--dashboard-port", "0"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        line = process.stdout.readline() if process.stdout else ""
        if line.startswith("Dashboard URL:"):
            url = line.split(": ", 1)[1].strip()
            return process, url, int(url.rsplit(":", 1)[1]), command
        if process.poll() is not None:
            break
    process.terminate()
    raise RuntimeError("dashboard did not publish its bound URL")


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def exercise(args: argparse.Namespace) -> int:
    if args.phase != PHASE:
        raise RuntimeError("this operator implements Phase 1 only")
    output = args.output_dir.resolve()
    output_preexisted = output.exists()
    if output_preexisted and any(output.iterdir()):
        raise RuntimeError(f"refusing preexisting nonempty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / OWNED_MARKER).write_text(
        f"phase=1\ncreated_dir={0 if output_preexisted else 1}\n", encoding="utf-8")
    checks: list[dict[str, object]] = []

    def check(name: str, condition: bool, evidence: str) -> None:
        checks.append({"name": name, "pass": bool(condition), "evidence": evidence})

    process = None
    command: list[str] = []
    url = ""
    port = 0
    try:
        process, url, port, command = launch(args)
        status, _, page = request(port, "GET", "/")
        check("packaged FHSS page", status == 200 and b"GraphX FHSS Dashboard" in page and
              b"Synthetic IQ evaluation only" in page, f"HTTP {status}; bytes={len(page)}")
        schema_names = {
            "/healthz": "health", "/readyz": "readiness", "/api/v1/version": "version",
            "/api/v1/fhss/graph": "graph", "/api/v1/fhss/config": "config",
            "/api/v1/fhss/status": "runtime-status", "/api/v1/fhss/metrics": "metrics",
            "/api/v1/fhss/diagnostics": "diagnostics", "/api/v1/fhss/events?client_id=operator": "events",
            "/api/v1/fhss/metrics/edges": "edge-metrics",
            "/api/v1/fhss/config/authoritative": "scenario",
            "/api/v1/fhss/config/effective": "config",
            "/api/v1/fhss/config/derived-paths": "derived-paths",
            "/api/v1/fhss/config/value?pointer=/fhss/scenario": "value",
            "/api/v1/fhss/nodes/source": "node",
            "/api/v1/fhss/nodes/source/parameters": "node-parameters",
            "/api/v1/fhss/visualization": "visualization",
        }
        schema_root = API_DIR / "schemas"
        for target, schema_name in schema_names.items():
            status, headers, body = request(port, "GET", target)
            parsed = json.loads(body)
            schema_ok = isinstance(parsed, dict)
            try:
                schema = json.loads((schema_root / f"{schema_name}.schema.json").read_text())
                Draft202012Validator.check_schema(schema)
                Draft202012Validator(schema).validate(parsed)
                validate_instance(parsed, schema)
            except (ValueError, KeyError) as error:
                schema_ok = False
                checks.append({"name": f"schema {target}", "pass": False, "evidence": str(error)})
            check(f"GET {target}", status == 200 and schema_ok, f"HTTP {status}; schema={schema_ok}")
            if target == "/healthz":
                required = ("content-security-policy", "x-content-type-options",
                            "referrer-policy", "x-frame-options")
                check("defensive response headers", all(item in headers for item in required), str(required))
        status, headers, body = request(port, "PUT", "/healthz")
        problem = json.loads(body)
        check("unsupported method", status == 405 and "allow" in headers, f"HTTP {status}")
        check("RFC 9457 problem", headers.get("content-type", "").startswith("application/problem+json")
              and problem.get("status") == status and all(k in problem for k in ("type", "title", "detail")),
              problem.get("type", "missing"))
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
        source_root = Path(__file__).resolve().parents[4]
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source_root,
                                  text=True, capture_output=True, check=False).stdout.strip() or "unknown"
        compiler = subprocess.run(["c++", "--version"], text=True, capture_output=True,
                                  check=False).stdout.splitlines()
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": PHASE,
            "source_revision": revision, "compiler": compiler[0] if compiler else "unknown",
            "build_profile": args.build_dir.name, "platform": platform.platform(),
            "commands": [command], "bound_address": "127.0.0.1", "bound_port": port,
            "dashboard_url": url, "api_version": "v1", "synthetic_data_only": True,
            "hwil_available": False, "production_rf_qualified": False,
            "input_hashes": {"openapi": sha256(Path(__file__).resolve().parents[1] / "api/openapi.json")},
            "artifact_hashes": {"dashboard_index": sha256(Path(__file__).resolve().parents[1] / "index.html")},
            "checks": checks,
            "result": "PASS" if all(item["pass"] for item in checks) else "FAIL"
        }
        report_path = output / "phase1-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 0 if report["result"] == "PASS" else 1
    except Exception as error:
        checks.append({"name": "operator execution", "pass": False, "evidence": str(error)})
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": PHASE,
            "source_revision": "unknown", "compiler": "unknown",
            "build_profile": args.build_dir.name, "platform": platform.platform(),
            "commands": [command] if command else [], "bound_address": "127.0.0.1",
            "bound_port": port or 1, "dashboard_url": url, "api_version": "v1",
            "synthetic_data_only": True, "hwil_available": False,
            "production_rf_qualified": False,
            "input_hashes": {"openapi": sha256(API_DIR / "openapi.json")},
            "artifact_hashes": {"dashboard_index": sha256(Path(__file__).resolve().parents[1] / "index.html")},
            "checks": checks, "result": "FAIL"
        }
        report_path = output / "phase1-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 1
    finally:
        if process is not None:
            stop(process)


def verify(args: argparse.Namespace) -> int:
    report_path = args.output_dir.resolve() / "phase1-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    schema = json.loads((Path(__file__).resolve().parent / "schemas/operator-report.schema.json").read_text())
    try:
        validate_schema(schema, "operator-report.schema.json")
        validate_instance(report, schema)
        dashboard_index = Path(__file__).resolve().parents[1] / "index.html"
        openapi = Path(__file__).resolve().parents[1] / "api/openapi.json"
        hashes_valid = (report.get("artifact_hashes", {}).get("dashboard_index") == sha256(dashboard_index)
                        and report.get("input_hashes", {}).get("openapi") == sha256(openapi))
    except ValueError:
        hashes_valid = False
    valid = report.get("phase") == PHASE and report.get("result") == "PASS" and all(
        item.get("pass") is True for item in report.get("checks", [])) and hashes_valid
    print("PASS" if valid else "FAIL")
    return 0 if valid else 1


def cleanup(args: argparse.Namespace) -> int:
    output = args.output_dir.resolve()
    marker = output / OWNED_MARKER
    if not marker.is_file():
        raise RuntimeError("refusing to remove an unmarked directory")
    created_dir = "created_dir=1" in marker.read_text(encoding="utf-8").splitlines()
    for name in ("phase1-report.json", OWNED_MARKER):
        tracked = output / name
        if tracked.is_file(): tracked.unlink()
    if created_dir:
        output.rmdir()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("serve", "exercise", "verify", "report", "cleanup"):
        item = commands.add_parser(name)
        item.add_argument("--phase", type=int, default=PHASE)
        item.add_argument("--build-dir", type=Path, default=Path("build-ninja/ninja-debug"))
        item.add_argument("--output-dir", type=Path, required=name != "serve",
                          default=Path("fhss-dashboard-operator-output"))
    args = parser.parse_args()
    if args.command == "exercise": return exercise(args)
    if args.command == "verify": return verify(args)
    if args.command == "cleanup": return cleanup(args)
    if args.command == "report":
        print((args.output_dir.resolve() / "phase1-report.json").read_text(encoding="utf-8"))
        return 0
    process, url, _, _ = launch(args)
    print(url, flush=True)
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
