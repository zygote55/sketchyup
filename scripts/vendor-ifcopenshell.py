#!/usr/bin/env python3
"""Reproduce and check the trimmed IfcOpenShell tree in third_party/ifcopenshell (ADR 0171).

  vendor-ifcopenshell.py fetch DIR
      Download the pinned upstream tag archive into DIR (a new, empty directory) and check its
      SHA-256 and the commit recorded in it.
  vendor-ifcopenshell.py check ARCHIVE
      Fail unless third_party/ifcopenshell is exactly the upstream archive trimmed by TRIM.txt,
      plus SketchyUp's own files (the OWN list below), byte for byte.
  vendor-ifcopenshell.py update ARCHIVE
      Rewrite third_party/ifcopenshell from the archive (own files are kept).
  (add --strict-archive to fail when the archive's SHA-256 differs from the pin)
  vendor-ifcopenshell.py pin
      Check that the pin recorded in README.sketchyup.md, dependencies.json and (once it exists)
      the CMake sub-project agree with this script (verify L3 and L4 of ADR 0171 section 6.3).

The archive is untrusted data: it is read with tarfile in memory-safe fashion (regular files
only, no absolute or parent paths) and nothing from it is executed.
"""
import hashlib
import io
import json
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TREE = ROOT / "third_party" / "ifcopenshell"
TAG = "ifcopenshell-v0.9.0"
COMMIT = "7ba701972f7182322e0a00db8f0e1f3de5f69840"
ARCHIVE_SHA256 = "9e6a5fb2cb10be248058c8e62f8f511466eb9fc828e40a1f4400ec162eef2c4b"
URL = f"https://github.com/IfcOpenShell/IfcOpenShell/archive/refs/tags/{TAG}.tar.gz"
# SketchyUp's own files inside the vendored directory; they are not part of the upstream tree.
STRICT_ARCHIVE = "--strict-archive" in sys.argv
OWN = ("README.sketchyup.md", "PATCHES.md", "TRIM.txt", "IFC-SOURCE.txt", "occt/")


def glob_regex(pattern):
    out, i = "", 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out, i = out + ".*", i + 2
        elif pattern[i] == "*":
            out, i = out + "[^/]*", i + 1
        else:
            out, i = out + re.escape(pattern[i]), i + 1
    return re.compile(out + r"\Z")


def trim_rules():
    rules = []
    for line in (TREE / "TRIM.txt").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        verb, pattern = line.split(None, 1)
        if verb == "keep":
            # A directory keeps everything under it; a file keeps itself.
            regex = glob_regex(pattern + "**") if pattern.endswith("/") else glob_regex(pattern)
            rules.append((True, regex))
        elif verb == "drop":
            rules.append((False, glob_regex(pattern)))
        else:
            sys.exit(f"TRIM.txt: unknown rule {line!r}")
    return rules


def keeps(rules, path):
    decision = False
    for keep, regex in rules:
        if regex.match(path):
            decision = keep
    return decision


def read_archive(archive):
    """The members of the upstream tag archive as {path: (bytes, executable)}.

    The pin is the commit the archive records and the bytes of every vendored file. The archive's
    own SHA-256 is checked too, but GitHub does not promise to compress tag archives the same way
    for ever: a different digest with the same commit and the same files is reported as a notice,
    not an error (--strict-archive makes it one)."""
    data = Path(archive).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        commit = tar.pax_headers.get("comment", "")
        if commit != COMMIT:
            sys.exit(f"archive records commit {commit!r}, expected {COMMIT}")
        for member in tar:
            if not member.isfile():
                continue
            parts = member.name.split("/", 1)
            if len(parts) != 2 or parts[1].startswith("/") or ".." in parts[1].split("/"):
                sys.exit(f"unexpected archive member {member.name!r}")
            files[parts[1]] = (tar.extractfile(member).read(), member.mode & 0o111 != 0)
    if digest != ARCHIVE_SHA256:
        message = f"archive SHA-256 is {digest}, the pin records {ARCHIVE_SHA256}"
        if STRICT_ARCHIVE:
            sys.exit(message)
        print(f"vendor-ifcopenshell: notice: {message} (same commit; the files are compared one by one)",
              file=sys.stderr)
    return files


