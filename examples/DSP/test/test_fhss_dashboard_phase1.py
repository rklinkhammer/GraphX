#!/usr/bin/env python3
"""Focused semantic checks for the single compiled Phase 1 dashboard."""

from __future__ import annotations

import json
import os
import re
import sys
import unittest
from pathlib import Path

SOURCE_ROOT = Path(os.environ.get(
    "GRAPHX_SOURCE_ROOT", Path(__file__).resolve().parents[3])).resolve()
DASHBOARD = SOURCE_ROOT / "examples/DSP/dashboard"
FRONTEND = DASHBOARD / "frontend"
sys.path.insert(0, str(DASHBOARD / "operator"))

from frontend_asset_inventory import (  # noqa: E402
    check_self_hosted_assets, inventory_frontend)


class Phase1DashboardTest(unittest.TestCase):
    def test_single_compiled_inventory_is_bounded_and_self_hosted(self) -> None:
        inventory = inventory_frontend(DASHBOARD / "dist")
        paths = [str(entry["path"]) for entry in inventory["entries"]]
        self.assertEqual([path for path in paths if path.endswith(".html")],
                         ["index.html"])
        self.assertTrue(any(path.endswith(".js") for path in paths))
        self.assertTrue(any(path.endswith(".css") for path in paths))
        self.assertFalse(any(path.endswith(".map") for path in paths))
        self.assertLess(inventory["total_bytes"], 16 * 1024 * 1024)
        self.assertTrue(all(int(entry["bytes"]) < 4 * 1024 * 1024
                            for entry in inventory["entries"]))
        self.assertEqual(check_self_hosted_assets(
            DASHBOARD / "dist", inventory)["result"], "PASS")
        self.assertFalse((DASHBOARD / "index.html").exists())
        self.assertFalse((DASHBOARD / "fhss_transport_state.js").exists())

    def test_frontend_architecture_preserves_phase_boundaries(self) -> None:
        source = "\n".join(path.read_text(encoding="utf-8")
                            for path in (FRONTEND / "src").glob("*.*"))
        self.assertIn("/api/v1/fhss", source)
        for forbidden in ("/api/v2", '"/legacy', '"/v2'):
            self.assertNotIn(forbidden, source)
        self.assertIn("nodesConnectable={false}", source)
        self.assertIn("edgesReconnectable={false}", source)
        self.assertIn("deleteKeyCode={null}", source)
        self.assertNotIn("animated: true", source)
        self.assertIn("Runtime metric and diagnostic identity overlays are intentionally deferred", source)
        self.assertIn("Semantic topology", source)
        self.assertIn("ArrowDown", source)
        self.assertIn("min-width: 320px", source)

    def test_build_is_pinned_and_has_no_production_source_maps(self) -> None:
        package = json.loads((FRONTEND / "package.json").read_text())
        lock = json.loads((FRONTEND / "package-lock.json").read_text())
        config = (FRONTEND / "vite.config.ts").read_text()
        self.assertEqual(package["packageManager"], "npm@11.5.2")
        self.assertEqual(lock["lockfileVersion"], 3)
        self.assertEqual(package["version"], "1.0.0-phase1")
        self.assertRegex(config, re.compile(r"sourcemap:\s*false"))
        self.assertEqual(package["scripts"]["build"], "vite build")

    def test_operator_guide_exposes_external_manual_checks(self) -> None:
        guide = (SOURCE_ROOT / "docs/dsp/fhss_dashboard_phase1_operator_test.md").read_text()
        for expectation in ("75 nodes", "137 exact-port edges", "320 CSS-pixel",
                            "/api/v2", "keyboard", "synthetic IQ only"):
            self.assertIn(expectation, guide)
        self.assertIn("Do not mark keyboard, focus, or reflow review", guide)


if __name__ == "__main__":
    unittest.main()
