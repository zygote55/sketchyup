#!/usr/bin/env python3
"""Score frozen, repeated provider trials plus explicit receipt/text review.

Inputs stay private. Output contains only whitelisted metrics and hashes.
This does not infer semantic correctness from provider completion text.
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


def score(manifest, bundle, root):
    require(manifest['corpusVersion'] == 1, 'Unsupported corpus')
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
        require(data['corpusVersion'] == 1 and data['trial'] == trial, 'Wrong corpus task')
        for field in ('inputSha256', 'promptSha256'):
            require(data[field] == task[field], 'Frozen input/prompt drift')
        require(digest(data['prompt'].encode()) == task['promptSha256'], 'Prompt hash mismatch')
        files = Path(str(path) + '.files')
        require(digest((files / 'input.sketchyup').read_bytes()) == task['inputSha256'],
                'Retained input mismatch')
        require(data['model'] == profile['model'], 'Wrong model')
        require(data['taskSeconds'] == manifest['deadlineSeconds'], 'Wrong deadline')
        if trial == 'unavailable':
            require(data['connection'] == 'ollama' and
                    data['endpoint'] == 'http://127.0.0.1:1', 'Wrong offline control')
        else:
            for field in ('connection', 'provider', 'outputTokensPerTurn', 'taskTurns',
                          'taskReportedTokens', 'contextTokens', 'threads'):
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
        summaries.append({'trial': trial, 'repeat': repeat, 'passed': bool(passed),
                          'rawReportSha256': raw_hash, 'phase': result['phase'],
                          'elapsedMs': data['elapsedMs'], 'reportedTokens': result['reportedTokens'],
                          'providerTurns': result['providerTurns'], 'toolCalls': result['toolCalls']})
    thresholds = {name: {'successes': totals[name], 'runs': repeats,
                         'minimum': task['minimumSuccessfulRuns']}
                  for name, task in tasks.items()}
    passed = not blockers and all(row['successes'] >= row['minimum'] for row in thresholds.values())
    return {'corpusVersion': 1, 'profile': bundle['profile'], 'model': profile['model'],
            'profileStatus': profile['status'], 'corpusPassed': passed, 'releaseAccepted': False,
            'monetaryCost': None, 'costPolicy': manifest['costPolicy'],
            'thresholds': thresholds, 'blockers': blockers, 'runs': summaries}


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
