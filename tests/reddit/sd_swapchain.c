// SDK display/present + V4X surface/swapchain reais; fronteiras GPU/kernel simuladas.
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include "gpu_private.h"
#include "host.h"
#include "objects.h"
#include "xsdk/tube360_video.h"
#include "xsdk/present.h"
extern int tube360_set_output(int);
extern int tube360_output_fixed(void);
extern float tube360_output_aspect(void);
static int native,allocs,interrupts,scaler_writes,trace_done;
static VIDEO_MODE video;
static uint32_t width,height;
static volatile uint32_t* buffers[3];
int tube360_native_video(void){return native;}
void boot_event(const char* phase,const char* step,const char* fmt,...){
    if(!strcmp(step,"display.scaler-write") && !strcmp(phase,"begin"))++scaler_writes;
    if(!strcmp(step,"display.first-show") && !strcmp(phase,"end"))trace_done=1;
    printf("%s %s ",phase,step);va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);puts("");
}
void xsdk_log_write(const char* fmt,...){(void)fmt;}
void VdQueryVideoMode(VIDEO_MODE* out){*out=video;}
NTSTATUS xsdk_thread_sleep(uint32_t ms){(void)ms;gpu_registers[0x1928]=2;gpu_registers[0x183b]=0;return 0;}
int fixture_clock_gettime(clockid_t c,struct timespec* t){(void)c;t->tv_sec=1;t->tv_nsec=0;return 0;}
void fixture_barrier(void){}
void xsdk_splash_end(void){}
void xsdk_splash_free(void){}
bool xsdk_splash_hand_over(uint32_t* w,uint32_t* h){(void)w;(void)h;assert(0 && "SD must not take a 720p splash");return false;}
void VdSetGraphicsInterruptCallback(VdGraphicsInterruptCallback cb,void* ctx){(void)cb;(void)ctx;assert(trace_done);++interrupts;}
uint32_t xsdk_gpu_resolve_bytes(uint32_t w,uint32_t h){return w*h*4;}
volatile uint32_t* xsdk_gpu_alloc(uint32_t size){assert(allocs<3);return buffers[allocs++]=calloc(1,size);}
void xsdk_gpu_free(volatile uint32_t* p){free((void*)p);}
uint32_t xsdk_gpu_address(const volatile uint32_t* p){for(int i=0;i<allocs;++i)if(p==buffers[i])return (i+1)*0x100000;abort();}
volatile uint32_t* xsdk_present_pixels(uint32_t address){return buffers[address/0x100000-1];}
uint32_t xsdk_present_buffer_address(uint32_t i){assert(i<(uint32_t)allocs);return (i+1)*0x100000;}
void xsdk_present_close(void){abort();} // Só ocorreria com dimensões divergentes na criação.
NTSTATUS xsdk_texture_init(XsdkTexture* t,uint32_t f,uint32_t w,uint32_t h,uint32_t levels,bool tiled){
    (void)f;(void)levels;(void)tiled;assert(w==width && h==height);t->size_bytes=w*h*4;return 0;
}
void xsdk_texture_attach(XsdkTexture* t,volatile uint32_t* p){(void)t;(void)p;}
void* v4x_host_alloc(const VkAllocationCallbacks* a,size_t n,VkSystemAllocationScope s){(void)a;(void)s;return calloc(1,n);}
void v4x_host_free(const VkAllocationCallbacks* a,void* p){(void)a;free(p);}
const VkAllocationCallbacks* v4x_host_device_allocator(VkDevice d,const VkAllocationCallbacks* a){(void)d;return a;}
VkResult v4x_host_unsupported(const char* what){fprintf(stderr,"unexpected unsupported: %s\n",what);abort();}
VkResult v4x_host_enumerate(uint32_t* count,void* out,const void* items,uint32_t n,size_t bytes){
    if(!out){*count=n;return VK_SUCCESS;}assert(*count>=n);memcpy(out,items,n*bytes);*count=n;return VK_SUCCESS;
}
int main(int argc,char** argv){
    assert(argc==2);native=!strncmp(argv[1],"native",6);
    video=(VIDEO_MODE){.display_width=640,.display_height=480,.is_interlaced=1,.refresh_rate=59.94f};
    width=640;height=480;
    if(strstr(argv[1],"576")){video.display_height=576;video.refresh_rate=50;height=576;}
    if(strstr(argv[1],"480p"))video.is_interlaced=0;
    if(strstr(argv[1],"wide")){width=720;video.display_width=720;video.is_widescreen=1;}
    void* mmio=mmap((void*)0x7fc80000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mmio==(void*)0x7fc80000);gpu_registers[0x1961]=width<<16|height;
    if(strstr(argv[1],"invalid")) {
        gpu_registers[0x1961]=0;
        assert(tube360_set_output(1080)==0 && !tube360_display_width && !tube360_display_height && !allocs && !interrupts);
        munmap(mmio,0x20000);puts("PASS SD pipeline invalid viewport rejected before any allocation/interrupt; no 720p assumption");return 0;
    }
    gpu_registers[0x1844]=0x100000;gpu_registers[0x1848]=width;gpu_registers[0x184d]=width;gpu_registers[0x184e]=height;
    const unsigned registers[]={0x1964,0x1965,0x1961,0x197a,0x196c,0x196d,0x196e,0x1970,0x1971,0x1972};
    uint32_t scaler[10];for(unsigned i=0;i<10;++i)scaler[i]=gpu_registers[registers[i]];
    assert(tube360_set_output(1080)==(int)height && tube360_output_fixed());
    assert(tube360_output_aspect()==(video.is_widescreen?16.f/9:4.f/3));
    VkDisplayPropertiesKHR props;uint32_t n=1;
    assert(vkGetPhysicalDeviceDisplayPropertiesKHR(0,&n,&props)==VK_SUCCESS);
    VkDisplayModePropertiesKHR mode;n=1;assert(vkGetDisplayModePropertiesKHR(0,props.display,&n,&mode)==VK_SUCCESS);
    assert(mode.parameters.visibleRegion.width==width && mode.parameters.visibleRegion.height==height);
    VkDisplaySurfaceCreateInfoKHR surfaceInfo={.displayMode=mode.displayMode,.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,.imageExtent={width,height}};
    struct VkInstance_T instance={0};VkSurfaceKHR surface;
    assert(vkCreateDisplayPlaneSurfaceKHR(&instance,&surfaceInfo,0,&surface)==VK_SUCCESS);
    VkSurfaceCapabilitiesKHR caps;assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(0,surface,&caps)==VK_SUCCESS);
    assert(caps.currentExtent.width==width && caps.currentExtent.height==height);
    VkSwapchainCreateInfoKHR info={.surface=surface,.minImageCount=2,.imageFormat=VK_FORMAT_B8G8R8A8_UNORM,.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,.imageExtent={width,height},.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=VK_PRESENT_MODE_FIFO_KHR};
    VkSwapchainKHR chain;assert(vkCreateSwapchainKHR(0,&info,0,&chain)==VK_SUCCESS);
    assert(allocs==2 && interrupts==1 && !scaler_writes);
    assert(gpu_registers[0x1961]==(width<<16|height));
    for(unsigned i=0;i<10;++i)assert(gpu_registers[registers[i]]==scaler[i]);
    Swapchain* result=v4x_object(chain);assert(result->image_count==2);
    for(int i=0;i<2;++i)assert(result->images[i].width==width && result->images[i].height==height);
    for(int i=0;i<allocs;++i)free((void*)buffers[i]);
    free(result);vkDestroySurfaceKHR(&instance,surface,0);munmap(mmio,0x20000);
    printf("PASS SD pipeline %s surface=swapchain=present=%ux%u buffers=%d interrupt=%d custom_scaler_writes=%d\n",argv[1],width,height,allocs,interrupts,scaler_writes);
}
