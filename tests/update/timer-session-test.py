#!/usr/bin/env python3
"""Executa powerDownXbox com export falso; observa o marcador C real no instante da chamada."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
build=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'client/build'
source=r'''
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#define LOG(...) ((void)0)
#include "sleep.hpp"
std::string directory,mode;
std::string state(){std::ifstream f(directory+"session.txt");std::string value;std::getline(f,value);return value;}
void power(uint32_t routine){assert(routine==5 && state()=="clean");if(mode=="power")std::_Exit(0);}
extern "C" uint32_t XexGetModuleHandle(const char*,HANDLE* module){*module=1;return 0;}
extern "C" NTSTATUS XexGetProcedureAddress(HANDLE,uint32_t ordinal,void** result){assert(ordinal==0x28);*result=reinterpret_cast<void*>(power);return mode=="missing"?-1:0;}
int main(int argc,char** argv){
    assert(argc==3);directory=std::string(argv[1])+"/";mode=argv[2];
    boot_session_begin(directory.c_str(),"timer test");assert(state()=="open");
    assert(!powerDownXbox());assert(state()=="open");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-timer-session-') as temporary:
    base=Path(temporary);cpp=base/'timer.cpp';cpp.write_text(source)
    boot=list(build.rglob('libtube360-boot.a'));assert len(boot)==1
    binary=base/'timer'
    subprocess.run(['c++','-std=c++20','-D__XSDK__','-I'+str(ROOT/'client/src'),
                    '-I'+str(ROOT/'.work/native-display/xsdk/include'),str(cpp),str(boot[0]),'-pthread','-o',str(binary)],check=True)
    for mode in ('power','return','missing'):
        folder=base/mode;folder.mkdir()
        subprocess.run([str(binary),str(folder),mode],check=True,timeout=5)
        expected='clean' if mode=='power' else 'open'
        assert (folder/'session.txt').read_text().splitlines()[0]==expected
        print('PASS timer session:',mode,'marker='+expected,flush=True)
