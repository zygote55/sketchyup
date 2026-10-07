# Frozen performance textures

These MIT-licensed test assets are the two 4096×4096 RGBA checker fixtures from
`real_model_benchmark`. They are stored as fixed PNG bytes because the same Qt
image-generation code produces different compressed PNG streams on the Intel
and AMD test systems, despite identical decoded pixels. Embedding the original
AMD bytes preserves the already recorded texture-container hash across platforms.

Each 64×64 checker cell alternates green 40/180; the front image has red 220,
blue 30, and the back image red 30, blue 220. Alpha is always 255. DPI metadata
is zero. Each decoded image is exactly 64 MiB. No scene or texture dimensions
were reduced to obtain matching hashes.

| File | PNG SHA-256 | Decoded RGBA SHA-256 |
| --- | --- | --- |
| front.png | `f54b21aa7880121dedf98bfc6ef153c204c36732fafb9bb762d415138d4e5b40` | `c8ae810260231b45c06c30d46209b76a06238696bfff4a7f7f1c1d585b1345d1` |
| back.png | `29ce13972c226f1de25e70dacfe38f7299ca4eb6d5faf2ed6de4b3474d144747` | `a90c0d0976215277a31b5204f25972c47d29c2355bf7fede54ba518a67b3fa92` |
