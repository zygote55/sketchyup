#!/usr/bin/env python3
"""Checks L1 to L6 of ADR 0171 section 6.3 (library packaging and licence).

L3 (the pin) and L4 need the repository files; they run in `source` mode.

  verify-ifc-library.py install --cmake CMAKE --build-dir DIR --source-dir DIR
      Install the ifc component of a build into a scratch prefix and check it (the CTest test
      ifc_library_install; it runs inside `makepkg check()`, from the source archive, so it reads
      only files the archive has).
  verify-ifc-library.py package --prefix /usr --source-dir DIR [--other-executables PATH...]
      Check an installed package (Arch package acceptance): the files under PREFIX, and that
      no other SketchyUp executable links IfcOpenShell or OpenCASCADE.
  verify-ifc-library.py source --source-dir DIR [--pkgbuild PATH]
      The source-only checks: the pin, dependencies.json, the PKGBUILD licences and the notice
      install lines (L3 and L4). The CTest test ifc_library_source (a git checkout) and package
      acceptance run it; --pkgbuild names the PKGBUILD when it is not packaging/arch/PKGBUILD.

L1  Only sketchyup-ifc-worker links IfcOpenShell or OpenCASCADE.
L2  The worker and the libraries have a RUNPATH of $ORIGIN (not an RPATH) and a copy of the
    libraries found through LD_LIBRARY_PATH is the one the worker loads.
L3  The notices and the written source offer are installed (naming the OpenCASCADE tag of the
    libraries the helper loads); the pinned commit agrees in the README, dependencies.json and CMake
    (`source` mode).
L4  THIRD_PARTY_NOTICES lines, the PKGBUILD licences and dependencies.json are updated (`source` mode).
L5  No GPL-3.0-or-later library is linked (ldd over the worker and the private libraries).
L6  The OpenCASCADE libraries the kernel plugin records are exactly the set installed; with the
    kernel plugin or any one of them removed, the probe reports "kernel unavailable" (exit 0, no
    signal) and the pass-through kernel still works.

Everything installed is untrusted data to the checks: they only read it, and run the helper's own
--probe in a scratch copy.
"""
import argparse
import json
import os
import re
import resource
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRIVATE = Path("lib") / "sketchyup"
WORKER = "sketchyup-ifc-worker"
LIBRARY_PATTERNS = (r"libifcopenshell\.", r"ifcopenshell_", r"libTK")


def is_elf(path):
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def dynamic(path):
    """(needed, runpath, rpath) of an ELF file from readelf -d."""
    out = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True).stdout
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]*)\]", out)
    runpath = re.findall(r"\(RUNPATH\)\s+Library runpath: \[([^\]]*)\]", out)
    rpath = re.findall(r"\(RPATH\)\s+Library rpath: \[([^\]]*)\]", out)
    return needed, runpath, rpath


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def probe(worker, env_extra=None, timeout=120):
    env = {"PATH": "/usr/bin:/bin"}
    env.update(env_extra or {})
    result = subprocess.run([str(worker), "--probe"], capture_output=True, text=True, env=env,
                            timeout=timeout, preexec_fn=no_core)
    return result


def links_ifc(needed):
    return [n for n in needed if any(re.match(p, n) for p in LIBRARY_PATTERNS)]


def occt_closure(private):
    """The libTK* closure that the kernel plugin records, by DT_NEEDED."""
    kernel = private / "ifcopenshell_geometry_kernel_opencascade.so"
    pending = [n for n in dynamic(kernel)[0] if n.startswith("libTK")]
    closure = set()
    while pending:
        name = pending.pop()
        if name in closure:
            continue
        closure.add(name)
        library = private / name
        if library.exists():
            pending += [n for n in dynamic(library)[0] if n.startswith("libTK")]
    return closure


