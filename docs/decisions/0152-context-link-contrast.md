# 0152 — Explicit rich-text context link colors

QLabel rich-text links can retain default blue under the application stylesheet,
even after setting QPalette::Link. Context breadcrumbs and shared-component
banner links therefore receive explicit anchor colors from the active theme.
Original application-authored markup is retained separately so theme switches
replace styling rather than accumulating attributes. Callers continue escaping
model names; target links and keyboard interaction are unchanged.

The native theme fixture measures the dominant painted foreground in the root
breadcrumb image against its effective background. This catches a mismatch between
palette metadata and actual rendered link text. Solid glyph interiors/underlines
dominate the color sample; antialiased edge pixels are not treated as the requested
text color. Both theme ratios must exceed 4.5:1, and existing input/button/hint
checks still run. Broader overlay, focus and assistive-technology audits remain open.
