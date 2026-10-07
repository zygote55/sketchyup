# Shared sun study authoring and inspection

R067.c, 2026-10-06. `document.solar` replaces the document's complete explicit sun
study through the shared atomic command path. Its `solar` parameter contains the
strict settings from contract 0088; there are no inferred location, time-zone or
current-time defaults in this command. Civil calendar validity is checked at execution
in addition to the published numeric/type bounds. Invalid requests reject before
publishing any state. A batch that makes no changes uses the existing no-change
rejection contract and adds no history.

Preview and private staging retain the prospective study without changing the live
model. A staged study produces one `solar` change row. Publish is revision-guarded,
Undo restores the prior study, and later batch failures roll the study back with
other commands. Document-level settings cannot be edited in a component scope.

`solar.describe` is read-only and bounded. It returns saved settings, the pinned
algorithm's resolved UTC instant and solar position, geometric horizon classification,
direct-light/shadow activation flags and the explicit-offset policy. It uses the same
identity/revision envelope as other inspection queries. Document descriptions also
include the saved study. Inspection does not consult an OS time zone or provider.

Saved-scene create/update accepts an optional complete `solar` property, including
solar-only scenes. Summaries identify it and descriptions return it. Recall applies
opted-in solar state with other saved model properties in one history edit. The
sun-study example supplies three explicit studies (morning, noon and evening) at
40° N, 105° W on 2010-06-21, and ends with noon recalled. Geometry remains native.

The seven executable capability catalogs describe the command and query across
headless sessions, shared transactions and native read-only inspection. Native
controls and viewport rendering follow; Blender lighting conversion remains R069.
