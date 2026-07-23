#!/usr/bin/env python3
"""Deterministic inventory for the single shipped dashboard frontend."""

from __future__ import annotations

import hashlib
import os
import re
import stat
import unicodedata
from pathlib import Path, PurePosixPath

MAX_ASSET_BYTES = 4 * 1024 * 1024
MAX_TOTAL_BYTES = 16 * 1024 * 1024
MAX_ASSET_FILES = 2048
FRONTEND_DIRECTORIES = frozenset(("assets", "fonts", "static"))
FRONTEND_SUFFIXES = frozenset(
    (".html", ".js", ".mjs", ".css", ".woff", ".woff2", ".ttf", ".otf"))
LICENSE_NAMES = frozenset(
    ("LICENSE", "LICENSE.md", "LICENSE.txt", "THIRD_PARTY_NOTICES.md",
     "THIRD_PARTY_NOTICES.txt"))
REMOTE_ASSET = re.compile(
    r"(?:src|href)\s*=\s*['\"](?:https?:)?//|"
    r"(?:import|from)\s*(?:\(|)\s*['\"]https?://", re.IGNORECASE)


class InventoryError(RuntimeError):
    """The deployable frontend violates the Phase 0 asset policy."""


def _relative(root: Path, path: Path) -> str:
    try:
        relative = path.relative_to(root)
    except ValueError as error:
        raise InventoryError(f"asset escapes frontend root: {path}") from error
    pure = PurePosixPath(relative.as_posix())
    if pure.is_absolute() or not pure.parts or any(
            part in ("", ".", "..") for part in pure.parts):
        raise InventoryError(f"invalid frontend asset path: {relative}")
    return unicodedata.normalize("NFC", pure.as_posix())


def _candidate(root: Path, path: Path) -> bool:
    relative = path.relative_to(root)
    if len(relative.parts) == 1:
        return path.name in LICENSE_NAMES or path.suffix.lower() in FRONTEND_SUFFIXES \
            or path.name.endswith(".worker.js") or path.name.endswith(".map")
    return relative.parts[0] in FRONTEND_DIRECTORIES


def inventory_frontend(root: Path, *, permit_source_maps: bool = False,
                       max_file_bytes: int = MAX_ASSET_BYTES,
                       max_total_bytes: int = MAX_TOTAL_BYTES
                       ) -> dict[str, object]:
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise InventoryError(f"frontend root is not a directory: {root}")
    candidates: list[Path] = []
    for directory, names, files in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        relative = directory_path.relative_to(root)
        if not relative.parts:
            selected = sorted(name for name in names if name in FRONTEND_DIRECTORIES)
            names[:] = selected
        else:
            names.sort()
        for name in names:
            path = directory_path / name
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
                raise InventoryError(f"invalid frontend directory: {_relative(root, path)}")
        candidates.extend(directory_path / name for name in sorted(files)
                          if _candidate(root, directory_path / name))
    if len(candidates) > MAX_ASSET_FILES:
        raise InventoryError("frontend asset count exceeds policy")

    entries: list[dict[str, object]] = []
    total = 0
    paths: set[str] = set()
    for path in sorted(candidates, key=lambda item: item.relative_to(root).as_posix()):
        relative = _relative(root, path)
        if relative in paths:
            raise InventoryError(f"duplicate normalized frontend path: {relative}")
        paths.add(relative)
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            raise InventoryError(f"frontend asset is not a regular file: {relative}")
        if info.st_mode & 0o444 == 0:
            raise InventoryError(f"frontend asset is unreadable: {relative}")
        if info.st_size > max_file_bytes:
            raise InventoryError(f"frontend asset exceeds {max_file_bytes} bytes: {relative}")
        if relative.endswith(".map") and not permit_source_maps:
            raise InventoryError(f"release source map is forbidden: {relative}")
        total += info.st_size
        if total > max_total_bytes:
            raise InventoryError("frontend total exceeds policy")
        entries.append({"path":relative, "bytes":info.st_size,
                        "sha256":hashlib.sha256(path.read_bytes()).hexdigest()})
    if "index.html" not in paths:
        raise InventoryError("the single dashboard entrypoint index.html is missing")
    return {
        "schema":"graphx.fhss.dashboard.frontend_asset_inventory.v1",
        "entrypoint":"index.html", "entry_count":len(entries),
        "total_bytes":total, "max_asset_bytes":max_file_bytes,
        "max_total_bytes":max_total_bytes,
        "source_maps_permitted":permit_source_maps, "entries":entries}


def compare_frontend_inventories(source: dict[str, object],
                                 installed: dict[str, object]) -> None:
    if source != installed:
        raise InventoryError("source and installed frontend inventories diverge")


def check_self_hosted_assets(root: Path,
                             inventory: dict[str, object]) -> dict[str, object]:
    remote: list[str] = []
    for entry in inventory["entries"]:
        relative = str(entry["path"])
        if Path(relative).suffix.lower() not in (".html", ".js", ".mjs", ".css"):
            continue
        text = (root / relative).read_text(encoding="utf-8")
        if REMOTE_ASSET.search(text):
            remote.append(relative)
    return {"files_scanned":sum(
                Path(str(entry["path"])).suffix.lower() in
                (".html", ".js", ".mjs", ".css")
                for entry in inventory["entries"]),
            "remote_asset_files":remote,
            "result":"PASS" if not remote else "FAIL"}
