#!/usr/bin/env python3
"""Checks of the SketchUp (.skp) helper's linkage and notices (ADR 0176 sections 1 and 9, R102.d).

  verify-skp-library.py install --cmake CMAKE --build-dir DIR
      Install the main component of a build into a scratch prefix (DESTDIR, as makepkg does) and
      check it, with symbols (the CTest test skp_library_install; Arch package acceptance runs it
      on the unstripped build tree, because makepkg strips the installed binaries).
  verify-skp-library.py package --prefix /usr
      Check an installed package by its dynamic linkage and its files (Arch package acceptance).
  verify-skp-library.py source --source-dir DIR [--pkgbuild PATH]
      The source-only checks: the pins, the notice install lines, the source record and the helper's
      install rule (the CTest test skp_library_source, in a git checkout only).

L1  Only sketchyup-skp-worker links the parser (OpenSKP and its miniz). Its symbols (static link;
    install mode) or its DT_NEEDED entry (shared build) identify the link; no other installed
    executable has either.
L2  The worker's DT_NEEDED entries are the C and C++ runtime, and in a package every library it
    loads resolves to the system (ldd). A shared build names the parser library, so its closure is
    checked in the static package only.
L3  The notices and the source record are installed: OpenSKP-LICENSE and miniz-LICENSE (MIT) under
    share/licenses/sketchyup, and share/doc/sketchyup/SKP-SOURCE.txt, which names the pinned commit.
    The helper's install rule carries no component, so the main package installs it.

Everything installed is untrusted data: the checks only read it, and run no part of it.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKER = "sketchyup-skp-worker"
PARSER_LIBRARY = re.compile(r"libsketchyup_openskp\.so")
PARSER_SYMBOL = re.compile(r"_ZN7openskp|\bmz_zip_")
RUNTIME = ("libc.so.", "libm.so.", "libgcc_s.so.", "libstdc++.so.")
PIN = "23a15e8742e936694bf14aa029cb0e2862dce5b9"
ARCHIVE_SHA = "5b304cc024b2d72504ac5a6676968166fdd300f180d3f1078efe8b959951e1a5"


def is_elf(path):
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def needed(path):
    out = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True).stdout
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]*)\]", out)


def symbols(path):
    out = subprocess.run(["nm", str(path)], capture_output=True, text=True).stdout
    return [line.split()[-1] for line in out.splitlines() if line.split()]


def executables(prefix):
    """SketchyUp's executables in the prefix: sketchyup* in bin, and every helper in lib/sketchyup."""
    found = []
    bin_dir = prefix / "bin"
    if bin_dir.is_dir():
        found += [p for p in sorted(bin_dir.iterdir())
                  if p.name.startswith("sketchyup") and p.is_file() and not p.is_symlink() and is_elf(p)]
    private = prefix / "lib" / "sketchyup"
    if private.is_dir():
        found += [p for p in sorted(private.iterdir()) if p.is_file() and not p.is_symlink() and is_elf(p)]
    return found


def check_l1(prefix, problems, with_symbols):
    worker = prefix / "lib" / "sketchyup" / WORKER
    if not worker.is_file():
        problems.append(f"L1: {worker} is missing")
        return 0
    # The worker's own link is seen only with symbols: a stripped static package has neither.
    if with_symbols and not (any(PARSER_LIBRARY.search(n) for n in needed(worker))
                             or any(PARSER_SYMBOL.search(s) for s in symbols(worker))):
        problems.append(f"L1: {WORKER} links no parser: neither a parser library nor an OpenSKP symbol")
    checked = 0
    for path in executables(prefix):
        if path.name == WORKER:
            continue
        checked += 1
        if any(PARSER_LIBRARY.search(n) for n in needed(path)):
            problems.append(f"L1: {path} names the parser library; only {WORKER} may link the parser")
        if with_symbols and any(PARSER_SYMBOL.search(s) for s in symbols(path)):
            problems.append(f"L1: {path} carries OpenSKP or miniz symbols; only {WORKER} may link the parser")
    return checked


def check_l2(prefix, problems, with_ldd):
    worker = prefix / "lib" / "sketchyup" / WORKER
    names = needed(worker)
    if any(PARSER_LIBRARY.search(n) for n in names):
        # A shared build (SKETCHYUP_SHARED_LIBS): the parser is a library the install does not carry,
        # so its closure is checked by the package mode of the static release build instead.
        print("verify-skp-library: a shared parser library; the closure is checked in the static package",
              file=sys.stderr)
        return
    stray = [n for n in names if not n.startswith(RUNTIME)]
    if stray:
        problems.append(f"L2: {WORKER} needs {stray}; only the C and C++ runtime is allowed")
    if with_ldd:
        out = subprocess.run(["ldd", str(worker)], capture_output=True, text=True,
                             env={"PATH": "/usr/bin:/bin"}).stdout
        for line in out.splitlines():
            if "not found" in line:
                problems.append(f"L2: {WORKER} cannot find a library: {line.strip()}")
            match = re.search(r"=> (\S+)", line)
            if match and str(prefix) in match.group(1):
                problems.append(f"L2: {WORKER} loads {match.group(1)} from the package itself")


