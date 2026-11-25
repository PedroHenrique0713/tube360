"""Executa output/quiesce reais com __XSDK__, mocks Vulkan estritos e nenhuma GPU."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'client/src/gfx.hpp').read_text()
def function(signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
prefix=r'''
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>
enum{TUBE360_SIM_OUTPUT}; int tube360_board_sim_step(unsigned){return 0;}
static bool native,kernel,failIdle,failReset,idle,reset;static int mutations,setter;
extern "C" int tube360_native_video(){return native;}
extern "C" int tube360_output_fixed(){return !native || kernel;}
void boot_event(const char*,const char*,const char*,...){}
#define LOG(...) ((void)0)
namespace gfx {[[noreturn]] void fail(const char*){throw std::runtime_error("GPU failure");}}
extern "C" VkResult vkDeviceWaitIdle(VkDevice){idle=!failIdle;return failIdle?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
extern "C" VkResult vkResetCommandPool(VkDevice,VkCommandPool,VkCommandPoolResetFlags){assert(idle);reset=!failReset;return failReset?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
#define DESTROY(name,type) extern "C" void name(VkDevice,type,const VkAllocationCallbacks*){assert(idle && reset);++mutations;}
DESTROY(vkDestroyPipeline,VkPipeline)
DESTROY(vkDestroyFramebuffer,VkFramebuffer)
DESTROY(vkDestroyImageView,VkImageView)
DESTROY(vkDestroyRenderPass,VkRenderPass)
DESTROY(vkDestroySwapchainKHR,VkSwapchainKHR)
extern "C" void vkDestroySurfaceKHR(VkInstance,VkSurfaceKHR,const VkAllocationCallbacks*){assert(idle && reset);++mutations;}
extern "C" int tube360_set_output(int h){assert(idle && reset);++setter;return h;}
namespace gfx {
enum Pipe {SHAPE};enum{PIPE_COUNT=1};
struct Gfx {
 VkDevice dev{};VkInstance inst{};VkCommandPool cmdPool{};VkSurfaceKHR surface{};VkRenderPass pass{};VkSwapchainKHR swap{};
 VkPipeline pipes[1]{};std::vector<VkFramebuffer> fbs{0};std::vector<VkImageView> views{0};
 std::shared_ptr<int> retained[2];struct Sample{bool pending=true;};Sample samples[2];
 VkExtent2D extent{1280,720};float scale=1;bool bootFrame=false,outputRejected=false;
};
VkSurfaceKHR displaySurface(Gfx&){assert(idle && reset);return 1;}
void makeSwapchain(Gfx& g,int w,int h){assert(idle && reset);g.extent={(uint32_t)w,(uint32_t)h};}
VkPipeline makePipeline(Gfx&,Pipe){assert(idle && reset);return 1;}
'''
macro=next(line for line in source.splitlines() if line.startswith('#define VK('))
body=function('inline void quiesce(')+'\n'+function('inline bool fixedOutput(')+'\n'+function('inline bool output(')+'\n}\n'
suffix=r'''
int main(){
 gfx::Gfx g;g.retained[0]=std::make_shared<int>(1);
 assert(!gfx::output(g,1080) && !idle && !reset && !mutations && !setter && g.retained[0]);
 native=true;kernel=true;
 assert(!gfx::output(g,1080) && !idle && !reset && !mutations && !setter && g.retained[0]);
 kernel=false;failIdle=true;
 try{gfx::output(g,1080);assert(false);}catch(const std::runtime_error&){}
 assert(!mutations && !setter && g.retained[0]);
 failIdle=false;failReset=true;
 try{gfx::output(g,1080);assert(false);}catch(const std::runtime_error&){}
 assert(!mutations && !setter && g.retained[0]);
 failReset=false;
 assert(gfx::output(g,1080) && mutations && setter==1 && !g.retained[0] && g.extent.height==1080 && g.bootFrame);
 puts("PASS native settings: safe is a complete no-op; idle/reset failures retire nothing; native change after GPU stopped");
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-settings-native-') as tmp:
    work=Path(tmp);(work/'test.cpp').write_text(prefix+macro+'\n'+body+suffix)
    subprocess.run(['c++','-std=c++17','-D__XSDK__','-DVK_USE_64_BIT_PTR_DEFINES=0',str(work/'test.cpp'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
