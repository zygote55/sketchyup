#!/usr/bin/env python3
"""Decide what the CI run does (ADR 0170), and keep the blast-radius map honest.

  ci-plan.py check
      Fail unless .github/ci-path-map.json covers every tracked file, names only real
      CTest tests and known areas, reproduces its own examples, and every documentation
      file CMakeLists.txt installs exists.
  ci-plan.py areas PATH...
      Print the fast-tier areas the given changed paths select.
  ci-plan.py plan
      In GitHub Actions: pick the tier, the areas and the compile mode, and write them
      to $GITHUB_OUTPUT. Reads the event payload, the git history (a full clone) and,
      through `gh`, the repository's labels and compiler caches.

The full tier runs on release tags (v*), release/* branches and pull requests from them,
pull requests labelled full-ci, the nightly schedule, and workflow_dispatch with
tier=full. Everything else gets the fast tier, sized by the changed paths.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP = ROOT / ".github" / "ci-path-map.json"
FULL_LABEL = "full-ci"
CACHE_PREFIX = "ccache-fast-v1-"
# Changes after which most objects miss the compiler cache: headers, the build definition,
# vendored code and the pinned mirror (it decides the compiler). The fast tier then compiles
# in parallel shards first.
WIDE = re.compile(r"(\.(hpp|h|hh|inl|ipp|in)$|(^|/)CMakeLists\.txt$|^CMakePresets\.json$"
                  r"|^third_party/|^packaging/arch/mirrorlist$)")
# Full-tier jobs that test the development build tree (the `build` job's artifact).
DEV_BUILD_CONSUMERS = {"x11-1x", "x11-2x", "wayland-1x", "wayland-2x", "audit", "fractional"}
# More changed sources than this also compile in shards.
NARROW_SOURCE_LIMIT = 24
# Paths whose headers, sources and CMake files no fast-tier object depends on: IfcOpenShell and the
# helper that links it are built by the IFC job (ADR 0171) from their own prefix, never by the
# `ci-fast` build. Counting them would plan every pull request wide after the vendored tree changes.
NOT_IN_FAST_BUILD = ("third_party/ifcopenshell/", "src/ifc/")


def glob_regex(pattern):
    out = ""
    i = 0
    while i < len(pattern):
        if pattern.startswith("**", i):
            out += ".*"
            i += 2
        elif pattern[i] == "*":
            out += "[^/]*"
            i += 1
        elif pattern[i] == "?":
            out += "[^/]"
            i += 1
        else:
            out += re.escape(pattern[i])
            i += 1
    return re.compile(out + r"\Z")


def cmake_text():
    return (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")


def executable_sources():
    """Source paths of every executable CMakeLists.txt declares, by target."""
    sources = {}
    pattern = r"(?:add_executable|sketchyup_native_fixture|target_sources)\(\s*(\S+)\s+([^)]*)\)"
    for match in re.finditer(pattern, cmake_text()):
        words = [w for w in match.group(2).split() if re.match(r"(src|tests|third_party)/", w)]
        sources.setdefault(match.group(1), set()).update(words)
    return sources


def native_fixture_sources():
    """Sources of the executables the X11 1x lines run (the fast tier's native job)."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import importlib
    run_lines = importlib.import_module("ci-run-lines")
    _, lines = run_lines.step_lines(str(ROOT / ".github" / "workflows" / "native.yml"), "x11-1x",
                                    "Native interaction and OpenGL tests")
    targets = {t for line in lines for t in re.findall(r"build/dev/([A-Za-z0-9_-]+)", line)}
    by_target = executable_sources()
    return set().union(*(by_target.get(target, set()) for target in targets))


def ctest_names():
    return set(re.findall(r"add_test\(\s*NAME\s+(\S+)", cmake_text()))


def ctest_sources():
    """Source paths each CTest test runs: its executables' sources and named test files."""
    by_target = executable_sources()
    tests = {}
    for match in re.finditer(r"add_test\(\s*NAME\s+(\S+)\s+COMMAND\s+((?:[^()]|\([^()]*\))*)\)",
                             cmake_text()):
        name, command = match.group(1), match.group(2)
        paths = set(re.findall(r"\b((?:tests|scripts)/[A-Za-z0-9_./-]+)", command))
        for target in re.findall(r"\$<TARGET_FILE:([^>]+)>", command) + command.split()[:1]:
            paths |= by_target.get(target, set())
        tests[name] = paths
    return tests


class PathMap:
    def __init__(self, data):
        self.data = data
        self.native_sources = native_fixture_sources()
        self.rules = []
        # A slow test's own sources select its group, so a change to the test runs it.
        sources = ctest_sources()
        for area, names in data["slow_tests"].items():
            paths = set().union(*(sources.get(name, set()) for name in names))
            self.rules.append(([glob_regex(path) for path in sorted(paths)], {"build", area}))
        for rule in data["rules"]:
            patterns = []
            for pattern in rule["paths"]:
                if pattern == "@native-fixture-sources":
                    patterns.append(pattern)
                else:
                    patterns.append(glob_regex(pattern))
            self.rules.append((patterns, set(rule["areas"])))

    def match(self, path):
        """The areas of every rule matching path, or None when no rule does."""
        areas = None
        for patterns, rule_areas in self.rules:
            for pattern in patterns:
                hit = (path in self.native_sources if pattern == "@native-fixture-sources"
                       else pattern.match(path))
                if hit:
                    areas = (areas or set()) | rule_areas
                    break
        return areas

    def areas(self, paths):
        selected, unmatched = set(), []
        for path in paths:
            areas = self.match(path)
            if areas is None:
                unmatched.append(path)
                selected |= set(self.data["fallback"])
            else:
                selected |= areas
        return selected, unmatched

    def ctest_exclude(self, areas):
        excluded = sorted(test for area, tests in self.data["slow_tests"].items()
                          if area not in areas for test in tests)
        return "^(" + "|".join(excluded) + ")$" if excluded else ""


def load():
    return PathMap(json.loads(MAP.read_text(encoding="utf-8")))


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True,
                          text=True).stdout


