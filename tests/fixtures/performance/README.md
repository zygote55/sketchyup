# Frozen performance textures

These MIT-licensed test assets are the two 4096×4096 RGBA checker fixtures from
`real_model_benchmark`. They are stored as fixed PNG bytes because the same Qt
image-generation code produces different compressed PNG streams on the Intel
and AMD test systems, despite identical decoded pixels. Embedding the original
AMD bytes preserves the already recorded texture-container hash across platforms.

Each 64×64 checker cell alternates green 40/180; the front image has red 220,
blue 30, and the back image red 30, blue 220. Alpha is always 255. Density metadata is fixed at 3780 pixels/metre on both axes (approximately
96 DPI); Qt ignores a request to set zero density. Capturing with a core-only
application initially produced 3937 pixels/metre, so the final capture uses the
same GUI application context as the benchmark. Each decoded image is exactly 64 MiB. No scene or texture dimensions
were reduced to obtain matching hashes.

| File | PNG SHA-256 | Decoded RGBA SHA-256 |
| --- | --- | --- |
| front.png | `bef19ea75d4f1b1a636d6db100f1f0b32c89cc0f6f5301af159f8b27b31e940b` | `c8ae810260231b45c06c30d46209b76a06238696bfff4a7f7f1c1d585b1345d1` |
| back.png | `dc7164dbfa82e0992f6d75fd5287e3fd17226fdbb30cb2142244d0c3a6b9c268` | `a90c0d0976215277a31b5204f25972c47d29c2355bf7fede54ba518a67b3fa92` |
