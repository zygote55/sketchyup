# R084.a — Local native accessibility inventory

2026-10-07. Fixture `91b2d54`; [contract](../decisions/0145-native-accessibility-inventory.md).
Native Wayland [1×](R084a-wayland-1x.json) and [2×](R084a-wayland-2x.json) runs both
complete, using Qt 6.11.2, Fusion and the isolated software-rendered compositor.
Each inventories **150 public actions**, **12 model-panel tabs in two themes** and
**12 F6 transitions**, while preserving native document bytes. This overlapping
fixture/audit work does not accept R084, M9 or a supported-hardware configuration.

Both scales identify the same unnamed focusable objects for triage:

| Object | Follow-up |
| --- | --- |
| Tool rail | Give the focusable tool region a meaningful accessible name; verify tool button names and arrow navigation |
| Model, side and organization tab containers | Inspect parent/child names with assistive technology; the named tab children may already identify the context |
| Entity-info and material scroll areas | Verify the named content and entry focus; add container names where needed |
| History tree and history details editor | Add contextual accessible names and verify keyboard selection/detail reading |

Observed F6 owners cycle through viewport, the current Images panel's import button,
Measurements, Commands and tool rail at both scales. The assistant is hidden in this
fixture and is correctly outside this observed cycle; its visible-state keyboard
workflow needs a separate audit. These observations do not establish that every
control is reachable or that the order is optimal.

Source review also identifies remaining R084 work: shortcuts are currently assigned
from fixed defaults, with no persisted user-binding editor or migration contract.
Navigation, theme, units, recovery and reduced-motion settings exist, but upgrade
fixtures must verify their preservation. Contrast, independent UI text scaling,
visible focus, all-dialog access and representative mouse-free modeling still need
explicit checks. Current interaction tests include pointer steps and cannot substitute
for that final workflow.
