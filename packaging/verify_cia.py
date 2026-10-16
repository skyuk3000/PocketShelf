#!/usr/bin/env python3
"""Verify release-critical PocketShelf CIA metadata and hashes."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

EXPECTED_TITLE_ID = "0004000000f3d500"
EXPECTED_PRODUCT_CODE = "CTR-P-PKSH"
EXPECTED_VERSION = "0.6.1 (97)"
EXPECTED_PRIORITY = 48
EXPECTED_SECTION_HASHES = 4


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    cia = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "pocketshelf.cia"
    ctrtool = root / "tools" / "bin" / "ctrtool"
    require(cia.is_file(), f"CIA not found: {cia}")
    require(ctrtool.is_file(), f"ctrtool not found: {ctrtool}")

    result = subprocess.run(
        [str(ctrtool), "-y", str(cia)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    output = result.stdout
    require(result.returncode == 0, f"ctrtool exited with {result.returncode}")
    require(f"Title id:               {EXPECTED_TITLE_ID}" in output, "unexpected title ID")
    require(f"Product code:           {EXPECTED_PRODUCT_CODE}" in output, "unexpected product code")

    priority = re.search(r"Main thread priority:\s+(\d+)\s+\(GOOD\)", output)
    require(priority is not None, "missing valid main-thread priority")
    require(int(priority.group(1)) == EXPECTED_PRIORITY,
            f"main-thread priority is {priority.group(1)}, expected {EXPECTED_PRIORITY}")
    require(f"TitleVersion:  {EXPECTED_VERSION}" in output, "unexpected title version")

    require("Exheader hash: (GOOD)" in output, "exheader hash is not GOOD")
    require("ExeFS hash: (GOOD)" in output, "ExeFS hash is not GOOD")
    content_hashes = len(re.findall(r"^\s*\\?- Hash: \(GOOD\)", output, re.MULTILINE))
    require(content_hashes >= 1, "no GOOD CIA content hash found")
    section_hashes = output.count("Section hash: (GOOD)")
    require(section_hashes == EXPECTED_SECTION_HASHES,
            f"found {section_hashes} GOOD section hashes, expected {EXPECTED_SECTION_HASHES}")

    print(
        f"CIA verified: title_id={EXPECTED_TITLE_ID} product={EXPECTED_PRODUCT_CODE} "
        f"version={EXPECTED_VERSION} priority={EXPECTED_PRIORITY} "
        f"content_hashes={content_hashes} section_hashes={section_hashes}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"CIA verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
