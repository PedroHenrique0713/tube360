"""Audita os ELF finais: hooks do CRT ligados e imports realmente presentes no XEX."""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path)
p.add_argument('--sdk',type=Path,default=ROOT/'.work/native-display/xsdk')
p.add_argument('--toolchain',required=True,help='prefixo completo, terminando em powerpc-xsdk-elf-')
a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
catalog={}
for line in (a.sdk/'imports/imports.txt').read_text().splitlines():
    parts=line.split()
    if len(parts)>=3 and parts[1].startswith('0x'):catalog[parts[0],int(parts[1],16)]=parts[2]
spec=importlib.util.spec_from_file_location('xex',a.sdk/'tools/elf2xex.py');xex=importlib.util.module_from_spec(spec);spec.loader.exec_module(xex)
for title in ('tube360','tube360-launcher'):
    elf=ROOT/'client/build-x360'/title
    symbols=subprocess.check_output([a.toolchain+'nm',str(elf)],text=True)
    for hook in ('tube360_boot_raw','tube360_boot_raw_close','tube360_boot_early','boot_event','tube360_video_policy_init','tube360_native_video',
                 'tube360_video_policy_normal_log','tube360_board_sim_init','tube360_board_sim_step','tube360_board_sim_display_ok','tube360_board_sim_extent'):
        assert re.search(r'^[0-9a-f]+ T '+hook+r'$',symbols,re.M),(title,hook)
    crt=subprocess.check_output([a.toolchain+'objdump','-d','--disassemble=xsdk_start',str(elf)],text=True)
    assert crt.index('<tube360_boot_raw>')<crt.index('<xsdk_lock_init>')<crt.index('<xsdk_reent_init>')<crt.index('<tube360_boot_early>')<crt.index('<main>'),crt
    assert crt.index('<xsdk_log_open>')<crt.index('<tube360_video_policy_normal_log>')<crt.index('<main>'),crt
    (a.evidence/(title+'-crt.txt')).write_text(crt)
    _,sections=xex.read_elf(elf.read_bytes());image,_=xex.build_image(sections)
    imports=xex.read_imports(sections,image)
    names={int(address,16):name for address,name in re.findall(r'^([0-9a-f]+) R xsdk_imp_(\w+)$',symbols,re.M)}
    rows=[]
    for library,entries in imports.items():
        assert library.lower()!='xbdm.xex'
        for entry in entries:
            assert not (library=='xboxkrnl.exe' and entry['ordinal']==3),'DbgPrint must remain optional'
            assert not (library=='xboxkrnl.exe' and entry['ordinal']==0x156),'Board export must remain optional'
            name=names.get(entry['slot'],catalog.get((library,entry['ordinal']),'?'))
            rows.append(f"{library:15} ordinal={entry['ordinal']:04x} {name}")
    (a.evidence/(title+'-imports.txt')).write_text('\n'.join(rows)+'\n')
    print('PASS linked CRT order/hooks and actual imports:',title,len(rows),'imports; no XBDM/DbgPrint requirement',flush=True)
