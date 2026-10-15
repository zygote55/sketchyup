# R092.d — Sheet export and print: acceptance at the 1.1.0 RC

2026-10-15. This record checks the R092 Verify line against the release candidate `main`
at `093484f941fb3439b5033b2d9fa5fae951a2c09c` (merge of PR #331). That is the same commit
and artifacts as the [1.1.0 RC walkthrough](F01-rc-1.1.0.md). [Summary](R092d-sheet-export.json).

The first candidate, `251fd52d`, also passed every check below. It was superseded because
its provider corpus run failed (D-035, PR #331). PR #331 changes only the in-app
assistant's tool list, its test and documents; no export, preflight or print code changed.
Every check was run again at the new RC.

R092 adds batch PDF and print output for drawing sheets ([ADR 0164 "R092 export"](../decisions/0164-sheet-layout-interface.md#r092-export)):
R092.a, the headless engine, preflight, reconciliation report and `sketchyup-cli
--export-sheets` (PR #325); R092.b, the desktop export dialog with live preflight, the
export report sheet, viewport raster views and Print at 100% (PR #327); and R092.c, the
1.1.0 release preparation (PR #328). The Verify line is:

> Measured scale, page count, clipping and annotation fixtures match output; missing
> fonts/references are reported before publishing.

**Result: pass.** Every check below ran at the RC, and none failed.

## Evidence at the RC

| Source | Where it ran | Result |
| --- | --- | --- |
| `sheet_export` (ctest) | CI run [38807074119](https://github.com/zygote55/sketchyup/actions/runs/38807074119): development, sanitized (bounded CLI step) and package jobs; locally with `-V` | Pass in all four. Locally it prints its sections: pages and paper sizes, real text and clipping, preflight findings, atomic and read-only, and the CLI round trip |
| `sheet_export_real` (ctest, `SKETCHYUP_PDF_TEST=/usr/bin/pdftoppm`) | CI "Independent measured PDF consumer" step; locally with `-V` | Pass in both ("poppler scale, text and clipping: ok") |
| `sheet_export_input_tests` | CI: X11 and Wayland at 1×, 2× and 1.5. Locally: X11 and Wayland at 1.5 | 8/8 pass. Each run printed a passing summary |
| `sheet_acceptance_input_tests` | Same cells | 8/8 pass |
| Installed CLI on the retained fixture | Both clean machines in the RC walkthrough (`drawing-sheets-v27.sketchyup`), checked with the host's Poppler | Pass |

## Verify line

**Measured scale.**
- `sheet_export_real` renders page 1 of its fixture with Poppler at 254 dpi (10 px/mm). It
  finds the stroke centres of the 2 m slab drawn at 1:50, which must be 40 mm apart on paper
  to within 0.1 mm. It passed.
- The report lists each view's scale. The PDF's attached `sheet-export-report.json` gives
  page 1's view as 1:50. The `scaleSummary` finding counts the views at each measured scale.
- Print at 100% (`sheet_export_input_tests`): the first printed A3 page is rasterized at
  254 dpi. The margin frame must be 10 mm in from the edge and 400 mm wide, to within
  0.2 mm. All eight runs reported `marginFrameMm: 400`.
- A sheet smaller than the paper is not scaled: the print dialog warns `paperSmaller`
  instead.

**Page count.**
- `sheet_export`: two pages, with one media box each. Page 1 is A3 landscape and page 2 is
  A4 portrait, at actual size (within 0.5 pt). The report lists both pages, with their paper.
- Page ranges select only their pages: by number, by position, `..` and en-dash ranges, and
  lists. Unknown or reversed ranges are `INVALID_REQUEST`.
- `sheet_export_real`: Poppler renders A3 landscape and A4 portrait at the expected size.
- `sheet_export_input_tests`: `pdfinfo` counts three pages in all eight runs.
- The installed CLI on the v27 fixture wrote 2 pages (A3) on both clean machines, matching
  the export report. The file's SHA-256 matches the report.

**Clipping.**
- `sheet_export`: the 1:20 detail's display-list primitives are clipped to the 60 × 60 mm
  view frame.
- `sheet_export_real`: on page 2 rendered by Poppler, the detail's overflowing slab edges
  leave no dark pixel outside the frame, apart from its caption and the margin frame.

**Annotation fixtures match output.**
- `sheet_export_real`: `pdftotext` extracts the dimension value ("2000"), "General notes",
  the title-block fields ("House", "Ground floor plan", "A-101", "1 of 2") and the scale
  ("1:50"). `pdffonts` shows TrueType Identity-H fonts embedded with Unicode maps and no
  Type 3 fonts.
- `sheet_export`: the text is real text with embedded subsets, and no font fell back to
  outlines.
- `sheet_export_input_tests`: the exported set contains "General notes", "House", "Ground
  floor plan", "1:50", "A-101" and "Section B-B" as text.
- The installed CLI on the v27 fixture: the extracted text holds the general notes, the
  dimension values, the view captions with their scales and the title block. The broken
  dimension prints as "[Missing reference]", and the broken view as "Scene was deleted".

**Missing fonts and references are reported before publishing.**
- `sheet_export` preflight findings:
  - a deleted scene gives `viewBroken`;
  - changed views give `viewChanged`;
  - a raster view without a renderer gives `rasterUnavailable`;
  - an absent family ("No Such Font R092") gives `fontSubstituted`.
- A blocked export throws `EXPORT_BLOCKED`, carries its findings and leaves no file. The
  opt-ins name the views exported as they are, or as empty frames, in the report. The
  inspection query `drawing_sheets.export_preflight` runs the same preflight.
- The CLI round trip: `--preflight-only` writes nothing. A broken view refuses the export
  with `EXPORT_BLOCKED` and its findings until `--allow-broken`. An existing file is never
  replaced.
- `sheet_export_input_tests`, in the desktop preflight dialog:
  - "1 broken view blocks export. A-301, view 1 “Section B-B”: its scene …";
  - "3 views changed since accepted …";
  - "1 font is not installed. “ISOCPEUR” …".
- Export PDF stays disabled while a finding blocks it, and each opt-in unblocks only its
  own finding. All eight runs report `blocked: true` with `viewBroken`, `viewChanged`,
  `fontSubstituted`, `scaleSummary` and `rasterViews`. The report sheet lists the font
  substitution and the empty frame.
- The installed CLI on the v27 fixture (both clean machines, and the RC `build/dev`):
  - Preflight reports `blocked: true`, with `viewBroken` ("page 2, view 1 “Detail A”: its
    scene was deleted"), `viewChanged`, `rasterUnavailable`, `fontSubstituted` ("“Liberation
    Serif” … will be replaced by DejaVu Sans"), `itemBroken` ("page 2 dimension 9 … Reattach
    it to fix this"), `scaleSummary` and `rasterViews`.
  - The plain export exits 1 with `EXPORT_BLOCKED` and writes nothing.

## Limits

- The PDF page box is rounded to whole points (within 0.18 mm). The measured scale is exact.
- Qt embeds font subsets without the "ABCDEF+" name tag, so `pdffonts` shows "sub no". The
  subset is checked by size in `sheet_export`.
- The CLI cannot render raster views, so they block until `--allow-broken` and print as
  labelled frames. The desktop renders them from the viewport.
- These are isolated Xvfb and Weston sessions with software rendering, and a PDF-backed
  QPrinter. No physical printer or paper measurement was made. ADR 0161 leaves that to the
  owner's daily use.

The evidence logs are in `~/sketchyup-verification/rc-1.1.0b/` and are not published. The
JSON summary lists their SHA-256.
