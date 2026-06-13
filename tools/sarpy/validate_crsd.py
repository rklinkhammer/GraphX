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
        "tool": "validate_crsd",
        "local_only": True,
        "ci_safe": False,
        "notes": [
            "SarPy CRSD validation is optional and local-only.",
            "This script is not a GraphX runtime dependency.",
            "This script does not implement any CRSD writing behavior.",
        ],
        "packages": {
            "sarpy": _package_status("sarpy"),
            "numpy": _package_status("numpy"),
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


def _to_dict(value: Any) -> dict[str, Any]:
    if value is None:
        return {}
    if isinstance(value, dict):
        return value
    if hasattr(value, "to_dict"):
        try:
            return value.to_dict()
        except Exception:
            pass
    if hasattr(value, "to_json"):
        try:
            maybe_json = value.to_json()
            if isinstance(maybe_json, dict):
                return maybe_json
        except Exception:
            pass
    result = {}
    for key in dir(value):
        if key.startswith("_"):
            continue
        try:
            attr = getattr(value, key)
        except Exception:
            continue
        if callable(attr):
            continue
        if isinstance(attr, (str, int, float, bool, list, dict, type(None))):
            result[key] = attr
    return result


def _extract_dimensions(reader: Any) -> list[int] | None:
    for name in ("data_size", "shape"):
        value = getattr(reader, name, None)
        if isinstance(value, tuple):
            return [int(x) for x in value]
        if isinstance(value, list):
            return [int(x) for x in value]

    for name in ("get_data_size_as_tuple", "get_data_size"):
        method = getattr(reader, name, None)
        if callable(method):
            try:
                value = method()
                if isinstance(value, tuple):
                    return [int(x) for x in value]
                if isinstance(value, list):
                    return [int(x) for x in value]
            except Exception:
                continue

    return None


def _extract_dtype(reader: Any) -> str | None:
    for name in ("dtype", "raw_dtype"):
        value = getattr(reader, name, None)
        if value is not None:
            return str(value)
    return None


def _extract_sample_slices(reader: Any) -> dict[str, Any]:
    # Best-effort API probing across SarPy versions; failures are surfaced as notes.
    candidates = (
        ("read", (slice(0, 1), slice(0, 1), slice(0, 8))),
        ("read_chip", (0, (0, 1), (0, 8))),
        ("read_signal_block", (0, 0, 8)),
    )
    for method_name, args in candidates:
        method = getattr(reader, method_name, None)
        if not callable(method):
            continue
        try:
            value = method(*args)
            return {
                "method": method_name,
                "preview": str(value)[:240],
            }
        except Exception:
            continue

    return {
        "method": None,
        "preview": None,
        "note": "sample slice preview unavailable with current SarPy reader API",
    }


def _extract_pvp_arrays(reader: Any) -> dict[str, Any]:
    for attr_name in ("pvp", "PVP", "pvp_array"):
        value = getattr(reader, attr_name, None)
        if value is not None:
            shape = getattr(value, "shape", None)
            dtype = getattr(value, "dtype", None)
            return {
                "source": attr_name,
                "shape": [int(x) for x in shape] if shape is not None else None,
                "dtype": str(dtype) if dtype is not None else None,
            }

    for method_name in ("read_pvp_array", "get_pvp_array"):
        method = getattr(reader, method_name, None)
        if callable(method):
            try:
                value = method()
                shape = getattr(value, "shape", None)
                dtype = getattr(value, "dtype", None)
                return {
                    "source": method_name,
                    "shape": [int(x) for x in shape] if shape is not None else None,
                    "dtype": str(dtype) if dtype is not None else None,
                }
            except Exception:
                continue

    return {
        "source": None,
        "shape": None,
        "dtype": None,
        "note": "PVP array extraction unavailable with current SarPy reader API",
    }


def validate_crsd(input_crsd: Path) -> dict[str, Any]:
    if not input_crsd.exists():
        raise FileNotFoundError(f"CRSD file not found: {input_crsd}")

    sarpy = _import_optional("sarpy")
    if sarpy is None:
        raise RuntimeError("SarPy is not installed")

    reader = _open_crsd_reader(input_crsd)
    meta = getattr(reader, "crsd_meta", None) or getattr(reader, "meta", None)
    meta_dict = _to_dict(meta)

    version = None
    for key in ("Version", "version", "CRSDVersion"):
        if key in meta_dict:
            version = str(meta_dict[key])
            break

    report = {
        "schema": "graphx.sar.crsd_validation_report.v1",
        "local_only": True,
        "input_crsd": str(input_crsd),
        "validation": {
            "opened_by_sarpy": True,
            "status": "ok",
            "errors": [],
            "warnings": [],
        },
        "crsd_version": version,
        "dimensions": _extract_dimensions(reader),
        "dtype": _extract_dtype(reader),
        "sample_slices": _extract_sample_slices(reader),
        "pvp_arrays": _extract_pvp_arrays(reader),
        "metadata_keys": sorted(list(meta_dict.keys())),
    }

    close_method = getattr(reader, "close", None)
    if callable(close_method):
        try:
            close_method()
        except Exception:
            pass

    return report


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local-only SarPy CRSD validation harness")
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    probe = subparsers.add_parser("probe-environment", help="Report local package availability")
    probe.add_argument("--output-json", required=True)

    validate = subparsers.add_parser("validate", help="Validate and summarize a CRSD file through SarPy")
    validate.add_argument("--input-crsd", required=True)
    validate.add_argument("--output-json", required=True)

    return parser


def main() -> int:
    args = build_argument_parser().parse_args()

    if args.command_name == "probe-environment":
        output_path = Path(args.output_json).resolve()
        _write_json(output_path, probe_environment())
        print(output_path)
        return 0

    if args.command_name == "validate":
        output_path = Path(args.output_json).resolve()
        try:
            report = validate_crsd(Path(args.input_crsd).resolve())
            _write_json(output_path, report)
            print(output_path)
            return 0
        except Exception as exc:
            _write_json(
                output_path,
                {
                    "schema": "graphx.sar.crsd_validation_report.v1",
                    "local_only": True,
                    "validation": {
                        "opened_by_sarpy": False,
                        "status": "error",
                        "errors": [str(exc)],
                        "warnings": [],
                    },
                },
            )
            print(f"error: {exc}", file=sys.stderr)
            print(output_path)
            return 2

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
