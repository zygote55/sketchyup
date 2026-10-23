#!/usr/bin/env python3
"""R097.b: measures the budgets of ADR 0171 section 6.4 with generated files (opt-in; not a CTest test).

    python3 -I tests/ifc_budgets.py WORKER CLI OUTPUT_DIR [--keep]

It writes four IFC4 files and times the helper and the command line on them, and prints one JSON object:

  parse-1m      1,000,000 unreferenced IfcCartesianPoint instances (about 56 MB): the helper's dry run
  parse-2m      2,000,000 instances (about 114 MB): the dry run, and the entity limit with the count named
  ico-5k        5,000 tessellated icosahedra in one storey (100,000 triangles that do not merge, 60,000
                vertices, 5,004 bodies: the document limits' "100,000-face, 10,000-body" model): the whole
                import by the command line
  walls-20k     20,000 extruded rectangular walls: the same, which goes past the document limits (10,000
                bodies) and is imported partially with the rest reported
"""
import json
import os
import resource
import subprocess
import sys
import time

WORKER, CLI, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)

HEADER = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('ViewDefinition [ReferenceView_V1.2]'),'2;1');
FILE_NAME('budget.ifc','2026-10-22T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R097.b','generated','');
FILE_SCHEMA(('IFC4'));
ENDSEC;
DATA;
"""
SPINE = """#1=IFCPROJECT('0000000000000000000001',$,'Budget',$,$,$,$,(#6),#2);
#2=IFCUNITASSIGNMENT((#3,#4,#5));
#3=IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.);
#4=IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.);
#5=IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.);
#6=IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#8,$);
#7=IFCGEOMETRICREPRESENTATIONSUBCONTEXT('Body','Model',*,*,*,*,#6,$,.MODEL_VIEW.,$);
#8=IFCAXIS2PLACEMENT3D(#9,$,$);
#9=IFCCARTESIANPOINT((0.,0.,0.));
#10=IFCSITE('0000000000000000000002',$,'Site',$,$,#11,$,$,.ELEMENT.,$,$,$,$,$);
#11=IFCLOCALPLACEMENT($,#8);
#12=IFCBUILDING('0000000000000000000003',$,'Building',$,$,#13,$,$,.ELEMENT.,$,$,$);
#13=IFCLOCALPLACEMENT(#11,#8);
#14=IFCBUILDINGSTOREY('0000000000000000000004',$,'Level 1',$,$,#15,$,$,.ELEMENT.,0.);
#15=IFCLOCALPLACEMENT(#13,#8);
#16=IFCRELAGGREGATES('0000000000000000000005',$,$,$,#1,(#10));
#17=IFCRELAGGREGATES('0000000000000000000006',$,$,$,#10,(#12));
#18=IFCRELAGGREGATES('0000000000000000000007',$,$,$,#12,(#14));
#19=IFCDIRECTION((0.,0.,1.));
"""
FOOT = "#20=IFCAXIS2PLACEMENT2D(#9,$);\n"


def write(name, builder):
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        builder(f)
    return path


def points(count):
    def build(f):
        f.write(HEADER + SPINE)
        for i in range(count - 19):
            f.write("#%d=IFCCARTESIANPOINT((%d.,%d.,%d.));\n" % (100 + i, i % 1000, (i // 1000) % 1000, i // 1000000))
        f.write("ENDSEC;\nEND-ISO-10303-21;\n")
    return build


def icosahedra(count):
    phi = (1 + 5 ** 0.5) / 2
    scale = 200.0
    v = [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0), (0, -1, phi), (0, 1, phi), (0, -1, -phi), (0, 1, -phi),
         (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]
    t = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    points = ",".join("(%s)" % ",".join("%.6f" % (c * scale) for c in p) for p in v)
    faces = ",".join("(%d,%d,%d)" % (a + 1, b + 1, c + 1) for a, b, c in t)

    def build(f):
        f.write(HEADER + SPINE)
        refs = []
        for i in range(count):
            base = 100 + i * 8
            x, y = (i % 100) * 1000, (i // 100) * 1000
            f.write("#%d=IFCCARTESIANPOINTLIST3D((%s));\n#%d=IFCTRIANGULATEDFACESET(#%d,$,.T.,(%s),$);\n" % (base, points, base + 1, base, faces))
            f.write("#%d=IFCSHAPEREPRESENTATION(#7,'Body','Tessellation',(#%d));\n#%d=IFCPRODUCTDEFINITIONSHAPE($,$,(#%d));\n" % (base + 2, base + 1, base + 3, base + 2))
            f.write("#%d=IFCCARTESIANPOINT((%d.,%d.,0.));\n#%d=IFCAXIS2PLACEMENT3D(#%d,$,$);\n#%d=IFCLOCALPLACEMENT(#15,#%d);\n"
                    % (base + 4, x, y, base + 5, base + 4, base + 6, base + 5))
            f.write("#%d=IFCBUILDINGELEMENTPROXY('%022d',$,'ico %d',$,$,#%d,#%d,$,$);\n" % (base + 7, 1000 + i, i, base + 6, base + 3))
            refs.append("#%d" % (base + 7))
        f.write("#34=IFCRELCONTAINEDINSPATIALSTRUCTURE('0000000000000000000008',$,$,$,(%s),#14);\n" % ",".join(refs))
        f.write("ENDSEC;\nEND-ISO-10303-21;\n")
    return build


def walls(count):
    def build(f):
        f.write(HEADER + SPINE + FOOT)
        f.write("#30=IFCRECTANGLEPROFILEDEF(.AREA.,$,#20,1000.,200.);\n")
        profile = 30
        refs = []
        for i in range(count):
            base = 100 + i * 7
            x, y = (i % 200) * 3000, (i // 200) * 3000
            # Each wall has its own extrusion and shape representation, so the library shares nothing between them.
            f.write("#%d=IFCCARTESIANPOINT((%d.,%d.,0.));\n#%d=IFCAXIS2PLACEMENT3D(#%d,$,$);\n#%d=IFCLOCALPLACEMENT(#15,#%d);\n"
                    % (base, x, y, base + 1, base, base + 2, base + 1))
            f.write("#%d=IFCEXTRUDEDAREASOLID(#%d,#8,#19,2500.);\n#%d=IFCSHAPEREPRESENTATION(#7,'Body','SweptSolid',(#%d));\n"
                    "#%d=IFCPRODUCTDEFINITIONSHAPE($,$,(#%d));\n" % (base + 3, profile, base + 4, base + 3, base + 5, base + 4))
            f.write("#%d=IFCWALL('%022d',$,'wall %d',$,$,#%d,#%d,$,$);\n" % (base + 6, 1000 + i, i, base + 2, base + 5))
            refs.append("#%d" % (base + 6))
        f.write("#34=IFCRELCONTAINEDINSPATIALSTRUCTURE('0000000000000000000008',$,$,$,(%s),#14);\n" % ",".join(refs))
        f.write("ENDSEC;\nEND-ISO-10303-21;\n")
    return build


def timed(command, timeout=900):
    """Runs a command; returns (process-like result, wall seconds, peak RSS of the command and its children in MiB,
    CPU seconds of the command and its children)."""
    start = time.time()
    with open(os.path.join(OUT, "stderr.txt"), "w") as errors:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors, text=True, env={"LC_ALL": "C.UTF-8"})
        stdout = proc.stdout.read()
        _, status, usage = os.wait4(proc.pid, 0)
    proc.returncode = os.waitstatus_to_exitcode(status)
    proc.stdout.close()
    result = subprocess.CompletedProcess(command, proc.returncode, stdout, open(os.path.join(OUT, "stderr.txt")).read())
    return result, round(time.time() - start, 2), round(usage.ru_maxrss / 1024), round(usage.ru_utime + usage.ru_stime, 2)


results = {}
for name, builder in (("parse-1m", points(1000000)), ("parse-2m", points(2000000))):
    path = write(name + ".ifc", builder)
    proc, seconds, rss, cpu = timed([WORKER, "--import", path, "--dry-run", "--threads", "1"])
    results[name] = {"bytes": os.path.getsize(path), "seconds": seconds, "peakRssMiB": rss, "cpuSeconds": cpu,
                     "result": [l for l in proc.stdout.splitlines() if l.startswith("R ")][-1:]}
    if name == "parse-2m":
        proc, seconds, rss, cpu = timed([WORKER, "--import", path, "--dry-run", "--max-entities", "1999999"])
        results[name]["limitRefusal"] = {"seconds": seconds, "result": [l for l in proc.stdout.splitlines() if l.startswith("R ")][-1:]}
    os.remove(path)
for name, builder in (("ico-5k", icosahedra(5000)), ("walls-20k", walls(20000))):
    path = write(name + ".ifc", builder)
    out = os.path.join(OUT, name + ".sketchyup")
    if os.path.exists(out):
        os.remove(out)
    proc, seconds, rss, cpu = timed([CLI, "--import-ifc", path, "--ifc-worker", WORKER, "--output", out, "--ifc-deadline", "600"])
    entry = {"bytes": os.path.getsize(path), "seconds": seconds, "peakRssMiB": rss, "cpuSeconds": cpu, "exit": proc.returncode}
    # The helper alone (the geometry iteration, no package), on four threads and on one.
    for threads in (4, 1):
        helper, helper_seconds, helper_rss, helper_cpu = timed([WORKER, "--import", path, "--dry-run", "--threads", str(threads)])
        entry["helperDryRun%dThreads" % threads] = {"seconds": helper_seconds, "peakRssMiB": helper_rss, "cpuSeconds": helper_cpu}
    try:
        report = json.loads(proc.stdout)["importReport"]
        entry["geometry"] = {k: report["geometry"][k] for k in ("bodies", "faces", "vertices", "sourceTriangles", "mergedFaces", "tasks")}
        entry["notImported"] = report["elements"]["notImported"]
        entry["nativeBytes"] = os.path.getsize(out)
    except Exception as error:  # noqa: BLE001
        entry["error"] = str(error)[:200] + proc.stderr[-300:]
    results[name] = entry
    os.remove(path)
print(json.dumps(results, indent=1, sort_keys=True))
