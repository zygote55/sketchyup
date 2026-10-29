#!/usr/bin/env python3
"""The STEP probe and import of R098.a against the worker's own writer (ADR 0172).

    python3 -I tests/ifc_step_worker_tests.py WORKER

The corpus is the nine self-generated files: the three of ADR 0172 section 7 (a box 10 x
20 x 30, a cylinder of radius 5 and height 20, an assembly of the two, all written in
millimetres by the basic writer), the placed and mirror fixtures, and the four R098.b
XDE-writer fixtures (the instanced assembly, the named, the coloured and the layered
corpora). The test writes the corpus with --write-step and checks it back through
--probe-step and --import-step: the schema (AP214 as written, AP242 for the option), the
declared units from the raw text, the hand-computed geometry, the writer/reader round trip
(sections 1 and 2) and the unit policy of decision 4 (the declared unit is authoritative,
the option only when nothing is declared, refusal when neither is there). R098.a2 adds the
tessellated import (--import-step --output PKG): the package against the hand-computed
geometry (E1), the unit policy and the triangle caps of section 6, and the layout the
application's decoder reads (SKIFCP01, the counts, the node tree). R098.b adds the
assembly/instance node tree (a group always converts; a material index is a reference into
the manifest's materials) and the section 4 mapping. The OCCT the worker ran on is pinned
with the IfcOpenShell prefix (ADR 0171), so a change in this output is a design review, not
a silent change.
"""
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

WORKER = Path(sys.argv[1])
failures = []


def expect(condition, message):
    if not condition:
        failures.append(message)


def near(actual, expected, relative=1e-6, absolute=1e-9):
    return abs(actual - expected) <= max(relative * max(1.0, abs(expected)), absolute)


def run(*arguments, timeout=300):
    result = subprocess.run([str(WORKER), *arguments], capture_output=True, text=True, timeout=timeout)
    # The OCCT transfer statistics share stdout. The report is the last line the worker wrote
    # as its result: "R {json}" (the lifecycle runs, the package import) or a bare {json}
    # (the metadata report).
    report_lines = [line for line in result.stdout.splitlines() if line.lstrip().startswith("R ")]
    if report_lines:
        payload = json.loads(report_lines[-1][2:])
    else:
        plain = [line for line in result.stdout.splitlines() if line.lstrip().startswith("{")]
        payload = json.loads(plain[-1]) if plain else {}
    return result.returncode, payload


def box_box(report, where):
    """The six-number bounding box of a report (a shape entry or a round-trip entry)."""
    box = report.get("boundingBox")
    expect(isinstance(box, list) and len(box) == 6, f"{where}: a six-number bounding box")
    if not isinstance(box, list) or len(box) != 6:
        return
    expect(near(box[0], 0.0) and near(box[1], 0.0) and near(box[2], 0.0), f"{where}: the origin")
    expect(near(box[3], 10.0) and near(box[4], 20.0) and near(box[5], 30.0), f"{where}: the corner (10, 20, 30)")


