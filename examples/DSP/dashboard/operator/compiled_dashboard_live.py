#!/usr/bin/env python3
"""Live source/install acceptance lane for the single compiled dashboard."""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from fhss_dashboard_operator import FirefoxBidiSession, launch, request, stop


def document(port: int, method: str, target: str, body: object | None = None,
             headers: dict[str, str] | None = None,
             expected: tuple[int, ...] = (200,)) -> tuple[dict[str, str], dict[str, object]]:
    encoded = None if body is None else json.dumps(body).encode()
    status, response_headers, payload = request(
        port, method, target, encoded, headers, timeout=30)
    if status not in expected:
        raise RuntimeError(
            f"{method} {target}: HTTP {status}: {payload.decode(errors='replace')}")
    parsed = json.loads(payload) if payload else {}
    if not isinstance(parsed, dict):
        raise RuntimeError(f"{method} {target}: response is not an object")
    return response_headers, parsed


def require_schema(value: dict[str, object], expected: str) -> None:
    if value.get("schema") != expected:
        raise RuntimeError(f"expected {expected}, received {value.get('schema')}")


def wait_job(port: int, job_id: str) -> dict[str, object]:
    deadline = time.monotonic() + 90
    latest: dict[str, object] = {}
    while time.monotonic() < deadline:
        _, latest = document(port, "GET", f"/api/v1/fhss/jobs/{job_id}")
        if latest.get("state") in ("completed", "failed", "cancelled", "timed_out"):
            return latest
        time.sleep(.05)
    raise RuntimeError(f"job did not terminate: {latest}")


def wait_operation(port: int, operation_id: str) -> dict[str, object]:
    deadline = time.monotonic() + 90
    latest: dict[str, object] = {}
    while time.monotonic() < deadline:
        _, latest = document(
            port, "GET", f"/api/v1/fhss/investigations/operations/{operation_id}")
        if latest.get("state") in ("completed", "failed", "cancelled", "timed_out"):
            return latest
        time.sleep(.05)
    raise RuntimeError(f"investigation did not terminate: {latest}")


