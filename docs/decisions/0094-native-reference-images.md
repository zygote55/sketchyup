# Native reference image display and editing

R068.d, 2026-10-06. The viewport derives a two-triangle display plane from each
reference-image record. These triangles have no modeled face identity and never
enter the document's surface, geometry counts, inference or face-editing tools.
Selection addresses the whole image. Frame-all and entity bounds include its four
placed corners, including affine placement and parent transforms.

Image row zero maps to the top of the plane: normalized (0,0) is local (0,height,0).
Both sides display the same image coordinates. Managed PNG/JPEG decoding uses the
existing asynchronous bounded texture cache. Reference pixels clamp at image edges,
retain their raster colors in all model styles, and remain unlit. Stored opacity and
pixel alpha participate in depth/transparency and CPU/GPU picking. Fully transparent
pixels do not intercept selection. Ordinary hidden/tag/lock/context presentation
continues to apply. Reference planes neither cast nor receive sun shadows.

Free clipping and named sections clip derived triangles and interpolate their UVs.
Image planes generate no section caps or cut edges. Missing, invalid, unsupported or
budget-limited image pixels display a purple plane with an unavailable-image notice;
no filesystem or network lookup is attempted. The viewport's existing sorted-alpha
limitations also apply to overlapping transparent reference planes.

View → Reference images opens the Images tray. Import accepts a readable static PNG
or JPEG within the existing 16 MiB encoded, 4096-pixel-per-side and 64 MiB decoded
limits. The user supplies nominal width in document units; height follows pixel
aspect ratio. Placement starts on the current group/component context's local XY
plane at its origin. Embedded asset and placement publish atomically in one Undo
step. Ordinary Move, Rotate and Scale tools position the whole image.

The edit dialog changes name, dimensions and opacity, preserving untouched numeric
values exactly. Calibration takes two normalized top-left image coordinates and a
known length in document units. The first world point remains fixed and the image
aspect ratio is retained. Its default distance uses the placed world length; unchanged
calibration preserves exact values and publishes no edit. All operations use shared commands and their lock, bounds,
component-scope and atomic-history rules. Stale dialogs reject instead of overwriting
newer work. Unchanged property dialogs do not publish edits.

Raster export is a separate R068 layer. The viewport preview alone is not evidence
of exact-resolution image export.
