#!/usr/bin/env python3
"""The STEP probe and import of R098.a against the worker's own writer (ADR 0172).

    python3 -I tests/ifc_step_worker_tests.py WORKER

The corpus is the three self-generated files of ADR 0172 section 7 (a box 10 x 20 x 30, a
cylinder of radius 5 and height 20, an assembly of the two, all written in millimetres by
the basic writer). The test writes the corpus with --write-step and checks it back through
--probe-step and --import-step: the schema (AP214 as written, AP242 for the option), the
declared units from the raw text, the hand-computed geometry, the writer/reader round trip
(sections 1 and 2) and the unit policy of decision 4 (the declared unit is authoritative,
the option only when nothing is declared, refusal when neither is there). The OCCT the
worker ran on is pinned with the IfcOpenShell prefix (ADR 0171), so a change in this output
is a design review, not a silent change.
"""
import json
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
    # The OCCT transfer statistics share stdout; the report is the last line that opens with "{".
    lines = [line for line in result.stdout.splitlines() if line.lstrip().startswith("{")]
    payload = json.loads(lines[-1]) if lines else {}
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
        # 1. The self-generated corpus: three files, all AP214, all deterministic.
        code, corpus = run("--write-step", str(temp_path))
        expect(code == 0, f"--write-step exited {code}")
        files = corpus.get("files", [])
        expect(len(files) == 3, f"write-step writes three files, got {len(files)}")
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

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"{len(failures)} check(s) failed")
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
