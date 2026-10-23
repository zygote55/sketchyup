#!/usr/bin/env python3
"""Writes the original (MIT) IFC fixtures of the R097.c georeferencing checks (ADR 0171 sections 3.6 and 6.3, F1-F4).

    python3 -I tests/fixtures/ifc/make_georef_fixtures.py OUTPUT_DIR

Every file is hand-checkable P21 text: one wall (an extruded rectangle) under Project > Site > Building > Level 1,
a Model context, and an IfcProjectedCRS with an IfcMapConversion (or something that looks like one). The numbers the
tests expect are in the comments; tests/ifc_georeference_tests.py regenerates the files and fails if a checked-in one
differs. The certified samples (bsi-ifc4-road.ifc with a rotation of 0, bsi-ifc4-arch.ifc with 60 degrees) are the
buildingSMART files of NOTICE.md.
"""
import sys
from pathlib import Path

out = Path(sys.argv[1])


def r(v):
    return repr(float(v))


class File:
    def __init__(self, schema, purpose):
        self.schema = schema
        self.purpose = purpose
        self.lines = []
        self.n = 0
        self.guids = 0

    def add(self, text):
        self.n += 1
        self.lines.append(f"#{self.n}={text};")
        return self.n

    def guid(self):
        self.guids += 1
        return "%022d" % self.guids

    def point(self, *c):
        return self.add("IFCCARTESIANPOINT((" + ",".join(r(v) for v in c) + "))")

    def direction(self, *c):
        return self.add("IFCDIRECTION((" + ",".join(r(v) for v in c) + "))")

    def axis3(self, x=0, y=0, z=0):
        return self.add(f"IFCAXIS2PLACEMENT3D(#{self.point(x, y, z)},$,$)")

    def placement(self, rel=None, x=0, y=0, z=0):
        return self.add(f"IFCLOCALPLACEMENT({'#%d' % rel if rel else '$'},#{self.axis3(x, y, z)})")

    def write(self, name):
        header = (
            "ISO-10303-21;\nHEADER;\n"
            f"FILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]','SketchyUp R097.c fixture: {self.purpose}'),'2;1');\n"
            "FILE_NAME('fixture.ifc','2026-10-22T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R097.c','hand-written','');\n"
            f"FILE_SCHEMA(('{self.schema}'));\nENDSEC;\nDATA;\n"
        )
        (out / name).write_text(header + "\n".join(self.lines) + "\nENDSEC;\nEND-ISO-10303-21;\n")


