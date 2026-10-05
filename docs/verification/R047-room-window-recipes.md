# R047 room and instance-only window recipes

Date: 2026-10-05 UTC. Local verification recorded below; CI/merge acceptance pending.
[Contract and dimension convention](../decisions/0031-room-window-recipes.md).

The default recipe creates a 6 × 4 × 2.7 m outside room, 200 mm wall thickness and
two shared 1.2 × 1 m outer-frame windows. Independent solid measurement gives
9.888 m³ for the walls after both openings. Widening Window A to 1.4 m gives
9.848 m³ wall volume and 1.24 m clear frame width. Only the selected opening's
eight corner vertices move, each jamb by 100 mm. Window center, sill, height,
100 mm depth and 80 mm jamb/rail thickness remain unchanged.

The model recipe suite checks exact sibling/unrelated body fingerprints, shared
definition preservation, explicit make-unique scope, no new definition on a
second resize, shrinking, one history entry per recipe, undo/redo, persistence,
rigid rotation and reflection, and a separately parameterized 8 × 5 m room.
Failures cover conflicting dimensions, malformed values, missing scope, wrong
scope, unknown parameters, nonexistent bodies, missing/stale metadata, scaled
hosts, independently moved windows, altered frame geometry and locked wall/room.
A preceding valid command also rolls back when a later recipe in its batch fails.

The shipped room-only and room-plus-resize recipes complete all 8 and 16 steps
respectively through the real headless session, private staging, measurements,
preview, durable commit and explicit save. Reloaded models preserve the exact
widths and definition counts. Typed references carry allocated IDs between steps.
The shared command catalog's executable cases verify required/unknown fields,
single publication and undo for both new commands.

All 57 development CTest suites pass in 32.20 s.

ASan/UBSan with leak detection enabled: model recipe suite passed in 45.09 s;
headless recipe suite passed in 17.05 s. No checks were disabled. Local installed
CLI runs both installed recipes without DISPLAY, WAYLAND_DISPLAY or
QT_QPA_PLATFORM, and both installed session/MCP catalogs match discovery. The
source archive contains implementation, tests, examples and the decision record.
Package CI also executes the combined installed recipe and verifies removal.

The actual native Wayland application opens the saved revision-two model with
nine bodies at DPR 2, renders both framed openings, captures successfully and
reports no GL errors on Intel Arc / Mesa 26.2.2. Its model bytes are preserved.
The capture is a renderer/open check; selection-driven assistant interaction and
live provider acceptance remain R046/R051 work.
