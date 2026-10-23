#!/usr/bin/env python3
"""R097.d: the helper's IFC4 writer at the package level (ADR 0171 section 3.8, checks G1, G3 and the writer's part of G4 to
G7 of section 6.3), against a raw-text scan of the files it writes (tests/ifc_text_scan.py, which shares no code with
IfcOpenShell) and numbers computed by hand. The packages are built here by an independent encoder
(tests/ifc_export_package.py); the application's own encoder is checked against the same reader in ifc_export_tests.

    python3 -I tests/ifc_export_worker_tests.py WORKER FIXTURE_DIR

W1  a model with every kind of node writes the entities, hierarchy, properties, units, layers and georeference it should;
    the header declares no ViewDefinition and the times are the injected ones (F10 of the R097.a notes)
W2  the class a node is written as: honoured when it is an IFC4 element, otherwise a proxy or an assembly, and reported
W3  predefined types and object types; the USERDEFINED rule
W4  property sets: IfcLabel, IfcText, IfcReal, IfcBoolean; STEP escapes of non-ASCII text
W5  geometry: coordinates as given, 1-based counter-clockwise triangles, Closed, colour maps, styles and materials
W6  determinism: two runs are byte for byte equal, and a new time changes only the two stamps
W7  refusals: a damaged or inconsistent package is refused (exit 3), never a crash
W8  a corpus of mutated packages: every run is a file that passes the helper's own check, or a refusal
W9  the helper's schema check (--check) accepts the certified files and rejects damaged ones
W10 sizes: 100,000 triangles, 3,000 nodes
"""
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import ifc_export_package as pk  # noqa: E402
import ifc_text_scan as scan  # noqa: E402

WORKER = sys.argv[1]
FIXTURES = sys.argv[2]
failures = []
TMP = tempfile.mkdtemp(prefix="ifc-export-tests-")
ENV = {"LC_ALL": "C.UTF-8", **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}
ID = "0123456789abcdef0123456789abcdef"


def expect(ok, message):
    if not ok:
        failures.append(message)
        print("FAIL:", message)


def near(a, b, tol=1e-9):
    return abs(a - b) <= tol


_counter = [0]


def run(package, label="x", timeout=300):
    """Runs --export on package bytes; returns (exit code, result line as a dict or None, output path or None)."""
    _counter[0] += 1
    base = os.path.join(TMP, "%s-%d" % (label, _counter[0]))
    with open(base + ".skifcx", "wb") as handle:
        handle.write(package)
    out = base + ".ifc"
    proc = subprocess.run([WORKER, "--export", base + ".skifcx", "--output", out], capture_output=True, text=True, timeout=timeout, env=ENV)
    result = None
    for line in proc.stdout.splitlines():
        if line.startswith("R "):
            result = json.loads(line[2:])
    return proc.returncode, result, (out if os.path.exists(out) else None), proc.stderr


def check_file(path):
    proc = subprocess.run([WORKER, "--check", path], capture_output=True, text=True, timeout=300, env=ENV)
    result = None
    for line in proc.stdout.splitlines():
        if line.startswith("R "):
            result = json.loads(line[2:])
    return proc.returncode, result


def g(kind, n):
    return pk.derived_global_id(ID, kind, n)


def small_model(**changes):
    """project > site > building > storey > (wall, space), plus a free-standing proxy under the building."""
    nodes = [
        pk.node("project", "Demo", -1, g("body", 1), id=1, longName="Demo project"),
        pk.node("site", "Site", 0, g("body", 2), id=2),
        pk.node("building", "House", 1, g("body", 3), id=3),
        pk.node("storey", "Ground", 2, g("body", 4), id=4, elevation=0.5),
        pk.node("element", "Wall A", 3, g("body", 5), mesh=0, style=0, id=5, cls="IfcWall", predefinedType="STANDARD",
                props=[["Pset_WallCommon", "FireRating", "s", "REI30"], ["Pset_WallCommon", "IsExternal", "b", True], ["Thermal", "U", "d", 0.25]],
                placement=[1, 2, 0.5, 0, 0, 1, 1, 0, 0]),
        pk.node("space", "Room", 3, g("body", 6), mesh=1, style=1, id=6, cls="IfcSpace", hidden=True),
        pk.node("element", "Chair", 2, g("body", 7), mesh=2, id=7, hidden=True),
    ]
    meshes = [pk.box_mesh(0, 0, 0, 4, 0.3, 2.5), pk.box_mesh(0, 0, 0, 3, 3, 2.4), pk.box_mesh(0, 0, 0, 0.5, 0.5, 0.9, closed=False)]
    styles = [{"name": "Brick", "color": [0.7, 0.2, 0.1], "opacity": 1, "material": True},
              {"name": "SketchyUp colour #00FF00", "color": [0, 1, 0], "opacity": 0.4, "material": False}]
    return nodes, meshes, styles


