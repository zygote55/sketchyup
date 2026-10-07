# R084.l — Linux accessibility bridge

2026-10-07. Selection-description correction `674aa4e`; fixture `9672ded`.
Contract: [0155](../decisions/0155-linux-accessibility-bridge.md).

The real AT-SPI bridge exposed a changing-label defect: the stable accessible name
“Selection summary” hid the visible selection contents. The label now updates its
accessible description along with its visible text. The formal [negative baseline](R084l-bridge-before.json)
fails against the installed R084.h application with “Selected entity is absent
from accessible description.”

The [four-case matrix](R084l-platform-matrix.json) passes on Wayland and X11 at
1×/2× in **16.687 s**. Through AT-SPI text, selection and action interfaces,
it reads the named Measurements input, selects the courtyard slab, observes its
name in the changing summary, opens Entity info and reads independently expected
8×6×0.15 m bounds, 100.2 m² area and 7.2 m³ volume. The [bounded reports](R084l-bridge-results.json)
cover 561 accessible nodes per run; they contain only known fixture values.

Environment: at-spi2-core 2.60.7-1, D-Bus 1.16.2-1, GLib 2.88.3-1, Qt base
6.11.2-3; Weston 15 and Xvfb with software rendering. Each check has a private D-Bus
session, settings/data directory and owned demo process. No provider is configured
or contacted. CI runs the same four checks with an explicit at-spi2-core dependency.

This verifies the actual Linux accessibility protocol and data. It does not claim
an interactive Orca, speech or braille session, every dialog, or sanitizer coverage
for this external-process fixture. The disposable process is terminated after the
check, so it is not shutdown/recovery evidence. R084/M9 acceptance remains open.
