# PR183 diagnostic action readiness

The old DXF-workflow head `5bc48f3ab76a9dcd4cbc315c6eb3529dcb2c8ca0` failed its [sanitized native diagnostic check](https://github.com/zygote55/sketchyup/actions/runs/37607687870): after clearing an assistant preview, the fixture waited a fixed250ms and checked the repair button before its asynchronous enabled state had updated. The report still showed the expected active-preview guard. Other successful workflows on that same head do not waive this failure.

Backport the existing qualified readiness change from `c09035b81f1c9e484a183214960b7063613e44e2` to candidate `3214c6caebdc97dae0640d7ae525b381936224bf`: wait up to five seconds for the actual repair button to become enabled. Production code, interaction assertions, native test deadlines and guards remain unchanged. No forced enabled state or blind retry is added.

All eight fresh checks passed: X11/Wayland at1×/2×, normally and under ASan/UBSan with leak detection enabled. They use the font-equipped disposable image and SHA-frozen Wayland client; the original missing-font image and prior readiness failure remain separate evidence. The complete807-input source snapshot matches the published application/test inputs byte for byte. Fresh full CI and ordered M8 acceptance remain required.

[Source, binary, raw log and result identities](CI-PR183-diagnostic-readiness.json).