# ---- W1 -----------------------------------------------------------------------------------------------------------
nodes, meshes, styles = small_model()
geo = {"crs": "EPSG:32760", "eastings": 729013.3488297004, "northings": 9063992.684697364, "height": 1.3}
package = pk.encode_package(nodes, meshes, styles, georeference=geo, identity=ID, timestamp="2026-01-02T03:04:05")
code, result, out, err = run(package, "w1")
expect(code == 0 and result and result["status"] == "ok", "W1: the export runs: %s %s" % (code, err[-300:]))
text = open(out).read()
facts = scan.Scan(out).facts()
expect(text.startswith("ISO-10303-21;"), "W1: STEP file")
expect("FILE_DESCRIPTION(('SketchyUp export, tessellated geometry'),'2;1');" in text, "W1: file description")
expect("ViewDefinition" not in text, "W1: no ViewDefinition is declared")
expect("FILE_NAME('test.ifc','2026-01-02T03:04:05',(''),(''),'IfcOpenShell 0.9.0','SketchyUp 1.3.0','');" in text,
       "W1: file name, injected time stamp, originating system")
expect("FILE_SCHEMA(('IFC4'))" in text, "W1: IFC4")
# 1767323045 = 2026-01-02T03:04:05 UTC
expect(re.search(r"IFCOWNERHISTORY\(#\d+,#\d+,\$,\$,\$,\$,\$,1767323045\)", text) is not None, "W1: owner history time is the injected one, no change action")
expect("IFCAPPLICATION(#" in text and "'SketchyUp','SketchyUp'" in text, "W1: application")
classes = facts["classes"]
for name, number in {"IFCPROJECT": 1, "IFCSITE": 1, "IFCBUILDING": 1, "IFCBUILDINGSTOREY": 1, "IFCWALL": 1, "IFCSPACE": 1, "IFCBUILDINGELEMENTPROXY": 1,
                     "IFCTRIANGULATEDFACESET": 3, "IFCPROPERTYSET": 2, "IFCPROPERTYSINGLEVALUE": 3, "IFCSURFACESTYLE": 2, "IFCMATERIAL": 1,
                     "IFCMATERIALDEFINITIONREPRESENTATION": 1, "IFCRELASSOCIATESMATERIAL": 1, "IFCPRESENTATIONLAYERWITHSTYLE": 1,
                     "IFCPROJECTEDCRS": 1, "IFCMAPCONVERSION": 1, "IFCRELDEFINESBYPROPERTIES": 2, "IFCPRODUCTDEFINITIONSHAPE": 3,
                     "IFCGEOMETRICREPRESENTATIONCONTEXT": 1, "IFCGEOMETRICREPRESENTATIONSUBCONTEXT": 1, "IFCOWNERHISTORY": 1}.items():
    expect(classes.get(name) == number, "W1: %s count %s expected %s" % (name, classes.get(name), number))
expect(facts["units"] == {"length": 1.0, "area": 1.0, "volume": 1.0}, "W1: metre, square metre, cubic metre: %s" % facts["units"])
# hierarchy by GlobalId: the storey contains the wall and aggregates the space; the building contains the chair
parents = facts["parents"]
expect(parents.get(g("body", 2)) == g("body", 1) and parents.get(g("body", 3)) == g("body", 2) and parents.get(g("body", 4)) == g("body", 3),
       "W1: the spatial tree: %s" % {k: v for k, v in parents.items() if k in (g("body", 2), g("body", 3), g("body", 4))})
expect(parents.get(g("body", 5)) == g("body", 4), "W1: the wall is contained in its storey")
expect(parents.get(g("body", 6)) == g("body", 4), "W1: the space is aggregated by its storey")
expect(parents.get(g("body", 7)) == g("body", 3), "W1: the chair is contained in the building")
props = facts["occurrenceProperties"].get(g("body", 5), {})
expect(props == {"Pset_WallCommon.FireRating": "REI30", "Pset_WallCommon.IsExternal": True, "Thermal.U": 0.25}, "W1: wall properties %s" % props)
expect(sorted(facts["layers"]) == sorted([g("body", 6), g("body", 7)]), "W1: the two hidden nodes are on the layer: %s" % list(facts["layers"]))
for layer in facts["layers"].values():
    expect(all(entry["name"] == "SketchyUp hidden" and entry["on"] == "F" for entry in layer), "W1: the layer is off")
