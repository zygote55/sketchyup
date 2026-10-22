#!/usr/bin/env python3
"""Writes the original (MIT) IFC fixtures of the R097.b import checks (ADR 0171 section 6).

    python3 -I tests/fixtures/ifc/make_import_fixtures.py OUTPUT_DIR

Every file is hand-checkable P21 text with a proper spatial structure. The numbers the tests expect are
computed by hand in the comments next to each fixture; tests/ifc_import_worker_tests.py regenerates the
files and fails if a checked-in one differs.
"""
import sys
from pathlib import Path

out = Path(sys.argv[1])


def r(v):
    return repr(float(v))


class File:
    def __init__(self, schema="IFC4", unit="mm", purpose="", extra_units=(), units=None):
        self.schema = schema
        self.purpose = purpose
        self.lines = []
        self.n = 0
        self.unit = unit
        self.guid_counter = 0
        self.extra_units = list(extra_units)
        self.custom_units = units

    def add(self, text):
        self.n += 1
        self.lines.append(f"#{self.n}={text};")
        return self.n

    def guid(self):
        self.guid_counter += 1
        return "%022d" % self.guid_counter

    # ---- geometry helpers ----------------------------------------------------------------------
    def point(self, x, y, z=None):
        coords = f"{r(x)},{r(y)}" + (f",{r(z)}" if z is not None else "")
        return self.add(f"IFCCARTESIANPOINT(({coords}))")

    def direction(self, x, y, z=None):
        coords = f"{r(x)},{r(y)}" + (f",{r(z)}" if z is not None else "")
        return self.add(f"IFCDIRECTION(({coords}))")

    def axis3(self, x=0, y=0, z=0, axis=None, ref=None):
        loc = self.point(x, y, z)
        a = self.direction(*axis) if axis else None
        rd = self.direction(*ref) if ref else None
        return self.add(f"IFCAXIS2PLACEMENT3D(#{loc},{'#%d' % a if a else '$'},{'#%d' % rd if rd else '$'})")

    def placement(self, rel=None, x=0, y=0, z=0, axis=None, ref=None):
        a = self.axis3(x, y, z, axis, ref)
        return self.add(f"IFCLOCALPLACEMENT({'#%d' % rel if rel else '$'},#{a})")

    # ---- the spine -----------------------------------------------------------------------------
    def header(self):
        return (
            "ISO-10303-21;\nHEADER;\n"
            f"FILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]','SketchyUp R097.b fixture: {self.purpose}'),'2;1');\n"
            "FILE_NAME('fixture.ifc','2026-10-22T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R097.b','hand-written','');\n"
            f"FILE_SCHEMA(('{self.schema}'));\nENDSEC;\nDATA;\n"
        )

    def spine(self, storeys=1, site=True, building=True, project_extra="", world_up=True):
        """Project, units, contexts, site, building and storeys. Returns a dict of ids."""
        ids = {}
        if self.custom_units is not None:
            unit_refs = self.custom_units(self)
        else:
            unit_defs = {
                "mm": [f"IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.)"],
                "m": [f"IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)"],
            }[self.unit]
            unit_refs = [self.add(d) for d in unit_defs]
            unit_refs.append(self.add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)"))
            unit_refs.append(self.add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)"))
            unit_refs += self.extra_units
        assignment = self.add("IFCUNITASSIGNMENT((" + ",".join(f"#{u}" for u in unit_refs) + "))")
        origin = self.axis3(0, 0, 0)
        ctx = self.add(f"IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#{origin},$)")
        body = self.add(f"IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#{ctx},$,.MODEL_VIEW.,$)")
        ids["body"] = body
        ids["context"] = ctx
        ids["project"] = self.add(f"IFCPROJECT('{self.guid()}',$,'Project',$,$,'Long project name','Phase 1',(#{ctx}),#{assignment})")
        parent = ids["project"]
        ids["site"] = None
        ids["building"] = None
        if site:
            sp = self.placement()
            ids["site_placement"] = sp
            ids["site"] = self.add(f"IFCSITE('{self.guid()}',$,'Site',$,$,#{sp},$,$,.ELEMENT.,(24,28,0),(54,25,0),10.,$,$)")
            self.add(f"IFCRELAGGREGATES('{self.guid()}',$,$,$,#{parent},(#{ids['site']}))")
            parent = ids["site"]
            parent_place = sp
        else:
            parent_place = None
        if building:
            bp = self.placement(parent_place)
            ids["building_placement"] = bp
            ids["building"] = self.add(f"IFCBUILDING('{self.guid()}',$,'Building',$,$,#{bp},$,$,.ELEMENT.,$,$,$)")
            self.add(f"IFCRELAGGREGATES('{self.guid()}',$,$,$,#{parent},(#{ids['building']}))")
            parent = ids["building"]
            parent_place = bp
        ids["storeys"] = []
        ids["storey_places"] = []
        stor = []
        for k in range(storeys):
            z = 3000 * k if self.unit == "mm" else 3 * k
            sp = self.placement(parent_place, 0, 0, z)
            sid = self.add(f"IFCBUILDINGSTOREY('{self.guid()}',$,'Level {k + 1}',$,$,#{sp},$,$,.ELEMENT.,{r(z)})")
            ids["storeys"].append(sid)
            ids["storey_places"].append(sp)
            stor.append(sid)
        if stor:
            self.add(f"IFCRELAGGREGATES('{self.guid()}',$,$,$,#{parent},(" + ",".join(f"#{s}" for s in stor) + "))")
        return ids

    # ---- representations -----------------------------------------------------------------------
    def extrusion(self, profile, depth, ids, position=None, direction=(0, 0, 1)):
        pos = position if position else self.axis3(0, 0, 0)
        d = self.direction(*direction)
        return self.add(f"IFCEXTRUDEDAREASOLID(#{profile},#{pos},#{d},{r(depth)})")

    def rect_profile(self, w, d):
        c = self.add("IFCAXIS2PLACEMENT2D(#%d,$)" % self.point(0, 0))
        return self.add(f"IFCRECTANGLEPROFILEDEF(.AREA.,$,#{c},{r(w)},{r(d)})")

    def circle_profile(self, radius):
        c = self.add("IFCAXIS2PLACEMENT2D(#%d,$)" % self.point(0, 0))
        return self.add(f"IFCCIRCLEPROFILEDEF(.AREA.,$,#{c},{r(radius)})")

    def polyline_profile(self, pts, voids=()):
        def curve(points):
            ids = [self.point(x, y) for x, y in points]
            ids.append(ids[0])
            return self.add("IFCPOLYLINE((" + ",".join(f"#{i}" for i in ids) + "))")

        outer = curve(pts)
        if voids:
            inner = [curve(v) for v in voids]
            return self.add(f"IFCARBITRARYPROFILEDEFWITHVOIDS(.AREA.,$,#{outer},(" + ",".join(f"#{i}" for i in inner) + "))")
        return self.add(f"IFCARBITRARYCLOSEDPROFILEDEF(.AREA.,$,#{outer})")

    def shape(self, ids, items, rep_type="SweptSolid", identifier="Body"):
        rep = self.add(f"IFCSHAPEREPRESENTATION(#{ids['body']},'{identifier}','{rep_type}',(" + ",".join(f"#{i}" for i in items) + "))")
        return self.add(f"IFCPRODUCTDEFINITIONSHAPE($,$,(#{rep}))")

    # ---- products ------------------------------------------------------------------------------
    def product(self, cls, name, place, shape=None, guid=None, extra=None, oid="$"):
        """IFC4 IfcBuildingElement-shaped product: (GlobalId, OwnerHistory, Name, Description, ObjectType,
        ObjectPlacement, Representation, Tag, PredefinedType)."""
        g = guid if guid else self.guid()
        tail = extra if extra is not None else "$,$"
        return self.add(f"{cls}('{g}',{oid},'{name}',$,$,#{place},{'#%d' % shape if shape else '$'},{tail})")

    def contain(self, structure, elements):
        return self.add(f"IFCRELCONTAINEDINSPATIALSTRUCTURE('{self.guid()}',$,$,$,(" + ",".join(f"#{e}" for e in elements) + f"),#{structure})")

    def aggregate(self, whole, parts):
        return self.add(f"IFCRELAGGREGATES('{self.guid()}',$,$,$,#{whole},(" + ",".join(f"#{p}" for p in parts) + "))")

    def write(self, name):
        text = self.header() + "\n".join(self.lines) + "\nENDSEC;\nEND-ISO-10303-21;\n"
        (out / name).write_text(text)


