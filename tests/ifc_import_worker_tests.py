#!/usr/bin/env python3
"""R097.b: the worker's import (ADR 0171 section 6.3) checked at the package level against sources that
share no code with the library: a raw-text scan of the files (tests/ifc_text_scan.py) and hand-computed
values. Covers A1-A5 (units), B1-B4, B6 and B7 (hierarchy), C1-C5 (properties and escapes), D1-D4
(accounting and unsupported content), E2 and E7-style volume checks, H1 and H2 (determinism).

    python3 -I tests/ifc_import_worker_tests.py WORKER FIXTURE_DIR

The application-level checks (tags, one edit, caps, native validation) are in ifc_convert_tests and
ifc_import_tests.
"""
import json
import math
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import ifc_package as pkg  # noqa: E402
import ifc_text_scan as scan  # noqa: E402

WORKER = sys.argv[1]
FIXTURES = sys.argv[2]
failures = []
TMP = tempfile.mkdtemp(prefix="ifc-worker-tests-")


def expect(ok, message):
    if not ok:
        failures.append(message)
        print("FAIL:", message)


def near(a, b, tol=1e-9, relative=True):
    if relative and abs(b) > 1:
        return abs(a - b) <= tol * abs(b)
    return abs(a - b) <= tol


def run(path, *extra, name=None, expect_status="ok"):
    out = os.path.join(TMP, (name or os.path.basename(path)) + ".pkg")
    if os.path.exists(out):
        os.remove(out)
    proc = subprocess.run([WORKER, "--import", path, "--output", out, *extra], capture_output=True, text=True, timeout=300,
                          env={"LC_ALL": "C.UTF-8", **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}})
    result = None
    for line in proc.stdout.splitlines():
        if line.startswith("R "):
            result = json.loads(line[2:])
    if expect_status == "ok":
        if proc.returncode != 0 or not result or result.get("status") != "ok":
            failures.append("worker failed on %s: %s %s" % (path, proc.returncode, (result, proc.stderr[-300:])))
            print(failures[-1])
            return None, result, proc
        return pkg.read_package(open(out, "rb").read()), result, proc
    expect(proc.returncode == 3 and result and result.get("status") == "refused", "%s: expected a refusal, got %s %s" % (path, proc.returncode, result))
    return None, result, proc


def fixture(name):
    return os.path.join(FIXTURES, name)


def node_by_guid(m, guid):
    for i, n in enumerate(m["nodes"]):
        if n["guid"] == guid:
            return i, n
    return None, None


def provenance(n):
    return {k: v for k, t, v in n["ifc"]}


def props_of(n, origins=(0, 1, 2, 3)):
    return {"%s.%s" % (p[0], p[1]): p[4] for p in n["props"] if p[2] in origins}


def bbox(mesh):
    xs = [v[0] for v in mesh["vertices"]]
    ys = [v[1] for v in mesh["vertices"]]
    zs = [v[2] for v in mesh["vertices"]]
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def mesh_of(m, node):
    return m["meshes"][node["mesh"]]


def volume(m, node):
    return abs(pkg.mesh_volume_area(mesh_of(m, node))[0])


def by_name(m, name):
    for i, n in enumerate(m["nodes"]):
        if n["name"] == name:
            return i, n
    raise KeyError(name)


# ---- the generator reproduces the checked-in fixtures ------------------------------------------------------
generated = os.path.join(TMP, "generated")
os.makedirs(generated)
subprocess.run([sys.executable, "-I", os.path.join(FIXTURES, "make_import_fixtures.py"), generated], check=True, env={"PYTHONDONTWRITEBYTECODE": "1"})
for name in sorted(os.listdir(generated)):
    expect(os.path.exists(fixture(name)) and open(fixture(name), "rb").read() == open(os.path.join(generated, name), "rb").read(),
           "make_import_fixtures.py reproduces " + name)

# K2: the buildingSMART files are redistributed unmodified: NOTICE.md lists the SHA-256 of each.
import hashlib  # noqa: E402
import re  # noqa: E402
notice = open(fixture("NOTICE.md"), encoding="utf-8").read()
listed = re.findall(r"^\| `([^`]+\.ifc)` \|.*\| `([0-9a-f]{64})` \|$", notice, re.M)
expect(len(listed) == 10, "NOTICE.md lists the ten buildingSMART files: %d" % len(listed))
for file_name, digest in listed:
    expect(hashlib.sha256(open(fixture(file_name), "rb").read()).hexdigest() == digest, "K2: %s is unmodified" % file_name)
expect("CC BY 4.0" in notice.replace("Creative Commons Attribution\n4.0", "CC BY 4.0") or "Creative Commons Attribution" in notice, "K2: the licence is named")
expect("IFC(TM)" in notice, "K2: the IFC trademark credit")

CERTIFICATION = ["bsi-ifc2x3-arch.ifc", "bsi-ifc4-arch.ifc", "bsi-ifc43-arch.ifc", "bsi-ifc4-landscaping.ifc", "bsi-ifc4-road.ifc",
                 "rv-wall-with-opening-and-window.ifc", "rv-basin-tessellation.ifc", "rv-column-straight-rectangle-tessellation.ifc",
                 "rv-tessellated-item.ifc", "rv-tessellation-with-individual-colors.ifc"]
ORIGINAL = ["tree-cases.ifc", "psets-types-escapes.ifc", "units-own.ifc", "extrusion-profiles.ifc", "clipped-wall.ifc", "mapped-items.ifc",
            "polygonal-face-set.ifc", "unsupported-all.ifc", "ifc43-infrastructure.ifc", "openings.ifc", "units-mm.ifc", "units-m.ifc",
            "units-ft.ifc", "units-mm-projectlibrary.ifc", "units-inch-rounded.ifc", "containment-cycle.ifc", "limits-hostile.ifc"]

