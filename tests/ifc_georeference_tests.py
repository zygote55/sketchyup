#!/usr/bin/env python3
"""R097.c: georeferencing on import (ADR 0171 section 3.6, checks F1 to F4 of section 6.3) at the package level, against
a raw-text scan of the files (tests/ifc_text_scan.py, no IFC library) and numbers computed by hand.

    python3 -I tests/ifc_georeference_tests.py WORKER FIXTURE_DIR

F1  a map conversion without a rotation sets the site origin: bsi-ifc4-road.ifc (E 729011.2258823584, N 9063960.607644705,
    H 0, EPSG:32760), the IFC4.3 IfcMapConversionScaled with unit factors, a metre project with no map unit.
F2  the certified 60 degree samples (bsi-ifc4-arch, bsi-ifc43-arch, bsi-ifc4-landscaping) keep the project axes and report
    the whole conversion; with the grid mode every vertex is the hand-computed mapped coordinate minus the translation.
F3  a model beyond 1e6 m is refused, naming the extent, with a map conversion or without.
F4  a map unit of US survey feet, a scale that is not the project unit, a non-identity world coordinate system, two map
    conversions, two Model contexts, a conversion of a Plan context, factors, a bad axis, and IFC2X3 with the community
    ePSets are each handled as the ADR says.
The application's side (the site origin in the document, schema 29, the report) is checked in ifc_import_tests.
"""
import math
import os
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
TMP = tempfile.mkdtemp(prefix="ifc-georef-tests-")


def expect(ok, message):
    if not ok:
        failures.append(message)
        print("FAIL:", message)


def near(a, b, tol=1e-9):
    return abs(a - b) <= tol


def fixture(name):
    return os.path.join(FIXTURES, name)


def run(name, mode="keep", expect_status="ok", extra=()):
    path = fixture(name)
    out = os.path.join(TMP, name + "." + mode + ".pkg")
    if os.path.exists(out):
        os.remove(out)
    proc = subprocess.run([WORKER, "--import", path, "--output", out, "--threads", "1", "--georef", mode, *extra], capture_output=True, text=True,
                          timeout=300, env={"LC_ALL": "C.UTF-8", **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}})
    result = None
    for line in proc.stdout.splitlines():
        if line.startswith("R "):
            import json
            result = json.loads(line[2:])
    if expect_status == "ok":
        if proc.returncode != 0 or not result or result.get("status") != "ok":
            expect(False, "%s (%s): the worker failed: %s %s" % (name, mode, proc.returncode, (result, proc.stderr[-300:])))
            return None, result
        return pkg.read_package(open(out, "rb").read()), result
    expect(proc.returncode == 3 and result and result.get("status") == "refused" and not os.path.exists(out),
           "%s (%s): expected a refusal and no package, got %s %s" % (name, mode, proc.returncode, result))
    return None, result


def georef(m):
    return m["report"]["georeference"]


def codes(g):
    return [r["code"] for r in g.get("reasons", [])]


def notices(m):
    return {n["code"]: n["count"] for n in m["report"]["notices"]}


def provenance(n):
    return {k: v for k, t, v in n["ifc"]}


def root_is_identity(m):
    flat = m["nodes"][0]["transform"]
    return all(near(flat[i], 1.0 if i in (0, 5, 10, 15) else 0.0, 1e-12) for i in range(16))


def vertices_world(m):
    """Every vertex of every node with a mesh, in world metres, keyed by (node guid, vertex index)."""
    out = {}
    for i, n in enumerate(m["nodes"]):
        if n["mesh"] < 0:
            continue
        world = pkg.node_world(m, i)
        for k, v in enumerate(m["meshes"][n["mesh"]]["vertices"]):
            out[(n["guid"], k)] = pkg.world_point(world, v)
    return out


