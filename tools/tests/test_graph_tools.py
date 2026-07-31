#!/usr/bin/env python3
"""Executable-level contract checks for graph-cli and graphx-dashboard."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
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

def wait_until_http(
    base_url: str, process: subprocess.Popen[str], expected_status: int
) -> str:
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
            if status == expected_status:
                return document
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.05)
    raise AssertionError(
        f"dashboard did not return expected HTTP {expected_status}"
    )


def stop_dashboard(process: subprocess.Popen[str]) -> None:
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


def start_dashboard(
    dashboard: Path, graph: Path
) -> tuple[subprocess.Popen[str], str]:
    port = available_port()
    process = subprocess.Popen(
        [str(dashboard), "--graph", str(graph), "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return process, f"http://127.0.0.1:{port}"


def verify_cli(cli: Path, graph: Path, work_dir: Path) -> None:
    count = run(str(cli), "--graph", str(graph), "node-count")
    if "DEPRECATED: graph-cli" not in count.stderr:
        raise AssertionError("graph-cli did not emit its deprecation warning")
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
    help_result = run(str(dashboard), "--help")
    if "--enable-execution" in help_result.stdout:
        raise AssertionError("dashboard help still advertises --enable-execution")
    run(
        str(dashboard),
        "--graph",
        str(graph),
        "--enable-execution",
        expected=2,
    )
    run(str(dashboard), "--graph", str(graph), "--port", "0", expected=2)
    run(str(dashboard), "--graph", str(graph), "--port", "65536", expected=2)
    run(str(dashboard), "--graph", str(graph) + ".missing", expected=1)

    process, base_url = start_dashboard(dashboard, graph)
    try:
        index = wait_until_ready(base_url, process)
        if "GraphX Management Dashboard" not in index:
            raise AssertionError("dashboard did not serve the checked-in UI")

        status, graph_body = request(f"{base_url}/api/v1/graph")
        if status != 200 or len(json.loads(graph_body)["data"]["nodes"]) != 2:
            raise AssertionError("dashboard graph API did not return both nodes")

        status, state_body = request(f"{base_url}/api/v1/execution/state")
        state = json.loads(state_body)["data"]
        if (
            status != 200
            or state["state"] != "CONFIGURED"
            or state["coordinator_revision"] != 0
            or state["configured_revision"] != 0
            or state["active_revision"] is not None
            or state["graph_generation"] != 1
            or state["configuration_dirty"]
        ):
            raise AssertionError(
                f"dashboard did not start as a clean lazy executor: {state_body}"
            )

        status, commands_body = request(
            f"{base_url}/api/v1/execution/commands"
        )
        command_names = {
            item["name"] for item in json.loads(commands_body)["data"]
        }
        if status != 200 or command_names != {
            "configure",
            "init",
            "start",
            "run",
            "stop",
            "join",
        }:
            raise AssertionError(
                f"typed command discovery was incomplete: {commands_body}"
            )

        status, _ = request(
            f"{base_url}/api/v1/nodes/source_1",
            method="PATCH",
            body={"node_config": {"message_count": 25}},
        )
        if status != 200:
            raise AssertionError(f"node parameter update returned HTTP {status}")

        status, state_body = request(f"{base_url}/api/v1/execution/state")
        state = json.loads(state_body)["data"]
        if (
            status != 200
            or state["coordinator_revision"] != 1
            or state["configured_revision"] != 0
            or not state["configuration_dirty"]
        ):
            raise AssertionError(
                f"coordinator edit did not dirty the executor: {state_body}"
            )

        status, _ = request(
            f"{base_url}/api/v1/execution/init", method="POST"
        )
        if status != 409:
            raise AssertionError(
                "dirty executor unexpectedly accepted init"
            )

        status, configure_body = request(
            f"{base_url}/api/v1/execution/commands/configure",
            method="POST",
        )
        configured = json.loads(configure_body)["data"]
        if (
            status != 200
            or configured["state"] != "CONFIGURED"
            or configured["configured_revision"] != 1
            or configured["active_revision"] is not None
            or configured["graph_generation"] != 2
            or configured["configuration_dirty"]
        ):
            raise AssertionError(
                f"configure did not atomically adopt the edit: {configure_body}"
            )

        status, _ = request(
            f"{base_url}/api/v1/execution/commands/unknown",
            method="POST",
        )
        if status != 404:
            raise AssertionError("unknown typed command did not return 404")

        status, _ = request(
            f"{base_url}/api/v1/execution/operations/not-an-operation"
        )
        if status != 404:
            raise AssertionError("unknown operation did not return 404")
    finally:
        stop_dashboard(process)


def verify_installed_dashboard_resources(
    dashboard: Path, graph: Path, work_dir: Path, source_root: Path
) -> None:
    source_assets = source_root / "libgraph" / "resources" / "web"

    def prepare(name: str, omitted: set[str]) -> Path:
        prefix = work_dir / name
        if prefix.exists():
            shutil.rmtree(prefix)
        bin_dir = prefix / "bin"
        resource_dir = prefix / "share" / "graphx" / "dashboard"
        bin_dir.mkdir(parents=True, exist_ok=True)
        resource_dir.mkdir(parents=True, exist_ok=True)
        installed_dashboard = bin_dir / dashboard.name
        shutil.copy2(dashboard, installed_dashboard)
        for relative in (
            Path("index.html"),
            Path("assets/graphx-dashboard.js"),
            Path("assets/graphx-dashboard.css"),
        ):
            if relative.as_posix() in omitted:
                continue
            destination = resource_dir / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_assets / relative, destination)
        for relative in omitted:
            if (resource_dir / relative).exists():
                raise AssertionError(
                    f"stale omitted install fixture asset survived: {relative}"
                )
        return installed_dashboard

    complete = prepare("installed-complete", set())
    process, base_url = start_dashboard(complete, graph)
    try:
        index = wait_until_ready(base_url, process)
        if "GraphX Management Dashboard" not in index:
            raise AssertionError("complete installed dashboard did not serve its index")
        for asset in (
            "assets/graphx-dashboard.js",
            "assets/graphx-dashboard.css",
        ):
            status, body = request(f"{base_url}/{asset}")
            if status != 200 or not body:
                raise AssertionError(
                    f"complete installed dashboard failed to serve {asset}"
                )
    finally:
        stop_dashboard(process)

    missing_index = prepare("installed-missing-index", {"index.html"})
    process, base_url = start_dashboard(missing_index, graph)
    try:
        document = wait_until_http(base_url, process, 503)
        if "ui_unavailable" not in document:
            raise AssertionError(
                "missing installed index did not report ui_unavailable"
            )
    finally:
        stop_dashboard(process)

    for asset in (
        "assets/graphx-dashboard.js",
        "assets/graphx-dashboard.css",
    ):
        installed = prepare(f"installed-missing-{Path(asset).suffix[1:]}", {asset})
        process, base_url = start_dashboard(installed, graph)
        try:
            wait_until_ready(base_url, process)
            status, _ = request(f"{base_url}/{asset}")
            if status != 404:
                raise AssertionError(
                    f"incomplete install borrowed missing {asset} from the source tree"
                )
        finally:
            stop_dashboard(process)


def verify_dashboard_architecture(source_root: Path) -> None:
    generic_sources = [
        source_root / "tools" / "graph-dashboard.cpp",
        source_root / "libgraph" / "include" / "graph" / "GraphHttpServer.hpp",
        source_root / "libgraph" / "src" / "graph" / "GraphHttpServer.cpp",
        source_root / "libgraph" / "resources" / "web" / "index.html",
    ]
    combined = "\n".join(path.read_text(encoding="utf-8") for path in generic_sources)
    for forbidden in (
        "/api/v1/fhss",
        "EmbeddedDashboardServer",
        "GraphRuntimeSession",
        "examples/DSP/dashboard",
    ):
        if forbidden in combined:
            raise AssertionError(
                f"generic dashboard architecture contains forbidden dependency: "
                f"{forbidden}"
            )

    server_header = generic_sources[1].read_text(encoding="utf-8")
    server_source = generic_sources[2].read_text(encoding="utf-8")
    if (
        '"graph/GraphExecutor.hpp"' in server_header
        or "class GraphExecutor;" in server_header
        or "GraphExecutor *" in server_header
        or "executor_->" in server_source
    ):
        raise AssertionError(
            "GraphHttpServer regained direct GraphExecutor lifecycle access"
        )

    cmake = (source_root / "CMakeLists.txt").read_text(encoding="utf-8")
    if cmake.count("add_executable(graphx_graph_dashboard ") != 1:
        raise AssertionError(
            "source tree must define exactly one generic dashboard executable"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dashboard", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--graph", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)
    source_root = Path(__file__).resolve().parents[2]
    verify_cli(args.cli.resolve(), args.graph.resolve(), args.work_dir.resolve())
    verify_dashboard(args.dashboard.resolve(), args.graph.resolve())
    verify_installed_dashboard_resources(
        args.dashboard.resolve(),
        args.graph.resolve(),
        args.work_dir.resolve(),
        source_root,
    )
    verify_dashboard_architecture(source_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