def vendored(files):
    rules = trim_rules()
    return {path: value for path, value in files.items() if keeps(rules, path)}


def tree_files():
    out = {}
    for path in sorted(TREE.rglob("*")):
        if path.is_file() and not path.is_symlink():
            rel = path.relative_to(TREE).as_posix()
            if not any(rel == own or (own.endswith("/") and rel.startswith(own)) for own in OWN):
                out[rel] = path
    return out


def check(archive):
    expected = vendored(read_archive(archive))
    actual = tree_files()
    problems = [f"missing from the tree: {p}" for p in sorted(set(expected) - set(actual))]
    problems += [f"not in the trimmed upstream tree: {p}" for p in sorted(set(actual) - set(expected))]
    for path in sorted(set(expected) & set(actual)):
        if actual[path].read_bytes() != expected[path][0]:
            problems.append(f"differs from upstream: {path} (record the change in PATCHES.md)")
    for problem in problems[:40]:
        print(f"vendor-ifcopenshell: {problem}", file=sys.stderr)
    if problems:
        return 1
    size = sum(len(data) for data, _ in expected.values())
    print(f"third_party/ifcopenshell equals {TAG} ({COMMIT[:8]}) trimmed by TRIM.txt: "
          f"{len(expected)} files, {size} bytes.")
    return 0


def update(archive):
    expected = vendored(read_archive(archive))
    for path, source in tree_files().items():
        if path not in expected:
            source.unlink()
    for path, (data, _) in expected.items():
        target = TREE / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return check(archive)


def fetch(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        sys.exit(f"{directory} is not empty; use a new directory")
    target = directory / f"{TAG}.tar.gz"
    with urllib.request.urlopen(URL, timeout=120) as response:
        target.write_bytes(response.read())
    read_archive(target)
    print(target)
    return 0


def pin():
    errors = []
    readme = (TREE / "README.sketchyup.md").read_text(encoding="utf-8")
    for label, value in (("tag", TAG), ("commit", COMMIT), ("archive SHA-256", ARCHIVE_SHA256)):
        if value not in readme:
            errors.append(f"README.sketchyup.md does not record the {label} {value}")
    dependencies = json.loads((ROOT / "dependencies.json").read_text(encoding="utf-8"))
    entry = next((d for d in dependencies["dependencies"] if d["name"] == "IfcOpenShell"), None)
    if entry is None:
        errors.append("dependencies.json has no IfcOpenShell entry")
    else:
        if entry.get("commit") != COMMIT or entry.get("archiveSha256") != ARCHIVE_SHA256:
            errors.append("dependencies.json pins another commit or archive")
        if entry.get("version") != (TREE / "VERSION").read_text(encoding="utf-8").strip():
            errors.append("dependencies.json version differs from third_party/ifcopenshell/VERSION")
    cmake_path = ROOT / "src" / "ifc" / "CMakeLists.txt"  # absent until the CMake sub-project lands
    if cmake_path.exists() and f"SKETCHYUP_IFC_PIN_COMMIT {COMMIT}" not in cmake_path.read_text(encoding="utf-8"):
        errors.append("src/ifc/CMakeLists.txt pins another commit")
    for error in errors:
        print(f"vendor-ifcopenshell: {error}", file=sys.stderr)
    if not errors:
        print(f"Pin {TAG} {COMMIT} agrees in README.sketchyup.md, dependencies.json and CMake.")
    return 1 if errors else 0


def main(argv):
    argv = [a for a in argv if a != "--strict-archive"]
    if len(argv) == 3 and argv[1] == "fetch":
        return fetch(argv[2])
    if len(argv) == 3 and argv[1] == "check":
        return check(argv[2])
    if len(argv) == 3 and argv[1] == "update":
        return update(argv[2])
    if len(argv) == 2 and argv[1] == "pin":
        return pin()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
