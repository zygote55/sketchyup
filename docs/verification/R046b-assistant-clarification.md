# R046.b structured clarification verification

Date: 2026-10-05 UTC. Local acceptance passed; CI pending.
[Dialogue contract](../decisions/0038-assistant-clarification.md).

All 62 enabled development suites passed in 37.20 s. The real-Blender opt-in test
was skipped; this change does not alter rendering. Assistant, native-session and
both provider suites passed ASan/UBSan with leak detection in 18.28 s.

Task tests verify bounded structured choices, distinct host identities, no Apply
or provider request while waiting, matching one-time answers, free-text policy,
invalid/duplicate answers, unchanged command authorization, malformed/unadvertised
questions, rejection of mixed question/operation replies, cancellation of private
staging, expiry without budget reset, and stale human edits.

OpenAI and Ollama transport fixtures both deliver real structured questions to
the task, wait across multiple transport poll ticks without another request, then
resume with the original accepted provider output and a matching host answer
receipt. Ollama retains its successful capability probes. These tests use injected
network replies; they make no live model-quality or credential claims.

Source packaging and installation pass. Native cards, viewport picking and the
rest of the R046 panel remain subsequent integration work.
