#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.io import loadmat


def _to_float(value, default=0.0):
    try:
        if isinstance(value, np.ndarray):
            if value.size == 0:
                return float(default)
            return float(np.asarray(value).reshape(-1)[0])
        return float(value)
    except Exception:
        return float(default)


def _to_array(value) -> np.ndarray:
    if isinstance(value, np.ndarray):
        return value
    return np.asarray([value])


def _index_or_last(values: np.ndarray, index: int, default: float = 0.0) -> float:
    if values.size == 0:
        return default
    clamped = min(max(index, 0), values.size - 1)
    return float(values.reshape(-1)[clamped])


def _extract_struct(path: Path):
    mat = loadmat(path, squeeze_me=True, struct_as_record=False)
    if "subData" not in mat:
        raise RuntimeError(f"missing 'subData' struct in {path}")
    return mat["subData"]


def _build_sidecar(path: Path, pulse_index: int) -> dict:
    s = _extract_struct(path)

    phdata = np.asarray(getattr(s, "phdata"))
    if phdata.ndim == 1:
        pulse = phdata
        used_pulse_index = 0
    elif phdata.ndim == 2:
        used_pulse_index = min(max(pulse_index, 0), phdata.shape[1] - 1)
        pulse = phdata[:, used_pulse_index]
    else:
        raise RuntimeError(f"unsupported phdata shape in {path}: {phdata.shape}")

    pulse = np.asarray(pulse).reshape(-1)
    if not np.iscomplexobj(pulse):
        raise RuntimeError(f"phdata is not complex in {path}")

    k = int(_to_float(getattr(s, "K", 0), default=0.0))
    delta_f = _to_float(getattr(s, "deltaF", 0.0), default=0.0)
    min_f = _to_float(getattr(s, "minF", 0.0), default=0.0)
    bandwidth = float(max(delta_f * max(k, 1), 1.0))
    sample_rate = bandwidth
    carrier = min_f + (bandwidth * 0.5)

    ant_x = _to_array(getattr(s, "AntX", np.array([0.0])))
    ant_y = _to_array(getattr(s, "AntY", np.array([0.0])))
    ant_z = _to_array(getattr(s, "AntZ", np.array([0.0])))
    npulses = _to_array(getattr(s, "Np", np.array([used_pulse_index])))

    position = [
        _index_or_last(ant_x, used_pulse_index, 0.0),
        _index_or_last(ant_y, used_pulse_index, 0.0),
        _index_or_last(ant_z, used_pulse_index, 0.0),
    ]

    iq_samples = [
        {"real": float(np.real(v)), "imag": float(np.imag(v))}
        for v in pulse.astype(np.complex64)
    ]

    return {
        "carrier_hz": carrier,
        "bandwidth_hz": bandwidth,
        "sample_rate_hz": sample_rate,
        "frequency_axis_hz": [
            min_f,
            min_f + (delta_f * max(len(iq_samples) - 1, 0)),
        ],
        "platform_position_m": position,
        "platform_velocity_mps": [0.0, 0.0, 0.0],
        "pulse_time_seconds": _index_or_last(npulses, used_pulse_index, float(used_pulse_index)),
        "range_sample_start": 0,
        "iq_samples": iq_samples,
        "source_field_names": {
            "iq_samples": "subData.phdata",
            "carrier_hz": "subData.minF + subData.deltaF * subData.K / 2",
            "bandwidth_hz": "subData.deltaF * subData.K",
            "platform_position_m": "subData.AntX/subData.AntY/subData.AntZ",
            "pulse_time_seconds": "subData.Np",
        },
    }


def _write_manifest(dataset_dir: Path, mat_files: list[Path]) -> Path:
    manifest = {
        "schema": "graphx.gotcha.input_manifest.v1",
        "files": [{"path": f.name} for f in mat_files],
    }
    path = dataset_dir / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


def _write_checksums(dataset_dir: Path, mat_files: list[Path]) -> Path:
    lines = []
    for f in mat_files:
        digest = hashlib.sha256(f.read_bytes()).hexdigest()
        lines.append(f"{digest} {f.name}")
    path = dataset_dir / "checksums.sha256"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate GOTCHA sidecar JSON + manifest/checksums")
    parser.add_argument("--input-dir", required=True, help="Directory containing GOTCHA subData*.mat files")
    parser.add_argument("--pulse-index", type=int, default=0, help="Pulse index to extract per MAT file")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing sidecar JSON files")
    args = parser.parse_args()

    dataset_dir = Path(args.input_dir).resolve()
    if not dataset_dir.is_dir():
        raise SystemExit(f"input directory not found: {dataset_dir}")

    mat_files = sorted(dataset_dir.glob("*.mat"))
    if not mat_files:
        raise SystemExit(f"no .mat files found in {dataset_dir}")

    written_sidecars = 0
    for mat_file in mat_files:
        sidecar_path = Path(str(mat_file) + ".json")
        if sidecar_path.exists() and not args.overwrite:
            continue

        sidecar = _build_sidecar(mat_file, args.pulse_index)
        sidecar_path.write_text(json.dumps(sidecar, indent=2) + "\n", encoding="utf-8")
        written_sidecars += 1

    manifest_path = _write_manifest(dataset_dir, mat_files)
    checksums_path = _write_checksums(dataset_dir, mat_files)

    print(json.dumps(
        {
            "dataset": str(dataset_dir),
            "mat_files": len(mat_files),
            "sidecars_written": written_sidecars,
            "manifest": str(manifest_path),
            "checksums": str(checksums_path),
        },
        indent=2,
    ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
