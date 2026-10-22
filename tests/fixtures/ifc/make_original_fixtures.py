#!/usr/bin/env python3
"""Writes the original (MIT) IFC4 fixtures of the API probe (ADR 0171, R097.a).

    python3 -I tests/fixtures/ifc/make_original_fixtures.py OUTPUT_DIR

The files are hand-checkable P21 text with the expected volumes in comments below; the probe test
regenerates them and fails if a checked-in file differs, so the fixtures are what this script says.
Every file is one IfcProject with a millimetre, metre or foot length unit, a Model/Body context and
a few IfcBuildingElementProxy products. Entity numbers are by product: product n has its entities at
100 + n * stride (stride 20, or 40 in openings-hard.ifc), its IfcBuildingElementProxy at +7."""
import sys
from pathlib import Path

out = Path(sys.argv[1])

def r(v):
    return repr(float(v))

HEADER = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('SketchyUp probe fixture: {purpose}'),'2;1');
FILE_NAME('{name}','2026-10-22T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R097.a','hand-written','');
FILE_SCHEMA(('IFC4'));
ENDSEC;
DATA;
"""

def prelude(units):
    """Entities 1-19: project, units, context, placement basics. units: list of unit lines."""
    lines = [
        "#1=IFCPROJECT('0000000000000000000001',$,'Probe',$,$,$,$,(#10),#2);",
        "#2=IFCUNITASSIGNMENT((" + ",".join(units["refs"]) + "));",
    ] + units["defs"] + [
        "#10=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#11,$);",
        "#11=IFCAXIS2PLACEMENT3D(#12,$,$);",
        "#12=IFCCARTESIANPOINT((0.,0.,0.));",
        "#13=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#10,$,.MODEL_VIEW.,$);",
        "#14=IFCDIRECTION((0.,0.,1.));",
        "#15=IFCDIRECTION((1.,0.,0.));",
        "#16=IFCCARTESIANPOINT((0.,0.));",
        "#17=IFCAXIS2PLACEMENT2D(#16,$);",
    ]
    return lines

MM = {"refs": ["#3", "#4"], "defs": [
    "#3=IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.);",
    "#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);"]}
M = {"refs": ["#3", "#4"], "defs": [
    "#3=IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.);",
    "#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);"]}
FT = {"refs": ["#3", "#4"], "defs": [
    "#3=IFCCONVERSIONBASEDUNIT(#5,.LENGTHUNIT.,'FOOT',#6);",
    "#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);",
    "#5=IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0);",
    "#6=IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.3048),#7);",
    "#7=IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.);"]}
NOLEN = {"refs": ["#4"], "defs": ["#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);"]}
# Same assignment as MM plus a stray second length unit outside it, as the buildingSMART
# Architecture files carry (their entity #19).
MM_STRAY = {"refs": MM["refs"], "defs": MM["defs"] + ["#7=IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.);"]}

def box_product(n, name, x, y, z, w, d, h, color=None, base=100, stride=20):
    """An IfcBuildingElementProxy with one extruded rectangle: w x d footprint (centred on its
    local origin), extruded h along Z, placed at (x, y, z). Returns the lines."""
    b = base + n * stride
    return [
        f"#{b}=IFCCARTESIANPOINT(({r(x)},{r(y)},{r(z)}));",
        f"#{b+1}=IFCAXIS2PLACEMENT3D(#{b},$,$);",
        f"#{b+2}=IFCLOCALPLACEMENT($,#{b+1});",
        f"#{b+3}=IFCRECTANGLEPROFILEDEF(.AREA.,$,#17,{r(w)},{r(d)});",
        f"#{b+4}=IFCEXTRUDEDAREASOLID(#{b+3},#11,#14,{r(h)});",
        f"#{b+5}=IFCSHAPEREPRESENTATION(#13,'Body','SweptSolid',(#{b+4}));",
        f"#{b+6}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{b+5}));",
        f"#{b+7}=IFCBUILDINGELEMENTPROXY('P{n:021d}',$,'{name}',$,$,#{b+2},#{b+6},$,$);",
    ]

def write(name, purpose, body_lines):
    text = HEADER.format(purpose=purpose, name=name) + "\n".join(body_lines) + "\nENDSEC;\nEND-ISO-10303-21;\n"
    (out / name).write_text(text)

# units: 1 x 2 x 3 (in the file's length unit), centred footprint, at the origin. Volumes:
# mm file 1000x2000x3000 mm = 6 m3; m file 1x2x3 m = 6 m3; ft file 10x20x30 ft = 6000 ft3
# = 6000 * 0.3048^3 = 169.901079552 m3 (0.3048^3 = 0.028316846592).
write("units-mm.ifc", "length unit millimetre, one 1000 x 2000 x 3000 box = 6 m3",
      prelude(MM) + box_product(1, "box", 0, 0, 0, 1000, 2000, 3000))
write("units-m.ifc", "length unit metre, one 1 x 2 x 3 box = 6 m3",
      prelude(M) + box_product(1, "box", 0, 0, 0, 1, 2, 3))
write("units-ft.ifc", "length unit foot (conversion based, 0.3048), one 10 x 20 x 30 ft box = 169.901079552 m3",
      prelude(FT) + box_product(1, "box", 0, 0, 0, 10, 20, 30))
write("units-missing.ifc", "no length unit in the unit assignment: one 1 x 2 x 3 box, the library assumes metres = 6 m3",
      prelude(NOLEN) + box_product(1, "box", 0, 0, 0, 1, 2, 3))
write("units-mm-stray.ifc", "as units-mm.ifc plus a second, unassigned metre length unit (as the buildingSMART Architecture files)",
      prelude(MM_STRAY) + box_product(1, "box", 0, 0, 0, 1000, 2000, 3000))

# Placement away from the origin, and a second product, to check which frame the mesh is in:
# a 1000 x 1000 x 1000 mm box placed at (5000, 6000, 7000) mm.
write("placement-mm.ifc", "mm; a 1000 mm cube placed at (5000,6000,7000) mm, local mesh frame check",
      prelude(MM) + box_product(1, "cube", 5000, 6000, 7000, 1000, 1000, 1000))

# Openings (IfcRelVoidsElement): wall A has one opening, wall B two, wall C (an open shell: a faceted
# brep with its top face missing) has one. All in mm. A host is a box 4000 x 300 x 2500 mm, its
# footprint centred on its origin: 3.0 m3; each opening is a box 1000 x 300 x 2000 mm through the
# host's thickness, 0.6 m3, fully inside the host, so
#   wall A: 3.0 - 0.6 = 2.4 m3,  wall B: 3.0 - 0.6 - 0.6 = 1.8 m3 (second opening at x = +1200).
def opening(n, name, host_n, x, base=300):
    b = base + n * 20
    host_place = 100 + host_n * 20 + 2
    return [
        f"#{b}=IFCCARTESIANPOINT(({r(x)},0.,0.));",
        f"#{b+1}=IFCAXIS2PLACEMENT3D(#{b},$,$);",
        f"#{b+2}=IFCLOCALPLACEMENT(#{host_place},#{b+1});",
        f"#{b+3}=IFCRECTANGLEPROFILEDEF(.AREA.,$,#17,1000.,300.);",
        f"#{b+4}=IFCEXTRUDEDAREASOLID(#{b+3},#11,#14,2000.);",
        f"#{b+5}=IFCSHAPEREPRESENTATION(#13,'Body','SweptSolid',(#{b+4}));",
        f"#{b+6}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{b+5}));",
        f"#{b+7}=IFCOPENINGELEMENT('O{n:021d}',$,'{name}',$,$,#{b+2},#{b+6},$,.OPENING.);",
        f"#{b+8}=IFCRELVOIDSELEMENT('R{n:021d}',$,$,$,#{100 + host_n*20 + 7},#{b+7});",
    ]

def wall(n, name, x):
    lines = box_product(n, name, x, 0, 0, 4000, 300, 2500)
    return lines

def wall_open_shell(n, name, x):
    """A faceted brep box 4000 x 300 x 2500 mm with the top face missing (not closed)."""
    b = 100 + n * 20
    pts = [(-2000, -150, 0), (2000, -150, 0), (2000, 150, 0), (-2000, 150, 0),
           (-2000, -150, 2500), (2000, -150, 2500), (2000, 150, 2500), (-2000, 150, 2500)]
    lines = [f"#{b}=IFCCARTESIANPOINT(({r(x)},0.,0.));",
             f"#{b+1}=IFCAXIS2PLACEMENT3D(#{b},$,$);",
             f"#{b+2}=IFCLOCALPLACEMENT($,#{b+1});"]
    pid = b + 100  # points #{b+100}..
    for i, (px, py, pz) in enumerate(pts):
        lines.append(f"#{pid+i}=IFCCARTESIANPOINT(({r(px)},{r(py)},{r(pz)}));")
    # faces of the box (bottom, four sides) - the top face (4,5,6,7) is omitted
    faces = [(0, 3, 2, 1), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    fid = pid + 20
    face_refs = []
    for i, f in enumerate(faces):
        lines.append(f"#{fid+i*3}=IFCPOLYLOOP((" + ",".join(f"#{pid+k}" for k in f) + "));")
        lines.append(f"#{fid+i*3+1}=IFCFACEOUTERBOUND(#{fid+i*3},.T.);")
        lines.append(f"#{fid+i*3+2}=IFCFACE((#{fid+i*3+1}));")
        face_refs.append(f"#{fid+i*3+2}")
    sid = fid + 20
    lines += [f"#{sid}=IFCCLOSEDSHELL((" + ",".join(face_refs) + "));",
              f"#{sid+1}=IFCFACETEDBREP(#{sid});",
              f"#{sid+2}=IFCSHAPEREPRESENTATION(#13,'Body','Brep',(#{sid+1}));",
              f"#{sid+3}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{sid+2}));",
              f"#{b+7}=IFCBUILDINGELEMENTPROXY('P{n:021d}',$,'{name}',$,$,#{b+2},#{sid+3},$,$);"]
    return lines

# Entities are numbered by product index n: product base = 100 + 20n, so n=1 -> 120.., n=2 -> 140.., n=3 -> 160..
open_lines = (prelude(MM)
              + wall(1, "wall-A-one-opening", 0) + opening(1, "opening-A1", 1, 0)
              + wall(2, "wall-B-two-openings", 10000) + opening(2, "opening-B1", 2, -1200) + opening(3, "opening-B2", 2, 1200)
              + wall_open_shell(3, "wall-C-open-shell", 20000) + opening(4, "opening-C1", 3, 0))
write("openings.ifc", "mm; walls with one, two and (open shell) one opening; volumes 2.4, 1.8 and an uncuttable host",
      open_lines)

# Failure reporting: a good box, a box with zero extrusion depth, a box with a zero-width profile,
# and a product with an empty body representation, in one file (mm).
fail = prelude(MM)
fail += box_product(1, "good-box", 0, 0, 0, 1000, 1000, 1000)
fail += box_product(2, "zero-depth", 3000, 0, 0, 1000, 1000, 0)
fail += box_product(3, "zero-width", 6000, 0, 0, 0, 1000, 1000)
b = 100 + 4 * 20
fail += [f"#{b}=IFCSHAPEREPRESENTATION(#13,'Body','SweptSolid',());",
         f"#{b+1}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{b}));",
         f"#{b+2}=IFCBUILDINGELEMENTPROXY('P{4:021d}',$,'empty-body',$,$,$,#{b+1},$,$);"]
write("failures.ifc", "mm; one good box, one zero-depth extrusion, one zero-width profile, one empty representation",
      fail)

# --- openings-hard.ifc: hosts and openings the Boolean may fail on (all mm, 1000 mm cubes) ---
# D: closed tessellated cube with a through opening; E: the same cube with its top missing;
# F: an extruded cube with an open-shell opening; G: an extruded cube with an opening that does
# not touch it. Cubes are centred on x = 0, y = 0 with z in [0, 1000], placed at x = 0, 5000, ...
CUBE_PTS = [(-500., -500., 0.), (500., -500., 0.), (500., 500., 0.), (-500., 500., 0.),
            (-500., -500., 1000.), (500., -500., 1000.), (500., 500., 1000.), (-500., 500., 1000.)]
# triangles (1-based) of the six faces, outward normals counter-clockwise
CUBE_TRIS = [(1, 3, 2), (1, 4, 3), (5, 6, 7), (5, 7, 8), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 4, 8), (3, 8, 7), (4, 1, 5), (4, 5, 8)]

def tess_host(n, name, x, tris):
    b = 100 + n * 40
    pts = ",".join("(" + ",".join(r(c) for c in p) + ")" for p in CUBE_PTS)
    idx = ",".join("(" + ",".join(str(i) for i in t) + ")" for t in tris)
    return [f"#{b}=IFCCARTESIANPOINT(({r(x)},0.,0.));",
            f"#{b+1}=IFCAXIS2PLACEMENT3D(#{b},$,$);",
            f"#{b+2}=IFCLOCALPLACEMENT($,#{b+1});",
            f"#{b+3}=IFCCARTESIANPOINTLIST3D(({pts}));",
            f"#{b+4}=IFCTRIANGULATEDFACESET(#{b+3},$,.T.,({idx}),$);",
            f"#{b+5}=IFCSHAPEREPRESENTATION(#13,'Body','Tessellation',(#{b+4}));",
            f"#{b+6}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{b+5}));",
            f"#{b+7}=IFCBUILDINGELEMENTPROXY('P{n:021d}',$,'{name}',$,$,#{b+2},#{b+6},$,$);"]

def hard_opening(n, name, host_n, ox, rep_lines=None):
    """An opening 400 x 400 x 1200 mm centred at ox on its host (placement relative to the host)."""
    b = 500 + n * 40
    host_place = 100 + host_n * 40 + 2
    host_id = 100 + host_n * 40 + 7
    lines = [f"#{b}=IFCCARTESIANPOINT(({r(ox)},0.,-100.));",
             f"#{b+1}=IFCAXIS2PLACEMENT3D(#{b},$,$);",
             f"#{b+2}=IFCLOCALPLACEMENT(#{host_place},#{b+1});"]
    if rep_lines is None:
        lines += [f"#{b+3}=IFCRECTANGLEPROFILEDEF(.AREA.,$,#17,400.,400.);",
                  f"#{b+4}=IFCEXTRUDEDAREASOLID(#{b+3},#11,#14,1200.);",
                  f"#{b+5}=IFCSHAPEREPRESENTATION(#13,'Body','SweptSolid',(#{b+4}));"]
    else:
        lines += rep_lines(b)
    lines += [f"#{b+6}=IFCPRODUCTDEFINITIONSHAPE($,$,(#{b+5}));",
              f"#{b+7}=IFCOPENINGELEMENT('O{n:021d}',$,'{name}',$,$,#{b+2},#{b+6},$,.OPENING.);",
              f"#{b+8}=IFCRELVOIDSELEMENT('R{n:021d}',$,$,$,#{host_id},#{b+7});"]
    return lines

def open_shell_opening(b):
    # a 400 x 400 x 1200 box missing its top face, as a faceted brep (ids b+10..)
    pts = [(-200., -200., 0.), (200., -200., 0.), (200., 200., 0.), (-200., 200., 0.),
           (-200., -200., 1200.), (200., -200., 1200.), (200., 200., 1200.), (-200., 200., 1200.)]
    lines, ids = [], []
    for i, (px, py, pz) in enumerate(pts):
        lines.append(f"#{b+10+i}=IFCCARTESIANPOINT(({r(px)},{r(py)},{r(pz)}));")
    faces = [(0, 3, 2, 1), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    for i, f in enumerate(faces):
        lines.append(f"#{b+20+i*3}=IFCPOLYLOOP((" + ",".join(f"#{b+10+k}" for k in f) + "));")
        lines.append(f"#{b+21+i*3}=IFCFACEOUTERBOUND(#{b+20+i*3},.T.);")
        lines.append(f"#{b+22+i*3}=IFCFACE((#{b+21+i*3}));")
        ids.append(f"#{b+22+i*3}")
    lines += [f"#{b+40}=IFCCLOSEDSHELL((" + ",".join(ids) + "));",
              f"#{b+41}=IFCFACETEDBREP(#{b+40});",
              f"#{b+5}=IFCSHAPEREPRESENTATION(#13,'Body','Brep',(#{b+41}));"]
    return lines

hard = prelude(MM)
hard += tess_host(1, "D-closed-tessellated", 0, CUBE_TRIS) + hard_opening(1, "opening-D", 1, 0)
hard += tess_host(2, "E-open-tessellated", 5000, CUBE_TRIS[:2] + CUBE_TRIS[4:]) + hard_opening(2, "opening-E", 2, 0)
hard += box_product(3, "F-extruded", 10000, 0, 0, 1000, 1000, 1000, stride=40) + hard_opening(3, "opening-F-open-shell", 3, 0, open_shell_opening)
hard += box_product(4, "G-extruded", 15000, 0, 0, 1000, 1000, 1000, stride=40) + hard_opening(4, "opening-G-far-away", 4, 5000)
write("openings-hard.ifc", "mm; D closed tessellated host, E open tessellated host, F open-shell opening, G opening that misses its host", hard)

# --- units-mm-projectlibrary.ifc: units-mm plus an IfcProjectLibrary. In IFC4 both are IfcContext,
# so the library's "single project or context" test fails and it assumes metres (the
# buildingSMART Reference View wall has this shape). ---
lib = prelude(MM)
lib.append("#9=IFCPROJECTLIBRARY('L000000000000000000001',$,$,$,$,$,$,$,$);")
lib += box_product(1, "box", 0, 0, 0, 1000, 2000, 3000)
write("units-mm-projectlibrary.ifc", "mm plus an IfcProjectLibrary next to the IfcProject: 1000 x 2000 x 3000 mm box = 6 m3", lib)

# --- contexts.ifc: one product with Body, Axis, Box and FootPrint representations in the usual
# contexts (Model: Body, Axis, Box; Plan: FootPrint). All mm. Body is the 1000 x 2000 x 3000 mm box. ---
ctx = prelude(MM)
ctx += [
    "#30=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Axis','Model',*,*,*,*,#10,$,.GRAPH_VIEW.,$);",
    "#31=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Box','Model',*,*,*,*,#10,$,.MODEL_VIEW.,$);",
    "#32=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Plan',2,1.E-05,#11,$);",
    "#33=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('FootPrint','Plan',*,*,*,*,#32,$,.PLAN_VIEW.,$);",
    "#34=IFCCARTESIANPOINT((0.,0.,0.));",
    "#35=IFCCARTESIANPOINT((0.,0.,3000.));",
    "#36=IFCPOLYLINE((#34,#35));",
    "#37=IFCBOUNDINGBOX(#34,1000.,2000.,3000.);",
    "#38=IFCCARTESIANPOINT((-500.,-1000.));",
    "#39=IFCCARTESIANPOINT((500.,-1000.));",
    "#40=IFCCARTESIANPOINT((500.,1000.));",
    "#41=IFCCARTESIANPOINT((-500.,1000.));",
    "#42=IFCPOLYLINE((#38,#39,#40,#41,#38));",
]
ctx += box_product(1, "multi-representation", 0, 0, 0, 1000, 2000, 3000)
# product #127 has shape definition #126 with only a Body representation (#125); give it more
ctx = [line.replace("#126=IFCPRODUCTDEFINITIONSHAPE($,$,(#125));",
                    "#126=IFCPRODUCTDEFINITIONSHAPE($,$,(#125,#128,#129,#130));") for line in ctx]
ctx += [
    "#128=IFCSHAPEREPRESENTATION(#30,'Axis','Curve3D',(#36));",
    "#129=IFCSHAPEREPRESENTATION(#31,'Box','BoundingBox',(#37));",
    "#130=IFCSHAPEREPRESENTATION(#33,'FootPrint','Curve2D',(#42));",
]
# the Plan context is a second context of the project
ctx = [line.replace("(#10),#2);", "(#10,#32),#2);") for line in ctx]
write("contexts.ifc", "mm; one product with Body, Axis, Box and FootPrint representations", ctx)