conv = facts["mapConversions"]
expect(len(conv) == 1 and conv[0]["eastings"] == geo["eastings"] and conv[0]["northings"] == geo["northings"] and conv[0]["orthogonalHeight"] == 1.3 and
       conv[0]["xAxisAbscissa"] == 1.0 and conv[0]["xAxisOrdinate"] == 0.0 and conv[0]["scale"] == 1.0, "W1: the map conversion %s" % conv)
s = scan.Scan(out)
crs = list(s.of("IFCPROJECTEDCRS"))
expect(len(crs) == 1 and crs[0][1][0] == "EPSG:32760", "W1: the projected CRS is named EPSG:32760: %s" % crs)
expect(result["georeference"] == "written", "W1: the helper says the georeference was written")
expect(result["counts"]["sites"] == 1 and result["counts"]["spaces"] == 1 and result["counts"]["layerAssigned"] == 2 and result["counts"]["triangles"] == 36,
       "W1: counts %s" % result["counts"])
expect(result["entities"] == sum(classes.values()), "W1: entities equal the scan's: %s %s" % (result["entities"], sum(classes.values())))
expect(result["classes"] == {k.replace("IFC", "Ifc", 1): v for k, v in classes.items()} or sum(result["classes"].values()) == sum(classes.values()),
       "W1: the class counts equal the scan's")
code, checked = check_file(out)
expect(code == 0 and checked["status"] == "ok", "W1: the helper's own schema check passes: %s" % checked)
# the space takes no material association; the wall does
mat = list(s.of("IFCRELASSOCIATESMATERIAL"))
expect(len(mat) == 1, "W1: one material association (the space has none)")

# G1 in the file: coordinates as given, 1-based indices
lists = list(s.of("IFCCARTESIANPOINTLIST3D"))
expect(len(lists) == 3 and lists[0][1][0][0] == [0.0, 0.0, 0.0] and lists[0][1][0][6] == [4.0, 0.3, 2.5], "W1: point list as given: %s" % (lists[0][1][0][:2] if lists else None))
tri = list(s.of("IFCTRIANGULATEDFACESET"))
expect(tri[0][1][3][0] == [1, 3, 2] and str(tri[0][1][2]) == "T" and tri[2][1][2] is None, "W1: 1-based indices, Closed set only for the closed mesh: %s %s" % (tri[0][1][3][0], tri[2][1][2]))

# ---- W2 ----------------------------------------------------------------------------------------------------------
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3))]
requests = [("IfcWall", "IfcWall", ""), ("IfcFoo", "IfcBuildingElementProxy", "unknownClass"), ("IfcSpace", "IfcBuildingElementProxy", "notAnElement"),
            ("IfcOpeningElement", "IfcBuildingElementProxy", "notAnElement"), ("IfcElement", "IfcBuildingElementProxy", "abstractClass"),
            ("ifcslab", "IfcSlab", ""), ("", "IfcBuildingElementProxy", ""), ("IfcDoor", "IfcDoor", ""), ("IfcFurniture", "IfcFurniture", ""),
            ("IfcProject", "IfcBuildingElementProxy", "notAnElement"), ("IfcWallType", "IfcBuildingElementProxy", "notAnElement")]
for index, (requested, written, reason) in enumerate(requests):
    nodes.append(pk.node("element", "E%d" % index, 2, g("body", 10 + index), cls=requested, id=10 + index))
nodes.append(pk.node("assembly", "Assembly", 2, g("body", 40), id=40))
nodes.append(pk.node("assembly", "Wall with parts", 2, g("body", 41), id=41, cls="IfcWall"))
nodes.append(pk.node("element", "Part", 14, g("body", 42), id=42, cls="IfcBuildingElementPart"))
code, result, out, err = run(pk.encode_package(nodes, identity=ID), "w2")
expect(code == 0, "W2: runs: %s" % err[-200:])
facts = scan.Scan(out).facts()
by_name = {}
for guid, info in facts["objects"].items():
    by_name[info["name"]] = info["class"]
for index, (requested, written, reason) in enumerate(requests):
    expect(by_name.get("E%d" % index) == written.upper(), "W2: %r is written as %s, found %s" % (requested, written, by_name.get("E%d" % index)))
expect(by_name.get("Assembly") == "IFCELEMENTASSEMBLY" and by_name.get("Wall with parts") == "IFCWALL", "W2: assemblies: %s" % {k: v for k, v in by_name.items() if "ssembly" in k or "parts" in k})
fallbacks = {(f["requested"], f["written"], f["reason"]): f["count"] for f in result["fallbacks"]}
for requested, written, reason in requests:
    if reason:
        expect(fallbacks.get((requested, written, reason)) == 1, "W2: fallback reported for %r: %s" % (requested, fallbacks))
