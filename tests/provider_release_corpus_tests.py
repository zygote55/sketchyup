#!/usr/bin/env python3
"""Offline freeze check: generated inputs/prompts must match the committed corpus."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

manifest = json.loads((Path(__file__).parent / 'provider-release-corpus.json').read_text())
with tempfile.TemporaryDirectory(prefix='sketchyup-corpus-') as name:
    root = Path(name)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
               XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'))
    for task in manifest['tasks']:
        for repeat in range(2):
            report = root / f"{task['trial']}-{repeat}.json"
            subprocess.run([sys.argv[1], '--describe', 'gpt-6-astra', task['trial'], str(report)],
                           env=env, check=True, stdout=subprocess.DEVNULL, timeout=30)
            data = json.loads(report.read_text())
            for field in ('trial', 'prompt', 'inputSha256', 'promptSha256'):
                if data[field] != task[field]:
                    raise SystemExit('Frozen corpus drift: ' + task['trial'] + '/' + field)
            actual = Path(str(report) + '.files/input.sketchyup').read_bytes()
            if hashlib.sha256(actual).hexdigest() != task['inputSha256']:
                raise SystemExit('Retained input differs: ' + task['trial'])
            if data['networkRequested'] is not False:
                raise SystemExit('Description mode requested network')
print('Eight frozen fixtures reproduced twice; no provider requests.')
