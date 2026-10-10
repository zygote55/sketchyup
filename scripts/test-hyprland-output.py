#!/usr/bin/env python3
"""Move the viewport fixture's own window on Hyprland 0.55+ to an awake output.

Opt in with SKETCHYUP_TEST_OUTPUT_HELPER pointing to this executable when
running viewport_tests --screens. This changes no compositor configuration.
"""

import json
import re
import subprocess
import sys


def query(name):
    return json.loads(
        subprocess.check_output(["hyprctl", "-j", name], text=True, timeout=2)
    )


def main():
    if len(sys.argv) != 3 or not sys.argv[1].isdigit():
        raise ValueError("Expected test PID and output name")
    pid, screen = int(sys.argv[1]), sys.argv[2]
    if pid <= 1:
        raise ValueError("Invalid test PID")
    monitors = [monitor for monitor in query("monitors") if monitor["name"] == screen]
    if len(monitors) != 1 or not monitors[0].get("dpmsStatus") or monitors[0].get("disabled"):
        raise ValueError("Requested output must exist and already be awake")
    clients = [
        client
        for client in query("clients")
        if client.get("pid") == pid
        and client.get("title") == "SketchyUp output test " + screen
        and client.get("mapped")
    ]
    if len(clients) != 1:
        raise ValueError("Expected exactly one matching test window")
    address = clients[0]["address"]
    workspace = monitors[0]["activeWorkspace"]["id"]
    if not re.fullmatch(r"0x[0-9a-fA-F]+", address) or not isinstance(workspace, int) or workspace <= 0:
        raise ValueError("Invalid window address or ordinary workspace")
    # Explicit address: never act on whichever user window happens to have focus.
    dispatcher = (
        'hl.dsp.window.move({workspace="'
        + str(workspace)
        + '",follow=false,window="address:'
        + address
        + '"})'
    )
    result = subprocess.run(
        ["hyprctl", "dispatch", dispatcher],
        text=True,
        capture_output=True,
        timeout=2,
    )
    if result.returncode:
        raise RuntimeError("Compositor rejected test-window move: " + (result.stdout + result.stderr)[:512])


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Output test helper: {error}", file=sys.stderr)
        sys.exit(1)
