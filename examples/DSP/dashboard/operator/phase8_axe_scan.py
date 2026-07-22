#!/usr/bin/env python3
"""Capture provenance-bound raw axe-core 4.12.1 output in maintained Firefox."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import uuid
from pathlib import Path

from fhss_dashboard_operator import FirefoxBidiSession
from phase8_completion_report import (
    AXE_CORE_VERSION, AXE_LICENSE_SHA256, AXE_MIN_SHA256,
    AXE_REGISTRY_INTEGRITY, AXE_REQUIRED_TAGS,
    AXE_TARBALL_SHA256, AXE_TARBALL_SHA512, AXE_TARBALL_URL,
    AXE_THIRD_PARTY_LICENSE_SHA256, validate_raw_axe_report)


def digest(path: Path, algorithm: str = "sha256") -> str:
    return hashlib.new(algorithm, path.read_bytes()).hexdigest()


def require_digest(path: Path, expected: str, algorithm: str = "sha256") -> None:
    if not path.is_file() or digest(path, algorithm) != expected:
        raise RuntimeError(f"pinned axe-core artifact mismatch: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dashboard-url", required=True)
    parser.add_argument("--axe-tarball", type=Path, required=True)
    parser.add_argument("--axe-package-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--label", choices=("source", "installed"), required=True)
    args = parser.parse_args()
    if not args.dashboard_url.startswith("http://127.0.0.1:"):
        raise RuntimeError("axe qualification requires an explicit IPv4 loopback URL")

    tarball = args.axe_tarball.resolve()
    package_root = args.axe_package_root.resolve()
    axe_min = package_root / "axe.min.js"
    license_path = package_root / "LICENSE"
    third_party_license = package_root / "LICENSE-3RD-PARTY.txt"
    require_digest(tarball, AXE_TARBALL_SHA256)
    require_digest(tarball, AXE_TARBALL_SHA512, "sha512")
    require_digest(axe_min, AXE_MIN_SHA256)
    require_digest(license_path, AXE_LICENSE_SHA256)
    require_digest(third_party_license, AXE_THIRD_PARTY_LICENSE_SHA256)

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = f"{args.label}-axe-core-{AXE_CORE_VERSION}"
    evidence_files = {
        "package":output / f"{prefix}.tgz",
        "axe_min":output / f"{prefix}-axe.min.js",
        "license":output / f"{prefix}-LICENSE",
        "third_party":output / f"{prefix}-LICENSE-3RD-PARTY.txt"}
    for source, destination in (
            (tarball, evidence_files["package"]),
            (axe_min, evidence_files["axe_min"]),
            (license_path, evidence_files["license"]),
            (third_party_license, evidence_files["third_party"])):
        shutil.copyfile(source, destination)

    source = axe_min.read_text(encoding="utf-8")
    with FirefoxBidiSession(output) as browser:
        browser.navigate(args.dashboard_url)
        observed_version = browser.evaluate(
            "(() => { const source=" + json.dumps(source) +
            "; (0,eval)(source); return axe.version; })()")
        if observed_version != AXE_CORE_VERSION:
            raise RuntimeError(f"browser loaded unexpected axe-core {observed_version}")
        options = {"runOnly":{"type":"tag","values":sorted(AXE_REQUIRED_TAGS)}}
        encoded = browser.evaluate(
            "axe.run(document," + json.dumps(options) +
            ").then(result => JSON.stringify(result))")
        raw = json.loads(str(encoded))
        browser_version = str(browser.capabilities.get("browserVersion", "unknown"))

    findings = validate_raw_axe_report(raw, args.dashboard_url)
    raw_path = output / f"{args.label}-axe-raw.json"
    raw_path.write_text(json.dumps(raw, indent=2) + "\n", encoding="utf-8")
    record = {
        "schema":"graphx.fhss.dashboard.phase8_accessibility_engine.v1",
        "tree":args.label,
        "execution_id":f"axe-{args.label}-{uuid.uuid4()}",
        "executed_at":str(raw["timestamp"]),
        "engine":"axe-core", "engine_version":AXE_CORE_VERSION,
        "browser":f"Firefox/{browser_version}",
        "dashboard_url":args.dashboard_url.rstrip("/"),
        "result":"PASS" if all(
            findings[kind]["counts"][impact] == 0
            for kind in ("violations", "incomplete")
            for impact in ("critical", "serious")) else "FAIL",
        "violations":findings["violations"],
        "incomplete":findings["incomplete"],
        "report_path":raw_path.name,
        "report_sha256":digest(raw_path),
        "provenance":{
            "package_name":"axe-core", "package_version":AXE_CORE_VERSION,
            "tarball_url":AXE_TARBALL_URL,
            "registry_integrity":AXE_REGISTRY_INTEGRITY,
            "package_path":evidence_files["package"].name,
            "package_sha256":digest(evidence_files["package"]),
            "package_sha512":digest(evidence_files["package"], "sha512"),
            "axe_min_path":evidence_files["axe_min"].name,
            "axe_min_sha256":digest(evidence_files["axe_min"]),
            "license_path":evidence_files["license"].name,
            "license_sha256":digest(evidence_files["license"]),
            "third_party_license_path":evidence_files["third_party"].name,
            "third_party_license_sha256":digest(evidence_files["third_party"])}}
    record_path = output / f"{args.label}-axe.json"
    record_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(record_path)
    return 0 if record["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
