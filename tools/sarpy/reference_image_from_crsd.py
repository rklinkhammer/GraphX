#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib
import json
import sys
from pathlib import Path
from typing import Any


def _import_optional(module_name: str):
    try:
        return importlib.import_module(module_name)
    except Exception:
        return None


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _package_status(module_name: str) -> dict[str, Any]:
    module = _import_optional(module_name)
    installed = module is not None
    version = getattr(module, "__version__", "unknown") if installed else None
    return {
        "installed": installed,
        "version": version,
    }


def probe_environment() -> dict[str, Any]:
    return {
        "tool": "reference_image_from_crsd",
        "local_only": True,
        "ci_safe": False,
        "notes": [
            "SarPy CRSD reference image extraction is optional and local-only.",
            "This script is not a GraphX runtime dependency.",
            "This script does not implement any CRSD writing behavior.",
        ],
        "packages": {
            "sarpy": _package_status("sarpy"),
            "numpy": _package_status("numpy"),
            "matplotlib": _package_status("matplotlib"),
        },
    }


def _open_crsd_reader(input_path: Path):
    converter = _import_optional("sarpy.io.received.converter")
    if converter is not None and hasattr(converter, "open_received"):
        return converter.open_received(str(input_path))

    crsd_module = _import_optional("sarpy.io.received.crsd")
    if crsd_module is not None and hasattr(crsd_module, "CRSDReader"):
        return crsd_module.CRSDReader(str(input_path))

    raise RuntimeError("unable to open CRSD file with installed SarPy API")


def _read_reference_array(reader: Any):
    numpy = _import_optional("numpy")
    if numpy is None:
        raise RuntimeError("numpy is not installed")

    def _as_nonempty_2d(value: Any):
        arr = numpy.asarray(value)
        if arr.size == 0:
            return None
        if arr.ndim == 1:
            return arr.reshape(1, -1)
        if arr.ndim == 2:
            return arr
        return arr.reshape(arr.shape[-2], arr.shape[-1])

    data_size = getattr(reader, "data_size", None)
    if data_size is None:
        method = getattr(reader, "get_data_size_as_tuple", None)
        if callable(method):
            try:
                data_size = method()
                if isinstance(data_size, tuple) and len(data_size) == 1:
                    data_size = data_size[0]
            except Exception:
                data_size = None

    # Best-effort extraction from SarPy reader APIs.
    method = getattr(reader, "read", None)
    if callable(method):
        if isinstance(data_size, tuple) and len(data_size) == 2:
            rows = min(int(data_size[0]), 64)
            cols = min(int(data_size[1]), 256)
            try:
                arr = _as_nonempty_2d(method(slice(0, rows), slice(0, cols)))
                if arr is not None:
                    return arr
            except Exception:
                pass

        try:
            arr = method(slice(0, 1), slice(0, 1), slice(0, 256))
            arr = _as_nonempty_2d(arr)
            if arr is not None:
                return arr
        except Exception:
            pass

    method = getattr(reader, "read_chip", None)
    if callable(method):
        if isinstance(data_size, tuple) and len(data_size) == 2:
            rows = min(int(data_size[0]), 64)
            cols = min(int(data_size[1]), 256)
            try:
                arr = _as_nonempty_2d(method((0, rows), (0, cols)))
                if arr is not None:
                    return arr
            except Exception:
                pass

        try:
            arr = method(0, (0, 64), (0, 64))
            arr = _as_nonempty_2d(arr)
            if arr is not None:
                return arr
        except Exception:
            pass

    raise RuntimeError("unable to extract sample block from CRSD reader")


def generate_reference_image(
    input_crsd: Path,
    output_magnitude_png: Path,
    output_metadata_json: Path,
) -> dict[str, Any]:
    if not input_crsd.exists():
        raise FileNotFoundError(f"CRSD file not found: {input_crsd}")

    sarpy = _import_optional("sarpy")
    numpy = _import_optional("numpy")
    matplotlib = _import_optional("matplotlib")
    if sarpy is None:
        raise RuntimeError("SarPy is not installed")
    if numpy is None:
        raise RuntimeError("numpy is not installed")
    if matplotlib is None:
        raise RuntimeError("matplotlib is not installed")

    matplotlib.use("Agg")
    pyplot = importlib.import_module("matplotlib.pyplot")

    reader = _open_crsd_reader(input_crsd)
    reference_block = _read_reference_array(reader)

    magnitude = numpy.abs(reference_block).astype(numpy.float32)
    output_magnitude_png.parent.mkdir(parents=True, exist_ok=True)
    output_metadata_json.parent.mkdir(parents=True, exist_ok=True)
    pyplot.imsave(output_magnitude_png, magnitude, cmap="viridis")

    metadata = {
        "schema": "graphx.sar.crsd_reference_image_metadata.v1",
        "local_only": True,
        "input_crsd": str(input_crsd),
        "magnitude_png_file": str(output_magnitude_png),
        "height": int(magnitude.shape[0]),
        "width": int(magnitude.shape[1]),
        "dtype": str(magnitude.dtype),
    }
    _write_json(output_metadata_json, metadata)

    close_method = getattr(reader, "close", None)
    if callable(close_method):
        try:
            close_method()
        except Exception:
            pass

    return metadata


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SarPy CRSD reference image extraction")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report local package availability")
    probe.add_argument("--output-json", required=True)

    generate = subparsers.add_parser("generate-reference", help="Emit CRSD-derived magnitude image and metadata")
    generate.add_argument("--input-crsd", required=True)
    generate.add_argument("--output-magnitude-png", required=True)
    generate.add_argument("--output-metadata-json", required=True)

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "probe-environment":
        output_path = Path(args.output_json).resolve()
        _write_json(output_path, probe_environment())
        print(output_path)
        return 0

    if args.command_name == "generate-reference":
        try:
            generate_reference_image(
                Path(args.input_crsd).resolve(),
                Path(args.output_magnitude_png).resolve(),
                Path(args.output_metadata_json).resolve(),
            )
            print(Path(args.output_metadata_json).resolve())
            return 0
        except Exception as exc:
            _write_json(
                Path(args.output_metadata_json).resolve(),
                {
                    "schema": "graphx.sar.crsd_reference_image_metadata.v1",
                    "local_only": True,
                    "status": "error",
                    "errors": [str(exc)],
                },
            )
            print(f"error: {exc}", file=sys.stderr)
            print(Path(args.output_metadata_json).resolve())
            return 2

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
