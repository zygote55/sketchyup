# R083.c — Local transaction sequence evidence

2026-10-07, implementation `ed2ee2a`. Normal **1/1 in 7.27 s** and ASan/UBSan
**1/1 in 38.91 s**, with leak detection and halt-on-error. The deterministic
[report](R083c-transaction-sequences.json) is identical in both builds: 256 prepared
transactions, 198 committed, 58 aborted, 60 uncertain outcomes reconciled and 32
coordinator restarts. Every mode executes at least 25 times. All transitions validate
native roundtrip, unchanged neighbor and the independent translation oracle;
confirmed edits verify one-step undo/redo and duplicate/late receipt lookup.

The [contract](../decisions/0144-seeded-transaction-sequences.md) describes replay,
fault boundaries and limitations. This is overlapping M9 fixture work under the
roadmap rule; M8/M9 acceptance and R083 completion are not claimed.