# --- tree-cases.ifc -------------------------------------------------------------------------------------
# Project > Site > Building > Level 1, Level 2 (3 m up). Level 1 holds wall W1 (1000 x 200 x 2000 mm at
# the origin) and proxy E2 (500 mm cube at x = 2000); E2 is also listed in Level 2's container (two
# parents: the first, Level 1, wins). Assembly A1 (no representation) in Level 1 aggregates proxies P1 and
# P2. Space SP1 (a 1000 mm box) is aggregated by Level 1. U1 is a proxy that nothing contains. Level 2
# holds D1 and D2 (the same GlobalId), I1 (a GlobalId of 21 characters) and L1 (on a presentation layer that
# is off). Group G1 and zone Z1 and system S1 collect W1 and E2. C1 and C2 aggregate each other (a cycle
# no spatial element reaches).
f = File(purpose="tree cases: containment, aggregation, two parents, uncontained, duplicate and invalid GlobalIds, a layer that is off, groups, a cycle")
ids = f.spine(storeys=2)
S1, S2 = ids["storeys"]
P1_, P2_ = ids["storey_places"]
BOX = lambda w, d, h: f.shape(ids, [f.extrusion(f.rect_profile(w, d), h, ids)])
W1 = f.product("IFCWALL", "W1", f.placement(P1_, 0, 0, 0), BOX(1000, 200, 2000))
E2 = f.product("IFCBUILDINGELEMENTPROXY", "E2", f.placement(P1_, 2000, 0, 0), BOX(500, 500, 500))
A1 = f.add(f"IFCELEMENTASSEMBLY('{f.guid()}',$,'A1',$,$,#{f.placement(P1_, 5000, 0, 0)},$,$,$,$)")
PA = f.product("IFCBUILDINGELEMENTPROXY", "P1", f.placement(P1_, 5000, 0, 0), BOX(400, 400, 400))
PB = f.product("IFCBUILDINGELEMENTPROXY", "P2", f.placement(P1_, 5600, 0, 0), BOX(400, 400, 400))
SP1 = f.add(f"IFCSPACE('{f.guid()}',$,'SP1',$,$,#{f.placement(P1_, 8000, 0, 0)},#{BOX(1000, 1000, 1000)},'Room one',.ELEMENT.,.INTERNAL.,$)")
U1 = f.product("IFCBUILDINGELEMENTPROXY", "U1", f.placement(None, 20000, 0, 0), BOX(300, 300, 300))
dup_guid = f.guid()
D1 = f.product("IFCBUILDINGELEMENTPROXY", "D1", f.placement(P2_, 0, 0, 0), BOX(300, 300, 300), guid=dup_guid)
D2 = f.product("IFCBUILDINGELEMENTPROXY", "D2", f.placement(P2_, 1000, 0, 0), BOX(300, 300, 300), guid=dup_guid)
I1 = f.product("IFCBUILDINGELEMENTPROXY", "I1", f.placement(P2_, 2000, 0, 0), BOX(300, 300, 300), guid="TooShortGlobalId12345")
L1 = f.product("IFCBUILDINGELEMENTPROXY", "L1", f.placement(P2_, 3000, 0, 0), BOX(300, 300, 300))
f.contain(S1, [W1, E2, A1])
f.aggregate(A1, [PA, PB])
f.aggregate(S1, [SP1])
f.contain(S2, [E2, D1, D2, I1, L1])
# L1's shape representation is two entities before the product (representation, definition shape, product).
f.add(f"IFCPRESENTATIONLAYERWITHSTYLE('Hidden Layer',$,(#{L1 - 2}),'0',.F.,.F.,.F.,())")
G1 = f.add(f"IFCGROUP('{f.guid()}',$,'Fire compartment',$,$)")
Z1 = f.add(f"IFCZONE('{f.guid()}',$,'Zone one',$,$,$)")
SYS = f.add(f"IFCSYSTEM('{f.guid()}',$,'Heating system',$,$)")
for grp, members in ((G1, [W1, E2]), (Z1, [W1]), (SYS, [E2])):
    f.add(f"IFCRELASSIGNSTOGROUP('{f.guid()}',$,$,$,(" + ",".join(f"#{m}" for m in members) + f"),$,#{grp})")
