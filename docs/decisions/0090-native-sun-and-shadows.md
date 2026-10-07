# Native sun studies and shadow preview

R067.d, 2026-10-06. View → Sun and shadows opens the Sun tray. Its editor exposes
sun lighting and shadow toggles, latitude/longitude, local date/time, explicit UTC
offset and clockwise model north rotation. The UTC offset includes the user's
chosen daylight-saving adjustment; it is not inferred. Untouched coordinates retain
exact stored precision. Save uses one shared atomic command; invalid or stale input
stays in the editor, Cancel publishes nothing, and an unchanged editor has no history.

The tray reports the stored study and deterministic solar elevation/azimuth with
geometric horizon state. New saved scenes opt into the study alongside other view
properties; users may uncheck it. Existing scenes without solar state leave it alone.
Native scene recall and Undo preserve exact settings and update lighting immediately.

When enabled, the viewport shades each physical face side using transformed smooth
normals and the model's sun direction. Reflected placements retain correct physical
front/back lighting. Ambient light remains at night, with no direct sunlight or
shadows below the geometric horizon. Disabling sun lighting restores the existing
modeling light. Model appearance, topology and picking identities do not change.

The preview uses one bounded 2048×2048 depth map and a 3×3 comparison filter. It fits
visible caster geometry in a sun-aligned basis, using double-precision camera-relative
coordinates before GPU upload. A camera-target neighborhood (twice camera distance,
expanded for wide viewports) bounds the projected XY footprint so distant offscreen
objects cannot consume nearby shadow resolution. Triangle bounds are intersected with
that footprint; casters anywhere along the sun axis still contribute when their
projection overlaps it. Shadow detail is a preview around the camera target, not an
unbounded scene-wide map. Visible ground extends the receiver depth range.
Opaque surfaces and texture/opacity fragments with alpha at least 0.5 cast shadows;
this is an opaque-cutout preview, not refractive or colored transmission. Ground is
a visual receiver and remains non-pickable. Free clipping and named section geometry
(including caps) affect the shadow pass. Hidden geometry contributes no caster.

Shadows appear in textured, shaded and monochrome modes; wireframe and X-ray retain
their existing display semantics. Fixed map resolution bounds memory and entails the
usual preview resolution/bias limits at very large spans. The framebuffer, viewport,
texture-unit and shader state are restored before ordinary drawing and picking.
OpenGL resources are owned by the viewport and released with its context.

The native preview does not yet change Blender's lighting adapter. Handoff settings
and the explicit unapplied-lighting transfer report remain as in contracts 0088–0089;
R069 implements that conversion. This study requires no map service or AI provider.
