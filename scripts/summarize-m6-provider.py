#!/usr/bin/env python3
"""Validate the editing-trial predicates and publish only whitelisted synthetic evidence."""
import hashlib
import json
import math
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit('Usage: summarize-m6-provider.py PRIVATE_ADVANCED.json PRIVATE_SITE.json')

summaries = []
for trial, argument in zip(('advanced', 'site'), sys.argv[1:]):
    path = pathlib.Path(argument)
    data = json.loads(path.read_text())
    assert data['trial'] == trial
    assert data['geometryVerified'] and data['unrelatedPreserved']
    assert data['proposal']['phase'] == 'preview-ready'
    assert data['result']['phase'] == 'completed' and data['result']['applied']
    inspected = {}
    commands = []
    for receipt in data['executedReceipts']:
        request = receipt['request']
        commands.extend(request.get('commands', []))
        query = request.get('request', {})
        if request.get('operation') == 'transaction.inspect' and query.get('query') == 'measure.entity':
            measured = receipt['result']['data']
            assert measured['space'] == 'world'
            inspected[query['target']['body']] = measured
    if trial == 'advanced':
        actual = {item['kind']: item for item in data['measurements']['assemblies']}
        assert set(actual) == {'gable-roof', 'straight-stairs', 'table', 'cabinet'}
        for name, expected in (('gable-roof', 4.554), ('straight-stairs', 4.368)):
            assert math.isclose(inspected[actual[name]['body']]['volume'], expected, abs_tol=1e-6)
        for name, expected in (('table', [1.2, .8, .75]), ('cabinet', [.9, .4, 1.2])):
            measured = inspected[actual[name]['body']]
            assert measured['volume'] is None and measured['solidStatus'] == 'multiple_records'
            assert all(math.isclose(a, b, abs_tol=1e-6) for a, b in zip(measured['bounds']['dimensions'], expected))
        needed = {'assembly.roof', 'assembly.stairs', 'assembly.table', 'assembly.cabinet'}
        assert {c['command'] for c in commands if c['command'].startswith('assembly.')} == needed
    else:
        placement = [c for c in commands if c['command'] == 'assembly.site_place']
        assert len(placement) == 1
        command = placement[0]
        assert command['positionUnit'] == 'mm' and command['frame'] == 'world'
        assert command['position'] == [100000125, 200000250, 12500]
        assert math.isclose(command['yawDeltaRadians'], math.pi / 6, abs_tol=1e-10)
        measured = inspected[command['body']]
        assert all(math.isfinite(v) for edge in ('minimum', 'maximum') for v in measured['bounds'][edge])
        assert data['measurements']['worldOrigin'] == [100000.125, 200000.25, 12.5]
    models = pathlib.Path(str(path) + '.files')
    summaries.append({
        'trial': trial, 'provider': data['provider'], 'model': data['model'],
        'recordedAt': data['recordedAt'], 'elapsedMs': data['elapsedMs'],
        'previewReviewedAndApplied': True, 'savedGeometryVerified': True,
        'existingRecordsPreserved': True, 'privateMeasurementsVerified': True,
        'stagedMeasurements': [{'body': body, 'bounds': value['bounds'], 'volume': value.get('volume'),
                                'solidStatus': value['solidStatus']} for body, value in inspected.items()],
        'measurements': data['measurements'],
        'rawEvidenceSha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'inputModelSha256': hashlib.sha256((models/'input.sketchyup').read_bytes()).hexdigest(),
        'outputModelSha256': hashlib.sha256((models/'output.sketchyup').read_bytes()).hexdigest(),
    })
assert summaries[0]['model'] == summaries[1]['model']
print(json.dumps({'liveProvider': True, 'trials': summaries}, indent=2))
