// Usa Worker/ThreadPolicy reais. Só as chamadas XSDK são adaptadas ao scheduler Linux.
#include "sponsorblock_worker.hpp"
#include <mbedtls/ecdsa.h>
#include <sys/resource.h>
#include <sched.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
int cpu=0;
bool control=false;
thread_local int relativePriority=0;
std::atomic_bool stopLoad{false}, startLoad{false}, networkDone{false}, logReleased{false};
std::atomic_int started{0}, failures{0}, restored{-999}, requested{-999}, received{-999};
std::atomic_uint64_t loadWork{0};
double cryptoMs=0, requestMs=0;
std::string body;
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
int pin(int target) { cpu_set_t set; CPU_ZERO(&set); CPU_SET(target,&set); return pthread_setaffinity_np(pthread_self(),sizeof(set),&set); }
double threadMs() { timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
int randomBytes(void*,unsigned char* bytes,size_t n) {
    // Chaves sintéticas descartáveis; nunca serve a um TLS ou credencial real.
    static thread_local uint32_t state=7;
    while (n--) { state=state*1664525+1013904223; *bytes++=(unsigned char)(state>>24); }
    return 0;
}
struct Transport : yt::Transport {
    const std::atomic_bool* cancel;
    explicit Transport(const std::atomic_bool* flag):cancel(flag) {}
    bool cancelled() const override { return cancel && *cancel; }
    yt::Response request(const std::string&,const std::string&,bool) override { throw std::runtime_error("send expected"); }
    yt::Response send(const yt::Request& request) override {
        auto wall=SDL_GetTicks(); auto start=threadMs();
        mbedtls_ecdsa_context key; mbedtls_ecdsa_init(&key);
        int rc=mbedtls_ecdsa_genkey(&key,MBEDTLS_ECP_DP_SECP384R1,randomBytes,nullptr);
        unsigned char hash[32]={7}, signature[MBEDTLS_ECDSA_MAX_LEN]; size_t size=0;
        if (request.trace) request.trace("tls_begin",0);
        while (!rc && !cancelled() && threadMs()-start<600 && SDL_GetTicks()-wall<(uint64_t)request.timeoutMs)
            rc=mbedtls_ecdsa_write_signature(&key,MBEDTLS_MD_SHA256,hash,sizeof(hash),signature,sizeof(signature),&size,randomBytes,nullptr);
        cryptoMs=threadMs()-start; requestMs=(double)(SDL_GetTicks()-wall);
        mbedtls_ecdsa_free(&key);
        bool ok=!rc && cryptoMs>=600 && requestMs<2000;
        if (ok && request.trace) request.trace("tls_done",requestMs);
        yt::Response response;
        response.seconds=requestMs/1000;
        response.timing.phase=ok ? "done" : "tls";
        if (ok) response.timing.tlsMs=requestMs;
        if (ok) { response.status=200; response.body=body; }
        else { response.failure=yt::Failure::NETWORK; response.error="simulated CPU-starved handshake timeout"; response.transportCode=28; }
        networkDone=true;
        return response;
    }
};
}

NTSTATUS xsdk_thread_move(uint32_t hw) { return hw==5 ? pin(cpu) : -1; }
NTSTATUS xsdk_thread_sleep(uint32_t ms) { if (ms) SDL_Delay(ms); else sched_yield(); return 0; }
uint32_t xsdk_thread_hardware_thread() { return sched_getcpu()==cpu ? 5 : 0; }
NTSTATUS xsdk_thread_set_priority(HANDLE,int32_t level,int32_t* previous) {
    if (previous) *previous=relativePriority;
    if (level==sponsorblock::ThreadPolicy::priority) requested=level;
    int effective=control && level>1 ? 1 : level;
    int nice=effective>=3 ? 0 : effective==1 ? 10 : 15;
    if (setpriority(PRIO_PROCESS,0,nice) || getpriority(PRIO_PROCESS,0)!=nice) return -1;
    relativePriority=effective;
    if (level==0) restored=effective;
    return 0;
}
int f34_getschedparam(pthread_t,int* policy,sched_param* value) {
    int nice=getpriority(PRIO_PROCESS,0);
    int measured=nice==0 ? 3 : nice==10 ? 1 : nice==15 ? 0 : -999;
    *policy=SCHED_OTHER; value->sched_priority=measured;
    if (networkDone && measured==0) restored=0;
    else received=measured;
    return 0;
}
void xsdk_log_write(const char* format,...) {
    // Um logger preso não pode atrasar a publicação dos dados à UI.
    while (networkDone && !logReleased) SDL_Delay(1);
    va_list args; va_start(args,format); std::vprintf(format,args); va_end(args); std::puts("");
}
namespace yt { std::unique_ptr<Transport> https(const std::atomic_bool* flag) { return std::make_unique<::Transport>(flag); } }

