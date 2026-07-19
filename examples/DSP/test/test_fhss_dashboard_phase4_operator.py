#!/usr/bin/env python3
"""Fast evidence, screenshot, tamper, and cleanup smoke for the Phase 4 operator."""

from __future__ import annotations

import argparse
import base64
import importlib.util
import json
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
OPERATOR_PATH = ROOT / "examples/DSP/dashboard/operator/fhss_dashboard_operator.py"
SPEC = importlib.util.spec_from_file_location("fhss_dashboard_operator", OPERATOR_PATH)
operator = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(operator)

PNG_1X1 = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUB"
    "AScY42YAAAAASUVORK5CYII=")


def png(width: int, height: int, varied: bool = True) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            color = ((20 + x // 5 + y // 9) & 0xFF,
                     (70 + x // 11 + y // 4) & 0xFF,
                     (110 + (x ^ y) // 7) & 0xFF) if varied else (20, 70, 110)
            rows.extend(color)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def write_json(path: Path, document: object) -> None:
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        parent = Path(temporary)
        output = parent / "preexisting-output"
        output.mkdir()
        (output / operator.OWNED_MARKER).write_text(
            "phase=4\ncreated_dir=0\n", encoding="utf-8")
        (output / "phase4-cases").mkdir()
        iq_names = {"clean": "replay.cf32", "impaired": "impaired-cfo-awgn.cf32",
                    "negative": "negative-no-message.cf32"}
        for case, iq_name in iq_names.items():
            iq_path = output / iq_name
            iq_path.write_bytes(b"\0" * 8)
            config = output / "phase4-cases" / f"{case}-config.json"
            write_json(config, {"receiver_input": {"file_path": str(iq_path)}})
            write_json(output / f"phase4-{case}-served-state.json", {
                "case": case, "truth_files_present": False,
                "url": "http://127.0.0.1:18084", "generation": 1,
                "run_epoch": 1,
                "config_sha256": operator.sha256(config), "iq_path": str(iq_path),
                "iq_sha256": operator.sha256(iq_path),
                "observation_id": "observation-g1-r1", "observation_sha256": "a" * 64,
            })
        write_json(output / "phase4-screenshot-manifest.json", {
            "schema": "graphx.dashboard.phase4.screenshot_manifest.v1",
            "required_cases": ["clean", "impaired", "negative"],
            "captured_files": {}, "synthetic_data_only": True,
        })
        jq = shutil.which("jq")
        if jq is None:
            raise AssertionError("jq is required for the receiver audit smoke")
        valid_receiver = parent / "receiver-minimal.json"
        invalid_receiver = parent / "receiver-with-messages.json"
        write_json(valid_receiver, {"graph": {"nodes": [], "edges": []}})
        write_json(invalid_receiver, {
            "graph": {"nodes": [], "edges": [], "messages": []}})
        assert subprocess.run(
            [jq, "-e", operator.RECEIVER_AUDIT_JQ, str(valid_receiver)],
            capture_output=True, check=False).returncode == 0
        assert subprocess.run(
            [jq, "-e", operator.RECEIVER_AUDIT_JQ, str(invalid_receiver)],
            capture_output=True, check=False).returncode != 0
        schema_dir = operator.API_DIR / "schemas"
        report = {
            "schema": "graphx.fhss.dashboard.operator_report.v1", "phase": 4,
            "source_revision": "smoke", "compiler": "smoke", "build_profile": "smoke",
            "platform": "smoke", "commands": [[]], "bound_address": "127.0.0.1",
            "bound_port": 1, "dashboard_url": "http://127.0.0.1:1", "api_version": "v1",
            "synthetic_data_only": True, "hwil_available": False,
            "production_rf_qualified": False,
            "input_hashes": {
                "openapi": operator.sha256(operator.API_DIR / "openapi.json"),
                "operator": operator.sha256(OPERATOR_PATH),
                "operator_report_schema": operator.sha256(
                    OPERATOR_PATH.parent / "schemas/operator-report.schema.json"),
                **{f"schema:{path.name}": operator.sha256(path)
                   for path in sorted(schema_dir.glob("*.json"))},
            },
            "artifact_hashes": {"dashboard_index": operator.sha256(
                OPERATOR_PATH.parents[1] / "index.html")},
            "checks": [{"name": "smoke", "pass": True, "evidence": "synthetic"}],
            "evidence_status": "partial_pre_browser", "result": "PARTIAL",
        }
        write_json(output / "phase4-report.json", report)
        partial_args = argparse.Namespace(
            output_dir=output, phase=4, require_screenshots=False)
        assert operator.verify(partial_args) == 0
        screenshot_source = parent / "capture.png"
        trivial = parent / "trivial.png"
        trivial.write_bytes(PNG_1X1)
        try:
            operator.record_screenshot(argparse.Namespace(
                output_dir=output, case="clean", path=trivial))
            raise AssertionError("trivial screenshot was accepted")
        except RuntimeError:
            pass
        uniform = parent / "uniform.png"
        uniform.write_bytes(png(800, 450, varied=False))
        try:
            operator.record_screenshot(argparse.Namespace(
                output_dir=output, case="clean", path=uniform))
            raise AssertionError("uniform screenshot was accepted")
        except RuntimeError:
            pass
        screenshot_source.write_bytes(png(800, 450))
        for case in ("clean", "impaired", "negative"):
            assert operator.record_screenshot(argparse.Namespace(
                output_dir=output, case=case, path=screenshot_source)) == 0
        unverified_report = json.loads(
            (output / "phase4-report.json").read_text(encoding="utf-8"))
        assert unverified_report["result"] == "PARTIAL"
        assert unverified_report["evidence_status"] == "captures_complete_unverified"
        verify_args = argparse.Namespace(output_dir=output, phase=4, require_screenshots=True)
        assert operator.verify(verify_args) == 0
        final_report = json.loads(
            (output / "phase4-report.json").read_text(encoding="utf-8"))
        assert final_report["result"] == "PASS"
        assert final_report["evidence_status"] == "final_verified"
        manifest_path = output / "phase4-screenshot-manifest.json"
        original_manifest = manifest_path.read_text(encoding="utf-8")
        mismatched_manifest = json.loads(original_manifest)
        mismatched_manifest["captured_files"]["clean"]["served_url"] = (
            "http://127.0.0.1:1")
        write_json(manifest_path, mismatched_manifest)
        assert operator.verify(verify_args) == 1
        manifest_path.write_text(original_manifest, encoding="utf-8")
        assert operator.verify(verify_args) == 0
        clean = output / "screenshots/clean.png"
        clean.write_bytes(clean.read_bytes() + b"tamper")
        assert operator.verify(verify_args) == 1
        assert operator.record_screenshot(argparse.Namespace(
            output_dir=output, case="clean", path=screenshot_source)) == 0
        assert operator.verify(verify_args) == 0
        assert operator.cleanup(argparse.Namespace(output_dir=output, phase=4)) == 0
        assert output.is_dir() and not any(output.iterdir())

        # Reproduce an exercise-owned output directory at the point of the
        # original cleanup failure: every other exact artifact was removed,
        # leaving only the independent oracle and ownership marker.
        exercise_output = parent / "exercise-output"
        exercise_output.mkdir()
        (exercise_output / operator.OWNED_MARKER).write_text(
            "phase=4\ncreated_dir=1\n", encoding="utf-8")
        write_json(exercise_output / "phase4-clean-independent-oracle.json", {
            "schema": "graphx.dashboard.phase4.independent_oracle.v1",
            "pulses": [],
        })
        assert operator.cleanup(argparse.Namespace(
            output_dir=exercise_output, phase=4)) == 0
        assert not exercise_output.exists()
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
