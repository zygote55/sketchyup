#!/usr/bin/env python3
"""Check real standalone discovery with missing/present nonexecuted dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='sketchyup-optional-') as directory:
    root = Path(directory)
    binary = root / 'bin/sketchyup-cli'
    binary.parent.mkdir()
    shutil.copy2(sys.argv[1], binary)
    dependency = root / 'path'
    dependency.mkdir()
    env = dict(os.environ, PATH=str(dependency))
    def run(*args):
        return subprocess.run([str(binary), '--optional-capabilities', *args], env=env,
                              capture_output=True, text=True, timeout=30)
    def report():
        p = run()
        assert p.returncode == 0, p.stderr
        value = json.loads(p.stdout)
        assert value['apiVersion'] == 1 and value['sideEffects'] == 'none'
        assert value['providers']['readiness'] == 'not-probed'
        assert not value['providers']['credentialsRead'] and not value['providers']['networkRequests']
        assert not value['editor']['selectionAvailable']
        assert str(root) not in p.stdout
        return value
    initial = report()
    for key in ('blender', 'textGeometry', 'extensions'):
        assert not initial[key]['executablePresent'] and initial[key]['readiness'] == 'unavailable'
        assert initial[key]['alternative']
    # These executables would leave a marker if discovery incorrectly ran them.
    marker = root / 'executed'
    text = '#!/bin/sh\nprintf executed > "' + str(marker) + '"\nexit 1\n'
    for path in (dependency / 'blender', binary.parent / 'sketchyup-text-worker',
                 root / 'lib/sketchyup/sketchyup-extension-worker'):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        path.chmod(0o700)
    present = report()
    for key in ('blender', 'textGeometry', 'extensions'):
        assert present[key]['executablePresent'] and present[key]['readiness'] == 'not-verified'
    assert not marker.exists()
    # Nonexecutable files and mixed modes must not be advertised or accepted.
    (dependency / 'blender').chmod(0o600)
    assert not report()['blender']['executablePresent']
    for args in [('--new',), ('--output', str(root/'unused')), ('--capabilities',), ('unexpected',)]:
        p = run(*args)
        assert p.returncode != 0
        assert 'INVALID_REQUEST' in p.stdout + p.stderr
    assert not (root / 'unused').exists() and not marker.exists()
print('Optional discovery: missing/present helpers, no execution, no paths and mixed-mode rejection passed')
