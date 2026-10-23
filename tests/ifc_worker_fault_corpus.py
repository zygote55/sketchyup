#!/usr/bin/env python3
"""R097.b (ADR 0171 check J2): the certification files, mutated, through the helper's dry run in a child
process. Every mutation must end in one of the helper's defined outcomes, and the test process must
survive all of them:

    ok       exit 0 and a result line {"status": "ok"}
    refused  exit 3 and a result line {"status": "refused", ...}
    error    exit 1 (a library exception, reported on stderr)
    crash    the child died on a signal (SIGSEGV, SIGABRT, an address-space or CPU limit)
    timeout  the child ran past the deadline and was killed

`crash` and `timeout` are what the process boundary exists for: the application restarts the helper with
the element at fault left out, or fails the import, and the document is never touched. They are counted and
listed (the mutation's seed, so it can be replayed), and they fail the test only above the bound below.

    python3 -I tests/ifc_worker_fault_corpus.py WORKER FIXTURE_DIR [MUTATIONS_PER_FILE] [JOBS]
"""
import hashlib
import json
import os
import random
import signal
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

WORKER, FIXTURES = sys.argv[1], sys.argv[2]
PER_FILE = int(sys.argv[3]) if len(sys.argv) > 3 else 512
JOBS = int(sys.argv[4]) if len(sys.argv) > 4 else 2
DEADLINE = 40
# The sanitizer build maps huge shadow memory, so no address-space limit there (CTest sets this).
LIMIT_AS = [] if os.environ.get("IFC_NO_ADDRESS_LIMIT") else ["--rlimit-as", str(4 << 30)]
CHILD_ENV = {"LC_ALL": "C.UTF-8", **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}
FILES = ["bsi-ifc2x3-arch.ifc", "bsi-ifc4-arch.ifc", "bsi-ifc43-arch.ifc", "bsi-ifc4-landscaping.ifc", "bsi-ifc4-road.ifc",
         "rv-wall-with-opening-and-window.ifc", "rv-basin-tessellation.ifc", "rv-column-straight-rectangle-tessellation.ifc",
         "rv-tessellated-item.ifc", "rv-tessellation-with-individual-colors.ifc"]
TMP = tempfile.mkdtemp(prefix="ifc-fault-corpus-")


def mutate(data, rng):
    """One to three edits of the file's bytes: flips, deletions, insertions, truncation, line duplication
    and swaps, and the replacement of a number or a reference."""
    data = bytearray(data)
    for _ in range(rng.randint(1, 3)):
        kind = rng.choice(["flip", "flip", "delete", "insert", "truncate", "dupline", "swapline", "number", "reference", "string", "keyword"])
        at = rng.randrange(len(data))
        if kind == "flip":
            data[at] ^= 1 << rng.randrange(8)
        elif kind == "delete":
            del data[at:at + rng.randint(1, 20)]
        elif kind == "insert":
            data[at:at] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 8)))
        elif kind == "truncate":
            del data[at:]
            data += b"\nENDSEC;\nEND-ISO-10303-21;\n" if rng.random() < 0.5 else b""
        elif kind in ("dupline", "swapline"):
            lines = data.split(b"\n")
            i = rng.randrange(len(lines))
            if kind == "dupline":
                lines.insert(i, lines[i])
            else:
                j = rng.randrange(len(lines))
                lines[i], lines[j] = lines[j], lines[i]
            data = bytearray(b"\n".join(lines))
        elif kind == "number":
            text = bytes(data)
            starts = [i for i in range(len(text) - 1) if text[i:i + 1].isdigit() and not text[i - 1:i].isdigit()]
            if starts:
                i = rng.choice(starts)
                data[i:i] = rng.choice([b"1e308", b"-1e308", b"9999999999", b"0.", b"nan", b"-0"])
        elif kind == "reference":
            text = bytes(data)
            hashes = [i for i in range(len(text)) if text[i:i + 1] == b"#"]
            if hashes:
                i = rng.choice(hashes)
                data[i + 1:i + 1] = rng.choice([b"9", b"0", b"999999", b"1"])
        elif kind == "string":
            i = data.find(b"'", at)
            if i >= 0:
                data[i:i] = rng.choice([b"\\X2\\D800\\X0\\", b"\\S\\", b"\\X\\ZZ", b"\\\\", b"'", b"\\X4\\110000\\X0\\"])
        elif kind == "keyword":
            i = data.find(b"IFC", at)
            if i >= 0:
                data[i:i + 3] = rng.choice([b"IFX", b"ifc", b"   "])
    return bytes(data)


def classify(code, stdout):
    result = None
    for line in stdout.splitlines():
        if line.startswith("R "):
            try:
                result = json.loads(line[2:])
            except ValueError:
                pass
    if code == 0 and result and result.get("status") == "ok":
        return "ok"
    if code == 3 and result and result.get("status") == "refused":
        return "refused"
    if code == 1:
        return "error"
    if code < 0:
        return "crash"
    return "unclassified"


def one(job):
    name, index = job
    seed = int(hashlib.sha256(("%s:%d" % (name, index)).encode()).hexdigest()[:12], 16)
    rng = random.Random(seed)
    with open(os.path.join(FIXTURES, name), "rb") as f:
        original = f.read()
    mutant = mutate(original, rng)
    path = os.path.join(TMP, "%s.%d.ifc" % (name, index))
    with open(path, "wb") as f:
        f.write(mutant)
    try:
        proc = subprocess.run([WORKER, "--import", path, "--dry-run", "--threads", "1", *LIMIT_AS, "--rlimit-cpu", str(DEADLINE * 4),
                               "--rlimit-fsize", str(256 << 20), "--max-entities", "2000000"],
                              capture_output=True, text=True, timeout=DEADLINE, env=CHILD_ENV, errors="replace")
        outcome = classify(proc.returncode, proc.stdout)
        detail = proc.stderr.strip()[-160:]
    except subprocess.TimeoutExpired:
        outcome, detail = "timeout", ""
    os.remove(path)
    return name, index, seed, outcome, detail


jobs = [(name, i) for name in FILES for i in range(PER_FILE)]
counts = {}
findings = []
with ThreadPoolExecutor(max_workers=JOBS) as pool:
    for name, index, seed, outcome, detail in pool.map(one, jobs):
        counts[outcome] = counts.get(outcome, 0) + 1
        if outcome in ("crash", "timeout", "unclassified"):
            findings.append({"file": name, "index": index, "seed": seed, "outcome": outcome, "stderr": detail})
total = len(jobs)
print(json.dumps({"mutations": total, "perFile": PER_FILE, "outcomes": counts, "findings": findings[:20]}, indent=1, sort_keys=True))
bad = counts.get("unclassified", 0)
# The bound: the process boundary contains crashes and hangs, but a corpus where more than 2% of the mutations
# bring the helper down would mean the library is not usable on damaged input at all.
if bad or counts.get("crash", 0) + counts.get("timeout", 0) > 0.02 * total:
    print("fault corpus: too many unclassified, crashed or timed-out runs")
    sys.exit(1)
print("ifc_worker_fault_corpus ok")