def check_identity(name, m):
    """D1 on the new files: converted + supporting + omitted is the scan's count, and the conversion and CRS are not omitted
    when they were read."""
    facts = scan.Scan(fixture(name)).facts()
    acc = m["report"]["accounting"]
    expect(acc["entities"] == facts["entities"] and acc["converted"] + acc["supporting"] + acc["omitted"] == acc["entities"],
           "%s: the accounting identity" % name)
    expect(sum(m["report"]["omittedEntities"].values()) == acc["omitted"], "%s: omittedEntities sums to omitted" % name)


# ---- the generator reproduces the checked-in fixtures -------------------------------------------------------------------
generated = os.path.join(TMP, "generated")
os.makedirs(generated)
subprocess.run([sys.executable, "-I", fixture("make_georef_fixtures.py"), generated], check=True, env={"PYTHONDONTWRITEBYTECODE": "1"})
names = sorted(os.listdir(generated))
expect(len(names) == 24, "the generator writes 24 files: %d" % len(names))
for name in names:
    expect(os.path.exists(fixture(name)) and open(fixture(name), "rb").read() == open(os.path.join(generated, name), "rb").read(),
           "make_georef_fixtures.py reproduces " + name)

# ---- F1: no rotation, the site origin ------------------------------------------------------------------------------------
facts = scan.Scan(fixture("bsi-ifc4-road.ifc")).facts()
want = facts["mapConversions"][0]
expect(want["eastings"] == 729011.2258823584 and want["northings"] == 9063960.607644705 and want["orthogonalHeight"] == 0 and want["scale"] == 0.001,
       "F1: the scan reads the road's conversion as the ADR quotes it")
for mode in ("keep", "grid"):
    m, _ = run("bsi-ifc4-road.ifc", mode)
    if not m:
        continue
    g = georef(m)
    expect(g["mode"] == ("keepAxes" if mode == "keep" else "grid") and g["result"] == "siteOrigin" and "reasons" not in g, "F1 (%s): the site origin is set: %s" % (mode, g["result"]))
    so = g["siteOrigin"]
    expect(near(so["easting"], want["eastings"]) and near(so["northing"], want["northings"]) and near(so["elevation"], want["orthogonalHeight"]),
           "F1 (%s): easting, northing and elevation are the conversion's within 1e-9 m: %r" % (mode, so))
    expect(so["crs"] == "EPSG:32760", "F1 (%s): the label is the CRS name: %r" % (mode, so["crs"]))
    expect(g["crs"]["mapUnit"] == "METRE" and g["crs"]["mapUnitGiven"] is True, "F1 (%s): the map unit is the metre, given" % mode)
    expect(root_is_identity(m) and g["rotationAppliedDegrees"] == 0, "F1 (%s): nothing is rotated" % mode)
    expect(not any(k.startswith("georeference") for k in notices(m)), "F1 (%s): no georeference notice: %r" % (mode, notices(m)))
    project = provenance(m["nodes"][0])
    expect(project["ifc.mapConversion.eastings"] == want["eastings"] and project["ifc.mapConversion.northings"] == want["northings"]
           and project["ifc.mapConversion.orthogonalHeight"] == 0 and project["ifc.mapConversion.scale"] == 0.001
           and project["ifc.mapConversion.rotationDegrees"] == 0 and project["ifc.mapConversion.crs"] == "EPSG:32760",
           "F1 (%s): the project keeps the conversion as ifc.mapConversion.*" % mode)
    expect("IfcMapConversion" not in m["report"]["omittedEntities"] and "IfcProjectedCRS" not in m["report"]["omittedEntities"],
           "F1 (%s): the conversion and the CRS were read, not omitted" % mode)
    check_identity("bsi-ifc4-road.ifc", m)
road_keep, _ = run("bsi-ifc4-road.ifc", "keep")
road_grid, _ = run("bsi-ifc4-road.ifc", "grid")
if road_keep and road_grid:
    expect(vertices_world(road_keep) == vertices_world(road_grid), "F1: the road's vertices are the same in both modes")

