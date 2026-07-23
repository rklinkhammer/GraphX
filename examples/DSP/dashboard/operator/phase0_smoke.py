#!/usr/bin/env python3
"""Small public-interface smoke test for source or installed dashboard trees."""

from __future__ import annotations

import argparse
import json
import signal
import subprocess
import re
import urllib.error
import urllib.request
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--graph-config", type=Path, required=True)
    args = parser.parse_args()
    process = subprocess.Popen([
        str(args.executable), "--graph-config", str(args.graph_config),
        "--dashboard", "--dashboard-host", "127.0.0.1", "--dashboard-port", "0",
        "--dashboard-assets", str(args.assets)], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True)
    try:
        assert process.stdout is not None
        line = process.stdout.readline().strip()
        if not line.startswith("Dashboard URL: "):
            raise RuntimeError(f"dashboard did not publish its URL: {line}")
        base_url = line.removeprefix("Dashboard URL: ")
        with urllib.request.urlopen(base_url + "/", timeout=10) as response:
            body = response.read()
            if response.status != 200 or b"GraphX FHSS Dashboard" not in body:
                raise RuntimeError("root dashboard smoke failed")
        for asset in re.findall(rb'(?:src|href)="(/assets/[^"]+)"', body):
            with urllib.request.urlopen(base_url + asset.decode(), timeout=10) as response:
                content_type = response.headers.get("Content-Type", "")
                if response.status != 200 or not content_type.startswith(
                        "text/javascript" if asset.endswith(b".js") else "text/css"):
                    raise RuntimeError("compiled asset MIME smoke failed")
        with urllib.request.urlopen(base_url + "/api/v1/fhss/graph",
                                    timeout=10) as response:
            graph = json.loads(response.read())
            if response.status != 200 or graph.get("schema") != "graphx.dashboard.graph.v1":
                raise RuntimeError("FHSS graph API smoke failed")
        for forbidden in ("/api/v2", "/legacy", "/v2"):
            try:
                urllib.request.urlopen(base_url + forbidden, timeout=10)
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    raise RuntimeError(f"unexpected status for {forbidden}: {error.code}")
            else:
                raise RuntimeError(f"alternate route unexpectedly exists: {forbidden}")
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
        try:
            process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise RuntimeError("dashboard smoke process did not stop cleanly")
    if process.returncode != 0:
        raise RuntimeError(f"dashboard smoke process exited {process.returncode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
