# 0155 — Verify the Linux accessibility bridge and dynamic selection text

The native selection-summary label keeps its stable accessible name and also
updates its accessible description with the visible selection text. Without the
description, its explicit name concealed the changing contents from AT-SPI clients.

The bridge fixture starts the real application with its built-in courtyard demo,
private config/data directories and accessibility enabled. It only traverses the
AT-SPI application whose process ID matches its own child. A separate D-Bus session
keeps the test away from other applications. No provider is configured or called.
The bounded tree walk limits depth, children and total nodes; reports include only
known fixture fields rather than an unrestricted desktop inventory.

Through AT-SPI, the fixture reads the named Measurements text interface, selects
the courtyard slab in the Outliner, verifies the changing selection description,
activates Entity info and reads independently expected 8×6×0.15 m bounds,
100.2 m² area and 7.2 m³ volume. These use native accessibility interfaces rather
than internal QObject access or pointer simulation. The disposable demo process
is terminated after the check; this does not test application shutdown recovery.

Run under `dbus-run-session` with the existing Wayland/X11 environment and
`at-spi2-core`. This is a real bridge protocol/data check. It does not certify an
interactive speech/braille session, every dialog or every assistive-software version.
