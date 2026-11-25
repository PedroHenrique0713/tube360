#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "objects.h"
#include "host.h"
#include "xsdk/kernel.h"
static int native,gpu_allocs,physical_allocs,cache_ops,sync_ops;
void fixture_cache(uintptr_t address){(void)address;++cache_ops;}
void fixture_sync(void){++sync_ops;}
uint32_t tube360_display_width=1280,tube360_display_height=720;
int tube360_native_video(void){return native;}
void xsdk_log_write(const char* fmt,...){(void)fmt;}
void* v4x_host_alloc(const VkAllocationCallbacks* a,size_t size,VkSystemAllocationScope scope){(void)a;(void)scope;return calloc(1,size);}
void v4x_host_free(const VkAllocationCallbacks* a,void* p){(void)a;free(p);}
const VkAllocationCallbacks* v4x_host_device_allocator(VkDevice d,const VkAllocationCallbacks* a){(void)d;return a;}
volatile uint32_t* xsdk_gpu_alloc(uint32_t bytes){++gpu_allocs;return malloc(bytes);}
uint32_t xsdk_gpu_address(const volatile uint32_t* p){(void)p;return 0x200000;}
void* MmAllocatePhysicalMemoryEx(uint32_t t,uint32_t bytes,uint32_t p,uint32_t lo,uint32_t hi,uint32_t a){(void)t;(void)p;(void)lo;(void)hi;(void)a;++physical_allocs;return malloc(bytes);}
int main(int argc,char** argv){
    assert(argc==2);native=!strcmp(argv[1],"native");
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(0,&memory);assert(memory.memoryTypeCount==(native?3u:2u));
    VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(0,VK_FORMAT_R8_UNORM,&props);
    assert(!!(props.linearTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)==native);
    VkImageFormatProperties imageProps;
    VkResult result=vkGetPhysicalDeviceImageFormatProperties(0,VK_FORMAT_R8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_LINEAR,VK_IMAGE_USAGE_SAMPLED_BIT,0,&imageProps);
    assert((result==VK_SUCCESS)==native);
    Image image={.is_texture=true};image.texture.size_bytes=4096;VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(0,(VkImage)(uintptr_t)&image,&requirements);assert(requirements.memoryTypeBits==(native?5u:1u));
    VkMemoryAllocateInfo allocate={.allocationSize=4096,.memoryTypeIndex=2};VkDeviceMemory handle=0;
    result=vkAllocateMemory(0,&allocate,0,&handle);assert((result==VK_SUCCESS)==native);
    assert(physical_allocs==native && gpu_allocs==0);
    unsigned char cache[128];DeviceMemory cached={.physical_cached=true,.pointer=(volatile uint32_t*)cache,.size_bytes=128};
    VkMappedMemoryRange range={.memory=(VkDeviceMemory)(uintptr_t)&cached,.size=128};
    assert(vkFlushMappedMemoryRanges(0,1,&range)==VK_SUCCESS && vkInvalidateMappedMemoryRanges(0,1,&range)==VK_SUCCESS);
    assert((cache_ops>0)==native && sync_ops==(native?2:0));
    if(handle){DeviceMemory* object=v4x_object(handle);free((void*)object->pointer);free(object);}
    allocate.memoryTypeIndex=0;assert(vkAllocateMemory(0,&allocate,0,&handle)==VK_SUCCESS);assert(gpu_allocs==1);
    DeviceMemory* object=v4x_object(handle);free((void*)object->pointer);free(object);
    printf("PASS memory %s types=%u linear=%u bits=%x physical_allocs=%d staging_gpu_allocs=%d cache_ops=%d sync_ops=%d\n",argv[1],memory.memoryTypeCount,props.linearTilingFeatures,requirements.memoryTypeBits,physical_allocs,gpu_allocs,cache_ops,sync_ops);
}
