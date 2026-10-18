#!/usr/bin/env python3
"""Inventory of the test command lines in a GitHub Actions workflow.

Splitting a CI job into parallel jobs must not drop, duplicate or alter a test
line. This script lists every command line of every `run:` step, except the
plumbing steps named in PLUMBING_STEPS (package installation and cache, toolchain record,
compiler cache, build-tree transfer, and this check itself), and tags each line
with the conditions it runs under: the container image and options and the
job- and step-level environment (minus compiler-cache variables, which change
how objects are produced but not what is tested). Comments and blank lines are
ignored; repeated lines are counted.

  ci-line-inventory.py extract WORKFLOW            print the inventory
  ci-line-inventory.py check WORKFLOW INVENTORY    fail unless WORKFLOW's
                                                   inventory equals INVENTORY
  ci-line-inventory.py compare OLD NEW             fail unless two workflows
                                                   have the same inventory
"""
import collections
import re
import sys

import yaml

# Steps that set up a job rather than test anything. They are repeated per job,
# so counting them would make every split look like a change.
PLUMBING_STEPS = re.compile(
    r"^(Install .*dependencies|Record toolchain|Check the test-line inventory"
    r"|Prepare the compiler cache|Compiler cache statistics|Prepare the package cache"
    r"|Pack the development build tree|Unpack the development build tree)$"
)
# Environment that only affects how objects are compiled or cached.
IGNORED_ENV = re.compile(r"^(CCACHE_.*|CMAKE_C_COMPILER_LAUNCHER|CMAKE_CXX_COMPILER_LAUNCHER)$")


def container_context(job):
    container = job.get("container")
    if container is None:
        return "host"
    if isinstance(container, str):
        container = {"image": container}
    image = container.get("image", "")
    digest = image.rsplit("@", 1)[-1] if "@" in image else image
    options = container.get("options", "")
    return f"image={digest}" + (f" options={options}" if options else "")


def env_context(*envs):
    merged = {}
    for env in envs:
        merged.update(env or {})
    return " ".join(
        f"{key}={merged[key]}" for key in sorted(merged) if not IGNORED_ENV.match(key)
    )


def inventory(path):
    with open(path, encoding="utf-8") as handle:
        workflow = yaml.safe_load(handle)
    lines = collections.Counter()
    for job_id, job in workflow["jobs"].items():
        for index, step in enumerate(job.get("steps", [])):
            if "run" not in step:
                continue
            name = step.get("name", f"{job_id} step {index + 1}")
            if PLUMBING_STEPS.match(name):
                continue
            context = f"[{container_context(job)}; env: {env_context(job.get('env'), step.get('env'))}]"
            for raw in str(step["run"]).splitlines():
                line = raw.strip()
                if line and not line.startswith("#"):
                    lines[f"{context} {line}"] += 1
    return lines


def render(lines):
    return "".join(f"{line}\n" for line in sorted(lines.elements()))


def report(expected, actual, expected_name, actual_name):
    missing = expected - actual
    extra = actual - expected
    for line in sorted(missing.elements()):
        print(f"missing from {actual_name}: {line}", file=sys.stderr)
    for line in sorted(extra.elements()):
        print(f"not in {expected_name}: {line}", file=sys.stderr)
    total = sum(actual.values())
    if missing or extra:
        print(f"Inventory mismatch: {sum(missing.values())} missing, "
              f"{sum(extra.values())} extra.", file=sys.stderr)
        return 1
    print(f"Inventory matches: {total} lines ({len(actual)} distinct).")
    return 0


def read_inventory(path):
    with open(path, encoding="utf-8") as handle:
        return collections.Counter(line.rstrip("\n") for line in handle if line.strip())


def main(argv):
    if len(argv) == 3 and argv[1] == "extract":
        sys.stdout.write(render(inventory(argv[2])))
        return 0
    if len(argv) == 4 and argv[1] == "check":
        return report(read_inventory(argv[3]), inventory(argv[2]), argv[3], argv[2])
    if len(argv) == 4 and argv[1] == "compare":
        return report(inventory(argv[2]), inventory(argv[3]), argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