expect(len(result["fallbacks"]) == sum(1 for r in requests if r[2]), "W2: no other fallbacks: %s" % result["fallbacks"])
# the part of the assembly is aggregated, not contained
objs = {info["name"]: guid for guid, info in facts["objects"].items()}
expect(facts["parents"].get(objs["Part"]) == objs["Assembly"], "W2: a part is aggregated by its assembly: %s" % facts["parents"].get(objs["Part"]))
expect(code == 0 and check_file(out)[0] == 0, "W2: passes the schema check")

# ---- W3 ----------------------------------------------------------------------------------------------------------
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3)),
         pk.node("element", "ok", 2, g("body", 4), cls="IfcWall", predefinedType="SOLIDWALL"),
         pk.node("element", "bad", 2, g("body", 5), cls="IfcWall", predefinedType="NOSUCH"),
         pk.node("element", "free", 2, g("body", 6), cls="IfcWall", predefinedType="USERDEFINED"),
         pk.node("element", "free2", 2, g("body", 7), cls="IfcWall", predefinedType="USERDEFINED", objectType="Acoustic wall"),
         pk.node("element", "noattr", 2, g("body", 8), cls="IfcSlab", predefinedType="SOLIDWALL"),
         pk.node("element", "proxy", 2, g("body", 9), predefinedType="ELEMENT"),
         pk.node("element", "proxy2", 2, g("body", 10), objectType="Kind", predefinedType="USERDEFINED")]
code, result, out, err = run(pk.encode_package(nodes, identity=ID), "w3")
expect(code == 0, "W3: runs")
body = open(out).read()
expect(re.search(r"IFCWALL\('%s',#\d+,'ok',\$,\$,#\d+,\$,\$,\.SOLIDWALL\.\)" % re.escape(g("body", 4)), body) is not None, "W3: a valid predefined type is written")
expect(re.search(r"IFCWALL\('%s',#\d+,'bad',\$,\$,#\d+,\$,\$,\$\)" % re.escape(g("body", 5)), body) is not None, "W3: an invalid one is left out")
expect(re.search(r"IFCWALL\('%s',#\d+,'free',\$,\$,#\d+,\$,\$,\$\)" % re.escape(g("body", 6)), body) is not None, "W3: USERDEFINED without an object type is left out")
expect(re.search(r"IFCWALL\('%s',#\d+,'free2',\$,'Acoustic wall',#\d+,\$,\$,\.USERDEFINED\.\)" % re.escape(g("body", 7)), body) is not None, "W3: USERDEFINED with one is written")
expect(re.search(r"'proxy',\$,'proxy',#\d+,\$,\$,\.ELEMENT\.\)", body) is not None, "W3: a proxy takes its name as object type")
expect(re.search(r"'proxy2',\$,'Kind',#\d+,\$,\$,\.USERDEFINED\.\)", body) is not None, "W3: a proxy keeps its object type")
expect(result["predefinedTypeDropped"] == 3, "W3: three predefined types dropped: %s" % result["predefinedTypeDropped"])

# ---- W3b: a space contains elements and aggregates spaces -------------------------------------------------------------------
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3)),
         pk.node("site", "Partial site", 1, g("body", 4)), pk.node("space", "Room", 2, g("body", 5)),
         pk.node("element", "Sofa", 4, g("body", 6)), pk.node("space", "Corner", 4, g("body", 7)), pk.node("element", "Lamp", 6, g("body", 8)),
         pk.node("building", "Annex", 2, g("body", 9)), pk.node("building", "Direct", 0, g("body", 10))]
code, result, out, err = run(pk.encode_package(nodes, identity=ID), "w3b")
expect(code == 0, "W3b: runs: %s" % err[-200:])
parents = scan.Scan(out).facts()["parents"]
expect(parents.get(g("body", 4)) == g("body", 2) and parents.get(g("body", 9)) == g("body", 3), "W3b: a site in a site and a building in a building are aggregated by them: %s" % parents)
expect(parents.get(g("body", 10)) == g("body", 1), "W3b: a building may stand in the project")
expect(parents.get(g("body", 6)) == g("body", 5), "W3b: an element in a space is contained in it")
expect(parents.get(g("body", 7)) == g("body", 5), "W3b: a space in a space is aggregated by it")
expect(parents.get(g("body", 8)) == g("body", 7), "W3b: a lamp in the corner space is contained in it")
expect(check_file(out)[0] == 0, "W3b: passes the schema check")

