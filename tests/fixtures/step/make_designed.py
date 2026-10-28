#!/usr/bin/env python3
"""Writes the designed E4 fixtures of ADR 0172 section 7 (R098.e, R098.g): a file with a bad face, a PMI-only AP242 file,
a converting AP242 file with PMI beside its box, and a product tree deeper than the 128 levels the import keeps.

    python3 -I tests/fixtures/step/make_designed.py OUTPUT_DIR

Each file is written in ASCII with fixed text, so a second run is byte-identical. The fixtures are listed with their
SHA-256 in STEP-SOURCE.txt.

e4-bad-face.step   two boxes in one shape representation. The first is a closed solid of 10 x 20 x 30 mm. The second is
                   the same box whose face 2 (the top of the y = 0 side) has an edge loop of three oriented edges, not
                   four: an open loop, so the solid cannot be closed.
e4-pmi-ap242.step  an AP242 file with product structure and one dimension (a diameter on a shape aspect), and no shape
                   representation at all.
e4-pmi-solid.step  (R098.g) an AP242 file whose box converts, with PMI beside it: 34 dimensions (more than the 32 located
                   per kind), a shape aspect and its value, a flatness tolerance, a datum and a draughting callout.
deep-tree.step     (R098.g) a chain of 128 nested assemblies (depths 0 to 127), a box at depth 1 that converts, and a box
                   at depth 128 below the last assembly. The import keeps the depths 0 to 126 (the document's 128 records,
                   the file's root included), so the assembly at depth 127 and the box at depth 128 are not imported.
"""
import os
import sys