def check_l1(prefix, others, problems):
    private = prefix / PRIVATE
    worker = private / WORKER
    if not worker.exists():
        problems.append(f"L1: {worker} is missing")
        return
    if not links_ifc(dynamic(worker)[0]):
        problems.append("L1: the worker does not link libifcopenshell")
    candidates = list(others)
    for directory in (prefix / "bin", private):
        if directory.is_dir():
            candidates += [p for p in directory.iterdir() if p.is_file()]
    checked = 0
    for path in candidates:
        if path.name == WORKER or not is_elf(path):
            continue
        if any(re.match(p, path.name) for p in LIBRARY_PATTERNS):
            continue
        checked += 1
        linked = links_ifc(dynamic(path)[0])
        if linked:
            problems.append(f"L1: {path} links {linked}; only {WORKER} may")
    return checked


def check_l2(prefix, problems):
    private = prefix / PRIVATE
    files = [private / WORKER] + sorted(p for p in private.iterdir()
                                         if p.is_file() and not p.is_symlink() and is_elf(p)
                                         and any(re.match(x, p.name) for x in LIBRARY_PATTERNS))
    for path in files:
        needed, runpath, rpath = dynamic(path)
        if rpath:
            problems.append(f"L2: {path.name} has an RPATH {rpath}: LD_LIBRARY_PATH could not override it")
        # A library finds its dependencies through its own RUNPATH; one that needs none of ours
        # (the plugin loader needs only the C++ runtime) has none.
        if (path.name == WORKER or links_ifc(needed) or any(n.startswith("libTK") for n in needed)) \
                and "$ORIGIN" not in ":".join(runpath):
            problems.append(f"L2: {path.name} has no RUNPATH $ORIGIN (has {runpath})")
        if runpath and any(entry != "$ORIGIN" for entry in ":".join(runpath).split(":")):
            problems.append(f"L2: {path.name} has RUNPATH {runpath}, expected exactly $ORIGIN")
    with tempfile.TemporaryDirectory(prefix="ifc-l2-") as scratch:
        copy = Path(scratch) / "replacement"
        shutil.copytree(private, copy, symlinks=True)
        result = probe(private / WORKER, {"LD_LIBRARY_PATH": str(copy)})
        if result.returncode != 0:
            problems.append(f"L2: the probe failed with LD_LIBRARY_PATH set: {result.stderr[:300]}")
            return
        loaded = json.loads(result.stdout)["loadedLibraries"]
        parse = [p for p in loaded if "libifcopenshell.parse" in p]
        if not parse or not all(str(copy.resolve()) in p for p in parse):
            problems.append(f"L2: LD_LIBRARY_PATH did not replace libifcopenshell.parse: loaded {parse}")
        plugins = [p for p in loaded if "ifcopenshell_" in p]
        if not plugins or not all(str(copy.resolve()) in p for p in plugins):
            problems.append(f"L2: the plugins were not loaded from the replacement directory: {plugins}")


def check_l3(prefix, problems, occt_version=None):
    expected = [prefix / "share/licenses/sketchyup" / n for n in (
        "IfcOpenShell-COPYING.LESSER", "IfcOpenShell-COPYING", "OpenCASCADE-LICENSE",
        "OpenCASCADE-LGPL-EXCEPTION")] + [prefix / "share/doc/sketchyup/IFC-SOURCE.txt"]
    for path in expected:
        if not path.is_file() or path.stat().st_size == 0:
            problems.append(f"L3: {path} is missing or empty")
    offer = prefix / "share/doc/sketchyup/IFC-SOURCE.txt"
    if offer.is_file():
        text = offer.read_text(encoding="utf-8")
        for needle in ("7ba701972f7182322e0a00db8f0e1f3de5f69840", "three years", "Open CASCADE", "LD_LIBRARY_PATH"):
            if needle not in text:
                problems.append(f"L3: IFC-SOURCE.txt does not mention {needle!r}")
        if occt_version:
            tag = "V" + occt_version.replace(".", "_")
            if tag not in text:
                problems.append(f"L3: IFC-SOURCE.txt names no source tag {tag} for the OpenCASCADE {occt_version} the helper loads")
    lesser = prefix / "share/licenses/sketchyup/IfcOpenShell-COPYING.LESSER"
    if lesser.is_file() and "GNU LESSER GENERAL PUBLIC LICENSE" not in lesser.read_text(encoding="utf-8").upper():
        problems.append("L3: IfcOpenShell-COPYING.LESSER is not the LGPL text")


