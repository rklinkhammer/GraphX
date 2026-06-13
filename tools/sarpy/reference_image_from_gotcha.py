#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import sys
from pathlib import Path
from typing import Any


def _import_optional(module_name: str):
    try:
        return importlib.import_module(module_name)
    except Exception:
        return None


def _package_status(module_name: str) -> dict[str, Any]:
    module = _import_optional(module_name)
    installed = module is not None
    version = None
    if installed:
        version = getattr(module, "__version__", "unknown")
    return {
        "installed": installed,
        "version": version,
    }


def probe_environment() -> dict[str, Any]:
    return {
        "tool": "reference_image_from_gotcha",
        "local_only": True,
        "ci_safe": False,
        "notes": [
            "This tool is optional and intended for local reference/comparison workflows.",
            "It must not be used as a GraphX runtime dependency.",
            "MATLAB is not required.",
        ],
        "packages": {
            "numpy": _package_status("numpy"),
            "scipy": _package_status("scipy"),
            "h5py": _package_status("h5py"),
            "matplotlib": _package_status("matplotlib"),
            "sarpy": _package_status("sarpy"),
        },
    }


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _require_numpy_matplotlib():
    numpy = _import_optional("numpy")
    matplotlib = _import_optional("matplotlib")
    if numpy is None or matplotlib is None:
        missing = []
        if numpy is None:
            missing.append("numpy")
        if matplotlib is None:
            missing.append("matplotlib")
        raise RuntimeError("missing required package(s): " + ", ".join(missing))
    return numpy, matplotlib


def _load_complex_image_from_json(path: Path):
    numpy, _ = _require_numpy_matplotlib()
    data = json.loads(path.read_text(encoding="utf-8"))

    if "real" not in data or "imag" not in data:
        raise ValueError("input JSON must contain 'real' and 'imag' 2D arrays")

    real = numpy.asarray(data["real"], dtype=numpy.float32)
    imag = numpy.asarray(data["imag"], dtype=numpy.float32)
    if real.ndim != 2 or imag.ndim != 2:
        raise ValueError("'real' and 'imag' must be 2D arrays")
    if real.shape != imag.shape:
        raise ValueError("'real' and 'imag' arrays must have the same shape")

    return real + (1j * imag)


def _discover_fields_scipy(input_mat: Path) -> dict[str, Any] | None:
    scipy_io = _import_optional("scipy.io")
    numpy = _import_optional("numpy")
    if scipy_io is None or numpy is None:
        return None

    mat = scipy_io.loadmat(str(input_mat), simplify_cells=False)
    fields: list[dict[str, Any]] = []
    for key, value in mat.items():
        if key.startswith("__"):
            continue
        shape = list(value.shape) if hasattr(value, "shape") else []
        dtype = str(value.dtype) if hasattr(value, "dtype") else type(value).__name__
        kind = "ndarray" if isinstance(value, numpy.ndarray) else type(value).__name__
        fields.append(
            {
                "name": key,
                "kind": kind,
                "shape": shape,
                "dtype": dtype,
            }
        )

    return {
        "loader": "scipy.io.loadmat",
        "fields": fields,
    }


def _discover_fields_h5py(input_mat: Path) -> dict[str, Any] | None:
    h5py = _import_optional("h5py")
    if h5py is None:
        return None

    fields: list[dict[str, Any]] = []
    try:
        with h5py.File(input_mat, "r") as handle:
            def _visit(name: str, obj) -> None:
                if isinstance(obj, h5py.Dataset):
                    fields.append(
                        {
                            "name": name,
                            "kind": "dataset",
                            "shape": list(obj.shape),
                            "dtype": str(obj.dtype),
                        }
                    )
                elif isinstance(obj, h5py.Group):
                    fields.append(
                        {
                            "name": name,
                            "kind": "group",
                            "shape": [],
                            "dtype": "group",
                        }
                    )

            handle.visititems(_visit)
    except OSError:
        return None

    return {
        "loader": "h5py",
        "fields": fields,
    }


