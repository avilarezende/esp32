#!/usr/bin/env python3
"""Fail if the CYD release images are not a coherent OTA layout."""
from pathlib import Path

root = Path("release/cyd")
app = (root / "esp32-ota.bin").read_bytes()
plain = (root / "esp32.bin").read_bytes()
merged = (root / "firmware-cyd.bin").read_bytes()
if not app or app != plain:
    raise SystemExit("esp32-ota.bin must be a non-empty copy of esp32.bin")
off = 0x20000
if merged[off:off + len(app)] != app:
    raise SystemExit("firmware-cyd.bin does not contain the app at 0x20000")
if merged[0xF000:0xF000 + 16] != b"\xff" * 16:
    raise SystemExit("otadata in the merged image is not blank")
# NVS sits at 0x9000. The merged image pads that gap with 0xFF.
if merged[0x9000:0x9000 + 16] != b"\xff" * 16:
    raise SystemExit("merged image gap at NVS is unexpected")
print(f"merged {len(merged)} bytes, ota app {len(app)} bytes at 0x20000")
