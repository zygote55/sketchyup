# Frozen sun studies in Blender renders

R069.a, 2026-10-06. The worker consumes the immutable solar settings and evaluated
native-world sun direction already captured in the render manifest. It does not
recompute an astronomical position or consult host time, location or current model
state. The native NOAA/Meeus algorithm and north rotation remain authoritative.

An enabled study selects `solar-v1`: one Blender SUN points toward the captured unit
direction when above the geometric horizon. It uses energy 3, angular size 0.00935
radians and white ambient world strength 0.25. These fixed presentation values are
not a physical irradiance prediction. Below the horizon the ambient world remains,
with no direct light. The captured shadows flag controls Cycles sun shadows.
Disabled studies and pre-sun snapshots retain the existing two-area-light `studio-v1`.
Invalid directions, algorithm identities or inconsistent daylight flags fail.

The worker reports its exact lighting policy, captured inputs, daylight state and
applied values. The desktop verifies that report and the complete transfer-loss
object against the captured source before publishing pixels. A solar render clears
only `solarLightingOmitted`; a standalone surface-only GLB still reports that loss.
The render panel identifies Sun study or Studio alongside the actual device.

Actual Cycles checks verify analytical east/north shadow locations, disabled shadows,
ambient-only night, legacy/studio compatibility, immutable inputs and invalid-state
rejection. The real worker lifecycle also renders a captured native sun study while
the live document changes and confirms its source GLB remains byte exact. Existing
camera, texture, sided-material, section and normal conversion checks remain in force.
HDRI, preview engines and further material capabilities are subsequent R069 layers.