C1 = f.product("IFCBUILDINGELEMENTPROXY", "C1", f.placement(None, 30000, 0, 0), BOX(200, 200, 200))
C2 = f.product("IFCBUILDINGELEMENTPROXY", "C2", f.placement(None, 31000, 0, 0), BOX(200, 200, 200))
f.aggregate(C1, [C2])
f.aggregate(C2, [C1])
f.write("tree-cases.ifc")

# --- units-inch-rounded.ifc ----------------------------------------------------------------------------------
# The length unit is an inch whose factor is written to nine digits (0.025400005 m, within 1e-6 of 0.0254): the
# units reader snaps it to 0.0254, so a wall of 40 x 8 inches, 80 high, is 1.016 x 0.2032 x 2.032 m. The
# geometry library reads the factor as written, and the helper scales its numbers to the snapped unit.


def inch_units(f):
    ids = []
    dim = f.add("IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0)")
    metre = f.add("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)")
    measure = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.025400005),#{metre})")
    ids.append(f.add(f"IFCCONVERSIONBASEDUNIT(#{dim},.LENGTHUNIT.,'INCH',#{measure})"))
    ids.append(f.add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)"))
    ids.append(f.add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)"))
    return ids


f = File(purpose="an inch written to nine digits", units=inch_units)
ids = f.spine(storeys=1)
W = f.product("IFCWALL", "W", f.placement(ids["storey_places"][0], 0, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(40, 8), 80, ids)]))
f.contain(ids["storeys"][0], [W])
f.write("units-inch-rounded.ifc")

# --- limits-hostile.ifc ----------------------------------------------------------------------------------------
# One wall whose property set holds a text of 70,000 characters, a property name of 5,000 characters, 5,000
# further properties and a length that is infinite once converted (1e308 in exametres), and a wall "Far" whose
# extrusion is 3e12 mm (3e9 m) deep, past the coordinate bound of the package. The helper keeps the package within
# what the decoder accepts and says what it left out.
f = File(purpose="values past the package's bounds")
ids = f.spine(storeys=1)
W = f.product("IFCWALL", "W", f.placement(ids["storey_places"][0], 0, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 2000, ids)]))
FAR = f.product("IFCWALL", "Far", f.placement(ids["storey_places"][0], 3000, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 3.0e12, ids)]))
f.contain(ids["storeys"][0], [W, FAR])
exa = f.add("IFCSIUNIT(*,.LENGTHUNIT.,.EXA.,.METRE.)")
props = [
    f.add("IFCPROPERTYSINGLEVALUE('LongText',$,IFCTEXT('" + "t" * 70000 + "'),$)"),
    f.add("IFCPROPERTYSINGLEVALUE('" + "n" * 5000 + "',$,IFCLABEL('x'),$)"),
    f.add(f"IFCPROPERTYSINGLEVALUE('Huge',$,IFCLENGTHMEASURE(1.E308),#{exa})"),
]
props += [f.add(f"IFCPROPERTYSINGLEVALUE('p{i}',$,IFCINTEGER({i}),$)") for i in range(5000)]
pset = f.add(f"IFCPROPERTYSET('{f.guid()}',$,'Pset_Hostile',$,(" + ",".join(f"#{p}" for p in props) + "))")
f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',$,$,$,(#{W}),#{pset})")
f.write("limits-hostile.ifc")