def build(name, purpose, *, schema="IFC4", unit="mm", wall=(0.0, 0.0), size=(4000.0, 200.0, 2500.0), wcs=(0.0, 0.0, 0.0),
          second_context=None, crs="EPSG:32633", map_unit="metre", conversions=(), conversion_class="IFCMAPCONVERSION", epsets=None):
    """One wall of `size` (width, depth, height in project units, the rectangle centred on the wall's placement) at
    `wall` (x, y) in project units. `conversions` is a list of dicts: source ('model' or 'second'), e, n, h, abscissa,
    ordinate (None for $), scale, factors (for IfcMapConversionScaled). `map_unit` is 'none', 'metre' or 'ussft'."""
    f = File(schema, purpose)
    owner = "$"
    if schema == "IFC2X3":
        person = f.add("IFCPERSON($,'Fixture',$,$,$,$,$,$)")
        organization = f.add("IFCORGANIZATION($,'SketchyUp',$,$,$)")
        pao = f.add(f"IFCPERSONANDORGANIZATION(#{person},#{organization},$)")
        application = f.add(f"IFCAPPLICATION(#{organization},'1','fixture','fixture')")
        owner = "#" + str(f.add(f"IFCOWNERHISTORY(#{pao},#{application},$,.ADDED.,$,$,$,0)"))
    length = f.add("IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.)" if unit == "mm" else "IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)")
    area = f.add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)")
    volume = f.add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)")
    units = f.add(f"IFCUNITASSIGNMENT((#{length},#{area},#{volume}))")
    ctx = f.add(f"IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#{f.axis3(*wcs)},$)")
    body = f.add(f"IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#{ctx},$,.MODEL_VIEW.,$)")
    contexts = [ctx]
    second = None
    if second_context:
        second = f.add(f"IFCGEOMETRICREPRESENTATIONCONTEXT($,'{second_context}',{3 if second_context == 'Model' else 2},1.E-05,#{f.axis3()},$)")
        contexts.append(second)
    project = f.add(f"IFCPROJECT('{f.guid()}',{owner},'Project',$,$,$,$,(" + ",".join(f"#{c}" for c in contexts) + f"),#{units})")
    site_place = f.placement()
    site = f.add(f"IFCSITE('{f.guid()}',{owner},'Site',$,$,#{site_place},$,$,.ELEMENT.,$,$,$,$,$)")
    building_place = f.placement(site_place)
    building = f.add(f"IFCBUILDING('{f.guid()}',{owner},'Building',$,$,#{building_place},$,$,.ELEMENT.,$,$,$)")
    storey_place = f.placement(building_place)
    storey = f.add(f"IFCBUILDINGSTOREY('{f.guid()}',{owner},'Level 1',$,$,#{storey_place},$,$,.ELEMENT.,0.)")
    f.add(f"IFCRELAGGREGATES('{f.guid()}',{owner},$,$,#{project},(#{site}))")
    f.add(f"IFCRELAGGREGATES('{f.guid()}',{owner},$,$,#{site},(#{building}))")
    f.add(f"IFCRELAGGREGATES('{f.guid()}',{owner},$,$,#{building},(#{storey}))")
    # The wall: a rectangle of size[0] x size[1], centred on its placement, extruded size[2] up.
    profile = f.add(f"IFCRECTANGLEPROFILEDEF(.AREA.,$,#{f.add('IFCAXIS2PLACEMENT2D(#%d,$)' % f.point(0, 0))},{r(size[0])},{r(size[1])})")
    solid = f.add(f"IFCEXTRUDEDAREASOLID(#{profile},#{f.axis3()},#{f.direction(0, 0, 1)},{r(size[2])})")
    rep = f.add(f"IFCSHAPEREPRESENTATION(#{body},'Body','SweptSolid',(#{solid}))")
    shape = f.add(f"IFCPRODUCTDEFINITIONSHAPE($,$,(#{rep}))")
    tail = "$,$" if schema != "IFC2X3" else "$"
    wall_id = f.add(f"IFCWALL('{f.guid()}',{owner},'Wall',$,$,#{f.placement(storey_place, wall[0], wall[1], 0)},#{shape},{tail})")
    f.add(f"IFCRELCONTAINEDINSPATIALSTRUCTURE('{f.guid()}',{owner},$,$,(#{wall_id}),#{storey})")
    # The projected CRS and the conversions.
    if conversions:
        unit_ref = "$"
        if map_unit == "metre":
            unit_ref = "#" + str(f.add("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)"))
        elif map_unit == "ussft":
            metre = f.add("IFCSIUNIT(*,.LENGTHUNIT.,$,.METRE.)")
            measure = f.add(f"IFCMEASUREWITHUNIT(IFCLENGTHMEASURE(0.3048006096012192),#{metre})")
            exponents = f.add("IFCDIMENSIONALEXPONENTS(1,0,0,0,0,0,0)")
            unit_ref = "#" + str(f.add(f"IFCCONVERSIONBASEDUNIT(#{exponents},.LENGTHUNIT.,'US survey foot',#{measure})"))
        crs_id = f.add(f"IFCPROJECTEDCRS('{crs}','{crs} - fixture','WGS 84',$,$,$,{unit_ref})")
        for c in conversions:
            source = ctx if c.get("source", "model") == "model" else second
            ab = "$" if c.get("abscissa") is None else r(c["abscissa"])
            od = "$" if c.get("ordinate") is None else r(c["ordinate"])
            args = f"#{source},#{crs_id},{r(c['e'])},{r(c['n'])},{r(c['h'])},{ab},{od},{r(c.get('scale', 1.0))}"
            if conversion_class == "IFCMAPCONVERSIONSCALED":
                fx, fy, fz = c.get("factors", (1.0, 1.0, 1.0))
                args += f",{r(fx)},{r(fy)},{r(fz)}"
            f.add(f"{conversion_class}({args})")
    # The community ePSet convention on the IfcSite (IFC2X3 has no IfcMapConversion).
    if epsets:
        sets = []
        for set_name, props in epsets:
            ids = [f.add(f"IFCPROPERTYSINGLEVALUE('{k}',$,{v},$)") for k, v in props]
            sets.append(f.add(f"IFCPROPERTYSET('{f.guid()}',{owner},'{set_name}',$,(" + ",".join(f"#{i}" for i in ids) + "))"))
        for s in sets:
            f.add(f"IFCRELDEFINESBYPROPERTIES('{f.guid()}',{owner},$,$,(#{site}),#{s})")
    f.write(name)