def check():
    pm = load()
    errors = []
    known = set(pm.data["areas"])
    for rule in pm.data["rules"]:
        for area in rule["areas"]:
            if area not in known:
                errors.append(f"rule {rule['paths']} names unknown area {area}")
    workflow = (ROOT / ".github" / "workflows" / "native.yml").read_text(encoding="utf-8")
    for area in known:
        if area.startswith("job:"):
            job = area.split(":", 1)[1]
            if f"contains(needs.plan.outputs.jobs, ',{job},')" not in workflow:
                errors.append(f"area {area}: job {job} does not check needs.plan.outputs.jobs")
    for area in pm.data["fallback"]:
        if area not in known:
            errors.append(f"fallback names unknown area {area}")
    tests = ctest_names()
    for area, names in pm.data["slow_tests"].items():
        if area not in known:
            errors.append(f"slow_tests names unknown area {area}")
        if not any(area in rule["areas"] for rule in pm.data["rules"]):
            errors.append(f"no rule selects {area}: its tests would only run in the full tier")
        for name in names:
            if name not in tests:
                errors.append(f"{area}: no CTest test named {name} in CMakeLists.txt")
    if not pm.native_sources:
        errors.append("found no sources for the X11 1x executables")
    sources = ctest_sources()
    for area, names in pm.data["slow_tests"].items():
        for name in names:
            if not sources.get(name):
                errors.append(f"{area}: found no source files for CTest test {name}")
    # The package job copies these; a rename would otherwise fail only there.
    package_source = (ROOT / "scripts" / "package-source.sh").read_text(encoding="utf-8")
    for path in sorted(set(re.findall(r"\b(docs/[A-Za-z0-9_./-]+)", package_source))):
        if not (ROOT / path).exists():
            errors.append(f"scripts/package-source.sh copies missing path {path}")
    tracked = git("ls-files").split("\n")
    for path in filter(None, tracked):
        if pm.match(path) is None:
            errors.append(f"no rule covers tracked file {path}")
    for example in pm.data["examples"]:
        got, _ = pm.areas(example["paths"])
        if got != set(example["areas"]):
            errors.append(f"example '{example['why']}': expected {sorted(example['areas'])}, "
                          f"got {sorted(got)}")
    # A renamed document that CMake installs only fails at package time: catch it here.
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for doc in sorted(set(re.findall(r"\b(docs/[A-Za-z0-9_./-]+\.(?:md|json))", cmake))):
        if not (ROOT / doc).is_file():
            errors.append(f"CMakeLists.txt installs missing file {doc}")
    for error in errors:
        print(f"ci-path-map: {error}", file=sys.stderr)
    if errors:
        print(f"{len(errors)} problem(s) in {MAP.relative_to(ROOT)}.", file=sys.stderr)
        return 1
    print(f"Path map covers {len(tracked) - 1} tracked files with {len(pm.rules)} rules; "
          f"{len(pm.data['examples'])} examples hold.")
    return 0


