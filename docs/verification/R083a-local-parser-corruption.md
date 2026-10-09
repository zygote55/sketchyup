# R083.a local parser-corruption verification

2026-10-07. Fixture work overlaps M8 integration; R083 and M9 are not accepted.
Source `88e2991` executes **5,120 mutations across ten formats**. The normal gate
passes in **1.27 s** and ASan/UBSan in **21.54 s**, with leak detection and
halt-on-error. [Corpus results](R083a-parser-corpus.json), including all original
seed hashes, are identical across both runs. Accepted inputs retain parser
invariants; rejected inputs do not mutate the source. No sanitizer finding
occurred in this bounded corpus.

The first runs passed but revealed that component capture intentionally creates a
random standalone document identity. The fixture now normalizes only that identity
and its envelope hash before mutation, making the exercised bytes reproducible.
Application capture behavior is unchanged.

The [contract](../decisions/0142-deterministic-parser-corruption.md) states coverage
and limits. Continuous fuzzing, filesystem/journal fault combinations and the
complete release hardening gate remain subsequent work. Remote CI, dependency
merges and accepted milestone prerequisites remain required.
