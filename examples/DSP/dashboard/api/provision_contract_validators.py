#!/usr/bin/env python3
"""Provision the pinned authoritative dashboard contract validators."""

import argparse
import subprocess
import sys
import venv
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--venv", type=Path, required=True)
    parser.add_argument("--wheelhouse", type=Path)
    args = parser.parse_args()
    environment = args.venv.resolve()
    requirements = Path(__file__).resolve().parent / "requirements-contracts.lock"
    venv.EnvBuilder(with_pip=True, clear=False).create(environment)
    python = environment / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    command = [str(python), "-m", "pip", "install", "--disable-pip-version-check"]
    if args.wheelhouse:
        command += ["--no-index", "--find-links", str(args.wheelhouse.resolve())]
    command += ["-r", str(requirements)]
    subprocess.run(command, check=True)
    subprocess.run([str(python), "-c",
                    "import jsonschema, openapi_spec_validator; "
                    "print('dashboard contract validators provisioned')"], check=True)
    print(python)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
