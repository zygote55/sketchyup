# R084.o — Name native dialog controls

The command palette's query and result list, the shortcut editor's internal input,
and the Assistant Preferences numeric editors had empty names in the native Qt
accessibility interface. These now expose descriptive names: “Command and model
search”, “Matching commands and model objects”, “New key combination”, “Context
tokens” and “CPU threads”. The numeric and shortcut names are also applied to the
internal line edits exposed by Qt to accessibility clients.

The [original diagnostic inventory](R084o-original-dialog-inventory.txt) runs
four X11/Wayland cases at scales 1 and 2. Each records 14 contexts: Drawing plane,
Keyboard shortcuts, Commands and model search, and Assistant Preferences in four
synthetic provider modes, each in dark and light themes. It inventories enabled,
visible controls with Tab focus, including Qt's internal editors. Completion of
this diagnostic does not mean all controls were named; its findings are retained
in the [frozen evidence](R084o-native-dialog-controls.json).

The strengthened assertion [fails against the parent UI](R084o-baseline-naming-failure.txt).
The original nonzero exit and exact assertion message are verified. The fixed
source `788cae6a09001e3ec67f34aee86c2aab52b67b01` passes [four normal](R084o-normal-native.txt) and
[four ASan/UBSan](R084o-sanitized-native.txt) native cases with leak detection
enabled. All eight reports contain the same 14 contexts and zero unnamed controls
in this scope. The audit also checks that opening and cancelling these dialogs
preserves document bytes. The unchanged existing command/UI behavior remains in
the native regression fixture; no provider request is submitted.

All settings and credential helpers are synthetic. The audit cancels dialogs and
does not read the user's account, credentials or saved preferences. The sanitizer
Wayland cases use the qualified CI-client reference fix with asserted dynamic
linkage. Source hashes, eight JSON reports, original findings and raw logs are
retained. Temporary source overlays were restored after validation.

This checks names through Qt accessibility. Full keyboard operation, screen-reader
integration, other dialogs and final accessibility/release acceptance remain open.
R084 and M9 are not complete.