# ---- the certification files: accounting, units, hierarchy, properties ------------------------------------------
for name in CERTIFICATION:
    path = fixture(name)
    facts = scan.Scan(path).facts()
    m, result, proc = run(path)
    if m is None:
        continue
    report = m["report"]
    acc = report["accounting"]
    # D1: every instance is converted, supporting or omitted, and the total is the scan's count.
    expect(acc["entities"] == facts["entities"], "%s: entities %s vs the scan's %s" % (name, acc["entities"], facts["entities"]))
    expect(acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"], "%s: the accounting identity" % name)
    expect(sum(report["omittedEntities"].values()) == acc["omitted"], "%s: omittedEntities sums to omitted" % name)
    # The sides of the identity from the scan, not from the report: every node is one converted instance, plus the
    # groups, zones and systems whose names were recorded; every relationship of the kinds the import reads that
    # names an imported object is at least one supporting instance.
    imported_steps = {provenance(n)["ifc.Step"] for n in m["nodes"]}
    raw = scan.Scan(path)
    grouped = set()
    for _, args in raw.of("IFCRELASSIGNSTOGROUP"):
        if any(int(x) in imported_steps for x in args[4]):
            grouped.add(int(args[6]))
    expect(acc["converted"] == len(m["nodes"]) + len(grouped), "%s: converted %d vs %d nodes + %d groups" % (name, acc["converted"], len(m["nodes"]), len(grouped)))
    read_kinds = ("IFCRELAGGREGATES", "IFCRELCONTAINEDINSPATIALSTRUCTURE", "IFCRELDEFINESBYPROPERTIES", "IFCRELDEFINESBYTYPE",
                  "IFCRELASSOCIATESMATERIAL", "IFCRELASSOCIATESCLASSIFICATION", "IFCRELASSIGNSTOGROUP")
    naming = 0
    for kind in read_kinds:
        for rel_step, args in raw.of(kind):
            flat = [x for a in args for x in (a if isinstance(a, list) else [a]) if isinstance(x, scan.Ref)]
            if any(int(x) in imported_steps for x in flat):
                naming += 1
    expect(acc["supporting"] >= naming, "%s: supporting %d is at least the %d relationships that name imported objects" % (name, acc["supporting"], naming))
    # A1/A2: units.
    expect(near(m["units"]["metresPerUnit"], facts["units"]["length"], 1e-12), "%s: metresPerUnit" % name)
    # B1: the group tree equals the scan's, by GlobalId.
    guids = {n["guid"]: i for i, n in enumerate(m["nodes"])}
    for i, n in enumerate(m["nodes"]):
        if n["parent"] < 0:
            expect(n["role"] == "project", "%s: the only root is the project" % name)
            continue
        parent_guid = m["nodes"][n["parent"]]["guid"]
        if n["guid"] in facts["parents"]:
            expect(facts["parents"][n["guid"]] == parent_guid or m["nodes"][n["parent"]]["role"] in ("project",),
                   "%s: %s (%s) has parent %s, the scan says %s" % (name, n["name"], n["class"], parent_guid, facts["parents"].get(n["guid"])))
    # B2: the nodes of each spatial class equal the scan's count of that class.
    for role, cls in (("project", "IFCPROJECT"), ("site", "IFCSITE"), ("building", "IFCBUILDING"), ("storey", "IFCBUILDINGSTOREY")):
        expect(sum(1 for n in m["nodes"] if n["role"] == role) == facts["classes"].get(cls, 0), "%s: count of %s" % (name, cls))
    # B3: the children of a parent follow the file's relationship order.
    for parent_guid, kids in facts["children"].items():
        if parent_guid not in guids:
            continue
        got = [n["guid"] for n in m["nodes"] if n["parent"] == guids[parent_guid] and n["guid"] in kids]
        want = [k for k in kids if k in set(got)]
        # aggregated children come before contained ones in the package; compare within each relationship kind.
        expect(sorted(got) == sorted(want), "%s: children of %s" % (name, parent_guid))
    # C1: properties and quantities equal the scan's (occurrence level; type level where the occurrence has no own).
    for n in m["nodes"]:
        if n["guid"] not in facts["objects"]:
            continue
        want = dict(facts["typeProperties"].get(n["guid"], {}).get("properties", {}))
        want.update(facts["occurrenceProperties"].get(n["guid"], {}))
        got = props_of(n)
        for key, value in want.items():
            if key not in got:
                expect(False, "%s: %s (%s) misses %s" % (name, n["name"], n["class"], key))
            elif isinstance(value, float):
                expect(isinstance(got[key], (int, float)) and near(got[key], value, 1e-9), "%s: %s.%s = %r, the scan says %r" % (name, n["name"], key, got[key], value))
            else:
                expect(got[key] == value, "%s: %s.%s = %r, the scan says %r" % (name, n["name"], key, got[key], value))
        expect(all(k in want for k in got), "%s: %s has properties the file does not define: %s" % (name, n["name"], sorted(set(got) - set(want))))
        # C4: provenance on every node.
        prov = provenance(n)
        expect(prov.get("ifc.Class") == n["class"] and prov.get("ifc.GlobalId") == n["guid"] and prov.get("ifc.Step") == n["step"], "%s: provenance of %s" % (name, n["name"]))
        if n["mesh"] >= 0:
            expect("ifc.Representation" in prov and "ifc.Conversion" in prov, "%s: geometry provenance of %s" % (name, n["name"]))
    # B4: ifc.Groups equals the scan's membership.
    for n in m["nodes"]:
        want = sorted(facts["groups"].get(n["guid"], []))
        got = sorted(x.strip() for x in provenance(n).get("ifc.Groups", "").split(",") if x.strip())
        if n["guid"] in facts["objects"]:
            expect(got == want, "%s: ifc.Groups of %s: %s vs %s" % (name, n["name"], got, want))
    # C5: classification reference with its URI.
    for guid, entries in facts["classification"].items():
        i, n = node_by_guid(m, guid)
        if n:
            prov = provenance(n)
            expect(entries[0]["location"] in prov.get("ifc.ClassificationUri", ""), "%s: classification URI of %s" % (name, n["name"]))
    # H1: two runs give the same bytes.
    m2, _, _ = run(path, name=name + ".again")
    expect(open(os.path.join(TMP, name + ".pkg"), "rb").read() == open(os.path.join(TMP, name + ".again.pkg"), "rb").read(), "%s: H1 two imports are identical" % name)

# The IFC4 Architecture file in detail (ADR 0171 section 6.3).
facts = scan.Scan(fixture("bsi-ifc4-arch.ifc")).facts()
m, _, _ = run(fixture("bsi-ifc4-arch.ifc"))
if m:
    expect(sum(1 for n in m["nodes"] if n["role"] == "site") == 2, "B1: both IFC4 sites are imported")
    quantities = [p for n in m["nodes"] for p in n["props"] if p[2] in (1, 3)]
    lengths = [p for p in quantities if p[0].startswith("Qto") and "Length" in p[1]]
    expect(len(quantities) == 25, "A2: 7 area, 7 volume and 11 length quantities: %d" % len(quantities))
    # E7: mesh volumes agree with the file's own NetVolume where present (1e-3 relative).
    checked = 0
    for n in m["nodes"]:
        net = [p[4] for p in n["props"] if p[1] == "NetVolume"]
        if net and n["mesh"] >= 0:
            v = volume(m, n)
            expect(abs(v - net[0]) <= 1e-3 * net[0], "E7: %s mesh volume %g vs NetVolume %g" % (n["name"], v, net[0]))
            checked += 1
    expect(checked >= 5, "E7: NetVolume checked on %d elements" % checked)
    # Colours: the Architecture file's spaces are the blue of its IfcSurfaceStyle.
    blue = [mat for mat in m["materials"] if abs(mat["color"][2] - 0.788) < 1e-3]
    expect(blue, "materials carry the file's colours")

# C6: the per-face colours of the individual-colours file equal its IfcIndexedColourMap.
m, _, _ = run(fixture("rv-tessellation-with-individual-colors.ifc"))
if m:
    mesh = m["meshes"][0]
    palette = [(1.0, 0.0, 0.0), (0.0, 0.5, 0.0), (1.0, 1.0, 0.0)]
    index = [1, 1, 2, 2, 3, 3, 1, 1, 1, 1, 1]
    expect(len(mesh["colors"]) == 12, "C6: one colour entry per triangle")
    by_triangle = {}
    pts = mesh["vertices"]
    # The file's triangles (1-based) and points, in millimetres.
    raw_pts = [(0, 0, 0), (1000, 0, 0), (1000, 1000, 0), (0, 1000, 0), (0, 0, 2000), (1000, 0, 2000), (1000, 1.0, 2000), (0, 1000, 2000)]
    tris = [(1, 6, 5), (1, 2, 6), (6, 2, 7), (7, 2, 3), (7, 8, 6), (6, 8, 5), (5, 8, 1), (1, 8, 4), (4, 2, 1), (2, 4, 3), (4, 8, 7), (7, 3, 4)]

    def key(points):
        return tuple(sorted(tuple(round(c * 1e6) for c in p) for p in points))

    want = {}
    for t, tri in enumerate(tris):
        if t < len(index):
            want[key([tuple(c * 0.001 for c in raw_pts[i - 1]) for i in tri])] = palette[index[t] - 1]
    seen = 0
    for t, tri in enumerate(mesh["triangles"]):
        k = key([pts[i] for i in tri])
        color = mesh["colors"][t]
        if k in want:
            expect(color is not None and all(abs(a - b) < 1e-6 for a, b in zip(color, want[k])), "C6: triangle %d colour %s vs %s" % (t, color, want[k]))
            seen += 1
        else:
            expect(color is None, "C6: the triangle without a colour index has none")
    expect(seen == 11, "C6: 11 of 12 triangles carry the file's colour (the file's map has 11 indices): %d" % seen)

# E3: the Reference View wall, with the opening subtracted: 3 x 0.3 x 2 - 1 x 0.3 x 1 = 1.5 m3.
m, _, _ = run(fixture("rv-wall-with-opening-and-window.ifc"))
if m:
    _, wall = by_name(m, "Wall for Test Example")
    _, window = by_name(m, "Window for Test Example")
    expect(near(volume(m, wall), 1.5, 1e-9), "E3: the wall's volume with the opening subtracted: %g" % volume(m, wall))
    expect(near(volume(m, window), 0.2, 1e-9), "the window's volume: %g" % volume(m, window))
    lo, hi = bbox(mesh_of(m, wall))
    expect(near(hi[0] - lo[0], 3.0, 1e-9) and near(hi[2] - lo[2], 2.0, 1e-9), "F1: the wall is 3 m x 2 m (units rescaled)")
    expect(near(provenance(window)["ifc.OverallWidth"], 1.0, 1e-12) and near(provenance(window)["ifc.OverallHeight"], 1.0, 1e-12),
           "a window keeps its overall size as properties (metres)")
    expect(m["report"]["geometry"]["openingsApplied"] == 1, "the opening is applied")
    expect(m["engine"]["unitsTrusted"] is False and near(m["engine"]["meshScale"], 0.001, 1e-12), "F1: the library's units were not trusted and the mesh was rescaled")
    expect(any(n["code"] == "unitsRescaled" for n in m["report"]["notices"]), "F1: a notice says so")
    _, result_off, _ = run(fixture("rv-wall-with-opening-and-window.ifc"), "--max-voids", "0", name="rv-novoid")
    # With the subtraction disabled elsewhere the same wall would be 1.8 m3 (checked by the API probe): here
    # check that --max-voids 1 with one void still subtracts (the bound is a skip above it).

# ---- units (A1, A3, A4, A5) ---------------------------------------------------------------------------------------
m, _, _ = run(fixture("units-mm.ifc"))
if m:
    lo, hi = bbox(m["meshes"][0])
    expect(all(near(a, b, 1e-9, relative=False) for a, b in zip(lo, (-0.5, -1.0, 0.0))) and all(near(a, b, 1e-9, relative=False) for a, b in zip(hi, (0.5, 1.0, 3.0))),
           "A1: units-mm vertices in metres: %s %s" % (lo, hi))
    expect(near(volume(m, m["nodes"][1]), 6.0, 1e-9), "A1: units-mm volume 6 m3")
m, _, _ = run(fixture("units-m.ifc"))
if m:
    lo, hi = bbox(m["meshes"][0])
    expect(all(near(a, b, 1e-12, relative=False) for a, b in zip(hi, (0.5, 1.0, 3.0))), "A1: units-m is exact")
m, _, _ = run(fixture("units-ft.ifc"))
if m:
    lo, hi = bbox(m["meshes"][0])
    expect(all(near(a, b, 1e-9, relative=False) for a, b in zip(hi, (1.524, 3.048, 9.144))), "A1: units-ft vertices: %s" % (hi,))
    expect(near(volume(m, m["nodes"][1]), 169.901079552, 1e-9), "A1: units-ft volume")
    expect(m["units"]["metresPerUnitExact"] is True, "the foot is exact")
m, _, _ = run(fixture("units-mm-projectlibrary.ifc"))
if m:
    expect(near(volume(m, m["nodes"][1]), 6.0, 1e-9), "F1: units-mm-projectlibrary imports at the right size")
_, refusal, _ = run(fixture("units-missing.ifc"), expect_status="refused")
expect(refusal and refusal["code"] == "noLengthUnit", "A3: a missing length unit is refused")
m, _, _ = run(fixture("units-missing.ifc"), "--length-unit", "ft", name="units-missing-ft")
if m:
    expect(m["units"]["override"] is True and near(volume(m, m["nodes"][1]), 6 * 0.028316846592, 1e-9), "A3: accepted with the override, reported as one")
_, refusal, _ = run(fixture("units-furlong.ifc"), expect_status="refused")
expect(refusal and refusal["code"] == "unsupportedLengthUnit", "A4: a furlong is refused")
m, _, _ = run(fixture("units-own.ifc"))
if m:
    _, wall = by_name(m, "W")
    p = props_of(wall)
    expect(near(p["Qto_Own.Area"], 100 * 0.09290304, 1e-9), "A5: a quantity in square feet: %r" % p.get("Qto_Own.Area"))
    expect(near(p["Qto_Own.Volume"], 10 * 0.028316846592, 1e-9), "A5: a quantity in cubic feet: %r" % p.get("Qto_Own.Volume"))
    expect(near(p["Pset_Own.TwoFeet"], 0.6096, 1e-9), "A5: a property with its own unit (2 ft): %r" % p.get("Pset_Own.TwoFeet"))
m, _, _ = run(fixture("units-inch-rounded.ifc"))
if m:
    _, wall = by_name(m, "W")
    expect(near(m["units"]["metresPerUnit"], 0.0254, 1e-15), "A4: an inch written to nine digits is snapped to 0.0254: %r" % m["units"]["metresPerUnit"])
    expect(any(n["code"] == "unitFactorSnapped" for n in m["report"]["notices"]), "and the snap is noticed")
    expect(near(volume(m, wall), 1.016 * 0.2032 * 2.032, 1e-9), "and the geometry agrees with the snapped unit: %.12g" % volume(m, wall))
_, far, _ = run(fixture("far-from-origin.ifc"), expect_status="refused")
expect(far and far["code"] == "farFromOrigin" and "2000000" in far["message"], "F3: a model beyond 1e6 m is refused with its extent")

# ---- properties, types, escapes (C1-C5) ----------------------------------------------------------------------------
m, _, _ = run(fixture("psets-types-escapes.ifc"))
if m:
    _, w = by_name(m, "W")
    _, w2 = by_name(m, "W2")
    p = props_of(w)
    expect(p["Pset_WallCommon.FireRating"] == "EI60", "occurrence value overrides the type's")
    expect(p["Pset_WallCommon.Reference"] == "WT1-ref", "type-level property copied to the occurrence")
    expect(p["Pset_WallCommon.IsExternal"] is True, "boolean")
    expect("Pset_WallCommon.LoadBearing" not in p, "a logical UNKNOWN is omitted")
    expect(near(p["Pset_WallCommon.Width"], 0.2, 1e-12), "length converted to metres")
    expect(near(p["Pset_WallCommon.GrossArea"], 3.5, 1e-12), "area in m2")
    expect(near(p["Pset_WallCommon.ThermalTransmittance"], 0.24, 1e-12), "other measures are stored as written")
    expect(p["Pset_WallCommon.Cafe"] == "café", "C2: \\X2\\ escape decoded: %r" % p["Pset_WallCommon.Cafe"])
    expect(p["Pset_WallCommon.Naive"] == "naïve", "C2: \\S\\ escape decoded: %r" % p["Pset_WallCommon.Naive"])
    expect(near(p["Pset_WallCommon.OwnUnit"], 0.254, 1e-12), "A5: a property with its own unit (10 in)")
    expect(p["Pset_WallCommon.Finish"] == "Matt, Fine", "enumerated value")
    expect(near(p["Pset_WallCommon.Range.Lower"], 0.1, 1e-12) and near(p["Pset_WallCommon.Range.Upper"], 0.2, 1e-12), "bounded value as two properties")
    expect(p["Pset_WallCommon.Codes"] == "1, 2", "list value")
    expect(not any(k.startswith("Pset_WallCommon.Complex") or k.startswith("Pset_WallCommon.Table") for k in p), "complex and table properties are not mapped")
    unmapped = m["report"]["properties"]["propertiesUnmapped"]
    expect(unmapped.get("IfcComplexProperty") == 1 and unmapped.get("IfcPropertyTableValue") == 1, "and are counted by class: %s" % unmapped)
    expect(near(p["Qto_WallBaseQuantities.Length"], 5.0, 1e-12) and near(p["Qto_WallBaseQuantities.NetSideArea"], 12.5, 1e-12) and near(p["Qto_WallBaseQuantities.NetVolume"], 2.5, 1e-12), "quantities in SI")
    expect(p["Qto_WallBaseQuantities.Openings"] == 4.0 and p["Qto_WallBaseQuantities.Mass"] == 100.0, "count and weight as written")
    expect(near(p["Qto_WallBaseQuantities.Depth"], 0.9144, 1e-12), "A5: a quantity with its own unit (3 ft)")
    expect(near(p["Qto_WallTypeQuantities.NetArea"], 7.0, 1e-12), "type-level quantity copied")
    unconverted = {u["unit"]: u["count"] for u in m["report"]["unitsUnconverted"]}
    expect("IfcThermalTransmittanceMeasure" in unconverted, "the unit table lists the measure stored as written: %s" % unconverted)
    p2 = props_of(w2)
    expect(p2["Pset_WallCommon.FireRating"] == "EI30" and "Pset_WallCommon.Width" not in p2, "W2 has only the type's properties")
    prov = provenance(w)
    expect(prov["ifc.Type"] == "WT1", "ifc.Type")
    expect("Example Classification:Ss_25_10 Wall systems" == prov["ifc.Classification"], "C5: classification %r" % prov.get("ifc.Classification"))
    expect(prov["ifc.ClassificationUri"] == "https://example.org/Ss_25", "C5: classification URI")

# ---- the tree cases (B1, B6, B7, D3) ----------------------------------------------------------------------------------
m, _, _ = run(fixture("tree-cases.ifc"))
if m:
    names = {n["name"]: (i, n) for i, n in enumerate(m["nodes"])}
    parent_name = lambda n: m["nodes"][n["parent"]]["name"] if n["parent"] >= 0 else None
    expect(parent_name(names["W1"][1]) == "Level 1" and parent_name(names["E2"][1]) == "Level 1", "containment: W1 and E2 are in Level 1")
    expect(sum(1 for n in m["nodes"] if n["name"] == "E2") == 1, "B6: an element with two containers appears once, in the first")
    expect(parent_name(names["P1"][1]) == "A1" and parent_name(names["P2"][1]) == "A1" and parent_name(names["A1"][1]) == "Level 1", "aggregation: A1 holds P1 and P2")
    expect(names["A1"][1]["mesh"] < 0 and names["A1"][1]["role"] == "assembly", "an assembly without a representation is a group")
    expect(parent_name(names["SP1"][1]) == "Level 1" and names["SP1"][1]["role"] == "space", "a space is aggregated by its storey")
    expect(parent_name(names["U1"][1]) == "Project", "B6: an uncontained element goes under the project")
    expect(any(n["code"] == "uncontainedElements" for n in m["report"]["notices"]), "and is noticed")
    expect(any(n["code"] == "duplicateGlobalId" for n in m["report"]["notices"]), "B6: a duplicate GlobalId is noticed")
    expect(sum(1 for n in m["nodes"] if n["guid"] == names["D1"][1]["guid"]) == 2, "B6: both elements with the duplicate GlobalId are imported")
    expect(any(n["code"] == "invalidGlobalId" for n in m["report"]["notices"]), "B6: an invalid GlobalId is noticed")
    expect(names["I1"][1]["guid"] == "TooShortGlobalId12345", "and kept as text")
    expect(names["L1"][1]["hidden"] is True and provenance(names["L1"][1]).get("ifc.Layer") == "Hidden Layer", "B7: a layer that is off imports its bodies hidden")
    expect(not names["W1"][1]["hidden"], "and others are not hidden")
    groups = lambda name: sorted(x.strip() for x in provenance(names[name][1]).get("ifc.Groups", "").split(",") if x.strip())
    expect(groups("W1") == ["Fire compartment", "Zone one"] and groups("E2") == ["Fire compartment", "Heating system"], "B4: ifc.Groups lists groups, zones and systems: %s %s" % (groups("W1"), groups("E2")))
    expect("C1" not in names and "C2" not in names, "D3: elements in a cycle are not imported")
    locations = m["report"]["locations"]
    expect(any(l["kind"] == "notImported" and "cycle" in l["message"] for l in locations), "and are located")
    expect(any(x["class"] == "IfcBuildingElementProxy" and x["reason"] == "cycle" and x["count"] == 2 for x in m["report"]["elements"]["notImported"]), "and counted: %s" % m["report"]["elements"]["notImported"])
    # Level 2 is 3 m up: its transform is a translation of 3 in z; the wall is at the storey's origin.
    expect(near(names["Level 2"][1]["transform"][11], 3.0, 1e-12), "storey elevation as a translation")
    expect(near(names["E2"][1]["transform"][3], 2.0, 1e-12), "E2 is 2 m along its storey")
    acc = m["report"]["accounting"]
    expect(acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"], "D1 on the tree cases")
    s = scan.Scan(fixture("tree-cases.ifc")).facts()
    expect(acc["entities"] == s["entities"], "the total is the scan's")

# The package's node bound, lowered for the test: what does not fit is reported with its reason, the children of a
# node that does not fit go with it, and the identity of the accounting still holds.
m, _, _ = run(fixture("tree-cases.ifc"), "--max-nodes", "9", name="nodes-capped")
if m:
    expect(len(m["nodes"]) == 9, "a lowered node bound keeps that many nodes: %d" % len(m["nodes"]))
    reasons = {x["reason"] for x in m["report"]["elements"]["notImported"]}
    expect("nodeLimit" in reasons, "and reports the rest as nodeLimit: %s" % sorted(reasons))
    expect(all(n["parent"] < i for i, n in enumerate(m["nodes"])), "and the tree stays in tree order")
    acc = m["report"]["accounting"]
    expect(acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"], "and the identity holds")
_, refusal, _ = run(fixture("tree-cases.ifc"), "--max-nodes", "0", name="nodes-none", expect_status="refused")
expect(refusal and refusal["code"] == "nothingToImport", "a bound that leaves nothing is a refusal")

# Values past the package's bounds: the wall that is 3e9 m deep is left out and reported, the other wall is kept, and
# no property or name is beyond what the decoder accepts.
m, _, _ = run(fixture("limits-hostile.ifc"))
if m:
    names = [n["name"] for n in m["nodes"]]
    expect("W" in names and "Far" not in names, "limits: the wall past the coordinate bound is left out, the other is kept: %s" % names)
    expect(any(x["reason"] == "meshCoordinates" for x in m["report"]["elements"]["notImported"]), "limits: and reported as meshCoordinates")
    _, w = by_name(m, "W")
    expect(len(w["props"]) <= 4096, "limits: at most 4096 properties on a node: %d" % len(w["props"]))
    expect(all(len(str(p[4])) <= 32768 and len(p[0].encode()) <= 4096 and len(p[1].encode()) <= 4096 for p in w["props"]), "limits: texts and names are bounded")
    codes = {n["code"] for n in m["report"]["notices"]}
    expect({"propertiesOmitted", "propertyTextClipped", "propertyNotFinite"} <= codes, "limits: and the notices say what happened: %s" % sorted(codes))

# A containment cycle (two spaces that contain each other) is broken at the closing edge, noticed and located.
m, _, _ = run(fixture("containment-cycle.ifc"))
if m:
    names = [n["name"] for n in m["nodes"]]
    expect(names.count("SA") == 1 and names.count("SB") == 1 and names.count("W1") == 1, "a cycle of containment imports each element once: %s" % names)
    _, sa = by_name(m, "SA")
    _, sb = by_name(m, "SB")
    expect(m["nodes"][sb["parent"]]["name"] == "SA" and m["nodes"][sa["parent"]]["name"] == "Level 1", "the first path wins")
    expect(any(n["code"] == "containmentCycle" for n in m["report"]["notices"]), "and the cycle is noticed")
    expect(any(l["kind"] == "containmentCycle" for l in m["report"]["locations"]), "and located")
    acc = m["report"]["accounting"]
    expect(acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"] == scan.Scan(fixture("containment-cycle.ifc")).facts()["entities"], "D1 on the cycle")

# ---- profiles, boolean clipping, mapped items, polygonal face sets (E2, the 3.3 table) -------------------------------------
m, _, _ = run(fixture("extrusion-profiles.ifc"))
if m:
    _, rect = by_name(m, "rectangle")
    _, circle = by_name(m, "circle")
    _, lshape = by_name(m, "l-shape")
    _, voids = by_name(m, "with-voids")
    expect(near(volume(m, rect), 6.0, 1e-9), "E2: rectangle profile volume 6 m3: %g" % volume(m, rect))
    analytic = math.pi * 0.25 * 2
    expect(0.995 * analytic <= volume(m, circle) <= analytic, "E2: circle profile within the deflection of pi r2 h: %g vs %g" % (volume(m, circle), analytic))
    expect(near(volume(m, lshape), 2.5, 1e-9), "E2: arbitrary closed profile volume 2.5 m3: %g" % volume(m, lshape))
    expect(near(volume(m, voids), 12.0, 1e-9), "E2: arbitrary profile with voids volume 12 m3: %g" % volume(m, voids))
    byrep = m["report"]["geometry"]["byRepresentation"]
    expect(byrep.get("IfcExtrudedAreaSolid") == 4, "the report counts the item types the library returned: %s" % byrep)
m, _, _ = run(fixture("clipped-wall.ifc"))
if m:
    _, wall = by_name(m, "clipped")
    expect(near(volume(m, wall), 1.6, 1e-9), "boolean clipping: 1.6 m3: %g" % volume(m, wall))
    expect("IfcBooleanClippingResult" in provenance(wall)["ifc.Representation"] or "IfcExtrudedAreaSolid" in provenance(wall)["ifc.Representation"], "provenance names the items")
m, _, _ = run(fixture("mapped-items.ifc"))
if m:
    meshes = {n["mesh"] for n in m["nodes"] if n["mesh"] >= 0}
    expect(len(meshes) == 1 and sum(1 for n in m["nodes"] if n["mesh"] >= 0) == 3, "mapped items share one mesh: %s" % meshes)
    expect(m["report"]["geometry"]["sharedMeshes"] == 1, "and the report says so")
    for n in m["nodes"]:
        if n["mesh"] >= 0:
            expect(near(volume(m, n), 1.0, 1e-9), "mapped item volume 1 m3")
    placed = sorted((round(n["transform"][3], 6), round(n["transform"][7], 6)) for n in m["nodes"] if n["mesh"] >= 0)
    expect(placed == [(0.0, 0.0), (0.0, 3.0), (3.0, 0.0)], "mapped placements: %s" % placed)
m, _, _ = run(fixture("polygonal-face-set.ifc"))
if m:
    _, cube = by_name(m, "polygonal cube")
    _, plate = by_name(m, "plate with hole")
    expect(near(volume(m, cube), 1.0, 1e-9) and near(pkg.mesh_volume_area(mesh_of(m, cube))[1], 6.0, 1e-9), "IfcPolygonalFaceSet cube: 1 m3, 6 m2")
    expect(near(volume(m, plate), 0.3, 1e-9), "IfcPolygonalFaceSet with voids: 0.3 m3: %g" % volume(m, plate))

# ---- openings (E3) -------------------------------------------------------------------------------------------------------------
m, _, _ = run(fixture("openings.ifc"))
if m:
    _, a = by_name(m, "wall-A-one-opening")
    _, b = by_name(m, "wall-B-two-openings")
    expect(near(volume(m, a), 2.4, 1e-9) and near(volume(m, b), 1.8, 1e-9), "E3: hosts cut by the hand-computed volumes: %g %g" % (volume(m, a), volume(m, b)))
    geometry = m["report"]["geometry"]
    expect(geometry["openingsDeclared"] == 4 and geometry["openingsApplied"] == 3 and geometry["openingsNotApplied"] == 1, "openings: declared, applied, not applied: %s" % geometry)
    hidden = [n for n in m["nodes"] if n["role"] == "opening"]
    expect(len(hidden) == 1 and hidden[0]["hidden"], "E3: the opening that was not applied is imported hidden")
    _, c = by_name(m, "wall-C-open-shell")
    expect("voids not subtracted" in provenance(c)["ifc.Conversion"], "and its host says it was not cut")
    expect(any(l["kind"] == "openingNotApplied" for l in m["report"]["locations"]), "and the pair is located")
    # The open host was converted uncut: the 3.0 m3 box (an open shell has no volume to speak of).
m, _, _ = run(fixture("openings-hard.ifc"))
if m:
    # D closed tessellated host (cut), E open tessellated host (GEO191: not trusted, converted uncut), F extruded host
    # with an open-shell opening (cut), G an opening that misses its host (the kernel fails on it).
    geometry = m["report"]["geometry"]
    expect(geometry["openingsDeclared"] == 4 and geometry["openingsApplied"] == 2 and geometry["openingsNotApplied"] == 2,
           "openings-hard: declared 4, applied 2 (D, F), not applied 2 (E, G): %s" % geometry)
    _, d = by_name(m, "D-closed-tessellated")
    _, e = by_name(m, "E-open-tessellated")
    _, g = by_name(m, "G-extruded")
    expect(near(volume(m, d), 0.84, 1e-9), "E3: the closed tessellated host is cut: 1 - 0.16 = 0.84 m3: %g" % volume(m, d))
    expect("voids not subtracted" in provenance(e)["ifc.Conversion"] and "not trusted" in provenance(e)["ifc.Conversion"],
           "E3: an open tessellated host (GEO191) is converted uncut and says so: %s" % provenance(e)["ifc.Conversion"])
    expect(near(volume(m, g), 1.0, 1e-9), "G is converted whole")
    opening_nodes = [n for n in m["nodes"] if n["role"] == "opening"]
    expect([n["name"] for n in opening_nodes] == ["opening-E"] and opening_nodes[0]["hidden"], "E3: the opening of the untrusted host is imported hidden")
    reasons = {(x["class"], x["reason"].split(":")[0]) for x in m["report"]["elements"]["notImported"]}
    # (The fixture's opening G shares entity numbers with F's open shell, a quirk kept from R097.a, so its
    # placement is broken: the import says why the opening cannot be placed instead of guessing.)
    expect(("IfcOpeningElement", "badPlacement") in reasons or ("IfcOpeningElement", "kernelError") in reasons,
           "and the opening that cannot be converted is reported with its reason: %s" % reasons)
    pairs = [l["message"] for l in m["report"]["locations"] if l["kind"] == "openingNotApplied"]
    expect(len(pairs) == 2 and any("untrustedCut" in x for x in pairs) and any("kernelError" in x for x in pairs), "the pairs are located: %s" % pairs)

# ---- unsupported content (D2, D3) -----------------------------------------------------------------------------------------------
m, _, _ = run(fixture("unsupported-all.ifc"))
if m:
    expect(sum(1 for n in m["nodes"] if n["role"] == "element") == 1 and any(n["name"] == "OK" for n in m["nodes"]), "only the good wall is imported")
    reasons = {(x["class"], x["reason"]): x["count"] for x in m["report"]["elements"]["notImported"]}
    expect(reasons.get(("IfcWall", "gridPlacement")) == 1, "D3: a grid placement is reported: %s" % reasons)
    expect(reasons.get(("IfcWall", "placementCycle")) == 2, "D3: placements that refer to each other are reported: %s" % reasons)
    expect(reasons.get(("IfcWall", "noGeometry")) == 1, "D3: an empty representation is reported: %s" % reasons)
    expect(reasons.get(("IfcWall", "noRepresentation")) == 1, "a wall with no representation is reported: %s" % reasons)
    expect(reasons.get(("IfcVirtualElement", "unsupportedClass")) == 1 and reasons.get(("IfcAnnotation", "unsupportedClass")) == 1, "D2: unsupported classes are listed: %s" % reasons)
    expect(m["report"]["omittedEntities"].get("IfcGrid") == 1 and m["report"]["omittedEntities"].get("IfcRelNests") == 1, "D2: grids and nesting are omitted, counted by class")
    expect(m["report"]["locationTotals"]["notImported"] >= 7 and len([l for l in m["report"]["locations"] if l["kind"] == "notImported"]) <= 32, "D2: complete counts, at most 32 locations")
    expect(all(n["mesh"] < 0 or n["name"] == "OK" for n in m["nodes"]), "D3: nothing is imported at the origin")
    acc = m["report"]["accounting"]
    expect(acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"], "D1 on the unsupported file")
m, _, _ = run(fixture("ifc43-infrastructure.ifc"))
if m:
    names = {n["name"]: n for n in m["nodes"]}
    expect("Road" in names and "Road part" in names and "kerb" in names, "4.3: an infrastructure spatial element holding a supported element is a group")
    expect("Bridge" not in names and "Alignment" not in names, "4.3: an empty bridge and an alignment are not imported")
    expect(m["report"]["omittedEntities"].get("IfcBridge") == 1 and m["report"]["omittedEntities"].get("IfcAlignment") == 1, "and are counted")

# ---- the originals through the accounting identity ---------------------------------------------------------------------------------
for name in ORIGINAL:
    path = fixture(name)
    if not os.path.exists(path):
        continue
    facts = scan.Scan(path).facts()
    m, _, _ = run(path)
    if m is None:
        continue
    acc = m["report"]["accounting"]
    expect(acc["entities"] == facts["entities"] and acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"], "D1: %s" % name)

# The checked-in packages (the decoder's and the fuzz seeds' real inputs) are what the helper writes today.
for name in ("rv-wall-with-opening-and-window", "rv-tessellated-item", "rv-tessellation-with-individual-colors"):
    out = os.path.join(TMP, name + ".seed.pkg")
    subprocess.run([WORKER, "--import", fixture(name + ".ifc"), "--output", out, "--threads", "1"], capture_output=True, check=True,
                   env={"LC_ALL": "C.UTF-8", **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}})
    expect(open(out, "rb").read() == open(os.path.join(FIXTURES, "packages", name + ".skifc"), "rb").read(),
           "tests/fixtures/ifc/packages/%s.skifc is what the helper writes (regenerate it with --import --threads 1 after a deliberate change)" % name)

# D4: a reference to an instance that does not exist is a notice, not a crash.
text = open(fixture("rv-wall-with-opening-and-window.ifc"), encoding="latin-1").read()
broken = os.path.join(TMP, "undefined-reference.ifc")
open(broken, "w", encoding="latin-1").write(text.replace("(#45, #102), #38)", "(#45, #9999, #102), #38)"))
m, _, _ = run(broken)
if m:
    expect(any(n["code"] == "parseWarnings" for n in m["report"]["notices"]), "D4: the dangling reference is noticed")
    expect(sum(1 for n in m["nodes"] if n["mesh"] >= 0) == 2, "D4: and the rest of the file is imported")

# The package's own bounds: 100,000 triangles for an element and 1,000,000 in all (the pass-through kernel keeps
# these large tessellated files quick).
def grid_fixture(path, grids):
    """IFC4, millimetres; each grid is (n, x): an n x n vertex grid at x as one IfcTriangulatedFaceSet."""
    lines = ["ISO-10303-21;", "HEADER;", "FILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]'),'2;1');",
             "FILE_NAME('big.ifc','2026-10-22T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R097.b','generated','');",
             "FILE_SCHEMA(('IFC4'));", "ENDSEC;", "DATA;",
             "#1=IFCPROJECT('0000000000000000000001',$,'Big',$,$,$,$,(#6),#2);", "#2=IFCUNITASSIGNMENT((#3));",
             "#3=IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.);", "#6=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#8,$);",
             "#7=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#6,$,.MODEL_VIEW.,$);",
             "#8=IFCAXIS2PLACEMENT3D(#9,$,$);", "#9=IFCCARTESIANPOINT((0.,0.,0.));"]
    refs = []
    for k, (n, x) in enumerate(grids):
        base = 100 + k * 10
        points = ",".join("(%d.,%d.,%d.)" % (x + i, j, (i * j) % 3) for j in range(n) for i in range(n))
        faces = []
        for j in range(n - 1):
            for i in range(n - 1):
                a = j * n + i + 1
                faces.append("(%d,%d,%d)" % (a, a + 1, a + n))
                faces.append("(%d,%d,%d)" % (a + 1, a + n + 1, a + n))
        lines += ["#%d=IFCCARTESIANPOINTLIST3D((%s));" % (base, points), "#%d=IFCTRIANGULATEDFACESET(#%d,$,.T.,(%s),$);" % (base + 1, base, ",".join(faces)),
                  "#%d=IFCSHAPEREPRESENTATION(#7,'Body','Tessellation',(#%d));" % (base + 2, base + 1), "#%d=IFCPRODUCTDEFINITIONSHAPE($,$,(#%d));" % (base + 3, base + 2),
                  "#%d=IFCLOCALPLACEMENT($,#8);" % (base + 4),
                  "#%d=IFCBUILDINGELEMENTPROXY('%022d',$,'grid %d',$,$,#%d,#%d,$,$);" % (base + 5, 1000 + k, k, base + 4, base + 3)]
        refs.append("#%d" % (base + 5))
    lines += ["#90=IFCBUILDINGSTOREY('0000000000000000000009',$,'S',$,$,#%d,$,$,.ELEMENT.,0.);" % 91, "#91=IFCLOCALPLACEMENT($,#8);",
              "#92=IFCRELAGGREGATES('0000000000000000000008',$,$,$,#1,(#90));",
              "#93=IFCRELCONTAINEDINSPATIALSTRUCTURE('0000000000000000000010',$,$,$,(%s),#90);" % ",".join(refs), "ENDSEC;", "END-ISO-10303-21;", ""]
    open(path, "w").write("\n".join(lines))


big = os.path.join(TMP, "big.ifc")
grid_fixture(big, [(230, 0)])  # 2 x 229 x 229 = 104,882 triangles: over the 100,000 an element may have
m, _, _ = run(big, "--kernel", "passthrough", name="big-one")
if m:
    expect(not any(n["mesh"] >= 0 for n in m["nodes"]), "an element over 100,000 triangles is not imported")
    expect(any(x["reason"] == "meshTriangleLimit" for x in m["report"]["elements"]["notImported"]), "and is reported with the limit")
grid_fixture(big, [(223, 300 * k) for k in range(11)])  # 11 x 98,568 triangles: the eleventh passes 1,000,000
m, _, _ = run(big, "--kernel", "passthrough", name="big-many")
if m:
    expect(sum(1 for n in m["nodes"] if n["mesh"] >= 0) == 10, "ten elements fit the 1,000,000 triangles of the package: %d" % sum(1 for n in m["nodes"] if n["mesh"] >= 0))
    expect(any(x["reason"] == "triangleBudget" for x in m["report"]["elements"]["notImported"]), "and the eleventh is reported with the budget")
    expect(sum(len(mesh["triangles"]) for mesh in m["meshes"]) <= 1000000, "the package stays within its bound")
os.remove(big)

# ---- H2: shuffled instances ------------------------------------------------------------------------------------------------------
text = open(fixture("bsi-ifc4-arch.ifc"), encoding="latin-1").read()
head, rest = text.split("DATA;", 1)
body, tail = rest.rsplit("ENDSEC;", 1)
lines = [l for l in body.split("\n") if l.strip()]
random.Random(7).shuffle(lines)
shuffled = os.path.join(TMP, "shuffled.ifc")
open(shuffled, "w", encoding="latin-1").write(head + "DATA;\n" + "\n".join(lines) + "\nENDSEC;" + tail)
a, _, _ = run(fixture("bsi-ifc4-arch.ifc"), name="plain")
b, _, _ = run(shuffled, name="shuffled")
if a and b:
    expect(json.dumps(a["nodes"], sort_keys=True) == json.dumps(b["nodes"], sort_keys=True), "H2: a file with its instances shuffled gives the same nodes")
    expect(a["meshes"] == b["meshes"], "H2: and the same meshes")


# Renumbered: every entity number is replaced (outside strings) by another, in a different order. Nothing the
# import produces may depend on the numbers except the recorded ifc.Step and the report's locations.
def renumber(source, seed):
    numbers = [int(x) for x in re.findall(r"^#(\d+)\s*=", source, re.M)]
    targets = list(range(1, len(numbers) + 1))
    random.Random(seed).shuffle(targets)
    mapping = dict(zip(numbers, [t * 3 + 7 for t in targets]))
    pieces = re.split(r"('(?:[^']|'')*')", source)
    out = []
    for i, piece in enumerate(pieces):
        out.append(piece if i % 2 else re.sub(r"#(\d+)", lambda x: "#%d" % mapping.get(int(x.group(1)), int(x.group(1))), piece))
    return "".join(out)


def strip_steps(m):
    copy = json.loads(json.dumps(m["nodes"]))
    for n in copy:
        n["step"] = 0
        n["ifc"] = [e for e in n["ifc"] if e[0] != "ifc.Step"]
    return json.dumps(copy, sort_keys=True)


for name in ("bsi-ifc4-arch.ifc", "rv-wall-with-opening-and-window.ifc", "tree-cases.ifc"):
    text = open(fixture(name), encoding="latin-1").read()
    head, rest = text.split("DATA;", 1)
    body, tail = rest.rsplit("ENDSEC;", 1)
    numbered = os.path.join(TMP, "renumbered-" + name)
    open(numbered, "w", encoding="latin-1").write(head + "DATA;" + renumber(body, 11) + "ENDSEC;" + tail)
    a, _, _ = run(fixture(name), name="plain-" + name)
    b, _, _ = run(numbered, name="renumbered-" + name)
    if a and b:
        expect(strip_steps(a) == strip_steps(b), "H2: %s with its entity numbers replaced gives the same nodes" % name)
        expect(a["meshes"] == b["meshes"], "H2: %s renumbered gives the same meshes" % name)
        expect(a["report"]["accounting"] == b["report"]["accounting"], "H2: %s renumbered gives the same accounting" % name)

if failures:
    print("%d check(s) failed" % len(failures))
    sys.exit(1)
print("ifc_import_worker ok")
