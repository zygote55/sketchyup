# External checks of the IFC export (R097.d, ADR 0171 check G1)

The golden exports in `tests/fixtures/ifc/export` and any file `sketchyup-cli --export-ifc` writes can be read by programs that share
no code with SketchyUp's helper. These scripts run three of them. None is a CI gate (ADR 0171, 6.2 point 5): they download code, so each
tool goes in its own new, empty directory and the scripts run with `python -I`.

| Script | Tool | What it shows |
| --- | --- | --- |
| `validate_ifcos.py FILE [N]` | `ifcopenshell.validate` with EXPRESS rules (PyPI `ifcopenshell`, `pytest`) | the schema's WHERE rules; the same engine as the helper, so a consistency check |
| `webifc_read.mjs DIR FILE` | web-ifc (npm `web-ifc`, installed in DIR) | a second reader: schema, meshes, triangles, bounds |
| `run_gherkin.py REPO PYTHON FILE OUT` | buildingSMART `ifc-gherkin-rules` (git clone with submodule; PyPI `behave`, `ifcopenshell` and the requirements of the repository) | the implementer agreements and informal propositions of the validation service, run locally |

The hosted validation service (<https://validate.buildingsmart.org>) needs a signed-in account and is run by a person.

Results for the pull request that added these (R097.d) are in the implementation notes of ADR 0171.