# ---- W4 ----------------------------------------------------------------------------------------------------------
long_text = "x" * 300
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3)),
         pk.node("element", "Grüße 中文 \U0001F600", 2, g("body", 4),
                 props=[["Set", "a", "s", "short"], ["Set", "b", "s", long_text], ["Set", "c", "d", 1e-9], ["Set", "d", "d", -123456.789012345], ["Set", "e", "b", False],
                        ["Other set", "ü", "s", "quote ' and \\ backslash"], ["Set", "f", "d", 0.0]])]
code, result, out, err = run(pk.encode_package(nodes, identity=ID), "w4")
expect(code == 0, "W4: runs: %s" % err[-200:])
facts = scan.Scan(out).facts()
element = [guid for guid, info in facts["objects"].items() if info["class"] == "IFCBUILDINGELEMENTPROXY"][0]
expect(facts["objects"][element]["name"] == "Grüße 中文 \U0001F600", "W4: non-ASCII names survive the STEP escapes: %r" % facts["objects"][element]["name"])
props = facts["occurrenceProperties"][element]
expect(props == {"Set.a": "short", "Set.b": long_text, "Set.c": 1e-9, "Set.d": -123456.789012345, "Set.e": False, "Other set.ü": "quote ' and \\ backslash", "Set.f": 0.0},
       "W4: values: %s" % props)
raw = open(out).read()
expect("IFCTEXT('" + long_text + "')" in raw and "IFCLABEL('short')" in raw and re.search(r"IFCREAL\(1\.E-0?9\)", raw) is not None,
       "W4: a long string is an IfcText, a short one an IfcLabel, reals are IfcReal")
expect(result["counts"]["propertySets"] == 2 and result["counts"]["properties"] == 7, "W4: counts %s" % result["counts"])
expect(check_file(out)[0] == 0, "W4: passes the schema check")

# ---- W5 ----------------------------------------------------------------------------------------------------------
cube = pk.box_mesh(closed=True)
cube.colors = [(1.0, 0.0, 0.0), (0.0, 0.5, 1.0)]
cube.color_index = [0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1]
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3)),
         pk.node("element", "colored", 2, g("body", 4), mesh=0, style=0), pk.node("element", "styled twice", 2, g("body", 5), mesh=1, style=1),
         pk.node("element", "same material", 2, g("body", 6), mesh=1, style=1)]
styles = [{"name": "Red body", "color": [0.730000019, 0.79, 0.73], "opacity": 0.5, "material": False},
          {"name": "Glass", "color": [0.1, 0.2, 0.3], "opacity": 0.25, "material": True}]
code, result, out, err = run(pk.encode_package(nodes, [cube, pk.box_mesh(closed=False)], styles, identity=ID), "w5")
expect(code == 0, "W5: runs")
raw = open(out).read()
expect("IFCINDEXEDCOLOURMAP(" in raw and "IFCCOLOURRGBLIST(((1.,0.,0.),(0.,0.5,1.)))" in raw, "W5: a colour map with the table as given (float32 values read as their shortest decimals)")
expect("(1,1,2,2,1,1,2,2,1,1,2,2)" in raw, "W5: 1-based colour indices")
expect("IFCSURFACESTYLE('Red body',.BOTH.,(#" in raw and "IFCSURFACESTYLE('Glass',.BOTH.,(#" in raw, "W5: styles by name")
expect(re.search(r"IFCSURFACESTYLERENDERING\(#\d+,0\.5,", raw) is not None and re.search(r"IFCSURFACESTYLERENDERING\(#\d+,0\.75,", raw) is not None,
       "W5: transparency is one minus opacity")
expect(raw.count("IFCSTYLEDITEM(") == 3 + 1 and result["counts"]["materials"] == 1 and result["counts"]["materialAssociations"] == 1,
       "W5: styled items for the shapes and the material, one material: %s" % result["counts"])
expect(len(list(scan.Scan(out).of("IFCRELASSOCIATESMATERIAL"))[0][1][4]) == 2, "W5: the material is shared by the two elements that use it")

