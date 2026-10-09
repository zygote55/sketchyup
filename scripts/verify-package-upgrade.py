#!/usr/bin/env python3
"""Check two actual application packages inside a disposable Arch container."""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prior-package", type=Path, required=True)
    parser.add_argument("--candidate-package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not (Path("/.dockerenv").is_file() and os.geteuid() == 0
            and os.environ.get("SKETCHYUP_PACKAGE_SANDBOX") == "1"):
        parser.error("requires the disposable bounded package container")
    packages = [path.resolve(strict=True) for path in
                (args.prior_package, args.candidate_package)]
    if packages[0] == packages[1] or digest(packages[0]) == digest(packages[1]):
        parser.error("provide distinct prior and candidate application packages")
    root = args.output.resolve()
    if root == Path("/tmp") or Path("/tmp") in root.parents:
        parser.error("evidence must be outside /tmp")
    root.mkdir()  # An earlier attempt's evidence must never be overwritten.
    report = {"passed": False, "packages": [], "steps": []}
    builder = pwd.getpwnam("builder")

    def own(path):
        os.chown(path, builder.pw_uid, builder.pw_gid)

    own(root)
    env = dict(os.environ)
    for kind in ("config", "data", "cache", "runtime"):
        path = root / kind
        path.mkdir(mode=0o700)
        own(path)
        name = "XDG_RUNTIME_DIR" if kind == "runtime" else "XDG_" + kind.upper() + "_HOME"
        env[name] = str(path)
    env.update(QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1")
    # These are synthetic settings in an isolated container, never user settings.
    config = root / "config/SketchyUp"
    config.mkdir()
    own(config)
    preferences = config / "SketchyUp.conf"
    preferences.write_text("[General]\npackageSentinel=retain\ntheme=1\n"
                           "interfaceTextPercent=150\nreducedMotion=true\n"
                           "trackpadNavigation=true\nrecoverySeconds=0\nfieldOfView=67\n"
                           "\n[futureNamespace]\ndata=opaque-upgrade-sentinel\n")
    own(preferences)
    for kind in ("data", "cache"):
        sentinel = root / kind / "sentinel"
        sentinel.write_text("retain-" + kind + "\n")
        own(sentinel)

    def run(label, command, user=False, timeout=120):
        if user:
            command = ["runuser", "-u", "builder", "--", *command]
        step = {"name": label, "command": [str(arg) for arg in command]}
        report["steps"].append(step)
        with (root / (label + ".stdout")).open("x") as out, \
                (root / (label + ".stderr")).open("x") as err:
            result = subprocess.run(command, env=env, stdout=out, stderr=err,
                                    timeout=timeout, check=False)
        step["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(label + " failed; inspect its retained logs")
        return (root / (label + ".stdout")).read_text()

    def read_preferences():
        result = configparser.ConfigParser(interpolation=None)
        result.read(preferences)
        return {section: dict(result[section]) for section in result.sections()}

    document = root / "Prior application model.sketchyup"
    try:
        # The pinned minimal image omits docs by default. Installed examples are
        # part of this check, so include them in this disposable environment.
        pacman_config = Path("/etc/pacman.conf")
        lines = pacman_config.read_text().splitlines(keepends=True)
        pacman_config.write_text("".join(
            line.replace("usr/share/doc/*", "") if line.lstrip().startswith("NoExtract")
            else line for line in lines))
        for path in packages:
            report["packages"].append({"file": path.name, "bytes": path.stat().st_size,
                                       "sha256": digest(path)})
        versions = []
        for label, package in zip(("prior", "candidate"), packages):
            info = run(label + "-package-info", ["pacman", "-Qp", package]).strip()
            name, version = info.split()
            if name != "sketchyup":
                raise RuntimeError("expected the application package, not debug symbols")
            versions.append(version)
        if int(run("version-order", ["vercmp", versions[1], versions[0]]).strip()) <= 0:
            raise RuntimeError("candidate must have a newer package version")
        report["versions"] = versions
        run("install-prior", ["pacman", "-U", "--noconfirm", packages[0]])
        run("prior-owned-files", ["pacman", "-Qkk", "sketchyup"])
        report["priorExecutables"] = {name: digest(Path("/usr/bin") / name)
                                       for name in ("sketchyup", "sketchyup-cli")}
        # Use the installed example to author and save with the earlier CLI.
        run("prior-create", ["sketchyup-cli", "--recipe",
            "/usr/share/doc/sketchyup/examples/transaction-face-recipe.json",
            "--new", "--output", document, "--outcomes", root / "prior-outcomes"], user=True)
        inspection = run("prior-inspect", ["sketchyup-cli", "--input", document], user=True)
        expected = json.loads(inspection)
        report["documentSha256"] = digest(document)

        def capture(label):
            result = json.loads(run(label, ["xvfb-run", "-a", "timeout", "40s",
                "sketchyup", document, "--capture", root / (label + ".png")], user=True,
                timeout=55))
            if not (result.get("passed") and result["documentId"] == expected["documentId"]
                    and result["revision"] == expected["revision"]
                    and result["bodies"] == len(expected["bodies"])):
                raise RuntimeError(label + " did not reopen the expected document")
            if not (root / (label + ".png")).is_file():
                raise RuntimeError(label + " did not write a capture")

        capture("prior-desktop")
        before = read_preferences()
        shutil.copyfile(preferences, root / "preferences-before.conf")
        run("upgrade-candidate", ["pacman", "-U", "--noconfirm", packages[1]])
        run("candidate-owned-files", ["pacman", "-Qkk", "sketchyup"])
        report["candidateExecutables"] = {name: digest(Path("/usr/bin") / name)
                                           for name in report["priorExecutables"]}
        if report["candidateExecutables"] == report["priorExecutables"]:
            raise RuntimeError("no application executable changed across the upgrade")
        actual = run("candidate-inspect", ["sketchyup-cli", "--input", document], user=True)
        if json.loads(actual) != expected:
            raise RuntimeError("upgrade changed public model inspection")
        capture("candidate-desktop")
        after = read_preferences()
        for section, keys in before.items():
            # Opening the same document can legitimately update window/recent state.
            for key in ("packagesentinel", "theme", "interfacetextpercent", "reducedmotion",
                        "trackpadnavigation", "recoveryseconds", "fieldofview", "data"):
                if key in keys and after.get(section, {}).get(key) != keys[key]:
                    raise RuntimeError("upgrade changed preference " + section + "/" + key)
        report["retainedPreferences"] = {section: {key: value for key, value in keys.items()
            if key in ("packagesentinel", "theme", "interfacetextpercent", "reducedmotion",
                       "trackpadnavigation", "recoveryseconds", "fieldofview", "data")}
            for section, keys in before.items()}
        owned = run("candidate-paths", ["pacman", "-Qlq", "sketchyup"]).splitlines()
        run("remove-candidate", ["pacman", "-R", "--noconfirm", "sketchyup"])
        for name in owned:
            if not name.endswith("/") and (Path(name).exists() or Path(name).is_symlink()):
                raise RuntimeError("package-owned file survived removal: " + name)
        if digest(document) != report["documentSha256"] or read_preferences() != after:
            raise RuntimeError("upgrade/removal modified the user document or preferences")
        for kind in ("data", "cache"):
            if (root / kind / "sentinel").read_text() != "retain-" + kind + "\n":
                raise RuntimeError("removal changed the " + kind + " sentinel")
        report["passed"] = True
    finally:
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Prior-application package upgrade, reopen, preferences and removal passed.")


if __name__ == "__main__":
    main()
