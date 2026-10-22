# IFC fixtures: sources and notices

Fixtures are **untrusted input**: they are read by `tests/ifc_worker_probe_tests.py` and the later
IFC suites, never executed. None is installed in the package.

## buildingSMART International (CC BY 4.0), unmodified

| File here | Source file in [buildingSMART/Certification-datasets](https://github.com/buildingSMART/Certification-datasets) (commit `80d976a9`, 2026-09-04) | SHA-256 |
| --- | --- | --- |
| `bsi-ifc2x3-arch.ifc` | `Simple-Scene/IFC2X3/Building-Architecture.ifc` (headed `ViewDefinition [CoordinationView_V2.0]`) | `c4db65ba847f6b369a95d6c54fa11f4750cbe6d59f021934e8923d8d578e5885` |
| `bsi-ifc4-arch.ifc` | `Simple-Scene/IFC4/Building-Architecture.ifc` (`ViewDefinition [ReferenceView_V1.2]`) | `8790a1e193e82b8e7e7f337ec2633cd40f2120590317a1443503a25b079e2e80` |
| `bsi-ifc43-arch.ifc` | `Simple-Scene/IFC4.3/Building-Architecture.ifc` (`ViewDefinition [ReferenceView]`) | `96e7103812b15b65eb1fd7802d36b9962833485dce5ead4f440bbf529530881d` |
| `rv-wall-with-opening-and-window.ifc` | `ISO Spec - ReferenceView_V1.2` folder, wall with opening and window | `73b0e45d931d5dc13bfee5fdc7bd80f796526445458b2de74c4168d209097832` |
| `rv-tessellated-item.ifc` | same folder, tessellated item | `758974f7d558f11b8d8121816954179def32594ed7eafcd1faae70ee0a8e5946` |
| `rv-tessellation-with-individual-colors.ifc` | same folder, tessellation with individual colours | `3ab1290207d8538da9d8ebc14826b769eeb79fce7d75aaa85c35ed7a5a8dbd92` |

Author: buildingSMART International Ltd. The repository's licence is Creative Commons Attribution
4.0 International (<https://creativecommons.org/licenses/by/4.0/>), "(C) buildingSMART
International Ltd.". The files are redistributed **unmodified** (the hashes above are of the
downloaded files). The three Architecture files name `Sketchup-IFC-manager 5.6.0 / SketchUp 2026` as
the writing application; the Reference View files' headers name RDF Ltd. (2011) as the author of
those samples. Community sample files are not used (buildingSMART's own README says most do not
pass validity checks and their licences are per contributor).

IFC(TM) and ifcXML(TM) are trademarks of buildingSMART International Limited. SketchyUp is not
certified by buildingSMART and claims no view or certification (ADR 0171, section 2).

## Original fixtures (MIT, the project's own)

`units-*.ifc`, `placement-mm.ifc`, `openings.ifc`, `openings-hard.ifc`, `failures.ifc`,
`contexts.ifc` and `units-mm-projectlibrary.ifc` are written by `make_original_fixtures.py` (run it
with `python3 -I`, one output directory argument); the volumes the tests expect are computed by hand
in its comments. `tests/ifc_worker_probe_tests.py` regenerates them and fails if a checked-in file
differs.

## Packages (derived from the files above)

`packages/*.skifc` are IFC conversion packages (`src/io/ifc_package.hpp`) that `sketchyup-ifc-worker --import
FILE --output PACKAGE --threads 1` writes from `rv-wall-with-opening-and-window.ifc`, `rv-tessellated-item.ifc`
and `rv-tessellation-with-individual-colors.ifc`: the geometry, names and properties of those buildingSMART
files (CC BY 4.0, (C) buildingSMART International Ltd.) in the package form, converted to metres. They are the
real inputs of the package decoder test and of the fuzz seeds; `ifc_import_worker` fails when the helper's
output differs, and the files are then regenerated on purpose.
