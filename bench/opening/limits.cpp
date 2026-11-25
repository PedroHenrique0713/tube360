#include "yt/media.hpp"
#include "local_library.hpp"
#include "update/app.hpp"
void* __dso_handle;
extern "C" { volatile int64_t results[10];
int64_t range_seek(yt::RangeFile*,int64_t,int);
int range_read(yt::RangeFile*,uint8_t*,int);
void range_fetch(yt::RangeFile*);
bool local_decode(local::Library*,const std::string*);
void __cxa_pure_virtual(){for(;;);}
int getentropy(void*,size_t){return -1;}
NTSTATUS xsdk_thread_move(uint32_t){return -1;}
NTSTATUS xsdk_thread_set_priority(HANDLE,int32_t,int32_t*){return -1;}
NTSTATUS xsdk_thread_sleep(uint32_t){return -1;}
uint32_t xsdk_thread_hardware_thread(){return 0;}
void xsdk_log_write(const char*,...){}
SDL_PropertiesID SDL_CreateProperties(){return 0;}
bool SDL_SetPointerProperty(SDL_PropertiesID,const char*,void*){return false;}
bool SDL_SetStringProperty(SDL_PropertiesID,const char*,const char*){return false;}
bool SDL_SetNumberProperty(SDL_PropertiesID,const char*,Sint64){return false;}
void SDL_DestroyProperties(SDL_PropertiesID){}
SDL_Thread* SDL_CreateThreadWithPropertiesRuntime(SDL_PropertiesID,SDL_FunctionPointer,SDL_FunctionPointer){results[8]=results[8]+1;return nullptr;}
int SDL_AddAtomicInt(SDL_AtomicInt*,int){return 0;}
Uint64 SDL_GetTicks(){return 0;}
void SDL_WaitThread(SDL_Thread*,int*){}
SDL_Thread* SDL_CreateThreadRuntime(SDL_ThreadFunction,const char*,void*,SDL_FunctionPointer,SDL_FunctionPointer){return nullptr;}
}
int64_t resourceSize=INT64_C(0x180000001);
struct Fake:yt::Transport {
 yt::Response request(const yt::string&,const yt::string&,bool) override{return {};}
 yt::Response send(const yt::Request& r) override{
  long long a=0,b=0;sscanf(r.headers[0].c_str(),"Range: bytes=%lld-%lld",&a,&b);
  yt::Response v;v.status=206;v.contentRange=" bytes "+std::to_string(a)+"-"+std::to_string(b)+"/"+std::to_string(resourceSize);v.body.assign(size_t(b-a+1),'x');return v;
 }
};
static void ck(int i,int64_t got,int64_t want){results[0]=results[0]+1;if(got!=want){results[1]=results[1]|(INT64_C(1)<<i);results[2]=got;results[3]=want;}}
int main(){
 yt::RangeFile f(std::make_unique<Fake>(),"https://fixture.googlevideo.com/a",nullptr);f.aheadEnabled=false;
 int64_t sizes[]={INT64_C(0x7fffffff),INT64_C(0x80000000),INT64_C(0x8c89588f),INT64_C(0xdddddddd),INT64_C(0xffffffff),INT64_C(0x100000000),INT64_C(0x180000001)};
 for(auto n:sizes){resourceSize=n;f.length=n;ck(0,range_seek(&f,0,AVSEEK_SIZE),n);ck(1,range_seek(&f,0,SEEK_END),n);ck(2,range_seek(&f,-1,SEEK_END),n-1);ck(3,range_seek(&f,n,SEEK_SET),n);ck(4,range_seek(&f,-n,SEEK_CUR),0);
  f.pos=n-3;f.current={};uint8_t tail[3];ck(15,range_read(&f,tail,3),3);ck(16,f.length,n);ck(17,f.pos,n);ck(18,tail[2],'x');ck(19,range_read(&f,tail,3),AVERROR_EOF);
 }
 resourceSize=INT64_C(0x180000001);
 f.length=INT64_C(0x180000001);f.pos=INT64_C(0x80000000);uint8_t data[3];ck(5,range_read(&f,data,3),3);ck(6,f.pos,INT64_C(0x80000003));ck(7,f.length,INT64_C(0x180000001));ck(8,data[0],'x');
 f.pos=INT64_C(0xdddddddd);f.current={};ck(9,range_read(&f,data,3),3);ck(10,f.pos,INT64_C(0xdddddde0));
 std::string raw=R"({"version":1,"later":[{"id":"abcdefghijk","title":"a","channel":"b","duration":60,"position":20,"order":6442450945},{"id":"bcdefghijkl","title":"c","channel":"d","duration":60,"position":20,"order":2147483648}],"history":[]})";
 local::Library l;ck(11,local_decode(&l,&raw),1);ck(12,l.sequence,INT64_C(0x180000001));if(l.later.size()==2){ck(13,l.later[0].order,INT64_C(0x180000001));ck(14,l.later[1].order,INT64_C(0x80000000));}
 const char* sizesText[]={"67108864","2147483648","3723217268","4294967295","6442450945","-1","9223372036854775807"};
 for(int i=0;i<7;i++){
  std::string manifest=std::string(R"({"schema":1,"kind":"tube360-app","build":6,"version":"0.3.2-rc4","url":"https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.2/tube360.xex","size":)")+sizesText[i]+R"(,"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","notes":{"en":"fixture","pt":"fixture"}})";
  updates::Manifest m;ck(20,updates::decodeManifest(manifest,m),i==0);
 }
 results[9]=0x463436;for(;;)__asm__ volatile("nop");
}