# --- georef-m-theta0.ifc (F1) ---------------------------------------------------------------------------------
# Metres. A wall 4 x 0.2 x 2.5 at (1, 2). Map conversion E 500000.5, N 4000000.25, H 12.5, axis (1, 0), scale 1, and a
# CRS with no map unit: the metre is assumed (noticed). Site origin (500000.5, 4000000.25, 12.5), label EPSG:32633.
build("georef-m-theta0.ifc", "metres, no rotation, no map unit", unit="m", wall=(1.0, 2.0), size=(4.0, 0.2, 2.5), map_unit="none",
      conversions=[dict(e=500000.5, n=4000000.25, h=12.5, abscissa=1.0, ordinate=0.0, scale=1.0)])

# --- georef-mm-rotated.ifc (F2) -------------------------------------------------------------------------------
# Millimetres. A wall 4000 x 200 x 2500 at (1000, 2000). Map conversion E 1000.5, N 2000.25, H 3.5, axis (0.6, 0.8)
# (a rotation of atan2(0.8, 0.6) = 53.13010235415598 degrees), scale 0.001, map unit metre. Project axes: the wall's
# corners are x in {-1, 3}, y in {1.9, 2.1} (metres). Grid mode turns them by (c, s) = (0.6, 0.8): x' = 0.6x - 0.8y,
# y' = 0.8x + 0.6y, so the corners go to (-2.12, 0.34), (-2.28, 0.46), (0.28, 3.54) and (0.12, 3.66).
build("georef-mm-rotated.ifc", "millimetres, rotated 3-4-5", wall=(1000.0, 2000.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=0.6, ordinate=0.8, scale=0.001)])

# --- georef-ussft-rotated.ifc (F4) ----------------------------------------------------------------------------
# Millimetres, a State Plane style conversion in US survey feet: the scale takes millimetres to feet, the map unit
# is the US survey foot. Not representable by a site origin in metres (reasons mapUnit and scale, and a rotation,
# which grid mode would answer but the others it does not).
build("georef-ussft-rotated.ifc", "map unit US survey foot", wall=(1000.0, 2000.0), map_unit="ussft", crs="EPSG:2227",
      conversions=[dict(e=6000000.0, n=2000000.0, h=10.0, abscissa=0.6, ordinate=0.8, scale=0.001 / 0.3048006096012192)])

