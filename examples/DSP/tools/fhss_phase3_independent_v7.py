#!/usr/bin/env python3
"""Frozen Phase 3 v7 entry point over the sealed v6 evaluator implementation."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
from pathlib import Path
import sys


_ENTRY_POINT = Path(__file__).resolve()
_SEALED_IMPLEMENTATION = _ENTRY_POINT.with_name("fhss_phase3_independent.py")
_SEALED_IMPLEMENTATION_SHA256 = (
    "f3a73c70e140071bbbe3b1d795ae7b8c92969a9fa6d132f0d004071798d0746d"
)


def _load_sealed_implementation():
    actual_sha256 = hashlib.sha256(_SEALED_IMPLEMENTATION.read_bytes()).hexdigest()
    if actual_sha256 != _SEALED_IMPLEMENTATION_SHA256:
        raise RuntimeError(
            "sealed v6 evaluator dependency hash mismatch: "
            f"expected {_SEALED_IMPLEMENTATION_SHA256}, got {actual_sha256}"
        )
    spec = importlib.util.spec_from_file_location(
        "graphx_fhss_phase3_v7_sealed_implementation", _SEALED_IMPLEMENTATION
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load the sealed Phase 3 evaluator")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


_implementation = _load_sealed_implementation()
_validate_v6_profile = _implementation.validate_profile


def _validate_v7_profile(profile):
    if (
        profile.get("schema") != "graphx.fhss.phase3-validation-profile.v7"
        or profile.get("version") != 7
    ):
        raise ValueError("only the post-circular-FIR v7 profile is accepted")
    compatibility_view = copy.deepcopy(profile)
    compatibility_view["schema"] = "graphx.fhss.phase3-validation-profile.v6"
    compatibility_view["version"] = 6
    _validate_v6_profile(compatibility_view)


_implementation.validate_profile = _validate_v7_profile
# The sealed implementation hashes its module-global __file__ for provenance.
# Point it at this entry point so a v7 result binds both this wrapper and its
# verified dependency hash above.
_implementation.__file__ = str(_ENTRY_POINT)


if __name__ == "__main__":
    raise SystemExit(_implementation.main())
