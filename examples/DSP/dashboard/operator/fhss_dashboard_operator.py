#!/usr/bin/env python3
"""External Phase 1/2 operator for the loopback-only GraphX FHSS dashboard."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import http.client
import json
import os
import platform
import selectors
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1] / "api"
sys.path.insert(0, str(API_DIR))
from schema_subset import load_registry, validate_instance, validate_schema  # noqa: E402
try:
    from jsonschema import Draft202012Validator
except ImportError as error:
    raise SystemExit("install ../api/requirements-contracts.lock for authoritative live validation") from error

PHASE = 2
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


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def exercise(args: argparse.Namespace) -> int:
    if args.phase not in (1, 2):
        raise RuntimeError("this operator implements Phase 1 and Phase 2 only")
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
        status, _, page = request(port, "GET", "/")
        check("packaged FHSS page", status == 200 and b"GraphX FHSS Dashboard" in page and
              b"Synthetic IQ evaluation only" in page, f"HTTP {status}; bytes={len(page)}")
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
        schema_root = API_DIR / "schemas"
        live_registry = load_registry(list(schema_root.glob("*.schema.json")))
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
            Draft202012Validator(schema).validate(parsed)
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
            persist("authoritative", authoritative_bytes)
            serialized_receiver = json.dumps(receiver, sort_keys=True)
            source = next((node for node in receiver.get("nodes", []) if node.get("id") == "source"), {})
            minimal_nodes = [node for node in receiver.get("nodes", [])
                             if node.get("id") in ("preamble", "assembler")]
            forbidden = ("messages", "truth_from_fixture", "truth_path", "generator_metadata")
            check("receiver-minimal truth separation", receiver_status == 200 and
                  source.get("type") == "FHSSBinaryIqFileSourceNode" and
                  all(token not in serialized_receiver for token in forbidden) and
                  all("active_frequency_indices" not in node.get("node_config", {})
                      for node in minimal_nodes), f"HTTP {receiver_status}; source={source.get('type')}")
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
        source_root = Path(__file__).resolve().parents[4]
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source_root,
                                  text=True, capture_output=True, check=False).stdout.strip() or "unknown"
        compiler = subprocess.run(["c++", "--version"], text=True, capture_output=True,
                                  check=False).stdout.splitlines()
        schema_hashes = {f"schema:{path.name}": sha256(path)
                         for path in sorted((API_DIR / "schemas").glob("*.json"))}
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
            "result": "PASS" if all(item["pass"] for item in checks) else "FAIL"
        }
        report_path = output / f"phase{phase}-report.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(report_path)
        return 0 if report["result"] == "PASS" else 1
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
            "checks": checks, "result": "FAIL"
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
            evidence = evidence_dir / (hashlib.sha256(key.encode()).hexdigest() + ".bin")
            hashes_valid = hashes_valid and evidence.is_file() and sha256(evidence) == digest
    except ValueError:
        hashes_valid = False
    valid = report.get("phase") == args.phase and report.get("result") == "PASS" and all(
        item.get("pass") is True for item in report.get("checks", [])) and hashes_valid
    print("PASS" if valid else "FAIL")
    return 0 if valid else 1


def cleanup(args: argparse.Namespace) -> int:
    output = args.output_dir.resolve()
    marker = output / OWNED_MARKER
    if not marker.is_file():
        raise RuntimeError("refusing to remove an unmarked directory")
    created_dir = "created_dir=1" in marker.read_text(encoding="utf-8").splitlines()
    for name in (f"phase{args.phase}-report.json", OWNED_MARKER):
        tracked = output / name
        if tracked.is_file(): tracked.unlink()
    evidence_dir = output / "artifacts"
    if evidence_dir.is_dir():
        for artifact in evidence_dir.iterdir():
            if artifact.is_file(): artifact.unlink()
        evidence_dir.rmdir()
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
        print((args.output_dir.resolve() / f"phase{args.phase}-report.json").read_text(encoding="utf-8"))
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
