#!/usr/bin/env python3
"""An independent reader and writer of the IFC export package (src/io/ifc_export.hpp documents the layout), for
the tests of R097.d. It shares no code with the application's encoder or the helper's reader, so a package the
application writes can be decoded here and a package built here can be given to the helper (ADR 0171 section 6.2).

    python3 -I tests/ifc_export_package.py PACKAGE        prints the decoded manifest and mesh summary as JSON
"""
import hashlib
import json
import struct
import sys

ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$"
DERIVE_PREFIX = b"SketchyUp IFC GlobalId v1\0"


def compress_guid(sixteen_bytes):
    """ISO 10303-21 / IFC compression of 128 bits: a leading group of 2 bits, then 21 groups of 6."""
    number = int.from_bytes(sixteen_bytes, "big")
    out = [ALPHABET[number >> 126]]
    for group in range(1, 22):
        out.append(ALPHABET[(number >> (126 - 6 * group)) & 63])
    return "".join(out)


def derived_global_id(identity, kind, body_id, attempt=0):
    """Decision 16: SHA-256 of a fixed prefix, the document identity, a kind and the body's stable ID."""
    seed = DERIVE_PREFIX + identity.encode() + b"\0" + kind.encode() + b"\0" + str(body_id).encode()
    if attempt:
        seed += b"\0" + str(attempt).encode()
    return compress_guid(hashlib.sha256(seed).digest()[:16])


def aux_global_id(identity, kind, owner, index=0, attempt=0):
    """The helper's identifiers for relationships and property sets."""
    seed = DERIVE_PREFIX + identity.encode() + b"\0aux:" + kind.encode() + b"\0" + owner.encode() + b"\0" + str(index).encode()
    if attempt:
        seed += b"\0" + str(attempt).encode()
    return compress_guid(hashlib.sha256(seed).digest()[:16])


class Mesh:
    def __init__(self, vertices, triangles, closed=False, colors=None, color_index=None):
        self.vertices = [tuple(v) for v in vertices]
        self.triangles = [tuple(t) for t in triangles]
        self.closed = closed
        self.colors = [tuple(c) for c in (colors or [])]
        self.color_index = list(color_index or [])


def box_mesh(x0=0.0, y0=0.0, z0=0.0, x1=1.0, y1=1.0, z1=1.0, closed=True):
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    # outward, counter-clockwise from outside
    t = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    return Mesh(v, t, closed)


def node(role, name, parent, guid, mesh=-1, style=-1, **extra):
    n = {"id": extra.pop("id", 0), "role": role, "class": extra.pop("cls", ""), "guid": guid, "name": name, "longName": "",
         "objectType": "", "predefinedType": "", "parent": parent, "placement": [0, 0, 0, 0, 0, 1, 1, 0, 0],
         "mesh": mesh, "style": style, "hidden": False, "props": []}
    if "elevation" in extra:
        n["elevation"] = extra.pop("elevation")
    n.update(extra)
    return n


def encode_package(nodes, meshes=(), styles=(), georeference=None, identity="0123456789abcdef0123456789abcdef",
                   timestamp="2026-01-02T03:04:05", file_name="test.ifc", manifest_overrides=None):
    chunk = bytearray()
    mesh_entries = []
    for m in meshes:
        for v in m.vertices:
            chunk += struct.pack("<ddd", *v)
        for t in m.triangles:
            chunk += struct.pack("<III", *t)
        if m.colors:
            for i in m.color_index:
                chunk += struct.pack("<I", i)
            for c in m.colors:
                chunk += struct.pack("<fff", *c)
        mesh_entries.append({"vertices": len(m.vertices), "triangles": len(m.triangles), "colors": len(m.colors), "closed": m.closed})
    manifest = {"format": "sketchyup-ifc-export-package", "version": 1, "meshSha256": hashlib.sha256(bytes(chunk)).hexdigest(),
                "documentId": identity, "application": {"name": "SketchyUp", "version": "1.3.0"}, "timestamp": timestamp,
                "fileName": file_name, "georeference": georeference, "styles": list(styles), "meshes": mesh_entries, "nodes": list(nodes)}
    if manifest_overrides:
        manifest.update(manifest_overrides)
    text = json.dumps(manifest, separators=(",", ":")).encode()
    return b"SKIFCX01" + struct.pack("<IIQ", len(text), 0, len(chunk)) + text + bytes(chunk)


def decode_package(data):
    assert data[:8] == b"SKIFCX01", "magic"
    manifest_bytes, reserved, mesh_bytes = struct.unpack("<IIQ", data[8:24])
    assert reserved == 0
    assert len(data) == 24 + manifest_bytes + mesh_bytes, "length"
    manifest = json.loads(data[24:24 + manifest_bytes])
    chunk = data[24 + manifest_bytes:]
    assert hashlib.sha256(chunk).hexdigest() == manifest["meshSha256"], "digest"
    meshes = []
    at = 0
    for entry in manifest["meshes"]:
        v = entry["vertices"]
        t = entry["triangles"]
        k = entry["colors"]
        vertices = [struct.unpack_from("<ddd", chunk, at + 24 * i) for i in range(v)]
        at += 24 * v
        triangles = [struct.unpack_from("<III", chunk, at + 12 * i) for i in range(t)]
        at += 12 * t
        color_index, colors = [], []
        if k:
            color_index = [struct.unpack_from("<I", chunk, at + 4 * i)[0] for i in range(t)]
            at += 4 * t
            colors = [struct.unpack_from("<fff", chunk, at + 12 * i) for i in range(k)]
            at += 12 * k
        meshes.append(Mesh(vertices, triangles, entry["closed"], colors, color_index))
    assert at == len(chunk), "mesh chunk length"
    return manifest, meshes


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as handle:
        manifest, meshes = decode_package(handle.read())
    print(json.dumps({"manifest": manifest, "meshes": [{"vertices": len(m.vertices), "triangles": len(m.triangles), "colors": len(m.colors),
                                                         "closed": m.closed} for m in meshes]}))
