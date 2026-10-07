# R084.m — Filtered shortcut keyboard workflow

2026-10-07. Final product correction `4d10182`; keyboard fixture `fe232ff`.
Contract: [0156](../decisions/0156-filtered-shortcut-keyboard-navigation.md).

The all-keyboard task found a wrong-selection defect: searching “Rectangle”, then
pressing Tab/Home, selected the hidden “New” command. Retained local negative log:
`build/r084m-filter-before.log` in the native verification worktree; diagnostic
`Keyboard Rectangle selection: query=Rectangle current=New — Ctrl+N focus=shortcutActions`.
The editor now lists only matching commands, preserving an existing matching choice
or selecting the first result. Hidden commands cannot be assignment targets.

The [eight-case matrix](R084m-platform-matrix.json) passes on Wayland/X11 at 1×/2×:
normal **18.654 s**, ASan/UBSan **26.859 s**, with leak detection and halt-on-error.
After initial viewport setup, actual keys find the shortcut editor, search Rectangle,
record Ctrl+Alt+R, assign/save it, activate the rebound tool, then restore defaults.
The same fixture retains conflict, cancellation, persistence, unknown-setting and
model-preservation checks. Tests await compositor focus instead of assigning it.

This closes the representative mouse-free shortcut-setup task. It does not certify
every keyboard layout, speech/braille workflow or dialog; R084/M9 acceptance remains
open. Private test settings configure no submitted provider request.