int main(int argc,char** argv) {
    if (argc!=3) return 2;
    control=std::string(argv[1])=="control";
    std::ifstream file(argv[2]); body.assign(std::istreambuf_iterator<char>(file),{});
    SDL_Init(0);
    std::vector<std::thread> workers;
    try {
        cpu_set_t allowed; CPU_ZERO(&allowed); require(!sched_getaffinity(0,sizeof(allowed),&allowed),"affinity query");
        int ui=-1; cpu=-1;
        for (int i=0;i<CPU_SETSIZE;++i) if (CPU_ISSET(i,&allowed)) { if (cpu<0) cpu=i; else { ui=i; break; } }
        require(cpu>=0 && ui>=0 && !pin(ui),"two allowed CPUs required");
        for (int n=0;n<6;++n) workers.emplace_back([] {
            if (pin(cpu) || setpriority(PRIO_PROCESS,0,10) || getpriority(PRIO_PROCESS,0)!=10) ++failures;
            ++started;
            while (!startLoad && !stopLoad) sched_yield();
            uint64_t value=7;
            while (!stopLoad) {
                for (int i=0;i<10000;++i) value=value*6364136223846793005ULL+1;
                loadWork.fetch_add(value,std::memory_order_relaxed);
            }
        });
        auto until=SDL_GetTicks()+3000;
        while (started!=6 && SDL_GetTicks()<until) SDL_Delay(1);
        require(started==6 && !failures,"six workers ready on the request CPU");
        startLoad=true;
        sponsorblock::Worker request; request.init(); sponsorblock::Playback state;
        auto selected=SDL_GetTicks(); request.select("wWC8wVI4E3E");
        uint64_t ticks=0; bool published=false;
        while (SDL_GetTicks()-selected<2500) {
            ++ticks;
            if (request.poll(state)) { published=true; break; }
            SDL_Delay(1);
        }
        auto applied=SDL_GetTicks()-selected;
        logReleased=true; request.close();
        stopLoad=true; for (auto& worker:workers) worker.join(); workers.clear();
        require(published && restored==0 && requested==3,"publication before blocked logger and priority restoration");
        require(ticks>50 && loadWork!=0,"UI progresses while all six workers spin without yielding");
        if (control) require(state.segments.empty() && cryptoMs<600 && received==1,"control must reproduce CPU starvation");
        else require(state.segments.size()==1 && applied<=2000 && cryptoMs>=600 && received==3,"boosted request must get 600 ms of CPU and publish within two seconds");
        std::printf("PASS %s: workers=6 cpu=%d worker_nice=10 request_nice=%d requested=%d effective=%d restored=%d ecc_cpu_ms=%.1f request_ms=%.1f applied_ms=%llu UI_ticks=%llu\n",
            control?"same-priority control":"boosted",cpu,control?10:0,requested.load(),received.load(),restored.load(),cryptoMs,requestMs,
            (unsigned long long)applied,(unsigned long long)ticks);
    } catch (const std::exception& error) {
        logReleased=true; stopLoad=true;
        for (auto& worker:workers) if (worker.joinable()) worker.join();
        std::fprintf(stderr,"FAIL %s\n",error.what()); return 1;
    }
    SDL_Quit();
}
