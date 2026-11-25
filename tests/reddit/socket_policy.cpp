#include <xsdk/kernel.h>
#include <xsdk/net.h>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
extern "C" int tube360_test_socket(int,int,int);
extern "C" const XsdkNetworkLimits xsdk_network_limits={32,8,63,16};
static const char* mode;
static int starts,closes,lastError;
extern "C" void xsdk_log_write(const char*,...){}
extern "C" void boot_event(const char* phase,const char* step,const char* fmt,...){
    std::printf("%s %s ",phase,step);va_list args;va_start(args,fmt);std::vprintf(fmt,args);va_end(args);std::puts("");
}
extern "C" int32_t NetDll_XNetStartup(uint32_t,const uint8_t* params){
    ++starts;if(!std::strcmp(mode,"limits") && params)return 10022;
    return !std::strcmp(mode,"startup-denied")?10013:0;
}
extern "C" int32_t NetDll_WSAStartup(uint32_t,uint16_t,void*){return !std::strcmp(mode,"wsa-failed")?10050:0;}
extern "C" int32_t NetDll_socket(uint32_t,int32_t,int32_t,int32_t){return 101;}
extern "C" int32_t NetDll_closesocket(uint32_t,int32_t){++closes;return 0;}
extern "C" int32_t NetDll_WSAGetLastError(void){return lastError;}
extern "C" int32_t NetDll_setsockopt(uint32_t,int32_t,int32_t,int32_t,const void*,int32_t){
    if(!std::strcmp(mode,"unsupported"))lastError=10042;
    else if(!std::strcmp(mode,"not-supported"))lastError=10045;
    else if(!std::strcmp(mode,"denied"))lastError=10013;
    else return 0;
    return -1;
}
int main(int argc,char** argv){
    assert(argc==2);mode=argv[1];int fd=tube360_test_socket(AF_INET,SOCK_STREAM,0);
    bool deny=!std::strcmp(mode,"denied"),startup=!std::strcmp(mode,"startup-denied"),wsa=!std::strcmp(mode,"wsa-failed");
    assert((fd<0)==(deny||startup||wsa));
    if(deny)assert(errno==EACCES && closes==1);
    if(startup||wsa)assert(errno==ENETDOWN);
    if(!std::strcmp(mode,"limits"))assert(starts==2);
    else assert(starts==1);
    if(!deny)assert(!closes);
    std::printf("PASS policy %s fd=%d errno=%d starts=%d closes=%d\n",mode,fd,errno,starts,closes);
}