# --- containment-cycle.ifc ------------------------------------------------------------------------------------
# Level 1 contains space SA (a 1000 mm box), which contains space SB (the same box 2 m along), which aggregates SA
# again (the two kinds of relationship are each a tree on their own, so only walking both finds the loop): the
# relationship that closes the loop is dropped and noticed, both spaces are imported once. Wall W1 is an ordinary
# neighbour.
f = File(purpose="a cycle through containment and aggregation: two spaces that hold each other")
ids = f.spine(storeys=1)
(S1,) = ids["storeys"]
(P1_,) = ids["storey_places"]
BOX = lambda w, d, h: f.shape(ids, [f.extrusion(f.rect_profile(w, d), h, ids)])
W1 = f.product("IFCWALL", "W1", f.placement(P1_, 0, 0, 0), BOX(1000, 200, 2000))
SA = f.add(f"IFCSPACE('{f.guid()}',$,'SA',$,$,#{f.placement(P1_, 4000, 0, 0)},#{BOX(1000, 1000, 1000)},'Room A',.ELEMENT.,.INTERNAL.,$)")
SB = f.add(f"IFCSPACE('{f.guid()}',$,'SB',$,$,#{f.placement(P1_, 6000, 0, 0)},#{BOX(1000, 1000, 1000)},'Room B',.ELEMENT.,.INTERNAL.,$)")
f.contain(S1, [W1, SA])
f.contain(SA, [SB])
f.aggregate(SB, [SA])
f.write("containment-cycle.ifc")

# --- restart-silent.ifc ---------------------------------------------------------------------------------------
# Walls in one storey, in this order: a good wall G1; Z1 whose extrusion has no depth (the library logs an
# error and yields nothing); E1 whose representation has no items (nothing is yielded either); nine more like Z1;
# X1, a wall that stops the library (an IfcMaterialLayerSetUsage whose ForLayerSet is a point, not a layer set); and a good
# wall G2. A restart that left out "the next task" would blame Z1 and then E1; the helper tries the tasks one
# in growing and halving ranges and leaves out X1 only.
f = File(purpose="walls that yield nothing before a wall that stops the library")
ids = f.spine(storeys=1)
(S1,) = ids["storeys"]
(P1_,) = ids["storey_places"]
BOX = lambda w, d, h: f.shape(ids, [f.extrusion(f.rect_profile(w, d), h, ids)])
G1 = f.product("IFCWALL", "G1", f.placement(P1_, 0, 0, 0), BOX(1000, 200, 2000))
Z1 = f.product("IFCWALL", "Z1", f.placement(P1_, 2000, 0, 0), BOX(1000, 200, 0))
E1 = f.product("IFCWALL", "E1", f.placement(P1_, 4000, 0, 0), f.shape(ids, []))
# Nine more walls without depth, so that the search for the wall at fault has several ranges to grow and halve.
more = [f.product("IFCWALL", "Z%d" % (k + 2), f.placement(P1_, 10000 + 2000 * k, 0, 0), BOX(1000, 200, 0)) for k in range(9)]
X1 = f.product("IFCWALL", "X1", f.placement(P1_, 6000, 0, 0), BOX(1000, 200, 2000))
G2 = f.product("IFCWALL", "G2", f.placement(P1_, 8000, 0, 0), BOX(1000, 200, 2000))
f.contain(S1, [G1, Z1, E1, *more, X1, G2])
usage = f.add(f"IFCMATERIALLAYERSETUSAGE(#{f.point(0, 0, 0)},.AXIS2.,.POSITIVE.,-100.,$)")
f.add(f"IFCRELASSOCIATESMATERIAL('{f.guid()}',$,$,$,(#{X1}),#{usage})")
f.write("restart-silent.ifc")

# --- psets-types-escapes.ifc ----------------------------------------------------------------------------------
# One wall W (mm file) with Pset_WallCommon (a label, a boolean, a logical that is UNKNOWN, a length of 200
# mm = 0.2 m, an area, a thermal transmittance that is stored as written, a label with STEP escapes
# 'caf\X2\00E9\X0\' = "café" and 'na\S\i' = "naïve", a length of 10 with its own unit INCH = 0.254 m, an enumerated
# value, a bounded value 100..200 mm, a list value, a complex property, a table value) and a quantity set
# (Length 5000 mm = 5 m, Area 12.5, Volume 2.5, Count 4, Weight 100, a length of 3 with its own unit FOOT =
# 0.9144 m). The wall's type WT has Pset_WallCommon (FireRating 'EI30', which the wall overrides, and
# Reference 'WT1-ref') and a quantity set (NetArea 7 m2). A second wall W2 has only the type. A
# classification reference is attached to W.
f = File(purpose="properties, types, escapes, own units, classification")
ids = f.spine(storeys=1)
S1 = ids["storeys"][0]
P1_ = ids["storey_places"][0]
W = f.product("IFCWALL", "W", f.placement(P1_, 0, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 2000, ids)]))
W2 = f.product("IFCWALL", "W2", f.placement(P1_, 3000, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 2000, ids)]))
f.contain(S1, [W, W2])


def prop(name, value, unit="$"):
    return f.add(f"IFCPROPERTYSINGLEVALUE('{name}',$,{value},{unit})")