# --- georef-wcs-offset.ifc (F4) -------------------------------------------------------------------------------
# The Model context's world coordinate system is at (500, 0, 0): the file is refused (applying it and ignoring it
# are both guesses).
build("georef-wcs-offset.ifc", "a Model context whose world coordinate system is not the identity", wall=(1000.0, 2000.0), wcs=(500.0, 0.0, 0.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-two-conversions.ifc (F4) --------------------------------------------------------------------------
# Two map conversions on the one Model context: which applies is not decided, so none does.
build("georef-two-conversions.ifc", "two map conversions on one context", wall=(1000.0, 2000.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001),
                   dict(e=2000.5, n=3000.25, h=4.5, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-two-contexts.ifc (F4) -----------------------------------------------------------------------------
# Two Model contexts and one map conversion (on the first).
build("georef-two-contexts.ifc", "two Model contexts", wall=(1000.0, 2000.0), second_context="Model",
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-conversion-on-plan.ifc (F4) -----------------------------------------------------------------------
# The one map conversion converts a Plan context, not the Model context the geometry is in.
build("georef-conversion-on-plan.ifc", "a map conversion that does not convert the Model context", wall=(1000.0, 2000.0), second_context="Plan",
      conversions=[dict(source="second", e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-axis-partial.ifc, georef-axis-zero.ifc, georef-scale-mm.ifc, georef-range.ifc ----------------------------
build("georef-axis-partial.ifc", "XAxisOrdinate without XAxisAbscissa", wall=(1000.0, 2000.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=None, ordinate=0.8, scale=0.001)])
build("georef-axis-zero.ifc", "a zero X axis direction", wall=(1000.0, 2000.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=0.0, ordinate=0.0, scale=0.001)])
# A scale of 1 in a millimetre project: the map coordinates would be millimetres, not metres.
build("georef-scale-mm.ifc", "a scale that is not the project unit in metres", wall=(1000.0, 2000.0),
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=1.0)])
# An easting beyond the site origin's bound of 100,000 km.
build("georef-range.ifc", "an easting beyond the bound of the site origin", wall=(1000.0, 2000.0),
      conversions=[dict(e=2.0e8, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-absolute-far.ifc (F3) -----------------------------------------------------------------------------
# A wall at x = 2,500,000 m (2.5e9 mm): a model authored in absolute coordinates. Refused in both modes, naming the
# extent, whatever its map conversion (E 0, N 0, H 0) says.
build("georef-absolute-far.ifc", "a model 2,500 km from the origin with a map conversion", wall=(2.5e9, 0.0),
      conversions=[dict(e=0.0, n=0.0, h=0.0, abscissa=1.0, ordinate=0.0, scale=0.001)])

# --- georef-ifc43-scaled.ifc, georef-ifc43-factors.ifc (F1, F4) -----------------------------------------------
# IFC4X3_ADD2 IfcMapConversionScaled: with factors of 1 it is a plain conversion (site origin set); with a factor
# of 0.5 on Z it is not representable.
build("georef-ifc43-scaled.ifc", "IfcMapConversionScaled with unit factors", schema="IFC4X3_ADD2", wall=(1000.0, 2000.0), conversion_class="IFCMAPCONVERSIONSCALED",
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001, factors=(1.0, 1.0, 1.0))])
build("georef-ifc43-factors.ifc", "IfcMapConversionScaled with a Z factor", schema="IFC4X3_ADD2", wall=(1000.0, 2000.0), conversion_class="IFCMAPCONVERSIONSCALED",
      conversions=[dict(e=1000.5, n=2000.25, h=3.5, abscissa=1.0, ordinate=0.0, scale=0.001, factors=(1.0, 1.0, 0.5))])

# --- georef-ifc2x3-epset.ifc (F4) -------------------------------------------------------------------------------
# IFC2X3 has no IfcMapConversion. The community ePSet_MapConversion and ePSet_ProjectedCRS on the IfcSite are read as
# ordinary properties and noticed; nothing is georeferenced.
build("georef-ifc2x3-epset.ifc", "IFC2X3 with ePSet_MapConversion on the site", schema="IFC2X3", wall=(1000.0, 2000.0),
      epsets=[("ePSet_MapConversion", [("Eastings", "IFCLENGTHMEASURE(1000.5)"), ("Northings", "IFCLENGTHMEASURE(2000.25)"),
                                       ("OrthogonalHeight", "IFCLENGTHMEASURE(3.5)"), ("Scale", "IFCREAL(0.001)")]),
              ("ePSet_ProjectedCRS", [("Name", "IFCLABEL('EPSG:32633')")])])
