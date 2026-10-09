# Legacy preference compatibility and persistent themes

R084.c, 2026-10-07. Existing unversioned application preferences remain in the
SketchyUp/SketchyUp settings namespace. Reading a window does not replace the
settings store or remove unrelated/unknown keys. Units, camera field of view,
mouse/trackpad navigation, reduced motion, recovery interval, recent files, library
folder and renderer path retain their existing representations.

The theme menu previously changed only the active window even though startup read
a saved theme value. Each explicit theme selection now writes the same existing
`theme` integer key (0 system, 1 light, 2 dark) before applying the theme. No model
record or undo entry is created. No desktop/compositor configuration is modified.

The native regression seeds isolated legacy preferences and opaque future data,
then creates four successive windows. It checks effective units, field of view,
recovery, navigation and reduced motion; changes all three theme modes through
public actions; and verifies selected preferences survive subsequent window
construction. Unrelated values retain their original QVariant types and bytes.
Settings are synchronized to the private test store and document bytes/history
remain unchanged. No real user settings or credentials are read or modified.

This is the compatibility baseline for existing preferences, not a claim that the
planned shortcut editor or binding migration exists. Future migration must preserve
unknown values and user choices; shortcut and independent text-scale preferences
remain separate R084 work. The fixture recreates windows within one process rather
than claiming an installed-binary upgrade rehearsal.
