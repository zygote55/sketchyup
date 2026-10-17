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

    def surface(self, data, trial):
        """Versions 1 to 3 freeze no assistant session, tools or instructions."""

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
                            taskSeconds=self.manifest['deadlineSeconds'],
                            geometryVerified=edit,
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
                self.surface(data, trial)
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
        self.assertEqual(result['corpusVersion'], self.manifest['corpusVersion'])
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



class VersionThreeScoreTests(VersionTwoScoreTests):
    """v2 rules unchanged; the ChatGPT-plan profile carries the PR #338 native limits."""
    MANIFEST = 'tests/provider-release-corpus-v3.json'

    def test_profile_freezes_native_chatgpt_limits(self):
        profile = self.manifest['profiles']['chatgpt-astra']
        self.assertEqual(self.manifest['deadlineSeconds'], 300)
        self.assertEqual((profile['taskTurns'], profile['taskReportedTokens'],
                          profile['outputTokensPerTurn'], profile['outputTokenLimitEnforced']),
                         (16, 524288, None, False))

    def test_version_two_budget_report_rejected(self):
        """A report produced with the pre-#338 ChatGPT-plan budget is configuration drift."""
        for field, value in (('outputTokensPerTurn', 8192), ('taskReportedTokens', 262144),
                             ('outputTokenLimitEnforced', True)):
            with self.subTest(field=field):
                self.setUp()
                self.data[0][field] = value
                self.write()
                with self.assertRaisesRegex(ValueError, 'Configuration drift: ' + field):
                    self.evaluate()

    def test_missing_output_limit_declaration_fails_closed(self):
        del self.data[0]['outputTokenLimitEnforced']
        self.write()
        with self.assertRaises(KeyError):
            self.evaluate()

    def test_version_two_report_rejected_by_version_three_manifest(self):
        self.data[0]['corpusVersion'] = 2
        self.write()
        with self.assertRaisesRegex(ValueError, 'Wrong corpus task'):
            self.evaluate()


INSTRUCTIONS = 'Synthetic assistant instructions.'