def discover_fields(input_mat: Path) -> dict[str, Any]:
    if not input_mat.exists():
        raise FileNotFoundError(f"input MAT file not found: {input_mat}")

    scipy_result = _discover_fields_scipy(input_mat)
    if scipy_result is not None:
        return {
            "tool": "reference_image_from_gotcha",
            "command": "discover-fields",
            "local_only": True,
            "input_file": str(input_mat),
            "result": scipy_result,
        }

    h5_result = _discover_fields_h5py(input_mat)
    if h5_result is not None:
        return {
            "tool": "reference_image_from_gotcha",
            "command": "discover-fields",
            "local_only": True,
            "input_file": str(input_mat),
            "result": h5_result,
        }

    raise RuntimeError("unable to inspect MAT file: install scipy and/or h5py")


def generate_reference(
    input_json: Path,
    output_reference_npy: Path,
    output_magnitude_png: Path,
    output_metadata_json: Path,
) -> dict[str, Any]:
    numpy, matplotlib = _require_numpy_matplotlib()
    matplotlib.use("Agg")
    pyplot = importlib.import_module("matplotlib.pyplot")

    complex_image = _load_complex_image_from_json(input_json)
    magnitude = numpy.abs(complex_image).astype(numpy.float32)

    output_reference_npy.parent.mkdir(parents=True, exist_ok=True)
    output_magnitude_png.parent.mkdir(parents=True, exist_ok=True)
    output_metadata_json.parent.mkdir(parents=True, exist_ok=True)

    numpy.save(output_reference_npy, complex_image.astype(numpy.complex64))
    pyplot.imsave(output_magnitude_png, magnitude, cmap="viridis")

    digest = hashlib.sha256(complex_image.astype(numpy.complex64).tobytes()).hexdigest()
    metadata = {
        "schema": "graphx.sar.reference_image_metadata.v1",
        "local_only": True,
        "source": "json_complex_grid",
        "reference_image_file": str(output_reference_npy),
        "magnitude_png_file": str(output_magnitude_png),
        "height": int(complex_image.shape[0]),
        "width": int(complex_image.shape[1]),
        "dtype": "complex64",
        "magnitude_min": float(magnitude.min()),
        "magnitude_max": float(magnitude.max()),
        "magnitude_mean": float(magnitude.mean()),
        "content_sha256": digest,
    }
    _write_json(output_metadata_json, metadata)
    return metadata


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Local-only GOTCHA reference image helper (discovery + reference image generation)"
    )
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report optional package availability")
    probe.add_argument("--output-json", required=True)

    discover = subparsers.add_parser("discover-fields", help="Inspect MAT file fields with scipy/h5py")
    discover.add_argument("--input-mat", required=True)
    discover.add_argument("--output-json", required=True)

    generate = subparsers.add_parser(
        "generate-reference",
        help="Generate a deterministic complex reference image from JSON real/imag arrays",
    )
    generate.add_argument("--input-json", required=True)
    generate.add_argument("--output-reference-npy", required=True)
    generate.add_argument("--output-magnitude-png", required=True)
    generate.add_argument("--output-metadata-json", required=True)

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "probe-environment":
        output_json = Path(args.output_json).resolve()
        _write_json(output_json, probe_environment())
        print(output_json)
        return 0

    if args.command_name == "discover-fields":
        output_json = Path(args.output_json).resolve()
        result = discover_fields(Path(args.input_mat).resolve())
        _write_json(output_json, result)
        print(output_json)
        return 0

    if args.command_name == "generate-reference":
        output_json = Path(args.output_metadata_json).resolve()
        try:
            generate_reference(
                Path(args.input_json).resolve(),
                Path(args.output_reference_npy).resolve(),
                Path(args.output_magnitude_png).resolve(),
                output_json,
            )
        except Exception as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2
        print(output_json)
        return 0

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
