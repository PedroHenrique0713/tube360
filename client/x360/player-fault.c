// ABI medida pelo xsdk no Xenon (docs/HARDWARE.md, milestone 40).
#include <stdint.h>
#include <stdio.h>
#include "boot_log.h"
#include <xsdk/kernel.h>
#include <xsdk/log.h>

static unsigned handling;
static uint32_t word(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static int in_stack(uint32_t p, uint32_t n, uint32_t low, uint32_t high) {
    return !(p & 3) && p >= low && p < high && n <= high - p;
}

static int report(uint32_t pointers) {
    if (__atomic_exchange_n(&handling, 1, __ATOMIC_ACQ_REL)) return 0;
    uint32_t pcr;
    __asm__("mr %0,13" : "=r"(pcr));
    uint32_t low = word(pcr + 0x74), high = word(pcr + 0x70);
    if (in_stack(pointers, 8, low, high)) {
        uint32_t record = word(pointers), ctx = word(pointers + 4);
        if (in_stack(record, 20, low, high))
            xsdk_log_write("excecao: codigo %08x endereco %08x", word(record), word(record + 12));
        if (in_stack(ctx, 0x28, low, high)) {
            uint32_t sp = word(ctx + 0x24);
            xsdk_log_write("pilha: PC %08x LR %08x SP %08x", word(ctx + 8), word(ctx + 12), sp);
            for (unsigned i = 0; i < 32 && in_stack(sp, 4, low, high); ++i) {
                uint32_t next = word(sp);
                if (next <= sp || !in_stack(next, 8, low, high)) break;
                xsdk_log_write("pilha: %u SP %08x retorno %08x", i, sp, word(next + 4));
                sp = next;
            }
        }
    }
    XamLoaderTerminateTitle();
    return 0;
}

void install_player_fault_handler(void) {
    FILE* option=fopen("fault-handler.txt","r");
    int enabled=0;if(option){if(fscanf(option,"%d",&enabled)!=1)enabled=0;fclose(option);}
    if(enabled!=1){boot_event("detect","fault-handler","disabled (default); optional 17559 ABI; no PCR/context offsets read");return;}
    HANDLE kernel;
    void *address = 0,*version=0;
    if(XexGetModuleHandle("xboxkrnl.exe",&kernel)!=0 || XexGetProcedureAddress(kernel,0x158,&version)<0 || !version || ((const uint16_t*)version)[2]!=17559) {
        boot_event("detect","fault-handler","not installed: unknown kernel ABI even though opt-in requested");return;
    }
    boot_event("begin","fault-handler.install","opt-in on kernel 17559; PCR stack offsets 0x70/0x74");
    if (!XexGetModuleHandle("xboxkrnl.exe", &kernel) &&
        XexGetProcedureAddress(kernel, 0x36d, &address) >= 0 && address) {
        typedef uint32_t (*Install)(uintptr_t, uintptr_t);
        uint32_t status = ((Install)address)((uintptr_t)report, (uintptr_t)report);
        xsdk_log_write("excecao: registro %08x", status);
        boot_event("end","fault-handler.install","status=%08x",status);
    } else {xsdk_log_write("excecao: registro indisponivel");boot_event("detect","fault-handler.install","export 0x36d absent; continuing without handler");}
}