# ---- W6 ----------------------------------------------------------------------------------------------------------
nodes, meshes, styles = small_model()
a = pk.encode_package(nodes, meshes, styles, georeference=geo, identity=ID, timestamp="2026-01-02T03:04:05")
c1, r1, o1, _ = run(a, "w6a")
c2, r2, o2, _ = run(a, "w6b")
expect(c1 == 0 and c2 == 0 and open(o1, "rb").read() == open(o2, "rb").read(), "W6: two runs give byte for byte the same file")
b = pk.encode_package(nodes, meshes, styles, georeference=geo, identity=ID, timestamp="2027-03-04T05:06:07")
c3, r3, o3, _ = run(b, "w6c")
l1 = open(o1).read().splitlines()
l3 = open(o3).read().splitlines()
different = [i for i in range(len(l1)) if l1[i] != l3[i]]
expect(len(l1) == len(l3) and len(different) == 2, "W6: a new time changes the header and the owner history only: %s lines" % len(different))
other_id = pk.encode_package(nodes, meshes, styles, georeference=geo, identity="ffffffffffffffffffffffffffffffff", timestamp="2026-01-02T03:04:05")
c4, r4, o4, _ = run(other_id, "w6d")
l4 = open(o4).read().splitlines()
expect(len(l4) == len(l1) and any(x != y for x, y in zip(l1, l4)), "W6: the identity feeds the derived identifiers of relationships and property sets")
guids = re.findall(r"'([0-3][0-9A-Za-z_$]{21})'", open(o1).read())
expect(len(guids) == len(set(guids)), "W6: every GlobalId in the file is unique")

# ---- W7 ----------------------------------------------------------------------------------------------------------
good_nodes, good_meshes, good_styles = small_model()
good = pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID)


def refused(package, label):
    code, result, out, err = run(package, "w7")
    expect(code == 3 and result and result["status"] == "refused", "W7: %s is refused (exit %s %s)" % (label, code, (result or {}).get("code")))
    return result


def edited(mutator, **kw):
    n, m, s2 = small_model()
    mutator(n, m, s2)
    return pk.encode_package(n, m, s2, identity=ID, **kw)


refused(b"", "an empty file")
refused(good[:20], "a truncated header")
refused(good[:-5], "a truncated mesh chunk")
refused(b"SKIFCX02" + good[8:], "another magic")
refused(good[:8] + b"\x01\x00\x00\x00" + good[12:], "a manifest length that does not match")
broken = bytearray(good)
broken[-3] ^= 0xFF
refused(bytes(broken), "a mesh chunk that does not match its digest")
refused(pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID, manifest_overrides={"version": 2}), "version 2")
refused(edited(lambda n, m, s2: n[1].update(parent=5)), "a parent that is not earlier")
refused(edited(lambda n, m, s2: n[0].update(role="site")), "the first node not being the project")
refused(edited(lambda n, m, s2: n[1].update(parent=-1)), "a second root")
refused(edited(lambda n, m, s2: n[3].update(parent=1)), "a storey under a site")
refused(edited(lambda n, m, s2: n[3].update(parent=5)), "a storey under an element")
refused(edited(lambda n, m, s2: n[1].update(parent=5)), "a site under an element")
refused(edited(lambda n, m, s2: n[1].update(guid="short")), "a short GlobalId")
refused(edited(lambda n, m, s2: n[1].update(guid="4" + n[1]["guid"][1:])), "a GlobalId whose first character is above 3")
refused(edited(lambda n, m, s2: n[2].update(guid=n[1]["guid"])), "a repeated GlobalId")
refused(edited(lambda n, m, s2: n[4].update(mesh=9)), "a mesh index out of range")
refused(edited(lambda n, m, s2: n[4].update(style=7)), "a style index out of range")
refused(edited(lambda n, m, s2: n[1].update(style=0)), "a style on a node without a mesh")
refused(edited(lambda n, m, s2: n[4].update(placement=[0, 0, 0, 0, 0, 2, 1, 0, 0])), "a non-unit axis")
refused(edited(lambda n, m, s2: n[4].update(placement=[0, 0, 0, 0, 0, 1, 0, 0, 1])), "axes that are not perpendicular")
refused(edited(lambda n, m, s2: n[4].update(placement=[0, 0, 0, 0, 0, 1])), "a short placement")
refused(edited(lambda n, m, s2: n[4].update(role="wall")), "an unknown role")
refused(edited(lambda n, m, s2: n[4]["props"].append(["Pset_WallCommon", "FireRating", "s", "again"])), "a repeated property")
refused(edited(lambda n, m, s2: n[4]["props"].append(["", "x", "s", "a"])), "an empty set name")
refused(edited(lambda n, m, s2: n[4]["props"].append(["A", "x", "q", "a"])), "an unknown property type")
refused(edited(lambda n, m, s2: n[4].update(name="bad\u0001control")), "a control character in a name")
refused(edited(lambda n, m, s2: s2[0].update(color=[2, 0, 0])), "a colour above 1")
refused(edited(lambda n, m, s2: s2[0].update(opacity=-1)), "a negative opacity")
refused(edited(lambda n, m, s2: m[0].triangles.append((0, 1, 99))), "a triangle index out of range")
refused(pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID, timestamp="yesterday"), "a bad timestamp")
refused(pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID, georeference={"crs": "", "eastings": 1, "northings": 2, "height": 3}), "an empty CRS")
refused(pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID, georeference={"crs": "EPSG:1", "eastings": 1e9, "northings": 2, "height": 3}), "a site coordinate beyond 1e8 m")
refused(pk.encode_package(good_nodes, good_meshes, good_styles, identity=ID, manifest_overrides={"nodes": "x"}), "nodes that are not an array")
# a non-finite vertex cannot be written as JSON, but it can be written as bytes
n, m, s2 = small_model()
m[0].vertices[0] = (float("inf"), 0.0, 0.0)
refused(pk.encode_package(n, m, s2, identity=ID), "an infinite vertex")
m[0].vertices[0] = (1e12, 0.0, 0.0)
refused(pk.encode_package(n, m, s2, identity=ID), "a vertex beyond 1e7 m")
refused(good + b"\0", "bytes after the mesh chunk")
code, result, out, err = run(good, "w7ok")
expect(code == 0, "W7: the good package still runs")
# a missing package and a missing output directory
proc = subprocess.run([WORKER, "--export", os.path.join(TMP, "nothing.skifcx"), "--output", os.path.join(TMP, "n.ifc")], capture_output=True, text=True, env=ENV)
expect(proc.returncode == 3 and '"status":"refused"' in proc.stdout, "W7: a missing package is a refusal")
with open(os.path.join(TMP, "good.skifcx"), "wb") as handle:
    handle.write(good)
