#!/usr/bin/env python3
"""Positive and adversarial tests for evidence-bound Phase 8 sign-off."""

import argparse
import copy
import hashlib
import importlib.util
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from unittest import mock
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[3]
SCRIPT = SOURCE / "examples/DSP/dashboard/operator/phase8_completion_report.py"
spec = importlib.util.spec_from_file_location("phase8_completion", SCRIPT)
module = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(module)
qualification_spec = importlib.util.spec_from_file_location(
    "phase8_qualification",
    SOURCE / "examples/DSP/dashboard/operator/phase8_qualification.py")
qualification_module = importlib.util.module_from_spec(qualification_spec)
assert qualification_spec.loader
qualification_spec.loader.exec_module(qualification_module)
operator_spec = importlib.util.spec_from_file_location(
    "fhss_dashboard_operator",
    SOURCE / "examples/DSP/dashboard/operator/fhss_dashboard_operator.py")
operator_module = importlib.util.module_from_spec(operator_spec)
assert operator_spec.loader
operator_spec.loader.exec_module(operator_module)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expect_rejected(args, label):
    try:
        module.generate(args)
    except Exception:
        return
    raise AssertionError(f"forged evidence accepted: {label}")


def main():
    # The qualification-only BiDi channel must accommodate the reviewed axe
    # result while remaining finite. Dashboard protocol clients stay at the
    # independently reviewed 256 KiB bound.
    assert (operator_module.FIREFOX_BIDI_REVIEWED_AXE_FRAME_BYTES <
            operator_module.FIREFOX_BIDI_RECEIVE_MAX_BYTES <= 4 * 1024 * 1024)
    operator_source = (SOURCE /
        "examples/DSP/dashboard/operator/fhss_dashboard_operator.py").read_text()
    assert operator_source.count("max_size=256 * 1024") == 3

    class CloseFrame:
        code = 1009

    class OversizeClose(Exception):
        rcvd = None
        sent = CloseFrame()

    class NormalClose(Exception):
        rcvd = type("NormalFrame", (), {"code": 1000})()
        sent = None

    assert operator_module.firefox_bidi_message_too_big(OversizeClose())
    assert not operator_module.firefox_bidi_message_too_big(NormalClose())

    with tempfile.TemporaryDirectory(
            prefix="graphx-firefox-group-test-") as group_temp:
        group_root = Path(group_temp)
        profile = group_root / "profile"
        profile.mkdir()
        child_pid_path = group_root / "child.pid"
        stub = (
            "import os,signal,subprocess,sys,time;"
            "signal.signal(signal.SIGTERM, signal.SIG_IGN);"
            "child=subprocess.Popen([sys.executable,'-c',"
            "'import signal,time; signal.signal(signal.SIGTERM, "
            "signal.SIG_IGN); time.sleep(300)']);"
            "open(sys.argv[1],'w').write(str(child.pid));"
            "time.sleep(300)")
        process = subprocess.Popen(
            [sys.executable, "-c", stub, str(child_pid_path)],
            start_new_session=True)
        pgid = process.pid
        try:
            deadline = time.monotonic() + 5
            while (not child_pid_path.is_file() and
                   time.monotonic() < deadline):
                time.sleep(0.01)
            assert child_pid_path.is_file()
            child_pid = int(child_pid_path.read_text())
            assert os.getpgid(child_pid) == pgid
            operator_module.stop_firefox_process_group(
                process, pgid, profile, start_new_session=True,
                grace_timeout=0,
                term_timeout=0.1, kill_timeout=5)
            assert process.poll() is not None
            assert not operator_module.firefox_process_group_alive(pgid)
        finally:
            try:
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass

    class NeverExits:
        pid = 424242

        @staticmethod
        def poll():
            return None

        @staticmethod
        def wait(timeout):
            raise subprocess.TimeoutExpired("firefox-stub", timeout)

    with (mock.patch.object(operator_module.os, "killpg") as killpg,
          mock.patch.object(operator_module.os, "getpgid",
                            return_value=424242)):
        try:
            operator_module.stop_firefox_process_group(
                NeverExits(), 424242, Path("diagnostic-profile"),
                start_new_session=True,
                grace_timeout=0, term_timeout=0, kill_timeout=0,
                platform_name="linux")
            raise AssertionError("live Firefox group cleanup failure was hidden")
        except RuntimeError as error:
            assert "pgid=424242" in str(error)
            assert "profile=diagnostic-profile" in str(error)
        assert killpg.call_args_list[-1] == mock.call(424242, 0)

    class DarwinProcess:
        pid = 434343
        killed = False

        def poll(self):
            return 0 if self.killed else None

        def wait(self, timeout):
            return 0

    darwin_process = DarwinProcess()
    darwin_signals = []

    def darwin_killpg(pgid, group_signal):
        assert pgid == darwin_process.pid
        darwin_signals.append(group_signal)
        if group_signal == signal.SIGKILL:
            darwin_process.killed = True
        elif group_signal == 0 and darwin_process.killed:
            raise ProcessLookupError

    with (mock.patch.object(operator_module.os, "killpg",
                            side_effect=darwin_killpg),
          mock.patch.object(operator_module.os, "getpgid",
                            return_value=darwin_process.pid)):
        operator_module.stop_firefox_process_group(
            darwin_process, darwin_process.pid, Path("darwin-profile"),
            start_new_session=True,
            grace_timeout=0, kill_timeout=0, platform_name="darwin")
    assert signal.SIGTERM not in darwin_signals
    assert signal.SIGKILL in darwin_signals

    class DarwinRootFallback:
        pid = 444444

        def __init__(self, root_kill_fails=False):
            self.killed = False
            self.waited = False
            self.root_kill_fails = root_kill_fails

        def poll(self):
            return 0 if self.killed else None

        def kill(self):
            if self.root_kill_fails:
                raise PermissionError("root EPERM")
            self.killed = True

        def wait(self, timeout):
            self.waited = True
            return 0

    fallback = DarwinRootFallback()

    def fallback_killpg(pgid, group_signal):
        assert pgid == fallback.pid
        if group_signal == signal.SIGKILL:
            raise PermissionError("group EPERM")
        if group_signal == 0 and fallback.killed:
            raise ProcessLookupError

    with (mock.patch.object(operator_module.os, "killpg",
                            side_effect=fallback_killpg),
          mock.patch.object(operator_module.os, "getpgid",
                            return_value=fallback.pid)):
        operator_module.stop_firefox_process_group(
            fallback, fallback.pid, Path("fallback-profile"),
            start_new_session=True, grace_timeout=0, kill_timeout=0,
            platform_name="darwin")
    assert fallback.killed and fallback.waited

    failed_fallback = DarwinRootFallback(root_kill_fails=True)

    def failed_fallback_killpg(pgid, group_signal):
        assert pgid == failed_fallback.pid
        if group_signal == signal.SIGKILL:
            raise PermissionError("group EPERM")

    with tempfile.TemporaryDirectory(
            prefix="graphx-firefox-root-fallback-") as fallback_temp:
        fallback_profile = Path(fallback_temp) / "profile"
        fallback_profile.mkdir()
        with (mock.patch.object(operator_module.os, "killpg",
                                side_effect=failed_fallback_killpg),
              mock.patch.object(operator_module.os, "getpgid",
                                return_value=failed_fallback.pid)):
            try:
                operator_module.stop_firefox_process_group(
                    failed_fallback, failed_fallback.pid, fallback_profile,
                    start_new_session=True, grace_timeout=0, kill_timeout=0,
                    platform_name="darwin")
                raise AssertionError("dual Firefox cleanup failure was hidden")
            except RuntimeError as error:
                assert "group and root kills both failed" in str(error)
                assert "group EPERM" in str(error)
                assert "root EPERM" in str(error)
        assert fallback_profile.is_dir()

    class FakeSocket:
        closed = False

        def close(self):
            self.closed = True

    with tempfile.TemporaryDirectory(
            prefix="graphx-firefox-failure-evidence-") as evidence_temp:
        evidence_root = Path(evidence_temp)
        retained_profile = evidence_root / "profile"
        retained_profile.mkdir()
        retained_log = (evidence_root / "firefox.log").open("w")
        retained_socket = FakeSocket()
        session = object.__new__(operator_module.FirefoxBidiSession)
        session._socket = retained_socket
        session._process = NeverExits()
        session._pgid = 424242
        session._profile = retained_profile
        session._log = retained_log
        try:
            with (mock.patch.object(
                    operator_module.FirefoxBidiSession, "call",
                    side_effect=RuntimeError("transport closed")),
                  mock.patch.object(
                    operator_module, "stop_firefox_process_group",
                    side_effect=RuntimeError("pgid=424242 cleanup failed"))):
                try:
                    session.close()
                    raise AssertionError("Firefox cleanup failure was hidden")
                except RuntimeError as error:
                    assert "pgid=424242" in str(error)
            assert retained_profile.is_dir()
            assert not retained_socket.closed
            assert not retained_log.closed
        finally:
            retained_log.close()

    outage_result = {
        "outage_http":{"reset_status":200, "health_status":200,
                       "snapshot_status":200},
        "coherent_return":{"agrees":False},
        "behavioral":{"malformed_hello":True, "atomic_replacement":False},
        "stable_reconnect_reset":{"before":2, "after":1},
        "messages":[{"level":"warning", "text":"deterministic fixture"}],
    }
    outage_predicates = operator_module.browser_websocket_outage_predicates(
        outage_result)
    assert outage_predicates == {
        "reset_status_200":True,
        "health_status_200":True,
        "snapshot_status_200":True,
        "coherent_return_agrees":False,
        "behavioral":{"malformed_hello":True, "atomic_replacement":False},
        "behavioral_nonempty":True,
        "reconnect_attempt_observed":True,
        "stable_reconnect_reset":False,
        "console_clean":False,
    }
    assert not operator_module.browser_websocket_outage_passes(
        outage_predicates)
    passing_predicates = copy.deepcopy(outage_predicates)
    passing_predicates["coherent_return_agrees"] = True
    passing_predicates["behavioral"]["atomic_replacement"] = True
    passing_predicates["stable_reconnect_reset"] = True
    passing_predicates["console_clean"] = True
    assert operator_module.browser_websocket_outage_passes(passing_predicates)
    assert "time.sleep(5.2)" not in operator_source
    assert "state.reconnect_attempt === 0" in operator_source

    with tempfile.TemporaryDirectory(prefix="graphx-phase8-completion-") as temp:
        root = Path(temp)
        unreachable = qualification_module.smoke(
            "http://127.0.0.1:1", root / "unreachable-corpus")
        assert unreachable["result"] == "FAIL"
        assert unreachable["production_endpoints_exercised"] is False
        manual_capture = root / "manual.png"
        manual_capture.write_bytes(b"genuine-test-fixture")
        manual_ids = sorted(module.MANUAL_IDS)
        manual = {
            "schema":"graphx.fhss.dashboard.phase8_manual_wcag.v1",
            "execution_mode":"human", "tester":"contract-test-human",
            "executed_at":"2026-07-21T20:00:00Z",
            "browser":{"name":"Firefox","version":"test-version"},
            "host":"test-host", "viewport":{"width_css_px":320,
                "height_css_px":800,"zoom_percent":200},
            "attestation":"I personally executed every listed manual WCAG check and recorded the observed evidence.",
            "axe_incomplete_resolutions":[],
            "items":[{"id":name,"result":"PASS",
                      "evidence_paths":[manual_capture.name],
                      "evidence_sha256":{manual_capture.name:digest(manual_capture)},
                      "notes":"test fixture"}
                     for name in manual_ids]}
        manual_path = root / "manual.json"
        write_json(manual_path, manual)

        browser = {"schema":"graphx.fhss.dashboard.phase8_browser_accessibility.v1",
            "browser_name":"firefox","browser_version":"152.0.5",
            "dashboard_url":"http://127.0.0.1:12345","unnamed_controls":[],
            "unsafe_inline_handlers":0,"contrast_ratios":{"all":7.0},
            "reflow_320_css_px":{"document_width":320,"body_width":320},"console":[]}
        browser_path = root / "browser.json"; write_json(browser_path, browser)
        installed_browser = copy.deepcopy(browser)
        installed_browser["dashboard_url"] = "http://127.0.0.1:12346"
        installed_browser_path = root / "installed-browser.json"
        write_json(installed_browser_path, installed_browser)
        assert module.AXE_CORE_VERSION == "4.12.1"
        assert module.AXE_TARBALL_SHA256 == "4341a01268b5ecbea826f3c7a7d1d69280a2cab3484c93e1bf4c9554460c6ca0"
        assert module.AXE_MIN_SHA256 == "66a8aaa95a8b044a7fd74a5435873bf04ff65a1ca75567c921b7509742085a14"
        axe_package = root / "axe-core-4.12.1.tgz"
        axe_package.write_bytes(b"test-only-axe-package")
        axe_min = root / "axe.min.js"; axe_min.write_bytes(b"test-only-axe-min")
        axe_license = root / "LICENSE"; axe_license.write_bytes(b"test-only-license")
        axe_third_party = root / "LICENSE-3RD-PARTY.txt"
        axe_third_party.write_bytes(b"test-only-third-party-license")
        module.AXE_TARBALL_SHA256 = digest(axe_package)
        module.AXE_TARBALL_SHA512 = hashlib.sha512(axe_package.read_bytes()).hexdigest()
        module.AXE_MIN_SHA256 = digest(axe_min)
        module.AXE_LICENSE_SHA256 = digest(axe_license)
        module.AXE_THIRD_PARTY_LICENSE_SHA256 = digest(axe_third_party)
        raw_axe = {"testEngine":{"name":"axe-core","version":"4.12.1"},
            "testRunner":{"name":"axe"},
            "testEnvironment":{"userAgent":"Mozilla/5.0 Firefox/152.0"},
            "timestamp":"2026-07-21T20:00:00Z",
            "url":"http://127.0.0.1:12345/",
            "toolOptions":{"runOnly":{"type":"tag","values":sorted(module.AXE_REQUIRED_TAGS)}},
            "passes":[],"incomplete":[],"violations":[],"inapplicable":[]}
        axe_report = root / "axe-report.json"; write_json(axe_report, raw_axe)
        axe = {"schema":"graphx.fhss.dashboard.phase8_accessibility_engine.v1",
            "tree":"source",
            "execution_id":"axe-source-00000000-0000-4000-8000-000000000001",
            "executed_at":"2026-07-21T20:00:00Z",
            "engine":"axe-core","engine_version":"4.12.1","browser":"Firefox/152.0.5",
            "dashboard_url":"http://127.0.0.1:12345","result":"PASS",
            "violations":{"counts":{"critical":0,"serious":0,"moderate":0,"minor":0},
                          "items":[]},
            "incomplete":{"counts":{"critical":0,"serious":0,"moderate":0,"minor":0},
                          "items":[]},
            "report_path":axe_report.name,"report_sha256":digest(axe_report),
            "provenance":{"package_name":"axe-core","package_version":"4.12.1",
                "tarball_url":module.AXE_TARBALL_URL,
                "registry_integrity":module.AXE_REGISTRY_INTEGRITY,
                "package_path":axe_package.name,"package_sha256":digest(axe_package),
                "package_sha512":hashlib.sha512(axe_package.read_bytes()).hexdigest(),
                "axe_min_path":axe_min.name,"axe_min_sha256":digest(axe_min),
                "license_path":axe_license.name,"license_sha256":digest(axe_license),
                "third_party_license_path":axe_third_party.name,
                "third_party_license_sha256":digest(axe_third_party)}}
        axe_path = root / "axe.json"; write_json(axe_path, axe)
        installed_raw_axe = copy.deepcopy(raw_axe)
        installed_raw_axe["timestamp"] = "2026-07-21T20:01:00Z"
        installed_raw_axe["url"] = "http://127.0.0.1:12346/"
        installed_axe_report = root / "installed-axe-report.json"
        write_json(installed_axe_report, installed_raw_axe)
        installed_axe = copy.deepcopy(axe)
        installed_axe.update({
            "tree":"installed",
            "execution_id":"axe-installed-00000000-0000-4000-8000-000000000002",
            "executed_at":"2026-07-21T20:01:00Z",
            "dashboard_url":"http://127.0.0.1:12346",
            "report_path":installed_axe_report.name,
            "report_sha256":digest(installed_axe_report)})
        installed_axe_path = root / "installed-axe.json"
        write_json(installed_axe_path, installed_axe)
        corpus_records = []
        for target in ("http_json","json_patch","websocket","sigmf_import"):
            corpus = root / f"{target}.bin"; corpus.write_bytes(target.encode())
            corpus_records.append({"target":target,"path":corpus.name,
                "sha256":digest(corpus),"bytes":corpus.stat().st_size,
                "coverage_signature":"200:test"})
        security = {"fuzz":{"result":"PASS","production_endpoints_exercised":True,
            "targets":["http_json","json_patch","websocket","sigmf_import"],
            "retained_regression_seeds":corpus_records},
            "security":{"result":"PASS","unsafe_dynamic_html_sinks":0}}
        security_path = root / "security.json"; write_json(security_path, security)
        soak = {"result":"PASS","supported_profile":"macOS",
            "probe_support":{"rss":True,"threads":True,"handles":True},
            "dimensions":{name:"PASS" for name in
                ("load","reconnect","lifecycle","shutdown","export_replay")}}
        soak_path = root / "soak.json"; write_json(soak_path, soak)

        dashboard = SOURCE / "examples/DSP/dashboard"
        entries = []
        for path in [*sorted((dashboard / "api/schemas").glob("*.schema.json")),
                     *sorted((dashboard / "operator/schemas").glob("*.schema.json"))]:
            entries.append({"path":str(path.relative_to(dashboard)),
                            "sha256":digest(path),
                            "id":json.loads(path.read_text()).get("$id")})
        inventory = {"schema":"graphx.fhss.dashboard.phase8_schema_inventory.v1",
            "count":len(entries),"metaschema_validation":"PASS","entries":entries}
        source_inventory = root / "source-inventory.json"
        installed_inventory = root / "installed-inventory.json"
        write_json(source_inventory, inventory); write_json(installed_inventory, inventory)
        package_entries = {f"asset-{index}":"a" * 64 for index in range(60)}
        for name, value in (
                ("fhss_dashboard_phase8_manual_operator_test.md", "e"),
                ("fhss_dashboard_phase8_security_support.md", "f"),
                ("fhss_dashboard_phase8_architecture_recommendation.md", "1")):
            package_entries[f"docs/{name}"] = value * 64
        package = {"schema":"graphx.fhss.dashboard.phase8_package_manifest.v1",
            "entry_count":len(package_entries),"entries":package_entries,
            "executables":{"graphx-dsp-fhss-demo":"c" * 64,
                           "graphx-dsp-fhss-iq-generator":"d" * 64}}
        source_package = root / "source-package.json"
        installed_package = root / "installed-package.json"
        write_json(source_package, package); write_json(installed_package, package)

        artifact_hashes = {"phase8_browser_accessibility":digest(browser_path),
            "phase8_fuzz_security":digest(security_path),"phase8_soak":digest(soak_path),
            "phase8_schema_inventory":digest(source_inventory),
            "phase8_package_manifest":digest(source_package)}
        for record in corpus_records:
            artifact_hashes[f"phase8_fuzz_seed:{record['target']}:{record['sha256']}"] = record["sha256"]
        qualification = {"dashboard_scope":"FHSS-specific",
            "input_evidence":"synthetic IQ only",
            "hwil_conducted_ota_evidence":"unavailable and deferred",
            "receiver_truth_isolation":"PASS","browser_automation":"PASS",
            "security_local_profile":"PASS","operator_workflow":"PASS",
            "production_rf_qualification":"NOT QUALIFIED"}
        operator = {"schema":"graphx.fhss.dashboard.operator_report.v1",
            "phase":8,"source_revision":"test","compiler":"clang C++26",
            "build_profile":"test","platform":"test-host","commands":[["ctest"]],
            "bound_address":"127.0.0.1","bound_port":12345,
            "dashboard_url":"http://127.0.0.1:12345","api_version":"v1",
            "synthetic_data_only":True,"hwil_available":False,
            "production_rf_qualified":False,"input_hashes":{"openapi":"a" * 64},
            "phase":8,"result":"PASS","evidence_status":"final_verified",
            "qualification":qualification,
            "checks":[{"name":"test","pass":True,"evidence":"fixture"}],
            "artifact_hashes":{"dashboard_index":"a" * 64,**artifact_hashes}}
        source_operator = root / "source-operator.json"; write_json(source_operator, operator)
        installed_operator = root / "installed-operator.json"
        installed_doc = copy.deepcopy(operator)
        installed_doc["bound_port"] = 12346
        installed_doc["dashboard_url"] = "http://127.0.0.1:12346"
        installed_doc["artifact_hashes"]["phase8_browser_accessibility"] = digest(
            installed_browser_path)
        installed_doc["artifact_hashes"]["phase8_schema_inventory"] = digest(installed_inventory)
        installed_doc["artifact_hashes"]["phase8_package_manifest"] = digest(installed_package)
        write_json(installed_operator, installed_doc)

        lane_paths = {}
        profiles = module.LANES
        commands = {"focused":["ctest","focused"],"full":["ctest","full"],
                    "sanitizer":["ctest","asan","ubsan"],
                    "concurrency":["ctest","tsan"]}
        for lane, profile in profiles.items():
            junit = root / f"{lane}.xml"
            junit.write_text('<testsuite tests="1"><testcase name="pass"/></testsuite>')
            manifest = {"schema":"graphx.fhss.dashboard.phase8_lane_evidence.v1",
                "lane":lane,"profile":profile,"command":commands[lane],"status":"PASS",
                "junit_path":junit.name,"junit_sha256":digest(junit),
                "counts":{"passed":1,"total":1,"failed":0,"skipped":0}}
            lane_path = root / f"{lane}.json"; write_json(lane_path, manifest)
            lane_paths[lane] = lane_path
        args = argparse.Namespace(source_operator_report=source_operator,
            installed_operator_report=installed_operator,browser_evidence=browser_path,
            security_fuzz_evidence=security_path,soak_evidence=soak_path,
            manual_wcag_evidence=manual_path,source_schema_inventory=source_inventory,
            installed_schema_inventory=installed_inventory,
            source_package_manifest=source_package,
            installed_package_manifest=installed_package,
            installed_browser_evidence=installed_browser_path,
            installed_security_fuzz_evidence=security_path,
            installed_soak_evidence=soak_path,
            source_accessibility_engine_evidence=axe_path,
            installed_accessibility_engine_evidence=installed_axe_path,
            focused_evidence=lane_paths["focused"],full_evidence=lane_paths["full"],
            sanitizer_evidence=lane_paths["sanitizer"],
            concurrency_evidence=lane_paths["concurrency"],signed_off_by="test")
        result = module.generate(args)
        assert result["browser_accessibility"] == "PASS"
        assert result["focused_tests"]["passed"] == 1
        assert len(result["evidence_hashes"]) >= 20

        unpinned_axe = copy.deepcopy(axe)
        unpinned_axe["engine_version"] = "4.12.0"
        write_json(axe_path, unpinned_axe)
        expect_rejected(args, "unpinned axe-core version")
        write_json(axe_path, axe)
        mismatched_counts = copy.deepcopy(axe)
        mismatched_counts["violations"]["counts"]["moderate"] = 1
        write_json(axe_path, mismatched_counts)
        expect_rejected(args, "caller axe counts disagree with raw output")
        write_json(axe_path, axe)
        malformed_raw = {"violations":[]}
        write_json(axe_report, malformed_raw)
        malformed_record = copy.deepcopy(axe)
        malformed_record["report_sha256"] = digest(axe_report)
        write_json(axe_path, malformed_record)
        expect_rejected(args, "malformed non-axe raw output")
        write_json(axe_report, raw_axe); write_json(axe_path, axe)
        moderate_raw = copy.deepcopy(raw_axe)
        moderate_raw["incomplete"] = [{
            "id":"landmark-unique", "impact":"moderate",
            "tags":["best-practice"], "description":"landmark review",
            "help":"Landmarks should be unique", "helpUrl":"https://example.test/axe",
            "nodes":[{"target":["main"], "failureSummary":"manual review"}]}]
        write_json(axe_report, moderate_raw)
        moderate_axe = copy.deepcopy(axe)
        moderate_axe["incomplete"] = {
            "counts":{"critical":0,"serious":0,"moderate":1,"minor":0},
            "items":[{"rule_id":"landmark-unique", "impact":"moderate",
                      "node_count":1, "targets":[["main"]]}]}
        moderate_axe["report_sha256"] = digest(axe_report)
        write_json(axe_path, moderate_axe)
        moderate_result = module.generate(args)
        assert moderate_result["accessibility_engine"]["source"][
            "incomplete"]["counts"]["moderate"] == 1
        assert moderate_result["accessibility_engine"]["source"][
            "incomplete"]["items"][0]["rule_id"] == "landmark-unique"
        write_json(axe_report, raw_axe); write_json(axe_path, axe)

        ignored_incomplete_raw = copy.deepcopy(raw_axe)
        ignored_incomplete_raw["incomplete"] = [{
            "id":"color-contrast", "impact":"serious",
            "tags":["wcag2aa"], "description":"contrast review",
            "help":"Elements must meet contrast", "helpUrl":"https://example.test/axe",
            "nodes":[{"target":["h1"], "failureSummary":"background unresolved"}]}]
        write_json(axe_report, ignored_incomplete_raw)
        ignored_incomplete = copy.deepcopy(axe)
        ignored_incomplete["report_sha256"] = digest(axe_report)
        write_json(axe_path, ignored_incomplete)
        expect_rejected(args, "serious axe incomplete cannot be ignored")

        relabelled_incomplete = copy.deepcopy(ignored_incomplete)
        relabelled_incomplete["incomplete"] = {
            "counts":{"critical":0,"serious":0,"moderate":1,"minor":0},
            "items":[{"rule_id":"color-contrast", "impact":"moderate",
                      "node_count":1, "targets":[["h1"]]}]}
        relabelled_incomplete["result"] = "PASS"
        write_json(axe_path, relabelled_incomplete)
        expect_rejected(args, "serious axe incomplete cannot be relabelled")

        derived_incomplete = copy.deepcopy(ignored_incomplete)
        derived_incomplete["incomplete"] = {
            "counts":{"critical":0,"serious":1,"moderate":0,"minor":0},
            "items":[{"rule_id":"color-contrast", "impact":"serious",
                      "node_count":1, "targets":[["h1"]]}]}
        derived_incomplete["result"] = "FAIL"
        write_json(axe_path, derived_incomplete)
        expect_rejected(args, "unresolved serious axe incomplete cannot pass completion")

        mismatched_resolution = copy.deepcopy(manual)
        mismatched_resolution["axe_incomplete_resolutions"] = [{
            "tree":"source", "rule_id":"color-contrast", "impact":"serious",
            "node_count":1, "targets":[["header"]],
            "manual_item_id":"contrast_non_color",
            "resolution_result":"PASS",
            "resolution_notes":"Human contrast review of the cited target."}]
        write_json(manual_path, mismatched_resolution)
        expect_rejected(args, "manual resolution target must exactly match raw axe")

        mismatched_count_resolution = copy.deepcopy(mismatched_resolution)
        mismatched_count_resolution["axe_incomplete_resolutions"][0][
            "targets"] = [["h1"]]
        mismatched_count_resolution["axe_incomplete_resolutions"][0][
            "node_count"] = 2
        write_json(manual_path, mismatched_count_resolution)
        expect_rejected(args, "manual resolution node count must exactly match raw axe")

        manual_resolution = copy.deepcopy(manual)
        manual_resolution["axe_incomplete_resolutions"] = [{
            "tree":"source", "rule_id":"color-contrast", "impact":"serious",
            "node_count":1, "targets":[["h1"]],
            "manual_item_id":"contrast_non_color",
            "resolution_result":"PASS",
            "resolution_notes":"Human verified the exact h1 contrast finding."}]
        missing_adjudication = copy.deepcopy(manual_resolution)
        missing_adjudication["axe_incomplete_resolutions"][0][
            "resolution_notes"] = ""
        write_json(manual_path, missing_adjudication)
        expect_rejected(args, "manual axe resolution requires human adjudication notes")
        write_json(manual_path, manual_resolution)
        duplicate_resolution = copy.deepcopy(manual_resolution)
        duplicate_resolution["axe_incomplete_resolutions"].append(
            copy.deepcopy(duplicate_resolution["axe_incomplete_resolutions"][0]))
        write_json(manual_path, duplicate_resolution)
        expect_rejected(args, "duplicate manual axe resolution")
        write_json(manual_path, manual_resolution)
        result = module.generate(args)
        assert result["browser_accessibility"] == "PASS"
        write_json(manual_path, manual)
        write_json(axe_report, raw_axe); write_json(axe_path, axe)

        mismatched_distribution = copy.deepcopy(axe)
        mismatched_distribution["provenance"]["axe_min_sha256"] = "0" * 64
        write_json(axe_path, mismatched_distribution)
        expect_rejected(args, "mismatched axe distribution provenance")
        write_json(axe_path, axe)

        wrong_target_raw = copy.deepcopy(raw_axe)
        wrong_target_raw["url"] = "http://127.0.0.1:12346/"
        write_json(axe_report, wrong_target_raw)
        wrong_target_axe = copy.deepcopy(axe)
        wrong_target_axe["dashboard_url"] = "http://127.0.0.1:12346"
        wrong_target_axe["report_sha256"] = digest(axe_report)
        write_json(axe_path, wrong_target_axe)
        expect_rejected(args, "source axe evidence targets installed URL")
        write_json(axe_report, raw_axe); write_json(axe_path, axe)
        wrong_browser_axe = copy.deepcopy(axe)
        wrong_browser_axe["browser"] = "Firefox/151.0.1"
        write_json(axe_path, wrong_browser_axe)
        expect_rejected(args, "source axe Firefox identity disagrees with browser evidence")
        write_json(axe_path, axe)
        reused_args = copy.copy(args)
        reused_args.installed_accessibility_engine_evidence = axe_path
        expect_rejected(reused_args, "source axe wrapper reused as installed evidence")
        same_time_raw = copy.deepcopy(installed_raw_axe)
        same_time_raw["timestamp"] = raw_axe["timestamp"]
        write_json(installed_axe_report, same_time_raw)
        same_time_axe = copy.deepcopy(installed_axe)
        same_time_axe["executed_at"] = raw_axe["timestamp"]
        same_time_axe["report_sha256"] = digest(installed_axe_report)
        write_json(installed_axe_path, same_time_axe)
        expect_rejected(args, "source and installed axe timestamps are reused")
        write_json(installed_axe_report, installed_raw_axe)
        write_json(installed_axe_path, installed_axe)

        missing = copy.copy(args); missing.manual_wcag_evidence = root / "missing.json"
        expect_rejected(missing, "missing manual record")
        missing_artifact = copy.deepcopy(manual)
        missing_artifact["items"][0]["evidence_paths"] = ["missing.png"]
        missing_artifact["items"][0]["evidence_sha256"] = {"missing.png":"0" * 64}
        write_json(manual_path, missing_artifact)
        expect_rejected(args, "missing manual evidence artifact")
        escaped_artifact = copy.deepcopy(manual)
        escaped_artifact["items"][0]["evidence_paths"] = ["../escaped.png"]
        escaped_artifact["items"][0]["evidence_sha256"] = {"../escaped.png":"0" * 64}
        write_json(manual_path, escaped_artifact)
        expect_rejected(args, "escaped manual evidence artifact")
        self_artifact = copy.deepcopy(manual)
        self_artifact["items"][0]["evidence_paths"] = [manual_path.name]
        self_artifact["items"][0]["evidence_sha256"] = {manual_path.name:"0" * 64}
        write_json(manual_path, self_artifact)
        expect_rejected(args, "manual record cannot cite itself as observed evidence")
        symlink_path = root / "manual-link.png"
        symlink_path.symlink_to(manual_capture.name)
        symlink_artifact = copy.deepcopy(manual)
        symlink_artifact["items"][0]["evidence_paths"] = [symlink_path.name]
        symlink_artifact["items"][0]["evidence_sha256"] = {
            symlink_path.name:digest(manual_capture)}
        write_json(manual_path, symlink_artifact)
        expect_rejected(args, "symlink manual evidence artifact")
        symlink_path.unlink()
        directory_path = root / "manual-directory"
        directory_path.mkdir()
        directory_artifact = copy.deepcopy(manual)
        directory_artifact["items"][0]["evidence_paths"] = [directory_path.name]
        directory_artifact["items"][0]["evidence_sha256"] = {
            directory_path.name:"0" * 64}
        write_json(manual_path, directory_artifact)
        expect_rejected(args, "non-regular manual evidence artifact")
        directory_path.rmdir()
        write_json(manual_path, manual)
        manual_capture.write_bytes(b"mutated-test-fixture")
        expect_rejected(args, "mutated manual evidence artifact")
        manual_capture.write_bytes(b"genuine-test-fixture")
        duplicate_artifact = copy.deepcopy(manual)
        duplicate_artifact["items"][0]["evidence_paths"] = [
            manual_capture.name, manual_capture.name]
        write_json(manual_path, duplicate_artifact)
        expect_rejected(args, "duplicate manual evidence path")
        write_json(manual_path, manual)
        malformed_operator = copy.deepcopy(operator); malformed_operator.pop("compiler")
        write_json(source_operator, malformed_operator)
        expect_rejected(args, "schema-invalid operator report")
        write_json(source_operator, operator)
        failed_manual = copy.deepcopy(manual); failed_manual["items"][0]["result"] = "FAIL"
        write_json(manual_path, failed_manual); expect_rejected(args, "failed manual item")
        write_json(manual_path, manual)
        focused_junit = root / "focused.xml"; focused_junit.write_text("<testsuite/>")
        expect_rejected(args, "forged JUnit hash")
        focused_junit.write_text('<testsuite tests="1"><testcase name="pass"/></testsuite>')
        focused_manifest = json.loads(lane_paths["focused"].read_text())
        not_executed_cases = (
            '<testcase name="ctest-disabled" status="disabled">'
            '<properties/><system-out/></testcase>',
            '<testcase name="status-notrun" status="notrun"/>',
            '<testcase name="status-not-run" status="not-run"/>',
            '<testcase name="result-disabled" result="disabled"/>',
            '<testcase name="result-notrun" result="notrun"/>',
            '<testcase name="result-not-run" result="not_run"/>',
            '<testcase name="explicit-skip"><skipped/></testcase>',
        )
        for index, testcase in enumerate(not_executed_cases):
            focused_junit.write_text(
                f'<testsuite tests="1">{testcase}</testsuite>')
            assert module.junit_counts(focused_junit) == {
                "passed":0, "total":1, "failed":0, "skipped":1}
            disabled_manifest = copy.deepcopy(focused_manifest)
            disabled_manifest["junit_sha256"] = digest(focused_junit)
            disabled_manifest["counts"] = {
                "passed":0, "total":1, "failed":0, "skipped":1}
            write_json(lane_paths["focused"], disabled_manifest)
            expect_rejected(args, f"not-executed JUnit testcase variant {index}")
        focused_junit.write_text(
            '<testsuite tests="1"><testcase name="pass"/></testsuite>')
        write_json(lane_paths["focused"], focused_manifest)
        bad_inventory = copy.deepcopy(inventory); bad_inventory["entries"].pop(); bad_inventory["count"] -= 1
        write_json(source_inventory, bad_inventory)
        bad_operator = copy.deepcopy(operator)
        bad_operator["artifact_hashes"]["phase8_schema_inventory"] = digest(source_inventory)
        write_json(source_operator, bad_operator)
        expect_rejected(args, "incomplete schema inventory")
        write_json(source_inventory, inventory); write_json(source_operator, operator)
        divergent_package = copy.deepcopy(package)
        divergent_package["entries"]["asset-0"] = "b" * 64
        write_json(installed_package, divergent_package)
        divergent_installed = copy.deepcopy(installed_doc)
        divergent_installed["artifact_hashes"]["phase8_package_manifest"] = digest(installed_package)
        write_json(installed_operator, divergent_installed)
        expect_rejected(args, "divergent installed package")
        write_json(installed_package, package); write_json(installed_operator, installed_doc)
        missing_doc_package = copy.deepcopy(package)
        missing_doc_package["entries"].pop(
            "docs/fhss_dashboard_phase8_manual_operator_test.md")
        missing_doc_package["entry_count"] -= 1
        write_json(installed_package, missing_doc_package)
        missing_doc_installed = copy.deepcopy(installed_doc)
        missing_doc_installed["artifact_hashes"]["phase8_package_manifest"] = digest(installed_package)
        write_json(installed_operator, missing_doc_installed)
        expect_rejected(args, "missing installed Phase8 document")
        divergent_doc_package = copy.deepcopy(package)
        divergent_doc_package["entries"][
            "docs/fhss_dashboard_phase8_security_support.md"] = "2" * 64
        write_json(installed_package, divergent_doc_package)
        divergent_doc_installed = copy.deepcopy(installed_doc)
        divergent_doc_installed["artifact_hashes"]["phase8_package_manifest"] = digest(installed_package)
        write_json(installed_operator, divergent_doc_installed)
        expect_rejected(args, "divergent installed Phase8 document")
        write_json(installed_package, package); write_json(installed_operator, installed_doc)
        divergent_browser_path = root / "divergent-installed-browser.json"
        divergent_browser = copy.deepcopy(installed_browser)
        divergent_browser["browser_version"] = "different-installed-version"
        write_json(divergent_browser_path, divergent_browser)
        divergent_installed = copy.deepcopy(installed_doc)
        divergent_installed["artifact_hashes"]["phase8_browser_accessibility"] = digest(divergent_browser_path)
        write_json(installed_operator, divergent_installed)
        installed_browser_args = copy.copy(args)
        installed_browser_args.installed_browser_evidence = divergent_browser_path
        expect_rejected(installed_browser_args, "divergent installed browser evidence")
        write_json(installed_operator, installed_doc)
        failed_security = copy.deepcopy(security); failed_security["fuzz"]["result"] = "FAIL"
        write_json(security_path, failed_security)
        bad_operator = copy.deepcopy(operator)
        bad_operator["artifact_hashes"]["phase8_fuzz_security"] = digest(security_path)
        write_json(source_operator, bad_operator)
        expect_rejected(args, "failing fuzz evidence")
    print("Phase 8 completion evidence gate: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
