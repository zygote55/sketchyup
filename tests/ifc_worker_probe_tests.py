#!/usr/bin/env python3
"""The API probe of ADR 0171 (R097.a): what the pinned IfcOpenShell 0.9.0 really does.

    python3 -I tests/ifc_worker_probe_tests.py WORKER FIXTURE_DIR

Drives `sketchyup-ifc-worker --probe`, `--probe-api` and `--probe-write` (the geometry iterator, the
settings API and the typed IFC4 writer, exactly as the import and export will use them) and checks
every library claim of ADR 0171 section 1.2 against hand-computed values and a raw-text scan of the
fixtures, which share no code with the library. Where the library contradicts the ADR the test
asserts what the library does and says so in a "CONTRADICTS ADR" comment; the findings are
recorded in the ADR's implementation notes (R097.a). A library upgrade that changes any of this
fails here, and is a design review, not a silent change.
"""
import json
import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path

WORKER = Path(sys.argv[1])
FIXTURES = Path(sys.argv[2])
PIN = "7ba701972f7182322e0a00db8f0e1f3de5f69840"
failures = []


def expect(condition, message):
    if not condition:
        failures.append(message)


def near(actual, expected, relative=1e-9, absolute=1e-12):
    return math.isclose(actual, expected, rel_tol=relative, abs_tol=absolute)


def run(arguments, timeout=300):
    return subprocess.run([str(WORKER), *arguments], capture_output=True, text=True, timeout=timeout)


def api(name, *options, expect_ok=True):
    path = name if Path(name).is_absolute() else str(FIXTURES / name)
    result = run(["--probe-api", *options, path])
    if result.returncode != 0 or not result.stdout.strip():
        failures.append(f"--probe-api {name} {' '.join(options)} exited {result.returncode}: {result.stderr[:300]}")
        return {"elements": [], "geometryMessages": [], "parseMessages": [], "initialized": False}
    document = json.loads(result.stdout)
    if expect_ok:
        expect("exception" not in document, f"{name}: exception {document.get('exception')}")
    return document


def codes(document, severity=None):
    return [m["code"] for m in document["geometryMessages"] if severity in (None, m["severity"])]


def messages(document, code):
    return [m for m in document["geometryMessages"] if m["code"] == code]


def element(document, step):
    found = [e for e in document["elements"] if e["step"] == step]
    return found[0] if found else None


def one_box(document, name):
    expect(len(document["elements"]) == 1, f"{name}: expected one element, got {len(document['elements'])}")
    return document["elements"][0] if document["elements"] else None