# Metres, scale 1, no map unit: the metre is assumed, and said. Wall at (1, 2), 4 x 0.2 x 2.5: x in {-1, 3}, y in {1.9, 2.1}.
m, _ = run("georef-m-theta0.ifc", "keep")
if m:
    g = georef(m)
    expect(g["result"] == "siteOrigin" and g["siteOrigin"] == {"easting": 500000.5, "northing": 4000000.25, "elevation": 12.5, "crs": "EPSG:32633"},
           "F1: georef-m-theta0 sets the site origin: %r" % g.get("siteOrigin"))
    expect(g["crs"]["mapUnitGiven"] is False and notices(m).get("georeferenceMapUnitAssumed") == 1, "F1: the missing map unit is assumed to be the metre and noticed")
    xs = sorted({round(v[0], 9) for v in vertices_world(m).values()})
    ys = sorted({round(v[1], 9) for v in vertices_world(m).values()})
    zs = sorted({round(v[2], 9) for v in vertices_world(m).values()})
    expect(xs == [-1.0, 3.0] and ys == [1.9, 2.1] and zs == [0.0, 2.5], "F1: the wall's corners are the hand-computed ones: %r %r %r" % (xs, ys, zs))
    check_identity("georef-m-theta0.ifc", m)

# IFC4.3 IfcMapConversionScaled with factors of one is a plain conversion; with a factor of 0.5 on Z it is not representable.
for name, result, reasons in (("georef-ifc43-scaled.ifc", "siteOrigin", []), ("georef-ifc43-factors.ifc", "notRepresentable", ["perAxisFactors"])):
    m, _ = run(name, "keep")
    if m:
        g = georef(m)
        expect(g["result"] == result and codes(g) == reasons, "F1/F4: %s is %s %r: got %s %r" % (name, result, reasons, g["result"], codes(g)))
        if result == "siteOrigin":
            expect(g["siteOrigin"]["easting"] == 1000.5 and g["siteOrigin"]["elevation"] == 3.5, "F1: the scaled conversion's numbers")
        else:
            expect(g["mapConversion"]["factors"] == [1.0, 1.0, 0.5] and "siteOrigin" not in g, "F4: the factors are reported and no origin is set")
        check_identity(name, m)