class VersionFourScoreTests(VersionThreeScoreTests):
    """v3 rules unchanged; runs use the native session, its 15-minute lifetime and the
    frozen tool surface and instructions."""
    MANIFEST = 'tests/provider-release-corpus-v4.json'

    def surface(self, data, trial):
        assistant = self.manifest['assistant']
        frozen = assistant['surfaces'][trial]
        data.update(assistantSession=assistant['session'],
                    maximumDraftTtlSeconds=assistant['maximumDraftTtlSeconds'],
                    clarificationAvailable=assistant['clarificationAvailable'],
                    allowedCommands=list(frozen['allowedCommands']),
                    toolsSha256=frozen['toolsSha256'])

    def wire(self, text):
        return {'path': '/v1/responses', 'status': 200,
                'request': {'instructions': text + '\nHost task budget: {"remainingSeconds":899}'}}

    def test_profile_freezes_native_chatgpt_limits(self):
        profile = self.manifest['profiles']['chatgpt-astra']
        self.assertEqual(self.manifest['deadlineSeconds'], 900)
        self.assertEqual((profile['taskTurns'], profile['taskReportedTokens'],
                          profile['outputTokensPerTurn'], profile['outputTokenLimitEnforced']),
                         (16, 524288, None, False))
        assistant = self.manifest['assistant']
        self.assertEqual((assistant['session'], assistant['maximumDraftTtlSeconds'],
                          assistant['clarificationAvailable']), ('native', 900, False))
        for value in [profile['instructionsSha256'],
                      self.manifest['profiles']['ollama-qwen4b']['instructionsSha256']] + \
                [row['toolsSha256'] for row in assistant['surfaces'].values()]:
            self.assertRegex(value, '^[0-9a-f]{64}$')

    def test_tasks_identical_to_version_three(self):
        v3 = json.loads((ROOT / 'tests/provider-release-corpus-v3.json').read_text())
        v4 = json.loads((ROOT / self.MANIFEST).read_text())  # setUp rewrites input hashes
        self.assertEqual(v4['tasks'], v3['tasks'])
        for key in ('repetitions', 'hardZero', 'requiredPostconditions', 'postconditionPolicy',
                    'costPolicy'):
            self.assertEqual(v4[key], v3[key])
        for name in ('chatgpt-astra', 'ollama-qwen4b'):
            for key, value in v3['profiles'][name].items():
                self.assertEqual(v4['profiles'][name][key], value)

    def test_headless_deadline_report_rejected(self):
        """A report from the public headless session's 300 s ceiling is not a v4 run."""
        for field, value in (('taskSeconds', 300), ('maximumDraftTtlSeconds', 300),
                             ('assistantSession', 'headless')):
            with self.subTest(field=field):
                self.setUp()
                self.data[0][field] = value
                self.write()
                with self.assertRaisesRegex(ValueError, 'Wrong deadline|Assistant session drift'):
                    self.evaluate()

    def test_changed_instructions_rejected(self):
        self.data[0]['instructionsSha256'] = SCORE.digest(b'other instructions')
        self.write()
        with self.assertRaisesRegex(ValueError, 'Configuration drift: instructionsSha256'):
            self.evaluate()

    def test_recorded_requests_must_carry_frozen_instructions(self):
        frozen = SCORE.digest(INSTRUCTIONS.encode())
        self.manifest['profiles']['chatgpt-astra']['instructionsSha256'] = frozen
        for data in self.data:
            data['instructionsSha256'] = frozen
        self.data[0]['exchanges'] = [self.wire(INSTRUCTIONS)]
        self.write()
        self.assertTrue(self.evaluate()['corpusPassed'])
        for exchanges in ([self.wire(INSTRUCTIONS + ' Changed.')],
                          [{'request': {'instructions': INSTRUCTIONS}}],
                          [{'request': {'messages': [{'role': 'system',
                                                      'content': 'Other local instructions.'
                                                                 '\nHost task budget: {}'}]}}]):
            with self.subTest(exchanges=exchanges):
                self.data[0]['exchanges'] = exchanges
                self.write()
                with self.assertRaisesRegex(ValueError, 'Recorded instructions differ'):
                    self.evaluate()

    def test_tool_surface_drift_rejected(self):
        for field, value in (('toolsSha256', SCORE.digest(b'other tools')),
                             ('allowedCommands', ['assembly.room']),
                             ('clarificationAvailable', True)):
            with self.subTest(field=field):
                self.setUp()
                self.data[0][field] = value
                self.write()
                with self.assertRaisesRegex(ValueError, 'Assistant tool surface drift'):
                    self.evaluate()

    def test_missing_surface_fields_fail_closed(self):
        for field in ('instructionsSha256', 'toolsSha256', 'assistantSession',
                      'allowedCommands', 'maximumDraftTtlSeconds'):
            with self.subTest(field=field):
                self.setUp()
                del self.data[0][field]
                self.write()
                with self.assertRaises(KeyError):
                    self.evaluate()

    def test_version_three_report_rejected_by_version_four_manifest(self):
        self.data[0]['corpusVersion'] = 3
        self.write()
        with self.assertRaisesRegex(ValueError, 'Wrong corpus task'):
            self.evaluate()

    def test_manifest_must_declare_native_surface(self):
        for change in ('session', 'ttl', 'surfaces'):
            with self.subTest(change=change):
                manifest = copy.deepcopy(self.manifest)
                if change == 'session':
                    manifest['assistant']['session'] = 'headless'
                elif change == 'ttl':
                    manifest['assistant']['maximumDraftTtlSeconds'] = 300
                else:
                    del manifest['assistant']['surfaces']['site']
                with self.assertRaisesRegex(ValueError, 'native session surface'):
                    SCORE.score(manifest, self.bundle, self.root)


if __name__ == '__main__':
    unittest.main()
