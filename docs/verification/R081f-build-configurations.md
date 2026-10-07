# R081.f — Desktop tests without a CLI build

2026-10-07. Implementation `53bb8b9`. A configure-only probe with desktop ON,
CLI OFF and tests ON reproduced five missing-target errors: measured CLI checks
and DXF/STL/OBJ/glTF import checks unconditionally depended on `sketchyup-cli`.

Measured CLI tests now exist only when the CLI is built. Each import suite retains
its core conversion, persistence, validation and independent-producer checks in both
configurations; only CLI subprocess assertions are conditional on a generated
`CLI_PATH`. When enabled, the path names the actual CMake target and all earlier
CLI assertions still execute. Application and format implementations are unchanged.
CI includes the previously failing configure-only combination; existing triggers
and build/test gates remain intact.

Validation on the complete local M8 source:

- Desktop ON / CLI OFF: configure succeeds; **4/4 import suites pass in 2.94 s**.
  The old CLI executable was moved aside for the test and restored afterward, so
  the run cannot have accidentally used a leftover binary. CLI ON configuration
  was then restored in that development tree.
- Desktop OFF / CLI ON: **5/5 import and measured-CLI suites pass in 4.29 s**.
- The [source package](R081f-source-package.json) contains 196 byte-exact installed
  inputs; the Python build dependency from R081.e is retained.

This repairs a declared build option and its verification boundary. Full integrated
package acceptance and ordered PR gates remain required before M8 is accepted.
