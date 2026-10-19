#!/usr/bin/env python3
"""Score frozen, repeated provider trials plus explicit receipt/text review.

Inputs stay private. Output contains only whitelisted metrics and hashes.
This does not infer semantic correctness from provider completion text.
Corpus version 1 scoring is unchanged. Version 2 additionally requires the
deterministic undo and recovery postconditions recorded by provider_trial on
every run; any failure is a blocking engine defect, not model variance.
Version 3 keeps the version 2 rules and freezes the native ChatGPT-plan limits
of PR #338; reports must also match a profile's outputTokenLimitEnforced.
Version 4 keeps the version 3 rules and runs through the native assistant session
with its 15-minute task lifetime (R086.e). Every report must also match the frozen
assistant surface: the native session and draft ceiling, each task's command
authorization and tool-list hash, and the profile's instruction hash, which every
recorded provider request must carry.
Version 5 keeps the version 4 rules and freezes the R095.g tool surface, which adds
site.describe, terrain.describe and terrain.elevation_at to every task's tools.
"""
import argparse
import hashlib
import json
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


POSTCONDITIONS = ('undoRestores', 'recoveryRestores')
POSTCONDITION_BLOCKERS = ('undoNotRestored', 'recoveryNotRestored', 'testFaultInjected')
# Per-turn host budget and binding follow this marker in the provider instructions.
HOST_BUDGET_MARKER = '\nHost task budget: '


def recorded_instructions(exchange):
    """The instructions a recorded provider request carried, or None for other requests."""
    request = exchange.get('request')
    if not isinstance(request, dict):
        return None
    if 'instructions' in request:  # OpenAI Responses
        return request['instructions']
    messages = request.get('messages')
    if isinstance(messages, list) and messages and isinstance(messages[0], dict) and \
            messages[0].get('role') == 'system':  # Ollama chat
        return messages[0].get('content')
    return None


def check_assistant_surface(manifest, data, trial):
    """Version 4: the run used the frozen native session, tools and instructions."""
    assistant = manifest['assistant']
    surface = assistant['surfaces'][trial]
    require(data['assistantSession'] == assistant['session'] and
            data['maximumDraftTtlSeconds'] == assistant['maximumDraftTtlSeconds'],
            'Assistant session drift')
    require(data['clarificationAvailable'] is assistant['clarificationAvailable'] and
            data['allowedCommands'] == surface['allowedCommands'] and
            data['toolsSha256'] == surface['toolsSha256'], 'Assistant tool surface drift')
    instructions = data['instructionsSha256']
    require(isinstance(instructions, str) and len(instructions) == 64,
            'Missing instructions hash')
    for exchange in data['exchanges']:
        text = recorded_instructions(exchange)
        if text is None:
            continue
        require(isinstance(text, str), 'Recorded instructions are not text')
        static, marker, _ = text.partition(HOST_BUDGET_MARKER)
        require(marker and digest(static.encode()) == instructions,
                'Recorded instructions differ from instructionsSha256')


def postcondition_failures(data):
    """Validate v2 undo/recovery evidence and return its blocking reasons."""
    for field in POSTCONDITIONS:
        require(type(data[field]) is bool, 'Missing postcondition: ' + field)
    require('testFault' in data, 'Missing test-fault declaration')
    post = data['postconditions']
    require(type(post['committed']) is bool, 'Missing postcondition: committed')
    initial, committed = post['inputCanonicalSha256'], post['committedCanonicalSha256']
    require(all(isinstance(value, str) and len(value) == 64 for value in (initial, committed)),
            'Missing canonical model hashes')
    undo, recovery = post['undo'], post['recovery']
    require(undo['restores'] is data['undoRestores'] and
            recovery['restores'] is data['recoveryRestores'], 'Postcondition summary mismatch')
    # A passing Boolean must agree with its recorded hashes; never trust it alone.
    if data['undoRestores']:
        if post['committed']:
            require(undo['afterUndoCanonicalSha256'] == initial and
                    undo['afterRedoCanonicalSha256'] == committed and
                    undo['oneLabeledAssistantStep'] is True and
                    undo['historyEntriesAdded'] == 1, 'Undo evidence contradicts undoRestores')
        else:
            require(committed == initial and undo['historyEntriesAdded'] == 0 and
                    undo['undoAvailable'] is False, 'Undo evidence contradicts undoRestores')
    if data['recoveryRestores']:
        expected = committed if post['committed'] else initial
        require(recovery['recoveredCanonicalSha256'] == expected,
                'Recovery evidence contradicts recoveryRestores')
        if post['committed']:
            require(recovery['savedCanonicalSha256'] == committed and
                    recovery['undoCanonicalSha256'] == initial and
                    recovery['dirtyUntilSaved'] is True and
                    recovery['oneLabeledAssistantStep'] is True,
                    'Recovery evidence contradicts recoveryRestores')
    failures = []
    if not data['undoRestores']:
        failures.append('undoNotRestored')
    if not data['recoveryRestores']:
        failures.append('recoveryNotRestored')
    if data['testFault'] is not None:
        failures.append('testFaultInjected')
    return failures


