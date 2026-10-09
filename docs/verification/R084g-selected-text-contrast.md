# R084.g — Native selected-text contrast

2026-10-07. Fixture `789b42e`; correction `41dbd13`.
Contract: [0150](../decisions/0150-selected-text-contrast.md).

The [before report](R084g-contrast-before.json) reproduces dark-mode Measurements
selection at **1.859:1**: white text on the light green accent. The light-mode
selection already passes at **4.995:1**. Giving each theme an explicit selection
foreground raises the dark-mode ratio to **8.052:1**, retaining the light result.
The [after report](R084g-contrast-after.json) includes all eight measured states;
ordinary input, command-button and secondary-hint text also exceed 4.5:1.

The [platform matrix](R084g-platform-matrix.json) passes **8/8** across Wayland/X11
at 1×/2×, normal **11.351 s**, ASan/UBSan **15.123 s**, with leak detection and
halt-on-error. Every case reads the actual styled window palette and verifies
unchanged document content/history. The selected input colors are now independent
of a platform's default highlighted-text foreground.

This bounded active-control check does not cover every dialog/state, disabled text,
overlay, text scaling or screen-reader workflow. R084/M9 remain open. All settings
are isolated; no actual user preference or provider credential is used.