def main():
    with tempfile.TemporaryDirectory(prefix="sketchyup-step-test-") as temp:
        temp_path = Path(temp)
        # 1. The self-generated corpus: nine files (the three section 7 basic-writer files,
        # the placed and mirror fixtures and the four R098.b XDE-writer fixtures), all
        # AP214 as written, all deterministic.
        code, corpus = run("--write-step", str(temp_path))
        expect(code == 0, f"--write-step exited {code}")
        files = corpus.get("files", [])
        expect(len(files) == 9, f"write-step writes nine files, got {len(files)}")
        for entry in files:
            expect(entry.get("written") is True, f"write-step: {entry.get('file')} written")
            expect(entry.get("bytes", 0) > 0, f"write-step: {entry.get('file')} has bytes")
            expect(entry.get("schema") == "ap214", f"write-step: {entry.get('file')} is AP214 as written")
        code2, again = run("--write-step", str(temp_path))
        expect(code2 == 0, "write-step (second) exits 0")
        for first, second in zip(files, again.get("files", [])):
            expect(
                first.get("bytes") == second.get("bytes"),
                f"write-step: deterministic bytes for {first.get('file')}",
            )

        # 2. The box, probed with the round trip (ADR 0172 section 2).
        box = temp_path / "sketchyup-step-box.step"
        code, report = run("--probe-step", str(box), "--round-trip-directory", str(temp_path / "rt"))
        expect(code == 0, f"probe of the box exited {code}")
        file = report.get("file", {})
        expect(file.get("isoHeader") is True, "probe: the ISO-10303-21 header is there")
        expect(file.get("schema") == "ap214", f"probe: the schema is AP214, got {file.get('schema')}")
        units = report.get("units", {})
        expect(units.get("declared") is True, "probe: the units are declared in the raw text")
        expect(units.get("length") == "millimetre", f"probe: the declared length unit, got {units.get('length')}")
        expect(near(units.get("lengthMetres", 0.0), 0.001, absolute=1e-12), "probe: the millimetre is 0.001 m")
        expect(units.get("angle") == "RADIAN", f"probe: the plane angle is the radian, got {units.get('angle')}")
        expect(report.get("occt", {}).get("version", "") != "", "probe: the OCCT version is reported")
        expect(report.get("entities", 0) > 0, "probe: the entity count is positive")
        expect(report.get("transferred", 0) >= 1, "probe: a shape was transferred")
        shapes = report.get("shapes", [])
        expect(len(shapes) == 1, f"probe: one shape, got {len(shapes)}")
        if shapes:
            shape = shapes[0]
            expect(shape.get("solids") == 1, f"probe: the box is one solid, got {shape.get('solids')}")
            expect(shape.get("faces") == 6, f"probe: the box has six faces, got {shape.get('faces')}")
            box_box(shape, "probe")
        round_trip = report.get("writeRoundTrip", {})
        for schema in ("ap214", "ap242"):
            entry = round_trip.get(schema, {})
            expect(entry.get("written") is True, f"round trip {schema}: the file was written")
            expect(entry.get("readStatus") == "done", f"round trip {schema}: it reads ({entry.get('readStatus')})")
            expect(
                entry.get("schema") == schema,
                f"round trip {schema}: it reads back {schema}, got {entry.get('schema')}",
            )
            if schema == "ap242":
                expect("10303 442" in entry.get("fileSchemaRaw", ""), "round trip AP242: the identifier carries part 442")
            box_box(entry, f"round trip {schema}")

        # 3. The cylinder and the assembly.
        code, cylinder = run("--probe-step", str(temp_path / "sketchyup-step-cylinder.step"))
        shapes = cylinder.get("shapes", [])
        expect(code == 0 and len(shapes) == 1, f"cylinder: one shape, got {len(shapes)}")
        if shapes:
            expect(shapes[0].get("solids") == 1, "cylinder: one solid")
            expect(shapes[0].get("faces") == 3, f"cylinder: three faces, got {shapes[0].get('faces')}")
        code, assembly = run("--probe-step", str(temp_path / "sketchyup-step-assembly.step"))
        shapes = assembly.get("shapes", [])
        expect(code == 0 and len(shapes) == 1, f"assembly: one shape, got {len(shapes)}")
        if shapes:
            expect(shapes[0].get("solids") == 2, f"assembly: two solids, got {shapes[0].get('solids')}")
            expect(shapes[0].get("faces") == 9, f"assembly: nine faces, got {shapes[0].get('faces')}")

        # 4. The unit policy (ADR 0172 decision 4): the declared unit is authoritative.
        code, imported = run("--import-step", str(box))
        expect(code == 0, f"import (no option) exited {code}")
        expect(imported.get("status") == "ok", f"import (no option): ok, got {imported.get('status')}")
        units = imported.get("units", {})
        expect(units.get("applied") == "declared", f"import (no option): the declared unit is applied, got {units.get('applied')}")
        expect(near(units.get("mmPerUnit", 0.0), 1.0, absolute=1e-12), "import (no option): the millimetre is 1 mm")
        expect(near(units.get("rawLengthMetres", 0.0), 0.001, absolute=1e-12), "import: the raw text says 0.001 m per unit")
        code, with_option = run("--import-step", str(box), "--step-length-unit", "in")
        expect(code == 0, f"import (with an option) exited {code}")
        expect(
            with_option.get("units", {}).get("applied") == "declared",
            "import (with an option): the declared unit still wins",
        )

        # 4b. The conversion-based unit without a unit context: the raw text's first declaration is
        # the conversion's millimetre base; the reader resolves no unit for a file without a
        # shape or a unit context it follows, so the raw text's declaration stands, flagged.
        # (A full conversion-based file - the committed tests/fixtures/step/inch-units.step,
        # the corpus box whose context applies the inch - is checked by the C++ test,
        # against the reader's conversion: 25.4 mm per file unit, the box 254 x 508 x 762 mm.)
        inch = temp_path / "inch-units-no-context.step"
        inch.write_text(
            "ISO-10303-21;\nHEADER;\n"
            "FILE_DESCRIPTION(('x'),'2;1');\n"
            "FILE_NAME('a','',(''),(''),'','','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n"
            "#1=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n"
            "#2=( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n"
            "#3=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#2);\n"
            "#4=( CONVERSION_BASED_UNIT('INCH',#3) LENGTH_UNIT() NAMED_UNIT(#1) );\n"
            "#5=( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\n"
            "#6=( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );\n"
            "ENDSEC;\nEND-ISO-10303-21;\n"
        )
        code, imported = run("--import-step", str(inch))
        expect(code == 0, f"import (inch, no context) exited {code}")
        expect(imported.get("status") == "no-shapes", f"import (inch, no context): no-shapes, got {imported.get('status')}")
        units = imported.get("units", {})
        expect(units.get("applied") == "declared", f"import (inch, no context): the declared unit is applied, got {units.get('applied')}")
        expect(near(units.get("mmPerUnit", 0.0), 1.0, absolute=1e-12), "import (inch, no context): the raw text's millimetre base stands, 1 mm")
        expect(near(units.get("rawLengthMetres", 0.0), 0.001, absolute=1e-12), "import (inch, no context): the raw text says 0.001 m per unit")
        expect(units.get("length") == "millimetre", f"import (inch, no context): the raw text's first declaration is the base, got {units.get('length')}")
        expect(imported.get("modelLengthUnit", "") == "", "import (inch, no context): the model resolved no unit")
        expect(
            any("raw text's declaration" in notice for notice in imported.get("notices", [])),
            "import (inch, no context): a notice flags the raw text's declaration",
        )

        # 5. The refusals: no ISO header, and a header without a length unit and without the option.
        no_header = temp_path / "no-header.step"
        no_header.write_text("this is not a STEP file\n")
        code, refused = run("--import-step", str(no_header))
        expect(code == 2, f"refusal without the ISO header: exit 2, got {code}")
        expect(
            str(refused.get("status", "")).startswith("refused"),
            f"refusal without the ISO header: refused-*, got {refused.get('status')}",
        )
        no_units = temp_path / "no-units.step"
        no_units.write_text(
            "ISO-10303-21;\nHEADER;\n"
            "FILE_DESCRIPTION(('test'),'2;1');\n"
            "FILE_NAME('test','2026-01-01',('A'),('O'),('T'),('P'),'');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 2 10303 214 0 1 1 1 }'));\n"
            "ENDSEC;\nDATA;\nENDSEC;\n"
        )
        code, refused = run("--import-step", str(no_units))
        expect(code == 2, f"refusal without units and without the option: exit 2, got {code}")
        expect(
            str(refused.get("status", "")).startswith("refused"),
            f"refusal without units and without the option: refused-*, got {refused.get('status')}",
        )
        code, with_option = run("--import-step", str(no_units), "--step-length-unit", "mm")
        expect(
            with_option.get("units", {}).get("applied") == "option",
            f"import (no units, with the option): the option is applied, got {with_option.get('units', {}).get('applied')}",
        )

        # 6. R098.a2: the tessellated package import (--import-step --output PKG). The package
        # against the hand-computed geometry (E1: the box exact, the cylinder and the assembly
        # within the tessellation error), the unit policy and the triangle caps of section 6,
        # and the layout the application's decoder reads (SKIFCP01, the counts, the node tree).
        def import_package(name, *extra):
            return run("--import-step", str(temp_path / name), "--output", str(temp_path / (name + ".pkg")), *extra)

        def decode_package(path):
            """The package, checked the way the application's decoder checks it (the same
            invariants as src/io/ifc_package.cpp). Returns (problem, counts)."""
            data = Path(path).read_bytes()
            if len(data) < 24 or data[:8] != b"SKIFCP01":
                return "bad magic or length", {}
            (manifest_bytes, reserved,) = struct.unpack_from("<II", data, 8)
            (mesh_bytes,) = struct.unpack_from("<Q", data, 16)
            if reserved != 0:
                return "the reserved word", {}
            if manifest_bytes > 32 * 1024 * 1024 or mesh_bytes > 128 * 1024 * 1024:
                return "a section over its bound", {}
            if len(data) != 24 + manifest_bytes + mesh_bytes:
                return "the file is not exactly 24 + manifest + mesh bytes long", {}
            manifest = json.loads(data[24:24 + manifest_bytes])
            keys = {"format", "version", "meshSha256", "engine", "source", "units", "report", "meshes", "materials", "nodes"}
            unknown = set(manifest) - keys
            missing = keys - set(manifest)
            if unknown or missing:
                return f"manifest keys: unknown {sorted(unknown)}, missing {sorted(missing)}", {}
            if manifest["format"] != "sketchyup-ifc-package" or manifest["version"] != 1:
                return "the format or version is wrong", {}
            if not manifest["meshes"]:
                return "no meshes", {}
            total = 0
            count = {}
            offset = 0
            for mesh in manifest["meshes"]:
                n, t = mesh["vertices"], mesh["triangles"]
                e, colors = mesh["edges"], mesh["colors"]
                if n < 0 or t < 0 or e < 0 or n > 300000 or t > 100000:
                    return "a mesh over the package's bounds", count
                total += n * 24 + t * 12 + t * 4 + e * 8 + (t * 12 if colors else 0)
                base = 24 + manifest_bytes + offset
                for i in range(n * 3):
                    (v,) = struct.unpack_from("<d", data, base + i * 8)
                    if not math.isfinite(v) or abs(v) > 1e6:
                        return "a vertex outside the coordinate bound", count
                offset += n * 24
                for i in range(t * 3):
                    (index,) = struct.unpack_from("<I", data, base + n * 24 + i * 4)
                    if index >= n:
                        return "a triangle index out of range", count
                offset += t * 12
                for i in range(t):
                    (material,) = struct.unpack_from("<I", data, base + n * 24 + t * 12 + i * 4)
                    # R098.b: the step import ships the DefaultMaterial (and the styled
                    # colour), so an index is a reference into the manifest's materials.
                    if material != 0xFFFFFFFF and material >= len(manifest.get("materials", [])):
                        return "a material index outside the package's materials", count
                offset += t * 4 + e * 8 + (t * 12 if colors else 0)
            if total != mesh_bytes:
                return "the mesh chunk length disagrees with its meshes", count
            for i, node in enumerate(manifest["nodes"]):
                class_name = node["class"]
                if len(class_name) < 4 or not class_name.startswith("Ifc"):
                    return "a node class without the Ifc prefix", count
                if not class_name.isalnum():
                    return "a node class with a non-alphanumeric character", count
                transform = node["transform"]
                if len(transform) != 16 or any(not math.isfinite(v) for v in transform):
                    return "a node transform that is not 16 finite numbers", count
                if abs(transform[12]) > 1e-12 or abs(transform[13]) > 1e-12 or abs(transform[14]) > 1e-12 or abs(transform[15] - 1.0) > 1e-12:
                    return "a non-affine node transform", count
                if node["parent"] < -1 or node["parent"] >= i:
                    return "a node parent that is not an earlier node", count
            if not manifest["nodes"]:
                return "no nodes", count
            if manifest["nodes"][0]["class"] != "IfcStepAssembly":
                return "the root node is not an IfcStepAssembly", count
            count["nodes"] = len(manifest["nodes"])
            count["meshes"] = len(manifest["meshes"])
            return None, count

        def volume_and_area(path):
            """The package's combined mesh volume and area from the chunk (None when it cannot be read)."""
            data = Path(path).read_bytes()
            if len(data) < 24:
                return None
            (manifest_bytes,) = struct.unpack_from("<I", data, 8)
            manifest = json.loads(data[24:24 + manifest_bytes])
            volume, area, any_triangle = 0.0, 0.0, False
            offset = 0
            for mesh in manifest["meshes"]:
                n, t = mesh["vertices"], mesh["triangles"]
                base = 24 + manifest_bytes + offset
                vertices = [struct.unpack_from("<ddd", data, base + i * 24) for i in range(n)]
                for i in range(t):
                    a, b, c = struct.unpack_from("<III", data, base + n * 24 + i * 12)
                    p, q, r = vertices[a], vertices[b], vertices[c]
                    volume += (p[0] * (q[1] * r[2] - r[1] * q[2]) + p[1] * (r[0] * q[2] - q[0] * r[2])
                               + p[2] * (q[0] * r[1] - r[0] * q[1])) / 6.0
                    e1x, e1y, e1z = q[0] - p[0], q[1] - p[1], q[2] - p[2]
                    e2x, e2y, e2z = r[0] - p[0], r[1] - p[1], r[2] - p[2]
                    cx = e1y * e2z - e1z * e2y
                    cy = e1z * e2x - e1x * e2z
                    cz = e1x * e2y - e1y * e2x
                    area += 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
                    any_triangle = True
                offset += n * 24 + t * 12 + t * 4 + mesh["edges"] * 8 + (t * 12 if mesh["colors"] else 0)
            return (volume, area) if any_triangle else None

        # The box: exact (planar faces carry no tessellation error).
        code, box_result = import_package("sketchyup-step-box.step")
        expect(code == 0, f"package import (box) exited {code}")
        expect(box_result.get("status") == "ok", f"package import (box): ok, got {box_result.get('status')}")
        expect(box_result.get("meshes") == 1 and box_result.get("converted") == 1,
               f"package import (box): one converted mesh, got {box_result.get('meshes')}/{box_result.get('converted')}")
        expect(box_result.get("triangles") == 12, f"package import (box): twelve triangles, got {box_result.get('triangles')}")
        expect(box_result.get("omitted") == 0 and box_result.get("unsupported") == 0,
               "package import (box): nothing omitted or unsupported")
        problem, _ = decode_package(temp_path / "sketchyup-step-box.step.pkg")
        expect(problem is None, f"package decode (box): {problem}")
        geometry = volume_and_area(temp_path / "sketchyup-step-box.step.pkg")
        expect(geometry is not None, "package box: the geometry could be read")
        if geometry:
            volume, area = geometry
            expect(abs(volume - 0.01 * 0.02 * 0.03) <= 1e-12,
                 f"package box: the volume is the analytic value, got {volume}")
            expect(abs(area - 2.0 * (0.01 * 0.02 + 0.01 * 0.03 + 0.02 * 0.03)) <= 1e-12,
                   f"package box: the area is the analytic value, got {area}")

        # The cylinder and the assembly: within the tessellation error (E1: 1e-3 relative for
        # analytic primitives; the 1 mm deflection is far below the 10 mm radius, so a 5%
        # envelope is a generous bound on the mesh's chord error).
        code, cylinder_result = import_package("sketchyup-step-cylinder.step")
        expect(code == 0 and cylinder_result.get("status") == "ok", "package import (cylinder): ok")
        geometry = volume_and_area(temp_path / "sketchyup-step-cylinder.step.pkg")
        expect(geometry is not None, "package cylinder: the geometry could be read")
        if geometry:
            volume, area = geometry
            expect(abs(volume - 1.5708e-06) <= 0.05 * 1.5708e-06,
                 f"package cylinder: the volume is within the tessellation error, got {volume}")
            expect(abs(area - 0.000785398) <= 0.05 * 0.000785398,
                   f"package cylinder: the area is within the tessellation error, got {area}")
        code, assembly_result = import_package("sketchyup-step-assembly.step")
        expect(code == 0 and assembly_result.get("status") == "ok", "package import (assembly): ok")
        # R098.b: a group (an assembly) is always converted, so the converted count is the
        # two parts plus the root; the meshes are the two parts' own.
        expect(assembly_result.get("meshes") == 2 and assembly_result.get("converted") == 3,
               f"package import (assembly): the two parts and the root convert, got {assembly_result.get('meshes')}/{assembly_result.get('converted')}")
        expect(assembly_result.get("triangles") == 104, f"package import (assembly): 104 triangles, got {assembly_result.get('triangles')}")
        problem, _ = decode_package(temp_path / "sketchyup-step-assembly.step.pkg")
        expect(problem is None, f"package decode (assembly): {problem}")
        geometry = volume_and_area(temp_path / "sketchyup-step-assembly.step.pkg")
        expect(geometry is not None, "package assembly: the geometry could be read")
        if geometry:
            volume, area = geometry
            expect(abs(volume - (0.01 * 0.02 * 0.03 + 1.5708e-06)) <= 0.05 * (0.01 * 0.02 * 0.03 + 1.5708e-06),
                   f"package assembly: the combined volume is within the tessellation error, got {volume}")
            expect(abs(area - (0.0022 + 0.000785398)) <= 0.05 * (0.0022 + 0.000785398),
                   f"package assembly: the combined area is within the tessellation error, got {area}")

        # The unit policy (decision 4) held by the package import: the declared unit is
        # authoritative (a mismatching option does not rescale), a file with no unit and no
        # option refuses (and writes no package), and the option stands for a file that
        # declares nothing.
        code, with_option = import_package("sketchyup-step-box.step", "--step-length-unit", "in")
        expect(code == 0 and with_option.get("status") == "ok",
               "package import (declared units, with a mismatching option): the declared unit still wins")
        # The units the import applied ride the package's manifest (the report line is the package's counts).
        with_option_bytes = (temp_path / "sketchyup-step-box.step.pkg").read_bytes()
        (with_option_manifest_bytes,) = struct.unpack_from("<I", with_option_bytes, 8)
        with_option_manifest = json.loads(with_option_bytes[24:24 + with_option_manifest_bytes])
        expect(with_option_manifest.get("units", {}).get("applied") == "declared",
               f"package import (with an option): the declared unit is applied, got {with_option_manifest.get('units', {}).get('applied')}")
        no_units_shape = temp_path / "no-units-shape.step"
        box_text = (temp_path / "sketchyup-step-box.step").read_text()
        for snippet in (
            "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#349)) GLOBAL_UNIT_ASSIGNED_CONTEXT\n((#346,#347,#348)) ",
            "#346 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n",
            "#347 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\n",
            "#348 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );\n",
            "#349 = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#346,\n  'distance_accuracy_value','confusion accuracy');\n",
        ):
            box_text = box_text.replace(snippet, "")
        no_units_shape.write_text(box_text)
        code, refused = import_package("no-units-shape.step")
        expect(code == 3, f"package refusal (no units, no option): exit 3, got {code}")
        expect(refused.get("status") == "refused", f"package refusal (no units, no option): refused, got {refused.get('status')}")
        expect(not (temp_path / "no-units-shape.step.pkg").exists(), "package refusal: nothing was written")
        code, with_option = import_package("no-units-shape.step", "--step-length-unit", "mm")
        expect(code == 0 and with_option.get("status") == "ok", "package import (no units, with the option): ok")
        no_units_option_bytes = (temp_path / "no-units-shape.step.pkg").read_bytes()
        (no_units_option_manifest_bytes,) = struct.unpack_from("<I", no_units_option_bytes, 8)
        no_units_option_manifest = json.loads(no_units_option_bytes[24:24 + no_units_option_manifest_bytes])
        expect(no_units_option_manifest.get("units", {}).get("applied") == "option",
               f"package import (no units, with the option): the option is applied, got {no_units_option_manifest.get('units', {}).get('applied')}")

        # The caps (ADR 0172 section 6): a solid over its per-element cap is reported as not
        # imported (and a file nothing converts writes no package), and the total budget the
        # remaining solids report with the notice.
        code, capped = import_package("sketchyup-step-cylinder.step", "--step-max-triangles", "50")
        expect(code == 3, f"package import (the 92-triangle cylinder, per-element cap 50): exit 3, got {code}")
        expect(capped.get("status") == "refused", f"package import (capped): refused, got {capped.get('status')}")
        code, budget = import_package("sketchyup-step-assembly.step", "--step-max-total-triangles", "50")
        expect(code == 0, f"package import (the 104-triangle assembly, total cap 50) exited {code}")
        # R098.b: the root group converts at no triangle cost, so the converted count is
        # the box and the root; the cylinder is reported omitted.
        expect(budget.get("converted") == 2 and budget.get("omitted") == 1,
               f"package import (total cap 50): the box and the root convert, the cylinder is reported omitted, got {budget.get('converted')}/{budget.get('omitted')}")
        # The report the package carries (its manifest's report section) has the notice.
        budget_bytes = (temp_path / "sketchyup-step-assembly.step.pkg").read_bytes()
        (budget_manifest_bytes,) = struct.unpack_from("<I", budget_bytes, 8)
        budget_manifest = json.loads(budget_bytes[24:24 + budget_manifest_bytes])
        expect(
            any("budget" in notice.get("code", "").lower() for notice in budget_manifest.get("report", {}).get("notices", [])),
            f"package import (total cap 50): the report carries the budget notice, got {budget_manifest.get('report', {}).get('notices')}",
        )
        code, bad_deflection = import_package("sketchyup-step-box.step", "--step-deflection-mm", "0")
        expect(code == 3 and bad_deflection.get("status") == "refused", "package import (deflection 0): refused")

        # The metadata import is unchanged (R098.a): without --output the report stands, with
        # the units the import applies.
        code, metadata = run("--import-step", str(box))
        expect(code == 0 and metadata.get("status") == "ok", "metadata import (no --output): the report stands")
        expect("units" in metadata and "nodes" not in metadata, "metadata import: the report is metadata, not a package")

        # The committed mirror fixture (tests/fixtures/step/mirror-box.step, the corpus box
        # with its 3D points negated in x): the step subset's placements are proper
        # isometries, so the mirror-image solid reaches the kernel only as a valid B-rep;
        # the metadata sees the mirrored bounding box and the C++ test checks the package's
        # positive signed volume against the analytic values.
        mirror_fixture = Path(__file__).resolve().parent.parent / "tests/fixtures/step/mirror-box.step"
        if mirror_fixture.exists():
            code, mirror_metadata = run("--import-step", str(mirror_fixture))
            expect(code == 0 and mirror_metadata.get("status") == "ok",
                   f"metadata import (the mirrored box) exited {code}: {mirror_metadata.get('status')}")
            mirror_shapes = mirror_metadata.get("shapes", [])
            expect(len(mirror_shapes) == 1 and mirror_shapes[0].get("solids") == 1
                   and mirror_shapes[0].get("faces") == 6 if mirror_shapes else False,
                   f"metadata import (the mirrored box): one solid, six faces, got {mirror_shapes}")
            if mirror_shapes:
                mbox = mirror_shapes[0].get("boundingBoxMm", [])
                if len(mbox) == 6:
                    expect(near(mbox[0], -10.0, absolute=1e-6) and near(mbox[3], 0.0, absolute=1e-6),
                           f"metadata import (the mirrored box): the box spans x -10 to 0, got {mbox[0]}..{mbox[3]}")
                    expect(near(mbox[1], 0.0, absolute=1e-6) and near(mbox[4], 20.0, absolute=1e-6),
                           f"metadata import (the mirrored box): y 0 to 20, got {mbox[1]}..{mbox[4]}")
                    expect(near(mbox[2], 0.0, absolute=1e-6) and near(mbox[5], 30.0, absolute=1e-6),
                           f"metadata import (the mirrored box): z 0 to 30, got {mbox[2]}..{mbox[5]}")
    
        # R098.i (ADR 0172 section 5): --exclude-step leaves out one base of the tessellation (the value of its P at line).
        # The base is absent from the package and reported (a P excluded line, and an excluded location); the bases that
        # remain are the same bytes as in the run without the option; and a base the file does not have changes nothing.
        restart_fixture = Path(__file__).resolve().parent.parent / "tests/fixtures/step/sketchyup-step-assembly.step"
        if restart_fixture.exists():
            def mesh_blobs(path):
                data = Path(path).read_bytes()
                (manifest_bytes,) = struct.unpack_from("<I", data, 8)
                manifest = json.loads(data[24:24 + manifest_bytes])
                blobs = []
                offset = 24 + manifest_bytes
                for mesh in manifest["meshes"]:
                    n, t, e = mesh["vertices"], mesh["triangles"], mesh["edges"]
                    size = n * 24 + t * 12 + t * 4 + e * 8 + (t * 12 if mesh["colors"] else 0)
                    blobs.append(data[offset:offset + size])
                    offset += size
                return manifest, blobs

            base_pkg = temp_path / "exclude-base.pkg"
            code, base_result = run("--import-step", str(restart_fixture), "--output", str(base_pkg))
            expect(code == 0 and base_result.get("status") == "ok", "exclude: the run without the option is ok")
            excl_pkg = temp_path / "exclude-one.pkg"
            excluded_run = subprocess.run(
                [str(WORKER), "--import-step", str(restart_fixture), "--output", str(excl_pkg), "--exclude-step", "1"],
                capture_output=True, text=True, timeout=300)
            excluded_lines = excluded_run.stdout.splitlines()
            expect(excluded_run.returncode == 0, f"exclude: the run with --exclude-step 1 exited {excluded_run.returncode}")
            expect("P excluded 1" in [line.strip() for line in excluded_lines], "exclude: the helper reports P excluded 1")
            excluded_reports = [line for line in excluded_lines if line.startswith("R ")]
            excluded_result = json.loads(excluded_reports[-1][2:]) if excluded_reports else {}
            expect(excluded_result.get("status") == "ok", "exclude: the run with the option is ok")
            expect(excluded_result.get("converted") == base_result.get("converted", 0) - 1,
                   f"exclude: one body fewer converted, got {excluded_result.get('converted')}")
            expect(excluded_result.get("omitted") == base_result.get("omitted", 0) + 1,
                   f"exclude: one base omitted, got {excluded_result.get('omitted')}")
            expect(excluded_result.get("meshes") == base_result.get("meshes", 0) - 1,
                   f"exclude: one mesh fewer, got {excluded_result.get('meshes')}")
            base_manifest, base_blobs = mesh_blobs(base_pkg)
            excl_manifest, excl_blobs = mesh_blobs(excl_pkg)
            expect(len(excl_blobs) == len(base_blobs) - 1, "exclude: the left-out base's mesh is absent from the package")
            expect(excl_blobs == [blob for blob in base_blobs if blob in excl_blobs],
                   "exclude: the meshes that remain are the run without the option's, byte for byte and in order")
            expect(len(excl_manifest["nodes"]) < len(base_manifest["nodes"]), "exclude: the left-out base's nodes are absent")
            expect(excl_manifest["nodes"][0]["class"] == "IfcStepAssembly", "exclude: the root is still first")
            located = [loc for loc in excl_manifest["report"].get("locations", []) if loc.get("kind") == "excluded"]
            expect(len(located) == 1 and "left out after the helper stopped on it" in located[0].get("message", ""),
                   f"exclude: the left-out base is located as excluded, got {located}")
            unknown_pkg = temp_path / "exclude-unknown.pkg"
            code, unknown_result = run("--import-step", str(restart_fixture), "--output", str(unknown_pkg), "--exclude-step", "99")
            expect(code == 0 and unknown_result.get("status") == "ok", "exclude: a base the file does not have is ok")
            expect(Path(unknown_pkg).read_bytes() == Path(base_pkg).read_bytes(),
                   "exclude: a base the file does not have changes nothing (the same package bytes)")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"{len(failures)} check(s) failed")
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
