#!/usr/bin/env python3
"""Exercise only our private demo process through the Linux AT-SPI bridge.

Run under dbus-run-session and the native Wayland/X11 test environment. This is a
protocol/data check, not a claim that an interactive speech or braille session ran.
"""
import argparse
import ctypes as C
from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


class Bridge:
    def __init__(self):
        self.at = C.CDLL("libatspi.so.0")
        self.glib = C.CDLL("libglib-2.0.so.0")
        self.objects = C.CDLL("libgobject-2.0.so.0")
        pointer, integer = C.c_void_p, C.c_int
        signatures = {
            "init": ([], integer),
            "exit": ([], integer),
            "get_desktop": ([integer], pointer),
            "accessible_get_process_id": ([pointer, pointer], C.c_uint),
            "accessible_get_child_count": ([pointer, pointer], integer),
            "accessible_get_child_at_index": ([pointer, integer, pointer], pointer),
            "accessible_get_name": ([pointer, pointer], pointer),
            "accessible_get_description": ([pointer, pointer], pointer),
            "accessible_get_role_name": ([pointer, pointer], pointer),
            "accessible_get_selection_iface": ([pointer], pointer),
            "selection_select_child": ([pointer, integer, pointer], integer),
            "accessible_get_action_iface": ([pointer], pointer),
            "action_get_n_actions": ([pointer, pointer], integer),
            "action_get_name": ([pointer, integer, pointer], pointer),
            "action_do_action": ([pointer, integer, pointer], integer),
            "accessible_get_text_iface": ([pointer], pointer),
            "text_get_text": ([pointer, integer, integer, pointer], pointer),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.at, "atspi_" + name)
            function.argtypes, function.restype = args, result
            setattr(self, name, function)
        self.glib.g_free.argtypes = [pointer]
        self.objects.g_object_unref.argtypes = [pointer]
        self.glib.g_main_context_iteration.argtypes = [pointer, integer]
        self.glib.g_main_context_iteration.restype = integer
        assert self.init() == 0, "AT-SPI client initialization failed"

    @contextmanager
    def owned(self, pointer):
        assert pointer, "Required accessibility interface is unavailable"
        try:
            yield pointer
        finally:
            self.objects.g_object_unref(pointer)

    def string(self, pointer):
        if not pointer:
            return ""
        try:
            return C.string_at(pointer).decode("utf-8")
        finally:
            self.glib.g_free(pointer)

    def pump(self):
        for _ in range(100):
            if not self.glib.g_main_context_iteration(None, False):
                break

    def snapshot(self, node):
        remaining = 4096

        def visit(current, path):
            nonlocal remaining
            remaining -= 1
            assert remaining >= 0 and len(path) <= 24, "Accessibility tree exceeds audit bounds"
            count = self.accessible_get_child_count(current, None)
            assert 0 <= count <= 256, "Invalid or excessive accessible child count"
            result = {
                "name": self.string(self.accessible_get_name(current, None)),
                "description": self.string(self.accessible_get_description(current, None)),
                "role": self.string(self.accessible_get_role_name(current, None)),
                "path": path,
            }
            rows = [result]
            for index in range(count):
                with self.owned(self.accessible_get_child_at_index(current, index, None)) as child:
                    rows.extend(visit(child, path + [index]))
            return rows

        return visit(node, [])

    @contextmanager
    def at_path(self, root, path):
        pointers = []
        try:
            current = root
            for index in path:
                current = self.accessible_get_child_at_index(current, index, None)
                assert current, "Accessible object disappeared during the audit"
                pointers.append(current)
            yield current
        finally:
            for pointer in reversed(pointers):
                self.objects.g_object_unref(pointer)


def find(rows, name, role=None):
    matches = [row for row in rows if row["name"] == name and (role is None or row["role"] == role)]
    assert len(matches) == 1, f"Expected one accessible {name!r} ({role}), found {len(matches)}"
    return matches[0]


def wait_for(bridge, condition, message):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        bridge.pump()
        result = condition()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError(message)


