# R040.a bounded document inspection

Date: 2026-10-04. Local implementation verification passed; CI pending.
Requires [PR #56](https://github.com/zygote55/sketchyup/pull/56) and the M4 gate
before acceptance. [Contract and schemas](../decisions/0019-bounded-inspection.md).

The targeted inspection suite passes. It loads the actual M4 room fixture,
supplies a session-bound selection, identifies Window A as a component and
measures its 1.2 × 0.3 × 1.0 m local bounds without a whole-model dump. It checks
that multipart volume is unavailable, traverses only the selected subtree and
finds the associated definition's instances. Document bytes and save stamp remain
unchanged. The file-only CLI measures the same target using its explicit typed
reference and correctly reports that desktop selection is unavailable.

Geometry fixtures cover rotated, mirrored and nonuniform local/world frames;
typed vertex/edge/face topology; radial incidence; a 256-segment curve and face
loop; affine ellipse parameterization; reflected face normals; and infinite
guides. Large escaped semantic values exercise the byte budget before the row
limit, then reconstruct every property through continuation pages.

Failure checks cover changed filters/editor state, stale document revisions,
wrong documents and selection sessions, mismatched context paths, wrong entity
namespaces, unknown capabilities/fields/enums, fractional/excess page limits,
oversized requests, bad tokens, uint64 overflow, missing local frames and
degenerate measurement geometry. Visibility checks include inherited persistent
hiding/locks and temporary editor hiding. The CLI rejects saving or mixed
operations and never creates the requested output file.

All registry entries have executable coverage. The published schema artifact is
checked against the live registry. This slice does not claim retained snapshots,
desktop view capture, MCP, providers or staged transaction outcomes.

The complete development build and all 45/45 CTest suites pass. The new inspection
suite, including real CLI subprocesses, also passes in a Qt Core build instrumented
with AddressSanitizer and UndefinedBehaviorSanitizer. CI runs that additional
sanitized boundary check after its existing 30-suite core sanitizer matrix.
The change adds only a session-membership accessor to the core selection API;
there are no renderer or native input behavior changes.

The source archive includes the published schema and contract. A staged local
installation contains both, and the installed CLI's registry exactly matches the
installed schema. Disposable Arch package acceptance also checks that match and
schema removal on uninstall. Full install/upgrade/removal CI remains pending.

Follow-up test review found a `QJsonValueRef` retained from a temporary JSON
object in the reflected-normal assertion. It now stores an owning `QJsonValue`.
The inspection engine and published schemas are unchanged.

CI runs 37244758087 and 37244755725 passed the build and all 45 package test
suites, then exposed the minimal Arch image's `NoExtract = usr/share/doc/*`
setting: installation excluded the new contracts and `pacman -Qkk` failed.
The guarded disposable acceptance script now removes only that exclusion before
installing packages. A fresh container probe reproduced the missing directory,
then verified that the correction installs the schema, passes `pacman -Qkk`
with zero altered files, and removes the schema on uninstall. This changes only
the disposable test container; application packages do not alter pacman settings.
The complete package acceptance CI is rerunning.
