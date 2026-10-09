# 0154 — Keyboard-only Outliner workflow

The representative Outliner workflow starts from a small seeded hierarchy and a
single initial viewport focus assignment. Every subsequent user operation uses
actual keys: F6 reaches the hierarchy, Home/Down chooses an entity, F2 opens Rename,
typed text and Enter accept it, Ctrl+Z/Ctrl+Shift+Z restore names, Ctrl+Shift+L
changes lock state, Space changes visibility, Enter opens a group and Escape
leaves it. A nested child is renamed without changing ownership; a neighboring
body remains untouched.

Organization dialogs explicitly reactivate their parent window and restore focus
to the originating panel's current focus proxy after dismissal. This addresses an
X11 failure where the parent remained inactive after Rename. The native fixture
never repairs focus through direct assignments after its initial setup; ordinary
panel keyboard routing must recover it.

Native Wayland/X11 1×/2× checks and sanitizer gates supplement the broader panel
interaction suite. They establish this bounded task, not every tree size, tag form,
window-manager policy or screen-reader interaction. All settings and geometry are
private fixtures; no user document or provider configuration is touched.
