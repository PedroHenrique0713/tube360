"""Mídia sintética da bancada existente, com bitrate medido para a decisão de recuperação."""
import json
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[2]
folder=Path(sys.argv[1]).resolve()
subprocess.run([sys.executable,str(root/'bench/quality/prepare.py'),str(folder),'--mode','both','--seconds','80'],check=True)
path=folder/'player.json';player=json.loads(path.read_text())
for fmt in player['streamingData']['adaptiveFormats']:
    media=folder/fmt['url'].rsplit('/',1)[1]
    fmt['bitrate']=int(media.stat().st_size*8/80)
path.write_text(json.dumps(player))
