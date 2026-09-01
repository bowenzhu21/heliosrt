#!/usr/bin/env python3
"""Record the software and hardware identity attached to a benchmark run."""

import argparse
import json
import os
import platform
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional


def command(*args: str) -> Optional[str]:
    if shutil.which(args[0]) is None:
        return None
    try:
        result = subprocess.run(args, check=True, capture_output=True, text=True, timeout=10)
    except (subprocess.SubprocessError, OSError):
        return None
    output = result.stdout.strip() or result.stderr.strip()
    return output.splitlines()[0] if output else None


def gpu_inventory() -> list[dict[str, str]]:
    if shutil.which("nvidia-smi") is None:
        return []
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=uuid,name,driver_version", "--format=csv,noheader,nounits"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (subprocess.SubprocessError, OSError):
        return []
    inventory = []
    for line in result.stdout.splitlines():
        fields = [field.strip() for field in line.split(",")]
        if len(fields) == 3:
            inventory.append({"uuid": fields[0], "name": fields[1], "driver_version": fields[2]})
    return inventory


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    fingerprint = {
        "fingerprint_version": 1,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "git_commit": command("git", "rev-parse", "HEAD"),
        "platform": {
            "os": platform.system(),
            "release": platform.release(),
            "architecture": platform.machine(),
            "processor": platform.processor() or None,
            "cpu_count": os.cpu_count(),
        },
        "toolchain": {
            "cxx": command("c++", "--version"),
            "cmake": command("cmake", "--version"),
            "nvcc": command("nvcc", "--version"),
            "python": platform.python_version(),
        },
        "gpus": gpu_inventory(),
    }
    serialized = json.dumps(fingerprint, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(serialized, encoding="utf-8")
    else:
        print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
