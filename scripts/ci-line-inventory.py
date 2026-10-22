#!/usr/bin/env python3
"""Inventory of the test command lines in a GitHub Actions workflow.

Splitting a CI job into parallel jobs must not drop, duplicate or alter a test
line. This script lists every command line of every `run:` step, except the
plumbing steps named in PLUMBING_STEPS (package installation and cache, toolchain record,
compiler cache, build-tree transfer, planning, and this check itself), and tags each line
with the conditions it runs under: the job's tier (ADR 0170: the plan outputs its `if:`
requires, such as `full` or `build`), the container image and options and the job- and
step-level environment (minus compiler-cache variables, which change how objects are
produced but not what is tested). Comments and blank lines are ignored; repeated lines
are counted.

`check` also proves every test runs in the full tier: some full-tier line runs the whole
CTest suite (`ctest --preset dev`), and every native display fixture CMakeLists.txt
declares with sketchyup_native_fixture() is named by a full-tier line, apart from the
manual tools in MANUAL_FIXTURES.

  ci-line-inventory.py extract WORKFLOW            print the inventory
  ci-line-inventory.py check WORKFLOW INVENTORY    fail unless WORKFLOW's
                                                   inventory equals INVENTORY and
                                                   the full tier runs every test
  ci-line-inventory.py compare OLD NEW             fail unless two workflows
                                                   have the same inventory
"""
import collections
import os
import re
import sys

import yaml

# Steps that set up a job rather than test anything. They are repeated per job,
# so counting them would make every split look like a change.
PLUMBING_STEPS = re.compile(
    r"^(Install .*dependencies|Record toolchain|Check the test-line inventory"
    r"|Prepare the compiler cache|Compiler cache statistics|Prepare the package cache"
    r"|Pack the development build tree|Unpack the development build tree"
    r"|Check the blast-radius map|Plan the run|Compile this shard's objects"
    r"|Export the new compiler-cache entries|Every selected job succeeded"
    r"|Keep the test timings with the compiler cache|Install compile dependencies)$"
)
# Native fixtures no CI tier runs: interactive or timing tools whose evidence is manual.
MANUAL_FIXTURES = {"native_provider_trial", "terrain_budget_benchmark", "real_model_benchmark"}
TIER = re.compile(r"needs\.plan\.outputs\.(\w+) == 'true'")
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


def tier_context(job):
    condition = str(job.get("if", ""))
    tiers = sorted(set(TIER.findall(condition)))
    return "tier=" + ("|".join(tiers) if tiers else "always")


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
            context = (f"[{tier_context(job)} {container_context(job)}; "
                       f"env: {env_context(job.get('env'), step.get('env'))}]")
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


def full_tier_gaps(workflow_path, lines):
    """What the full tier does not run: [] when it runs every CTest test and fixture."""
    full = [line.split("] ", 1)[1] for line in lines
            if re.match(r"\[tier=[^ ]*\bfull\b", line)]
    gaps = []
    if not any(re.fullmatch(r"(\S+=\S+ )*ctest --preset dev", line) for line in full):
        gaps.append("no full-tier line runs the whole CTest suite (ctest --preset dev)")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(workflow_path))))
    with open(os.path.join(root, "CMakeLists.txt"), encoding="utf-8") as handle:
        fixtures = re.findall(r"sketchyup_native_fixture\(\s*(\w+)", handle.read())
    text = "\n".join(full)
    # Scripts the full tier calls run fixtures too (verify-m5-offline.sh and the like).
    for script in sorted(set(re.findall(r"\bscripts/[\w.-]+", text))):
        path = os.path.join(root, script)
        if os.path.isfile(path):
            with open(path, encoding="utf-8", errors="replace") as handle:
                text += "\n" + handle.read()
    for fixture in fixtures:
        if fixture not in MANUAL_FIXTURES and not re.search(rf"\b{fixture}\b", text):
            gaps.append(f"no full-tier line runs native fixture {fixture}")
    return gaps


def read_inventory(path):
    with open(path, encoding="utf-8") as handle:
        return collections.Counter(line.rstrip("\n") for line in handle if line.strip())


def main(argv):
    if len(argv) == 3 and argv[1] == "extract":
        sys.stdout.write(render(inventory(argv[2])))
        return 0
    if len(argv) == 4 and argv[1] == "check":
        actual = inventory(argv[2])
        status = report(read_inventory(argv[3]), actual, argv[3], argv[2])
        gaps = full_tier_gaps(argv[2], actual)
        for gap in gaps:
            print(f"Full tier gap: {gap}", file=sys.stderr)
        return 1 if gaps else status
    if len(argv) == 4 and argv[1] == "compare":
        return report(inventory(argv[2]), inventory(argv[3]), argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
