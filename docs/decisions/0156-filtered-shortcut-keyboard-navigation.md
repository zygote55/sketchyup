# 0156 — Keep shortcut keyboard navigation within filtered commands

Shortcut search rebuilds its bounded command list from matching public action
names and effective key labels. It preserves the current command if still present,
otherwise selects the first match. With no matches, it selects no command. Hidden
rows are not retained as keyboard destinations or assignment identities.

A native keyboard regression revealed that hiding nonmatching QListWidget rows
left Home able to select the hidden “New” command after searching for “Rectangle”.
The replacement list contains only matches, so its keyboard and visible results
agree. Draft bindings, unknown saved entries, explicit conflict handling and Save
semantics remain governed by the existing shortcut policy.

The acceptance task starts with viewport focus, opens command search with Ctrl+K,
types “Keyboard shortcuts”, enters the editor, filters Rectangle, navigates with
Tab/Home, records Ctrl+Alt+R through actual key events, assigns and saves it, then
activates Rectangle with that key. A second all-keyboard pass restores defaults.
Native modal activation is awaited; the test never assigns focus after initial setup.
The enclosing regression also checks conflicts, persistence, cancellation and model
preservation. This does not certify every keyboard layout or assistive technology.
