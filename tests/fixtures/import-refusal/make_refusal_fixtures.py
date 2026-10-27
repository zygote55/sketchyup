#!/usr/bin/env python3
"""Writes the R105.a refusal fixtures (ADR 0179 section 8, V1).

Every file is self-made here, with fixed timestamps so that a rerun reproduces the bytes. No
third-party model is used: the ZIP wraps the repository's own synthetic box.skp (see
tests/fixtures/skp/SKP-SOURCE.txt). Run from any directory:

  python3 -I tests/fixtures/import-refusal/make_refusal_fixtures.py
"""
import pathlib
import struct
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
SKP_BOX = HERE.parent / "skp" / "box.skp"
# A fixed timestamp for every ZIP member: the archive bytes do not depend on the clock.
STAMP = (2026, 10, 27, 0, 0, 0)

COLLADA = """<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <contributor><authoring_tool>R105.a refusal fixture</authoring_tool></contributor>
    <created>2026-10-27T00:00:00</created>
    <modified>2026-10-27T00:00:00</modified>
    <unit name="meter" meter="1"/>
    <up_axis>Z_UP</up_axis>
  </asset>
  <library_geometries/>
  <library_visual_scenes/>
  <scene/>
</COLLADA>
"""


def write_zip(path, members):
    # Stored, not deflated: the bytes stay the same across zlib versions.
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in members:
            info = zipfile.ZipInfo(name, date_time=STAMP)
            info.compress_type = zipfile.ZIP_STORED
            archive.writestr(info, data)


def main():
    # The ZIP wrapper a download gives (ADR 0179 section 4): one model file inside.
    write_zip(HERE / "wrapped-box.zip", [("box.skp", SKP_BOX.read_bytes())])
    # A minimal COLLADA 1.4.1 document with no geometry: enough to name the format.
    (HERE / "minimal.dae").write_text(COLLADA, encoding="utf-8")
    # An empty-but-signed USDZ: a ZIP whose one member is a USD ASCII layer with no prims.
    write_zip(HERE / "stub.usdz", [("model.usda", b"#usda 1.0\n")])
    # An empty-but-signed FBX: the binary header (a 21-byte magic, 0x1a 0x00, version 7400)
    # and 16 null bytes, enough to name the format.
    fbx = b"Kaydara FBX Binary  \x00" + b"\x1a\x00" + struct.pack("<I", 7400) + b"\x00" * 16
    (HERE / "stub.fbx").write_bytes(fbx)


if __name__ == "__main__":
    main()
