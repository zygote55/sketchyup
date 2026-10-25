#!/usr/bin/env python3
"""Reproduce and check the vendored OpenSKP parser subset in third_party/openskp (ADR 0176).

  vendor-openskp.py fetch DIR
      Download the pinned OpenSKP and miniz commit archives into DIR (a new, empty directory)
      and check the commit and SHA-256 recorded in each.
  vendor-openskp.py check DIR
      Fail unless third_party/openskp equals the pinned archives' subset, byte for byte: the
      SUBSET file list below, the two recorded patches applied to their upstream files, and the
      miniz files at their pinned commit.
  vendor-openskp.py update DIR
      Rewrite third_party/openskp from the archives (the OWN files are kept).
  vendor-openskp.py pin
      Check that the pins recorded in VENDOR.txt, PATCHES.md and dependencies.json agree with
      this script.

The archives are untrusted data: they are read with tarfile in memory-safe fashion (regular files
only, no absolute or parent paths) and nothing from them is executed.
"""
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TREE = ROOT / "third_party" / "openskp"
OPENSKP_COMMIT = "23a15e8742e936694bf14aa029cb0e2862dce5b9"
OPENSKP_URL = f"https://github.com/iamahsanmehmood/openskp/archive/{OPENSKP_COMMIT}.tar.gz"
OPENSKP_SHA256 = "5b304cc024b2d72504ac5a6676968166fdd300f180d3f1078efe8b959951e1a5"
MINIZ_COMMIT = "77d0dce8627735138c51770d1799a1ef48f2117d"
MINIZ_URL = f"https://github.com/richgel999/miniz/archive/{MINIZ_COMMIT}.tar.gz"
MINIZ_SHA256 = "baa14349e7c5efb4a493a03af7d2f29d0ef45d87d488567fe5360edb33f8b7ba"

# The parser subset (ADR 0176 section 1): the model, the VFF and MFC containers, the attribute
# and page/dimension decoding, the transforms and the miniz zip support. No exporters, no
# writer, no scene baker, no tinygltf or flatbuffers.
OPENSKP_FILES = {
    "packages/cpp/include/openskp/errors.hpp": "include/openskp/errors.hpp",
    "packages/cpp/include/openskp/instanced_scene.hpp": "include/openskp/instanced_scene.hpp",
    "packages/cpp/include/openskp/model.hpp": "include/openskp/model.hpp",
    "packages/cpp/include/openskp/observability.hpp": "include/openskp/observability.hpp",
    "packages/cpp/include/openskp/parser.hpp": "include/openskp/parser.hpp",
    "packages/cpp/include/openskp/scene.hpp": "include/openskp/scene.hpp",
    "packages/cpp/src/core.cpp": "src/core.cpp",
    "packages/cpp/src/errors.cpp": "src/errors.cpp",
    "packages/cpp/src/geometry.cpp": "src/geometry.cpp",
    "packages/cpp/src/legacy.cpp": "src/legacy.cpp",
    "packages/cpp/src/model.cpp": "src/model.cpp",
    "packages/cpp/src/observability.cpp": "src/observability.cpp",
    "packages/cpp/src/pages_dimensions.cpp": "src/pages_dimensions.cpp",
    "packages/cpp/src/tlv.cpp": "src/tlv.cpp",
    "packages/cpp/src/transforms.cpp": "src/transforms.cpp",
    "packages/cpp/src/vff.cpp": "src/vff.cpp",
    "packages/cpp/src/internal.hpp": "src/internal.hpp",
    "packages/cpp/src/parser.cpp": "src/parser.cpp",
    "packages/cpp/cmake/miniz_export.h.in": "cmake/miniz_export.h.in",
    "LICENSE": "LICENSE",
}
# The two patched files: the patch file is applied to the upstream file and the result is the
# vendored file (see PATCHES.md).
PATCHED = {
    "src/parser.cpp": "patches/parser.cpp.patch",
    "src/internal.hpp": "patches/internal.hpp.patch",
}
MINIZ_FILES = {
    "miniz.h": "miniz/miniz.h",
    "miniz_common.h": "miniz/miniz_common.h",
    "miniz_tdef.c": "miniz/miniz_tdef.c",
    "miniz_tdef.h": "miniz/miniz_tdef.h",
    "miniz_tinfl.c": "miniz/miniz_tinfl.c",
    "miniz_tinfl.h": "miniz/miniz_tinfl.h",
    "miniz.c": "miniz/miniz.c",
    "miniz_zip.c": "miniz/miniz_zip.c",
    "miniz_zip.h": "miniz/miniz_zip.h",
    "LICENSE": "miniz/LICENSE",
}
# SketchyUp's own files inside the vendored directory; they are not part of the upstream tree.
# The files the repository adds on top of the upstream subset (checked as a whole, not
# against the upstream bytes).
OWN = ("VENDOR.txt", "PATCHES.md", "SKP-SOURCE.txt", "patches/", "CMakeLists.txt")


def read_archive(path, commit, sha256):
    """The members of a GitHub commit archive as {path: bytes}, checked against the pin."""
    data = Path(path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != sha256:
        print(f"vendor-openskp: notice: {Path(path).name} SHA-256 is {digest}, "
              f"the pin records {sha256} (same commit; the files are compared one by one)",
              file=sys.stderr)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        recorded = tar.pax_headers.get("comment", "")
        if recorded != commit:
            sys.exit(f"vendor-openskp: {Path(path).name} records commit {recorded!r}, expected {commit}")
        files = {}
        for member in tar:
            if not member.isfile():
                continue
            parts = member.name.split("/", 1)
            if len(parts) != 2 or parts[1].startswith("/") or ".." in parts[1].split("/"):
                sys.exit(f"vendor-openskp: unexpected archive member {member.name!r}")
            files[parts[1]] = tar.extractfile(member).read()
    return files


def apply_patch(source, patch, label):
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory) / "source"
        target.write_bytes(source)
        proc = subprocess.run(["patch", "--batch", "-p0", str(target)], input=patch, capture_output=True)
        if proc.returncode != 0:
            sys.exit(f"vendor-openskp: {label} does not apply to its upstream file:\n{proc.stderr.decode()}")
        return target.read_bytes()