HEADER = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('{description}'),'2;1');
FILE_NAME('{name}','2026-10-28T00:00:00',(''),(''),'','','');
FILE_SCHEMA(('{schema}'));
ENDSEC;
DATA;
"""

AP214 = "AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }"
AP242 = "AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF { 1 0 10303 442 1 1 4 }"

# The product structure of the part, as the writer of the box fixture writes it (ids 1 to 9). The shape
# representation (id 10) and its definition (id 3) follow only when the file has solids.
PRODUCT = """#1 = APPLICATION_PROTOCOL_DEFINITION('draft international standard','automotive_design',1998,#2);
#2 = APPLICATION_CONTEXT('core data for automotive mechanical design processes');
#4 = PRODUCT_DEFINITION_SHAPE('','',#5);
#5 = PRODUCT_DEFINITION('design','',#6,#9);
#6 = PRODUCT_DEFINITION_FORMATION('','',#7);
#7 = PRODUCT('{name}','{name}','',(#8));
#8 = PRODUCT_CONTEXT('',#2,'mechanical');
#9 = PRODUCT_DEFINITION_CONTEXT('part definition',#2,'design');
"""

# The 3D context with the millimetre unit (id 900), as the box fixture declares it.
UNITS = """#900 = ( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#905)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#901,#902,#903)) REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY') );
#901 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );
#902 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );
#903 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );
#905 = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#901,'distance_accuracy_value','confusion accuracy');
"""


class Writer:
    """Numbers the entities from a start id and keeps their lines."""

    def __init__(self, start):
        self.number = start
        self.lines = []

    def add(self, text):
        self.number += 1
        self.lines.append("#%d = %s;" % (self.number, text))
        return self.number


def number(value):
    return repr(float(value))


def box(writer, x0, y0, z0, dx, dy, dz, open_face=None):
    """Writes a closed box of six planar faces with shared edges; returns the MANIFOLD_SOLID_BREP's id.

    open_face: the index of a face whose edge loop omits its last oriented edge (an open loop: the bad face)."""
    def corner(i, j, k):
        return (x0 + i * dx, y0 + j * dy, z0 + k * dz)

    vertices = {}
    edges = {}

    def vertex(point):
        if point not in vertices:
            p = writer.add("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(number(c) for c in point))
            vertices[point] = writer.add("VERTEX_POINT('',#%d)" % p)
        return vertices[point]

    def edge(a, b):
        key = (a, b) if a < b else (b, a)
        if key not in edges:
            start, end = key
            va, vb = vertex(start), vertex(end)
            p = writer.add("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(number(c) for c in start))
            d = [end[i] - start[i] for i in range(3)]
            length = sum(c * c for c in d) ** 0.5
            d = [c / length for c in d]
            direction = writer.add("DIRECTION('',(%s,%s,%s))" % tuple(number(c) for c in d))
            vector = writer.add("VECTOR('',#%d,%s)" % (direction, number(length)))
            line = writer.add("LINE('',#%d,#%d)" % (p, vector))
            edges[key] = writer.add("EDGE_CURVE('',#%d,#%d,#%d,.T.)" % (va, vb, line))
        return edges[key], a == key[0]

    faces = [
        ((0, 0, -1), [corner(0, 0, 0), corner(0, 1, 0), corner(1, 1, 0), corner(1, 0, 0)]),
        ((0, 0, 1), [corner(0, 0, 1), corner(1, 0, 1), corner(1, 1, 1), corner(0, 1, 1)]),
        ((0, -1, 0), [corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1)]),
        ((0, 1, 0), [corner(0, 1, 0), corner(0, 1, 1), corner(1, 1, 1), corner(1, 1, 0)]),
        ((-1, 0, 0), [corner(0, 0, 0), corner(0, 0, 1), corner(0, 1, 1), corner(0, 1, 0)]),
        ((1, 0, 0), [corner(1, 0, 0), corner(1, 1, 0), corner(1, 1, 1), corner(1, 0, 1)]),
    ]
    face_ids = []
    for index, (normal, ring) in enumerate(faces):
        oriented = []
        for i in range(4):
            ec, forward = edge(ring[i], ring[(i + 1) % 4])
            oriented.append(writer.add("ORIENTED_EDGE('',*,*,#%d,%s)" % (ec, ".T." if forward else ".F.")))
        if open_face == index:
            oriented = oriented[:-1]
        loop = writer.add("EDGE_LOOP('',(%s))" % ",".join("#%d" % o for o in oriented))
        bound = writer.add("FACE_OUTER_BOUND('',#%d,.T.)" % loop)
        origin = writer.add("CARTESIAN_POINT('',(%s,%s,%s))" % tuple(number(c) for c in ring[0]))
        axis = writer.add("DIRECTION('',(%s,%s,%s))" % tuple(number(c) for c in normal))
        reference = (1.0, 0.0, 0.0) if abs(normal[0]) < 0.5 else (0.0, 1.0, 0.0)
        ref = writer.add("DIRECTION('',(%s,%s,%s))" % tuple(number(c) for c in reference))
        place = writer.add("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (origin, axis, ref))
        plane = writer.add("PLANE('',#%d)" % place)
        face_ids.append(writer.add("ADVANCED_FACE('',(#%d),#%d,.T.)" % (bound, plane)))
    shell = writer.add("CLOSED_SHELL('',(%s))" % ",".join("#%d" % f for f in face_ids))
    return writer.add("MANIFOLD_SOLID_BREP('',#%d)" % shell)


def write(directory, name, schema, product, body, solids, description="SketchyUp R098.e designed fixture"):
    """Writes one file: the header, the product, the body, the shape representation (id 10) and the units."""
    text = HEADER.format(name=name, schema=schema, description=description)
    text += product.format(name=name)
    text += "\n".join(body) + "\n"
    if solids:
        text += "#3 = SHAPE_DEFINITION_REPRESENTATION(#4,#10);\n"
        text += "#10 = ADVANCED_BREP_SHAPE_REPRESENTATION('',(%s),#900);\n" % ",".join("#%d" % s for s in solids)
    text += UNITS
    text += "ENDSEC;\nEND-ISO-10303-21;\n"
    with open(os.path.join(directory, name), "w", encoding="ascii", newline="\n") as handle:
        handle.write(text)


# The context of the product tree (ids 1, 2, 8 and 9), for files whose products are written one by one.
CONTEXT = """#1 = APPLICATION_PROTOCOL_DEFINITION('draft international standard','automotive_design',1998,#2);
#2 = APPLICATION_CONTEXT('core data for automotive mechanical design processes');
#8 = PRODUCT_CONTEXT('',#2,'mechanical');
#9 = PRODUCT_DEFINITION_CONTEXT('part definition',#2,'design');
"""

# The product tree's depth: the root is depth 0, the assemblies run to depth LEVELS - 1, and the part below the last one is
# at depth LEVELS. The import keeps the depths 0 to 126 (ADR 0172 section 4), so the assembly at depth 127 and that part
# are not kept.
LEVELS = 128


def pmi_solid(directory):
    """e4-pmi-solid.step (R098.g): the box converts, and the PMI beside it is counted, not imported."""
    writer = Writer(1000)
    good = box(writer, 0, 0, 0, 10, 20, 30)
    aspect = writer.add("SHAPE_ASPECT('','',#4,.F.)")
    dimensions = [writer.add("DIMENSIONAL_SIZE('diameter %d',#%d)" % (n, aspect)) for n in range(1, 35)]
    value = writer.add("MEASURE_REPRESENTATION_ITEM('',LENGTH_MEASURE(10.),#901)")
    shape = writer.add("SHAPE_DIMENSION_REPRESENTATION('',(#%d),#900)" % value)
    writer.add("DIMENSIONAL_CHARACTERISTIC_REPRESENTATION(#%d,#%d)" % (dimensions[0], shape))
    magnitude = writer.add("LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.05),#901)")
    writer.add("FLATNESS_TOLERANCE('flatness','',#%d,#%d)" % (magnitude, aspect))
    writer.add("DATUM('A','',#4,.T.,'A')")
    writer.add("DRAUGHTING_CALLOUT('callout',(#%d))" % dimensions[0])
    write(directory, "e4-pmi-solid.step", AP242, PRODUCT, writer.lines, [good], "SketchyUp R098.g designed fixture")


def deep_tree(directory):
    """deep-tree.step (R098.g): LEVELS nested assemblies, a box at level 1 and a box below the last level."""
    writer = Writer(1000)
    origin = writer.add("CARTESIAN_POINT('',(0.,0.,0.))")
    axis = writer.add("DIRECTION('',(0.,0.,1.))")
    reference = writer.add("DIRECTION('',(1.,0.,0.))")
    identity = writer.add("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (origin, axis, reference))
    transform = writer.add("ITEM_DEFINED_TRANSFORMATION('','',#%d,#%d)" % (identity, identity))

    def product(name):
        prod = writer.add("PRODUCT('%s','%s','',(#8))" % (name, name))
        formation = writer.add("PRODUCT_DEFINITION_FORMATION('','',#%d)" % prod)
        definition = writer.add("PRODUCT_DEFINITION('design','',#%d,#9)" % formation)
        shape = writer.add("PRODUCT_DEFINITION_SHAPE('','',#%d)" % definition)
        return definition, shape

    def assembly(name):
        definition, shape = product(name)
        rep = writer.add("SHAPE_REPRESENTATION('',(#%d),#900)" % identity)
        writer.add("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (shape, rep))
        return definition, rep

    def part(name, x0):
        definition, shape = product(name)
        solid = box(writer, x0, 0, 0, 10, 20, 30)
        rep = writer.add("ADVANCED_BREP_SHAPE_REPRESENTATION('',(#%d),#900)" % solid)
        writer.add("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (shape, rep))
        return definition, rep

    def use(parent, child, number):
        # The usage of the child in the parent, its placement (the identity) and the representation relationship.
        parent_definition, parent_rep = parent
        child_definition, child_rep = child
        usage = writer.add("NEXT_ASSEMBLY_USAGE_OCCURRENCE('%d','','',#%d,#%d,$)" % (number, parent_definition, child_definition))
        placement = writer.add("PRODUCT_DEFINITION_SHAPE('Placement','Placement of an item',#%d)" % usage)
        relation = writer.add("(REPRESENTATION_RELATIONSHIP('','',#%d,#%d) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#%d) "
                              "SHAPE_REPRESENTATION_RELATIONSHIP())" % (child_rep, parent_rep, transform))
        writer.add("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#%d,#%d)" % (relation, placement))

    top = assembly("Level 0")
    use(top, part("Shallow", 0), 1)
    parent = top
    for level in range(1, LEVELS):
        child = assembly("Level %d" % level)
        use(parent, child, level + 1)
        parent = child
    use(parent, part("Below the limit", 0), LEVELS + 1)
    text = HEADER.format(name="deep-tree.step", schema=AP214, description="SketchyUp R098.g designed fixture")
    text += CONTEXT + "\n".join(writer.lines) + "\n" + UNITS + "ENDSEC;\nEND-ISO-10303-21;\n"
    with open(os.path.join(directory, "deep-tree.step"), "w", encoding="ascii", newline="\n") as handle:
        handle.write(text)


def main(directory):
    os.makedirs(directory, exist_ok=True)
    # The bad face: a good box, and a box with an open loop on its face 2.
    writer = Writer(1000)
    good = box(writer, 0, 0, 0, 10, 20, 30)
    bad = box(writer, 50, 0, 0, 10, 20, 30, open_face=1)
    write(directory, "e4-bad-face.step", AP214, PRODUCT, writer.lines, [good, bad])
    # The PMI-only AP242 file: a diameter on a shape aspect, and no shape representation (no solid, no id 10).
    pmi = [
        "#20 = SHAPE_ASPECT('','',#4,.F.)",
        "#21 = DIMENSIONAL_SIZE('diameter',#20)",
        "#22 = MEASURE_REPRESENTATION_ITEM('',LENGTH_MEASURE(10.),#901)",
        "#23 = SHAPE_DIMENSION_REPRESENTATION('',(#22),#900)",
        "#24 = DIMENSIONAL_CHARACTERISTIC_REPRESENTATION(#21,#23)",
    ]
    write(directory, "e4-pmi-ap242.step", AP242, PRODUCT, pmi, [])
    pmi_solid(directory)
    deep_tree(directory)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_designed.py OUTPUT_DIR")
    main(sys.argv[1])