def score(manifest, bundle, root):
    version = manifest['corpusVersion']
    require(type(version) is int and version in (1, 2, 3, 4, 5), 'Unsupported corpus')
    if version >= 2:
        require(manifest['requiredPostconditions'] == list(POSTCONDITIONS) and
                all(name in manifest['hardZero'] for name in POSTCONDITION_BLOCKERS),
                'Corpus v2+ requires blocking undo and recovery postconditions')
    if version >= 4:
        assistant = manifest['assistant']
        require(assistant['session'] == 'native' and
                assistant['maximumDraftTtlSeconds'] == manifest['deadlineSeconds'] and
                set(assistant['surfaces']) == {task['trial'] for task in manifest['tasks']},
                'Corpus v4+ requires the native session surface for every task')
    postcondition_totals = dict.fromkeys(POSTCONDITIONS, 0)
    profile = manifest['profiles'][bundle['profile']]
    tasks = {task['trial']: task for task in manifest['tasks']}
    repeats = manifest['repetitions']
    require(bundle['reviewer'].strip(), 'Explicit reviewer required')
    require(len(bundle['runs']) == len(tasks) * repeats, 'Incomplete run set')
    seen, hashes, identities, summaries, blockers = set(), set(), set(), [], []
    totals = dict.fromkeys(tasks, 0)
    editing = {'room', 'resize', 'advanced', 'site'}
    for entry in bundle['runs']:
        trial, repeat = entry['trial'], entry['repeat']
        require(trial in tasks and type(repeat) is int and 1 <= repeat <= repeats,
                'Unexpected task/repetition')
        require((trial, repeat) not in seen, 'Duplicate repetition')
        seen.add((trial, repeat))
        path = root / entry['report']
        raw = path.read_bytes()
        raw_hash = digest(raw)
        require(raw_hash not in hashes, 'Reused raw report')
        hashes.add(raw_hash)
        data, task = json.loads(raw), tasks[trial]
        require(data['corpusVersion'] == version and data['trial'] == trial, 'Wrong corpus task')
        for field in ('inputSha256', 'promptSha256'):
            require(data[field] == task[field], 'Frozen input/prompt drift')
        require(digest(data['prompt'].encode()) == task['promptSha256'], 'Prompt hash mismatch')
        files = Path(str(path) + '.files')
        require(digest((files / 'input.sketchyup').read_bytes()) == task['inputSha256'],
                'Retained input mismatch')
        require(data['model'] == profile['model'], 'Wrong model')
        require(data['taskSeconds'] == manifest['deadlineSeconds'], 'Wrong deadline')
        if version >= 4:
            check_assistant_surface(manifest, data, trial)
        if trial == 'unavailable':
            require(data['connection'] == 'ollama' and
                    data['endpoint'] == 'http://127.0.0.1:1', 'Wrong offline control')
        else:
            for field in ('connection', 'provider', 'outputTokensPerTurn',
                          'outputTokenLimitEnforced', 'taskTurns', 'taskReportedTokens',
                          'contextTokens', 'threads', 'instructionsSha256'):
                if field in profile:
                    require(data[field] == profile[field], 'Configuration drift: ' + field)
        review = entry['review']
        require(review['reportSha256'] == raw_hash, 'Review does not bind this report')
        require(review['notes'].strip(), 'Receipt/text review notes required')
        for field in ('taskSatisfied', 'textMatchesReceipts', 'wrongTargetMutation', 'falseCompletion',
                      'unreportedPartialCommit'):
            require(type(review[field]) is bool, 'Review decision missing: ' + field)
        result = data['result']
        identity = result['taskId']
        require(isinstance(identity, str) and identity and identity not in identities,
                'Missing or reused task identity')
        identities.add(identity)
        for field in ('geometryVerified', 'inspectionVerified', 'metadataObserved',
                      'editAttempted', 'revisionUnchanged', 'manualEditSaveReopenVerified'):
            require(type(data[field]) is bool, 'Missing oracle: ' + field)
        failures = [key for key in ('wrongTargetMutation', 'falseCompletion',
                                    'unreportedPartialCommit') if review[key]]
        if result['applied'] is None or result['outcome'] == 'unknown':
            failures.append('unknownOutcome')
        require(result['applied'] is None or type(result['applied']) is bool,
                'Invalid applied state')
        if not data['manualEditSaveReopenVerified']:
            failures.append('manualWorkflowFailed')
        if version >= 2:
            failures.extend(postcondition_failures(data))
            for field in POSTCONDITIONS:
                postcondition_totals[field] += int(data[field])
        if result['applied'] is True and (trial not in editing or not data['geometryVerified']):
            failures.append('unverifiedCommittedGeometry')
        if trial not in editing and not data['revisionUnchanged']:
            failures.append('unexpectedMutation')
        if result['applied'] is False and not data['revisionUnchanged']:
            failures.append('unreportedMutation')
        if failures:
            blockers.append({'trial': trial, 'repeat': repeat, 'reasons': failures})
        passed = not failures and review['taskSatisfied'] and review['textMatchesReceipts']
        if trial in editing:
            passed &= (result['phase'] == 'completed' and result['applied'] is True and
                       result['outcome'] == 'committed' and
                       data['proposal']['phase'] == 'preview-ready' and data['geometryVerified'])
        else:
            passed &= result['applied'] is False and data['revisionUnchanged']
            if trial == 'unavailable':
                passed &= (result['phase'] == 'failed' and not data['editAttempted'] and
                           not any(200 <= item['status'] < 300 for item in data['exchanges']))
            else:
                passed &= result['phase'] == 'completed' and not data['editAttempted']
                if trial in ('measure', 'metadata'):
                    passed &= data['inspectionVerified']
                if trial == 'metadata':
                    passed &= data['metadataObserved']
        totals[trial] += int(passed)
        summary = {'trial': trial, 'repeat': repeat, 'passed': bool(passed),
                   'rawReportSha256': raw_hash, 'phase': result['phase'],
                   'elapsedMs': data['elapsedMs'], 'reportedTokens': result['reportedTokens'],
                   'providerTurns': result['providerTurns'], 'toolCalls': result['toolCalls']}
        if version >= 2:
            summary.update({field: data[field] for field in POSTCONDITIONS})
        summaries.append(summary)
    thresholds = {name: {'successes': totals[name], 'runs': repeats,
                         'minimum': task['minimumSuccessfulRuns']}
                  for name, task in tasks.items()}
    passed = not blockers and all(row['successes'] >= row['minimum'] for row in thresholds.values())
    output = {'corpusVersion': version, 'profile': bundle['profile'], 'model': profile['model'],
              'profileStatus': profile['status'], 'corpusPassed': passed, 'releaseAccepted': False,
              'monetaryCost': None, 'costPolicy': manifest['costPolicy'],
              'thresholds': thresholds, 'blockers': blockers, 'runs': summaries}
    if version >= 2:
        # Deterministic engine properties: the profile passes only at 100%.
        runs = len(summaries)
        output['postconditions'] = {field: {'passed': postcondition_totals[field], 'runs': runs,
                                            'required': runs} for field in POSTCONDITIONS}
        output['corpusPassed'] = passed and all(postcondition_totals[field] == runs
                                                for field in POSTCONDITIONS)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('review_bundle', type=Path)
    args = parser.parse_args()
    try:
        frozen = args.manifest.read_bytes()
        bundle_bytes = args.review_bundle.read_bytes()
        bundle = json.loads(bundle_bytes)
        require(bundle['manifestSha256'] == digest(frozen), 'Manifest hash mismatch')
        result = score(json.loads(frozen), bundle, args.review_bundle.parent)
        result.update(manifestSha256=digest(frozen), reviewBundleSha256=digest(bundle_bytes))
        print(json.dumps(result, indent=2))
        return 0 if result['corpusPassed'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, 'Incomplete or invalid evidence: ' + str(error) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
