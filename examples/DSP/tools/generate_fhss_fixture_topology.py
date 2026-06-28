#!/usr/bin/env python3
"""Generate the ordinary expanded GraphX JSON for the canonical FHSS fixture."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


FREQUENCY_COUNT = 64
ACTIVE_FREQUENCIES = [24, 28, 32, 36]
PREAMBLE_WORDS = [2863311530, 2004318071, 303174162, 1650614882]
BODY_PULSES = [
    {"frequency_index": 24, "value": 16909060, "role": "body"},
    {"frequency_index": 28, "value": 2779096538, "role": "body"},
]


def _preamble_pulses(*, word_key: str) -> list[dict[str, Any]]:
    return [
        {"frequency_index": frequency, word_key: word, **({"role": "preamble"} if word_key == "value" else {})}
        for _ in range(4)
        for frequency, word in zip(ACTIVE_FREQUENCIES, PREAMBLE_WORDS, strict=True)
    ]


def _messages() -> list[dict[str, Any]]:
    return [
        {
            "message_id": message_id,
            "transmit_start_sample": (message_id - 1) * 117000,
            "pulses": _preamble_pulses(word_key="value") + BODY_PULSES,
        }
        for message_id in range(1, 5)
    ]


def _fixture_config() -> dict[str, Any]:
    return {
        "active_frequency_indices": ACTIVE_FREQUENCIES,
        "iq_center_frequency_hz": 1240000000.0,
        "messages": _messages(),
        "idle_mode": "zero",
        "idle_duration_samples": 0,
        "occupied_bandwidth_hz": 5000000.0,
        "max_abs_cfo_hz": 1000.0,
        "enable_noise": False,
        "enable_doppler": False,
        "enable_multipath": False,
        "allow_overlap": False,
    }


def build_topology() -> dict[str, Any]:
    """Return a fresh, fully expanded GraphX configuration."""
    frequency_indices = list(range(FREQUENCY_COUNT))
    fixture_config = _fixture_config()
    nodes: list[dict[str, Any]] = [
        {"id": "source", "type": "FHSSSyntheticIqSourceNode", "node_config": fixture_config},
        {
            "id": "downconverter",
            "type": "FHSSDownconverterNode",
            "node_config": {
                "input_iq_center_frequency_hz": 1240000000.0,
                "input_reference_frequency_hz": 1240000000.0,
                "output_iq_center_frequency_hz": 1240000000.0,
                "output_reference_frequency_hz": 1240000000.0,
                "translation_frequency_hz": 0.0,
                "passthrough": True,
                "sample_rate_hz": 500000000.0,
            },
        },
        {
            "id": "channelizer",
            "type": "FHSSFixtureFrequencyChannelizerNode",
            "node_config": {
                "iq_center_frequency_hz": 1240000000.0,
                "receiver_frequency_indices": frequency_indices,
                "channel_ids": frequency_indices,
                "transmitted_active_frequency_indices": ACTIVE_FREQUENCIES,
                "transmitted_pulse_frequency_indices": ACTIVE_FREQUENCIES,
                "channel_sample_rate_hz": 500000000.0,
                "decimation_factor": 1,
                "filter_group_delay_input_samples": 0,
                "occupied_bandwidth_hz": 5000000.0,
                "max_abs_cfo_hz": 1000.0,
            },
        },
    ]
    nodes.extend(
        {
            "id": f"detector_{index}",
            "type": "PerChannelPulseDetectorNode",
            "node_config": {
                "detector_id": index,
                "packet_sequence": 14,
                "min_power_linear": 1e-12,
                "min_symbol_coherence": 0.5,
                "noise_floor_db": -120.0,
                "nominal_bandwidth_hz": 5000000.0,
                "max_pulse_input_samples": 3200,
            },
        }
        for index in frequency_indices
    )
    preamble = _preamble_pulses(word_key="word_value")
    nodes.extend(
        [
            {"id": "merge", "type": "FHSSPulseMergeNode"},
            {"id": "candidate", "type": "FHSSPulseCandidateNode"},
            {"id": "branch_metric", "type": "CPSMBranchMetricNode"},
            {"id": "viterbi", "type": "CPSMViterbiDecoderNode"},
            {"id": "word_decoder", "type": "FHSSPulseWordDecoderNode"},
            {
                "id": "preamble",
                "type": "FHSSPreambleDetectorNode",
                "node_config": {
                    "active_frequency_indices": ACTIVE_FREQUENCIES,
                    "preamble_pulses": preamble,
                },
            },
            {
                "id": "assembler",
                "type": "FHSSMessageAssemblerNode",
                "node_config": {**fixture_config, "preamble_pulses": preamble},
            },
            {"id": "sink", "type": "FHSSMessageSinkNode"},
        ]
    )

    edges = [
        {"source_node_id": "source", "source_port": 0, "target_node_id": "downconverter", "target_port": 0},
        {"source_node_id": "downconverter", "source_port": 0, "target_node_id": "channelizer", "target_port": 0},
    ]
    edges.extend(
        {
            "source_node_id": "channelizer",
            "source_port": index,
            "target_node_id": f"detector_{index}",
            "target_port": 0,
        }
        for index in frequency_indices
    )
    edges.extend(
        {
            "source_node_id": f"detector_{index}",
            "source_port": 0,
            "target_node_id": "merge",
            "target_port": index + 1,
        }
        for index in frequency_indices
    )
    edges.extend(
        {"source_node_id": source, "source_port": source_port, "target_node_id": target, "target_port": 0}
        for source, source_port, target in [
            ("merge", 1, "candidate"),
            ("candidate", 0, "branch_metric"),
            ("branch_metric", 0, "viterbi"),
            ("viterbi", 0, "word_decoder"),
            ("word_decoder", 0, "preamble"),
            ("preamble", 0, "assembler"),
            ("assembler", 0, "sink"),
        ]
    )
    return {
        "name": "fhss_cpsm_channelized_fixture_500msps",
        "fhss_graph_role": "canonical_channelized_fixture",
        "canonical_fhss_graph": True,
        "reference_only": False,
        "description": (
            "Canonical deterministic FHSS fixture graph using downconverter, "
            "64-port fixture frequency channelizer, and one per-channel detector per frequency."
        ),
        "execution_backend": "auto",
        "backend_fallback_policy": "strict",
        "resolver_diagnostics": True,
        "nodes": nodes,
        "edges": edges,
    }


def render_topology() -> str:
    return json.dumps(build_topology(), indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="generated GraphX JSON path")
    parser.add_argument("--check", action="store_true", help="fail if output is not current")
    args = parser.parse_args()
    rendered = render_topology()
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != rendered:
            parser.error(f"generated fixture is stale: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
