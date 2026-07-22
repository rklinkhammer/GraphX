#!/usr/bin/env python3
"""Create Phase 8 sign-off exclusively from independently produced evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from xml.etree import ElementTree

from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parent
SCHEMAS = ROOT / "schemas"
LANES = {
    "focused": "c++26-focused",
    "full": "c++26-full-regression",
    "sanitizer": "asan-ubsan",
    "concurrency": "tsan",
}
MANUAL_IDS = {
    "keyboard_only", "tabs", "focus", "names_structure", "status_errors",
    "contrast_non_color", "reduced_motion", "zoom_reflow",
    "visualization_alternatives",
}
AXE_CORE_VERSION = "4.12.1"
AXE_TARBALL_URL = (
    "https://registry.npmjs.org/axe-core/-/axe-core-4.12.1.tgz")
AXE_REGISTRY_INTEGRITY = (
    "sha512-s7iGf5GaVMxEG0ENN9x+xTr7GFZCb1ZP/1uATUpCEK2X78nDB3RwbtFCo9pGAf9ru+VwoQ464DkaLEeRM08wJA==")
AXE_TARBALL_SHA256 = (
    "4341a01268b5ecbea826f3c7a7d1d69280a2cab3484c93e1bf4c9554460c6ca0")
AXE_TARBALL_SHA512 = (
    "b3b8867f919a54cc441b410d37dc7ec53afb1856426f564fff5b804d4a4210ad"
    "97efc9c30774706ed142a3da4601ff6bbbe570a10e3ae0391a2c4791334f3024")
AXE_MIN_SHA256 = (
    "66a8aaa95a8b044a7fd74a5435873bf04ff65a1ca75567c921b7509742085a14")
AXE_LICENSE_SHA256 = (
    "af175b9d96ee93c21a036152e1b905b0b95304d4ae8c2c921c7609100ba8df7e")
AXE_THIRD_PARTY_LICENSE_SHA256 = (
    "4f8563870d0fca38bbc3e00b6f670cb7fa9f380ba9f26a7f7d1184a6b18b1653")
AXE_REQUIRED_TAGS = {
    "wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22a", "wcag22aa",
    "best-practice"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha512(path: Path) -> str:
    return hashlib.sha512(path.read_bytes()).hexdigest()


def relative_evidence_path(record_path: Path, value: object) -> Path:
    root = record_path.parent.resolve()
    candidate = (root / str(value)).resolve()
    if not candidate.is_relative_to(root):
        raise ValueError(f"evidence path escapes its record directory: {value}")
    return candidate


def validate_raw_axe_report(raw: dict[str, object], dashboard_url: str) -> dict[str, object]:
    engine = raw.get("testEngine")
    runner = raw.get("testRunner")
    environment = raw.get("testEnvironment")
    options = raw.get("toolOptions")
    run_only = options.get("runOnly") if isinstance(options, dict) else None
    collections = ("passes", "incomplete", "violations", "inapplicable")
    if (not isinstance(engine, dict) or engine.get("name") != "axe-core" or
            engine.get("version") != AXE_CORE_VERSION or
            not isinstance(runner, dict) or runner.get("name") != "axe" or
            not isinstance(environment, dict) or
            not isinstance(environment.get("userAgent"), str) or
            not environment.get("userAgent") or
            not isinstance(raw.get("timestamp"), str) or
            str(raw.get("url", "")).rstrip("/") != dashboard_url.rstrip("/") or
            not isinstance(run_only, dict) or run_only.get("type") != "tag" or
            set(run_only.get("values", [])) != AXE_REQUIRED_TAGS or
            any(not isinstance(raw.get(name), list) for name in collections)):
        raise ValueError("malformed, unpinned, or wrong-target raw axe-core output")
    try:
        timestamp_text = str(raw["timestamp"])
        timestamp = datetime.fromisoformat(timestamp_text.replace("Z", "+00:00"))
        if "T" not in timestamp_text or timestamp.tzinfo is None:
            raise ValueError("timezone-qualified timestamp required")
    except ValueError as error:
        raise ValueError("raw axe-core timestamp is invalid") from error
    def derive(collection: str) -> dict[str, object]:
        counts = {impact:0 for impact in
                  ("critical", "serious", "moderate", "minor")}
        items: list[dict[str, object]] = []
        for finding in raw[collection]:
            if (not isinstance(finding, dict) or
                    finding.get("impact") not in counts or
                    not isinstance(finding.get("id"), str) or
                    not finding.get("id") or
                    not isinstance(finding.get("tags"), list) or
                    not isinstance(finding.get("description"), str) or
                    not isinstance(finding.get("help"), str) or
                    not isinstance(finding.get("helpUrl"), str) or
                    not isinstance(finding.get("nodes"), list) or
                    not finding.get("nodes")):
                raise ValueError(f"raw axe-core {collection} finding is malformed")
            targets: list[list[str]] = []
            for node in finding["nodes"]:
                if (not isinstance(node, dict) or
                        not isinstance(node.get("target"), list) or
                        not node.get("target") or
                        any(not isinstance(target, str) or not target
                            for target in node["target"]) or
                        not isinstance(node.get("failureSummary"), str)):
                    raise ValueError(
                        f"raw axe-core {collection} node is malformed")
                targets.append(list(node["target"]))
            impact = str(finding["impact"])
            counts[impact] += 1
            items.append({"rule_id":finding["id"], "impact":impact,
                          "node_count":len(targets), "targets":targets})
        return {"counts":counts, "items":items}

    return {"violations":derive("violations"),
            "incomplete":derive("incomplete")}


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"evidence is not an object: {path}")
    return value


def validate_schema(document: dict[str, object], name: str) -> None:
    schema = load_json(SCHEMAS / name)
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)


def junit_counts(path: Path) -> dict[str, int]:
    root = ElementTree.parse(path).getroot()
    cases = root.findall(".//testcase")
    if root.tag == "testcase":
        cases = [root]
    def not_executed(case: ElementTree.Element) -> bool:
        for attribute in ("status", "result"):
            value = case.attrib.get(attribute, "").strip().lower()
            normalized = value.replace("_", "-").replace(" ", "-")
            if normalized in {"disabled", "notrun", "not-run"}:
                return True
        return case.find("skipped") is not None

    failed = 0
    skipped = 0
    for case in cases:
        if case.find("failure") is not None or case.find("error") is not None:
            failed += 1
        elif not_executed(case):
            skipped += 1
    total = len(cases)
    return {"passed": total - failed - skipped, "total": total,
            "failed": failed, "skipped": skipped}


def validate_lane(manifest_path: Path, lane: str,
                  evidence_hashes: dict[str, str]) -> dict[str, int]:
    manifest = load_json(manifest_path)
    validate_schema(manifest, "phase8-lane-evidence.schema.json")
    if manifest.get("lane") != lane or manifest.get("profile") != LANES[lane]:
        raise ValueError(f"{lane} lane/profile mismatch")
    junit = (manifest_path.parent / str(manifest["junit_path"])).resolve()
    if not junit.is_file() or sha256(junit) != manifest.get("junit_sha256"):
        raise ValueError(f"{lane} JUnit is missing or hash-mismatched")
    counts = junit_counts(junit)
    if manifest.get("status") != "PASS" or manifest.get("counts") != counts:
        raise ValueError(f"{lane} manifest disagrees with JUnit")
    if counts["total"] < 1 or counts["failed"] or counts["skipped"] or \
            counts["passed"] != counts["total"]:
        raise ValueError(f"{lane} is not a nonempty all-pass lane")
    command = " ".join(str(item).lower() for item in manifest["command"])
    required_tokens = {
        "focused": ("ctest",), "full": ("ctest",),
        "sanitizer": ("asan", "ubsan"), "concurrency": ("tsan",),
    }[lane]
    if not all(token in command for token in required_tokens):
        raise ValueError(f"{lane} command does not identify its required profile")
    evidence_hashes[f"lane_manifest:{lane}"] = sha256(manifest_path)
    evidence_hashes[f"lane_junit:{lane}"] = sha256(junit)
    return counts


def validate_operator(path: Path, tree: str,
                      evidence_hashes: dict[str, str]) -> dict[str, object]:
    report = load_json(path)
    validate_schema(report, "operator-report.schema.json")
    expected = {
        "dashboard_scope":"FHSS-specific", "input_evidence":"synthetic IQ only",
        "hwil_conducted_ota_evidence":"unavailable and deferred",
        "receiver_truth_isolation":"PASS", "browser_automation":"PASS",
        "security_local_profile":"PASS", "operator_workflow":"PASS",
        "production_rf_qualification":"NOT QUALIFIED"}
    if (report.get("phase") != 8 or report.get("result") != "PASS" or
            report.get("evidence_status") != "final_verified" or
            report.get("qualification") != expected or
            not report.get("checks") or
            not all(item.get("pass") is True for item in report["checks"])):
        raise ValueError(f"{tree} operator report is not final_verified all-pass")
    evidence_hashes[f"operator:{tree}"] = sha256(path)
    return report


def require_bound_artifact(report: dict[str, object], key: str,
                           path: Path, evidence_hashes: dict[str, str]) -> None:
    digest = sha256(path)
    if report.get("artifact_hashes", {}).get(key) != digest:
        raise ValueError(f"operator report does not bind {key}")
    evidence_hashes[key] = digest


def generate(args: argparse.Namespace) -> dict[str, object]:
    paths = {name: getattr(args, name).resolve() for name in (
        "source_operator_report", "installed_operator_report",
        "browser_evidence", "security_fuzz_evidence", "soak_evidence",
        "manual_wcag_evidence", "source_schema_inventory",
        "installed_schema_inventory", "source_package_manifest",
        "installed_package_manifest", "installed_browser_evidence",
        "installed_security_fuzz_evidence", "installed_soak_evidence",
        "source_accessibility_engine_evidence",
        "installed_accessibility_engine_evidence")}
    if not all(path.is_file() for path in paths.values()):
        raise ValueError("required Phase 8 evidence file is missing")
    hashes: dict[str, str] = {}
    source = validate_operator(paths["source_operator_report"], "source", hashes)
    installed = validate_operator(paths["installed_operator_report"], "installed", hashes)
    require_bound_artifact(source, "phase8_browser_accessibility",
                           paths["browser_evidence"], hashes)
    require_bound_artifact(source, "phase8_fuzz_security",
                           paths["security_fuzz_evidence"], hashes)
    require_bound_artifact(source, "phase8_soak", paths["soak_evidence"], hashes)
    require_bound_artifact(installed, "phase8_browser_accessibility",
                           paths["installed_browser_evidence"], hashes)
    require_bound_artifact(installed, "phase8_fuzz_security",
                           paths["installed_security_fuzz_evidence"], hashes)
    require_bound_artifact(installed, "phase8_soak",
                           paths["installed_soak_evidence"], hashes)
    require_bound_artifact(source, "phase8_schema_inventory",
                           paths["source_schema_inventory"], hashes)
    require_bound_artifact(installed, "phase8_schema_inventory",
                           paths["installed_schema_inventory"], hashes)
    require_bound_artifact(source, "phase8_package_manifest",
                           paths["source_package_manifest"], hashes)
    require_bound_artifact(installed, "phase8_package_manifest",
                           paths["installed_package_manifest"], hashes)

    def inventory_map(path: Path) -> dict[str, str]:
        inventory = load_json(path)
        entries = inventory.get("entries", [])
        if (inventory.get("schema") !=
                "graphx.fhss.dashboard.phase8_schema_inventory.v1" or
                inventory.get("metaschema_validation") != "PASS" or
                inventory.get("count") != len(entries)):
            raise ValueError("schema inventory contract failed")
        return {str(item["path"]):str(item["sha256"]) for item in entries}

    source_inventory = inventory_map(paths["source_schema_inventory"])
    installed_inventory = inventory_map(paths["installed_schema_inventory"])
    dashboard_root = ROOT.parent
    local_paths = [
        *sorted((dashboard_root / "api" / "schemas").glob("*.schema.json")),
        *sorted((dashboard_root / "operator" / "schemas").glob("*.schema.json"))]
    local_inventory: dict[str, str] = {}
    for schema_path in local_paths:
        schema_document = load_json(schema_path)
        Draft202012Validator.check_schema(schema_document)
        local_inventory[str(schema_path.relative_to(dashboard_root))] = sha256(schema_path)
    if source_inventory != installed_inventory or source_inventory != local_inventory:
        raise ValueError("source/installed/local schema inventories are incomplete or differ")
    source_package = load_json(paths["source_package_manifest"])
    installed_package = load_json(paths["installed_package_manifest"])
    executable_names = {"graphx-dsp-fhss-demo",
                        "graphx-dsp-fhss-iq-generator"}
    required_document_paths = {
        "docs/fhss_dashboard_phase8_manual_operator_test.md",
        "docs/fhss_dashboard_phase8_security_support.md",
        "docs/fhss_dashboard_phase8_architecture_recommendation.md"}
    source_entries = source_package.get("entries", {})
    installed_entries = installed_package.get("entries", {})
    source_executables = source_package.get("executables", {})
    installed_executables = installed_package.get("executables", {})
    if (source_package.get("schema") !=
            "graphx.fhss.dashboard.phase8_package_manifest.v1" or
            source_package.get("entry_count") != len(source_entries) or
            len(source_entries) < 63 or
            not required_document_paths.issubset(source_entries) or
            source_entries != installed_entries or
            installed_package.get("entry_count") != len(installed_entries) or
            any(not isinstance(value, str) or
                re.fullmatch(r"[0-9a-f]{64}", value) is None
                for value in source_entries.values()) or
            set(source_executables) != executable_names or
            set(installed_executables) != executable_names or
            any(not isinstance(value, str) or
                re.fullmatch(r"[0-9a-f]{64}", value) is None
                for value in [*source_executables.values(),
                              *installed_executables.values()])):
        raise ValueError("source/installed packaged assets differ or are incomplete")

    browser = load_json(paths["browser_evidence"])
    installed_browser = load_json(paths["installed_browser_evidence"])
    def validate_browser(document: dict[str, object],
                         report: dict[str, object], tree: str) -> None:
        ratios = document.get("contrast_ratios", {})
        reflow = document.get("reflow_320_css_px", {})
        if (document.get("schema") !=
                "graphx.fhss.dashboard.phase8_browser_accessibility.v1" or
                str(document.get("browser_name", "")).lower() != "firefox" or
                not document.get("browser_version") or
                document.get("dashboard_url") != report.get("dashboard_url") or
                document.get("unnamed_controls") != [] or
                document.get("unsafe_inline_handlers") != 0 or
                not ratios or min(float(value) for value in ratios.values()) < 4.5 or
                reflow.get("document_width") != 320 or
                reflow.get("body_width") != 320 or
                any(str(item.get("level", "")).lower() in
                    {"warn", "warning", "error"}
                    for item in document.get("console", []))):
            raise ValueError(f"{tree} browser automation evidence failed")
    validate_browser(browser, source, "source")
    validate_browser(installed_browser, installed, "installed")
    for key in ("browser_name", "browser_version", "contrast_ratios",
                "reflow_320_css_px", "required_live_regions", "real_keyboard_input"):
        if installed_browser.get(key) != browser.get(key):
            raise ValueError(f"installed browser evidence diverges: {key}")

    manual = load_json(paths["manual_wcag_evidence"])
    validate_schema(manual, "phase8-manual-wcag.schema.json")
    if ({item.get("id") for item in manual["items"]} != MANUAL_IDS or
            not all(item.get("result") == "PASS" for item in manual["items"])):
        raise ValueError("human WCAG record is incomplete or failed")
    hashes["manual_wcag"] = sha256(paths["manual_wcag_evidence"])
    verified_manual_items: set[str] = set()
    manual_root = paths["manual_wcag_evidence"].parent.resolve()
    for index, item in enumerate(manual["items"]):
        declared_hashes = item["evidence_sha256"]
        if set(declared_hashes) != set(item["evidence_paths"]):
            raise ValueError("manual WCAG evidence hash inventory is incomplete")
        for evidence_index, relative in enumerate(item["evidence_paths"]):
            relative_path = Path(str(relative))
            unresolved = manual_root / relative_path
            evidence = relative_evidence_path(
                paths["manual_wcag_evidence"], relative)
            try:
                relative_parts = unresolved.relative_to(manual_root).parts
            except ValueError as error:
                raise ValueError("manual WCAG evidence path escapes record") from error
            cursor = manual_root
            if (relative_path.is_absolute() or
                    any(part in ("", ".", "..") for part in relative_path.parts)):
                raise ValueError("manual WCAG evidence path is not confined")
            for part in relative_parts:
                cursor = cursor / part
                if cursor.is_symlink():
                    raise ValueError("manual WCAG evidence path uses a symlink")
            actual_hash = sha256(evidence) if evidence.is_file() else ""
            if (not evidence.is_file() or
                    evidence == paths["manual_wcag_evidence"].resolve() or
                    declared_hashes.get(relative) != actual_hash):
                raise ValueError(
                    f"manual WCAG evidence is missing, duplicated, or changed: {relative}")
            hashes[f"manual_wcag:{index}:{evidence_index}"] = actual_hash
        verified_manual_items.add(str(item["id"]))

    axe_execution: dict[str, dict[str, object]] = {}
    axe_summaries: dict[str, dict[str, object]] = {}
    serious_incomplete: list[dict[str, object]] = []
    for tree, evidence_name, operator_report, browser_document in (
            ("source", "source_accessibility_engine_evidence", source, browser),
            ("installed", "installed_accessibility_engine_evidence",
             installed, installed_browser)):
        engine_path = paths[evidence_name]
        engine = load_json(engine_path)
        validate_schema(engine, "phase8-accessibility-engine.schema.json")
        report_path = relative_evidence_path(engine_path, engine["report_path"])
        provenance = engine["provenance"]
        package_path = relative_evidence_path(
            engine_path, provenance["package_path"])
        axe_min_path = relative_evidence_path(
            engine_path, provenance["axe_min_path"])
        license_path = relative_evidence_path(
            engine_path, provenance["license_path"])
        third_party_license_path = relative_evidence_path(
            engine_path, provenance["third_party_license_path"])
        provenance_ok = (
            engine.get("engine_version") == AXE_CORE_VERSION and
            provenance.get("package_name") == "axe-core" and
            provenance.get("package_version") == AXE_CORE_VERSION and
            provenance.get("tarball_url") == AXE_TARBALL_URL and
            provenance.get("registry_integrity") == AXE_REGISTRY_INTEGRITY and
            provenance.get("package_sha256") == AXE_TARBALL_SHA256 and
            provenance.get("package_sha512") == AXE_TARBALL_SHA512 and
            provenance.get("axe_min_sha256") == AXE_MIN_SHA256 and
            provenance.get("license_sha256") == AXE_LICENSE_SHA256 and
            provenance.get("third_party_license_sha256") ==
                AXE_THIRD_PARTY_LICENSE_SHA256 and
            package_path.is_file() and sha256(package_path) == AXE_TARBALL_SHA256 and
            sha512(package_path) == AXE_TARBALL_SHA512 and
            axe_min_path.is_file() and sha256(axe_min_path) == AXE_MIN_SHA256 and
            license_path.is_file() and sha256(license_path) == AXE_LICENSE_SHA256 and
            third_party_license_path.is_file() and
            sha256(third_party_license_path) == AXE_THIRD_PARTY_LICENSE_SHA256)
        raw_report = load_json(report_path) if report_path.is_file() else {}
        derived_counts = validate_raw_axe_report(
            raw_report, str(engine.get("dashboard_url", "")))
        user_agent = str(raw_report.get("testEnvironment", {}).get(
            "userAgent", ""))
        user_agent_match = re.search(
            r"(?:^|[ /])Firefox/([0-9]+(?:\.[0-9]+)*)", user_agent)
        browser_version = str(browser_document.get("browser_version", ""))
        firefox_identity_ok = (
            engine.get("browser") == f"Firefox/{browser_version}" and
            user_agent_match is not None and
            user_agent_match.group(1).split(".")[0] ==
                browser_version.split(".")[0])
        derived_violations = derived_counts["violations"]
        derived_incomplete = derived_counts["incomplete"]
        has_automated_blocker = any(
            derived_violations["counts"][impact] != 0 or
            derived_incomplete["counts"][impact] != 0
            for impact in ("critical", "serious"))
        expected_engine_result = "FAIL" if has_automated_blocker else "PASS"
        if (engine.get("result") != expected_engine_result or
                engine.get("tree") != tree or
                not str(engine.get("execution_id", "")).startswith(
                    f"axe-{tree}-") or
                engine.get("executed_at") != raw_report.get("timestamp") or
                engine.get("dashboard_url") != operator_report.get("dashboard_url") or
                engine.get("dashboard_url") != browser_document.get("dashboard_url") or
                not firefox_identity_ok or
                engine.get("violations") != derived_violations or
                engine.get("incomplete") != derived_incomplete or
                derived_violations["counts"]["critical"] != 0 or
                derived_violations["counts"]["serious"] != 0 or
                not provenance_ok or not report_path.is_file() or
                sha256(report_path) != engine["report_sha256"]):
            raise ValueError(f"{tree} recognized accessibility engine failed")
        for item in derived_incomplete["items"]:
            if item["impact"] in ("critical", "serious"):
                serious_incomplete.append({"tree":tree, **item})
        axe_summaries[tree] = {
            "violations":derived_violations,
            "incomplete":derived_incomplete}
        hashes[f"accessibility_engine:{tree}"] = sha256(engine_path)
        hashes[f"accessibility_engine_report:{tree}"] = sha256(report_path)
        hashes[f"accessibility_engine_package:{tree}"] = sha256(package_path)
        hashes[f"accessibility_engine_axe_min:{tree}"] = sha256(axe_min_path)
        hashes[f"accessibility_engine_license:{tree}"] = sha256(license_path)
        hashes[f"accessibility_engine_third_party_license:{tree}"] = sha256(
            third_party_license_path)
        axe_execution[tree] = {
            "wrapper_path":engine_path, "raw_path":report_path,
            "wrapper_sha256":sha256(engine_path),
            "raw_sha256":sha256(report_path),
            "execution_id":engine["execution_id"],
            "executed_at":engine["executed_at"]}
    if (axe_execution["source"]["wrapper_path"] ==
            axe_execution["installed"]["wrapper_path"] or
            axe_execution["source"]["raw_path"] ==
            axe_execution["installed"]["raw_path"] or
            axe_execution["source"]["wrapper_sha256"] ==
            axe_execution["installed"]["wrapper_sha256"] or
            axe_execution["source"]["raw_sha256"] ==
            axe_execution["installed"]["raw_sha256"] or
            axe_execution["source"]["execution_id"] ==
            axe_execution["installed"]["execution_id"] or
            axe_execution["source"]["executed_at"] ==
            axe_execution["installed"]["executed_at"]):
        raise ValueError("source/installed axe executions are not distinct")

    manual_items = {item["id"]:item for item in manual["items"]}
    expected_resolutions = {
        json.dumps(item, sort_keys=True, separators=(",", ":"))
        for item in serious_incomplete}
    actual_resolutions: set[str] = set()
    for resolution in manual["axe_incomplete_resolutions"]:
        finding = {key:resolution[key] for key in
                   ("tree", "rule_id", "impact", "node_count", "targets")}
        manual_item = manual_items.get(resolution["manual_item_id"])
        if (manual_item is None or manual_item.get("result") != "PASS" or
                resolution["manual_item_id"] not in verified_manual_items):
            raise ValueError("axe incomplete resolution is not bound to a passing manual item")
        actual_resolutions.add(
            json.dumps(finding, sort_keys=True, separators=(",", ":")))
    if (len(actual_resolutions) != len(manual["axe_incomplete_resolutions"]) or
            actual_resolutions != expected_resolutions):
        raise ValueError(
            "serious/critical axe incomplete findings lack exact human resolution bindings")

    security_fuzz = load_json(paths["security_fuzz_evidence"])
    installed_security_fuzz = load_json(paths["installed_security_fuzz_evidence"])
    fuzz = security_fuzz.get("fuzz", {})
    security = security_fuzz.get("security", {})
    if (fuzz.get("result") != "PASS" or
            fuzz.get("production_endpoints_exercised") is not True or
            set(fuzz.get("targets", [])) != {
                "http_json", "json_patch", "websocket", "sigmf_import"} or
            security.get("result") != "PASS" or
            security.get("unsafe_dynamic_html_sinks") != 0):
        raise ValueError("security/fuzz evidence failed")
    if (installed_security_fuzz.get("fuzz", {}).get("result") != "PASS" or
            installed_security_fuzz.get("fuzz", {}).get(
                "production_endpoints_exercised") is not True or
            installed_security_fuzz.get("security") != security or
            set(installed_security_fuzz.get("fuzz", {}).get("targets", [])) !=
                set(fuzz.get("targets", []))):
        raise ValueError("installed security/fuzz evidence failed or diverged")
    retained = fuzz.get("retained_regression_seeds", [])
    if not retained:
        raise ValueError("fuzz retained corpus is empty")
    for index, record in enumerate(retained):
        corpus = (paths["security_fuzz_evidence"].parent /
                  str(record.get("path", ""))).resolve()
        digest = sha256(corpus) if corpus.is_file() else ""
        key = "phase8_fuzz_seed:" + str(record.get("target")) + ":" + digest
        if (digest != record.get("sha256") or
                source.get("artifact_hashes", {}).get(key) != digest):
            raise ValueError("fuzz retained corpus is missing, changed, or unbound")
        hashes[f"fuzz_corpus:{index}"] = digest
    for index, record in enumerate(
            installed_security_fuzz.get("fuzz", {}).get(
                "retained_regression_seeds", [])):
        corpus = (paths["installed_security_fuzz_evidence"].parent /
                  str(record.get("path", ""))).resolve()
        digest = sha256(corpus) if corpus.is_file() else ""
        key = "phase8_fuzz_seed:" + str(record.get("target")) + ":" + digest
        if (digest != record.get("sha256") or
                installed.get("artifact_hashes", {}).get(key) != digest):
            raise ValueError("installed fuzz corpus is missing, changed, or unbound")
        hashes[f"installed_fuzz_corpus:{index}"] = digest

    soak = load_json(paths["soak_evidence"])
    installed_soak = load_json(paths["installed_soak_evidence"])
    if (soak.get("result") != "PASS" or soak.get("supported_profile") != "macOS" or
            soak.get("probe_support") != {"rss":True,"threads":True,"handles":True} or
            not all(soak.get("dimensions", {}).get(name) == "PASS" for name in (
                "load", "reconnect", "lifecycle", "shutdown", "export_replay"))):
        raise ValueError("complete supported-profile soak evidence failed")
    for document in (installed_soak,):
        if (document.get("result") != "PASS" or
                document.get("supported_profile") != "macOS" or
                document.get("probe_support") !=
                    {"rss":True,"threads":True,"handles":True} or
                document.get("dimensions") != soak.get("dimensions")):
            raise ValueError("installed complete soak evidence failed or diverged")

    lane_counts = {lane: validate_lane(getattr(args, f"{lane}_evidence").resolve(),
                                       lane, hashes) for lane in LANES}
    report = {
        "schema":"graphx.fhss.dashboard.phase8_completion_report.v1",
        "dashboard_scope":"FHSS-specific", "input_evidence":"synthetic IQ only",
        "hwil_conducted_ota_evidence":"unavailable and deferred",
        "receiver_truth_isolation":"PASS",
        "focused_tests":lane_counts["focused"],
        "full_regressions":lane_counts["full"],
        "browser_accessibility":"PASS",
        "accessibility_engine":axe_summaries,
        "security_local_profile":"PASS",
        "operator_workflow":"PASS", "sanitizer":"PASS", "concurrency":"PASS",
        "soak":"PASS", "fuzz_smoke":"PASS",
        "production_rf_qualification":"NOT QUALIFIED",
        "evidence_hashes":dict(sorted(hashes.items())),
        "manual_wcag_tester":manual["tester"],
        "manual_wcag_executed_at":manual["executed_at"],
        "signed_off_by":args.signed_off_by,
        "signed_off_at":datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    }
    validate_schema(report, "phase8-completion-report.schema.json")
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    for name in ("source-operator-report", "installed-operator-report",
                 "browser-evidence", "security-fuzz-evidence", "soak-evidence",
                 "manual-wcag-evidence", "source-schema-inventory",
                 "installed-schema-inventory", "source-package-manifest",
                 "installed-package-manifest", "focused-evidence", "full-evidence",
                 "sanitizer-evidence", "concurrency-evidence",
                 "installed-browser-evidence", "installed-security-fuzz-evidence",
                 "installed-soak-evidence", "source-accessibility-engine-evidence",
                 "installed-accessibility-engine-evidence"):
        parser.add_argument("--" + name, dest=name.replace("-", "_"),
                            type=Path, required=True)
    parser.add_argument("--signed-off-by", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = generate(args)
    except Exception as error:
        raise SystemExit(f"Phase 8 sign-off rejected: {error}") from error
    encoded = (json.dumps(report, indent=2) + "\n").encode()
    args.output.write_bytes(encoded)
    args.output.with_suffix(args.output.suffix + ".sha256").write_text(
        hashlib.sha256(encoded).hexdigest() + "  " + args.output.name + "\n")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
