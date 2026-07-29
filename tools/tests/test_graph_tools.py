#!/usr/bin/env python3
"""Executable-level contract checks for graph-cli and graphx-dashboard."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import signal
import socket
import subprocess
import time
import urllib.error
import urllib.request


def run(*arguments: str, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        arguments, check=False, capture_output=True, text=True, timeout=10
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{arguments!r} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def available_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def request(
    url: str, *, method: str = "GET", body: dict[str, object] | None = None
) -> tuple[int, str]:
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {} if data is None else {"Content-Type": "application/json"}
    call = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(call, timeout=2) as response:
            return response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8")


def wait_until_ready(base_url: str, process: subprocess.Popen[str]) -> str:
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(
                f"dashboard exited before readiness ({process.returncode})\n"
                f"stdout:\n{stdout}\nstderr:\n{stderr}"
            )
        try:
            status, document = request(f"{base_url}/")
            if status == 200:
                return document
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.05)
    raise AssertionError("dashboard did not become ready")


def verify_cli(cli: Path, graph: Path, work_dir: Path) -> None:
    count = run(str(cli), "--graph", str(graph), "node-count")
    if count.stdout.strip() != "2":
        raise AssertionError(f"unexpected node count: {count.stdout!r}")

    shown = run(str(cli), "--graph", str(graph), "show", "--format", "json")
    parsed = json.loads(shown.stdout)
    if len(parsed.get("nodes", [])) != 2:
        raise AssertionError("CLI JSON output did not preserve both nodes")

    run(str(cli), "--graph", str(work_dir / "missing.json"), "node-count",
        expected=1)
    malformed = work_dir / "malformed.json"
    malformed.write_text('{"nodes":', encoding="utf-8")
    run(str(cli), "--graph", str(malformed), "node-count", expected=1)


def verify_dashboard(dashboard: Path, graph: Path) -> None:
    run(str(dashboard), "--graph", str(graph), "--port", "0", expected=2)
    run(str(dashboard), "--graph", str(graph), "--port", "65536", expected=2)
    run(str(dashboard), "--graph", str(graph) + ".missing", expected=1)

    port = available_port()
    process = subprocess.Popen(
        [str(dashboard), "--graph", str(graph), "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        base_url = f"http://127.0.0.1:{port}"
        index = wait_until_ready(base_url, process)
        if "GraphX Management Dashboard" not in index:
            raise AssertionError("dashboard did not serve the checked-in UI")

        status, graph_body = request(f"{base_url}/api/v1/graph")
        if status != 200 or len(json.loads(graph_body)["data"]["nodes"]) != 2:
            raise AssertionError("dashboard graph API did not return both nodes")

        status, _ = request(
            f"{base_url}/api/v1/nodes/source_1",
            method="PATCH",
            body={"node_config": {"rate_hz": 25}},
        )
        if status != 200:
            raise AssertionError(f"node parameter update returned HTTP {status}")

        status, _ = request(
            f"{base_url}/api/v1/execution/init", method="POST"
        )
        if status != 501:
            raise AssertionError(
                "inspection-only dashboard unexpectedly enabled execution"
            )
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=5)
            raise AssertionError("dashboard did not stop after SIGTERM")
    if process.returncode != 0:
        raise AssertionError(f"dashboard shutdown returned {process.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dashboard", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--graph", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)
    verify_cli(args.cli.resolve(), args.graph.resolve(), args.work_dir.resolve())
    verify_dashboard(args.dashboard.resolve(), args.graph.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