def scan_length_unit_metres(path):
    """The project's length unit in metres, from the raw text (no IFC library): an SI unit with an
    optional prefix, or a conversion-based unit whose factor is its measure."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    prefixes = {"MILLI": 1e-3, "CENTI": 1e-2, "KILO": 1e3, "": 1.0}
    match = re.search(r"IFCSIUNIT\(\*,\.LENGTHUNIT\.,(?:\.(\w+)\.|\$),\.METRE\.\)", text)
    return prefixes[(match.group(1) or "") if match else ""] if match else None


# ---------------------------------------------------------------------------------------------
# --probe: versions, libraries loaded, schemas, kernels (the helper's own report)
# ---------------------------------------------------------------------------------------------
version = run(["--version"])
expect(version.returncode == 0 and version.stdout.startswith("sketchyup-ifc-worker "), "--version output")
for bad in ([], ["--nonsense"], ["--probe", "extra"], ["--probe-api"], ["--probe-api", "--bogus", "x.ifc"]):
    result = run(bad)
    expect(result.returncode == 2 and "usage:" in result.stderr, f"arguments {bad} must exit 2 with usage")

probe = run(["--probe"])
expect(probe.returncode == 0, f"--probe exited {probe.returncode}: {probe.stderr[:200]}")
info = json.loads(probe.stdout)
expect(info["worker"]["name"] == "sketchyup-ifc-worker", "probe: worker name")
expect(info["ifcopenshell"]["version"] == "0.9.0", f"probe: IfcOpenShell version {info['ifcopenshell']['version']}")
expect(info["ifcopenshell"]["pinnedCommit"] == PIN, "probe: pinned commit")
expect([s["name"] for s in info["schemas"]] == ["IFC2X3", "IFC4", "IFC4X3_ADD2"], "probe: the three schemas")
expect(all(s["available"] for s in info["schemas"]), "probe: every schema is available")
kernels = {k["name"]: k for k in info["kernels"]}
expect(kernels["opencascade"]["available"] and kernels["passthrough"]["available"], "probe: both kernels available")
expect(info["kernelAvailable"] is True and info["kernelStatus"] == "available", "probe: geometry kernel available")
expect(re.fullmatch(r"7\.\d+\.\d+", info["occtVersion"] or ""), f"probe: OpenCASCADE version {info['occtVersion']!r}")
loaded = info["loadedLibraries"]
for needle in ("libifcopenshell.parse.so", "libifcopenshell.plugin.so", "libifcopenshell.geometry.so",
               "ifcopenshell_parse_schema_ifc4.so", "ifcopenshell_geometry_kernel_opencascade.so",
               "libTKernel.so"):
    expect(any(needle in path for path in loaded), f"probe: {needle} not among the loaded libraries")
expect(all(path.startswith("/") for path in loaded), "probe: loaded library paths are absolute")

# ---------------------------------------------------------------------------------------------
# Units (ADR 1.2: LengthUnit; 3.5; the output-unit inconsistency the design worker saw)
# ---------------------------------------------------------------------------------------------
for name, unit, magnitude, volume, half in (
        ("units-mm.ifc", "MILLIMETRE", 0.001, 6.0, (0.5, 1.0, 3.0)),
        ("units-m.ifc", "METRE", 1.0, 6.0, (0.5, 1.0, 3.0)),
        ("units-ft.ifc", "FOOT", 0.3048, 169.901079552, (1.524, 3.048, 9.144)),
        ("units-mm-stray.ifc", "MILLIMETRE", 0.001, 6.0, (0.5, 1.0, 3.0))):
    document = api(name)
    expect(document["unitName"] == unit and near(document["unitMagnitude"], magnitude),
           f"{name}: unit {document['unitName']} {document['unitMagnitude']}")
    box = one_box(document, name)
    if box:
        # Output is in metres with ConvertBackUnits off: every vertex within 1e-9 of source * metresPerUnit.
        expect(near(box["volume"], volume, 1e-9), f"{name}: volume {box['volume']} != {volume}")
        for axis in range(3):
            expect(near(box["bboxMax"][axis], half[axis]), f"{name}: bboxMax[{axis}] {box['bboxMax'][axis]}")
        expect(near(box["bboxMin"][0], -half[0]) and near(box["bboxMin"][1], -half[1]) and near(box["bboxMin"][2], 0.0),
               f"{name}: bboxMin {box['bboxMin']}")
    # the raw-text scan, which shares no code with the library, gives the same factor
    if unit != "FOOT":
        expect(near(scan_length_unit_metres(FIXTURES / name) or 0, magnitude), f"{name}: raw scan of the length unit")
    # LengthUnit is a copy: the settings object handed to the iterator is not changed (the library
    # overwrites its own copy), so the worker reads units from the file (ADR 3.5), never from here.
    expect(document["lengthUnitSetting"] == 1.0, f"{name}: LengthUnit of the caller's settings changed")

# A missing length unit: the library only warns (GEO312) and assumes metres; refusing is the worker's job.
document = api("units-missing.ifc")
box = one_box(document, "units-missing.ifc")
expect(document["unitName"] == "METER" and document["unitMagnitude"] == 1.0, "units-missing: assumed metres")
expect(box and near(box["volume"], 6.0), "units-missing: numbers taken as metres")
expect("GEO312" in codes(document, "warning"), f"units-missing: GEO312 warning missing in {codes(document)}")

# ConvertBackUnits on: output stays in the file's unit (millimetres here), the unit is still reported.
document = api("units-mm.ifc", "--set", "convert-back-units=true")
box = one_box(document, "units-mm convert-back")
expect(box and near(box["volume"], 6e9) and near(box["bboxMax"][2], 3000.0), "convert-back-units: millimetre output")

# A mesh sits in its own local frame (UseWorldCoords off); the placement is the element transform.
document = api("placement-mm.ifc")
box = one_box(document, "placement-mm.ifc")
if box:
    expect(near(box["bboxMax"][0], 0.5) and near(box["bboxMin"][2], 0.0), "placement: the mesh is not placed")
    matrix = box["transform"]
    expect(len(matrix) == 16 and near(matrix[3], 5.0) and near(matrix[7], 6.0) and near(matrix[11], 7.0),
           f"placement: translation (5, 6, 7) m expected in the transform, got {matrix}")
document = api("placement-mm.ifc", "--set", "use-world-coords=true")
box = one_box(document, "placement-mm world")
expect(box and near(box["bboxMin"][0], 4.5) and near(box["bboxMax"][2], 8.0), "use-world-coords: the mesh is placed")

# CONTRADICTS ADR 1.2 ("with ... other than one IfcProject, the library only logs a warning and
# assumes metres"): the test is on IfcContext, which in IFC4 is IfcProject *and* IfcProjectLibrary.
# A file with both (the buildingSMART Reference View wall) loses its units: output in the file's raw
# millimetre numbers, unit "METER", GEO308 and GEO309. This is the output-unit inconsistency of
# prototype 1; the Architecture files have one IfcProject and come out in metres.
document = api("units-mm-projectlibrary.ifc")
box = one_box(document, "units-mm-projectlibrary.ifc")
expect(document["unitName"] == "METER" and document["unitMagnitude"] == 1.0, "projectlibrary: unit assumed metres")
expect({"GEO308", "GEO309"} <= set(codes(document, "warning")), f"projectlibrary: GEO308/GEO309 in {codes(document)}")
if box:
    expect(near(box["volume"], 6e9) and near(box["bboxMax"][2], 3000.0),
           f"projectlibrary: expected raw millimetre numbers, got volume {box['volume']}")
    # The rule the adapter uses: metres = mesh * fileMetresPerUnit / libraryUnitMagnitude.
    scale = scan_length_unit_metres(FIXTURES / "units-mm-projectlibrary.ifc") / document["unitMagnitude"]
    expect(near(box["volume"] * scale ** 3, 6.0), "projectlibrary: the rescale rule gives 6 m3")
document = api("rv-wall-with-opening-and-window.ifc")
wall = element(document, 45)
expect(document["unitName"] == "METER" and wall and near(wall["bboxMax"][0], 3000.0),
       "the Reference View wall comes out in raw millimetres (it has an IfcProjectLibrary)")
expect(wall and near(wall["volume"], 1.5e9, 1e-9),
       f"RV wall volume with the opening subtracted: {wall and wall['volume']} (3000x300x2000 - 1000x300x1000 = 1.5e9)")
document = api("rv-wall-with-opening-and-window.ifc", "--set", "disable-opening-subtractions=true")
wall = element(document, 45)
expect(wall and near(wall["volume"], 1.8e9, 1e-9), "RV wall without the subtraction: 1.8e9")
for arch, schema in (("bsi-ifc2x3-arch.ifc", "IFC2X3"), ("bsi-ifc4-arch.ifc", "IFC4"), ("bsi-ifc43-arch.ifc", "IFC4X3_ADD2")):
    document = api(arch)
    expect(document["schema"] == schema, f"{arch}: schema {document['schema']}")
    expect(document["unitName"] == "MILLIMETRE" and near(document["unitMagnitude"], 0.001), f"{arch}: unit")
    # The seven NetVolume quantities the file records (m3) are among the mesh volumes in metres.
    volumes = sorted(e["volume"] for e in document["elements"])
    for net in (6.4375, 1.26926, 1.78562, 4.23088, 0.164702, 6.72034, 9.36351):
        expect(any(near(v, net, 2e-5) for v in volumes), f"{arch}: no element of volume {net} m3 in {volumes}")
    expect(len(document["elements"]) == 14, f"{arch}: {len(document['elements'])} elements (14 expected)")
    types = {e["type"] for e in document["elements"]}
    # IfcSpace and IfcSpatialZone are emitted by default: it is IfcConvert, not the iterator, that excludes them.
    expect({"IfcSpace", "IfcWall", "IfcSlab"} <= types, f"{arch}: types {sorted(types)}")
    expect(("IfcSpatialZone" in types) == (schema != "IFC2X3"), f"{arch}: IfcSpatialZone exists from IFC4 on")

# ---------------------------------------------------------------------------------------------
# Voids and openings (ADR 1.2, 3.2)
# ---------------------------------------------------------------------------------------------
document = api("openings.ifc")
# Hosts are 4000 x 300 x 2500 mm = 3.0 m3; each opening 1000 x 300 x 2000 mm = 0.6 m3 and fully inside.
for step, name, volume in ((127, "wall A, one opening", 2.4), (147, "wall B, two openings", 1.8)):
    host = element(document, step)
    expect(host and near(host["volume"], volume, 1e-9), f"openings default: {name} volume {host and host['volume']} != {volume}")
for step in (327, 347, 367, 387):
    opening = element(document, step)
    # IfcOpeningElement is emitted as an element of its own by default (IfcConvert excludes it).
    expect(opening and opening["type"] == "IfcOpeningElement" and near(opening["volume"], 0.6),
           f"openings default: opening #{step} is an element of 0.6 m3")
# CONTRADICTS the expectation of ADR 3.2 that an open host is left uncut: an open-shell brep host that
# the kernel can read as an extrusion is cut as if it were closed (GEO191 warns, the volume is 2.4).
host = element(document, 167)
expect(host and near(host["volume"], 2.4) and "GEO191" in codes(document, "warning"),
       "openings default: the open-shell host is cut anyway, with a GEO191 warning")

document = api("openings.ifc", "--set", "disable-opening-subtractions=true")
for step in (127, 147):
    expect(element(document, step) and near(element(document, step)["volume"], 3.0), f"disable-opening-subtractions: host #{step} uncut (3.0)")
expect(len(document["elements"]) == 7, "disable-opening-subtractions: openings are still emitted")

# MaxVoidsPerElement: no voids at all for a host with more (a skip), and the log says so, naming the host.
document = api("openings.ifc", "--set", "max-voids-per-element=1")
expect(near(element(document, 147)["volume"], 3.0), "max-voids-per-element=1: host B (two openings) is uncut")
expect(near(element(document, 127)["volume"], 2.4), "max-voids-per-element=1: host A (one opening) is cut")
skips = messages(document, "GEO403")
expect(len(skips) == 1 and skips[0]["severity"] == "warning" and "#147=" in skips[0]["product"],
       f"max-voids-per-element=1: one GEO403 warning naming host #147, got {skips}")

# How a failed subtraction shows. Hosts of openings-hard.ifc (mm cubes of 1 m3, openings 400 x 400 x 1000 inside = 0.16):
document = api("openings-hard.ifc")
host = element(document, 147)  # D: closed tessellated cube, through opening: 1.0 - 0.16
expect(host and near(host["volume"], 0.84, 1e-9), "hard D: closed tessellated host cut to 0.84")
host = element(document, 227)  # F: extruded host, opening is an open shell (the kernel sews it)
expect(host and near(host["volume"], 0.84, 1e-9), "hard F: open-shell opening still cuts (0.84)")
# E: open tessellated host. The subtraction is attempted on a non-manifold shell and the result is
# wrong geometry (volume 0.4907 instead of 0.84, extent 1.1 m on a 1 m cube) with only a GEO191 warning.
host = element(document, 187)
expect(host and not near(host["volume"], 0.84, 1e-3) and host["bboxMax"][2] > 1.0001,
       f"hard E: open tessellated host gives a wrong result: {host and (host['volume'], host['bboxMax'])}")
expect(any(m["code"] == "GEO191" and "#187=" in m["product"] for m in document["geometryMessages"]),
       "hard E: GEO191 (non-manifold first operand) names host #187")
# G: the opening does not touch its host. The subtraction fails: the host is emitted UNCUT (volume
# 1.0) and the opening is not emitted at all; the error GEO033 names the host. This is "declared but
# not applied", and the library's only signal is the message.
host = element(document, 267)
expect(host and near(host["volume"], 1.0, 1e-9), "hard G: host emitted uncut")
expect(element(document, 667) is None, "hard G: the failing opening is not emitted")
expect(any(m["code"] == "GEO033" and m["severity"] == "error" and "#267=" in m["instance"] for m in document["geometryMessages"]),
       "hard G: GEO033 'Error processing openings' names host #267")
# had_error_processing_elements() is true here (an error while processing the openings) ...
expect(document["hadErrorProcessingElements"] is True, "hard G: had_error_processing_elements() is true")
expect(document["maxSeverity"] == "error", "hard G: the logger's maximum severity is error")

# ---------------------------------------------------------------------------------------------
# Per-element failure reporting
# ---------------------------------------------------------------------------------------------
document = api("failures.ifc")
expect([e["name"] for e in document["elements"]] == ["good-box"], f"failures: only the good box is emitted, got {[e['name'] for e in document['elements']]}")
expect(any(m["code"] == "SYS027" and "4 tasks for 4 products" in m["message"] for m in document["geometryMessages"]),
       "failures: SYS027 says 4 tasks for 4 products")
errors = [m for m in document["geometryMessages"] if m["severity"] == "error"]
expect({"GEO089", "GEO326", "GEO327"} <= {m["code"] for m in errors}, f"failures: error codes {sorted({m['code'] for m in errors})}")
# The messages name the failing representation item and representation, not the product
# (log_message.product is empty at the mapping stage): the worker must map #145 and #165 back to
# their products (#147 and #167) through IfcProductDefinitionShape.
named = {re.match(r"#(\d+)=", m["instance"]).group(1) for m in errors if re.match(r"#\d+=", m["instance"])}
expect({"144", "145", "164", "165"} <= named, f"failures: instances named {sorted(named)}")
expect(all(m["product"] == "" for m in errors if m["code"] in ("GEO089", "GEO326")), "failures: mapping errors carry no product")
# An empty body representation (product #182) is dropped silently: no message at all mentions it.
expect(not any("#18" in m["instance"] or "#18" in m["product"] for m in document["geometryMessages"] if m["severity"] != "notice"),
       "failures: the empty representation is dropped without a message")
# ... but false when whole conversions fail (three of the four products here), so it is no per-element signal.
expect(document["hadErrorProcessingElements"] is False, "failures: had_error_processing_elements() is false although three products failed")
expect(document["maxSeverity"] == "error", "failures: maximum severity error")
# The element filter (exclude by STEP id): the restart-with-the-culprit-excluded path of 1.6.
document = api("openings.ifc", "--exclude-step", "147")
expect(len(document["elements"]) == 6 and element(document, 147) is None, "exclude-step 147: six elements, host B gone")
document = api("failures.ifc", "--exclude-step", "127")
expect(document["emitted"] == 0 and document["initialized"] is False, "exclude-step 127 on failures.ifc: nothing left to convert")

# ---------------------------------------------------------------------------------------------
# Representation selection (ADR 1.2, 3.3)
# ---------------------------------------------------------------------------------------------
default = api("contexts.ifc")
expect([e["context"] for e in default["elements"]] == ["Body"] and near(default["elements"][0]["volume"], 6.0),
       "contexts: the default is the Body representation only (Axis, Box and FootPrint are skipped)")
for option in ("context-types=Plan", "context-identifiers=Axis"):
    document = api("contexts.ifc", "--set", option)
    expect([e["context"] for e in document["elements"]] == ["Body"], f"contexts: {option} has no effect (documented as such)")
# CONTRADICTS ADR 3.3 (priorities ordered "Model/Body/MODEL_VIEW"): that spelling matches no context, so
# nothing converts (GEO051). The grammar is IDENTIFIER[Attribute=Value,...] on IfcGeometricRepresentationSubContext.
document = api("contexts.ifc", "--set", "context-priorities=Model/Body/MODEL_VIEW")
expect(document["initialized"] is False and "GEO051" in codes(document, "warning"), "contexts: the ADR spelling of ContextPriorities converts nothing")
document = api("contexts.ifc", "--set", "context-priorities=Body[ContextType=Model,TargetView=MODEL_VIEW]")
expect([e["context"] for e in document["elements"]] == ["Body"], "contexts: Body[ContextType=Model,TargetView=MODEL_VIEW] selects the Body representation")
# Priorities do not fall back: the first context with a representation wins even when it yields nothing.
document = api("contexts.ifc", "--set", "context-priorities=Box;Body")
expect(document["initialized"] is False, "contexts: a priority list does not fall back from Box to Body")
document = api("contexts.ifc", "--set", "context-ids=13")
expect([e["context"] for e in document["elements"]] == ["Body"], "contexts: context-ids=13 (the Body sub-context) selects Body")
# Curves arrive as separate elements, one per representation (dimensionality 2 = curves, surfaces and solids).
document = api("contexts.ifc", "--set", "dimensionality=2")
expect(sorted(e["context"] for e in document["elements"]) == ["Axis", "Body", "FootPrint"],
       f"contexts: dimensionality=2 emits {[e['context'] for e in document['elements']]}")

# ---------------------------------------------------------------------------------------------
# Kernels, feature edges, threads
# ---------------------------------------------------------------------------------------------
occt = one_box(api("rv-tessellated-item.ifc"), "tessellated item, OpenCASCADE")
passthrough = one_box(api("rv-tessellated-item.ifc", "--kernel", "passthrough"), "tessellated item, pass-through")
expect(occt and passthrough and occt["triangles"] == passthrough["triangles"] == 12, "kernels: 12 triangles from both")
expect(occt and occt["edges"] > 0 and passthrough and passthrough["edges"] == 0, "kernels: feature edges only from the OpenCASCADE kernel")
# The pass-through kernel has no Booleans: a host with openings comes back as an element with no triangles, silently.
document = api("openings.ifc", "--kernel", "passthrough")
expect([element(document, s)["triangles"] for s in (127, 147, 167)] == [0, 0, 0], "kernels: pass-through hosts with voids are empty")
expect(all(element(document, s)["triangles"] == 12 for s in (327, 347, 367, 387)), "kernels: pass-through still meshes plain extrusions")
expect(not any(m["severity"] in ("error",) for m in document["geometryMessages"]), "kernels: the empty hosts are not reported as errors")
unknown = api("units-mm.ifc", "--kernel", "nonexistent", expect_ok=False)
expect("exception" in unknown, "kernels: an unknown kernel is an exception, not a crash")

single = [e["step"] for e in api("bsi-ifc4-arch.ifc")["elements"]]
again = [e["step"] for e in api("bsi-ifc4-arch.ifc")["elements"]]
four = [e["step"] for e in api("bsi-ifc4-arch.ifc", "--threads", "4")["elements"]]
expect(single == again == sorted(single), "threads: one thread gives a deterministic, file-ordered sequence")
expect(sorted(four) == sorted(single), "threads: four threads give the same elements")
# (the four-thread order differs from the single-thread order in practice; it is not asserted: it is a race)

# ---------------------------------------------------------------------------------------------
# Colours (ADR 1.2, 3.7)
# ---------------------------------------------------------------------------------------------
text = (FIXTURES / "bsi-ifc4-arch.ifc").read_text()
file_colours = {tuple(round(float(c), 6) for c in m.groups())
                for m in re.finditer(r"IFCCOLOURRGB\([^,]*,([\d.E+-]+),([\d.E+-]+),([\d.E+-]+)\)", text)}
document = api("bsi-ifc4-arch.ifc")
styled = [m for e in document["elements"] for m in e["materials"] if m["name"].startswith("IfcSurfaceStyleRendering")]
expect(len(styled) >= 10, f"colours: {len(styled)} styled materials in the IFC4 Architecture file")
expect(all(tuple(round(c, 6) for c in m["diffuse"]) in file_colours for m in styled), "colours: every resolved diffuse colour is an IfcColourRgb of the file")
expect(any(tuple(round(c, 3) for c in m["diffuse"]) == (0.0, 0.569, 0.788) for m in styled), "colours: the IfcSpace blue (0, 0.569, 0.788)")
# IfcIndexedColourMap is not read by the C++ geometry code: every triangle of the per-face-colour sample is
# one default material although the file carries several colours (so the adapter reads the colour map, C6).
colours = (FIXTURES / "rv-tessellation-with-individual-colors.ifc").read_text()
palette = re.search(r"IFCCOLOURRGBLIST\(\((.*)\)\);", colours)
indices = re.search(r"IFCINDEXEDCOLOURMAP\(#\d+,\$,#\d+,\(([\d,]+)\)\)", colours)
expect(palette is not None and len(re.findall(r"\(", palette.group(1))) == 3 and indices is not None
       and len(set(indices.group(1).split(","))) == 3, "colours: the sample carries an IfcIndexedColourMap with three colours in use")
box = one_box(api("rv-tessellation-with-individual-colors.ifc"), "individual colours")
expect(box and len(box["materials"]) == 1 and box["materials"][0]["name"] == "DefaultMaterial" and box["materials"][0]["triangles"] == 12,
       f"colours: IfcIndexedColourMap is ignored: {box and box['materials']}")

# ---------------------------------------------------------------------------------------------
# Writer API (hierarchy_helper<Ifc4>, operator<<)
# ---------------------------------------------------------------------------------------------
with tempfile.TemporaryDirectory(prefix="ifc-probe-") as scratch:
    target = Path(scratch) / "written.ifc"
    result = run(["--probe-write", str(target)])
    expect(result.returncode == 0 and result.stdout.strip() != "", f"--probe-write exited {result.returncode}: {result.stderr[:200]}")
    if result.returncode == 0:
        report = json.loads(result.stdout)
        expect("exception" not in report, f"writer: exception {report.get('exception')}")
        expect(report["schema"] == "IFC4" and report["startsWithStepHeader"] and report["endsWithStepFooter"] and report["fileSchemaDeclared"],
               "writer: a STEP file declaring IFC4")
        expect(report["instanceLines"] >= 60, f"writer: {report['instanceLines']} instance lines")
        reread = report["reread"]
        expect(reread["schema"] == "IFC4" and reread["unitName"] == "MILLIMETRE", "writer: the file reads back as IFC4 in millimetres")
        wall = [e for e in reread["elements"] if e["type"] == "IfcWall"]
        opening = [e for e in reread["elements"] if e["type"] == "IfcOpeningElement"]
        # Wall 4000 x 300 x 2500 mm = 3.0 m3 minus the 1000 x 300 x 1200 mm opening = 0.36 m3.
        expect(len(wall) == 1 and near(wall[0]["volume"], 2.64, 1e-9), f"writer: wall volume {wall and wall[0]['volume']} != 2.64")
        expect(len(opening) == 1 and near(opening[0]["volume"], 0.36, 1e-9), "writer: opening volume 0.36")
        written = target.read_text()
        # The writer stamps the current time into the header and the owner history: the byte-identical
        # export of G3 needs the worker to set both (recorded for R097.d).
        expect(re.search(r"FILE_NAME\('probe.ifc','\d{4}-\d\d-\d\dT", written) is not None, "writer: the header carries a timestamp")
        expect(re.search(r"IFCOWNERHISTORY\([^;]*\.ADDED\.,\d{9,}", written) is not None, "writer: the owner history carries the current time")

# ---------------------------------------------------------------------------------------------
# The checked-in original fixtures are what the generator says
# ---------------------------------------------------------------------------------------------
with tempfile.TemporaryDirectory(prefix="ifc-fixtures-") as scratch:
    generated = subprocess.run([sys.executable, "-I", str(FIXTURES / "make_original_fixtures.py"), scratch],
                               capture_output=True, text=True)
    expect(generated.returncode == 0, f"fixture generator failed: {generated.stderr[:200]}")
    for path in sorted(Path(scratch).glob("*.ifc")):
        checked_in = FIXTURES / path.name
        expect(checked_in.is_file() and checked_in.read_bytes() == path.read_bytes(), f"{path.name} differs from the generator's output")

if failures:
    for failure in failures:
        print(f"ifc_worker_probe_tests: {failure}", file=sys.stderr)
    print(f"{len(failures)} failure(s).", file=sys.stderr)
    sys.exit(1)
print("API probe: every claim of ADR 0171 section 1.2 re-checked against IfcOpenShell 0.9.0.")
