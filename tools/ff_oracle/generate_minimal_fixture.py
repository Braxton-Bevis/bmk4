#!/usr/bin/env python3
"""Generate BMK4's empty, unsigned IW3 fastfile fixture from first principles.

The emitted bytes contain no Activision data: IW3's public/decompiled container
magic and version, followed by a zlib stream containing zero-valued XFile and
XAssetList records. Slice 7 will add asset-bearing synthetic fixtures.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib


MAGIC = b"IWffu100"
VERSION = 5
XFILE_U32_FIELDS = 11  # size, externalSize, blockSize[9]
XASSETLIST_U32_FIELDS = 4  # string count/pointer, asset count/pointer


def build_fixture() -> bytes:
    payload = struct.pack(
        f"<{XFILE_U32_FIELDS + XASSETLIST_U32_FIELDS}I",
        *([0] * (XFILE_U32_FIELDS + XASSETLIST_U32_FIELDS)),
    )
    return MAGIC + struct.pack("<I", VERSION) + zlib.compress(payload, level=9)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_fixture())


if __name__ == "__main__":
    main()
