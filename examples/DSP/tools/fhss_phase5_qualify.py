#!/usr/bin/env python3
"""Deterministic FHSS Phase 5 software qualification governance.

This tool validates immutable evidence and traceability.  It intentionally has
no RF, network, release-publishing, or synthetic-waveform generation feature.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any

VERSION = "graphx.fhss.phase5-qualification.v1"
PREFIX = "fhss_phase5_"
SCHEMAS = {
    "qualification_profile": "graphx.fhss.phase5-qualification-profile.v1",
    "requirement_registry": "graphx.fhss.phase5-requirement-registry.v1",
    "claim_registry": "graphx.fhss.phase5-claim-registry.v1",
    "evidence_registry": "graphx.fhss.phase5-evidence-registry.v1",
    "traceability": "graphx.fhss.phase5-traceability.v1",
    "limitation_waiver_registry": "graphx.fhss.phase5-limitation-waiver-registry.v1",
    "regression_corpus_manifest": "graphx.fhss.phase5-regression-corpus.v1",
    "release_qualification_manifest": "graphx.fhss.phase5-release-qualification-manifest.v1",
    "failure_registry": "graphx.fhss.phase5-failure-registry.v1",
    "minimized_failure_registry": "graphx.fhss.phase5-minimized-failure-registry.v1",
}

GOVERNED_GATES = {
    "fhss_phase5_qualification": (["python3", "examples/DSP/test/test_fhss_phase5_qualification.py"], ["P5.HashMismatchFails", "P5.PathEscapeFails", "P5.DanglingReferenceFails", "P5.DuplicateIdFails", "P5.SyntheticPromotionFails", "P5.PhysicalCountsMustBeZero", "P5.ProductionPromotionFails", "P5.AmbiguousPassAbsent", "P5.HistoricalBindings", "phase5_governance"]),
    "fhss_phase3_report_verify": (["python3", "examples/DSP/tools/fhss_phase3_independent_v7.py", "verify", "--profile", "libdsp/config/fhss_phase3_validation_profile_v7.json", "--raw", "libdsp/config/fhss_phase3_evaluation_raw_v7.json", "--report", "libdsp/config/fhss_phase3_characterization_report_v7.json"], ["Phase 3 v7 collision matrix", "channelizer circular-history unit regression", "FHSS CPSM state/discontinuity isolated tests", "qualification limitation disclosure"]),
    "fhss_phase4_regressions": (["python3", "examples/DSP/test/test_fhss_phase4_recorded.py"], ["test_fhss_phase4_recorded.py"]),
    "fhss_cpp_regressions": (["./build-ninja/ninja-debug/libdsp/test/test_libdsp_unit", "--gtest_filter=FHSSGraphXConfigTest.*:FHSSPhase2ChannelizerTest.*:FHSSPhase2DetectorTest.*:FHSSGraphXNodeTest.EveryEmptyTerminalShapeReachesSinkWithoutInventingPulseEvidence:FHSSCpsmDecoderTest.*"], ["FHSS minimal receiver configuration tests", "fhss_phase2_candidate_characterization_smoke", "Phase 3 terminal isolated tests", "EmptyTerminalCandidateBatchReachesSinkWithoutInventingPulseEvidence"]),
    "fhss_truth_isolation": (["python3", "examples/DSP/tools/fhss_phase4_recorded.py", "scan-graph", "--graph", "libdsp/config/fhss_phase2_binary_iq_receiver.json"], []),
    "libdsp_unit_full": (["./build-ninja/ninja-debug/libdsp/test/test_libdsp_unit"], ["libdsp full regression suite"]),
    "dsp_example_unit_full": (["./build-ninja/ninja-debug/examples/DSP/test/test_dsp_example_unit"], ["DSP example full regression suite"]),
}

ALLOWED_METAL_SKIPS = [
    "DspGpuSpectrumGraphRuntimeTest.JsonTopologyRunsThroughExecutorAndSinkReceivesSpectrum",
    "DspGpuSpectrumParityTest.PeakFrequencyMatchesCpuReference",
    "DspGpuSpectrumParityTest.PeakMagnitudeMatchesCpuWithinTolerance",
    "DspGpuSpectrumParityTest.SelectedMagnitudeBinsMatchCpuWithinTolerance",
    "DspGpuSpectrumParityTest.SkipsClearlyWhenMetalUnavailable",
]
GATE_POLICIES = {
    suite_id: {"minimum_passed": 1, "expected_skipped": 0, "allowed_skip_ids": [], "timeout_seconds": 180}
    for suite_id in GOVERNED_GATES
}
GATE_POLICIES["libdsp_unit_full"] = {
    "accepted_outcomes": [
        {"passed": 174, "skipped": 0, "skip_ids": []},
        {"passed": 169, "skipped": 5, "skip_ids": ALLOWED_METAL_SKIPS},
    ],
    "timeout_seconds": 180,
}
GATE_POLICIES["dsp_example_unit_full"] = {"exact_passed": 56, "expected_skipped": 0, "allowed_skip_ids": [], "timeout_seconds": 180}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class ContractError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_json(path: Path) -> Any:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), parse_constant=lambda x: (_ for _ in ()).throw(ContractError(f"non-finite JSON: {x}")))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"cannot load {path}: {exc}") from exc
    finite(value)
    return value


def finite(value: Any) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        raise ContractError("non-finite number")
    if isinstance(value, dict):
        for child in value.values(): finite(child)
    elif isinstance(value, list):
        for child in value: finite(child)


def exact(obj: dict[str, Any], required: set[str], where: str) -> None:
    missing, extra = required - obj.keys(), obj.keys() - required
    if missing or extra:
        raise ContractError(f"{where}: missing={sorted(missing)} extra={sorted(extra)}")


def unique(items: list[dict[str, Any]], key: str, where: str) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for item in items:
        ident = item.get(key)
        if not isinstance(ident, str) or not ident or ident in out:
            raise ContractError(f"{where}: invalid or duplicate {key}: {ident!r}")
        out[ident] = item
    return out


def safe_artifact(root: Path, relative: str) -> Path:
    pure = PurePosixPath(relative)
    if pure.is_absolute() or not pure.parts or ".." in pure.parts:
        raise ContractError(f"unsafe artifact path: {relative}")
    candidate = root.joinpath(*pure.parts)
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise ContractError(f"missing artifact: {relative}") from exc
    if not resolved.is_relative_to(root.resolve()):
        raise ContractError(f"artifact escapes repository: {relative}")
    if not resolved.is_file():
        raise ContractError(f"artifact is not a file: {relative}")
    return resolved


def config(root: Path, name: str) -> Any:
    return load_json(root / "libdsp" / "config" / f"{PREFIX}{name}_v1.json")


def validate(root: Path) -> dict[str, Any]:
    profile = config(root, "qualification_profile")
    requirements = config(root, "requirement_registry")
    claims = config(root, "claim_registry")
    evidence = config(root, "evidence_registry")
    trace = config(root, "traceability")
    limitations = config(root, "limitation_waiver_registry")
    corpus = config(root, "regression_corpus_manifest")
    release = config(root, "release_qualification_manifest")
    failures = config(root, "failure_registry")
    minimized = config(root, "minimized_failure_registry")

    for name, value in (("qualification_profile", profile), ("requirement_registry", requirements), ("claim_registry", claims), ("evidence_registry", evidence), ("traceability", trace), ("limitation_waiver_registry", limitations), ("regression_corpus_manifest", corpus), ("release_qualification_manifest", release), ("failure_registry", failures), ("minimized_failure_registry", minimized)):
        if value.get("schema") != SCHEMAS[name]:
            raise ContractError(f"{name}: wrong schema discriminator/version")

    exact(profile, {"schema", "version", "scope", "physical_evidence_counts", "allowed_statuses", "verdict_classes", "required_registries", "freshness_policy", "ci_tiers", "trend_policy"}, "profile")
    if profile["scope"] != "synthetic_and_software_only":
        raise ContractError("profile must remain synthetic_and_software_only")
    counts = profile["physical_evidence_counts"]
    if set(counts) != {"conducted", "channel_emulator", "ota", "hwil", "recorded_field_failures", "paired_points", "searched_seconds"} or any(v != 0 for v in counts.values()):
        raise ContractError("all physical evidence counts must be exactly zero")
    trend = profile["trend_policy"]
    exact(trend, {"schema", "rule", "policies"}, "trend policy")
    if trend["schema"] != "graphx.fhss.phase5-trend-policy.v1": raise ContractError("wrong trend-policy schema/version")
    for policy in trend["policies"]:
        exact(policy, {"trend_id", "metric", "units", "baseline_artifact", "baseline_sha256", "sample_count", "noise_variance_treatment", "confidence", "tolerance", "alert_threshold", "failure_threshold", "rebaseline_approval", "decision_status"}, "trend policy item")
        if digest(safe_artifact(root, policy["baseline_artifact"]).read_bytes()) != policy["baseline_sha256"]: raise ContractError("stale trend baseline")
        if policy["decision_status"] != "UNVERIFIED" or any(policy[x] is not None for x in ("tolerance", "alert_threshold", "failure_threshold")):
            raise ContractError("unfrozen trend cannot claim a decision")

    exact(requirements, {"schema", "requirements"}, "requirement registry")
    exact(claims, {"schema", "claims"}, "claim registry")
    exact(evidence, {"schema", "evidence"}, "evidence registry")
    exact(trace, {"schema", "links"}, "traceability")
    exact(limitations, {"schema", "limitations"}, "limitation registry")
    exact(corpus, {"schema", "recorded_field_entry_count", "entries"}, "regression corpus")
    if corpus["recorded_field_entry_count"] != 0:
        raise ContractError("recorded field corpus count must be zero")
    for name, registry in (("failure registry", failures), ("minimized failure registry", minimized)):
        exact(registry, {"schema", "field_failure_count", "records"}, name)
    failure_records = unique(failures["records"], "failure_id", "failure registry") if failures["records"] else {}
    minimized_records = unique(minimized["records"], "failure_id", "minimized failure registry") if minimized["records"] else {}
    physical_failure_classes = {"recorded_field", "conducted", "channel_emulator", "ota_hwil", "customer_interoperability"}
    actual_field_count = sum(x.get("classification") in physical_failure_classes for x in failure_records.values())
    if failures["field_failure_count"] != actual_field_count or minimized["field_failure_count"] != sum(x.get("classification") in physical_failure_classes for x in minimized_records.values()): raise ContractError("failure field count mismatch")
    if actual_field_count != 0: raise ContractError("physical/field failure evidence is unavailable")
    for record in failure_records.values():
        exact(record, {"failure_id", "classification", "artifact_hash", "provenance", "license_privacy_review", "environment", "receiver_version", "observed_behavior", "retention_policy"}, record["failure_id"])
        if record["classification"] not in {"synthetic", "software_runtime", "unknown_unverified"}: raise ContractError("synthetic-to-field promotion")
    for record in minimized_records.values():
        exact(record, {"failure_id", "classification", "original_failure_id", "original_hash", "minimized_hash", "development_partition", "expected_result", "regression_test", "requirement_ids", "claim_ids", "fix_reference", "non_regression_evidence"}, record["failure_id"])
        if record["original_failure_id"] not in failure_records or record["classification"] != failure_records[record["original_failure_id"]]["classification"]: raise ContractError("minimized failure provenance/class mismatch")
        for ident in record["requirement_ids"]:
            if ident not in {x["requirement_id"] for x in requirements["requirements"]}: raise ContractError("minimized failure dangling requirement")
        for ident in record["claim_ids"]:
            if ident not in {x["claim_id"] for x in claims["claims"]}: raise ContractError("minimized failure dangling claim")

    reqs = unique(requirements["requirements"], "requirement_id", "requirements")
    cls = unique(claims["claims"], "claim_id", "claims")
    evs = unique(evidence["evidence"], "evidence_id", "evidence")
    lims = unique(limitations["limitations"], "limitation_id", "limitations")
    entries = unique(corpus["entries"], "failure_id", "corpus")

    for limitation in lims.values():
        exact(limitation, {"limitation_id", "description", "status", "waiver", "approval", "expires"}, limitation["limitation_id"])

    for req in reqs.values():
        exact(req, {"requirement_id", "source", "owner", "authority", "text", "subsystem", "phase", "acceptance_rule", "required_evidence_class", "configurations", "introduced", "status", "supersedes", "limitation_ids"}, req["requirement_id"])
        if not req["acceptance_rule"]:
            raise ContractError(f"{req['requirement_id']}: empty acceptance rule")
        for ident in req["limitation_ids"]:
            if ident not in lims: raise ContractError(f"dangling limitation {ident}")

    physical_classes = {"recorded_iq", "conducted", "channel_emulator", "ota_hwil", "production_rf", "regulatory", "interoperability"}
    for ev in evs.values():
        exact(ev, {"evidence_id", "evidence_class", "artifact", "sha256", "creation_command", "scope", "value_kind", "requirement_ids", "claim_ids", "freshness", "environment", "status", "provenance"}, ev["evidence_id"])
        path = safe_artifact(root, ev["artifact"])
        actual = digest(path.read_bytes())
        if actual != ev["sha256"]: raise ContractError(f"{ev['evidence_id']}: hash mismatch")
        if ev["evidence_class"] in physical_classes:
            raise ContractError(f"{ev['evidence_id']}: physical evidence prohibited in synthetic-only baseline")
        for ident in ev["requirement_ids"]:
            if ident not in reqs: raise ContractError(f"dangling requirement {ident}")
        for ident in ev["claim_ids"]:
            if ident not in cls: raise ContractError(f"dangling claim {ident}")

    allowed = set(profile["allowed_statuses"])
    for claim in cls.values():
        exact(claim, {"claim_id", "claim_class", "statement", "scope", "exclusions", "required_evidence_classes", "required_evidence_ids", "required_test_suites", "acceptance", "limitations", "revalidation", "status", "rationale"}, claim["claim_id"])
        if claim["status"] not in allowed: raise ContractError(f"{claim['claim_id']}: bad status")
        for ident in claim["required_evidence_ids"]:
            if ident not in evs: raise ContractError(f"{claim['claim_id']}: dangling evidence {ident}")
            if evs[ident]["evidence_class"] not in claim["required_evidence_classes"]:
                raise ContractError(f"{claim['claim_id']}: evidence class mismatch for {ident}")
        if claim["status"] == "PASS":
            if claim["claim_class"] in physical_classes: raise ContractError("physical claim cannot PASS")
            if not claim["required_evidence_ids"]: raise ContractError(f"{claim['claim_id']}: evidence-free PASS")
            for ident in claim["required_evidence_ids"]:
                if evs[ident]["status"] != "PASS": raise ContractError(f"{claim['claim_id']}: nonpassing evidence")
        if claim["claim_class"] in {"recorded_iq", "ota_hwil"} and claim["status"] != "UNAVAILABLE_DEFERRED":
            raise ContractError(f"{claim['claim_id']}: recorded/HIL must be unavailable")
        if claim["claim_class"] == "production_rf" and claim["status"] != "NOT_QUALIFIED":
            raise ContractError("production RF must be NOT_QUALIFIED")

    edges = trace["links"]
    seen_pairs: set[tuple[str, str]] = set()
    for edge in edges:
        exact(edge, {"requirement_id", "claim_id", "evidence_ids", "test_ids", "limitation_ids"}, "trace link")
        pair = edge["requirement_id"], edge["claim_id"]
        if pair in seen_pairs: raise ContractError(f"duplicate trace link {pair}")
        seen_pairs.add(pair)
        if pair[0] not in reqs or pair[1] not in cls: raise ContractError(f"dangling trace link {pair}")
        for ident in edge["evidence_ids"]:
            if ident not in evs: raise ContractError(f"dangling trace evidence {ident}")
        for ident in edge["limitation_ids"]:
            if ident not in lims: raise ContractError(f"dangling trace limitation {ident}")
        if not edge["test_ids"]: raise ContractError(f"trace link {pair} has no test")
    linked_reqs = {x["requirement_id"] for x in edges}
    linked_claims = {x["claim_id"] for x in edges}
    if linked_reqs != set(reqs) or linked_claims != set(cls): raise ContractError("traceability is not bidirectionally complete")

    for req_id in reqs:
        declared = {ev_id for ev_id, item in evs.items() if req_id in item["requirement_ids"]}
        traced = {ev_id for edge in edges if edge["requirement_id"] == req_id for ev_id in edge["evidence_ids"]}
        if declared != traced: raise ContractError(f"{req_id}: exact evidence trace mismatch")
    for claim_id in cls:
        declared = {ev_id for ev_id, item in evs.items() if claim_id in item["claim_ids"]}
        traced = {ev_id for edge in edges if edge["claim_id"] == claim_id for ev_id in edge["evidence_ids"]}
        if declared != traced: raise ContractError(f"{claim_id}: exact evidence trace mismatch")
    for edge in edges:
        for ev_id in edge["evidence_ids"]:
            if edge["requirement_id"] not in evs[ev_id]["requirement_ids"] or edge["claim_id"] not in evs[ev_id]["claim_ids"]:
                raise ContractError(f"trace edge/evidence reciprocal pair mismatch: {ev_id}")

    # Every declared evidence relationship must be reciprocated by trace links.
    for evidence_id, item in evs.items():
        for req_id in item["requirement_ids"]:
            if not any(x["requirement_id"] == req_id and evidence_id in x["evidence_ids"] for x in edges):
                raise ContractError(f"{evidence_id}: requirement trace asymmetry")
        for claim_id in item["claim_ids"]:
            if not any(x["claim_id"] == claim_id and evidence_id in x["evidence_ids"] for x in edges):
                raise ContractError(f"{evidence_id}: claim trace asymmetry")
    for claim in cls.values():
        for limitation_id in claim["limitations"]:
            if limitation_id not in lims: raise ContractError(f"{claim['claim_id']}: dangling limitation")
            if not any(x["claim_id"] == claim["claim_id"] and limitation_id in x["limitation_ids"] for x in edges):
                raise ContractError(f"{claim['claim_id']}: limitation trace asymmetry")
        declared_tests = {test for x in edges if x["claim_id"] == claim["claim_id"] for test in x["test_ids"]}
        if claim["status"] == "PASS" and not declared_tests:
            raise ContractError(f"{claim['claim_id']}: no reciprocal tests")
        if not set(claim["required_test_suites"]).issubset(set(release["required_test_suites"])):
            raise ContractError(f"{claim['claim_id']}: required suite not in release manifest")
    for req in reqs.values():
        if req["status"] == "PASS":
            linked_classes = {evs[evidence_id]["evidence_class"] for edge in edges if edge["requirement_id"] == req["requirement_id"] for evidence_id in edge["evidence_ids"]}
            if req["required_evidence_class"] not in linked_classes:
                raise ContractError(f"{req['requirement_id']}: required evidence class not linked")

    for entry in entries.values():
        exact(entry, {"failure_id", "classification", "original_evidence", "failure_class", "reproduction", "expected_result", "fixed_by", "requirement_ids", "claim_ids", "regression_test", "provenance", "sha256", "support_status"}, entry["failure_id"])
        if entry["classification"] not in {"synthetic", "software_runtime"}: raise ContractError("corpus contains non-synthetic/non-software entry")
        if not entry["expected_result"] or not entry["regression_test"]:
            raise ContractError(f"{entry['failure_id']}: incomplete expected outcome/test")
        if digest(canonical(entry["reproduction"])) != entry["sha256"]: raise ContractError(f"{entry['failure_id']}: reproduction hash mismatch")
        for ident in entry["requirement_ids"]:
            if ident not in reqs: raise ContractError(f"corpus dangling requirement {ident}")
        for ident in entry["claim_ids"]:
            if ident not in cls: raise ContractError(f"corpus dangling claim {ident}")
        if entry["original_evidence"] not in evs: raise ContractError(f"{entry['failure_id']}: unresolved original evidence")
        if entry["fixed_by"] is not None:
            safe_artifact(root, entry["fixed_by"])

    # Supersession must be acyclic.
    for start in reqs:
        cursor, visited = start, set()
        while reqs[cursor]["supersedes"] is not None:
            cursor = reqs[cursor]["supersedes"]
            if cursor not in reqs or cursor in visited: raise ContractError("dangling/circular supersession")
            visited.add(cursor)

    exact(release, {"schema", "release_id", "profile", "registries", "required_test_suites", "skip_policy", "outputs", "prohibited_actions"}, "release manifest")
    if release["skip_policy"] != "skips_are_not_passes": raise ContractError("unsafe skip policy")
    if set(release["registries"]) != set(profile["required_registries"]): raise ContractError("registry set mismatch")

    if "EV-P5-TESTS" not in evs:
        raise ContractError("missing Phase 5 test attestation evidence")
    attestation = load_json(safe_artifact(root, evs["EV-P5-TESTS"]["artifact"]))
    exact(attestation, {"schema", "compiler_mode", "environment", "source_revision", "source_hashes", "build_configuration", "build_configuration_sha256", "suites"}, "test attestation")
    if attestation["schema"] != "graphx.fhss.phase5-test-attestation.v2": raise ContractError("wrong test-attestation schema/version")
    environment_hash = digest((attestation["compiler_mode"] + "\n" + attestation["environment"] + "\n").encode())
    if digest(safe_artifact(root, attestation["build_configuration"]).read_bytes()) != attestation["build_configuration_sha256"]: raise ContractError("stale build configuration")
    for path_text, expected_hash in attestation["source_hashes"].items():
        if digest(safe_artifact(root, path_text).read_bytes()) != expected_hash: raise ContractError(f"stale source binding: {path_text}")
    suites = unique(attestation["suites"], "suite_id", "test attestation")
    for suite in suites.values():
        exact(suite, {"suite_id", "command_argv", "exit_status", "stdout_summary", "stdout_sha256", "stderr_sha256", "executable_sha256", "environment_sha256", "input_hashes", "passed", "failed", "skipped", "skip_disposition", "status", "regression_test_ids"}, suite["suite_id"])
        if not isinstance(suite["exit_status"], int) or isinstance(suite["exit_status"], bool) or suite["exit_status"] != 0:
            raise ContractError(f"invalid execution status: {suite['suite_id']}")
        if suite["status"] != "PASS" or suite["failed"] != 0 or suite["passed"] <= 0:
            raise ContractError(f"required suite did not pass: {suite['suite_id']}")
        expected_summary = f"{suite['passed']} passed, {suite['failed']} failed, {suite['skipped']} skipped\n"
        if suite["stdout_summary"] != expected_summary or digest(expected_summary.encode()) != suite["stdout_sha256"]:
            raise ContractError(f"forged execution summary: {suite['suite_id']}")
        if suite["stderr_sha256"] != digest(b"") or suite["environment_sha256"] != environment_hash:
            raise ContractError(f"execution binding mismatch: {suite['suite_id']}")
        for path_text, expected_hash in suite["input_hashes"].items():
            if digest(safe_artifact(root, path_text).read_bytes()) != expected_hash:
                raise ContractError(f"stale test input: {suite['suite_id']}:{path_text}")
        executable = suite["command_argv"][0]
        if executable.startswith("./") and digest(safe_artifact(root, executable[2:]).read_bytes()) != suite["executable_sha256"]:
            raise ContractError(f"stale test executable: {suite['suite_id']}")
        if suite["skipped"] and suite["skip_disposition"] == "none":
            raise ContractError(f"unexplained skips: {suite['suite_id']}")
    if set(release["required_test_suites"]) != set(GOVERNED_GATES): raise ContractError("release gate registry mismatch")

    regression_tests = {test for suite in suites.values() for test in suite["regression_test_ids"]}
    trace_tests = {test for edge in edges for test in edge["test_ids"]}
    for test_id in regression_tests | trace_tests:
        if test_id not in regression_tests:
            raise ContractError(f"unresolved test id: {test_id}")
    for entry in entries.values():
        if entry["regression_test"] not in regression_tests:
            raise ContractError(f"{entry['failure_id']}: unresolved regression test")

    return {"root": root, "profile": profile, "requirements": reqs, "claims": cls, "evidence": evs, "trace": trace, "limitations": lims, "corpus": entries, "release": release, "suites": suites, "failures": failures, "minimized": minimized}


def normalize_output(data: bytes) -> str:
    """Remove only elapsed-time values, the sole documented nondeterministic field."""
    text = data.decode("utf-8", errors="replace").replace("\r\n", "\n")
    text = re.sub(r"\(\d+(?:\.\d+)? ms(?: total)?\)", "(<elapsed-ms> ms)", text)
    text = re.sub(r"(Ran \d+ tests? in )\d+(?:\.\d+)?s", r"\1<elapsed-s>s", text)
    return text


def parse_test_result(output: str, returncode: int) -> tuple[int, int, int, list[str]]:
    passed_matches = [int(x) for x in re.findall(r"\[\s*PASSED\s*\]\s+(\d+) tests?", output)]
    failed_matches = [int(x) for x in re.findall(r"\[\s*FAILED\s*\]\s+(\d+) tests?", output)]
    skipped_matches = [int(x) for x in re.findall(r"\[\s*SKIPPED\s*\]\s+(\d+) tests?", output)]
    skip_ids = re.findall(r"^\[\s*SKIPPED\s*\]\s+([A-Za-z0-9_]+\.[A-Za-z0-9_]+)(?:\s|$)", output, re.MULTILINE)
    # GTest prints each skip once at execution and once in the final list.
    skip_ids = list(dict.fromkeys(skip_ids))
    if passed_matches:
        return passed_matches[-1], failed_matches[-1] if failed_matches else 0, skipped_matches[-1] if skipped_matches else 0, skip_ids
    ran = re.findall(r"Ran (\d+) tests?", output)
    if ran:
        skipped = int(re.findall(r"skipped=(\d+)", output)[-1]) if re.findall(r"skipped=(\d+)", output) else 0
        failures = sum(int(values[-1]) if values else 0 for values in (re.findall(r"failures=(\d+)", output), re.findall(r"errors=(\d+)", output)))
        return int(ran[-1]) - skipped - failures, failures, skipped, []
    return (1 if returncode == 0 else 0), (0 if returncode == 0 else 1), 0, []


def valid_sha256(value: Any) -> bool:
    return isinstance(value, str) and SHA256_RE.fullmatch(value) is not None and len(set(value)) > 1


def gate_bindings(root: Path, argv: list[str]) -> dict[str, Any]:
    attestation = config(root, "test_attestation")
    executable_path = Path(argv[0][2:]) if argv[0].startswith("./") else Path(shutil.which(argv[0]) or argv[0])
    if not executable_path.is_absolute(): executable_path = root / executable_path
    input_hashes = {token: digest((root / token).read_bytes()) for token in argv[1:] if not token.startswith("-") and (root / token).is_file()}
    source_state_sha256 = digest(canonical({"source_revision": attestation["source_revision"], "source_hashes": attestation["source_hashes"]}))
    compiler_environment_sha256 = digest(canonical({
        "compiler_mode": attestation["compiler_mode"],
        "declared_environment": attestation["environment"],
        "host": platform.platform(),
        "python": sys.version,
    }))
    return {
        "executable_sha256": digest(executable_path.read_bytes()),
        "input_hashes": input_hashes,
        "source_state_sha256": source_state_sha256,
        "build_configuration_sha256": attestation["build_configuration_sha256"],
        "compiler_environment_sha256": compiler_environment_sha256,
    }


def record_passes_policy(suite_id: str, record: dict[str, Any]) -> bool:
    policy = GATE_POLICIES[suite_id]
    if "accepted_outcomes" in policy:
        outcome_ok = any(
            record["passed"] == outcome["passed"]
            and record["skipped"] == outcome["skipped"]
            and record["skip_ids"] == outcome["skip_ids"]
            for outcome in policy["accepted_outcomes"]
        )
        return outcome_ok and record["failed"] == 0 and record["exit_status"] == 0
    passed_ok = record["passed"] == policy["exact_passed"] if "exact_passed" in policy else record["passed"] >= policy["minimum_passed"]
    return passed_ok and record["failed"] == 0 and record["skipped"] == policy["expected_skipped"] and record["skip_ids"] == policy["allowed_skip_ids"] and record["exit_status"] == 0


def gate_failure_reason(suite_id: str, record: dict[str, Any]) -> str:
    policy = GATE_POLICIES[suite_id]
    expected = policy.get("accepted_outcomes", {
        "minimum_passed": policy.get("minimum_passed"),
        "exact_passed": policy.get("exact_passed"),
        "skipped": policy.get("expected_skipped"),
        "skip_ids": policy.get("allowed_skip_ids"),
    })
    return (
        f"{suite_id}: status={record.get('status')!r} exit={record.get('exit_status')!r} "
        f"passed={record.get('passed')!r} failed={record.get('failed')!r} "
        f"skipped={record.get('skipped')!r} skip_ids={record.get('skip_ids')!r}; "
        f"accepted={expected!r}"
    )


def run_governed_gates(root: Path) -> list[dict[str, Any]]:
    records = []
    for suite_id, (argv, test_ids) in GOVERNED_GATES.items():
        timeout_seconds = GATE_POLICIES[suite_id]["timeout_seconds"]
        print(f"[phase5] running {suite_id} (timeout={timeout_seconds}s)", file=sys.stderr, flush=True)
        completed = subprocess.run(argv, cwd=root, capture_output=True, timeout=timeout_seconds, check=False)
        stdout, stderr = normalize_output(completed.stdout), normalize_output(completed.stderr)
        passed, failed, skipped, skip_ids = parse_test_result(stdout + stderr, completed.returncode)
        record = {"suite_id": suite_id, "argv": argv, "timeout_seconds": timeout_seconds, "exit_status": completed.returncode, "stdout_normalized": stdout, "stderr_normalized": stderr, "stdout_sha256": digest(stdout.encode()), "stderr_sha256": digest(stderr.encode()), "stdout_size": len(stdout.encode()), "stderr_size": len(stderr.encode()), **gate_bindings(root, argv), "passed": passed, "failed": failed, "skipped": skipped, "skip_ids": skip_ids, "regression_test_ids": test_ids}
        record["status"] = "PASS" if record_passes_policy(suite_id, record) else "FAIL"
        records.append(record)
    return records


def validate_gate_records(records: list[dict[str, Any]], root: Path | None = None) -> bool:
    try:
        by_id = unique(records, "suite_id", "fresh gate records")
    except ContractError:
        return False
    if set(by_id) != set(GOVERNED_GATES): return False
    for suite_id, (argv, test_ids) in GOVERNED_GATES.items():
        record = by_id[suite_id]
        try:
            exact(record, {"suite_id", "argv", "timeout_seconds", "exit_status", "stdout_normalized", "stderr_normalized", "stdout_sha256", "stderr_sha256", "stdout_size", "stderr_size", "executable_sha256", "input_hashes", "source_state_sha256", "build_configuration_sha256", "compiler_environment_sha256", "passed", "failed", "skipped", "skip_ids", "regression_test_ids", "status"}, suite_id)
        except ContractError:
            return False
        if record["argv"] != argv or record["timeout_seconds"] != GATE_POLICIES[suite_id]["timeout_seconds"] or record["regression_test_ids"] != test_ids or type(record["exit_status"]) is not int: return False
        for field in ("stdout_normalized", "stderr_normalized"):
            if not isinstance(record[field], str): return False
            prefix = field.removesuffix("_normalized")
            encoded = record[field].encode()
            if record[f"{prefix}_size"] != len(encoded) or record[f"{prefix}_sha256"] != digest(encoded): return False
        if any(not valid_sha256(record[field]) for field in ("stdout_sha256", "stderr_sha256", "executable_sha256", "source_state_sha256", "build_configuration_sha256", "compiler_environment_sha256")): return False
        if not isinstance(record["input_hashes"], dict) or any(not isinstance(path, str) or not valid_sha256(value) for path, value in record["input_hashes"].items()): return False
        if any(type(record[field]) is not int or record[field] < 0 for field in ("stdout_size", "stderr_size", "passed", "failed", "skipped")): return False
        if not isinstance(record["skip_ids"], list) or any(not isinstance(value, str) for value in record["skip_ids"]): return False
        if record["status"] != ("PASS" if record_passes_policy(suite_id, record) else "FAIL") or record["status"] != "PASS": return False
        if root is not None and any(record[key] != value for key, value in gate_bindings(root, argv).items()): return False
    return True


def semantic_gates(state: dict[str, Any], fresh_records: list[dict[str, Any]] | None = None) -> dict[str, bool]:
    root, evs = state["root"], state["evidence"]
    def artifact(evidence_id: str) -> Any:
        return load_json(safe_artifact(root, evs[evidence_id]["artifact"]))
    raw5, raw6, raw7 = artifact("EV-P3-V5-RAW"), artifact("EV-P3-V6-RAW"), artifact("EV-P3-V7-RAW")
    rep5, rep6, rep7 = artifact("EV-P3-V5-REPORT"), artifact("EV-P3-V6-REPORT"), artifact("EV-P3-V7-REPORT")
    freeze = artifact("EV-P3-V7-FREEZE")
    phase3_schema = raw5.get("schema") == "graphx.fhss.phase3-raw-evaluation.v3" and raw6.get("schema") == raw7.get("schema") == "graphx.fhss.phase3-raw-evaluation.v4" and rep5.get("schema") == "graphx.fhss.phase3-report.v3" and rep6.get("schema") == rep7.get("schema") == "graphx.fhss.phase3-report.v4" and freeze.get("schema") == "graphx.fhss.phase3-v7-freeze-manifest.v1"
    phase3_history = rep5.get("overall_pass") is False and rep6.get("overall_pass") is False and rep5.get("raw_cases_sha256") == raw5.get("cases_sha256") and rep6.get("raw_cases_sha256") == raw6.get("cases_sha256") and rep5.get("machine_acceptance", {}).get("all_receiver_executions_completed", {}).get("pass") is False and rep6.get("machine_acceptance", {}).get("all_receiver_executions_completed", {}).get("pass") is False
    frozen_profile = freeze.get("profile", {})
    phase3_v7 = rep7.get("overall_pass") is True and rep7.get("matrix_complete") is True and rep7.get("raw_cases_sha256") == raw7.get("cases_sha256") and rep7.get("profile_sha256") == raw7.get("profile_sha256") == frozen_profile.get("canonical_sha256") and rep7.get("frozen_sha256") == raw7.get("frozen_sha256") == frozen_profile.get("frozen_sha256") and rep7.get("machine_acceptance", {}).get("all_receiver_executions_completed", {}).get("pass") is True and len(raw7.get("cases", [])) == 162
    p4_profile, p4_ready, p4_corr = artifact("EV-P4-PROFILE"), artifact("EV-P4-READINESS"), artifact("EV-P4-CORRELATION")
    phase4_schema = p4_profile.get("schema") == "graphx.fhss.phase4-validation-profile.v1" and p4_ready.get("schema") == "graphx.fhss.phase4-readiness.v1" and p4_corr.get("schema") == "graphx.fhss.phase4-correlation-report.v1"
    counts = p4_ready.get("counts")
    expected_classes = {"conducted", "channel_emulator", "ota"}
    zero_counts = isinstance(counts, dict) and set(counts) == expected_classes and all(isinstance(counts[key], dict) and set(counts[key]) == {"captures", "sessions", "searched_seconds"} and counts[key]["captures"] == 0 and counts[key]["sessions"] == 0 and counts[key]["searched_seconds"] == 0.0 for key in expected_classes)
    phase4_boundary = p4_profile.get("status") == "synthetic_only_infrastructure_physical_validation_unavailable" and p4_ready.get("status") == p4_corr.get("status") == "UNAVAILABLE_DEFERRED" and zero_counts and p4_ready.get("held_out_replay_count") == 0 and p4_ready.get("simulation_hardware_paired_point_count") == 0 and p4_corr.get("paired_points") == []
    # Historical attestations are provenance only. Release PASS always requires
    # fresh governed records from this invocation.
    tests = validate_gate_records(fresh_records, root) if fresh_records is not None else False
    return {"schemas": phase3_schema and phase4_schema, "history": phase3_history, "synthetic": phase3_v7, "physical_boundary": phase4_boundary, "tests": tests, "traceability": True, "corpus": True}


def result(root: Path, fresh_records: list[dict[str, Any]] | None = None) -> tuple[dict[str, Any], dict[str, Any], str]:
    state = validate(root)
    gates = semantic_gates(state, fresh_records)
    tool_path = Path(__file__).resolve()
    bindings = {name: digest((root / "libdsp" / "config" / f"{PREFIX}{name}_v1.json").read_bytes()) for name in state["release"]["registries"] + ["qualification_profile", "release_qualification_manifest"]}
    software_pass = all(gates.values())
    synthetic_pass = gates["schemas"] and gates["history"] and gates["synthetic"]
    verdicts = {
        "software_engineering_release_readiness": "PASS" if software_pass else "FAIL",
        "synthetic_characterization": "PASS" if synthetic_pass else "FAIL",
        "recorded_iq_hil_validation": "UNAVAILABLE_DEFERRED",
        "production_rf_qualification": "NOT_QUALIFIED",
    }
    claims = {"CLAIM-SOFTWARE-READY": verdicts["software_engineering_release_readiness"], "CLAIM-SYNTHETIC-CHARACTERIZED": verdicts["synthetic_characterization"], "CLAIM-RECORDED-HIL": verdicts["recorded_iq_hil_validation"], "CLAIM-PRODUCTION-RF": verdicts["production_rf_qualification"]}
    raw = {
        "schema": "graphx.fhss.phase5-qualification-raw.v1",
        "tool_version": VERSION,
        "tool_sha256": digest(tool_path.read_bytes()),
        "profile_sha256": bindings["qualification_profile"],
        "registry_hashes": dict(sorted(bindings.items())),
        "validated_evidence": len(state["evidence"]),
        "trace_links": len(state["trace"]["links"]),
        "regression_entries": len(state["corpus"]),
        "physical_evidence_counts": state["profile"]["physical_evidence_counts"],
        "claim_results": claims,
        "evaluated_gates": dict(sorted(gates.items())),
        "execution_records": fresh_records if fresh_records is not None else [],
        "verdicts": verdicts,
        "ambiguity_guard": "software PASS does not imply recorded/HIL or production-RF qualification",
    }
    aggregate = {
        "schema": "graphx.fhss.phase5-qualification-report.v1",
        "raw_sha256": digest(canonical(raw)),
        "criteria": {"A": "PASS" if gates["schemas"] and gates["history"] and gates["physical_boundary"] else "FAIL", "B": "PASS" if gates["traceability"] else "FAIL", "C": "PASS" if gates["history"] and gates["synthetic"] else "FAIL", "D": "PASS" if gates["corpus"] else "FAIL", "E": "PASS" if gates["tests"] else "FAIL", "F": "PASS" if gates["tests"] else "FAIL", "G": "PASS" if gates["corpus"] and gates["physical_boundary"] else "FAIL", "H": "PASS" if gates["tests"] else "FAIL", "I": "PASS" if software_pass else "FAIL"},
        "verdicts": verdicts,
        "counts": {"requirements": len(state["requirements"]), "claims": len(state["claims"]), "evidence": len(state["evidence"]), "trace_links": len(state["trace"]["links"]), "regression_entries": len(state["corpus"]), **state["profile"]["physical_evidence_counts"]},
        "limitations": sorted(state["limitations"]),
        "statement": "Synthetic/software qualification only; no recorded, HIL, regulatory, interoperability, or production-RF claim.",
    }
    lines = ["# FHSS Phase 5 qualification report", "", aggregate["statement"], "", "## Verdicts", ""]
    for key, value in verdicts.items(): lines.append(f"- `{key}`: `{value}`")
    lines += ["", "## Acceptance", ""]
    for key, value in aggregate["criteria"].items(): lines.append(f"- `{key}`: `{value}`")
    lines += ["", f"Raw SHA-256: `{aggregate['raw_sha256']}`", "", "Physical sessions, captures, searched exposure, paired points, and recorded field failures: zero.", "", "Partial-hop t800/t1600 remains unsupported. The generic executor unbounded-join limitation remains disclosed. Native-Metal skips are not counted as passes.", ""]
    return raw, aggregate, "\n".join(lines)


def atomic_create(path: Path, data: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists(): raise ContractError(f"refusing to overwrite {path}")
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        return temporary
    except BaseException:
        try: os.unlink(temporary)
        except FileNotFoundError: pass
        raise


def qualify(root: Path, output: Path, fail_after: int | None = None, execute_gates: bool = False) -> None:
    records = run_governed_gates(root) if execute_gates else None
    if execute_gates and not validate_gate_records(records, root):
        for record in records:
            suite_id = record.get("suite_id")
            if suite_id not in GATE_POLICIES or not record_passes_policy(suite_id, record):
                print(f"[phase5] FAILED {gate_failure_reason(suite_id, record)}", file=sys.stderr, flush=True)
        raise ContractError("fresh required gate failed; qualification artifacts were not written")
    raw, report, markdown = result(root, records)
    names = ["fhss_phase5_qualification_raw_v1.json", "fhss_phase5_qualification_report_v1.json", "fhss_phase5_qualification_report_v1.md"]
    if any((output / name).exists() for name in names): raise ContractError("qualification output already exists")
    output.mkdir(parents=True, exist_ok=True)
    data = [canonical(raw), canonical(report), markdown.encode()]
    temps, committed = [], []
    try:
        for name, blob in zip(names, data): temps.append((output / name, atomic_create(output / name, blob)))
        for index, (target, temporary) in enumerate(temps, 1):
            os.replace(temporary, target); committed.append(target)
            if fail_after == index: raise OSError("injected transaction failure")
    except BaseException:
        for target in committed: target.unlink(missing_ok=True)
        for _, temporary in temps:
            try: os.unlink(temporary)
            except FileNotFoundError: pass
        raise


def verify(root: Path, raw_path: Path, report_path: Path, markdown_path: Path, execute_gates: bool = False) -> None:
    actual_raw, actual_report = load_json(raw_path), load_json(report_path)
    bound_records = actual_raw.get("execution_records", [])
    if execute_gates:
        rerun = run_governed_gates(root)
        if not validate_gate_records(rerun, root): raise ContractError("fresh required gate failed")
        # Normalized output excludes elapsed time only; every remaining
        # deterministic captured field must equal the bound qualification.
        if canonical(rerun) != canonical(bound_records): raise ContractError("bound gate result does not match fresh execution")
    raw, report, markdown = result(root, bound_records if bound_records else None)
    if actual_raw.get("schema") != "graphx.fhss.phase5-qualification-raw.v1" or actual_report.get("schema") != "graphx.fhss.phase5-qualification-report.v1":
        raise ContractError("wrong qualification result schema/version")
    expected = [(raw_path, canonical(raw)), (report_path, canonical(report)), (markdown_path, markdown.encode())]
    for path, data in expected:
        if path.read_bytes() != data: raise ContractError(f"verification mismatch: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    sub = parser.add_subparsers(dest="command", required=True)
    q = sub.add_parser("qualify"); q.add_argument("--output-dir", type=Path, required=True)
    v = sub.add_parser("verify"); v.add_argument("--raw", type=Path, required=True); v.add_argument("--report", type=Path, required=True); v.add_argument("--markdown", type=Path, required=True)
    sub.add_parser("validate")
    args = parser.parse_args()
    try:
        if args.command == "validate": validate(args.root)
        elif args.command == "qualify": qualify(args.root, args.output_dir, execute_gates=True)
        else: verify(args.root, args.raw, args.report, args.markdown, execute_gates=True)
    except (ContractError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
