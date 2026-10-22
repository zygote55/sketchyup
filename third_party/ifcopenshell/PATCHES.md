# Local patches to IfcOpenShell 0.9.0

None. The vendored tree is the upstream tag `ifcopenshell-v0.9.0` (commit
`7ba701972f7182322e0a00db8f0e1f3de5f69840`) trimmed by `TRIM.txt`, byte for byte:
`scripts/vendor-ifcopenshell.py check ARCHIVE` fails on any difference from the trimmed upstream
archive.

A change the build needs is recorded here, with the file, the hunk and the reason, marked in the
file with a `// SketchyUp patch:` comment (as `third_party/clipper2/PATCHES.md` does), and the
script's check is extended in the same change. Patches are published with the source
([`IFC-SOURCE.txt`](IFC-SOURCE.txt)).