# ---- F2: the certified samples with a 60 degree rotation ----------------------------------------------------------------
for name in ("bsi-ifc4-arch.ifc", "bsi-ifc43-arch.ifc", "bsi-ifc4-landscaping.ifc"):
    facts = scan.Scan(fixture(name)).facts()
    c = facts["mapConversions"][0]
    expect(near(math.degrees(math.atan2(c["xAxisOrdinate"], c["xAxisAbscissa"])), 60.0, 1e-9), "F2: %s rotates by 60 degrees" % name)
    keep, _ = run(name, "keep")
    grid, _ = run(name, "grid")
    if not (keep and grid):
        continue
    g = georef(keep)
    expect(g["mode"] == "keepAxes" and g["result"] == "notRepresentable" and codes(g) == ["rotation"] and "siteOrigin" not in g,
           "F2 (%s): keepAxes reports the rotation and sets no origin: %s %r" % (name, g["result"], codes(g)))
    mc = g["mapConversion"]
    expect(mc["eastings"] == c["eastings"] and mc["northings"] == c["northings"] and mc["orthogonalHeight"] == c["orthogonalHeight"]
           and mc["xAxisAbscissa"] == c["xAxisAbscissa"] and mc["xAxisOrdinate"] == c["xAxisOrdinate"] and mc["scale"] == c["scale"] == 0.001
           and near(mc["rotationDegrees"], 60.0, 1e-9), "F2 (%s): the whole conversion is in the report, exactly the file's" % name)
    expect(g["crs"]["name"] == "EPSG:32760" and g["crs"]["geodeticDatum"] == "WGS 84", "F2 (%s): the CRS is in the report" % name)
    expect(notices(keep).get("georeferenceRotationKept") == 1 and root_is_identity(keep), "F2 (%s): noticed, and the axes are kept" % name)
    project = provenance(keep["nodes"][0])
    expect(project["ifc.mapConversion.eastings"] == c["eastings"] and near(project["ifc.mapConversion.rotationDegrees"], 60.0, 1e-9)
           and project["ifc.mapConversion.crs"] == "EPSG:32760", "F2 (%s): ifc.mapConversion.* on the project" % name)
    check_identity(name, keep)
    # grid: the site origin is the conversion's translation, the root carries the rotation, and every vertex is the
    # hand-computed mapped coordinate (E + c x' - s y', N + s x' + c y', H + z') minus the translation.
    g2 = georef(grid)
    expect(g2["mode"] == "grid" and g2["result"] == "gridAligned" and codes(g2) == [], "F2 (%s): grid is aligned: %s" % (name, g2["result"]))
    so = g2["siteOrigin"]
    expect(near(so["easting"], c["eastings"]) and near(so["northing"], c["northings"]) and near(so["elevation"], c["orthogonalHeight"]),
           "F2 (%s): grid sets the site origin to the conversion's translation" % name)
    expect(near(g2["rotationAppliedDegrees"], 60.0, 1e-9) and notices(grid).get("georeferenceGridAligned") == 1, "F2 (%s): the rotation applied is 60 degrees" % name)
    length = math.hypot(c["xAxisAbscissa"], c["xAxisOrdinate"])
    cs, sn = c["xAxisAbscissa"] / length, c["xAxisOrdinate"] / length
    t = grid["nodes"][0]["transform"]
    expect(near(t[0], cs) and near(t[1], -sn) and near(t[4], sn) and near(t[5], cs) and near(t[10], 1) and t[3] == t[7] == t[11] == 0,
           "F2 (%s): the project node carries the rotation, no translation" % name)
    vk, vg = vertices_world(keep), vertices_world(grid)
    expect(len(vk) == len(vg) and len(vk) > 100, "F2 (%s): the same vertices in both modes: %d" % (name, len(vk)))
    worst = 0.0
    for key, (x, y, z) in vk.items():
        mapped = (cs * x - sn * y, sn * x + cs * y, z)
        worst = max(worst, max(abs(a - b) for a, b in zip(mapped, vg[key])))
    expect(worst <= 1e-9, "F2 (%s): every vertex is the hand-computed mapped coordinate minus the translation, worst %.3g m" % (name, worst))
    check_identity(name, grid)
    # H1 on the grid mode.
    grid2, _ = run(name, "grid")
    expect(grid2 is not None and grid2["nodes"] == grid["nodes"] and grid2["meshes"] == grid["meshes"], "F2/H1 (%s): two grid imports are identical" % name)

# The 3-4-5 fixture: corners computed by hand. x in {-1, 3}, y in {1.9, 2.1}; (c, s) = (0.6, 0.8).
keep, _ = run("georef-mm-rotated.ifc", "keep")
grid, _ = run("georef-mm-rotated.ifc", "grid")
if keep and grid:
    expect(georef(keep)["result"] == "notRepresentable" and codes(georef(keep)) == ["rotation"], "F2: the 3-4-5 file keeps its axes")
    g = georef(grid)
    expect(g["result"] == "gridAligned" and g["siteOrigin"] == {"easting": 1000.5, "northing": 2000.25, "elevation": 3.5, "crs": "EPSG:32633"}, "F2: the 3-4-5 file's origin")
    expect(near(g["mapConversion"]["rotationDegrees"], 53.13010235415598, 1e-12), "F2: atan2(0.8, 0.6) is 53.1301 degrees")
    got = sorted((round(v[0], 9), round(v[1], 9)) for v in vertices_world(grid).values() if abs(v[2]) < 1e-12)
    expect(got == sorted([(-2.12, 0.34), (-2.28, 0.46), (0.28, 3.54), (0.12, 3.66)]), "F2: the base corners are the hand-computed ones: %r" % got)
    expect(sorted({round(v[2], 9) for v in vertices_world(grid).values()}) == [0.0, 2.5], "F2: heights are not changed by the rotation")
    kept = sorted((round(v[0], 9), round(v[1], 9)) for v in vertices_world(keep).values() if abs(v[2]) < 1e-12)
    expect(kept == sorted([(-1.0, 1.9), (-1.0, 2.1), (3.0, 1.9), (3.0, 2.1)]), "F2: keepAxes leaves the corners where the file has them: %r" % kept)
    check_identity("georef-mm-rotated.ifc", grid)

