"""Executa as funções reais do swapchain preparado, com xsdk_present_begin simulado."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--v4x',type=Path,default=ROOT/'.work/native-display/vulkan4xenon');a=p.parse_args()
source=(a.v4x/'src/swapchain.c').read_text()
def function(signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
typedef int32_t NTSTATUS;
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xc00000b5)
#define NT_SUCCESS(x) ((x)>=0)
#define v4x_object(x) ((void*)(uintptr_t)(x))
#define V4X_PROFILE_CALL_ACQUIRE 0
enum {NANOSECONDS_PER_MILLISECOND=1000000,LONGEST_WAIT_MS=60000};
typedef struct {uint32_t address;} Image;
typedef struct {bool acquired;uint32_t acquired_index,image_count;Image images[2];} Swapchain;
static NTSTATUS status;static uint32_t address,wait_ms;static int signals,calls,retry;
static uint32_t v4x_profile_call_start(void){return 0;}
static void v4x_profile_call_end(int kind,uint32_t began){(void)kind;(void)began;}
static void v4x_sync_signal(void* fence){(void)fence;signals++;}
static void boot_event(const char* phase,const char* step,const char* format,...){(void)phase;(void)step;(void)format;}
static NTSTATUS xsdk_present_begin(uint32_t* out,uint32_t ms){
    calls++;wait_ms=ms;*out=address;
    return retry && calls==1?STATUS_IO_TIMEOUT:status;
}
'''
suffix=r'''
int main(void){
    assert(timeout_ms(UINT64_MAX)==60000 && timeout_ms(UINT64_MAX-1)==60000);
    assert(timeout_ms(0)==0 && timeout_ms(1)==1 && timeout_ms(5000000000ULL)==5000);
    Swapchain swap={.image_count=2,.images={{100},{200}}};uint32_t index=99;
    VkSwapchainKHR handle=(VkSwapchainKHR)(uintptr_t)&swap;
    address=200;status=0;
    assert(vkAcquireNextImageKHR(0,handle,5000000000ULL,0,0,&index)==VK_SUCCESS);
    assert(index==1 && swap.acquired && signals==1 && wait_ms==5000);
    assert(vkAcquireNextImageKHR(0,handle,0,0,0,&index)==VK_NOT_READY);
    swap.acquired=false;status=(NTSTATUS)0xc0000001;index=99;
    assert(vkAcquireNextImageKHR(0,handle,5000000000ULL,0,0,&index)==VK_ERROR_DEVICE_LOST);
    assert(!swap.acquired && index==99 && signals==1);
    status=0;address=999;
    assert(vkAcquireNextImageKHR(0,handle,1,0,0,&index)==VK_ERROR_OUT_OF_DATE_KHR);
    assert(!swap.acquired && index==99 && signals==1);
    status=STATUS_IO_TIMEOUT;
    assert(vkAcquireNextImageKHR(0,handle,1,0,0,&index)==VK_TIMEOUT);
    assert(vkAcquireNextImageKHR(0,handle,0,0,0,&index)==VK_NOT_READY);
    status=0;address=100;retry=1;calls=0;
    assert(vkAcquireNextImageKHR(0,handle,UINT64_MAX,0,0,&index)==VK_SUCCESS);
    assert(calls==2 && wait_ms==60000 && index==0 && signals==2);
    puts("PASS real acquire: timeout saturation, success, already acquired, native failure, unknown address, finite timeout, retry");
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-swapchain-') as tmp:
    path=Path(tmp);(path/'test.c').write_text(prefix+function('static uint32_t timeout_ms(')+'\n'+function('VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(')+suffix)
    subprocess.run(['cc','-std=c11',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
