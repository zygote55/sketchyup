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
            "accessible_get_state_set": ([pointer], pointer),
            "state_set_contains": ([pointer, integer], integer),
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


def parameter_fixture(cli, root, environment):
    """Writes a document with one placement of a parametric definition whose Width is
    overridden (R094.c)."""
    def ref(path):
        return {"$ref": path}
    document = ref("info#/result/documentId")
    # A fresh document numbers the rectangle's body 1 and its face 5; making it a component
    # keeps body 1 as the placement of definition 1.
    commands = [
        {"command": "geometry.rectangle", "body": "0", "origin": [0, 0, 0], "normal": [0, 0, 1],
         "xAxis": [1, 0, 0], "width": 1, "height": 0.6},
        {"command": "geometry.push_pull", "body": "1", "face": "5", "distance": 0.5},
        {"command": "component.create", "body": "1", "name": "Shelf unit"},
        {"command": "component_parameter.define", "definition": "1", "name": "width",
         "label": "Width", "type": "length", "mode": "input", "expression": "1 m",
         "min": 0.5, "max": 2},
        {"command": "component_parameter.override", "body": "1", "values": {"width": 1.5}},
    ]
    steps = [
        {"id": "info", "request": {"apiVersion": 1, "operation": "session.describe"}},
        {"id": "begin", "request": {"apiVersion": 1, "operation": "transaction.begin",
                                    "documentId": document,
                                    "expectedRevision": ref("info#/result/revision"),
                                    "history": {"label": "Parameter fixture",
                                                "taskId": "a11y-parameters", "assistant": False}}},
        {"id": "apply", "request": {"apiVersion": 1, "operation": "transaction.apply",
                                    "documentId": document,
                                    "transactionId": ref("begin#/result/transactionId"),
                                    "expectedVersion": 0, "operationId": "parameters",
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
    recipe = root / "parameters-recipe.json"
    recipe.write_text(json.dumps({"apiVersion": 1, "steps": steps}))
    output = root / "parameters.sketchyup"
    result = subprocess.run([str(cli), "--recipe", str(recipe), "--new", "--output", str(output),
                             "--outcomes", str(root / "parameter-outcomes")],
                            env=environment, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and output.exists(), \
        f"Parameter fixture recipe failed: {result.stdout[-2000:]} {result.stderr[-2000:]}"
    return output


def parameters_phase(bridge, application, root, environment):
    """R094.c (ADR 0166 §8): Entity info's placement parameters are named and described over
    AT-SPI: the section header, the value field (its caption and bounds), its reset button,
    Reset all and Edit definition…."""
    fixture = parameter_fixture(application.resolve().parent / "sketchyup-cli", root, environment)
    with (root / "parameters.log").open("w") as log:
        process = subprocess.Popen([str(application.resolve()), str(fixture)], env=environment,
                                   stdout=log, stderr=log)
        try:
            with bridge.owned(bridge.get_desktop(0)) as desktop:
                def own_application():
                    assert process.poll() is None, "Parameter fixture exited before registration"
                    for index in range(bridge.accessible_get_child_count(desktop, None)):
                        child = bridge.accessible_get_child_at_index(desktop, index, None)
                        if not child:
                            continue
                        if bridge.accessible_get_process_id(child, None) == process.pid:
                            return child
                        bridge.objects.g_object_unref(child)
                    return None

                with bridge.owned(wait_for(bridge, own_application,
                                           "The parameter fixture did not register")) as app:
                    def listed():
                        rows = bridge.snapshot(app)
                        cells = [r for r in rows if r["role"] == "table cell" and
                                 r["name"].startswith("Shelf unit")]
                        return (rows, cells[0]) if cells else None

                    rows, placement = wait_for(bridge, listed, "Model hierarchy lacks the placement")
                    hierarchy = find(rows, "Model hierarchy")
                    with bridge.at_path(app, hierarchy["path"]) as control:
                        with bridge.owned(bridge.accessible_get_selection_iface(control)) as selection:
                            assert bridge.selection_select_child(
                                selection, placement["path"][-1], None), \
                                "AT-SPI could not select the placement"
                    tab = find(rows, "Info", "page tab")
                    with bridge.at_path(app, tab["path"]) as control:
                        with bridge.owned(bridge.accessible_get_action_iface(control)) as action:
                            names = [bridge.string(bridge.action_get_name(action, i, None))
                                     for i in range(bridge.action_get_n_actions(action, None))]
                            assert "Press" in names and bridge.action_do_action(
                                action, names.index("Press"), None), "Could not open Entity info"
                    observed = []

                    def shown():
                        current = bridge.snapshot(app)
                        observed[:] = [(r["name"], r["role"]) for r in current
                                       if "Width" in r["name"] or "Parameters" in r["name"]]
                        fields = [r for r in current if r["name"] == "Width" and
                                  r["role"] == "text"]
                        return current if fields else None

                    try:
                        rows = wait_for(bridge, shown, "The Width field is not exposed")
                    except AssertionError as error:
                        raise AssertionError(f"{error}; observed {observed}") from None
                    field = find(rows, "Width", "text")
                    assert field["description"] == "Override, default 1 m, 0.5–2 m", \
                        f"The field's description is its caption: {field['description']!r}"
                    # The header is a checkable button, which AT-SPI reports as a check box.
                    header = find(rows, "Parameters · this placement", "check box")
                    assert header["description"] == "Expanded, 1 overridden", \
                        f"The header reads its state and count: {header['description']!r}"
                    reset = find(rows, "Reset Width to the default, 1 m", "button")
                    reset_all = find(rows, "Reset all", "button")
                    edit = find(rows, "Edit definition…", "button")
                    # R094.d (ADR 0166 §3, §8): Edit definition… opens the definition context;
                    # its sections, trees, header buttons and the Preview strip are named.
                    with bridge.at_path(app, edit["path"]) as control:
                        with bridge.owned(bridge.accessible_get_action_iface(control)) as action:
                            names = [bridge.string(bridge.action_get_name(action, i, None))
                                     for i in range(bridge.action_get_n_actions(action, None))]
                            assert "Press" in names and bridge.action_do_action(
                                action, names.index("Press"), None), "Could not open the definition"
                    # R094.e (ADR 0166 §4.1, §8): the placement has its own Width, so Edit
                    # definition… asks first. The dialog, its text and its three buttons are named;
                    # Edit definition at defaults (the default) opens the definition.
                    asked = []

                    def scope_dialog():
                        try:
                            current = bridge.snapshot(app)
                        except AssertionError:
                            return None
                        asked[:] = [(r["name"], r["role"]) for r in current if r["role"] == "dialog"]
                        dialogs = [r for r in current if r["role"] == "dialog" and
                                   r["name"] == "Shelf unit has its own values"]
                        return (current, dialogs[0]) if dialogs else None

                    try:
                        rows, dialog = wait_for(bridge, scope_dialog, "The values dialog is not exposed")
                    except AssertionError as error:
                        raise AssertionError(f"{error}; observed {asked}") from None
                    assert dialog["description"] == (
                        "Width 1.5 m differs from the defaults. Shape edits are made on the shared "
                        "definition at its default values, or on a unique copy."), dialog
                    # The buttons are looked up inside the dialog: the Components tab's values
                    # form (R094.f) also has a Cancel button, hidden in another Tray tab.
                    inside = [r for r in rows if r["path"][:len(dialog["path"])] == dialog["path"]]
                    choices = [find(inside, name, "button")["name"] for name in
                               ("Cancel", "Make unique and edit", "Edit definition at defaults")]
                    with bridge.at_path(app, find(inside, "Edit definition at defaults", "button")["path"]) \
                            as control:
                        with bridge.owned(bridge.accessible_get_action_iface(control)) as action:
                            names = [bridge.string(bridge.action_get_name(action, i, None))
                                     for i in range(bridge.action_get_n_actions(action, None))]
                            assert "Press" in names and bridge.action_do_action(
                                action, names.index("Press"), None), "Could not edit at defaults"
                    seen = []

                    def authoring():
                        # The Tray rebuilds its sections as the context opens: a child that
                        # goes away mid-walk is retried on the next pump.
                        try:
                            current = bridge.snapshot(app)
                        except AssertionError:
                            return None
                        seen[:] = [(r["name"], r["role"]) for r in current
                                   if "definition" in r["name"].lower() or "driver" in r["name"].lower()]
                        headers = [r for r in current if r["name"] == "Parameters · definition"
                                   and r["role"] == "check box"]
                        rows = [r for r in current if r["role"] == "table cell" and
                                r["name"] == "Width, width, Length, 0.5–2 m, 1 m"]
                        return current if headers and rows else None

                    try:
                        rows = wait_for(bridge, authoring, "The definition sections are not exposed")
                    except AssertionError as error:
                        raise AssertionError(f"{error}; observed {seen}") from None
                    definition = find(rows, "Parameters · definition", "check box")
                    drivers = find(rows, "Drivers", "check box")
                    assert definition["description"].startswith("Expanded"), definition
                    add_parameter = find(rows, "Add parameter", "button")
                    add_driver = [r for r in rows if r["name"] == "Add driver" and r["role"] == "button"]
                    assert add_driver, "The Drivers header's + Driver is named"
                    trees = [r for r in rows if r["name"] in ("Definition parameters", "Drivers")
                             and r["role"] != "check box"]
                    assert {r["name"] for r in trees} == {"Definition parameters", "Drivers"}, trees
                    cells = [r for r in rows if r["role"] == "table cell" and
                             r["name"] == "Width, width, Length, 0.5–2 m, 1 m"]
                    assert cells, ("The parameter row reads label, name, type, bounds and "
                                   f"default: {[r['name'] for r in rows if r['role'] == 'table cell']}")
                    # The Preview strip's field; the placement section's hidden Width field
                    # (described by its caption) stays in the tree.
                    strips = [r for r in rows if r["name"] == "Width" and r["role"] == "text"
                              and not r["description"]]
                    assert len(strips) == 1, f"The Preview strip's Width field: {strips}"
                    strip = strips[0]
                    return {"field": {"name": field["name"], "description": field["description"]},
                            "header": {"name": header["name"],
                                       "description": header["description"]},
                            "buttons": [reset["name"], reset_all["name"], edit["name"]],
                            "dialog": {"name": dialog["name"], "description": dialog["description"],
                                       "buttons": choices},
                            "authoring": {"sections": [definition["name"], drivers["name"]],
                                          "buttons": [add_parameter["name"], "Add driver"],
                                          "trees": sorted(r["name"] for r in trees),
                                          "row": cells[0]["name"], "preview": strip["name"]}}
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


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
                            # R094.a: the sidebar's resize handle is a named, vertical split pane
                            # (AT-SPI states 14 horizontal, 29 vertical).
                            sidebar_handle = find(rows, "Resize sidebar", "split pane")
                            with bridge.at_path(app, sidebar_handle["path"]) as control:
                                with bridge.owned(bridge.accessible_get_state_set(control)) as states:
                                    sidebar_handle["orientation"] = (
                                        "vertical" if bridge.state_set_contains(states, 29) else
                                        "horizontal" if bridge.state_set_contains(states, 14) else "none")
                            assert sidebar_handle["orientation"] == "vertical", \
                                f"Sidebar handle orientation is {sidebar_handle['orientation']}"
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
                            parameters = parameters_phase(bridge, args.application, root,
                                                          environment)
                            report = {
                                "fixture": "built-in courtyard demo; private config/data and owned process only",
                                "platformRequested": environment.get("QT_QPA_PLATFORM", "default"),
                                "scope": "AT-SPI tree/text/selection/action interfaces and measured Entity info",
                                "measurementsInput": {key: measurements[key] for key in ("name", "role", "description")},
                                "selectionDescription": summary["description"],
                                "entityInfo": expected,
                                "sheets": sheets,
                                "parameters": parameters,
                                "sidebarHandle": {key: sidebar_handle[key] for key in ("name", "role", "orientation")},
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