inch_dim = f.add("IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0)")
inch_measure = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.0254),#{f.add('IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)')})")
inch = f.add(f"IFCCONVERSIONBASEDUNIT(#{inch_dim},.LENGTHUNIT.,'INCH',#{inch_measure})")
foot_measure = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.3048),#{f.add('IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)')})")
foot = f.add(f"IFCCONVERSIONBASEDUNIT(#{inch_dim},.LENGTHUNIT.,'FOOT',#{foot_measure})")
props = [
    prop("FireRating", "IFCLABEL('EI60')"),
    prop("IsExternal", "IFCBOOLEAN(.T.)"),
    prop("LoadBearing", "IFCLOGICAL(.U.)"),
    prop("Width", "IFCPOSITIVELENGTHMEASURE(200.)"),
    prop("GrossArea", "IFCAREAMEASURE(3.5)"),
    prop("ThermalTransmittance", "IFCTHERMALTRANSMITTANCEMEASURE(0.24)"),
    prop("Cafe", "IFCLABEL('caf\\X2\\00E9\\X0\\')"),
    prop("Naive", "IFCTEXT('na\\S\\ove')"),
    prop("OwnUnit", "IFCLENGTHMEASURE(10.)", f"#{inch}"),
    f.add("IFCPROPERTYENUMERATEDVALUE('Finish',$,(IFCLABEL('Matt'),IFCLABEL('Fine')),$)"),
    f.add("IFCPROPERTYBOUNDEDVALUE('Range',$,IFCLENGTHMEASURE(200.),IFCLENGTHMEASURE(100.),$,$)"),
    f.add("IFCPROPERTYLISTVALUE('Codes',$,(IFCINTEGER(1),IFCINTEGER(2)),$)"),
    f.add("IFCCOMPLEXPROPERTY('Complex',$,'Usage',())"),
    f.add("IFCPROPERTYTABLEVALUE('Table',$,(IFCLABEL('a')),(IFCLABEL('b')),$,$,$,$)"),
]
pset = f.add(f"IFCPROPERTYSET('{f.guid()}',$,'Pset_WallCommon',$,(" + ",".join(f"#{p}" for p in props) + "))")
quantities = [
    f.add("IFCQUANTITYLENGTH('Length',$,$,5000.,$)"),
    f.add("IFCQUANTITYAREA('NetSideArea',$,$,12.5,$)"),
    f.add("IFCQUANTITYVOLUME('NetVolume',$,$,2.5,$)"),
    f.add("IFCQUANTITYCOUNT('Openings',$,$,4.,$)"),
    f.add("IFCQUANTITYWEIGHT('Mass',$,$,100.,$)"),
    f.add(f"IFCQUANTITYLENGTH('Depth',$,#{foot},3.,$)"),
]
qset = f.add(f"IFCELEMENTQUANTITY('{f.guid()}',$,'Qto_WallBaseQuantities',$,$,(" + ",".join(f"#{q}" for q in quantities) + "))")
f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',$,$,$,(#{W}),#{pset})")
f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',$,$,$,(#{W}),#{qset})")
tp = [prop("FireRating", "IFCLABEL('EI30')"), prop("Reference", "IFCIDENTIFIER('WT1-ref')")]
tpset = f.add(f"IFCPROPERTYSET('{f.guid()}',$,'Pset_WallCommon',$,(" + ",".join(f"#{p}" for p in tp) + "))")
tq = f.add("IFCQUANTITYAREA('NetArea',$,$,7.,$)")
tqset = f.add(f"IFCELEMENTQUANTITY('{f.guid()}',$,'Qto_WallTypeQuantities',$,$,(#{tq}))")
WT = f.add(f"IFCWALLTYPE('{f.guid()}',$,'WT1',$,$,(#{tpset},#{tqset}),$,$,$,.NOTDEFINED.)")
f.add(f"IFCRELDEFINESBYTYPE('{f.guid()}',$,$,$,(#{W},#{W2}),#{WT})")
cls = f.add("IFCCLASSIFICATION('Example Org','2026',$,'Example Classification',$,$,$)")
ref = f.add(f"IFCCLASSIFICATIONREFERENCE('https://example.org/Ss_25',  'Ss_25_10','Wall systems',#{cls},$,$)")
f.add(f"IFCRELASSOCIATESCLASSIFICATION('{f.guid()}',$,$,$,(#{W}),#{ref})")
f.write("psets-types-escapes.ifc")

# --- units-own.ifc -----------------------------------------------------------------------------------------
# mm length; the project's area unit is SQUARE FOOT (0.09290304) and volume unit CUBIC FOOT
# (0.028316846592). The wall has quantities Area 100 (= 9.290304 m2) and Volume 10 (= 0.28316846592 m3),
# and a property of 2 feet = 0.6096 m written as IFCLENGTHMEASURE with its own unit FOOT.


def own_units(f):
    ids = []
    ids.append(f.add("IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.)"))
    dim2 = f.add("IFCDIMENSIONALEXPONENTS(2,0,0,0,0,0,0)")
    dim3 = f.add("IFCDIMENSIONALEXPONENTS(3,0,0,0,0,0,0)")
    sq = f.add(f"IFCMEASUREWITHUNIT(IFCAREAMEASURE(0.09290304),#{f.add('IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)')})")
    cu = f.add(f"IFCMEASUREWITHUNIT(IFCVOLUMEMEASURE(0.028316846592),#{f.add('IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)')})")
    ids.append(f.add(f"IFCCONVERSIONBASEDUNIT(#{dim2},.AREAUNIT.,'SQUARE FOOT',#{sq})"))
    ids.append(f.add(f"IFCCONVERSIONBASEDUNIT(#{dim3},.VOLUMEUNIT.,'CUBIC FOOT',#{cu})"))
    return ids


