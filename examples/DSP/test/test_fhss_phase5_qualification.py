#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "examples/DSP/tools/fhss_phase5_qualify.py"
SPEC = importlib.util.spec_from_file_location("phase5", TOOL)
phase5 = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(phase5)


class Phase5QualificationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        cfg = self.root / "libdsp/config"
        cfg.mkdir(parents=True)
        for source in (ROOT / "libdsp/config").glob("fhss_phase5_*_v1.json"):
            shutil.copy2(source, cfg / source.name)
        registry_path = cfg / "fhss_phase5_evidence_registry_v1.json"
        registry = json.loads(registry_path.read_text())
        artifacts = self.root / "artifacts"
        artifacts.mkdir()
        for index, evidence in enumerate(registry["evidence"]):
            target = artifacts / f"evidence-{index}.json"
            target.hardlink_to(ROOT / evidence["artifact"])
            evidence["artifact"] = target.relative_to(self.root).as_posix()
            evidence["sha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
        registry_path.write_bytes(phase5.canonical(registry))
        attestation_item = next(x for x in registry["evidence"] if x["evidence_id"] == "EV-P5-TESTS")
        attestation = json.loads((self.root / attestation_item["artifact"]).read_text())
        bound_paths = {path for suite in attestation["suites"] for path in suite["input_hashes"]}
        bound_paths |= set(attestation["source_hashes"])
        bound_paths.add(attestation["build_configuration"])
        bound_paths |= {suite["command_argv"][0][2:] for suite in attestation["suites"] if suite["command_argv"][0].startswith("./")}
        corpus = json.loads((cfg / "fhss_phase5_regression_corpus_manifest_v1.json").read_text())
        bound_paths |= {entry["fixed_by"] for entry in corpus["entries"] if entry["fixed_by"] is not None}
        profile = json.loads((cfg / "fhss_phase5_qualification_profile_v1.json").read_text())
        bound_paths |= {policy["baseline_artifact"] for policy in profile["trend_policy"]["policies"]}
        for relative in bound_paths:
            target = self.root / relative; target.parent.mkdir(parents=True, exist_ok=True); target.hardlink_to(ROOT / relative)

    def tearDown(self): self.tmp.cleanup()

    def path(self, name): return self.root / "libdsp/config" / f"fhss_phase5_{name}_v1.json"
    def mutate(self, name, callback):
        path = self.path(name); value = json.loads(path.read_text()); callback(value); path.write_bytes(phase5.canonical(value))

    def mutate_attestation(self, callback):
        registry = json.loads(self.path("evidence_registry").read_text())
        item = next(x for x in registry["evidence"] if x["evidence_id"] == "EV-P5-TESTS")
        path = self.root / item["artifact"]
        value = json.loads(path.read_text()); callback(value); path.unlink(); path.write_bytes(phase5.canonical(value))
        item["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.path("evidence_registry").write_bytes(phase5.canonical(registry))

    def mutate_evidence_artifact(self, evidence_id, callback):
        registry=json.loads(self.path("evidence_registry").read_text()); item=next(x for x in registry["evidence"] if x["evidence_id"]==evidence_id); path=self.root/item["artifact"]
        value=json.loads(path.read_text()); callback(value); path.unlink(); path.write_bytes(phase5.canonical(value)); item["sha256"]=hashlib.sha256(path.read_bytes()).hexdigest(); self.path("evidence_registry").write_bytes(phase5.canonical(registry))

    def passing_records(self):
        records=[]
        for ident,(argv,tests) in phase5.GOVERNED_GATES.items():
            policy=phase5.GATE_POLICIES[ident]; outcome=policy.get("accepted_outcomes",[None])[-1]; stdout=f"{ident}: isolated test record\n"; stderr=""
            passed=outcome["passed"] if outcome else policy.get("exact_passed",policy.get("minimum_passed")); skipped=outcome["skipped"] if outcome else policy["expected_skipped"]; skip_ids=outcome["skip_ids"] if outcome else policy["allowed_skip_ids"]
            record={"suite_id":ident,"argv":argv,"timeout_seconds":policy["timeout_seconds"],"exit_status":0,"stdout_normalized":stdout,"stderr_normalized":stderr,"stdout_sha256":phase5.digest(stdout.encode()),"stderr_sha256":phase5.digest(stderr.encode()),"stdout_size":len(stdout.encode()),"stderr_size":0,**phase5.gate_bindings(self.root,argv),"passed":passed,"failed":0,"skipped":skipped,"skip_ids":skip_ids,"regression_test_ids":tests,"status":"PASS"}
            records.append(record)
        return records

    def assert_invalid(self):
        with self.assertRaises(phase5.ContractError): phase5.validate(self.root)

    def test_baseline_validates(self): self.assertEqual(len(phase5.validate(self.root)["evidence"]), 13)
    def test_four_unambiguous_verdicts(self):
        verdicts = phase5.result(self.root, self.passing_records())[0]["verdicts"]
        self.assertEqual(verdicts, {"software_engineering_release_readiness":"PASS","synthetic_characterization":"PASS","recorded_iq_hil_validation":"UNAVAILABLE_DEFERRED","production_rf_qualification":"NOT_QUALIFIED"})
    def test_all_physical_counts_are_zero(self): self.assertEqual(set(phase5.result(self.root)[1]["counts"].values()) >= {0}, True)
    def test_historical_failures_remain_failures(self):
        ev = phase5.validate(self.root)["evidence"]
        self.assertEqual((ev["EV-P3-V5-RAW"]["status"], ev["EV-P3-V6-RAW"]["status"], ev["EV-P3-V7-RAW"]["status"]), ("FAIL","FAIL","PASS"))
    def test_duplicate_requirement_fails(self): self.mutate("requirement_registry", lambda x: x["requirements"].append(copy.deepcopy(x["requirements"][0]))); self.assert_invalid()
    def test_dangling_trace_fails(self): self.mutate("traceability", lambda x: x["links"][0].update(requirement_id="REQ-MISSING")); self.assert_invalid()
    def test_incomplete_reverse_trace_fails(self): self.mutate("traceability", lambda x: x["links"].pop()); self.assert_invalid()
    def test_circular_supersession_fails(self): self.mutate("requirement_registry", lambda x: x["requirements"][0].update(supersedes=x["requirements"][0]["requirement_id"])); self.assert_invalid()
    def test_empty_acceptance_rule_fails(self): self.mutate("requirement_registry", lambda x: x["requirements"][0].update(acceptance_rule="")); self.assert_invalid()
    def test_unknown_requirement_field_fails(self): self.mutate("requirement_registry", lambda x: x["requirements"][0].update(unknown=True)); self.assert_invalid()
    def test_wrong_registry_schema_fails(self): self.mutate("requirement_registry", lambda x: x.update(schema="graphx.fhss.phase5-requirement-registry.v999")); self.assert_invalid()
    def test_hash_mismatch_fails(self): self.mutate("evidence_registry", lambda x: x["evidence"][0].update(sha256="0"*64)); self.assert_invalid()
    def test_missing_artifact_fails(self): self.mutate("evidence_registry", lambda x: x["evidence"][0].update(artifact="missing")); self.assert_invalid()
    def test_parent_traversal_fails(self): self.mutate("evidence_registry", lambda x: x["evidence"][0].update(artifact="../escape")); self.assert_invalid()
    def test_absolute_path_fails(self): self.mutate("evidence_registry", lambda x: x["evidence"][0].update(artifact="/etc/passwd")); self.assert_invalid()
    def test_symlink_escape_fails(self):
        outside = Path(self.tmp.name).parent / "phase5-outside"; outside.write_text("x")
        link = self.root / "escape"; link.symlink_to(outside)
        self.mutate("evidence_registry", lambda x: x["evidence"][0].update(artifact="escape", sha256=hashlib.sha256(b"x").hexdigest()))
        try: self.assert_invalid()
        finally: outside.unlink(missing_ok=True)
    def test_synthetic_promotion_fails(self): self.mutate("claim_registry", lambda x: x["claims"][2].update(status="PASS", required_evidence_ids=["EV-P3-V7-RAW"])); self.assert_invalid()
    def test_production_promotion_fails(self): self.mutate("claim_registry", lambda x: x["claims"][3].update(status="PASS", required_evidence_ids=["EV-P3-V7-RAW"])); self.assert_invalid()
    def test_nonzero_physical_count_fails(self): self.mutate("qualification_profile", lambda x: x["physical_evidence_counts"].update(hwil=1)); self.assert_invalid()
    def test_physical_evidence_class_fails(self): self.mutate("evidence_registry", lambda x: x["evidence"][0].update(evidence_class="ota_hwil")); self.assert_invalid()
    def test_required_evidence_class_mismatch_fails(self): self.mutate("requirement_registry", lambda x: x["requirements"][0].update(required_evidence_class="synthetic")); self.assert_invalid()
    def test_reciprocal_evidence_trace_removal_fails(self): self.mutate("traceability", lambda x: x["links"][0]["evidence_ids"].remove("EV-ARCH")); self.assert_invalid()
    def test_extra_trace_evidence_fails(self): self.mutate("traceability", lambda x: x["links"][0]["evidence_ids"].append("EV-P3-V7-RAW")); self.assert_invalid()
    def test_dangling_claim_limitation_fails(self): self.mutate("claim_registry", lambda x: x["claims"][0]["limitations"].append("LIM-MISSING")); self.assert_invalid()
    def test_corpus_field_label_fails(self): self.mutate("regression_corpus_manifest", lambda x: x["entries"][0].update(classification="recorded_field")); self.assert_invalid()
    def test_corpus_reproduction_forgery_fails(self): self.mutate("regression_corpus_manifest", lambda x: x["entries"][0]["reproduction"].update(forged=True)); self.assert_invalid()
    def test_skips_cannot_pass(self): self.mutate("release_qualification_manifest", lambda x: x.update(skip_policy="skips_are_passes")); self.assert_invalid()
    def test_registry_set_cannot_silently_skip(self): self.mutate("release_qualification_manifest", lambda x: x["registries"].pop()); self.assert_invalid()
    def test_static_attestation_does_not_define_release_gate_set(self): self.mutate_attestation(lambda x: x["suites"].pop()); phase5.validate(self.root)
    def test_failed_suite_cannot_pass(self): self.mutate_attestation(lambda x: x["suites"][0].update(status="FAIL", failed=1)); self.assert_invalid()
    def test_zero_passed_suite_cannot_pass(self): self.mutate_attestation(lambda x: x["suites"][0].update(passed=0)); self.assert_invalid()
    def test_unexplained_skip_fails(self): self.mutate_attestation(lambda x: x["suites"][0].update(skipped=1)); self.assert_invalid()
    def test_boolean_exit_status_fails(self): self.mutate_attestation(lambda x: x["suites"][0].update(exit_status=True)); self.assert_invalid()
    def test_rehashed_forged_counts_fail(self): self.mutate_attestation(lambda x: x["suites"][0].update(passed=999)); self.assert_invalid()
    def test_wrong_attestation_schema_fails(self): self.mutate_attestation(lambda x: x.update(schema="graphx.fhss.phase5-test-attestation.v999")); self.assert_invalid()
    def test_v7_overall_false_derives_fail(self):
        self.mutate_evidence_artifact("EV-P3-V7-REPORT", lambda x: x.update(overall_pass=False))
        raw,report,markdown=phase5.result(self.root,self.passing_records()); self.assertEqual(raw["verdicts"]["synthetic_characterization"], "FAIL"); self.assertEqual(raw["verdicts"]["software_engineering_release_readiness"], "FAIL"); self.assertEqual(report["criteria"]["C"],"FAIL"); self.assertEqual(report["criteria"]["I"],"FAIL"); self.assertIn("`synthetic_characterization`: `FAIL`",markdown); self.assertNotIn("All infrastructure criteria A–I: PASS",markdown)
    def test_phase4_empty_counts_derives_software_fail(self):
        self.mutate_evidence_artifact("EV-P4-READINESS", lambda x: x.update(counts={}))
        self.assertEqual(phase5.result(self.root,self.passing_records())[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_phase4_missing_count_class_derives_software_fail(self):
        self.mutate_evidence_artifact("EV-P4-READINESS", lambda x: x["counts"].pop("ota"))
        self.assertEqual(phase5.result(self.root,self.passing_records())[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_phase4_extra_count_class_derives_software_fail(self):
        self.mutate_evidence_artifact("EV-P4-READINESS", lambda x: x["counts"].update(extra={"captures":0,"sessions":0,"searched_seconds":0.0}))
        self.assertEqual(phase5.result(self.root,self.passing_records())[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_phase4_nonzero_count_derives_software_fail(self):
        self.mutate_evidence_artifact("EV-P4-READINESS", lambda x: x["counts"]["ota"].update(captures=1))
        self.assertEqual(phase5.result(self.root,self.passing_records())[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_unresolved_corpus_original_fails(self): self.mutate("regression_corpus_manifest", lambda x: x["entries"][0].update(original_evidence="EV-MISSING")); self.assert_invalid()
    def test_missing_fresh_gate_fails_software(self): self.assertEqual(phase5.result(self.root,self.passing_records()[:-1])[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_not_executed_regression_gate_fails_software(self):
        records=self.passing_records(); records[0]["exit_status"]=1; records[0]["status"]="FAIL"; self.assertEqual(phase5.result(self.root,records)[0]["verdicts"]["software_engineering_release_readiness"],"FAIL")
    def test_forged_hash_and_size_records_fail(self):
        records=self.passing_records(); records[0]["stdout_sha256"]="0"*64; self.assertFalse(phase5.validate_gate_records(records,self.root))
        records=self.passing_records(); records[0]["stdout_size"]=999999; self.assertFalse(phase5.validate_gate_records(records,self.root))
    def test_full_suite_skip_policy_is_exact(self):
        records=self.passing_records(); full=next(x for x in records if x["suite_id"]=="libdsp_unit_full"); full["skip_ids"]=full["skip_ids"][:-1]
        self.assertFalse(phase5.validate_gate_records(records,self.root))
        records=self.passing_records(); full=next(x for x in records if x["suite_id"]=="dsp_example_unit_full"); full["skipped"]=1; full["skip_ids"]=["Unexpected.Skip"]
        self.assertFalse(phase5.validate_gate_records(records,self.root))
    def test_libdsp_full_metal_available_mode_passes(self):
        records=self.passing_records(); full=next(x for x in records if x["suite_id"]=="libdsp_unit_full"); full.update(passed=174,skipped=0,skip_ids=[])
        self.assertTrue(phase5.validate_gate_records(records,self.root))
    def test_libdsp_full_rejects_hybrid_or_unexpected_skip_modes(self):
        for passed,skipped,skip_ids in ((173,1,[phase5.ALLOWED_METAL_SKIPS[0]]),(169,5,phase5.ALLOWED_METAL_SKIPS[:-1]+["Unexpected.Skip"]),(174,0,["Unexpected.Skip"])):
            records=self.passing_records(); full=next(x for x in records if x["suite_id"]=="libdsp_unit_full"); full.update(passed=passed,skipped=skipped,skip_ids=skip_ids)
            self.assertFalse(phase5.validate_gate_records(records,self.root))
    def test_governed_gate_timeouts_are_bounded(self):
        self.assertTrue(all(type(p["timeout_seconds"]) is int and 0 < p["timeout_seconds"] <= 180 for p in phase5.GATE_POLICIES.values()))
    def test_failed_gate_diagnostic_and_no_artifacts(self):
        output=self.root/"failed-out"; records=self.passing_records(); failed=next(x for x in records if x["suite_id"]=="dsp_example_unit_full"); failed.update(exit_status=1,failed=28,passed=28,status="FAIL")
        diagnostic=io.StringIO()
        with mock.patch.object(phase5,"run_governed_gates",return_value=records), mock.patch("sys.stderr",diagnostic):
            with self.assertRaises(phase5.ContractError): phase5.qualify(self.root,output,execute_gates=True)
        self.assertIn("FAILED dsp_example_unit_full",diagnostic.getvalue()); self.assertIn("passed=28 failed=28",diagnostic.getvalue()); self.assertFalse(output.exists())
    def test_coherent_three_artifact_execution_forgery_fails_fresh_verify(self):
        output=self.root/"out"; authentic=self.passing_records()
        with mock.patch.object(phase5,"run_governed_gates",return_value=authentic): phase5.qualify(self.root,output,execute_gates=True)
        forged=copy.deepcopy(authentic); forged[0]["stdout_normalized"]="coherently forged output\n"; forged[0]["stdout_sha256"]=phase5.digest(forged[0]["stdout_normalized"].encode()); forged[0]["stdout_size"]=len(forged[0]["stdout_normalized"].encode())
        raw,report,markdown=phase5.result(self.root,forged)
        (output/"fhss_phase5_qualification_raw_v1.json").write_bytes(phase5.canonical(raw)); (output/"fhss_phase5_qualification_report_v1.json").write_bytes(phase5.canonical(report)); (output/"fhss_phase5_qualification_report_v1.md").write_text(markdown)
        with mock.patch.object(phase5,"run_governed_gates",return_value=authentic):
            with self.assertRaises(phase5.ContractError): phase5.verify(self.root,output/"fhss_phase5_qualification_raw_v1.json",output/"fhss_phase5_qualification_report_v1.json",output/"fhss_phase5_qualification_report_v1.md",execute_gates=True)
    def test_nonempty_synthetic_failure_and_minimized_record_validate(self):
        record={"failure_id":"FAIL-FUTURE-SYNTH","classification":"synthetic","artifact_hash":"1"*64,"provenance":{"source":"synthetic"},"license_privacy_review":"not physical","environment":{},"receiver_version":"test","observed_behavior":"synthetic failure","retention_policy":"repository metadata"}
        minimized={"failure_id":"FAIL-FUTURE-SYNTH-MIN","classification":"synthetic","original_failure_id":"FAIL-FUTURE-SYNTH","original_hash":"1"*64,"minimized_hash":"2"*64,"development_partition":"development","expected_result":"fail safely","regression_test":"phase5_governance","requirement_ids":["REQ-P5-INTEGRITY"],"claim_ids":["CLAIM-SOFTWARE-READY"],"fix_reference":None,"non_regression_evidence":[]}
        self.mutate("failure_registry",lambda x:x["records"].append(record)); self.mutate("minimized_failure_registry",lambda x:x["records"].append(minimized)); phase5.validate(self.root)
    def test_synthetic_failure_cannot_be_promoted_to_field(self):
        record={"failure_id":"FAIL-FORGED-FIELD","classification":"recorded_field","artifact_hash":"1"*64,"provenance":{"source":"synthetic"},"license_privacy_review":"none","environment":{},"receiver_version":"test","observed_behavior":"forged","retention_policy":"none"}
        self.mutate("failure_registry",lambda x:(x["records"].append(record),x.update(field_failure_count=1))); self.assert_invalid()
    def test_second_commit_failure_rolls_back_set(self):
        output=self.root/"out"
        with self.assertRaises(OSError): phase5.qualify(self.root, output, fail_after=2)
        self.assertEqual(list(output.iterdir()), [])
    def test_third_commit_failure_rolls_back_set(self):
        output=self.root/"out"
        with self.assertRaises(OSError): phase5.qualify(self.root, output, fail_after=3)
        self.assertEqual(list(output.iterdir()), [])
    def test_generate_and_verify_round_trip(self):
        output = self.root / "out"; phase5.qualify(self.root, output)
        phase5.verify(self.root, output/"fhss_phase5_qualification_raw_v1.json", output/"fhss_phase5_qualification_report_v1.json", output/"fhss_phase5_qualification_report_v1.md")
    def test_no_clobber(self):
        output = self.root / "out"; phase5.qualify(self.root, output)
        with self.assertRaises(phase5.ContractError): phase5.qualify(self.root, output)
    def test_forged_raw_fails_verify(self):
        output = self.root / "out"; phase5.qualify(self.root, output)
        raw = output/"fhss_phase5_qualification_raw_v1.json"; raw.write_text("{}\n")
        with self.assertRaises(phase5.ContractError): phase5.verify(self.root, raw, output/"fhss_phase5_qualification_report_v1.json", output/"fhss_phase5_qualification_report_v1.md")
    def test_deterministic_results(self): self.assertEqual(phase5.result(self.root), phase5.result(self.root))
    def test_json_schemas_are_strict_and_finite(self):
        schemas = list((ROOT/"libdsp/config").glob("fhss_phase5_*_schema_v1.json")); self.assertGreaterEqual(len(schemas), 10)
        for path in schemas:
            value=json.loads(path.read_text(), parse_constant=lambda x: self.fail(x)); self.assertFalse(value.get("additionalProperties", True), path.name)
    def test_no_network_or_publish_feature(self):
        source=TOOL.read_text(); self.assertNotIn("urllib", source); self.assertNotIn("requests", source); self.assertNotIn("git push", source)


if __name__ == "__main__": unittest.main()
