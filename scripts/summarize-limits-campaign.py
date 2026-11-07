#!/usr/bin/env python3
"""Summarize one R114 limits campaign (scripts/session-limits-campaign.sh) for the record.

Reads every runs/FAMILY-TIER-rN/ directory under a campaign root and prints one row per case
(markdown) and, with --json PATH, writes the same rows as JSON. A case that wrote no JSON, or a
refusal, keeps its row with the reason. GPU memory is the largest sample taken while the case ran
(gpu.csv: vram_used and gtt_used in bytes, gpu_busy_percent).

Usage: summarize-limits-campaign.py RUNS_DIR [--json PATH]
Standard library only.
"""
import csv
import json
import os
import re
import sys

CASE = re.compile(r"^(?P<family>[a-z-]+)-(?P<tier>\d+)-r(?P<run>\d+)$")


def load_json(path):
    try:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        return None, "no result file"
    if not text.strip():
        return None, "no output"
    try:
        return json.loads(text), ""
    except json.JSONDecodeError:
        return None, "unparsable output"


def read_exit(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def gpu_peaks(path):
    peaks = {"samples": 0, "vramUsedFirstBytes": None, "vramUsedMaxBytes": 0,
             "gttUsedFirstBytes": None, "gttUsedMaxBytes": 0, "gpuBusyMaxPercent": 0}
    try:
        with open(path, encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                try:
                    peaks["samples"] += 1
                    if peaks["vramUsedFirstBytes"] is None:
                        peaks["vramUsedFirstBytes"] = int(row["vram_used"])
                        peaks["gttUsedFirstBytes"] = int(row["gtt_used"])
                    peaks["vramUsedMaxBytes"] = max(peaks["vramUsedMaxBytes"], int(row["vram_used"]))
                    peaks["gttUsedMaxBytes"] = max(peaks["gttUsedMaxBytes"], int(row["gtt_used"]))
                    peaks["gpuBusyMaxPercent"] = max(peaks["gpuBusyMaxPercent"], int(row["gpu_busy_percent"]))
                except (KeyError, TypeError, ValueError):
                    continue
    except OSError:
        pass
    return peaks


def last_line(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = [line.strip() for line in handle if line.strip()]
    except OSError:
        return ""
    return lines[-1] if lines else ""


def summarize(case_dir, label, match):
    result, reason = load_json(os.path.join(case_dir, "result.json"))
    exit_status = read_exit(os.path.join(case_dir, "exit.txt"))
    row = {
        "case": label,
        "family": match["family"],
        "tier": int(match["tier"]),
        "run": int(match["run"]),
        "exit": exit_status,
        "gpu": gpu_peaks(os.path.join(case_dir, "gpu.csv")),
    }
    if result is None:
        row["outcome"] = reason
        row["stderr"] = last_line(os.path.join(case_dir, "stderr.txt"))
        return row
    if "fixtureRefusal" in result:
        row["outcome"] = "fixture refused: " + result["fixtureRefusal"]
    elif "frameCompleteMs" in result:
        row["outcome"] = "measured"
    else:
        row["outcome"] = "incomplete"
    counts = result.get("counts", {})
    row["counts"] = counts
    row["fixtureBuildSeconds"] = result.get("fixtureBuildSeconds")
    row["fixturePeakRssMiB"] = result.get("fixturePeakRssMiB")
    if "modelJson" in result:
        row["modelJson"] = result["modelJson"]
        row["modelJsonParseSeconds"] = result.get("modelJsonParseSeconds")
    if "native" in result:
        row["native"] = result["native"]
    if "glb" in result:
        row["glb"] = result["glb"]
    for key in ("frameCompleteMs", "pickMs", "inferenceQueryMs", "editMove", "editDeform",
                "finalPeakRssMiB", "graphics", "textureSummary", "texturesReadyAfterShowSeconds",
                "inferenceInitialBuildSeconds", "geometryCacheMemory", "drmMemoryAfterFrames"):
        if key in result:
            row[key] = result[key]
    return row


def markdown(rows):
    head = ("| Case | Run | Exit | Outcome | Faces (unique / placed) | Frame p50 / p95 / p99 ms | Pick p95 ms "
            "| Inference p95 ms | Move feedback p50 / p95 ms | Deform feedback p50 / p95 ms | Undo p95 / Redo p95 ms "
            "| Save s / Open s | File MiB | GLB | Peak RSS MiB | VRAM first / max MiB | GTT first / max MiB |")
    columns = head.count("|") - 1
    lines = [head, "|" + "|".join(["---"] * columns) + "|"]
    for row in rows:
        counts = row.get("counts", {})
        frame = row.get("frameCompleteMs", {})
        move = row.get("editMove", {})
        deform = row.get("editDeform", {})
        native = row.get("native", {})
        glb = row.get("glb", {})
        feedback_move = move.get("editFeedbackMs", {})
        feedback_deform = deform.get("editFeedbackMs", {})
        glb_text = ("%.0f MB" % (glb["bytes"] / 1e6)) if glb.get("exported") else "refused"
        cells = [
            row["case"],
            str(row["run"]),
            "" if row["exit"] is None else str(row["exit"]),
            row["outcome"].replace("|", "/"),
            "%s / %s" % (counts.get("uniqueFaces", ""), counts.get("materializedFaces", "")),
            "%s / %s / %s" % tuple("%.1f" % frame[k] if k in frame else "" for k in ("p50", "p95", "p99")),
            "%.2f" % row["pickMs"]["p95"] if "pickMs" in row else "",
            "%.2f" % row["inferenceQueryMs"]["p95"] if "inferenceQueryMs" in row else "",
            ("refused (%s)" % move["refusal"] if move.get("edits") == 0 and "refusal" in move
             else "%.1f / %.1f" % (feedback_move.get("p50", 0), feedback_move.get("p95", 0)) if feedback_move else ""),
            ("refused (%s)" % deform["refusal"] if deform.get("edits") == 0 and "refusal" in deform
             else "%.1f / %.1f" % (feedback_deform.get("p50", 0), feedback_deform.get("p95", 0))
             if feedback_deform else ("refused: " + deform["refusal"] if "refusal" in deform else "")),
            "%.1f / %.1f" % (move.get("undoMs", {}).get("p95", 0), move.get("redoMs", {}).get("p95", 0))
            if move else "",
            "%.2f / %.2f" % (native.get("saveSeconds", 0), native.get("openSeconds", 0)) if native else "",
            "%.1f" % (native["fileBytes"] / 1048576) if native else "",
            glb_text if glb else "",
            "%.0f" % row["finalPeakRssMiB"] if "finalPeakRssMiB" in row else "",
            "%.0f / %.0f" % ((row["gpu"]["vramUsedFirstBytes"] or 0) / 1048576, row["gpu"]["vramUsedMaxBytes"] / 1048576),
            "%.0f / %.0f" % ((row["gpu"]["gttUsedFirstBytes"] or 0) / 1048576, row["gpu"]["gttUsedMaxBytes"] / 1048576),
        ]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    runs_dir = argv[1]
    json_path = argv[argv.index("--json") + 1] if "--json" in argv else None
    rows = []
    for name in sorted(os.listdir(runs_dir)):
        match = CASE.match(name)
        case_dir = os.path.join(runs_dir, name)
        if match and os.path.isdir(case_dir):
            rows.append(summarize(case_dir, name, match))
    rows.sort(key=lambda r: (r["family"], r["tier"], r["run"]))
    print(markdown(rows))
    if json_path:
        with open(json_path, "w", encoding="utf-8") as handle:
            json.dump(rows, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
