#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np
from scipy.io import savemat


SCRIPT_PATH = Path(__file__).with_name("generate_gotcha_subdata_sidecars.py")


class GenerateGotchaSubdataSidecarsTest(unittest.TestCase):
    def test_generated_sidecar_contains_required_raw_and_normalized_fields(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mat_path = root / "subData01.mat"
            phdata = np.asarray(
                [
                    [1.0 + 2.0j, 3.0 + 4.0j],
                    [5.0 + 6.0j, 7.0 + 8.0j],
                    [9.0 + 10.0j, 11.0 + 12.0j],
                ],
                dtype=np.complex64,
            )
            savemat(
                mat_path,
                {
                    "subData": {
                        "phdata": phdata,
                        "K": np.asarray([[3]]),
                        "deltaF": np.asarray([[2.0]]),
                        "minF": np.asarray([[10.0]]),
                        "AntX": np.asarray([[100.0, 101.0]]),
                        "AntY": np.asarray([[200.0, 201.0]]),
                        "AntZ": np.asarray([[300.0, 301.0]]),
                        "R0": np.asarray([[400.0, 401.0]]),
                        "Np": np.asarray([[10.0, 11.0]]),
                    },
                },
            )

            result = subprocess.run(
                [
                    "python3",
                    str(SCRIPT_PATH),
                    "--input-dir",
                    str(root),
                    "--pulse-index",
                    "1",
                    "--overwrite",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            summary = json.loads(result.stdout)
            self.assertEqual(summary["mat_files"], 1)
            self.assertEqual(summary["sidecars_written"], 1)

            sidecar = json.loads(Path(str(mat_path) + ".json").read_text(encoding="utf-8"))
            for field in ("Np", "K", "deltaF", "minF", "AntX", "AntY", "AntZ", "R0", "phdata"):
                self.assertIn(field, sidecar)
            for field in (
                "carrier_hz",
                "bandwidth_hz",
                "sample_rate_hz",
                "frequency_axis_hz",
                "platform_position_m",
                "platform_velocity_mps",
                "pulse_time_seconds",
                "range_sample_start",
                "iq_samples",
                "source_field_names",
            ):
                self.assertIn(field, sidecar)

            self.assertEqual(sidecar["Np"], 1)
            self.assertEqual(sidecar["K"], 3)
            self.assertEqual(sidecar["deltaF"], 2.0)
            self.assertEqual(sidecar["minF"], 10.0)
            self.assertEqual(sidecar["AntX"], 101.0)
            self.assertEqual(sidecar["AntY"], 201.0)
            self.assertEqual(sidecar["AntZ"], 301.0)
            self.assertEqual(sidecar["R0"], 401.0)
            self.assertEqual(sidecar["pulse_time_seconds"], 11.0)
            self.assertEqual(sidecar["iq_samples"], [
                {"real": 3.0, "imag": 4.0},
                {"real": 7.0, "imag": 8.0},
                {"real": 11.0, "imag": 12.0},
            ])

            manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema"], "graphx.gotcha.input_manifest.v1")
            self.assertEqual(manifest["files"], [{"path": "subData01.mat"}])
            self.assertIn("subData01.mat", (root / "checksums.sha256").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
