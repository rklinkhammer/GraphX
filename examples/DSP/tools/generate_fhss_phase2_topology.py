#!/usr/bin/env python3
"""Generate the truth-free Phase 2 binary-IQ receiver topology."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "libdsp/config/fhss_cpsm_binary_iq_500msps.json"
OUTPUT = ROOT / "libdsp/config/fhss_phase2_binary_iq_receiver.json"


def main() -> int:
    graph = json.loads(SOURCE.read_text())
    graph["name"] = "fhss_phase2_binary_iq_receiver"
    graph["fhss_graph_role"] = "phase2_truth_free_engineering_characterization"
    graph["description"] = (
        "Provisional Phase 2 receiver: binary IQ, FIR production candidate, "
        "and evidence-driven acquisition; no schedule, message truth, or "
        "transmitted-frequency hints."
    )
    graph["presentation"] = {
        "groups": [
            {
                "id": "detector-bank",
                "label": "Detector bank",
                "members": [f"detector_{index}" for index in range(64)],
                "layout": "grid",
                "collapsed_by_default": True,
            }
        ]
    }
    for node in graph["nodes"]:
        if node["id"] == "channelizer":
            node["type"] = "FHSSProductionCandidateChannelizerNode"
            node["node_config"] = {
                "receiver_frequency_indices": list(range(64)),
                "channel_ids": list(range(64)),
                "iq_offsets": [
                    {"index": index,
                     "iq_offset_frequency_hz": -236_250_000.0 + 7_500_000.0 * index}
                    for index in range(64)
                ],
                "occupied_bandwidth_hz": 5_000_000.0,
                "max_abs_cfo_hz": 1_000.0,
                "decimation_factor": 10,
                "fir_tap_count": 241,
                "passband_edge_hz": 2_500_000.0,
                "cutoff_frequency_hz": 4_000_000.0,
                "guarded_nyquist_margin_hz": 5_000_000.0,
                "max_input_samples": 4_194_304,
            }
        elif node["id"].startswith("detector_"):
            detector_id = int(node["id"].split("_")[1])
            node["type"] = "FHSSAcquisitionPulseDetectorNode"
            node["node_config"] = {
                "detector_id": detector_id,
                "noise_power_quantile": 0.2,
                "threshold_above_noise_linear": 8.0,
                "release_threshold_ratio": 0.5,
                "min_absolute_power_linear": 5.0e-2,
                "min_symbol_coherence": 0.75,
                "smoothing_window_channel_samples": 8,
                "min_pulse_input_samples": 2_400,
                "max_pulse_input_samples": 4_000,
                "bridge_gap_input_samples": 80,
                "duplicate_tolerance_input_samples": 160,
                "max_buffered_channel_samples": 419_431,
                "nominal_bandwidth_hz": 5_000_000.0,
            }
    forbidden = (
        "messages",
        "FHSSSyntheticIqSourceNode",
        "transmitted_active_frequency_indices",
        "transmitted_pulse_frequency_indices",
        "transmit_start_sample",
        "generator_truth",
    )
    rendered = json.dumps(graph, indent=2) + "\n"
    for term in forbidden:
        if term in rendered:
            raise SystemExit(f"generated Phase 2 topology contains forbidden term: {term}")
    OUTPUT.write_text(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
