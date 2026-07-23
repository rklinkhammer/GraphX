#!/usr/bin/env python3
"""Focused proportional tests for the Dashboard Phase 0 baseline."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

SOURCE_ROOT = Path(os.environ.get(
    "GRAPHX_SOURCE_ROOT", Path(__file__).resolve().parents[3])).resolve()
DASHBOARD_ROOT = SOURCE_ROOT / "examples/DSP/dashboard"
OPERATOR_ROOT = DASHBOARD_ROOT / "operator"
sys.path.insert(0, str(OPERATOR_ROOT))

from frontend_asset_inventory import (  # noqa: E402
    InventoryError, check_self_hosted_assets, compare_frontend_inventories,
    inventory_frontend)
from phase0_baseline import (  # noqa: E402
    topology_identity_document, topology_identity_from_path, verify)


class Phase0InventoryTest(unittest.TestCase):
    def make_root(self) -> Path:
        root = Path(tempfile.mkdtemp(prefix="graphx-phase0-assets-"))
        self.addCleanup(shutil.rmtree, root, True)
        (root / "index.html").write_text("<main>GraphX</main>", encoding="utf-8")
        return root

    def test_current_frontend_is_recursive_bounded_and_self_hosted(self) -> None:
        inventory = inventory_frontend(DASHBOARD_ROOT / "dist")
        paths = [str(item["path"]) for item in inventory["entries"]]
        self.assertIn("index.html", paths)
        self.assertEqual(paths, sorted(paths))
        self.assertLessEqual(inventory["total_bytes"], inventory["max_total_bytes"])
        self.assertEqual(check_self_hosted_assets(
            DASHBOARD_ROOT / "dist", inventory)["result"], "PASS")

    def test_nested_assets_are_deterministic_and_install_must_match(self) -> None:
        source = self.make_root()
        (source / "assets").mkdir()
        (source / "assets/app.js").write_text("export {};", encoding="utf-8")
        (source / "assets/app.css").write_text("body{}", encoding="utf-8")
        installed = self.make_root()
        shutil.copytree(source / "assets", installed / "assets")
        first = inventory_frontend(source)
        compare_frontend_inventories(first, inventory_frontend(installed))
        self.assertEqual([item["path"] for item in first["entries"]],
                         ["assets/app.css", "assets/app.js", "index.html"])
        (installed / "assets/app.js").write_text("changed", encoding="utf-8")
        with self.assertRaisesRegex(InventoryError, "diverge"):
            compare_frontend_inventories(first, inventory_frontend(installed))

    def test_straightforward_invalid_assets_fail(self) -> None:
        missing = self.make_root()
        (missing / "index.html").unlink()
        with self.assertRaisesRegex(InventoryError, "entrypoint"):
            inventory_frontend(missing)
        oversize = self.make_root()
        (oversize / "app.js").write_bytes(b"12345")
        with self.assertRaisesRegex(InventoryError, "exceeds"):
            inventory_frontend(oversize, max_file_bytes=4)
        linked = self.make_root()
        (linked / "real.js").write_text("x", encoding="utf-8")
        (linked / "link.js").symlink_to(linked / "real.js")
        with self.assertRaisesRegex(InventoryError, "regular file"):
            inventory_frontend(linked)
        source_map = self.make_root()
        (source_map / "app.js.map").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(InventoryError, "source map"):
            inventory_frontend(source_map)


class Phase0BaselineTest(unittest.TestCase):
    def test_baseline_is_current_schema_valid_and_semantic(self) -> None:
        verify(SOURCE_ROOT)
        schema = json.loads((OPERATOR_ROOT /
            "schemas/phase0-baseline.schema.json").read_text())
        document = json.loads((DASHBOARD_ROOT /
            "baseline/phase0-baseline.json").read_text())
        try:
            import jsonschema
        except ImportError:
            self.assertEqual(document["schema"],
                             "graphx.fhss.dashboard.phase0_baseline.v1")
        else:
            jsonschema.Draft202012Validator.check_schema(schema)
            jsonschema.Draft202012Validator(schema).validate(document)
        contracts = document["authoritative_contracts"]
        self.assertEqual(contracts["application_namespace"], "/api/v1/fhss")
        self.assertEqual(contracts["root_entrypoint"], "/")
        self.assertEqual(set(document["feature_expectations"]), {
            "configuration", "runtime_lifecycle", "message_jobs",
            "observations", "comparison", "spectrum", "investigations",
            "event_recovery", "shutdown"})
        self.assertFalse(document["qualification"]["hwil_available"])
        self.assertFalse(document["qualification"]["production_rf_qualified"])

    def test_identity_covers_all_nodes_edges_ports_and_reconstruction(self) -> None:
        graph_path = SOURCE_ROOT / "libdsp/config/fhss_phase2_binary_iq_receiver.json"
        identity = topology_identity_from_path(graph_path)
        self.assertEqual(identity["node_count"], 75)
        self.assertEqual(identity["edge_count"], 137)
        self.assertFalse(identity["array_positions_are_identities"])
        self.assertFalse(identity["runtime_overlays_enabled"])
        self.assertEqual(set(identity["deferred_until_phase3"]), {
            "runtime_node_to_configuration", "metric_to_configuration",
            "diagnostic_to_configuration"})
        edge_ids = {edge["configuration_edge_id"] for edge in identity["edges"]}
        self.assertEqual(len(edge_ids), 137)
        self.assertIn("detector_0:0->merge:1", edge_ids)
        self.assertIn("detector_63:0->merge:64", edge_ids)
        graph = json.loads(graph_path.read_text())
        digest = hashlib.sha256(graph_path.read_bytes()).hexdigest()
        reconstructed = {"nodes":list(reversed(graph["nodes"])),
                         "edges":list(reversed(graph["edges"]))}
        self.assertEqual(identity, topology_identity_document(reconstructed, digest))

    def test_host_toolchain_range_and_locked_dependencies(self) -> None:
        frontend = DASHBOARD_ROOT / "frontend"
        package = json.loads((frontend / "package.json").read_text())
        lock = json.loads((frontend / "package-lock.json").read_text())
        policy = json.loads((frontend / "toolchain.json").read_text())
        self.assertEqual(lock["lockfileVersion"], 3)
        self.assertEqual(package["engines"]["node"], ">=24.0.0 <27.0.0")
        self.assertEqual(package["engines"]["npm"], ">=11.0.0 <12.0.0")
        self.assertTrue(policy["host_toolchain_required"])
        self.assertFalse(policy["automatic_toolchain_provisioning"])
        self.assertFalse(policy["temporary_toolchain_paths_allowed"])
        self.assertEqual(policy["cmake_resolution"], "host_PATH")
        self.assertTrue(policy["application_present"])
        self.assertEqual(policy["application_phase"], 1)
        for section in ("dependencies", "devDependencies"):
            for name, version in package[section].items():
                self.assertEqual(lock["packages"][""][section][name], version)
        registry = [value for key, value in lock["packages"].items()
                    if key and value.get("resolved")]
        self.assertTrue(all(value.get("integrity") for value in registry))
        self.assertTrue(all(value.get("license") for value in registry))

    def test_one_implementation_policy_has_no_runtime_selector_or_api_v2(self) -> None:
        implementation_paths = [
            SOURCE_ROOT / "examples/DSP/src/fhss_demo.cpp",
            SOURCE_ROOT / "examples/DSP/dashboard/FHSSDashboardApi.cpp",
            SOURCE_ROOT / "libgraph/src/dashboard/EmbeddedDashboardServer.cpp"]
        implementation = "\n".join(path.read_text(encoding="utf-8")
                                   for path in implementation_paths)
        for forbidden in ("/api/v2", '"/legacy', '"/v2',
                          "--dashboard-ui", "--dashboard-version"):
            self.assertNotIn(forbidden, implementation)


if __name__ == "__main__":
    unittest.main()
