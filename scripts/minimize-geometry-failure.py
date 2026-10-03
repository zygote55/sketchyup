#!/usr/bin/env python3
"""Find the shortest failing prefix of a deterministic geometry edit sequence."""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--seed", type=int, required=True)
parser.add_argument("--steps", type=int, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if not 0 <= args.seed < 2**32 or not 1 <= args.steps <= 10000:
    parser.error("seed must be uint32 and steps must be 1..10000")
binary = str(args.binary.resolve(strict=True))


def run(steps):
    command = [binary, "--seed", str(args.seed), "--steps", str(steps)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=240)
    return command, result


command, result = run(args.steps)
if result.returncode == 0:
    parser.error("the supplied seed/prefix passes; no failure to minimize")
low, high = 1, args.steps
while low < high:
    middle = (low + high) // 2
    _, probe = run(middle)
    if probe.returncode:
        high = middle
    else:
        low = middle + 1
command, result = run(low)
if result.returncode == 0:
    parser.error("failure was not deterministic; retain the original log for investigation")
args.output.write_text(json.dumps({"seed": args.seed, "steps": low, "command": command,
                                   "exitCode": result.returncode, "stdout": result.stdout,
                                   "stderr": result.stderr}, indent=2) + "\n")
print(f"Shortest failing prefix: {low} steps; evidence: {args.output}")
