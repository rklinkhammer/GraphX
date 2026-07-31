#!/usr/bin/env python3
"""Verify that sanitizer qualification includes GraphX production libraries."""

import json
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify-instrumentation.py <compile_commands.json>", file=sys.stderr)
        return 64

    database_path = pathlib.Path(sys.argv[1])
    commands = json.loads(database_path.read_text(encoding="utf-8"))
    component_prefixes = (
        "/libgraph/src/",
        "/libdsp/src/",
        "/libgpu/src/",
        "/libaccelgraph/src/",
        "/libsensor/src/",
    )
    required_flags = ("-fsanitize=address,undefined", "-fno-omit-frame-pointer")
    failed = False

    for prefix in component_prefixes:
        matching = [entry for entry in commands if prefix in entry.get("file", "")]
        missing = [
            entry.get("file", "<unknown>")
            for entry in matching
            if any(flag not in entry.get("command", "") for flag in required_flags)
        ]
        component = prefix.strip("/").split("/")[0]
        if not matching:
            print(f"ERROR: no compile commands found for {component}", file=sys.stderr)
            failed = True
        elif missing:
            print(
                f"ERROR: {len(missing)}/{len(matching)} {component} translation units "
                "lack required sanitizer flags",
                file=sys.stderr,
            )
            for source in missing[:10]:
                print(f"  {source}", file=sys.stderr)
            failed = True
        else:
            print(f"verified sanitizer instrumentation: {component} ({len(matching)} files)")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
