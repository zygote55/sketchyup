"""Reads an IFC conversion package (src/io/ifc_package.hpp) in Python, independently of the C++ decoder.

Used by the worker tests; run with `python3 -I`. `read_package(bytes)` returns the manifest as a dict with
each mesh decoded: {"vertices": [(x, y, z)], "triangles": [(a, b, c)], "materials": [..], "edges": [(a, b)],
"colors": [(r, g, b) or None] or []}. Raises ValueError for anything that is not a well-formed package.
"""
import hashlib
import json
import math
import struct

MAGIC = b"SKIFCP01"


def read_package(data):
    if len(data) < 24 or data[:8] != MAGIC:
        raise ValueError("bad header")
    manifest_bytes, reserved, mesh_bytes = struct.unpack_from("<IIQ", data, 8)
    if reserved != 0 or len(data) != 24 + manifest_bytes + mesh_bytes:
        raise ValueError("bad lengths")
    manifest = json.loads(data[24:24 + manifest_bytes].decode("utf-8"))
    chunk = data[24 + manifest_bytes:]
    if hashlib.sha256(chunk).hexdigest() != manifest["meshSha256"]:
        raise ValueError("mesh digest mismatch")
    at = 0
    meshes = []
    for descriptor in manifest["meshes"]:
        v, t, e = descriptor["vertices"], descriptor["triangles"], descriptor["edges"]
        vertices = [struct.unpack_from("<3d", chunk, at + 24 * i) for i in range(v)]
        at += 24 * v
        triangles = [struct.unpack_from("<3I", chunk, at + 12 * i) for i in range(t)]
        at += 12 * t
        materials = list(struct.unpack_from("<%dI" % t, chunk, at))
        at += 4 * t
        edges = [struct.unpack_from("<2I", chunk, at + 8 * i) for i in range(e)]
        at += 8 * e
        colors = []
        if descriptor["colors"]:
            for i in range(t):
                c = struct.unpack_from("<3f", chunk, at + 12 * i)
                colors.append(None if math.isnan(c[0]) else c)
            at += 12 * t
        meshes.append({"vertices": vertices, "triangles": triangles, "materials": materials, "edges": edges, "colors": colors})
    if at != len(chunk):
        raise ValueError("mesh chunk length")
    manifest["meshes"] = meshes
    return manifest


def mesh_volume_area(mesh):
    """Signed volume (divergence theorem) and area of a triangle soup."""
    volume = 0.0
    area = 0.0
    for a, b, c in mesh["triangles"]:
        p, q, r = mesh["vertices"][a], mesh["vertices"][b], mesh["vertices"][c]
        cx = (q[1] - p[1]) * (r[2] - p[2]) - (q[2] - p[2]) * (r[1] - p[1])
        cy = (q[2] - p[2]) * (r[0] - p[0]) - (q[0] - p[0]) * (r[2] - p[2])
        cz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
        area += 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
        volume += (p[0] * (q[1] * r[2] - q[2] * r[1]) - p[1] * (q[0] * r[2] - q[2] * r[0]) + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6.0
    return volume, area


def node_world(manifest, index):
    """The world matrix (row-major 4x4 as nested lists) of a node: its transform times its ancestors'."""
    chain = []
    while index >= 0:
        chain.append(manifest["nodes"][index]["transform"])
        index = manifest["nodes"][index]["parent"]
    world = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    for flat in reversed(chain):
        m = [flat[4 * i:4 * i + 4] for i in range(4)]
        world = [[sum(world[i][k] * m[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    return world


def world_point(world, p):
    return tuple(sum(world[i][k] * p[k] for k in range(3)) + world[i][3] for i in range(3))
