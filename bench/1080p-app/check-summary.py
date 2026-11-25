#!/usr/bin/env python3
"""Synthetic harness calibration only: this file does not contain console evidence."""
import importlib.util
from pathlib import Path
import json

base = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('f40summary', base/'summarize.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
start = 'F40 APP START platform=xbox seconds=120 warmup=10 itag=137 nest=0 loop=48 id=4p_9OPKgcDE nest_clock=f25b_pending\n'
workers = ''.join(f'decod-worker: {i+1} HW {hw}, afinidade 00000000 prioridade 0 status 00000000 spread 1\n' for i, hw in enumerate((0,2,4,1,3,5)))
threads = ''.join(f'app-thread: {name} HW {hw} actual {hw} priority {prio} affinity_status 00000000 priority_status 00000000\n' for name,hw,prio in (('presentation',0,2),('youtube',1,1),('range',3,1),('cast-v2',5,1),('atualizações',5,-1),('cast-rpc',5,0)))
threads += 'app-audio: HW 1 effective_priority 9 query_status 0\n'
samples = ''.join(f'F40 APP SAMPLE ms={i*1000} itag=137 source=1080p30 target=30 shown={i*30} lost=0 queue=18 pcm_ms=6000 av_ms=10 clock_age_ms=0 free_mib=100 silent=0 feeder_wait_us=20 source_ok=1 audio_ok=1 policy_ok=1 route_ok=1\n' for i in range(121))
done = 'F40 APP DONE PASS fps=30.000 lost=0 source_ok=1 audio_ok=1 policy_ok=1 route_ok=1 av_samples=3601 av_p95_ms=20 av_max_ms=30 present_gap_p95_ms=34 queue_min=18 free_min_mib=100 memory_measured=1 silent=0 elapsed_ms=120000 console_gate=pass qualified=1\n'
valid = workers+threads+start+samples+done
assert module.summarize(valid)['console_gate'] == 'pass'
pc = valid.replace('platform=xbox','platform=pc').replace('console_gate=pass qualified=1','console_gate=pending_console qualified=0')
assert module.summarize(pc)['console_gate'] == 'pending_console'
nest = valid.replace('nest=0','nest=1').replace('console_gate=pass qualified=1','console_gate=pending_f25b_clock qualified=0')
assert module.summarize(nest)['console_gate'] == 'pending_f25b_clock'
muted = valid.replace('nest_clock=f25b_pending', 'nest_clock=f25b_pending muted=1').replace('console_gate=pass qualified=1', 'console_gate=pending_audio qualified=0')
assert module.summarize(muted)['console_gate'] == 'pending_audio'
mutants = {
    'partial': valid.replace(done,''),
    'duplicate_session': valid+valid,
    'wrong_affinity': valid.replace('HW 0, afinidade','HW 2, afinidade',1),
    'priority_failure': valid.replace('status 00000000 spread','status c0000001 spread',1),
    'missing_tls': valid.replace('app-thread: range','unobserved: range'),
    'wrong_pcm_hw': valid.replace('app-audio: HW 1','app-audio: HW 0'),
    'false_fps': valid.replace('fps=30.000','fps=31.000'),
    'nan_av': valid.replace('av_p95_ms=20','av_p95_ms=nan'),
    'negative_av': valid.replace('av_p95_ms=20','av_p95_ms=-20'),
    'false_done_status': valid.replace('DONE PASS','DONE FAIL'),
    'later_tls_mismatch': valid.replace(start, 'app-thread: range HW 3 actual 5 priority 1 affinity_status 00000000 priority_status 00000000\n'+start),
    'bad_source': valid.replace('itag=137 source=1080p30','itag=136 source=720p30'),
    'source_flag_regression': valid.replace('source_ok=1 audio_ok=1 policy_ok=1 route_ok=1\n','source_ok=0 audio_ok=1 policy_ok=1 route_ok=1\n',1),
    'sample_gap': valid.replace('ms=60000 itag','ms=63001 itag'),
    'too_few_av_samples': valid.replace('av_samples=3601','av_samples=120'),
    'memory_63': valid.replace('free_mib=100','free_mib=63').replace('free_min_mib=100','free_min_mib=63'),
    'pc_claims_console': pc.replace('pending_console qualified=0','pass qualified=1'),
    'nest_claims_console': nest.replace('pending_f25b_clock qualified=0','pass qualified=1'),
    'muted_claims_console': muted.replace('pending_audio qualified=0','pass qualified=1'),
}
for name, mutant in mutants.items():
    assert mutant != valid, f'{name}: mutation did not apply'
    try:
        result = module.summarize(mutant)
    except (ValueError,KeyError):
        continue
    assert result['console_gate'] != 'pass', f'{name} certified false evidence'
result = dict(synthetic_calibration=True, valid_controls=4, invalid_logs_rejected=len(mutants))
(base/'results/summary-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(result)
