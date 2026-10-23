"""Runs the buildingSMART ifc-gherkin-rules (the engine of the validation service) over one IFC file with behave in TESTING mode and
writes every scenario's status and every validation outcome to OUT as JSON lines.

    python -I run_gherkin.py REPO VENV_PYTHON FILE.ifc OUT

REPO is a checkout of https://github.com/buildingSMART/ifc-gherkin-rules with its submodule (git submodule update --init). See README.md.
"""
import base64, json, os, subprocess, sys, tempfile, collections
repo, venv_python, ifc = sys.argv[1], sys.argv[2], sys.argv[3]
fd, out = tempfile.mkstemp(suffix=".json")
os.close(fd)
proc = subprocess.run([venv_python, "-m", "behave", "--tags=-disabled", "--define", "input=" + os.path.abspath(ifc), "--define", "execution_mode=ExecutionMode.TESTING",
                       "--define", "max_outcomes_per_rule=0", "--define", "purepythonparser=False", "-f", "outcome_embedding_json", "-o", out],
                      cwd=repo, capture_output=True, text=True, timeout=1800)
try:
    log = json.load(open(out))
except Exception as e:
    print("behave output unreadable:", e, proc.stderr[-2000:], proc.stdout[-2000:])
    sys.exit(2)
counts = collections.Counter()
failures = []
for feature in log:
    for el in feature.get("elements", []):
        for key in ("protocol_errors", "caught_exceptions"):
            for blob in el.get(key, []) or []:
                data = json.loads(base64.b64decode(blob["data"]).decode())
                if data:
                    failures.append((feature["name"], key, str(data)[:300]))
        for blob in el.get("validation_outcomes", []) or []:
            for o in json.loads(base64.b64decode(blob["data"]).decode()):
                counts[(o.get("severity"), o.get("outcome_code"))] += 1
                if o.get("severity") in ("ERROR", "WARNING") or o.get("outcome_code") in ("E00001", "N00010", "W00010"):
                    failures.append((feature["name"], o.get("outcome_code"), str(o.get("observed"))[:200]))
print(ifc, "features:", len(log))
for k, v in sorted(counts.items(), key=lambda kv: str(kv[0])):
    print("  ", k, v)
for f in failures[:30]:
    print("  FINDING", f)
status = collections.Counter()
bad = []
for feature in log:
    for el in feature.get("elements", []):
        st = el.get("status")
        status[st] += 1
        if st not in ("passed", "skipped"):
            bad.append((feature["name"], el.get("name"), st))
print("scenario statuses:", dict(status))
for b in bad[:40]:
    print("  NOT PASSED", b)

with open(sys.argv[4], "w") as handle:
    for feature in log:
        for el in feature.get("elements", []):
            handle.write(json.dumps([feature["name"], el.get("name"), el.get("status")]) + "\n")
        for el in feature.get("elements", []):
            for blob in el.get("validation_outcomes", []) or []:
                for o in json.loads(base64.b64decode(blob["data"]).decode()):
                    handle.write(json.dumps(["OUTCOME", feature["name"], o.get("severity"), o.get("outcome_code"), str(o.get("observed"))[:160]]) + "\n")
