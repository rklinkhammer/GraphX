#!/usr/bin/env python3
"""Verify every pinned distribution and dependency in an isolated --target."""

from __future__ import annotations

import importlib.metadata
import pathlib
import sys

from packaging.requirements import Requirement
from packaging.utils import canonicalize_name


def main() -> int:
    lock_path = pathlib.Path(sys.argv[1])
    target = pathlib.Path(sys.argv[2])
    expected: dict[str, str] = {}
    for raw_line in lock_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, separator, version = line.partition("==")
        if not separator or not name or not version:
            raise RuntimeError(f"lock entry is not exact: {line}")
        expected[canonicalize_name(name)] = version

    distributions = {
        canonicalize_name(distribution.metadata["Name"]): distribution
        for distribution in importlib.metadata.distributions(path=[str(target)])
    }
    installed = {name: distribution.version for name, distribution in distributions.items()}
    if installed != expected:
        raise RuntimeError(
            f"target does not exactly match lock: expected={expected!r}, installed={installed!r}"
        )

    for distribution in distributions.values():
        for requirement_text in distribution.requires or []:
            requirement = Requirement(requirement_text)
            if requirement.marker is not None and not requirement.marker.evaluate():
                continue
            dependency = canonicalize_name(requirement.name)
            if dependency not in installed:
                raise RuntimeError(
                    f"{distribution.metadata['Name']} requires unlocked {requirement}"
                )
            if requirement.specifier and installed[dependency] not in requirement.specifier:
                raise RuntimeError(
                    f"{distribution.metadata['Name']} requires {requirement}, "
                    f"found {installed[dependency]}"
                )
    print(f"validated {len(expected)} exact locked Python distributions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
