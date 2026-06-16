#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib
import json
import sys
import hashlib
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
    supports_true_focused_reference = False
    limitation = (
        "SarPy does not expose a stable, portable direct CRSD-to-focused-image "
        "API in this harness; independent local surrogate mode is used when requested."
    )
    return {
        "tool": "reference_image_from_crsd",
        "local_only": True,
        "ci_safe": False,
        "workflow_mode": "focused_reference_local_only",
        "supports_true_focused_reference": supports_true_focused_reference,
        "supports_independent_local_surrogate": True,
        "quicklook_rejected_as_focused_reference": True,
        "limitations": [limitation],
        "notes": [
            "SarPy CRSD focused-reference workflow is optional and local-only.",
            "This script is not a GraphX runtime dependency.",
            "This script does not implement any CRSD writing behavior.",
            "CRSD signal magnitude quick-look is explicitly rejected as focused-reference output.",
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


def _load_ordered_crsd_paths(input_crsd_set_json: Path | None, input_crsd_paths: list[str]) -> list[Path]:
    ordered_paths: list[str] = []

    if input_crsd_set_json is not None:
        if not input_crsd_set_json.exists():
            raise FileNotFoundError(f"CRSD set json not found: {input_crsd_set_json}")
        payload = json.loads(input_crsd_set_json.read_text(encoding="utf-8"))
        if isinstance(payload, dict):
            if isinstance(payload.get("crsd_paths"), list):
                ordered_paths = [str(x) for x in payload["crsd_paths"]]
            elif isinstance(payload.get("ordered_crsd_paths"), list):
                ordered_paths = [str(x) for x in payload["ordered_crsd_paths"]]
            else:
                raise RuntimeError("CRSD set json must contain crsd_paths or ordered_crsd_paths array")
        elif isinstance(payload, list):
            ordered_paths = [str(x) for x in payload]
        else:
            raise RuntimeError("CRSD set json must be array or object")

    if input_crsd_paths:
        ordered_paths.extend(input_crsd_paths)

    if not ordered_paths:
        raise RuntimeError("at least one CRSD path is required")

    normalized: list[Path] = []
    for path in ordered_paths:
        p = Path(path).resolve()
        if not p.exists():
            raise FileNotFoundError(f"CRSD file not found: {p}")
        normalized.append(p)
    return normalized


def _focused_surrogate_from_ordered_set(input_crsd_paths: list[Path]):
    numpy = _import_optional("numpy")
    if numpy is None:
        raise RuntimeError("numpy is not installed")

    focused_images: list[Any] = []
    sample_shapes: list[tuple[int, int]] = []

    for crsd_path in input_crsd_paths:
        reader = _open_crsd_reader(crsd_path)
        block = _read_reference_array(reader)
        close_method = getattr(reader, "close", None)
        if callable(close_method):
            try:
                close_method()
            except Exception:
                pass

        complex_block = numpy.asarray(block, dtype=numpy.complex64)
        if complex_block.ndim != 2:
            complex_block = complex_block.reshape(complex_block.shape[-2], complex_block.shape[-1])

        # Independent surrogate reference path: coherent transform with phase retained.
        centered = complex_block - numpy.mean(complex_block)
        focused = numpy.fft.fftshift(numpy.fft.ifft2(centered))
        focused_images.append(focused.astype(numpy.complex64))
        sample_shapes.append((int(focused.shape[0]), int(focused.shape[1])))

    min_rows = min(s[0] for s in sample_shapes)
    min_cols = min(s[1] for s in sample_shapes)
    if min_rows <= 0 or min_cols <= 0:
        raise RuntimeError("focused surrogate produced empty image")

    fused = numpy.zeros((min_rows, min_cols), dtype=numpy.complex64)
    for image in focused_images:
        fused += image[:min_rows, :min_cols]

    return fused


def generate_reference_image(
    input_crsd_set_json: Path | None,
    input_crsd_paths: list[str],
    output_reference_npy: Path,
    output_magnitude_png: Path,
    output_metadata_json: Path,
    force_quicklook_extraction: bool,
) -> dict[str, Any]:
    if force_quicklook_extraction:
        raise RuntimeError(
            "quicklook_rejected:CRSD signal magnitude extraction is not accepted as focused reference")

    ordered_crsd_paths = _load_ordered_crsd_paths(input_crsd_set_json, input_crsd_paths)

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

    focused = _focused_surrogate_from_ordered_set(ordered_crsd_paths)
    magnitude = numpy.abs(focused).astype(numpy.float32)
    output_reference_npy.parent.mkdir(parents=True, exist_ok=True)
    output_magnitude_png.parent.mkdir(parents=True, exist_ok=True)
    output_metadata_json.parent.mkdir(parents=True, exist_ok=True)
    numpy.save(output_reference_npy, focused)
    pyplot.imsave(output_magnitude_png, magnitude, cmap="viridis")

    ordered_set_hash = hashlib.sha256(
        "\n".join(str(p) for p in ordered_crsd_paths).encode("utf-8")).hexdigest()

    metadata = {
        "schema": "graphx.sar.crsd_reference_image_metadata.v1",
        "local_only": True,
        "ci_safe": False,
        "reference_kind": "independent_local_focused_surrogate",
        "true_sarpy_focused_reference_available": False,
        "limitation": (
            "No stable direct SarPy CRSD-to-focused-image API is available in this harness; "
            "using independent local surrogate reference formation path."
        ),
        "ordered_crsd_inputs": [str(p) for p in ordered_crsd_paths],
        "ordered_set_count": len(ordered_crsd_paths),
        "ordered_set_hash": ordered_set_hash,
        "quicklook_rejected_as_focused_reference": True,
        "focused_reference_npy_file": str(output_reference_npy),
        "magnitude_png_file": str(output_magnitude_png),
        "height": int(magnitude.shape[0]),
        "width": int(magnitude.shape[1]),
        "dtype": str(magnitude.dtype),
        "focused_dtype": str(focused.dtype),
    }
    _write_json(output_metadata_json, metadata)

    return metadata


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SarPy CRSD reference image extraction")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report local package availability")
    probe.add_argument("--output-json", required=True)

    generate = subparsers.add_parser(
        "generate-reference",
        help="Emit focused-reference surrogate artifact and metadata from ordered CRSD set")
    generate.add_argument("--input-crsd", action="append", default=[])
    generate.add_argument("--input-crsd-set-json", required=False)
    generate.add_argument("--output-reference-npy", required=True)
    generate.add_argument("--output-magnitude-png", required=True)
    generate.add_argument("--output-metadata-json", required=True)
    generate.add_argument("--force-quicklook-extraction", action="store_true")

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
                Path(args.input_crsd_set_json).resolve() if args.input_crsd_set_json else None,
                list(args.input_crsd),
                Path(args.output_reference_npy).resolve(),
                Path(args.output_magnitude_png).resolve(),
                Path(args.output_metadata_json).resolve(),
                bool(args.force_quicklook_extraction),
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
