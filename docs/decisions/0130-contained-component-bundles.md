# Contained component bundles

R079.b, 2026-10-07. Component libraries use the same bounded, hash-verified
single-file envelope as template bundles (ADR 0129). The manifest kind is
`component` and the canonical nonzero decimal `definition` replaces `defaultScene`.
Template and component readers reject the other kind. All length, metadata,
thumbnail, native validation and atomic new-destination rules remain identical.

Capture follows the selected definition's acyclic nested references and includes
only their canonical records, assigned tags and parent folders, assigned front/back
materials and embedded material/reference-image assets. Unrelated model geometry,
missing unrelated assets, other resources, scenes, sections, annotations and hosted
attachments are excluded. Geometry remains in metres; the source display unit is
retained without rescaling. Component glue metadata is retained as part of canonical
definitions; document-specific hosted relationships are not library dependencies.

A bundle contains one identity-transform root instance and its resolved nested
instances. Decoding rejects unrelated definitions/resources, extra geometry or root
placements and document-specific records. The native reader validates the complete
reference graph and canonical-to-resolved bindings before the library reader accepts
it. Missing referenced assets reject before publication. Source records and history
are never edited by capture.

Tests cover nested references, dependency closure, an unrelated missing asset,
source immutability, strict kind separation, malformed bytes, a relocated single-file
library and existing-file protection. Insertion and duplicate-resource policy are
implemented in the following layer rather than inferred from successful decoding.
