#!/usr/bin/env python3
"""Acceptance scorer must reject incomplete evidence and surface safety failures."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('score', ROOT / 'scripts/score-provider-release.py')
SCORE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCORE)


class ScoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = json.loads((ROOT / 'tests/provider-release-corpus.json').read_text())
        self.bundle = {'profile': 'chatgpt-astra', 'reviewer': 'synthetic scorer test', 'runs': []}
        self.data = []
        # Synthetic scorer inputs exercise gate logic, never provider quality.
        for task in self.manifest['tasks']:
            task['inputSha256'] = SCORE.digest(b'synthetic input')
            for repeat in range(1, 4):
                trial = task['trial']
                edit = trial in ('room', 'resize', 'advanced', 'site')
                name = f'{trial}-{repeat}.json'
                entry = {'trial': trial, 'repeat': repeat, 'report': name,
                         'review': {'notes': 'Synthetic test only', 'taskSatisfied': True,
                                    'textMatchesReceipts': True, 'wrongTargetMutation': False,
                                    'falseCompletion': False, 'unreportedPartialCommit': False}}
                data = dict(self.manifest['profiles']['chatgpt-astra'],
                            trial=trial, corpusVersion=1, inputSha256=task['inputSha256'],
                            promptSha256=task['promptSha256'], prompt=task['prompt'],
                            taskSeconds=300, geometryVerified=edit,
                            inspectionVerified=trial in ('measure', 'metadata'),
                            metadataObserved=trial == 'metadata', editAttempted=edit,
                            revisionUnchanged=not edit, manualEditSaveReopenVerified=True,
                            elapsedMs=100, exchanges=[], proposal={'phase': 'preview-ready'},
                            result={'taskId': name, 'phase': 'completed', 'applied': edit,
                                    'outcome': 'committed' if edit else 'not-committed',
                                    'reportedTokens': 100, 'providerTurns': 1, 'toolCalls': 1})
                if trial == 'unavailable':
                    data.update(connection='ollama', endpoint='http://127.0.0.1:1')
                    data['result']['phase'] = 'failed'
                files = self.root / (name + '.files')
                files.mkdir()
                (files / 'input.sketchyup').write_bytes(b'synthetic input')
                self.data.append(data)
                self.bundle['runs'].append(entry)
        self.write()

    def write(self):
        for entry, data in zip(self.bundle['runs'], self.data):
            raw = json.dumps(data).encode()
            (self.root / entry['report']).write_bytes(raw)
            entry['review']['reportSha256'] = SCORE.digest(raw)

    def evaluate(self):
        return SCORE.score(self.manifest, self.bundle, self.root)

    def test_complete_synthetic_set(self):
        result = self.evaluate()
        self.assertTrue(result['corpusPassed'])
        self.assertFalse(result['releaseAccepted'])

    def test_missing_and_duplicate_runs(self):
        for change in ('missing', 'duplicate'):
            with self.subTest(change=change):
                bundle = copy.deepcopy(self.bundle)
                if change == 'missing':
                    bundle['runs'].pop()
                else:
                    bundle['runs'][-1] = bundle['runs'][0]
                with self.assertRaises(ValueError):
                    SCORE.score(self.manifest, bundle, self.root)

    def test_drift_and_unbound_review(self):
        for field, value in [('inputSha256', 'changed'), ('prompt', 'changed'),
                             ('model', 'different'), ('taskSeconds', 301)]:
            with self.subTest(field=field):
                old = self.data[0][field]
                self.data[0][field] = value
                self.write()
                with self.assertRaises(ValueError):
                    self.evaluate()
                self.data[0][field] = old
        self.write()
        self.bundle['runs'][0]['review']['reportSha256'] = 'changed'
        with self.assertRaises(ValueError):
            self.evaluate()

    def test_safety_failures_block_despite_success_rate(self):
        for field in ('wrongTargetMutation', 'falseCompletion', 'unreportedPartialCommit'):
            with self.subTest(field=field):
                self.bundle['runs'][3]['review'][field] = True
                result = self.evaluate()
                self.assertFalse(result['corpusPassed'])
                self.assertTrue(result['blockers'])
                self.bundle['runs'][3]['review'][field] = False

    def test_unknown_and_unreported_outcomes_block(self):
        for applied, unchanged in [(None, True), (False, False)]:
            self.data[0]['result']['applied'] = applied
            self.data[0]['revisionUnchanged'] = unchanged
            self.write()
            self.assertTrue(self.evaluate()['blockers'])

    def test_truthful_failure_is_not_success(self):
        self.bundle['runs'][0]['review']['taskSatisfied'] = False
        self.assertFalse(self.evaluate()['corpusPassed'])

    def test_metadata_must_reach_the_provider(self):
        index = next(i for i, data in enumerate(self.data) if data['trial'] == 'metadata')
        self.data[index]['metadataObserved'] = False
        self.write()
        self.assertFalse(self.evaluate()['corpusPassed'])

    def test_manual_workflow_required(self):
        self.data[0]['manualEditSaveReopenVerified'] = False
        self.write()
        self.assertTrue(self.evaluate()['blockers'])


if __name__ == '__main__':
    unittest.main()
