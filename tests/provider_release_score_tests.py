#!/usr/bin/env python3
"""Acceptance scorer must reject incomplete evidence and surface safety failures."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('score', ROOT / 'scripts/score-provider-release.py')
SCORE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCORE)


class ScoreTests(unittest.TestCase):
    MANIFEST = 'tests/provider-release-corpus.json'

    def postconditions(self, data, edit):
        """Version 1 reports carry no undo/recovery postconditions."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = json.loads((ROOT / self.MANIFEST).read_text())
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
                            trial=trial, corpusVersion=self.manifest['corpusVersion'],
                            inputSha256=task['inputSha256'],
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
                self.postconditions(data, edit)
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

    def test_version_one_output_is_unchanged(self):
        if self.manifest['corpusVersion'] != 1:
            self.skipTest('version 1 only')
        result = self.evaluate()
        self.assertEqual(list(result), ['corpusVersion', 'profile', 'model', 'profileStatus',
                                        'corpusPassed', 'releaseAccepted', 'monetaryCost',
                                        'costPolicy', 'thresholds', 'blockers', 'runs'])
        self.assertEqual(list(result['runs'][0]), ['trial', 'repeat', 'passed', 'rawReportSha256',
                                                   'phase', 'elapsedMs', 'reportedTokens',
                                                   'providerTurns', 'toolCalls'])


INPUT = SCORE.digest(b'synthetic canonical input')
COMMITTED = SCORE.digest(b'synthetic canonical committed')


class VersionTwoScoreTests(ScoreTests):
    """Every v1 gate still applies; undo and recovery are additionally required at 100%."""
    MANIFEST = 'tests/provider-release-corpus-v2.json'

    def postconditions(self, data, edit):
        after = COMMITTED if edit else INPUT
        undo = {'historyEntriesAdded': int(edit), 'undoAvailable': edit, 'restores': True}
        recovery = {'restores': True, 'recoveredCanonicalSha256': after}
        if edit:
            undo.update(oneLabeledAssistantStep=True, afterUndoCanonicalSha256=INPUT,
                        afterRedoCanonicalSha256=COMMITTED)
            recovery.update(mode='recover-latest', savedCanonicalSha256=COMMITTED,
                            undoCanonicalSha256=INPUT, dirtyUntilSaved=True,
                            oneLabeledAssistantStep=True)
        data.update(undoRestores=True, recoveryRestores=True, testFault=None,
                    postconditions={'committed': edit, 'inputCanonicalSha256': INPUT,
                                    'committedCanonicalSha256': after, 'undo': undo,
                                    'recovery': recovery})

    def index(self, trial):
        return next(i for i, data in enumerate(self.data) if data['trial'] == trial)

    def test_complete_synthetic_set_requires_all_postconditions(self):
        result = self.evaluate()
        self.assertTrue(result['corpusPassed'])
        self.assertEqual(result['corpusVersion'], 2)
        for field in SCORE.POSTCONDITIONS:
            self.assertEqual(result['postconditions'][field],
                             {'passed': 24, 'runs': 24, 'required': 24})
        self.assertTrue(all(row['undoRestores'] and row['recoveryRestores']
                            for row in result['runs']))

    def test_single_postcondition_failure_blocks_profile(self):
        for trial in ('room', 'measure'):
            for field, reason in (('undoRestores', 'undoNotRestored'),
                                  ('recoveryRestores', 'recoveryNotRestored')):
                with self.subTest(trial=trial, field=field):
                    self.setUp()
                    index = self.index(trial)
                    self.data[index][field] = False
                    key = 'undo' if field == 'undoRestores' else 'recovery'
                    self.data[index]['postconditions'][key]['restores'] = False
                    self.write()
                    result = self.evaluate()
                    self.assertFalse(result['corpusPassed'])
                    self.assertEqual(result['postconditions'][field]['passed'], 23)
                    self.assertIn({'trial': trial, 'repeat': 1, 'reasons': [reason]},
                                  result['blockers'])

    def test_injected_test_fault_blocks(self):
        index = self.index('resize')
        self.data[index]['testFault'] = 'extra-history-step'
        self.write()
        result = self.evaluate()
        self.assertFalse(result['corpusPassed'])
        self.assertIn('testFaultInjected', result['blockers'][0]['reasons'])

    def test_missing_or_contradicted_postconditions_fail_closed(self):
        edits = [('undoRestores', None), ('testFault', None),
                 ('postconditions', 'undo', 'afterUndoCanonicalSha256', COMMITTED),
                 ('postconditions', 'undo', 'historyEntriesAdded', 2),
                 ('postconditions', 'recovery', 'recoveredCanonicalSha256', INPUT),
                 ('postconditions', 'recovery', 'dirtyUntilSaved', False),
                 ('postconditions', 'recovery', 'restores', False)]
        for change in edits:
            with self.subTest(change=change):
                self.setUp()
                data = self.data[self.index('room')]
                if len(change) == 2:
                    del data[change[0]]
                else:
                    data[change[0]][change[1]][change[2]] = change[3]
                self.write()
                with self.assertRaises((ValueError, KeyError)):
                    self.evaluate()

    def test_version_one_report_rejected_by_version_two_manifest(self):
        self.data[0]['corpusVersion'] = 1
        self.write()
        with self.assertRaises(ValueError):
            self.evaluate()

    def test_manifest_must_declare_blocking_postconditions(self):
        for change in ('requiredPostconditions', 'hardZero'):
            with self.subTest(change=change):
                manifest = copy.deepcopy(self.manifest)
                if change == 'hardZero':
                    manifest['hardZero'].remove('undoNotRestored')
                else:
                    manifest['requiredPostconditions'] = ['undoRestores']
                with self.assertRaises(ValueError):
                    SCORE.score(manifest, self.bundle, self.root)


if __name__ == '__main__':
    unittest.main()
