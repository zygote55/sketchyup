# R086.at — Provider scope decision

2026-10-11. Records the owner decision on scope row A05; no code or evaluation
changed. The decision and its reasoning are in
[ADR 0160](../decisions/0160-provider-scope-for-1-0.md).

A passing local model configuration is not a 1.0 requirement. The supported
provider for 1.0 is the remote OpenAI connection through a ChatGPT plan sign-in,
which still has to pass the frozen corpus at the release candidate. Local profiles
stay documented and experimental. The earlier evaluations
([R086.b](R086b-remote-evaluation.md) 24/24 remote, [R086.c](R086c-local-evaluation.md)
0/3 local, [R086.as](R086as-openai-inspection-batches.md)) are unchanged. The
sentence in R086.as that A05 needs an accepted local configuration is superseded.

Files changed:

- `docs/decisions/0160-provider-scope-for-1-0.md` (new) and its install line in `CMakeLists.txt`
- `docs/SCOPE.md`: A05 acceptance text
- `docs/SUPPORT_MATRIX.md`: OpenAI and local assistant rows
- `docs/PR_ROADMAP.md`: R086 status
- `docs/M9_ACCEPTANCE.md`: R086 row
- `docs/USER_GUIDE.md`: assistant section
- `docs/verification/R089a-defect-triage.md`: D-001 reclassified to Limitation; counts
- `docs/verification/R088d-core-parity-gaps.md`: A05 row and gap lists
