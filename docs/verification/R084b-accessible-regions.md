# R084.b — Named modeling regions

2026-10-07. Follow-up to the [native inventory](R084a-local-accessibility-inventory.md).
Implementation `651ba21` adds contextual accessible names to the modeling tool rail,
model/render views, model/assistant side tabs, model-panel tabs, entity and material
property scroll areas, history tree and selected-step details. History also describes
the difference between selecting a step to read details and pressing Enter to restore it.

The existing Qt accessibility inventory was rerun on native Wayland at 1× and 2×.
Both inspect twelve model-panel tabs in two themes: **zero unnamed visible enabled
tab-focusable controls across all 24 contexts**, and all twelve F6 transitions
complete while preserving document bytes. The existing native History interaction
suite passes at 2×. [Timings and integration identity](R084b-accessible-regions.json).

This resolves the eight region/control naming findings in the current fixture.
It does not establish assistive-technology usability, hidden assistant controls,
all dialogs, contrast, text scaling, shortcut preferences or complete mouse-free
modeling. Those R084 checks and the M9 acceptance gate remain open. No user desktop
configuration or provider settings were changed.
