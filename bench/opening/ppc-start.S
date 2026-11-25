.text
.globl _start
_start:
 li 0,0x2000
 oris 0,0,0x200
 mtmsr 0
 isync
 lis 1,0x1800
 li 0,0
 stwu 1,-64(1)
 bl main
1: b 1b
