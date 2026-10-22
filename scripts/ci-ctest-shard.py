#!/usr/bin/env python3
"""Deal the CTest tests of a build over the fast tier's test jobs (ADR 0170).

  ci-ctest-shard.py BUILD_DIR SHARD SHARDS [EXCLUDE_REGEX]

Lists the build's tests (ctest --show-only=json-v1), drops those EXCLUDE_REGEX matches
(searched like ctest --exclude-regex), and deals the rest over SHARDS shards, longest first
to the least loaded shard, by the serial seconds in .github/ci-path-map.json
("test_seconds"; unlisted tests count 1 s). Every test lands in exactly one shard.
Shard 0 starts with 60 s of load for the repository checks it also runs. Prints shard
SHARD's tests (0-based) as an anchored --tests-regex, and writes CTest's cost
file for them, so `ctest --parallel` starts the longest first.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main(argv):
    if len(argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    build, shard, shards = argv[1], int(argv[2]), int(argv[3])
    exclude = re.compile(argv[4]) if len(argv) == 5 and argv[4] else None
    listing = json.loads(subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"],
                                        check=True, capture_output=True, text=True).stdout)
    names = [test["name"] for test in listing["tests"]]
    if exclude:
        names = [name for name in names if not exclude.search(name)]
    seconds = json.loads((ROOT / ".github" / "ci-path-map.json").read_text())["test_seconds"]
    cost = {name: float(seconds.get(name, 1)) for name in names}
    # Shard 1 (index 0) also runs mcp and the repository checks after CTest: about a minute.
    loads = [60.0] + [0.0] * (shards - 1)
    mine = []
    for name in sorted(names, key=lambda n: (-cost[n], n)):
        target = min(range(shards), key=lambda i: (loads[i], i))
        loads[target] += cost[name]
        if target == shard:
            mine.append(name)
    if not mine:
        print(f"shard {shard + 1}/{shards} has no tests", file=sys.stderr)
        return 1
    temporary = Path(build) / "Testing" / "Temporary"
    temporary.mkdir(parents=True, exist_ok=True)
    (temporary / "CTestCostData.txt").write_text(
        "".join(f"{name} 1 {cost[name]}\n" for name in mine) + "---\n")
    print(f"shard {shard + 1}/{shards}: {len(mine)} of {len(names)} tests, "
          f"~{loads[shard]:.0f} s serial (with any head start)", file=sys.stderr)
    print("^(" + "|".join(re.escape(name) for name in mine) + ")$")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
