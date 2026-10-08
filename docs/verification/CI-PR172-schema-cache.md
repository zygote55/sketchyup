# PR172 — Cache immutable command descriptions

The [original sanitizer workflow](https://github.com/zygote55/sketchyup/actions/runs/37607520315/job/112746541247)
at `57cb6aea02bf5c69f76caf2d23b19dabf3381281` timed out while the cabinet
example's CLI was reconstructing the command schema. The CLI deadline remains
30,000 ms. A separate complete workflow passed, but expensive repeated schema
construction remained observable; a bounded exact-source reproduction retained
a further hosted-room timeout and a model-recipe process killed at the 4 GiB cap.

`CommandRegistry::descriptions()` now constructs its immutable catalog once and
returns a value copy. Regression checks poison a caller's returned schema and
verify that a new call and every authoritative entry remain unchanged. This
backports the catalog portion of the separately qualified PR248; component
placement and production limits are unchanged.

The [frozen proof](CI-PR172-schema-cache.json) hashes all 743 source inputs and
verifies the exact two-file candidate. The original [baseline](CI-PR172-original-baseline.txt)
and first [candidate attempt](CI-PR172-original-candidate.txt) remain retained.
The latter passes model recipes and recipes, but fails commands because its text
worker crashes. The diagnostic preserves the [sanitizer stack](CI-PR172-original-worker-crash.txt).
The disposable image lacked `ttf-dejavu`, which the unchanged native CI and Arch
package already require. An independent [Qt-only program](CI-PR172-fontless-qt-control.txt)
reproduces the crash with no SketchyUp code. Installing that signed font package
in a derived disposable image makes the exact same worker binary return valid
geometry with empty stderr and leak detection enabled. No host package or
application text code changes are involved.

With that test dependency supplied, [three normal cases](CI-PR172-cache-normal.txt)
and three sanitizer repetitions of the same cases pass:
[first](CI-PR172-cache-sanitize-1.txt), [second](CI-PR172-cache-sanitize-2.txt),
[third](CI-PR172-cache-sanitize-3.txt). Sanitized model recipes take
8.77–8.89 s; the complete recipe campaigns take 39.06–39.23 s, with every
individual CLI invocation still subject to the original 30-second deadline.
Commands take 7.85–8.03 s. ASan/UBSan and leak detection remain enabled.
The 300-second CTest process cap does not change the CLI's own deadline.

All work uses disk-backed scratch, at most eight CPU slots, two compiler jobs,
4 GiB RAM and no added swap. Original failures and the font-package signature/hash
are preserved. Earlier source-package records retain their original source
identities; fresh complete remote CI and ordered merge remain required for this
updated head. This follow-up does not declare M8 or release acceptance.
