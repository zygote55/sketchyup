# 0150 — Explicit selected-text contrast

The native stylesheet sets both line-edit selection colors. Light mode retains a
white foreground against its dark accent; dark mode uses the dark surface color
against its light accent. Selection must not inherit a white foreground from the
platform palette in both modes.

The native test reads the effective active Qt palette after applying each theme to
the actual window. It verifies Measurements text and selected text, the command
button and secondary hint text against the UX contract's 4.5:1 threshold. Theme
changes preserve model content and history. The before/after report records exact
foreground/background colors and computed sRGB relative-luminance ratios.

This fixes a confirmed dark-mode selection failure. It is a bounded control-state
check, not a claim that every hover, disabled, tooltip, viewport overlay, dialog or
assistive-technology workflow has completed the broader R084 accessibility audit.
