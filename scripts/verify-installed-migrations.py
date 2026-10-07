#!/usr/bin/env python3
"""Exercise historical native migrations through an explicitly chosen installed CLI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)
    fixtures = args.fixtures.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    rows = []
    report = {"cliSha256": digest(cli), "passed": False, "fixtures": rows,
              "releaseAcceptance": False}

    def run(*arguments, success=True):
        process = subprocess.run([str(cli), *map(str, arguments)], capture_output=True,
                                 text=True, timeout=30)
        require((process.returncode == 0) if success else (process.returncode == 1),
                f"Unexpected CLI exit {process.returncode}: {arguments}")
        value = json.loads(process.stdout if success else process.stderr)
        require(isinstance(value, dict) and value, "CLI must return structured JSON")
        return value

    try:
        capabilities = run("--format-capabilities")
        report["formatCapabilities"] = capabilities
        names = sorted([*fixtures.glob("*.sketchyup"), *fixtures.glob("raw-*.json")])
        require(len(names) >= 20, "Historical fixture corpus is incomplete")
        invariant_fields = ("documentId", "revision", "units", "up", "bodies", "scenes",
                            "assets", "assetBytes", "missingAssets")
        for source in names:
            directory = output / source.name
            directory.mkdir()
            target = directory / "migrated.sketchyup"
            saved = directory / "ordinary-save.sketchyup"
            before_hash = digest(source)
            before = run("--inspect-native", source)
            original_model = run("--input", source)
            migrated = run("--migrate-native", source, "--output", target)
            after = run("--inspect-native", target)
            require(before["valid"] and after["valid"] and not after["migrationNeeded"],
                    f"Migration did not produce a valid current container: {source.name}")
            require(after["documentVersion"] == capabilities["documentVersion"] and
                    after["containerVersion"] == capabilities["containerVersion"],
                    f"Unexpected output format: {source.name}")
            require(migrated["status"] == "migrated" and migrated["sourceUnmodified"] and
                    migrated["outputSha256"] == digest(target) and
                    before["sha256"] == before_hash and digest(source) == before_hash,
                    f"Migration changed source or reported wrong output: {source.name}")
            require(all(before[key] == after[key] for key in invariant_fields),
                    f"Migration changed document metadata: {source.name}")
            require(run("--input", target) == original_model,
                    f"Migration changed public model inspection: {source.name}")
            # An ordinary load/save is a separate CLI path from migrate-native.
            # Compare complete containers, including all asset payloads and records.
            run("--input", source, "--output", saved)
            require(target.read_bytes() == saved.read_bytes(),
                    f"Migration differs from ordinary save: {source.name}")
            target_hash = digest(target)
            run("--migrate-native", source, "--output", target, success=False)
            require(digest(target) == target_hash and digest(source) == before_hash,
                    f"Existing destination or source was replaced: {source.name}")
            # Use a writable copy so rejection cannot merely be a read-only mount.
            writable = directory / "original-copy"
            writable.write_bytes(source.read_bytes())
            run("--migrate-native", writable, "--output", writable, success=False)
            require(digest(writable) == before_hash, "In-place migration changed source")
            malformed = directory / "truncated-input"
            malformed.write_bytes(source.read_bytes()[:16])
            malformed_hash = digest(malformed)
            rejected = directory / "must-not-exist.sketchyup"
            run("--migrate-native", malformed, "--output", rejected, success=False)
            require(not rejected.exists() and digest(malformed) == malformed_hash,
                    "Failed migration published output or changed input")
            rows.append({"fixture": source.name, "sourceVersion": before["documentVersion"],
                         "sourceSha256": before_hash, "outputSha256": target_hash,
                         "outputBytes": target.stat().st_size,
                         "metadata": {key: after[key] for key in invariant_fields},
                         "ordinarySaveByteExact": True, "sourceUnchanged": True,
                         "existingOutputPreserved": True, "inPlaceRejected": True,
                         "malformedInputRejected": True})
        report["passed"] = True
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Installed CLI migrated {len(rows)} historical fixtures; all preservation checks passed.")


if __name__ == "__main__":
    main()