def gh_json(*args):
    try:
        out = subprocess.run(["gh", "api", *args], cwd=ROOT, check=True, capture_output=True,
                             text=True, timeout=60).stdout
        return json.loads(out)
    except (subprocess.SubprocessError, json.JSONDecodeError, OSError) as error:
        print(f"::warning::gh api {args[0]} failed: {error}")
        return None


def changed_between(base, head):
    try:
        return [p for p in git("diff", "--name-only", f"{base}...{head}").split("\n") if p]
    except subprocess.CalledProcessError:
        return None


def newest_cache_commit(repo, ref):
    """The commit of the newest fast-tier compiler cache visible on ref."""
    data = gh_json(f"repos/{repo}/actions/caches?key={CACHE_PREFIX}&ref={ref}"
                   "&sort=created_at&direction=desc&per_page=5")
    for cache in (data or {}).get("actions_caches", []):
        match = re.match(re.escape(CACHE_PREFIX) + r"([0-9a-f]{40})-", cache["key"])
        if match:
            return match.group(1)
    return None


def plan():
    event_name = os.environ["GITHUB_EVENT_NAME"]
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    repo = os.environ["GITHUB_REPOSITORY"]
    ref = os.environ.get("GITHUB_REF", "")
    head = os.environ["GITHUB_SHA"]
    pm = load()
    reasons = []

    full = False
    if event_name == "schedule":
        full, reasons = True, ["nightly schedule"]
    elif event_name == "workflow_dispatch":
        tier = (event.get("inputs") or {}).get("tier", "full")
        full = tier == "full"
        reasons.append(f"workflow_dispatch tier={tier}")
    elif ref.startswith("refs/tags/v"):
        full, reasons = True, [f"release tag {ref}"]
    elif ref.startswith("refs/heads/release/"):
        full, reasons = True, [f"release branch {ref}"]
    elif event_name == "pull_request":
        pr = event["pull_request"]
        labels = {label["name"] for label in pr.get("labels", [])}
        live = gh_json(f"repos/{repo}/issues/{pr['number']}/labels")
        if isinstance(live, list):
            labels = {label["name"] for label in live}
        if FULL_LABEL in labels:
            full, reasons = True, [f"label {FULL_LABEL}"]
        elif pr["head"]["ref"].startswith("release/") or pr["base"]["ref"].startswith("release/"):
            full, reasons = True, ["release branch pull request"]

    outputs = {"full": full, "build": False, "native": False, "blender": False,
               "package": False, "wide": False, "ctest_exclude": "", "jobs": ","}
    if full:
        outputs.update(blender=True, package=True)
    else:
        # What changed: the pull request against its merge base, or the pushed range.
        if event_name == "pull_request":
            base_sha = event["pull_request"]["base"]["sha"]
            changed = changed_between(base_sha, event["pull_request"]["head"]["sha"])
            cache_ref = f"refs/heads/{event['pull_request']['base']['ref']}"
        else:
            before = event.get("before", "")
            changed = (changed_between(before, head)
                       if before and not re.fullmatch("0+", before) else None)
            cache_ref = ref
        if changed is None:
            reasons.append("changed paths unknown: fallback areas")
            areas = set(pm.data["fallback"])
        else:
            areas, unmatched = pm.areas(changed)
            reasons.append(f"{len(changed)} changed path(s)")
            if unmatched:
                reasons.append("unmapped: " + ", ".join(unmatched[:10]))
        for name in ("build", "native", "blender", "package"):
            outputs[name] = name in areas
        # Single full-tier jobs a harness change selects; the native ones need the dev build.
        jobs = sorted(area.split(":", 1)[1] for area in areas if area.startswith("job:"))
        if jobs:
            if set(jobs) & DEV_BUILD_CONSUMERS:
                jobs = sorted(set(jobs) | {"build"})
            outputs["jobs"] = "," + ",".join(jobs) + ","
            reasons.append("full-tier jobs " + ", ".join(jobs))
        if "full" in areas:
            # Harnesses only the full tier runs: run it.
            full = True
            reasons.append("paths only the full tier exercises: full tier")
            outputs.update(full=True, build=False, native=False, blender=True, package=True,
                           ctest_exclude="", jobs=",")
        if not full:
            outputs["ctest_exclude"] = pm.ctest_exclude(areas)
        if not full and (outputs["build"] or outputs["native"]):
            # Wide when the newest cache predates header or build-definition changes.
            cache_commit = newest_cache_commit(repo, cache_ref)
            since = None
            if cache_commit:
                subprocess.run(["git", "fetch", "-q", "--no-tags", "origin", cache_commit],
                               cwd=ROOT, capture_output=True)
                try:
                    since = [p for p in git("diff", "--name-only", cache_commit, head).split("\n") if p]
                except subprocess.CalledProcessError:
                    since = None
            if since is None:
                outputs["wide"] = True
                reasons.append(f"no usable compiler cache on {cache_ref}: wide compile")
            else:
                relevant = [p for p in since if not p.startswith(NOT_IN_FAST_BUILD)]
                wide_paths = [p for p in relevant if WIDE.search(p)]
                sources = [p for p in relevant if p.endswith(".cpp")]
                outputs["wide"] = bool(wide_paths) or len(sources) > NARROW_SOURCE_LIMIT
                reasons.append(
                    f"cache {cache_commit[:10]}: {len(since)} path(s) since ({len(since) - len(relevant)} not in the fast build), "
                    f"{len(wide_paths)} header/build, {len(sources)} sources -> "
                    + ("wide" if outputs["wide"] else "narrow") + " compile")
    summary = "; ".join(reasons)
    lines = [f"{key}={str(value).lower() if isinstance(value, bool) else value}"
             for key, value in outputs.items()] + [f"summary={summary}"]
    print("\n".join(lines))
    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")
    if "GITHUB_STEP_SUMMARY" in os.environ:
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as handle:
            handle.write("### CI plan\n\n" + summary + "\n\n| output | value |\n|---|---|\n")
            handle.write("".join(f"| {line.split('=', 1)[0]} | `{line.split('=', 1)[1]}` |\n"
                                 for line in lines[:-1]))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "check":
        return check()
    if len(argv) >= 2 and argv[1] == "areas":
        pm = load()
        areas, unmatched = pm.areas(argv[2:])
        print(" ".join(sorted(areas)) or "(none)")
        if unmatched:
            print("unmapped (fallback): " + " ".join(unmatched))
        print("ctest -E " + (pm.ctest_exclude(areas) or "(nothing excluded)"))
        return 0
    if len(argv) == 2 and argv[1] == "plan":
        return plan()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
