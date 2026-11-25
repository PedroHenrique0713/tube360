"""Planta defeitos em cópias privadas e exige falha na asserção/cenário que cada um deveria afetar."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
base = Path(sys.argv[1]).resolve()
base.mkdir(parents=True, exist_ok=True)
policy = (ROOT / 'client/src/video_policy.c').read_text()
display = (ROOT / '.work/native-display/vulkan4xenon/src/display.c').read_text()
mutants = [
    ('lost-owner-native', policy, 'if(native_default)',
     'if(0)', 'policy.py', '--policy-source', 'corona-auto'),
    ('lost-safe-veto', policy, 'if(override>=0){native=override;', 'if(override>=0){native=1;',
     'policy.py', '--policy-source', 'sim-veto-native'),
    ('bypassed-kernel-limit', display,
     '&& sim_height<=(int)tube360_display_height && sim_width<=(int)tube360_display_width',
     '&& 1', 'driver.py', '--source', 'real-kernel-limit'),
]
for name, source, before, after, script, option, expected in mutants:
    assert source.count(before) == 1, ('SETUP FAILED', name)
    folder = base / name
    folder.mkdir(exist_ok=True)
    path = folder / 'mutant.c'
    path.write_text(source.replace(before, after))
    run = subprocess.run(['python3', str(ROOT / 'tests/hardware' / script), str(folder / 'proof'), option, str(path)],
                         capture_output=True, text=True)
    log = run.stdout + run.stderr
    (folder / 'result.log').write_text(log)
    assert run.returncode != 0 and 'AssertionError' in log and expected in log, ('MUTANT SURVIVED OR SETUP FAILED', name, log[-2000:])
    print('PASS mutation killed:', name, 'by', expected, flush=True)
