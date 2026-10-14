#!/usr/bin/env python3
"""Regenerate the discovery schemas in docs/api from the built binaries.

Each file is the JSON a discovery option prints, written byte-for-byte in that
file's established layout. Use --check in CI. Only the Python standard library
is required. docs/api/tool-reference-v1.json is produced by
scripts/generate-tool-reference.py instead; every other docs/api file is
covered here, and none is maintained by hand.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


class Number(str):
    """A number token kept exactly as the engine printed it."""


def discover(binary, option):
    # The desktop binary prints discovery before showing a window; offscreen lets it
    # start without a display server (the generic theme avoids a GTK display probe).
    environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic')
    result = subprocess.run([str(binary), option], capture_output=True, check=True,
                            text=True, timeout=60, env=environment)
    return json.loads(result.stdout, parse_float=Number, parse_int=Number)


def render(value, indent, empty_break, depth=0):
    """Serialize with ': ' and one entry per line, as Qt and Python indentation do."""
    pad, end = ' ' * indent * (depth + 1), ' ' * indent * depth
    if isinstance(value, Number):
        return str(value)
    if isinstance(value, dict):
        if not value:
            return '{\n' + end + '}' if empty_break else '{}'
        items = [pad + json.dumps(key, ensure_ascii=False) + ': ' +
                 render(item, indent, empty_break, depth + 1) for key, item in value.items()]
        return '{\n' + ',\n'.join(items) + '\n' + end + '}'
    if isinstance(value, list):
        if not value:
            return '[\n' + end + ']' if empty_break else '[]'
        items = [pad + render(item, indent, empty_break, depth + 1) for item in value]
        return '[\n' + ',\n'.join(items) + '\n' + end + ']'
    return json.dumps(value, ensure_ascii=False)


# Layouts: 'qt' is QJsonDocument::Indented; 'py2' and 'py4' are json.dumps indents.
LAYOUTS = {'qt': (4, True), 'py2': (2, False), 'py4': (4, False)}

# (file, binary, option, key within the discovery output or None, layout)
ARTIFACTS = [
    ('transactions-v1.json', 'cli', '--capabilities', 'transactions', 'qt'),
    ('inspection-v1.json', 'cli', '--capabilities', 'inspection', 'py2'),
    ('inspection-session-v1.json', 'cli', '--capabilities', 'inspectionSession', 'py2'),
    ('headless-session-v1.json', 'cli', '--session-capabilities', None, 'qt'),
    ('mcp-2026-07-28.json', 'cli', '--mcp-capabilities', None, 'qt'),
    ('recipe-v1.json', 'cli', '--recipe-capabilities', None, 'qt'),
    ('extensions-v1.json', 'cli', '--extension-capabilities', None, 'qt'),
    ('native-format-v1.json', 'cli', '--format-capabilities', None, 'qt'),
    ('inspection-desktop-v1.json', 'app', '--inspection-capabilities', None, 'py4'),
    ('mcp-native-2026-07-28.json', 'app', '--mcp-inspection-capabilities', None, 'py4'),
]


def generate(binaries):
    cache, artifacts = {}, {}
    for name, binary, option, key, layout in ARTIFACTS:
        if (binary, option) not in cache:
            cache[binary, option] = discover(binaries[binary], option)
        value = cache[binary, option]
        if key is not None:
            value = value[key]
        artifacts['docs/api/' + name] = render(value, *LAYOUTS[layout]) + '\n'
    return artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True, help='built sketchyup-cli')
    parser.add_argument('--app', type=Path, required=True, help='built sketchyup desktop binary')
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    artifacts = generate({'cli': args.cli.resolve(), 'app': args.app.resolve()})
    drift = []
    for relative, content in artifacts.items():
        path = args.root / relative
        if args.check:
            if not path.is_file() or path.read_bytes() != content.encode():
                drift.append(relative)
        else:
            path.write_bytes(content.encode())
    if drift:
        raise SystemExit('Registry/schema drift: regenerate ' + ', '.join(drift) +
                         ' with scripts/generate-api-schemas.py')
    print(f'{len(artifacts)} generated API schemas ' +
          ('match the live registries' if args.check else 'written'))


if __name__ == '__main__':
    main()