def check_pin(source, problems):
    result = subprocess.run([sys.executable, "-I", str(source / "scripts" / "vendor-ifcopenshell.py"), "pin"],
                            capture_output=True, text=True)
    if result.returncode != 0:
        problems.append("L3: " + result.stderr.strip().replace("\n", "; "))


def check_l4(source, problems, pkgbuild_path=None):
    pkgbuild = Path(pkgbuild_path or source / "packaging" / "arch" / "PKGBUILD").read_text(encoding="utf-8")
    licenses = re.search(r"^license=\(([^)]*)\)", pkgbuild, re.M)
    names = re.findall(r"'([^']*)'", licenses.group(1)) if licenses else []
    for needed in ("LGPL-3.0-or-later", "LGPL-2.1-or-later"):
        if needed not in names:
            problems.append(f"L4: the PKGBUILD licence list lacks {needed}")
    dependencies = json.loads((source / "dependencies.json").read_text(encoding="utf-8"))
    entries = {d["name"]: d for d in dependencies["dependencies"]}
    for name in ("IfcOpenShell", "OpenCASCADE Technology"):
        if name not in entries:
            problems.append(f"L4: dependencies.json has no {name}")
    ifc = entries.get("IfcOpenShell", {})
    for key in ("version", "commit", "linkage", "license", "source", "patches"):
        if key not in ifc:
            problems.append(f"L4: the IfcOpenShell entry of dependencies.json lacks {key}")
    # THIRD_PARTY_NOTICES is built from these lines (scripts/release-artifacts.sh).
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    pattern = re.compile(r"^install\(FILES (\S+) DESTINATION share/licenses/sketchyup"
                         r"(?: RENAME (\S+?))?(?: COMPONENT [A-Za-z]+)?\)$", re.M)
    installed = {m.group(2): m.group(1) for m in pattern.finditer(cmake)}
    for name, origin in (("IfcOpenShell-COPYING.LESSER", "third_party/ifcopenshell/COPYING.LESSER"),
                         ("IfcOpenShell-COPYING", "third_party/ifcopenshell/COPYING"),
                         ("OpenCASCADE-LICENSE", "third_party/ifcopenshell/occt/LICENSE_LGPL_21.txt"),
                         ("OpenCASCADE-LGPL-EXCEPTION", "third_party/ifcopenshell/occt/OCCT_LGPL_EXCEPTION.txt")):
        if installed.get(name) != origin:
            problems.append(f"L4: THIRD_PARTY_NOTICES would not include {name} from {origin}")
        elif not (source / origin).is_file():
            problems.append(f"L4: {origin} is missing")


GPL3 = re.compile(r"^(A?GPL-3\.0(-only|-or-later)?|GPL3|AGPL3)$")


def package_owner(path):
    if shutil.which("pacman") is None:
        return None
    owner = subprocess.run(["pacman", "-Qoq", str(path)], capture_output=True, text=True)
    return owner.stdout.split()[0] if owner.returncode == 0 and owner.stdout.strip() else None


def package_licenses(path):
    """Licence identifiers of the Arch package owning path, or None when that cannot be told."""
    if shutil.which("pacman") is None:
        return None
    owner = subprocess.run(["pacman", "-Qoq", str(path)], capture_output=True, text=True)
    if owner.returncode != 0 or not owner.stdout.strip():
        return None
    info = subprocess.run(["pacman", "-Qi", owner.stdout.strip().split()[0]], capture_output=True, text=True).stdout
    match = re.search(r"^Licenses\s*:\s*(.*)$", info, re.M)
    return match.group(1).split() if match else None