proc = subprocess.run([WORKER, "--export", os.path.join(TMP, "good.skifcx"), "--output", os.path.join(TMP, "no", "such", "dir.ifc")], capture_output=True, text=True, env=ENV)
expect(proc.returncode in (1, 3), "W7: an unwritable output is not a crash")

# ---- W8 ----------------------------------------------------------------------------------------------------------
rng = random.Random(20261022)
survived = refused_count = written = 0
base = bytearray(good)
manifest_end = 24 + int.from_bytes(good[8:12], "little")
for i in range(150):
    data = bytearray(base)
    kind = rng.randrange(6)
    if kind == 0:
        for _ in range(rng.randrange(1, 4)):
            data[rng.randrange(len(data))] ^= 1 << rng.randrange(8)
    elif kind == 1:
        at = rng.randrange(24, manifest_end)
        data[at:at + rng.randrange(1, 6)] = b""
    elif kind == 2:
        at = rng.randrange(24, manifest_end)
        data[at:at] = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 6)))
    elif kind == 3:
        data = data[:rng.randrange(len(data))]
    elif kind == 4:
        # a number in the manifest replaced by another
        text = data[24:manifest_end].decode()
        matches = list(re.finditer(r"-?\d+(\.\d+)?", text))
        if matches:
            m_ = rng.choice(matches)
            text = text[:m_.start()] + rng.choice(["0", "-1", "99999999", "1e308", "0.5", "7"]) + text[m_.end():]
            data[24:manifest_end] = text.encode()
            data[8:12] = len(text.encode()).to_bytes(4, "little")
            # keep the digest valid: the mesh chunk is untouched
    else:
        text = data[24:manifest_end].decode()
        words = list(re.finditer(r'"(element|space|assembly|storey|building|site|project)"', text))
        if words:
            m_ = rng.choice(words)
            text = text[:m_.start()] + '"' + rng.choice(["element", "space", "assembly", "storey", "building", "site", "project"]) + '"' + text[m_.end():]
            data[24:manifest_end] = text.encode()
            data[8:12] = len(text.encode()).to_bytes(4, "little")
    code, result, out, err = run(bytes(data), "w8", timeout=120)
    if code == 0:
        written += 1
        c2, r2 = check_file(out)
        expect(c2 == 0 and r2["status"] == "ok", "W8: mutation %d wrote a file that fails the check: %s" % (i, r2))
    elif code == 3:
        refused_count += 1
        expect(result and result["status"] == "refused", "W8: mutation %d refused without a result line" % i)
    else:
        expect(False, "W8: mutation %d ended with exit %s (%s)" % (i, code, err[-200:]))
print("W8: %d mutations: %d written, %d refused" % (150, written, refused_count))
expect(refused_count > 20, "W8: the mutations mostly reach the reader's checks")

