#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import unittest


ROOT = Path(os.environ["GRAPHX_SOURCE_ROOT"])
TOOL = ROOT / "examples/DSP/tools/generate_fhss_fixture_topology.py"
CANONICAL = ROOT / "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"

spec = importlib.util.spec_from_file_location("fhss_fixture_generator", TOOL)
assert spec is not None and spec.loader is not None
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class FHSSFixtureTopologyGeneratorTest(unittest.TestCase):
    def test_generation_is_deterministic_and_canonical_is_current(self) -> None:
        first = generator.render_topology()
        second = generator.render_topology()
        self.assertEqual(first, second)
        self.assertEqual(CANONICAL.read_text(encoding="utf-8"), first)

    def test_expansion_has_64_distinct_ports_detectors_and_edges(self) -> None:
        graph = json.loads(generator.render_topology())
        detectors = [node for node in graph["nodes"] if node["type"] == "PerChannelPulseDetectorNode"]
        fanout = [edge for edge in graph["edges"] if edge["source_node_id"] == "channelizer"]
        fanin = [edge for edge in graph["edges"] if edge["target_node_id"] == "merge" and edge["source_node_id"].startswith("detector_")]

        self.assertEqual({node["id"] for node in detectors}, {f"detector_{i}" for i in range(64)})
        self.assertEqual({edge["source_port"] for edge in fanout}, set(range(64)))
        self.assertEqual({edge["target_node_id"] for edge in fanout}, {f"detector_{i}" for i in range(64)})
        self.assertEqual({edge["source_node_id"] for edge in fanin}, {f"detector_{i}" for i in range(64)})
        self.assertEqual({edge["target_port"] for edge in fanin}, set(range(1, 65)))
        self.assertEqual(len(fanout), 64)
        self.assertEqual(len(fanin), 64)

    def test_generated_graph_uses_fixture_channelizer_name_only(self) -> None:
        types = {node["type"] for node in generator.build_topology()["nodes"]}
        self.assertIn("FHSSFixtureFrequencyChannelizerNode", types)
        self.assertNotIn("ChannelizerNode", types)

    def test_generated_graph_has_no_aggregate_or_adapter_contract(self) -> None:
        rendered = generator.render_topology()
        graph = json.loads(rendered)

        for forbidden in (
            "FHSSChannelizedIqStreamPacket",
            "FHSSChannelizedIqStreamToken",
            "StaticNodeAdapter",
            "graph adaptor",
        ):
            self.assertNotIn(forbidden, rendered)

        fanout = [edge for edge in graph["edges"] if edge["source_node_id"] == "channelizer"]
        self.assertEqual(len(fanout), 64)

        incoming_by_detector: dict[str, list[dict[str, int | str]]] = {}
        for edge in fanout:
            incoming_by_detector.setdefault(edge["target_node_id"], []).append(edge)

        self.assertEqual(set(incoming_by_detector.keys()), {f"detector_{i}" for i in range(64)})
        self.assertTrue(all(len(edges) == 1 for edges in incoming_by_detector.values()))


if __name__ == "__main__":
    unittest.main()
