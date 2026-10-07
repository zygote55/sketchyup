# R088.a — Installed development guides and support boundaries

2026-10-07. Documentation source `3f513b9` adds an installed
[user guide](../USER_GUIDE.md) and [support matrix](../SUPPORT_MATRIX.md).
This is overlapping release preparation. **R088 and M9 remain unaccepted.**

The guide covers exact keyboard construction and measurement, focus and shortcut
management, saving and recovery, assistant preview/apply, exchange and rendering,
and troubleshooting. The matrix records implementation boundaries and the
versions exercised by existing evidence. Neither document treats all Linux/Qt
versions, available GPU classes, providers or file variants as tested.

CMake configuration with desktop, CLI and tests disabled verifies that the
documentation can be installed independently. Both new installed files match
their sources byte for byte. Relative links in the README and guides resolve;
repository-only evidence links use the immutable integration commit
`a4bf6e9505a8cd50c171ec2b1dcf42f002e2fbec` so they also work from installed help.
The [source archive check](R088a-source-package.json) verifies all 218 installed
inputs byte for byte and excludes build products and Git metadata.

No new product behavior or release-candidate workflow acceptance is claimed by
these documentation checks. The final 59-Core-row parity report, complete
release-candidate walkthrough, provider and package gates, remote CI, and
ordered merges remain open. Existing evidence remains linked with its original
scope and failures rather than being relabeled as final release acceptance.