f = File(purpose="area and volume units that are conversion based", units=own_units)
ids = f.spine(storeys=1)
W = f.product("IFCWALL", "W", f.placement(ids["storey_places"][0], 0, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 2000, ids)]))
f.contain(ids["storeys"][0], [W])
q = [f.add("IFCQUANTITYAREA('Area',$,$,100.,$)"), f.add("IFCQUANTITYVOLUME('Volume',$,$,10.,$)")]
qs = f.add(f"IFCELEMENTQUANTITY('{f.guid()}',$,'Qto_Own',$,$,(" + ",".join(f"#{x}" for x in q) + "))")
foot_dim = f.add("IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0)")
fm = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.3048),#{f.add('IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)')})")
ft = f.add(f"IFCCONVERSIONBASEDUNIT(#{foot_dim},.LENGTHUNIT.,'FOOT',#{fm})")
ps = f.add(f"IFCPROPERTYSET('{f.guid()}',$,'Pset_Own',$,(#{f.add('IFCPROPERTYSINGLEVALUE(' + chr(39) + 'TwoFeet' + chr(39) + ',$,IFCLENGTHMEASURE(2.),#%d)' % ft)}))")
f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',$,$,$,(#{W}),#{qs})")
f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',$,$,$,(#{W}),#{ps})")
f.write("units-own.ifc")

# --- units-furlong.ifc: a length unit that is neither SI, inch, foot, yard nor survey foot -----------------------
def furlong(f):
    dim = f.add("IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0)")
    m = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(201.168),#{f.add('IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)')})")
    return [f.add(f"IFCCONVERSIONBASEDUNIT(#{dim},.LENGTHUNIT.,'FURLONG',#{m})")]


f = File(purpose="a length unit the import refuses", units=furlong)
ids = f.spine(storeys=1)
f.write("units-furlong.ifc")

# --- extrusion-profiles.ifc -----------------------------------------------------------------------------------
# mm. Four proxies in Level 1, each an extruded profile at x = 0, 5000, 10000, 15000:
#   rectangle 2000 x 1000, depth 3000   : 2 x 1 x 3          = 6.0 m3
#   circle radius 500, depth 2000       : pi x 0.25 x 2      = 1.5707963 m3 (a polygon: within 0.5%)
#   L-shape 3000 x 3000 minus 2000 x 2000 corner, depth 500 : (9 - 4) x 0.5 = 2.5 m3  (area 5 m2)
#   square 4000 x 4000 with a 2000 x 2000 hole, depth 1000   : (16 - 4) x 1 = 12.0 m3  (area 12 m2)
f = File(purpose="extruded profiles: rectangle, circle, arbitrary, arbitrary with voids")
ids = f.spine(storeys=1)
S1, P1_ = ids["storeys"][0], ids["storey_places"][0]
prof = []
prof.append(("rectangle", f.rect_profile(2000, 1000), 3000))
prof.append(("circle", f.circle_profile(500), 2000))
prof.append(("l-shape", f.polyline_profile([(0, 0), (3000, 0), (3000, 1000), (1000, 1000), (1000, 3000), (0, 3000)]), 500))
prof.append(("with-voids", f.polyline_profile([(0, 0), (4000, 0), (4000, 4000), (0, 4000)], [[(1000, 1000), (1000, 3000), (3000, 3000), (3000, 1000)]]), 1000))
els = []
for k, (name, profile, depth) in enumerate(prof):
    els.append(f.product("IFCBUILDINGELEMENTPROXY", name, f.placement(P1_, 5000 * k, 0, 0), f.shape(ids, [f.extrusion(profile, depth, ids)])))
f.contain(S1, els)
f.write("extrusion-profiles.ifc")

# --- clipped-wall.ifc ---------------------------------------------------------------------------------------
# mm. A wall 4000 x 200 x 3000 (centred footprint, base at z = 0) clipped by IfcBooleanClippingResult
# (DIFFERENCE) with a half space whose plane is z = 2000 and AgreementFlag .F. (the material of the half
# space is on the +z side): what remains is z <= 2000: 4 x 0.2 x 2 = 1.6 m3.
f = File(purpose="a wall clipped by a half space: 1.6 m3")
ids = f.spine(storeys=1)
S1, P1_ = ids["storeys"][0], ids["storey_places"][0]
solid = f.extrusion(f.rect_profile(4000, 200), 3000, ids)
plane = f.add(f"IFCPLANE(#{f.axis3(0, 0, 2000)})")
half = f.add(f"IFCHALFSPACESOLID(#{plane},.F.)")
clip = f.add(f"IFCBOOLEANCLIPPINGRESULT(.DIFFERENCE.,#{solid},#{half})")
W = f.product("IFCWALL", "clipped", f.placement(P1_, 0, 0, 0), f.shape(ids, [clip], rep_type="Clipping"))
f.contain(S1, [W])
f.write("clipped-wall.ifc")

