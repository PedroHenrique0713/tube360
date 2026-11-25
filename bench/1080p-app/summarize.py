#!/usr/bin/env python3
"""Recompute the app gate; desktop and the pre-F25b Nest clock cannot certify Xbox."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


def fields(line):
    return dict(re.findall(r'([a-z][a-z0-9_]*)=([^\s]+)', line))


def number(row, key):
    value = float(row[key])
    if not math.isfinite(value):
        raise ValueError(f'non-finite {key}')
    return value


def summarize(log):
    starts = re.findall(r'F40 APP START ([^\n]+)', log)
    ends = re.findall(r'F40 APP DONE (PASS|FAIL) ([^\n]+)', log)
    if len(starts) != 1 or len(ends) != 1:
        raise ValueError('expected one complete START/DONE session')
    start, done = fields(starts[0]), fields(ends[0][1])
    if 'reason' in done:
        return dict(configuration=start, result='FAIL', console_gate='failed', reason=done['reason'])
    samples = [fields(line) for line in re.findall(r'F40 APP SAMPLE ([^\n]+)', log)]
    if len(samples) < 10 or not log.index('F40 APP START') < log.index('F40 APP SAMPLE') < log.index('F40 APP DONE'):
        raise ValueError('missing or unordered samples')
    previous = -1
    for row in samples:
        now = number(row, 'ms')
        if now <= previous or now - previous > 2500:
            raise ValueError('duplicate/out-of-order/gap in samples')
        previous = now
        for key in ('pcm_ms', 'av_ms', 'free_mib', 'shown', 'lost', 'clock_age_ms'):
            number(row, key)
    elapsed = number(done, 'elapsed_ms')
    if elapsed < number(start, 'seconds') * 1000 or elapsed != number(samples[-1], 'ms'):
        raise ValueError('short or inconsistent measurement')
    shown, lost = number(samples[-1], 'shown'), number(samples[-1], 'lost')
    fps = shown * 1000 / elapsed
    if abs(fps - number(done, 'fps')) > .0011 or lost != number(done, 'lost'):
        raise ValueError('inconsistent FPS/drop counters')
    if number(done, 'av_samples') < shown * .90:
        raise ValueError('too few per-frame A/V observations')
    if number(done, 'av_p95_ms') < 0 or number(done, 'av_max_ms') < number(done, 'av_p95_ms'):
        raise ValueError('invalid A/V percentiles')
    for key in ('source_ok', 'audio_ok', 'policy_ok', 'route_ok'):
        if done[key] == '1' and any(row[key] != '1' for row in samples):
            raise ValueError(f'{key} regressed to success')
    memory_measured = done['memory_measured'] == '1'
    minimum = min(number(row, 'free_mib') for row in samples)
    if minimum != number(done, 'free_min_mib'):
        raise ValueError('inconsistent memory minimum')
    source = all(row['itag'] == start['itag'] and row['source'] in ('1080p29', '1080p30') and row['target'] == '30' for row in samples)
    ok = source and all(done[key] == '1' for key in ('source_ok', 'audio_ok', 'policy_ok', 'route_ok')) and fps >= 29.5 and lost == 0 and number(done, 'av_p95_ms') <= 50
    if start['nest'] == '0':
        ok = ok and number(done, 'silent') == 0
    memory = memory_measured and minimum >= 64 and minimum < 512
    result = 'PASS' if ok and (memory or not memory_measured) else 'FAIL'
    if ends[0][0] != result:
        raise ValueError('DONE status disagrees with measured result')
    if start['platform'] == 'pc':
        gate = 'pending_console'
    elif start['platform'] != 'xbox':
        raise ValueError('unknown platform')
    else:
        placements = re.findall(r'app-thread: [^\n]+? HW (\d+) actual (\d+) priority -?\d+ affinity_status ([\da-fA-F]+) priority_status ([\da-fA-F]+)', log)
        if any(hw != actual or int(status, 16) or int(priority, 16) for hw, actual, status, priority in placements):
            raise ValueError('failed/mismatched app thread placement')
        expected = [0, 2, 4, 1, 3, 5]
        workers = re.findall(r'decod-worker: (\d+) HW (\d+), afinidade ([\da-fA-F]+) prioridade 0 status ([\da-fA-F]+) spread 1', log)
        if not workers or len(workers) % 6:
            raise ValueError('missing six-worker affinity evidence')
        for i, (worker, hw, status, priority) in enumerate(workers):
            if int(worker) != i % 6 + 1 or int(hw) != expected[i % 6] or int(status, 16) or int(priority, 16):
                raise ValueError('wrong/failed decoder worker placement')
        for name, hw, priority in (('presentation', 0, 2), ('youtube', 1, 1), ('range', 3, 1), ('cast-v2', 5, 1), ('atualizações', 5, -1), ('cast-rpc', 5, 0)):
            pattern = rf'app-thread: {name} HW {hw} actual {hw} priority {priority} affinity_status 00000000 priority_status 00000000'
            if not re.search(pattern, log):
                raise ValueError(f'missing actual {name} affinity')
        if not re.search(r'app-audio: HW 1 effective_priority \d+ query_status 0', log):
            raise ValueError('missing actual SDL PCM placement')
        gate = 'pending_f25b_clock' if start['nest'] == '1' and ok and memory else 'pass' if ok and memory and int(start['seconds']) >= 120 and int(start['warmup']) >= 10 else 'failed'
    if done['console_gate'] != gate or (done['qualified'] == '1') != (gate == 'pass'):
        raise ValueError('claimed gate disagrees with recomputed gate')
    return dict(configuration=start, result=result,
                fps=fps, lost=lost, av_p95_ms=number(done, 'av_p95_ms'), samples=len(samples),
                memory_measured=memory_measured, free_min_mib=minimum if memory_measured else None,
                console_gate=gate)


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument('log', type=Path)
    cli.add_argument('output', type=Path)
    args = cli.parse_args()
    data = args.log.read_text(errors='replace')
    try:
        result = summarize(data)
    except (ValueError, KeyError) as error:
        cli.exit(1, f'invalid F40 evidence: {error}\n')
    result['log_sha256'] = hashlib.sha256(args.log.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