def expected_subset(archives):
    openskp, miniz = archives["openskp"], archives["miniz"]
    out = {}
    for upstream, rel in OPENSKP_FILES.items():
        if upstream not in openskp:
            sys.exit(f"vendor-openskp: the OpenSKP archive has no {upstream}")
        if rel in PATCHED:
            out[rel] = apply_patch(openskp[upstream], (TREE / PATCHED[rel]).read_bytes(),
                                   f"{PATCHED[rel]} for {upstream}")
        else:
            out[rel] = openskp[upstream]
    for upstream, rel in MINIZ_FILES.items():
        if upstream not in miniz:
            sys.exit(f"vendor-openskp: the miniz archive has no {upstream}")
        out[rel] = miniz[upstream]
    return out


def tree_files():
    out = {}
    for path in sorted(TREE.rglob("*")):
        if path.is_file() and not path.is_symlink():
            rel = path.relative_to(TREE).as_posix()
            if not any(rel == own or (own.endswith("/") and rel.startswith(own)) for own in OWN):
                out[rel] = path
    return out


def check(archives):
    expected = expected_subset(archives)
    actual = tree_files()
    problems = [f"missing from the tree: {p}" for p in sorted(set(expected) - set(actual))]
    problems += [f"not in the expected subset: {p}" for p in sorted(set(actual) - set(expected))]
    for path in sorted(set(expected) & set(actual)):
        if actual[path].read_bytes() != expected[path]:
            problems.append(f"differs from the pinned upstream: {p}")
    for problem in problems[:40]:
        print(f"vendor-openskp: {problem}", file=sys.stderr)
    if problems:
        return 1
    size = sum(len(data) for data in expected.values())
    print(f"third_party/openskp is {OPENSKP_COMMIT[:8]} (plus miniz {MINIZ_COMMIT[:8]}) reduced to "
          f"the parser subset: {len(expected)} files, {size} bytes.")
    return 0


def update(archives):
    expected = expected_subset(archives)
    for path, _ in tree_files().items():
        if path not in expected:
            path.unlink()
    for path, data in expected.items():
        target = TREE / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return check(archives)


def fetch(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        sys.exit(f"{directory} is not empty; use a new directory")
    for name, url, commit, sha in (
        ("openskp.tar.gz", OPENSKP_URL, OPENSKP_COMMIT, OPENSKP_SHA256),
        ("miniz.tar.gz", MINIZ_URL, MINIZ_COMMIT, MINIZ_SHA256)):
        target = directory / name
        with urllib.request.urlopen(url, timeout=300) as response:
            target.write_bytes(response.read())
        read_archive(target, commit, sha)
    print(directory)
    return 0


def pin():
    errors = []
    vendor = (TREE / "VENDOR.txt").read_text(encoding="utf-8")
    for label, value in (
        ("OpenSKP commit", OPENSKP_COMMIT),
        ("OpenSKP archive SHA-256", OPENSKP_SHA256),
        ("miniz commit", MINIZ_COMMIT),
        ("miniz archive SHA-256", MINIZ_SHA256)):
        if value not in vendor:
            errors.append(f"VENDOR.txt does not record the {label} {value}")
    patches = (TREE / "PATCHES.md").read_text(encoding="utf-8")
    for patch in PATCHED.values():
        if patch not in patches:
            errors.append(f"PATCHES.md does not record {patch}")
    dependencies = json.loads((ROOT / "dependencies.json").read_text(encoding="utf-8"))
    entry = next((d for d in dependencies["dependencies"] if d["name"] == "OpenSKP"), None)
    if entry is None:
        errors.append("dependencies.json has no OpenSKP entry")
    else:
        if entry.get("commit") != OPENSKP_COMMIT or entry.get("archiveSha256") != OPENSKP_SHA256:
            errors.append("dependencies.json pins another OpenSKP commit or archive")
    for error in errors:
        print(f"vendor-openskp: {error}", file=sys.stderr)
    if not errors:
        print(f"Pins agree: OpenSKP {OPENSKP_COMMIT[:8]}, miniz {MINIZ_COMMIT[:8]}.")
    return 1 if errors else 0


def main(argv):
    if len(argv) == 3 and argv[1] == "fetch":
        return fetch(argv[2])
    if len(argv) == 3 and argv[1] == "check":
        return check({
            "openskp": read_archive(Path(argv[2]) / "openskp.tar.gz", OPENSKP_COMMIT, OPENSKP_SHA256),
            "miniz": read_archive(Path(argv[2]) / "miniz.tar.gz", MINIZ_COMMIT, MINIZ_SHA256)})
    if len(argv) == 3 and argv[1] == "update":
        return update({
            "openskp": read_archive(Path(argv[2]) / "openskp.tar.gz", OPENSKP_COMMIT, OPENSKP_SHA256),
            "miniz": read_archive(Path(argv[2]) / "miniz.tar.gz", MINIZ_COMMIT, MINIZ_SHA256)})
    if len(argv) == 2 and argv[1] == "pin":
        return pin()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
