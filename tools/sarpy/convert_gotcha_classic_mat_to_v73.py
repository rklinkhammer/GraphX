#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import h5py
import numpy as np
from scipy.io import loadmat


def _flatten_float64(value) -> np.ndarray:
    arr = np.asarray(value, dtype=np.float64)
    return arr.reshape(-1)


def _scalar_float(value, default: float = 0.0) -> float:
    arr = np.asarray(value)
    if arr.size == 0:
        return float(default)
    return float(arr.reshape(-1)[0])


def _vector_or_broadcast(value, size: int, default: float = 0.0) -> np.ndarray:
    vec = _flatten_float64(value)
    if vec.size == 0:
        return np.full((size,), float(default), dtype=np.float64)
    if vec.size == 1:
        return np.full((size,), float(vec[0]), dtype=np.float64)
    if vec.size >= size:
        return vec[:size].astype(np.float64)
    out = np.empty((size,), dtype=np.float64)
    out[: vec.size] = vec
    out[vec.size :] = vec[-1]
    return out


def _extract_subdata(path: Path):
    mat = loadmat(path, squeeze_me=True, struct_as_record=False)
    if "subData" not in mat:
        raise RuntimeError(f"missing 'subData' struct in {path}")
    return mat["subData"]


def _extract_phdata_and_shape(subdata, src: Path) -> tuple[np.ndarray, int, int]:
    phdata = np.asarray(getattr(subdata, "phdata"))
    if phdata.size == 0:
        raise RuntimeError(f"empty subData.phdata in {src}")

    if phdata.ndim == 1:
        k = int(phdata.shape[0])
        npulses = 1
        phdata_npk = phdata.reshape((1, k))
    elif phdata.ndim == 2:
        k = int(phdata.shape[0])
        npulses = int(phdata.shape[1])
        # MATLAB classic load returns [K, Np].
        # GraphX HDF5 reader expects HDF5 dims [Np, K].
        phdata_npk = phdata.T
    else:
        raise RuntimeError(f"unsupported phdata rank in {src}: {phdata.ndim}")

    if not np.iscomplexobj(phdata_npk):
        raise RuntimeError(f"subData.phdata is not complex in {src}")

    return phdata_npk, npulses, k


def _extract_pulse_times(subdata, npulses: int) -> np.ndarray:
    if not hasattr(subdata, "Np"):
        return np.arange(npulses, dtype=np.float64)

    raw = _flatten_float64(getattr(subdata, "Np"))
    if raw.size == 0:
        return np.arange(npulses, dtype=np.float64)

    if raw.size == 1 and int(round(raw[0])) == npulses:
        return np.arange(npulses, dtype=np.float64)

    return _vector_or_broadcast(raw, npulses, default=0.0)


def _write_hdf5_subdata(dst: Path, subdata, phdata_npk: np.ndarray, npulses: int, k: int) -> None:
    compound_dtype = np.dtype([("real", np.float64), ("imag", np.float64)])
    phdata_compound = np.empty((npulses, k), dtype=compound_dtype)
    phdata_compound["real"] = np.real(phdata_npk).astype(np.float64)
    phdata_compound["imag"] = np.imag(phdata_npk).astype(np.float64)

    ant_x = _vector_or_broadcast(getattr(subdata, "AntX", np.array([0.0])), npulses, 0.0)
    ant_y = _vector_or_broadcast(getattr(subdata, "AntY", np.array([0.0])), npulses, 0.0)
    ant_z = _vector_or_broadcast(getattr(subdata, "AntZ", np.array([0.0])), npulses, 0.0)
    r0 = _vector_or_broadcast(getattr(subdata, "R0", np.array([0.0])), npulses, 0.0)
    pulse_times = _extract_pulse_times(subdata, npulses)

    min_f = _scalar_float(getattr(subdata, "minF", 0.0), default=0.0)
    delta_f = _scalar_float(getattr(subdata, "deltaF", 0.0), default=0.0)

    dst.parent.mkdir(parents=True, exist_ok=True)
    with h5py.File(dst, "w") as handle:
        group = handle.create_group("subData")
        group.create_dataset("phdata", data=phdata_compound)
        group.create_dataset("AntX", data=ant_x.astype(np.float64))
        group.create_dataset("AntY", data=ant_y.astype(np.float64))
        group.create_dataset("AntZ", data=ant_z.astype(np.float64))
        group.create_dataset("R0", data=r0.astype(np.float64))
        group.create_dataset("Np", data=pulse_times.astype(np.float64))
        group.create_dataset("K", data=np.array([float(k)], dtype=np.float64))
        group.create_dataset("deltaF", data=np.array([delta_f], dtype=np.float64))
        group.create_dataset("minF", data=np.array([min_f], dtype=np.float64))


def _write_manifest(output_dir: Path, mat_files: list[Path]) -> Path:
    manifest = {
        "schema": "graphx.gotcha.input_manifest.v1",
        "files": [{"path": f.name} for f in mat_files],
    }
    path = output_dir / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


def _write_checksums(output_dir: Path, mat_files: list[Path]) -> Path:
    lines: list[str] = []
    for mat_file in mat_files:
        digest = hashlib.sha256(mat_file.read_bytes()).hexdigest()
        lines.append(f"{digest} {mat_file.name}")
    path = output_dir / "checksums.sha256"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert classic GOTCHA MAT files into HDF5-backed MAT v7.3-compatible files"
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing classic .mat files")
    parser.add_argument("--output-dir", required=True, help="Directory to write converted .mat files")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing converted files")
    args = parser.parse_args()

    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not input_dir.is_dir():
        raise SystemExit(f"input directory not found: {input_dir}")

    mat_files = sorted(input_dir.glob("*.mat"))
    if not mat_files:
        raise SystemExit(f"no .mat files found in {input_dir}")

    output_dir.mkdir(parents=True, exist_ok=True)

    converted: list[Path] = []
    skipped = 0
    for src in mat_files:
        dst = output_dir / src.name
        if dst.exists() and not args.overwrite:
            converted.append(dst)
            skipped += 1
            continue

        subdata = _extract_subdata(src)
        phdata_npk, npulses, k = _extract_phdata_and_shape(subdata, src)
        _write_hdf5_subdata(dst, subdata, phdata_npk, npulses, k)
        converted.append(dst)

    manifest_path = _write_manifest(output_dir, converted)
    checksums_path = _write_checksums(output_dir, converted)

    summary = {
        "input_dir": str(input_dir),
        "output_dir": str(output_dir),
        "input_mat_files": len(mat_files),
        "converted_mat_files": len(converted),
        "skipped_existing": skipped,
        "manifest": str(manifest_path),
        "checksums": str(checksums_path),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
