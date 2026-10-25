#!/usr/bin/env python3
"""R098.d: measures the STEP import budgets of ADR 0172 section 6 with generated files (opt-in; not a CTest test).

    python3 -I tests/step_budgets.py WORKER CLI OUTPUT_DIR [--tessellation-model PATH]

It writes two STEP files of exactly the budget sizes and times the helper on them, and checks the
application's refusal of a file one byte over the limit. It prints one JSON object:

  points-64m    1,000,000 unreferenced CARTESIAN_POINT entities in a file of 64 MiB (67,108,864 bytes): the
                helper's --import-step (parse and transfer). The budget is 3 s.
  points-128m   2,000,000 entities in a file of 128 MiB (134,217,728 bytes), the input limit: the same.
                The budget is 6 s.
  refuse-129m   a regular file of 128 MiB + 1 byte: the command line refuses it before the helper starts,
                with the limit named. The budget is a refusal with no document.
  tessellation  with --tessellation-model PATH: the whole import (the helper and the application's conversion)
                of a model of up to 1,000,000 triangles, with the command line and the helper's own time.
                The budget is 10 s. The model is not generated here: it is made with the OCCT writer (see
                the acceptance record, docs/verification/R098d-step-exchange.md, section 6).

The helper's time is wall-clock; the peak resident size is that of the command and its waited-for children.
"""
import json
from pathlib import Path
import os
import re
import subprocess
import sys
import time

WORKER, CLI, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
MODEL = sys.argv[sys.argv.index("--tessellation-model") + 1] if "--tessellation-model" in sys.argv else None
os.makedirs(OUT, exist_ok=True)

MIB = 1024 * 1024
HEADER_TEMPLATE = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('STEP budget fixture {pad}'),'2;1');
FILE_NAME('budget.step','2026-10-25T00:00:00',('SketchyUp'),('SketchyUp'),'SketchyUp R098.d','generated','');
FILE_SCHEMA(('AUTOMOTIVE_DESIGN {{ 1 2 10303 214 0 1 1 1 }}'));
ENDSEC;
DATA;
"""
FOOTER = "ENDSEC;\nEND-ISO-10303-21;\n"


def points_file(name, count, total_bytes):
    """A file of exactly total_bytes: the corpus box (a valid product with one solid, so the reader transfers a
    shape and reports success), then count unreferenced CARTESIAN_POINT entities that the reader must parse and
    does not transfer, then the trailer. The size is made exact with trailing zeros on the first coordinate of
    the points (a valid REAL): the reader rejects a header string longer than a few kilobytes, so the header is
    the corpus box's own."""
    global BOX_ENTITIES
    box = Path(__file__).resolve().parent / "fixtures" / "step" / "sketchyup-step-box.step"
    text = box.read_text(encoding="ascii")
    head, _, data = text.partition("DATA;\n")
    data = data[: data.rindex("ENDSEC;")]
    BOX_ENTITIES = len(re.findall(r"^#\d+ = ", data, re.M))
    trailer = "ENDSEC;\nEND-ISO-10303-21;\n"
    fixed = len(head) + len("DATA;\n") + len(data) + len(trailer)
    base = [len("#%d=CARTESIAN_POINT('',(%d.,%d.,%d.));\n" % (100000 + i, i % 1000, (i // 1000) % 1000, i % 7))
            for i in range(count)]
    extra = total_bytes - fixed - sum(base)
    if extra < 0:
        raise SystemExit(f"{name}: {count} entities do not fit in {total_bytes} bytes")
    per, remainder = divmod(extra, count)
    lines = []
    for i in range(count):
        zeros = "0" * (per + (1 if i < remainder else 0))
        lines.append("#%d=CARTESIAN_POINT('',(%d.%s,%d.,%d.));\n" % (100000 + i, i % 1000, zeros, (i // 1000) % 1000, i % 7))
    path = os.path.join(OUT, name + ".step")
    with open(path, "wb") as handle:
        handle.write((head + "DATA;\n" + data + "".join(lines) + trailer).encode("ascii"))
    assert os.path.getsize(path) == total_bytes, (name, os.path.getsize(path), total_bytes)
    return path


def timed(command):
    """Runs a command; returns (returncode, stdout, wall seconds, peak RSS in MiB of the command and its children)."""
    start = time.time()
    with open(os.path.join(OUT, "stderr.txt"), "w") as errors:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors, text=True,
                                env={**os.environ, "LC_ALL": "C.UTF-8"})
        stdout = proc.stdout.read()
        _, status, usage = os.wait4(proc.pid, 0)
    proc.stdout.close()
    code = os.waitstatus_to_exitcode(status)
    return code, stdout, round(time.time() - start, 2), round(usage.ru_maxrss / 1024)


def last_result(stdout):
    lines = [line for line in stdout.splitlines() if line.startswith("R ")]
    return json.loads(lines[-1][2:]) if lines else json.loads(stdout.strip().splitlines()[-1])


results = {}
for name, count, size in (("points-64m", 1000000, 64 * MIB), ("points-128m", 1999000, 128 * MIB)):
    path = points_file(name, count, size)
    code, stdout, seconds, rss = timed([WORKER, "--import-step", path])
    report = last_result(stdout)
    results[name] = {"bytes": size, "entities": count + BOX_ENTITIES, "exit": code, "status": report.get("status"),
                     "seconds": seconds, "peakRssMiB": rss}
    os.remove(path)

over = os.path.join(OUT, "refuse-129m.step")
with open(over, "wb") as handle:
    handle.write(HEADER_TEMPLATE.format(pad="").encode("ascii"))
    handle.truncate(128 * MIB + 1)
code, stdout, seconds, rss = timed([CLI, "--import-step", over, "--step-worker", WORKER])
with open(os.path.join(OUT, "stderr.txt")) as errors:
    stderr = errors.read()
results["refuse-129m"] = {"bytes": 128 * MIB + 1, "exit": code, "seconds": seconds, "peakRssMiB": rss,
                          "stdout": stdout.strip()[:300], "stderr": stderr.strip()[:300]}
os.remove(over)

if MODEL:
    code, stdout, seconds, rss = timed([CLI, "--import-step", MODEL, "--output",
                                        os.path.join(OUT, "tessellation.sketchyup"), "--step-worker", WORKER,
                                        "--step-deadline", "600"])
    entry = {"bytes": os.path.getsize(MODEL), "exit": code, "seconds": seconds, "peakRssMiB": rss,
             "reportBytes": len(stdout.encode("utf-8"))}
    try:
        report = json.loads(stdout).get("importReport", {})
        entry["report"] = {k: report[k] for k in ("accounting", "elements", "geometry", "engine") if k in report}
    except ValueError as error:
        entry["error"] = str(error)[:200]
    # The helper alone on the same model (the tessellation and the package, no application conversion).
    helper_code, helper_stdout, helper_seconds, helper_rss = timed(
        [WORKER, "--import-step", MODEL, "--output", os.path.join(OUT, "tessellation.pkg")])
    entry["helperOnly"] = {"exit": helper_code, "seconds": helper_seconds, "peakRssMiB": helper_rss,
                           "result": last_result(helper_stdout) if helper_code == 0 else None}
    results["tessellation"] = entry

print(json.dumps(results, indent=1, sort_keys=True))
