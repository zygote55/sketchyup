# R084.n — Label subscription preferences

The Assistant Preferences account and model selectors now have visible associated
labels, “ChatGPT account” and “ChatGPT model”, and explicit accessible names. The
scroll region is named “Assistant preferences”. This distinguishes the two
selectors for both visual navigation and Qt accessibility clients.

The existing native assistant regression now checks the accessible interfaces,
their selected values, native caption relations and visible label associations. The [parent UI run](R084n-baseline-label-failure.txt)
fails the descriptive-name property assertion; the unexpected-failure guard retains its nonzero
exit and refuses other failure messages as proof of this defect.

The [first candidate run](R084n-original-name-assertion-failure.txt) failed an
incorrect test expectation: Qt Unix combo boxes report the current selection as
the accessible name and expose the caption through a native relation
([Qt source](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/accessible/complexwidgets.cpp)).
The corrected regression checks both the selected value and the exact associated
caption. A [second candidate run](R084n-original-relation-assertion-failure.txt)
then failed because the test requested `QAccessible::Labelled`, the opposite
relation direction. An [isolated synthetic Qt 6.11.2 probe](R084n-native-relation-probe.txt)
verified that `QAccessible::Label` returns the caption when called on the selector
([Qt relation contract](https://doc.qt.io/qt-6/qaccessible.html#RelationFlag-enum)).
The frozen test checks that exact caption, the associated QLabel's buddy, the
selected value, and the separately configured QWidget accessible-name property.
Both original failures are retained. A subsequent scheduling refusal happened
before a build or test ran; it is retained in the JSON proof. The rerun waits for
the prior heavy job rather than changing any test or resource limit.

Source `cb31721b43041c97697ed179b476062dd78b6c3a` passes [four normal cases](R084n-normal-native.txt) and
[four ASan/UBSan cases](R084n-sanitized-native.txt): X11 and Wayland at scales 1 and 2,
with leak detection enabled. The complete existing regression also checks setup,
consent, synthetic preview/apply, clarification, staleness, layouts, manual fallback
and unknown-outcome reconciliation. [Identities and hashes](R084n-assistant-preference-labels.json)
bind these results to their inputs, including the qualified component-placement
and immutable-catalog core. The source-freeze proof preserves an older descriptive
field and explicitly corrects its relation-direction wording; compiled test bytes
use `QAccessible::Label`. The sanitizer Wayland runs use the previously
verified private client reference fix; dynamic linkage is asserted.

All settings and credential helpers are synthetic, and network responses are
injected. These runs do not read the user's account or key, sign in, call a live
provider or change saved provider/model choices. This verifies descriptive names
and visible labels through Qt; it does not substitute for a complete screen-reader,
all-dialog or release-candidate accessibility audit. R084 and M9 remain open.
