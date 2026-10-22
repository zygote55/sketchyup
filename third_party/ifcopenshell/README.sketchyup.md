# IfcOpenShell (vendored, trimmed, unmodified)

SketchyUp's IFC helper, `sketchyup-ifc-worker`, links IfcOpenShell. This directory holds the
source it is built from ([ADR 0171](../../docs/decisions/0171-ifc-exchange-subset.md) decisions 1,
17 and 21).

| | |
| --- | --- |
| Project | [IfcOpenShell](https://github.com/IfcOpenShell/IfcOpenShell) |
| Version | 0.9.0 (`VERSION`) |
| Tag | `ifcopenshell-v0.9.0` |
| Commit | `7ba701972f7182322e0a00db8f0e1f3de5f69840` |
| Archive | `https://github.com/IfcOpenShell/IfcOpenShell/archive/refs/tags/ifcopenshell-v0.9.0.tar.gz`, SHA-256 `9e6a5fb2cb10be248058c8e62f8f511466eb9fc828e40a1f4400ec162eef2c4b` |
| Licence | LGPL-3.0-or-later for the libraries built here (`COPYING.LESSER`, with `COPYING`, the GPL text, as upstream ships it) |
| Patches | none (`PATCHES.md`) |

## What is here

The tag archive trimmed by [`TRIM.txt`](TRIM.txt), byte for byte: `cmake/` (the top-level build,
which reads `../src/*`), `src/ifcparse` (IfcParse and the generated schema code for **IFC2X3, IFC4
and IFC4X3_ADD2** only; the other eight schemas are 50 MB and are not accepted), `src/ifcgeom`
(IfcGeom, the geometry mapping and the OpenCASCADE and pass-through kernels), `src/plugin` (the
plugin loader), the two serializer-runtime files IfcGeom compiles in `src/serializers`, and
`src/ifcfuzz` (upstream's libFuzzer parser harness, for the release fuzz campaign of ADR 0171
check J3). Not here: IfcConvert and its serializers' plugins, the CGAL and Manifold kernels (CGAL
has GPL-3.0-or-later parts that must never be linked), the Python wrapper, Bonsai (GPL-3.0-or-later),
the viewers, the documentation, upstream's tests (they fetch Catch2 over the network) and every other
directory of the repository.

SketchyUp's own files in this directory are `README.sketchyup.md`, `PATCHES.md`, `TRIM.txt`,
`IFC-SOURCE.txt` (the written source offer, installed with the package) and `occt/` (the licence
texts of OpenCASCADE, below). Everything else is upstream's.

## Checking it

`scripts/vendor-ifcopenshell.py` reproduces and checks the tree. It treats the upstream commit and the
bytes of every vendored file as the pin; the archive's SHA-256 above is what was downloaded, and the
script reports a different digest (GitHub does not promise to compress tag archives the same way for
ever) as a notice unless `--strict-archive` is given.

```sh
python3 -I scripts/vendor-ifcopenshell.py fetch /new/empty/dir      # download and check the tag archive
python3 -I scripts/vendor-ifcopenshell.py check /new/empty/dir/ifcopenshell-v0.9.0.tar.gz
python3 -I scripts/vendor-ifcopenshell.py pin                       # README, dependencies.json and CMake agree
```

CI's `IFC library and helper` job runs `fetch` and `check` on every run, and the test
`ifc_library_install` runs `pin` (check L3 of ADR 0171 section 6.3).

## Upgrading

An upgrade is an explicit change (decision 21): edit the tag, commit and archive digest in
`scripts/vendor-ifcopenshell.py`, this file, `dependencies.json` and `src/ifc/CMakeLists.txt`;
run `update` and `check`; update `TRIM.txt` if upstream moved files; and re-run the checks of ADR 0171
section 6, starting with `tests/ifc_worker_probe_tests.py`, which pins the library behaviour the design
depends on. The generated schema code is large and diffs are not reviewable; review `TRIM.txt`, the
script's output and the probe.

## OpenCASCADE

The OpenCASCADE kernel plugin needs OpenCASCADE Technology (OCCT), which is **not vendored**: it is
Arch's `opencascade` package (7.9.3 at the pinned snapshot), built against at package-build time
and copied, only the libraries the kernel plugin records, beside the IfcOpenShell libraries
(decision 18). `occt/LICENSE_LGPL_21.txt` and `occt/OCCT_LGPL_EXCEPTION.txt` are the licence texts of
that package (`/usr/share/licenses/opencascade`), copied unchanged so that they are tracked files
the release notices can collect.