def sheet_fixture(cli, root, environment):
    """Writes a document with one page holding a title block and a text item (R091.g)."""
    def ref(path):
        return {"$ref": path}
    document = ref("info#/result/documentId")
    commands = [
        {"command": "drawing_sheet.create", "name": "Ground floor", "number": "A-101",
         "paper": {"widthMm": 420, "heightMm": 297}},
        {"command": "drawing_item.create", "sheet": "1", "kind": "titleBlock",
         "data": {"frame": {"xMm": 250, "yMm": 257, "widthMm": 160, "heightMm": 30},
                  "fields": {"project": "House extension"}}},
        {"command": "drawing_item.create", "sheet": "1", "kind": "text",
         "data": {"frame": {"xMm": 240, "yMm": 160, "widthMm": 60, "heightMm": 10},
                  "text": "Notes"}},
    ]
    steps = [
        {"id": "info", "request": {"apiVersion": 1, "operation": "session.describe"}},
        {"id": "begin", "request": {"apiVersion": 1, "operation": "transaction.begin",
                                    "documentId": document,
                                    "expectedRevision": ref("info#/result/revision"),
                                    "history": {"label": "Sheet fixture", "taskId": "a11y-sheets",
                                                "assistant": False}}},
        {"id": "apply", "request": {"apiVersion": 1, "operation": "transaction.apply",
                                    "documentId": document,
                                    "transactionId": ref("begin#/result/transactionId"),
                                    "expectedVersion": 0, "operationId": "sheet-items",
                                    "commands": commands}},
        {"id": "seal", "request": {"apiVersion": 1, "operation": "transaction.preview",
                                   "documentId": document,
                                   "transactionId": ref("begin#/result/transactionId"),
                                   "expectedVersion": ref("apply#/result/stageVersion")}},
        {"id": "commit", "request": {"apiVersion": 1, "operation": "transaction.commit",
                                     "documentId": document,
                                     "requestId": ref("seal#/result/requestId"),
                                     "payloadHash": ref("seal#/result/payloadHash")}},
        {"id": "save", "request": {"apiVersion": 1, "operation": "document.save",
                                   "documentId": document,
                                   "expectedRevision": ref("commit#/result/result/revision")}},
    ]
    recipe = root / "sheets-recipe.json"
    recipe.write_text(json.dumps({"apiVersion": 1, "steps": steps}))
    output = root / "sheets.sketchyup"
    result = subprocess.run([str(cli), "--recipe", str(recipe), "--new", "--output", str(output),
                             "--outcomes", str(root / "outcomes")],
                            env=environment, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and output.exists(), \
        f"Sheet fixture recipe failed: {result.stdout[-2000:]} {result.stderr[-2000:]}"
    return output


def sheets_phase(bridge, application, root, environment):
    """R091.g: page items are visible over AT-SPI in the Pages tree and as canvas children."""
    fixture = sheet_fixture(application.resolve().parent / "sketchyup-cli", root, environment)
    with (root / "sheets.log").open("w") as log:
        process = subprocess.Popen([str(application.resolve()), str(fixture)], env=environment,
                                   stdout=log, stderr=log)
        try:
            with bridge.owned(bridge.get_desktop(0)) as desktop:
                def own_application():
                    assert process.poll() is None, "Sheets fixture exited before registration"
                    for index in range(bridge.accessible_get_child_count(desktop, None)):
                        child = bridge.accessible_get_child_at_index(desktop, index, None)
                        if not child:
                            continue
                        if bridge.accessible_get_process_id(child, None) == process.pid:
                            return child
                        bridge.objects.g_object_unref(child)
                    return None

                with bridge.owned(wait_for(bridge, own_application,
                                           "The sheets fixture did not register")) as app:
                    seen = []

                    def toggle():
                        rows = bridge.snapshot(app)
                        seen[:] = [(r["name"], r["role"]) for r in rows if "Sheets" in r["name"]]
                        found = [r for r in rows if r["name"] == "Sheets workspace" and
                                 r["role"] in ("push button", "toggle button", "check box")]
                        return found[0] if found else None

                    try:
                        button = wait_for(bridge, toggle, "Sheets workspace button is absent")
                    except AssertionError as error:
                        raise AssertionError(f"{error}; seen {seen}") from None
                    with bridge.at_path(app, button["path"]) as control:
                        with bridge.owned(bridge.accessible_get_action_iface(control)) as action:
                            names = [bridge.string(bridge.action_get_name(action, i, None))
                                     for i in range(bridge.action_get_n_actions(action, None))]
                            assert "Press" in names and bridge.action_do_action(
                                action, names.index("Press"), None), "Could not open Sheets"

                    def listed():
                        rows = bridge.snapshot(app)
                        names = {r["name"] for r in rows}
                        return rows if {"Title block · House extension", "Text · Notes"} <= names \
                            else None

                    rows = wait_for(bridge, listed, "Pages does not list the page items")
                    pages = find(rows, "Pages", "tree")
                    notes = find(rows, "Text · Notes", "table cell")
                    assert notes["description"] == "Current", \
                        f"The row's state is its description: {notes['description']!r}"
                    assert notes["path"][:-1] == pages["path"], "The item row belongs to Pages"
                    canvas = [r for r in rows if r["role"] == "canvas" and
                              r["name"].startswith("Page A-101")]
                    assert len(canvas) == 1, "The page canvas is exposed"
                    children = [r for r in rows if r["path"][:-1] == canvas[0]["path"]]
                    item_names = sorted(r["name"] for r in children)
                    assert item_names == ["Text · Notes · Current",
                                          "Title block · House extension · Current"], \
                        f"Canvas children name the items: {item_names}"
                    # Qt's tree selection counts the hidden header as a row that the child list
                    # leaves out: selecting child n selects the row of child n - columns. So the
                    # title block's row (above the text's) is selected through the child one row
                    # (two columns: number and name) further on.
                    columns = 2
                    block = find(rows, "Title block · House extension", "table cell")
                    with bridge.at_path(app, pages["path"]) as control:
                        with bridge.owned(bridge.accessible_get_selection_iface(control)) as selection:
                            assert bridge.selection_select_child(
                                selection, block["path"][-1] + columns, None), \
                                "AT-SPI could not select the title block's row"

                    observed = []

                    def described():
                        current = bridge.snapshot(app)
                        page = [r for r in current if r["role"] == "canvas" and
                                r["name"].startswith("Page A-101")]
                        observed[:] = [r["description"] for r in page] + \
                            [r["name"] for r in current if r["role"] == "label" and
                             r["name"].startswith("Entity info")]
                        return page[0] if page and page[0]["description"] == \
                            "Title block · House extension · Current" else None

                    try:
                        page = wait_for(bridge, described, "Selecting the row does not select the item")
                    except AssertionError as error:
                        raise AssertionError(f"{error}; observed {observed}") from None
                    return {"pagesItems": ["Title block · House extension", "Text · Notes"],
                            "canvasChildren": item_names,
                            "canvasDescription": page["description"],
                            "selectionRowOffsetColumns": columns}
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    assert os.environ.get("DBUS_SESSION_BUS_ADDRESS"), "Run under dbus-run-session"
    bridge = Bridge()
    try:
        with tempfile.TemporaryDirectory(prefix="sketchyup-accessibility-") as temporary:
            root = Path(temporary)
            config = root / "SketchyUp"
            config.mkdir()
            (config / "SketchyUp.conf").write_text("[General]\ndefaultUnits=m\nrecoverySeconds=0\n")
            # Without a terminal Qt sends its messages, qFatal included, to journald, which a CI
            # container lacks; force them into the application log that a failure prints (D-033).
            environment = dict(os.environ, XDG_CONFIG_HOME=str(root), XDG_DATA_HOME=str(root),
                               QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1", LC_ALL="C.UTF-8",
                               QT_FORCE_STDERR_LOGGING="1")
            # Logs are private fixture artifacts; never read settings from the user's profile.
            with (root / "application.log").open("w") as log:
                process = subprocess.Popen([str(args.application.resolve()), "--demo"],
                                           env=environment, stdout=log, stderr=log)
                passed = False
                try:
                    with bridge.owned(bridge.get_desktop(0)) as desktop:
                        def own_application():
                            assert process.poll() is None, "Demo exited before accessibility registration"
                            for index in range(bridge.accessible_get_child_count(desktop, None)):
                                child = bridge.accessible_get_child_at_index(desktop, index, None)
                                if not child:
                                    continue
                                if bridge.accessible_get_process_id(child, None) == process.pid:
                                    return child
                                bridge.objects.g_object_unref(child)
                            return None

                        with bridge.owned(wait_for(bridge, own_application,
                                                   "Our process did not register with AT-SPI")) as app:
                            def ready():
                                rows = bridge.snapshot(app)
                                return rows if any(r["name"] == "Model hierarchy" for r in rows) else None

                            rows = wait_for(bridge, ready, "Model hierarchy did not appear")
                            measurements = find(rows, "Measurements in meters", "text")
                            with bridge.at_path(app, measurements["path"]) as control:
                                with bridge.owned(bridge.accessible_get_text_iface(control)) as text:
                                    assert bridge.string(bridge.text_get_text(text, 0, -1, None)) == ""
                            hierarchy = find(rows, "Model hierarchy", "tree")
                            slab = find(rows, "Courtyard slab #1", "table cell")
                            assert slab["path"][:-1] == hierarchy["path"], "Fixture slab belongs to hierarchy"
                            with bridge.at_path(app, hierarchy["path"]) as control:
                                with bridge.owned(bridge.accessible_get_selection_iface(control)) as selection:
                                    assert bridge.selection_select_child(selection, slab["path"][-1], None), \
                                        "AT-SPI could not select the slab"

                            def selected_summary():
                                current = bridge.snapshot(app)
                                summary = find(current, "Selection summary", "label")
                                return current if "Courtyard slab" in summary["description"] else None

                            rows = wait_for(bridge, selected_summary, "Selected entity is absent from accessible description")
                            summary = find(rows, "Selection summary", "label")
                            tab = find(rows, "Info", "page tab")
                            with bridge.at_path(app, tab["path"]) as control:
                                with bridge.owned(bridge.accessible_get_action_iface(control)) as action:
                                    names = [bridge.string(bridge.action_get_name(action, i, None))
                                             for i in range(bridge.action_get_n_actions(action, None))]
                                    assert "Press" in names and bridge.action_do_action(action, names.index("Press"), None), \
                                        "AT-SPI could not open Entity info"

                            expected = ["8 m · 6 m · 0.15 m", "100.2 m²", "7.2 m³"]
                            def measured():
                                current = bridge.snapshot(app)
                                names = {row["name"] for row in current}
                                return current if all(value in names for value in expected) else None

                            rows = wait_for(bridge, measured, "Accessible Entity info measurements are incomplete")
                            # Leave the demo before the Sheets phase starts its own process.
                            process.terminate()
                            process.wait(timeout=10)
                            sheets = sheets_phase(bridge, args.application, root, environment)
                            report = {
                                "fixture": "built-in courtyard demo; private config/data and owned process only",
                                "platformRequested": environment.get("QT_QPA_PLATFORM", "default"),
                                "scope": "AT-SPI tree/text/selection/action interfaces and measured Entity info",
                                "measurementsInput": {key: measurements[key] for key in ("name", "role", "description")},
                                "selectionDescription": summary["description"],
                                "entityInfo": expected,
                                "sheets": sheets,
                                "accessibleNodes": len(rows),
                                "passes": True,
                                "releaseAcceptance": False,
                                "limitations": "No interactive speech/braille session or exhaustive state/dialog audit",
                            }
                            args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
                            print(json.dumps(report, ensure_ascii=False))
                            passed = True
                finally:
                    # This process owns only a private unsaved demo; no user document is open.
                    exited_early = process.poll()
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                    if not passed:
                        # The log lives in the temporary directory; show it before it is removed.
                        log.flush()
                        tail = (root / "application.log").read_text(errors="replace").splitlines()[-60:]
                        status = "still running" if exited_early is None else f"exited with {exited_early}"
                        print(f"Application {status}; last {len(tail)} log lines:", file=sys.stderr)
                        print("\n".join(tail), file=sys.stderr)
    finally:
        bridge.exit()


if __name__ == "__main__":
    main()
