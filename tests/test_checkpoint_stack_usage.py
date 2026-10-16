#!/usr/bin/env python3
"""Reject device code paths with stack frames too large for a 3DSX stack."""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path

MAX_CHECKPOINT_FRAME_BYTES = 4096


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="pocketshelf-stack-") as directory:
        output = Path(directory) / "checkpoint_sync.o"
        compiler = os.environ.get("CXX", "g++")
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-O2",
                "-fstack-usage",
                f"-I{root / 'include'}",
                "-c",
                str(root / "source/checkpoint_sync.cpp"),
                "-o",
                str(output),
            ],
            check=True,
        )
        reports = list(Path(directory).glob("*.su"))
        if len(reports) != 1:
            raise RuntimeError(f"expected one stack report, found {len(reports)}")
        offenders: list[str] = []
        for line in reports[0].read_text(encoding="utf-8").splitlines():
            fields = line.rsplit("\t", 2)
            if len(fields) != 3:
                continue
            size = int(fields[1])
            if size > MAX_CHECKPOINT_FRAME_BYTES:
                offenders.append(f"{size} bytes: {fields[0]}")
        if offenders:
            raise RuntimeError(
                "Checkpoint code exceeds the 3DS stack-frame budget:\n" + "\n".join(offenders)
            )
    print("Checkpoint stack usage test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