def check_l5(prefix, problems):
    private = prefix / PRIVATE
    targets = [private / WORKER] + sorted(p for p in private.iterdir()
                                          if p.is_file() and not p.is_symlink() and is_elf(p)
                                          and any(re.match(x, p.name) for x in LIBRARY_PATTERNS))
    seen = set()
    # gcc-libs is a set of packages (libgcc, libstdc++ and others) on current Arch
    allowed_owners = {"glibc", "gcc-libs", "libgcc", "libstdc++"}
    for target in targets:
        if any(n.startswith("libboost") for n in dynamic(target)[0]):
            problems.append(f"L5: {target.name} records a Boost library; the package does not depend on boost-libs")
        out = subprocess.run(["ldd", str(target)], capture_output=True, text=True,
                             env={"PATH": "/usr/bin:/bin"}).stdout
        for line in out.splitlines():
            match = re.match(r"\s*(\S+) => (\S+)", line)
            if not match:
                continue
            name, resolved = match.groups()
            if resolved == "not":
                problems.append(f"L5: {target.name} needs {name}, which cannot be found")
                continue
            real = os.path.realpath(resolved)
            if real in seen or real.startswith(str(private.resolve())):
                continue
            seen.add(real)
            if re.search(r"CGAL|libgmp", name):
                problems.append(f"L5: {target.name} links {name}")
            licenses = package_licenses(real)
            if licenses is None:
                continue
            # An entry is acceptable alone (LGPL, an exception, a permissive licence), or
            # together with an LGPL or exception entry; a bare GPL-3.0 entry is not.
            bare = [l for l in licenses if GPL3.match(l)]
            alternatives = [l for l in licenses if "LGPL" in l or "exception" in l.lower() or "WITH" in l]
            if bare and not alternatives:
                problems.append(f"L5: {target.name} links {name} ({real}), licensed {licenses}")
            owner = package_owner(real)
            if owner and owner not in allowed_owners:
                problems.append(f"L5: {target.name} links {name} from the package {owner}, which "
                                f"sketchyup-ifc does not depend on (add it to the PKGBUILD or stop linking it)")
    return len(seen)


def check_l6(prefix, problems):
    private = prefix / PRIVATE
    worker = private / WORKER
    base = probe(worker)
    if base.returncode != 0:
        problems.append(f"L6: the probe failed: {base.stderr[:300]}")
        return
    info = json.loads(base.stdout)
    if not info["kernelAvailable"]:
        problems.append("L6: the OpenCASCADE kernel is unavailable in the installed tree: "
                        + json.dumps([k for k in info["kernels"] if k["name"] == "opencascade"]))
        return
    closure = occt_closure(private)
    installed = {p.name for p in private.iterdir() if p.name.startswith("libTK") and p.is_file()
                 and not p.is_symlink()}
    soname_links = {p.name for p in private.iterdir() if p.name.startswith("libTK") and p.is_symlink()}
    recorded = closure
    present = installed | soname_links
    if recorded - present:
        problems.append(f"L6: libraries the kernel records are not installed: {sorted(recorded - present)}")
    surplus = {n for n in soname_links if n not in recorded}
    if surplus:
        problems.append(f"L6: libraries installed but not recorded by the kernel: {sorted(surplus)}")
    # Remove the kernel plugin, then each OpenCASCADE library in turn, in a scratch copy.
    with tempfile.TemporaryDirectory(prefix="ifc-l6-") as scratch:
        victims = ["ifcopenshell_geometry_kernel_opencascade.so"] + sorted(recorded)
        patchelf = shutil.which("patchelf")
        for victim in victims:
            copy = Path(scratch) / "private"
            if copy.exists():
                shutil.rmtree(copy)
            shutil.copytree(private, copy, symlinks=True)
            if victim.endswith(".so"):
                (copy / victim).unlink()
            else:
                # The machine running this check usually has OpenCASCADE installed, which would
                # satisfy a deleted private copy from /usr/lib; so make every dependent look for
                # a name that exists nowhere instead (the same as the library being absent).
                if patchelf is None:
                    problems.append("L6: patchelf is needed to simulate a missing OpenCASCADE library")
                    break
                for file in copy.iterdir():
                    if file.is_file() and not file.is_symlink() and is_elf(file) and victim in dynamic(file)[0]:
                        subprocess.run([patchelf, "--replace-needed", victim, victim + ".gone", str(file)], check=True)
            result = probe(copy / WORKER)
            if result.returncode != 0:
                problems.append(f"L6: with {victim} removed the probe exited {result.returncode}: "
                                f"{result.stderr[:200]}")
                continue
            report = json.loads(result.stdout)
            if report["kernelAvailable"] or report["kernelStatus"] != "kernel unavailable":
                problems.append(f"L6: with {victim} removed the probe still reports the kernel available")
            passthrough = [k for k in report["kernels"] if k["name"] == "passthrough"]
            if not passthrough or not passthrough[0]["available"]:
                problems.append(f"L6: with {victim} removed the pass-through kernel is unavailable")
    return len(recorded)