# ---- F3: beyond 1e6 m ------------------------------------------------------------------------------------------------------
for name in ("georef-absolute-far.ifc", "far-from-origin.ifc"):
    for mode in ("keep", "grid"):
        _, result = run(name, mode, expect_status="refused")
        expect(result and result["code"] == "farFromOrigin" and ("2500000" if "absolute" in name else "2000000") in result["message"],
               "F3 (%s, %s): refused, naming the extent: %r" % (name, mode, result))

# ---- F4 ---------------------------------------------------------------------------------------------------------------------
# A map unit of US survey feet (and a scale that is not the project unit): reported, never converted by guess.
for mode, reasons in (("keep", ["mapUnit", "scale", "rotation"]), ("grid", ["mapUnit", "scale"])):
    m, _ = run("georef-ussft-rotated.ifc", mode)
    if m:
        g = georef(m)
        expect(g["result"] == "notRepresentable" and codes(g) == reasons and "siteOrigin" not in g, "F4 (%s): US survey foot: %r" % (mode, codes(g)))
        expect(g["crs"]["mapUnit"].lower().startswith("us survey foot") and near(g["crs"]["mapUnitMetres"], 0.3048006096012192, 1e-12),
               "F4 (%s): the map unit is named: %r" % (mode, g["crs"]))
        expect(root_is_identity(m) and notices(m).get("georeferenceNotRepresentable") == 1 and "georeferenceGridAligned" not in notices(m),
               "F4 (%s): nothing is rotated when the origin cannot be set" % mode)
        expect(g["mapConversion"]["eastings"] == 6000000.0, "F4 (%s): the conversion is reported as written" % mode)
        check_identity("georef-ussft-rotated.ifc", m)
# A non-identity world coordinate system on the Model context: the file is refused, naming the context.
for mode in ("keep", "grid"):
    _, result = run("georef-wcs-offset.ifc", mode, expect_status="refused")
    expect(result and result["code"] == "worldCoordinateSystem" and "#7" in result["message"], "F4 (%s): a world coordinate system at (500, 0, 0) is refused, naming #7: %r" % (mode, result))
# The identity, written out, is accepted (the certified files have one).
m, _ = run("bsi-ifc4-arch.ifc", "keep")
expect(m is not None, "F4: an identity world coordinate system is accepted")
# Two conversions, two Model contexts, a conversion of a Plan context, a zero axis, half an axis, a scale in millimetres, a range.
cases = (("georef-two-conversions.ifc", ["severalConversions"], 2), ("georef-two-contexts.ifc", ["severalModelContexts"], 1),
         ("georef-conversion-on-plan.ifc", ["notOnModelContext"], 1), ("georef-axis-zero.ifc", ["badAxis"], 1),
         ("georef-axis-partial.ifc", ["incompleteAxis"], 1), ("georef-scale-mm.ifc", ["scale"], 1), ("georef-range.ifc", ["siteCoordinateRange"], 1),
         # Damaged conversions: a reason each, never a default. A wrong-typed value is not an absent one.
         ("georef-incomplete.ifc", ["incompleteConversion"], 1), ("georef-scale-negative.ifc", ["badScale"], 1), ("georef-scale-text.ifc", ["badScale"], 1),
         ("georef-axis-text.ifc", ["badAxis"], 1), ("georef-mapunit-mm.ifc", ["mapUnit"], 1), ("georef-mapunit-text.ifc", ["mapUnit"], 1),
         ("georef-ifc43-geographic.ifc", ["crsNotProjected"], 1))
