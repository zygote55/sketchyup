# Native accessibility and focus inventory

R084.a, 2026-10-07. `native_accessibility_audit` is a manual audit fixture, built
with the desktop test targets. It starts a real Window and synthetic demo in isolated
application settings, exposes all current model-panel tabs under both themes, and
queries Qt accessibility interfaces for visible, enabled, tab-focusable widgets.
The report includes interface presence, role, accessible name/description, tooltip,
focus policy and logical font size. It also lists public QAction IDs, shortcuts and
contexts, then records twelve actual F6 key-routing steps. Native document bytes
must remain unchanged. The fixture does not open assistant preferences, request
credentials or use a provider.

The default keyboard mode requires an exposed, active native window, for example through the existing isolated
Wayland test runner at SKETCHYUP_TEST_SCALE=1 and 2. Output is JSON on stdout with
releaseAcceptance=false. Names are collected from interfaces rather than inferred
from object IDs. A missing container name can be harmless when a named child owns
interaction, so unnamed counts are triage evidence and never automatic accessibility
failures. Negative point sizes identify pixel-sized fonts; they are not font errors.

This fixture does not prove screen-reader operation, contrast, focus-ring visibility,
text scaling, all-dialog reachability or complete mouse-free modeling. The audit
must exercise representative workflows and label/name relationships with assistive
technology before acceptance. Preference migration and conflict-aware shortcut
editing remain separate R084 implementation and validation requirements.

An explicit `--inventory-only` mode permits name/role/action inventory when the host
compositor does not grant activation. It does not request activation or inject F6;
`keyboardRoutingExercised` is false and the focus-owner list is empty. The report
also records active-window state. This mode must never be counted as a keyboard
workflow pass. A failed default-mode activation remains a disclosed audit limitation.
