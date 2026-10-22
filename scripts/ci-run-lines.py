#!/usr/bin/env python3
"""Run the command lines of one workflow step in parallel (ADR 0170).

  ci-run-lines.py run [--jobs N] [--shard K/M] WORKFLOW JOB STEP
      Run every command line of the step (or, with --shard, lines K, K+M, K+2M, ...
      counting from 1), N at a time, and fail if any fails.
  ci-run-lines.py targets WORKFLOW JOB STEP
      Print the build/dev executables the step's lines run, for `cmake --build --target`.

The fast tier runs the full tier's "Native tests: X11 1x" lines this way, so both tiers
read the same list. Each line is one self-contained shell command. It gets the step's
environment, its own HOME, XDG and temporary directories (tests keep settings and
recovery files there), and `xvfb-run -a` searches for a free display from a number of
its own, so parallel servers never race for the same display. Output is printed per
line when the line finishes. The longest deadline (scripts/test-deadline.sh) starts first.
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile
import time

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def step_lines(workflow, job_id, step_name):
    with open(workflow, encoding="utf-8") as handle:
        job = yaml.safe_load(handle)["jobs"][job_id]
    for step in job.get("steps", []):
        if step.get("name") == step_name:
            env = {**(job.get("env") or {}), **(step.get("env") or {})}
            lines = [line.strip() for line in str(step["run"]).splitlines()]
            return env, [line for line in lines if line and not line.startswith("#")]
    raise SystemExit(f"no step '{step_name}' in job '{job_id}' of {workflow}")


def deadline(line):
    match = re.search(r"test-deadline\.sh (\d+)", line)
    return int(match.group(1)) if match else 0


def run_line(index, line, env, slot_base):
    scratch = tempfile.mkdtemp(prefix=f"ci-line-{index}-")
    line_env = dict(os.environ)
    line_env.update({key: str(value) for key, value in env.items()})
    for name in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
                 "TMPDIR"):
        path = os.path.join(scratch, name.lower())
        os.makedirs(path)
        line_env[name] = path
    runtime = os.path.join(scratch, "runtime")
    os.makedirs(runtime, mode=0o700)
    line_env["XDG_RUNTIME_DIR"] = runtime
    command = line.replace("xvfb-run -a ", f"xvfb-run -a -n {slot_base} ", 1)
    started = time.monotonic()
    result = subprocess.run(["bash", "-c", command], cwd=ROOT, env=line_env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            errors="replace")
    return index, line, result.returncode, time.monotonic() - started, result.stdout


def run(args):
    env, lines = step_lines(args.workflow, args.job, args.step)
    shard, shards = (int(part) for part in args.shard.split("/"))
    if not 1 <= shard <= shards:
        raise SystemExit(f"bad --shard {args.shard}")
    mine = [i for i in range(len(lines)) if i % shards == shard - 1]
    order = sorted(mine, key=lambda i: -deadline(lines[i]))
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        # Display numbers 100, 120, ... : one search range per submitted line.
        futures = [pool.submit(run_line, i, lines[i], env, 100 + 20 * n)
                   for n, i in enumerate(order)]
        for future in concurrent.futures.as_completed(futures):
            index, line, status, seconds, output = future.result()
            verdict = "ok" if status == 0 else f"FAILED ({status})"
            print(f"::group::[{index + 1}/{len(lines)}] {verdict} {seconds:.0f}s: {line}")
            print(output, end="" if output.endswith("\n") else "\n")
            print("::endgroup::", flush=True)
            if status != 0:
                failures.append((index, line, status))
    for index, line, status in sorted(failures):
        print(f"::error::line {index + 1} exited {status}: {line}")
    print(f"{len(mine) - len(failures)} of {len(mine)} lines passed "
          f"(shard {shard}/{shards} of {len(lines)} lines).")
    return 1 if failures else 0


def targets(args):
    _, lines = step_lines(args.workflow, args.job, args.step)
    names = sorted({match for line in lines for match in re.findall(r"build/dev/([A-Za-z0-9_-]+)", line)})
    print(" ".join(names))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("run", "targets"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--shard", default="1/1")
    parser.add_argument("workflow")
    parser.add_argument("job")
    parser.add_argument("step")
    args = parser.parse_args()
    return run(args) if args.command == "run" else targets(args)


if __name__ == "__main__":
    sys.exit(main())