for name, reasons, count in cases:
    for mode in ("keep", "grid"):
        m, _ = run(name, mode)
        if not m:
            continue
        g = georef(m)
        expect(g["result"] == "notRepresentable" and codes(g) == reasons and "siteOrigin" not in g, "F4 (%s, %s): %r, got %s %r" % (name, mode, reasons, g["result"], codes(g)))
        expect(g["mapConversionsInFile"] == count and root_is_identity(m) and notices(m).get("georeferenceNotRepresentable") == 1,
               "F4 (%s, %s): %d conversion(s) counted, axes kept, noticed" % (name, mode, count))
        check_identity(name, m)
# A conversion whose source is the Body sub-context converts the Model context it belongs to.
m, _ = run("georef-source-body.ifc", "keep")
if m:
    g = georef(m)
    expect(g["result"] == "siteOrigin" and g["siteOrigin"]["easting"] == 1000.5 and g["mapConversionsOnModelContext"] == 1, "F1: a conversion of the Body sub-context is the Model context's: %r" % g["result"])
    check_identity("georef-source-body.ifc", m)
# A conversion whose scale cannot be read has no scale in the report (never a default), and none on the project.
m, _ = run("georef-scale-text.ifc", "keep")
if m:
    expect(georef(m)["mapConversion"]["scale"] is None and "ifc.mapConversion.scale" not in provenance(m["nodes"][0])
           and provenance(m["nodes"][0])["ifc.mapConversion.eastings"] == 1000.5, "F4: an unreadable scale is not reported as a number")
# A world coordinate system that cannot be read is refused like one that is not the identity.
for mode in ("keep", "grid"):
    _, result = run("georef-wcs-text.ifc", mode, expect_status="refused")
    expect(result and result["code"] == "worldCoordinateSystem" and "unreadable" in result["message"], "F4 (%s): an unreadable world coordinate system is refused: %r" % (mode, result))
# IFC2X3 has no IfcMapConversion: nothing is georeferenced, and the community ePSets are only properties.
m, _ = run("bsi-ifc2x3-arch.ifc", "grid")
if m:
    g = georef(m)
    expect(g["result"] == "none" and "IFC2X3" in g["reason"] and "siteOrigin" not in g and root_is_identity(m), "F4: IFC2X3 is not georeferenced: %r" % g)
m, _ = run("georef-ifc2x3-epset.ifc", "grid")
if m:
    g = georef(m)
    expect(g["result"] == "none" and g["epsets"] == 2 and notices(m).get("georeferenceEpsets") == 2, "F4: the ePSets are noticed, not used: %r" % g)
    site = [n for n in m["nodes"] if n["role"] == "site"][0]
    props = {"%s.%s" % (p[0], p[1]): p[4] for p in site["props"]}
    # Ordinary properties: lengths are converted to metres like any other (1000.5 mm), a real is stored as written.
    expect(near(props.get("ePSet_MapConversion.Eastings", 0), 1.0005) and props.get("ePSet_MapConversion.Scale") == 0.001
           and props.get("ePSet_ProjectedCRS.Name") == "EPSG:32633", "F4: they are ordinary properties of the site: %r" % props)
    check_identity("georef-ifc2x3-epset.ifc", m)
# A file without any conversion says so.
m, _ = run("units-mm.ifc", "keep")
if m:
    expect(georef(m)["result"] == "none" and georef(m)["mapConversionsInFile"] == 0, "no conversion: result none")
# The option is checked.
proc = subprocess.run([WORKER, "--import", fixture("units-mm.ifc"), "--output", os.path.join(TMP, "bad.pkg"), "--georef", "sideways"], capture_output=True, text=True)
expect(proc.returncode == 2 and "keep or grid" in proc.stderr, "--georef takes keep or grid: %d %r" % (proc.returncode, proc.stderr[:100]))

if failures:
    print("%d check(s) failed" % len(failures))
    sys.exit(1)
print("ifc_georeference ok")