def run_checks(prefix, others):
    problems = []
    summary = {}
    summary["L1"] = check_l1(prefix, others, problems)
    check_l2(prefix, problems)
    occt = None
    probed = probe(prefix / PRIVATE / WORKER)
    if probed.returncode == 0:
        occt = json.loads(probed.stdout).get("occtVersion")
    check_l3(prefix, problems, occt)
    summary["L5"] = check_l5(prefix, problems)
    summary["L6"] = check_l6(prefix, problems)
    return problems, summary


def report(problems, summary):
    for problem in problems:
        print(f"verify-ifc-library: {problem}", file=sys.stderr)
    if problems:
        print(f"{len(problems)} problem(s).", file=sys.stderr)
        return 1
    print("Checks L1 to L6 passed: " + json.dumps(summary))
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    install = sub.add_parser("install")
    install.add_argument("--cmake", required=True)
    install.add_argument("--build-dir", required=True)
    install.add_argument("--source-dir")  # accepted and unused: the install checks read no repository file
    package = sub.add_parser("package")
    package.add_argument("--prefix", default="/usr")
    package.add_argument("--source-dir")  # accepted and unused
    package.add_argument("--other-executables", nargs="*", default=[])
    src = sub.add_parser("source")
    src.add_argument("--source-dir", required=True)
    src.add_argument("--pkgbuild")
    args = parser.parse_args(argv[1:])
    source = Path(args.source_dir).resolve() if args.source_dir else None
    if args.mode == "source":
        problems = []
        check_pin(source, problems)
        check_l4(source, problems, args.pkgbuild)
        return report(problems, {"source": "pin, dependencies.json, PKGBUILD licences, notice lines"})
    if args.mode == "install":
        build = Path(args.build_dir).resolve()
        with tempfile.TemporaryDirectory(prefix="ifc-install-") as scratch:
            # Like makepkg: DESTDIR stages the files, the prefix is the final /usr.
            stage = Path(scratch) / "stage"
            prefix = stage / "usr"
            done = subprocess.run([args.cmake, "--install", str(build), "--component", "ifc",
                                   "--prefix", "/usr"], capture_output=True, text=True,
                                  env={**os.environ, "DESTDIR": str(stage)})
            if done.returncode != 0:
                print(done.stdout + done.stderr, file=sys.stderr)
                return 1
            # Every other executable the build made must not link the library either.
            others = [p for p in build.iterdir() if p.is_file() and not p.name.startswith("sketchyup-ifc-worker")]
            problems, summary = run_checks(prefix, others)
        return report(problems, summary)
    prefix = Path(args.prefix)
    problems, summary = run_checks(prefix, [Path(p) for p in args.other_executables])
    return report(problems, summary)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
