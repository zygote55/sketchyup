# Bounded OBJ and MTL text parsing

R075.a, 2026-10-07. The parsers are pure functions over captured UTF-8 bytes.
OBJ parsing requires explicit metres-per-unit and Y-up/Z-up options; Y-up becomes
native `(x,-z,y)` through a proper rotation. Positive indices address the complete
vertex list; negative indices resolve against the list at the statement position.
Face/line corner attributes must be consistent. Concave polygon loops remain
ordered for native validation and triangulation in the following conversion layer.

OBJ supports `v` (optional weight exactly one), two-dimensional `vt`, `vn`, `f`,
`l`, `o`, `g`, `s`, `usemtl` and `mtllib`. Texture V converts from bottom-left to
native top-left coordinates. Object, sorted group memberships, smoothing groups and
material names remain associated with each element. Other statements are counted
by kind; a file without supported faces/lines rejects. `csh` and `call` reject
explicitly. No input can execute a process or recursively include files.

Limits: 64 MiB UTF-8 input; one million physical lines/statements; 64 KiB logical
lines; 100,000 positions; 300,000 normals or UVs; 100,000 faces/lines; 256 corners
per element and 500,000 corners in aggregate; 10,000 state changes used by elements;
16 group names per state; 64 material libraries and unsupported statement kinds.
Names are bounded to 512 UTF-8 bytes. Finite units range from 1e-6 to 1e6 metres;
converted positions must fit native coordinate limits. Zero/out-of-range indices,
malformed slash references, invalid Unicode, NUL and incomplete continuations reject.

The MTL subset retains `newmtl`, linear RGB `Kd`, uniform `d`/`Tr`, and `map_Kd`
with two-dimensional scale/offset and repeat addressing. Conflicting `d`/`Tr`
values reject. Unsupported map options or clamp addressing omit that mapping with
an explicit notice; other shading statements are counted. MTL is bounded to 4 MiB,
100,000 physical lines, 1,024 uniquely named materials and the same logical-line,
name and unsupported-kind limits. Texture references are captured, not loaded.

Source references: the original Wavefront Advanced Visualizer Appendix B1
[OBJ manual mirror](https://github.com/Alhadis/language-wavefront/blob/master/docs/obj-spec.pdf)
and the Alias/Wavefront 1995 [MTL manual transcription](https://github.com/Alhadis/language-wavefront/blob/master/docs/mtl-spec.rst).
These document a larger language than this supported subset. Native geometry,
contained asset resolution, loss reports, export packaging and UI/CLI are later
R075 layers; parser acceptance alone does not complete interchange.
