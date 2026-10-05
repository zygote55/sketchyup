# R043.b native MCP inspection

Date: 2026-10-05 UTC. [PR #67](https://github.com/zygote55/sketchyup/pull/67)
merged after both CI runs passed:
[37258481496](https://github.com/zygote55/sketchyup/actions/runs/37258481496) and
[37258485215](https://github.com/zygote55/sketchyup/actions/runs/37258485215).
Native transport and platform acceptance are complete.
[Binding contract](../decisions/0029-native-mcp.md).

The native test opens the actual M4 room fixture in a visible `Window`, enters the
Room study context and selects Window A. A separate `sketchyup-cli --mcp-connect`
process discovers the native tool catalog over stdio and the private socket. It
reads that actual selection, measures the selected component's local width as
1.2 m and reads the camera matrices/context. Model container bytes, save stamp,
history and selection remain unchanged by inspection.

A resource subscription observes a manual change from Window A to Window B despite
an unchanged document revision and selected count. EOF completes the subscription
and exits the bridge while the window, model and listener remain alive. A final
unterminated request still receives its reply before EOF. Reopening the same
persistent document identity invalidates the old session and closes its endpoint.

Negative cases reject wrong documents, stale revisions, unadvertised mutation or
capture tools, mixed operation/query discriminators, nonprivate socket parents,
existing endpoint files and a third concurrent client. Existing endpoint bytes are
preserved. Oversized unterminated input closes its connection and reclaims its
slot. A client that stops reading after queuing 200 catalog requests does not
block the GUI or a second client's request. Wrong-thread construction fails before
reading the document. Disconnect destroys each client's retained inspection state.

All 55 development CTest suites passed. Native integration passed on software X11
at DPR 1 and 2, isolated Weston Wayland at DPR 1 and 2, and the actual Hyprland
Wayland desktop at DPR 2. The final owner-thread guard adjustment passed the native
fixture again on the actual desktop. Shared MCP regression tests, including the
new discriminator ambiguity case, pass under ASan/UBSan.

The native ASan/UBSan fixture passes on the actual Wayland desktop at DPR 2 and
in isolated software-rendered Weston at DPR 1, with leak detection enabled, when launched
with `QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion QT_IM_MODULE=compose`.
The initial host-theme run completed all assertions but LeakSanitizer reported
455,044 bytes rooted in the GTK/fontconfig stack. Removing that theme isolated
1,736 bytes in the Fcitx input-context plugin; using Qt's compose input context
removed the remaining report. No sanitizer checks or leak detection were disabled,
and no host desktop settings were edited.

A local installation launches the actual installed desktop executable with the
explicit model/socket flags, then connects the installed CLI bridge. Discovery
and session inspection succeed; EOF leaves the native process alive and preserves
fixture bytes. Installed native discovery exactly matches the installed JSON
catalog. The source archive includes transport, tests, schema and decision record.
Package CI checks discovery equality after install and artifact removal on uninstall.

The native binding intentionally exposes inspection only. Headless R043.a provides
staged mutations; native publication awaits the R046 assistant/coordinator work.
No provider or natural-language modeling acceptance is claimed here.