def check_l3(prefix, problems):
    notices = prefix / "share" / "licenses" / "sketchyup"
    for name in ("OpenSKP-LICENSE", "miniz-LICENSE"):
        path = notices / name
        if not path.is_file() or path.stat().st_size == 0:
            problems.append(f"L3: {path} is missing or empty")
            continue
        if "Permission is hereby granted" not in path.read_text(encoding="utf-8", errors="replace"):
            problems.append(f"L3: {path} is not the MIT licence text")
    record = prefix / "share" / "doc" / "sketchyup" / "SKP-SOURCE.txt"
    if not record.is_file():
        problems.append(f"L3: {record} is missing")
    else:
        text = record.read_text(encoding="utf-8")
        for needle in (PIN, ARCHIVE_SHA):
            if needle not in text:
                problems.append(f"L3: SKP-SOURCE.txt does not record {needle}")


def run_checks(prefix, with_symbols, with_ldd):
    problems = []
    summary = {"L1": check_l1(prefix, problems, with_symbols)}
    check_l2(prefix, problems, with_ldd)
    check_l3(prefix, problems)
    return problems, summary


def report(problems, summary):
    for problem in problems:
        print(f"verify-skp-library: {problem}", file=sys.stderr)
    if problems:
        print(f"{len(problems)} problem(s).", file=sys.stderr)
        return 1
    print("Checks L1 to L3 passed: " + repr(summary))
    return 0


def check_source(source, pkgbuild_path, problems):
    result = subprocess.run([sys.executable, "-I", str(source / "scripts" / "vendor-openskp.py"), "pin"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        problems.append("L3: " + result.stderr.strip().replace("\n", "; "))
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    for line in ("install(FILES third_party/openskp/LICENSE DESTINATION share/licenses/sketchyup RENAME OpenSKP-LICENSE)",
                 "install(FILES third_party/openskp/miniz/LICENSE DESTINATION share/licenses/sketchyup RENAME miniz-LICENSE)",
                 "install(FILES third_party/openskp/SKP-SOURCE.txt DESTINATION share/doc/sketchyup)"):
        if line not in cmake:
            problems.append(f"L3: CMakeLists.txt lacks the install line {line!r}")
    helper = (source / "src" / "skp" / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r"^install\(TARGETS sketchyup-skp-worker RUNTIME DESTINATION lib/sketchyup\)$", helper, re.M):
        problems.append("L3: the helper's install rule must have no COMPONENT (it belongs to the main package)")
    pkgbuild = Path(pkgbuild_path or source / "packaging" / "arch" / "PKGBUILD").read_text(encoding="utf-8")
    licenses = re.search(r"^license=\(([^)]*)\)", pkgbuild, re.M)
    if not licenses or "'MIT'" not in licenses.group(1):
        problems.append("L3: the PKGBUILD licence list lacks MIT, which OpenSKP and miniz are under")
    text = (source / "third_party" / "openskp" / "SKP-SOURCE.txt").read_text(encoding="utf-8")
    for needle in (PIN, ARCHIVE_SHA, "MIT", "Trimble"):
        if needle not in text:
            problems.append(f"L3: third_party/openskp/SKP-SOURCE.txt does not mention {needle!r}")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    install = sub.add_parser("install")
    install.add_argument("--cmake", required=True)
    install.add_argument("--build-dir", required=True)
    package = sub.add_parser("package")
    package.add_argument("--prefix", default="/usr")
    src = sub.add_parser("source")
    src.add_argument("--source-dir", required=True)
    src.add_argument("--pkgbuild")
    args = parser.parse_args(argv[1:])
    if args.mode == "source":
        problems = []
        check_source(Path(args.source_dir).resolve(), args.pkgbuild, problems)
        for problem in problems:
            print(f"verify-skp-library: {problem}", file=sys.stderr)
        if problems:
            print(f"{len(problems)} problem(s).", file=sys.stderr)
            return 1
        print("Checks L3 (source) passed: the pins, the notice lines, the source record and the helper's rule.")
        return 0
    if args.mode == "install":
        build = Path(args.build_dir).resolve()
        with tempfile.TemporaryDirectory(prefix="skp-install-") as scratch:
            # Like makepkg: DESTDIR stages the files, and the prefix is the final /usr.
            stage = Path(scratch) / "stage"
            done = subprocess.run([args.cmake, "--install", str(build), "--component", "Unspecified",
                                   "--prefix", "/usr"], capture_output=True, text=True,
                                  env={"PATH": "/usr/bin:/bin", "DESTDIR": str(stage)})
            if done.returncode != 0:
                print(done.stdout + done.stderr, file=sys.stderr)
                return 1
            problems, summary = run_checks(stage / "usr", with_symbols=True, with_ldd=False)
        return report(problems, summary)
    problems, summary = run_checks(Path(args.prefix), with_symbols=False, with_ldd=True)
    return report(problems, summary)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