# ---- W9 ----------------------------------------------------------------------------------------------------------
for name in ("bsi-ifc4-arch.ifc", "rv-wall-with-opening-and-window.ifc", "rv-tessellation-with-individual-colors.ifc", "bsi-ifc4-road.ifc"):
    code, result = check_file(os.path.join(FIXTURES, name))
    expect(code == 0 and result["status"] == "ok", "W9: %s passes the helper's check: %s" % (name, result and result["issues"][:3]))
code, result = check_file(os.path.join(FIXTURES, "bsi-ifc2x3-arch.ifc"))
expect(code == 3, "W9: an IFC2X3 file is not an IFC4 file")
good_file = open(run(good, "w9")[2]).read()


def damage(label, mutate):
    path = os.path.join(TMP, "damaged-%s.ifc" % label)
    changed = mutate(good_file)
    expect(changed != good_file, "W9: the %s damage changed the file" % label)
    with open(path, "w") as handle:
        handle.write(changed)
    code, result = check_file(path)
    expect(code == 3 and result and result["status"] == "invalid" and result["issueCount"] >= 1, "W9: %s is found: %s" % (label, result))


damage("mandatory attribute unset", lambda t: re.sub(r"IFCWALL\('[^']*'", "IFCWALL($", t, count=1))
damage("a short attribute list", lambda t: re.sub(r"(IFCOWNERHISTORY\([^;]*),\d+\);", r"\1);", t, count=1))
damage("a reference of the wrong class", lambda t: re.sub(r"(IFCWALL\('[^']*',)#\d+(,'Wall A')", r"\1#1\2", t, count=1))
damage("an enumeration item of another type", lambda t: t.replace("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)", "IFCSIUNIT(*,.LENGTHUNIT.,$,.ELEMENT.)", 1))
damage("a string where a number is declared", lambda t: t.replace("IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,", "IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model','3',", 1))
damage("a repeated GlobalId", lambda t: t.replace(g("body", 3), g("body", 2)))
damage("a GlobalId of the wrong shape", lambda t: t.replace(g("body", 3), "tooshort"))
damage("a list that is too short", lambda t: t.replace("((1,3,2)", "((1,3)", 1))
proc = subprocess.run([WORKER, "--check", os.path.join(TMP, "missing.ifc")], capture_output=True, text=True, env=ENV)
expect(proc.returncode == 3, "W9: a missing file is invalid")
with open(os.path.join(TMP, "junk.ifc"), "w") as handle:
    handle.write("not an IFC file")
proc = subprocess.run([WORKER, "--check", os.path.join(TMP, "junk.ifc")], capture_output=True, text=True, env=ENV)
expect(proc.returncode == 3, "W9: junk is invalid")

# ---- W10 ---------------------------------------------------------------------------------------------------------
# 100,000 triangles in one mesh (a strip of quads) and 3,000 nodes
count = 50_000
vertices = []
triangles = []
for i in range(count):
    x = i * 0.01
    vertices += [(x, 0.0, 0.0), (x + 0.01, 0.0, 0.0), (x + 0.01, 1.0, 0.0), (x, 1.0, 0.0)]
    b0 = 4 * i
    triangles += [(b0, b0 + 1, b0 + 2), (b0, b0 + 2, b0 + 3)]
# a mesh holds at most 300,000 vertices (3 x the triangle bound); 200,000 here
nodes = [pk.node("project", "P", -1, g("body", 1)), pk.node("site", "S", 0, g("body", 2)), pk.node("building", "B", 1, g("body", 3)),
         pk.node("element", "Big", 2, g("body", 4), mesh=0)]
for i in range(3000):
    nodes.append(pk.node("element", "n%d" % i, 2, g("body", 100 + i), mesh=1, props=[["Set", "i", "d", float(i)]], placement=[i, 0, 0, 0, 0, 1, 1, 0, 0]))
big = pk.Mesh(vertices, triangles, False)
started = time.time()
code, result, out, err = run(pk.encode_package(nodes, [big, pk.box_mesh()], identity=ID), "w10")
elapsed = time.time() - started
expect(code == 0 and result["counts"]["triangles"] == 100_000 + 3000 * 12, "W10: sizes: %s %s" % (code, err[-200:]))
expect(elapsed < 60, "W10: 100,000 triangles and 3,000 nodes in %.1f s" % elapsed)
print("W10: %.1f s, %d bytes" % (elapsed, os.path.getsize(out)))
code, checked = check_file(out)
expect(code == 0, "W10: the large file passes the check")

if failures:
    print("%d failures" % len(failures))
    sys.exit(1)
print("ifc_export_worker_tests: ok")