def websocket_recovery(port: int, url: str) -> None:
    from websockets.sync.client import connect

    endpoint = f"ws://127.0.0.1:{port}/api/v1/fhss/events/stream"

    def subscribe(epoch: str, sequence: int):
        client = connect(endpoint, origin=url, open_timeout=5, close_timeout=2,
                         max_size=256 * 1024, compression=None)
        hello = json.loads(client.recv(timeout=5))
        require_schema(hello, "graphx.dashboard.websocket_hello.v1")
        client.send(json.dumps({
            "action": "subscribe", "client_id": "compiled-dashboard-live",
            "publisher_epoch": epoch, "last_sequence": sequence,
        }))
        return client, hello

    first, hello = subscribe("", 0)
    try:
        event = json.loads(first.recv(timeout=5))
        if event.get("schema") not in (
                "graphx.dashboard.event.v1",
                "graphx.dashboard.websocket_resync_required.v1"):
            raise RuntimeError(f"unexpected WebSocket replay: {event}")
        sequence = int(event.get("sequence", hello["latest_sequence"]))
    finally:
        first.close()
    second, second_hello = subscribe(
        str(hello["publisher_epoch"]), max(0, sequence - 1))
    try:
        replay = json.loads(second.recv(timeout=5))
        if (second_hello.get("publisher_epoch") != hello.get("publisher_epoch") or
                replay.get("schema") not in (
                    "graphx.dashboard.event.v1",
                    "graphx.dashboard.websocket_resync_required.v1")):
            raise RuntimeError("WebSocket reconnect did not resume or resynchronize")
    finally:
        second.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    launch_args = argparse.Namespace(
        build_dir=args.build_dir, output_dir=args.output_dir, phase=5,
        port=0, case=None, investigation_qualification=False)
    process, url, port, _ = launch(launch_args)
    try:
        _, health = document(port, "GET", "/healthz")
        _, readiness = document(port, "GET", "/readyz")
        if health != {"status": "ok"} or not isinstance(readiness.get("ready"), bool):
            raise RuntimeError("health/readiness contract mismatch")

        config_headers, config = document(
            port, "GET", "/api/v1/fhss/config/effective")
        require_schema(config, "graphx.dashboard.config.v1")
        etag = config_headers.get("etag", "")
        if (not etag or etag != config.get("etag") or
                not isinstance(config.get("derived_paths"), list)):
            raise RuntimeError("effective configuration ETag/derived_paths mismatch")
        _, provenance = document(
            port, "GET", "/api/v1/fhss/config/provenance")
        require_schema(provenance, "graphx.dashboard.configuration_provenance.v1")
        if not isinstance(provenance.get("provenance"), list):
            raise RuntimeError("configuration provenance must be an array")

        # A maintained browser loads the compiled bundle against live API data.
        with FirefoxBidiSession(args.output_dir) as browser:
            browser.navigate(url)
            browser.wait_for(
                "document.querySelector('#configuration-patch') !== null")
            browser.wait_for(
                "[...document.querySelectorAll('button')].some("
                "b => b.textContent.includes('Apply with captured If-Match') && !b.disabled)")
            browser.wait_for(
                "document.body.innerText.includes('Captured authoritative ETag:')"
                " && !document.body.innerText.includes('Captured authoritative ETag: unavailable')")

        patch = [{"op": "replace", "path": "/iq_center_frequency_hz",
                  "value": 1240000001.0}]
        patch_headers = {
            "Content-Type": "application/json-patch+json", "If-Match": etag}
        _, validation = document(
            port, "POST", "/api/v1/fhss/config/validate", patch, patch_headers)
        require_schema(validation, "graphx.dashboard.config_validation.v1")
        after_validate_headers, _ = document(
            port, "GET", "/api/v1/fhss/config/effective")
        if after_validate_headers.get("etag") != etag:
            raise RuntimeError("validation mutated authoritative configuration")
        _, applied = document(
            port, "PATCH", "/api/v1/fhss/config", patch, patch_headers)
        require_schema(applied, "graphx.dashboard.config_result.v1")
        revision = int(applied["new_revision"])
        new_etag = str(applied["etag"])
        effective_headers, effective = document(
            port, "GET", "/api/v1/fhss/config/effective")
        _, provenance = document(port, "GET", "/api/v1/fhss/config/provenance")
        if (effective_headers.get("etag") != new_etag or
                effective.get("config_revision") != revision or
                provenance.get("config_revision") != revision):
            raise RuntimeError("applied configuration identity was not published")

        # The committed step command is the public one-shot lifecycle path: it
        # generates synthetic IQ, rebuilds, starts, and waits for receiver
        # terminal state without exposing generator truth to the receiver.
        _, submitted = document(
            port, "POST", "/api/v1/fhss/commands/step",
            {"request_id": "compiled-dashboard-live-step", "timeout_ms": 60000},
            {"Content-Type": "application/json",
             "Idempotency-Key": "compiled-dashboard-live-step"}, (202,))
        require_schema(submitted, "graphx.dashboard.fhss_job.v1")
        terminal = wait_job(port, str(submitted["job_id"]))
        if terminal.get("state") != "completed":
            raise RuntimeError(f"step job failed: {terminal}")
        work = terminal.get("work")
        lifecycle = terminal.get("graph_lifecycle")
        if (not isinstance(work, dict) or
                work.get("generator_invoked") is not True or
                work.get("receiver_replay_invoked") is not True or
                not isinstance(lifecycle, dict)):
            raise RuntimeError("step job did not exercise generator/rebuild/start lifecycle")
        _, stopped = document(
            port, "POST", "/api/v1/fhss/commands/stop",
            {"command_id": "compiled-dashboard-live-stop"},
            {"Content-Type": "application/json"}, (200, 202))
        require_schema(stopped, "graphx.dashboard.command_result.v1")

        resources = {
            "/api/v1/fhss/status": "graphx.dashboard.runtime_status.v1",
            "/api/v1/fhss/metrics": "graphx.dashboard.metrics.v1",
            "/api/v1/fhss/diagnostics": "graphx.dashboard.diagnostics.v1",
            "/api/v1/fhss/snapshot": "graphx.dashboard.fhss_snapshot.v1",
            "/api/v1/fhss/expected-truth": "graphx.dashboard.fhss_expected_truth.v1",
            "/api/v1/fhss/observations": "graphx.dashboard.fhss_receiver_observation.v1",
            "/api/v1/fhss/comparison": "graphx.dashboard.fhss_comparison_result.v1",
            "/api/v1/fhss/spectrum?channel=17&fft_size=128":
                "graphx.dashboard.fhss_receiver_spectrum.v1",
            "/api/v1/fhss/jobs": "graphx.dashboard.fhss_job_history.v1",
            "/api/v1/fhss/investigations/operations":
                "graphx.dashboard.fhss_investigation_operations.v1",
        }
        for target, expected_schema in resources.items():
            _, value = document(port, "GET", target)
            require_schema(value, expected_schema)
        _, selected = document(
            port, "GET", "/api/v1/fhss/spectrum?channel=17&fft_size=128")
        if selected.get("channel_index") != 17:
            raise RuntimeError("selected physical spectrum channel was not honored")

        bundle_name = f"compiled-dashboard-live-{str(terminal['job_id'])[-8:]}"
        _, exported = document(
            port, "POST", "/api/v1/fhss/investigations/exports",
            {"request_id": "compiled-dashboard-live-export",
             "bundle_name": bundle_name,
             "job_id": terminal["job_id"], "iq_mode": "reference",
             "timeout_ms": 60000},
            {"Content-Type": "application/json",
             "Idempotency-Key": "compiled-dashboard-live-export"}, (202,))
        export_terminal = wait_operation(port, str(exported["operation_id"]))
        if export_terminal.get("state") != "completed":
            raise RuntimeError(f"investigation export failed: {export_terminal}")
        _, validated = document(
            port, "POST", "/api/v1/fhss/investigations/import-validations",
            {"request_id": "compiled-dashboard-live-validation",
             "bundle_name": bundle_name, "timeout_ms": 60000},
            {"Content-Type": "application/json",
             "Idempotency-Key": "compiled-dashboard-live-validation"}, (202,))
        if wait_operation(port, str(validated["operation_id"])).get("state") != "completed":
            raise RuntimeError("investigation validation failed")

        _, events = document(
            port, "GET",
            "/api/v1/fhss/events?client_id=compiled-dashboard-live&last_sequence=0")
        require_schema(events, "graphx.dashboard.events_batch.v1")
        if int(events.get("latest_sequence", 0)) < 1:
            raise RuntimeError("event polling did not expose the publisher cursor")
        websocket_recovery(port, url)
        print("compiled dashboard live source/install acceptance: PASS")
        return 0
    finally:
        stop(process)


if __name__ == "__main__":
    raise SystemExit(main())
