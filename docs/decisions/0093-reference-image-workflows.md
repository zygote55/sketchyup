# Shared reference image workflows

R068.c, 2026-10-06. `reference_image.create` places an existing managed asset using
an explicit name, physical width and height. Opacity defaults to one; position and
a group parent are optional. No pixel metadata infers a physical size. Asset import
and reference creation can share one atomic batch, staging proposal and Undo entry.

`reference_image.update` replaces specified name, asset, dimensions or opacity;
at least one property is required. Equal values use the ordinary no-change batch
rejection. `reference_image.calibrate` takes two normalized top-left image points
and a known world length in meters, publishing the anchored result from contract
0091. Invalid inputs, locks and later batch failures publish nothing. Generic entity
placement, grouping, copying and deletion operate on the whole image body.

The same commands work in explicit component-definition and instance edit scopes.
Editing one instance makes it unique through the existing shared scope machinery;
it does not change other placements or grant access to global asset mutations.

`reference_images.query` pages image metadata, physical settings, world corners and
managed-asset availability without decoding every image. `reference_image.describe`
decodes one bounded asset through the existing PNG/JPEG decoder and reports status
and pixel dimensions, including explicit missing, unsupported, invalid or oversized
content. It does not return image bytes or mutate document state. Both queries retain
the ordinary identity/revision envelope and response limits. `entities.query` can
filter the distinct `reference_image` kind.

The installed reference-grid example embeds a deterministic 128×64 PNG, places a
4×2 meter image and calibrates its full width to 8 meters while fixing its lower-left
anchor. A separate native modeled block demonstrates the distinction between image
planes and editable surfaces. The source script and saved model require no external
asset paths. Native display and image controls, followed by exact raster export,
remain subsequent R068 layers.
