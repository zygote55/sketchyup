#!/usr/bin/env python3
"""Execute every shipped transaction recipe and verify measured, persisted geometry."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


def near(actual, expected):
    if isinstance(expected, list):
        assert len(actual) == len(expected), (actual, expected)
        for a, e in zip(actual, expected):
            near(a, e)
    else:
        assert math.isfinite(actual) and abs(actual - expected) < 1e-6, (actual, expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--examples', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve()
    def call(*arguments):
        p = subprocess.run([str(cli), *map(str, arguments)], text=True, capture_output=True,
                           timeout=180)
        assert p.returncode == 0, (arguments, p.stdout[-2000:], p.stderr[-2000:])
        return p.stdout
    cases = {
        'transaction-face-recipe.json': ([2, 3, 0], 6, None),
        'material-recipe-v1.json': ([2, 3, 0], 6, None),
        'room-recipe-v1.json': ([6, 4, 2.7], None, None),
        'room-window-resize-recipe-v1.json': ([1.4, .1, 1], None, None),
        'hosted-room-recipe-v1.json': ([1.4, .1, 1], None, None),
        'roof-recipe-v1.json': ([6.6, 4.6, 3.3 * math.tan(math.pi / 6) + .15],
                                None, 6.6 * 4.6 * .15),
        'stair-recipe-v1.json': ([12 * .28, 1, 12 * .2], None,
                                 .28 * 1 * .2 * sum(range(1, 13))),
        'table-recipe-v1.json': ([1.2, .8, .75], None, None),
        'cabinet-recipe-v1.json': ([.9, .4, 1.2], None, None),
        'site-recipe-v1.json': (None, None, None),
    }
    shipped = {p.name for p in args.examples.glob('*recipe*.json')}
    assert shipped == set(cases), ('Unverified or missing shipped recipes', shipped ^ set(cases))
    with tempfile.TemporaryDirectory(prefix='sketchyup-shipped-recipes-') as directory:
        root = Path(directory)
        for name, (dimensions, area, volume) in cases.items():
            recipe = args.examples / name
            model = root / (name + '.sketchyup')
            source = recipe.read_bytes()
            output = call('--recipe', recipe, '--new', '--output', model,
                          '--outcomes', root / (name + '-outcomes'))
            rows = [json.loads(line) for line in output.splitlines()]
            assert rows and all(row['ok'] for row in rows), rows[-1]
            assert len(rows) == len(json.loads(source)['steps'])
            responses = {row['id']: row['result'] for row in rows}
            measured = rows[-1]['result']['data']
            assert measured['space'] == 'world' and measured['units']['length'] == 'm'
            assert measured['area'] > 0 and measured['length'] > 0
            if dimensions is not None:
                near(measured['bounds']['dimensions'], dimensions)
            if area is not None:
                near(measured['area'], area)
            if volume is not None:
                assert measured['solidStatus'] == 'solid'
                near(measured['volume'], volume)
            if name == 'site-recipe-v1.json':
                # Placement converts millimetres to metres at a far-from-origin
                # world position. Rotation preserves surface area and total length.
                preview = responses['preview_measurement']['data']
                near(measured['area'], preview['area'])
                near(measured['bounds']['minimum'][2], 12.5)
                near(measured['bounds']['maximum'][2], 12.5 + 2.7 + 3 * math.tan(math.pi/6) + .15)
                assert 99990 < measured['bounds']['minimum'][0] < 100010
                assert 199990 < measured['bounds']['minimum'][1] < 200010
            # Validate and independently reload the published document, then query
            # the same exact target at the new session's revision.
            before = model.read_bytes()
            call('--validate-native', model)
            info = json.loads(call('--input', model, '--inspect', 'document.describe'))
            target = measured['target'].copy()
            target['documentId'] = info['documentId']
            request = dict(apiVersion=1, documentId=info['documentId'],
                           expectedRevision=info['revision'], query='measure.entity',
                           target=target, space='world')
            query = root / 'measurement.json'
            query.write_text(json.dumps(request))
            persisted = json.loads(call('--input', model, '--inspect-file', query))['data']
            near(persisted['bounds']['dimensions'], measured['bounds']['dimensions'])
            near(persisted['area'], measured['area'])
            if name == 'site-recipe-v1.json':
                document = json.loads(call('--input', model, '--query', 'document.describe'))
                placed = next(b for b in document['bodies'] if b['id'] == target['body'])
                c, s = math.cos(math.pi / 6), math.sin(math.pi / 6)
                near(placed['worldTransform'], [c, s, 0, 0, -s, c, 0, 0,
                                                0, 0, 1, 0, 100000.125, 200000.25, 12.5, 1])
            if name == 'material-recipe-v1.json':
                appearance = json.loads(call('--input', model, '--query', 'materials.describe'))
                materials = appearance['materials']
                material = next(m for m in materials if m['name'] == 'Terracotta')
                near(material['color'], [.7, .2, .1])
                near(material['opacity'], 1)
                query.write_text(json.dumps(dict(query='material.sample', body=target['body'])))
                sample = json.loads(call('--input', model, '--query-file', query))
                for side in ('front', 'back'):
                    assert sample[side]['material'] == material['id']
                    near(sample[side]['color'], [.7, .2, .1])
            assert model.read_bytes() == before and recipe.read_bytes() == source
            print(name + ': geometry, save/reload and source preservation passed', flush=True)


if __name__ == '__main__':
    main()
