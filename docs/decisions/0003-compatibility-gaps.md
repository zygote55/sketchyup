# ADR 0003: initial SKP and DWG delivery decision

Date: 2026-10-03. Roadmap: R005. Status: research recorded; unsupported formats.

The Core editor will not link a proprietary file SDK in this initial build.
This records gaps rather than removing C01/C05 from the roadmap.

## SKP

The [official C API documentation](https://extensions.sketchup.com/developers/sketchup_c_api/sketchup/index.html)
describes Windows DLL and macOS framework build/distribution paths. It does not
establish a supported native Linux delivery path for this application. We have
not licensed, downloaded or tested that SDK, and have no SKP fidelity corpus.
A vendor-supported Linux option or separately evaluated library, permitted
redistribution, and versioned sample files are required before R102 can deliver
support. Do not equate a Windows converter with native Linux SKP support.

## DWG

ODA supplies a [Linux file converter](https://www.opendesign.com/guestfiles/oda_file_converter)
and publishes [Drawings SDK information](https://www.opendesign.com/files/2024-10/Drawings%20Datasheet.pdf).
Those are candidate paths, not evidence that this project can bundle an SDK or
preserve DWG features. No membership agreement, redistributable SDK selection,
version matrix or round-trip corpus has been established here. R106 remains
conditional on those decisions. A user-managed external conversion path may be
investigated separately; it is not installed or invoked by this application.

The independent DXF subset in X04 remains planned. None of SKP, DWG or DXF is
currently an advertised import/export capability of the native spike. Core
work can proceed while these compatibility gates remain unsupported.