# --- mapped-items.ifc ---------------------------------------------------------------------------------------
# mm. One representation map (a 1000 mm cube) used by three proxies placed at x = 0, 3000 and (rotated 90
# degrees about z) at y = 3000. Each is 1 m3; the library shares one mesh.
f = File(purpose="three mapped items of one representation map")
ids = f.spine(storeys=1)
S1, P1_ = ids["storeys"][0], ids["storey_places"][0]
cube_shape_item = f.extrusion(f.rect_profile(1000, 1000), 1000, ids)
source_rep = f.add(f"IFCSHAPEREPRESENTATION(#{ids['body']},'Body','SweptSolid',(#{cube_shape_item}))")
rmap = f.add(f"IFCREPRESENTATIONMAP(#{f.axis3(0, 0, 0)},#{source_rep})")
els = []
for k, (x, y, rot) in enumerate([(0, 0, None), (3000, 0, None), (0, 3000, ((0, 1, 0), (-1, 0, 0)))]):
    op = f.add(f"IFCCARTESIANTRANSFORMATIONOPERATOR3D($,$,#{f.point(0, 0, 0)},$,$)")
    item = f.add(f"IFCMAPPEDITEM(#{rmap},#{op})")
    rep = f.add(f"IFCSHAPEREPRESENTATION(#{ids['body']},'Body','MappedRepresentation',(#{item}))")
    shape = f.add(f"IFCPRODUCTDEFINITIONSHAPE($,$,(#{rep}))")
    place = f.placement(P1_, x, y, 0, axis=(0, 0, 1), ref=rot[0] if rot else None)
    els.append(f.product("IFCBUILDINGELEMENTPROXY", f"mapped {k}", place, shape))
f.contain(S1, els)
f.write("mapped-items.ifc")

# --- polygonal-face-set.ifc ----------------------------------------------------------------------------------
# mm (IFC4). A 1000 mm cube as an IfcPolygonalFaceSet of six quadrilaterals (1.0 m3, 6.0 m2), and a 2000 x
# 2000 x 100 mm plate whose top and bottom faces have a 1000 x 1000 square hole (IfcIndexedPolygonalFaceWithVoids):
# volume (4 - 1) x 0.1 = 0.3 m3.
f = File(purpose="polygonal face sets: a cube and a plate with a hole")
ids = f.spine(storeys=1)
S1, P1_ = ids["storeys"][0], ids["storey_places"][0]
cube_pts = [(0, 0, 0), (1000, 0, 0), (1000, 1000, 0), (0, 1000, 0), (0, 0, 1000), (1000, 0, 1000), (1000, 1000, 1000), (0, 1000, 1000)]
plist = f.add("IFCCARTESIANPOINTLIST3D((" + ",".join("(" + ",".join(r(c) for c in p) + ")" for p in cube_pts) + "))")
quads = [(1, 4, 3, 2), (5, 6, 7, 8), (1, 2, 6, 5), (2, 3, 7, 6), (3, 4, 8, 7), (4, 1, 5, 8)]
faces = [f.add("IFCINDEXEDPOLYGONALFACE((" + ",".join(str(i) for i in q) + "))") for q in quads]
fset = f.add(f"IFCPOLYGONALFACESET(#{plist},.T.,(" + ",".join(f"#{x}" for x in faces) + "),$)")
cube = f.product("IFCBUILDINGELEMENTPROXY", "polygonal cube", f.placement(P1_, 0, 0, 0), f.shape(ids, [fset], rep_type="Tessellation"))
# plate: points 1-4 outer bottom, 5-8 outer top, 9-12 hole bottom, 13-16 hole top
pp = [(0, 0, 0), (2000, 0, 0), (2000, 2000, 0), (0, 2000, 0), (0, 0, 100), (2000, 0, 100), (2000, 2000, 100), (0, 2000, 100),
      (500, 500, 0), (1500, 500, 0), (1500, 1500, 0), (500, 1500, 0), (500, 500, 100), (1500, 500, 100), (1500, 1500, 100), (500, 1500, 100)]
plist2 = f.add("IFCCARTESIANPOINTLIST3D((" + ",".join("(" + ",".join(r(c) for c in p) + ")" for p in pp) + "))")
sides = [(1, 2, 6, 5), (2, 3, 7, 6), (3, 4, 8, 7), (4, 1, 5, 8), (9, 12, 16, 13), (12, 11, 15, 16), (11, 10, 14, 15), (10, 9, 13, 14)]
pf = [f.add("IFCINDEXEDPOLYGONALFACE((" + ",".join(str(i) for i in q) + "))") for q in sides]
top = f.add("IFCINDEXEDPOLYGONALFACEWITHVOIDS((5,6,7,8),((13,16,15,14)))")
bottom = f.add("IFCINDEXEDPOLYGONALFACEWITHVOIDS((1,4,3,2),((9,10,11,12)))")
fset2 = f.add(f"IFCPOLYGONALFACESET(#{plist2},.T.,(" + ",".join(f"#{x}" for x in pf + [top, bottom]) + "),$)")
plate = f.product("IFCBUILDINGELEMENTPROXY", "plate with hole", f.placement(P1_, 4000, 0, 0), f.shape(ids, [fset2], rep_type="Tessellation"))
f.contain(S1, [cube, plate])
f.write("polygonal-face-set.ifc")

