# 0151 — Independent interface text size

View → Interface text size offers 75%, 100%, 125%, 150%, 175% and 200%,
independently of display scaling. The `interfaceTextPercent` preference defaults
to 100%; malformed or unsupported values fall back without rewriting the stored
value. Cancel does not write. Accept applies immediately and persists for the next
window; model contents, history and unrelated settings are unchanged.

The native stylesheet scales ordinary text, section headings and navigation hints.
The tool rail and Measurements field grow with the preference. Command search
uses a compact label at narrow widths while retaining its shortcut in the tooltip.
Header/status text elides through Qt font metrics while keeping its complete label
text available to accessibility. Breadcrumbs and wrapped hints recompute geometry
after font/layout changes. Outliner and Tags buttons reflow into one to three
columns according to actual size hints. Panel and assistant widths remain bounded
by available window space.

The native fixture exercises every offered size, cancellation, invalid persisted
values and a second window, then checks 200% in both themes at logical widths
640, 900, 1200 and 1600. It asserts actual font pixels, unchanged device pixel ratio,
Measurements visibility, viewport width, breadcrumb/command fit, Outliner/Tags
button fit and hint height. Optional captures support visual inspection. Settings
are private to the test. This bounded set does not establish every panel/dialog,
text length, minimum height or screen-reader workflow in the wider R084 audit.