# --- unsupported-all.ifc -------------------------------------------------------------------------------------
# mm (IFC4). In Level 1: wall OK (a good box), wall GRID (IfcGridPlacement), walls CYC1 and CYC2 (placements
# that refer to each other), wall EMPTY (an empty body representation), wall NOREP (no representation), a virtual
# element (a box) and an annotation and a grid. Properties with a table value and a complex property are
# unmapped; an IfcRelSpaceBoundary and an IfcRelNests are omitted.
f = File(purpose="unsupported classes, placements and properties")
ids = f.spine(storeys=1)
S1, P1_ = ids["storeys"][0], ids["storey_places"][0]
BOX = lambda w, d, h: f.shape(ids, [f.extrusion(f.rect_profile(w, d), h, ids)])
OK = f.product("IFCWALL", "OK", f.placement(P1_, 0, 0, 0), BOX(1000, 200, 2000))
gx = f.add(f"IFCGRIDAXIS('A',#{f.add('IFCPOLYLINE((#%d,#%d))' % (f.point(0, 0), f.point(1000, 0)))},.T.)")
gy = f.add(f"IFCGRIDAXIS('1',#{f.add('IFCPOLYLINE((#%d,#%d))' % (f.point(0, 0), f.point(0, 1000)))},.T.)")
vg = f.add(f"IFCVIRTUALGRIDINTERSECTION((#{gx},#{gy}),(0.,0.,0.))")
gridplace = f.add(f"IFCGRIDPLACEMENT(#{vg},$)")
GRID = f.product("IFCWALL", "GRID", gridplace, BOX(1000, 200, 2000))
cyc1_axis = f.axis3(0, 0, 0)
cyc2_axis = f.axis3(0, 0, 0)
# placements #a and #b refer to each other
a_id = f.n + 1
b_id = f.n + 2
f.add(f"IFCLOCALPLACEMENT(#{b_id},#{cyc1_axis})")
f.add(f"IFCLOCALPLACEMENT(#{a_id},#{cyc2_axis})")
CYC1 = f.product("IFCWALL", "CYC1", a_id, BOX(1000, 200, 2000))
CYC2 = f.product("IFCWALL", "CYC2", b_id, BOX(1000, 200, 2000))
empty_rep = f.add(f"IFCSHAPEREPRESENTATION(#{ids['body']},'Body','SweptSolid',())")
EMPTY = f.product("IFCWALL", "EMPTY", f.placement(P1_, 6000, 0, 0), f.add(f"IFCPRODUCTDEFINITIONSHAPE($,$,(#{empty_rep}))"))
NOREP = f.product("IFCWALL", "NOREP", f.placement(P1_, 8000, 0, 0), None)
VIRT = f.add(f"IFCVIRTUALELEMENT('{f.guid()}',$,'virtual',$,$,#{f.placement(P1_, 10000, 0, 0)},#{BOX(500, 500, 500)},$)")
ann_rep = f.add(f"IFCSHAPEREPRESENTATION(#{f.add('IFCGEOMETRICREPRESENTATIONSUBCONTEXT(%s,%s,*,*,*,*,#%d,$,.PLAN_VIEW.,$)' % (chr(39) + 'Annotation' + chr(39), chr(39) + 'Model' + chr(39), ids['context']))},'Annotation','Annotation2D',(#{f.add('IFCPOLYLINE((#%d,#%d))' % (f.point(0, 0), f.point(500, 500)))}))")
ANN = f.add(f"IFCANNOTATION('{f.guid()}',$,'note',$,$,#{f.placement(P1_, 12000, 0, 0)},#{f.add('IFCPRODUCTDEFINITIONSHAPE($,$,(#%d))' % ann_rep)})")
GRIDELEM = f.add(f"IFCGRID('{f.guid()}',$,'grid',$,$,#{f.placement(P1_, 0, 0, 0)},$,(#{gx}),(#{gy}),$,$,$)")
f.contain(S1, [OK, GRID, CYC1, CYC2, EMPTY, NOREP, VIRT, ANN, GRIDELEM])
f.add(f"IFCRELNESTS('{f.guid()}',$,$,$,#{OK},(#{GRID}))")
f.write("unsupported-all.ifc")

# --- far-from-origin.ifc --------------------------------------------------------------------------------------
# mm. A wall at x = 2,000,000 m (2e9 mm): beyond the native limit.
f = File(purpose="a model 2,000 km from the origin")
ids = f.spine(storeys=1)
W = f.product("IFCWALL", "far", f.placement(ids["storey_places"][0], 2.0e9, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 2000, ids)]))
f.contain(ids["storeys"][0], [W])
f.write("far-from-origin.ifc")

# --- ifc43-infrastructure.ifc -----------------------------------------------------------------------------------
# IFC4X3_ADD2, mm. Site > Road (an IfcFacility) > RoadPart > one proxy; Site > Bridge with nothing in it; and
# an IfcAlignment (no geometry) aggregated by the site. The road is a plain group (it holds a supported
# element), the bridge is omitted, the alignment is not imported.
f = File(schema="IFC4X3_ADD2", purpose="IFC4.3 infrastructure spatial elements")
ids = f.spine(storeys=0, building=False)
site, sp = ids["site"], ids["site_placement"]
road = f.add(f"IFCROAD('{f.guid()}',$,'Road',$,$,#{f.placement(sp)},$,$,.ELEMENT.,$)")
part = f.add(f"IFCROADPART('{f.guid()}',$,'Road part',$,$,#{f.placement(sp)},$,$,.ELEMENT.,$,.ROADSEGMENT.)")
bridge = f.add(f"IFCBRIDGE('{f.guid()}',$,'Bridge',$,$,#{f.placement(sp)},$,$,.ELEMENT.,$)")
align = f.add(f"IFCALIGNMENT('{f.guid()}',$,'Alignment',$,$,#{f.placement(sp)},$,$)")
el = f.product("IFCBUILDINGELEMENTPROXY", "kerb", f.placement(sp, 0, 0, 0), f.shape(ids, [f.extrusion(f.rect_profile(1000, 200), 500, ids)]))
f.aggregate(site, [road, bridge, align])
f.aggregate(road, [part])
f.contain(part, [el])
f.write("ifc43-infrastructure.ifc")
