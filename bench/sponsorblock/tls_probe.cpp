#include "yt/sponsorblock.hpp"
#include <mbedtls/build_info.h>
#include <mbedtls/version.h>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#ifdef __XSDK__
#include <xsdk/log.h>
#define REPORT xsdk_log_write
#else
#define REPORT(...) do { std::printf(__VA_ARGS__); std::puts(""); std::fflush(stdout); } while (0)
#endif

#if !defined(MBEDTLS_ECDSA_C) || !defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED) || !defined(MBEDTLS_ECP_DP_SECP384R1_ENABLED)
#error SponsorBlock needs ECDSA, P-256 and P-384
#endif
namespace {
struct Probe { const char* url=nullptr; int result=1; };
void* run(void* ptr) {
    auto& probe=*static_cast<Probe*>(ptr);
    REPORT("TLS probe MbedTLS=%s config=%s epoch=%lld ecdsa=%d p256=%d p384=%d",MBEDTLS_VERSION_STRING,
        MBEDTLS_USER_CONFIG_FILE,(long long)std::time(nullptr),
        !mbedtls_version_check_feature("MBEDTLS_ECDSA_C"),
        !mbedtls_version_check_feature("MBEDTLS_ECP_DP_SECP256R1_ENABLED"),
        !mbedtls_version_check_feature("MBEDTLS_ECP_DP_SECP384R1_ENABLED"));
    auto net=yt::https();
    yt::Request q; q.url=probe.url ? probe.url : yt::sponsorblock::requestUrl("wWC8wVI4E3E"); q.maxBytes=512*1024;
    q.connectTimeoutMs=yt::sponsorblock::connectTimeoutMs; q.timeoutMs=yt::sponsorblock::requestTimeoutMs;
    q.trace=[](const char* phase,double ms) { REPORT("TLS probe phase=%s elapsed_ms=%.1f",phase,ms); };
    auto response=net->send(q);
    yt::sponsorblock::Diagnostic diagnostic;
    auto segments=probe.url ? yt::sponsorblock::Segments{} : yt::sponsorblock::parse(response.body,"wWC8wVI4E3E",&diagnostic);
    char verification[32];
    if (response.tlsVerify<0) std::snprintf(verification,sizeof(verification),"unavailable");
    else std::snprintf(verification,sizeof(verification),"0x%08lx",(unsigned long)response.tlsVerify);
    REPORT("TLS probe HTTP=%ld failure=%s error=%s curl=%ld mbedtls=-0x%04x verify=%s detail=%s bytes=%u segments=%u seconds=%.3f parse=%s reason=%s prefix=%s parse_detail=%s",response.status,
        yt::failureName(yt::failureOf(response)),response.error.c_str(),response.transportCode,(unsigned)-response.tlsCode,verification,response.transportDetail.c_str(),
        (unsigned)response.body.size(),(unsigned)segments.size(),response.seconds,yt::sponsorblock::stateName(diagnostic.state),diagnostic.reason.c_str(),yt::sponsorblock::hashPrefix("wWC8wVI4E3E").c_str(),diagnostic.detail.c_str());
    REPORT("TLS probe phase=%s dns_ms=%.1f tcp_ms=%.1f tls_ms=%.1f first_byte_ms=%.1f callback_gap_ms=%.1f nonblocking=%d os_errno=%ld",response.timing.phase,
        response.timing.dnsMs,response.timing.tcpMs,response.timing.tlsMs,response.timing.firstByteMs,response.timing.callbackGapMs,response.timing.nonblocking,response.timing.osError);
    for (const auto& s : segments) REPORT("segment %.6f -> %.6f duration %.3f",s.start,s.end,s.duration);
    probe.result=response.status==200 && response.error.empty() && (probe.url || !segments.empty()) ? 0 : 1;
    return nullptr;
}
}
int main(int argc,char** argv) {
    Probe probe; if (argc==2) probe.url=argv[1];
    pthread_t thread; pthread_attr_t attr;
    if (pthread_attr_init(&attr)) return 1;
    int result=pthread_attr_setstacksize(&attr,512*1024);
    if (!result) result=pthread_create(&thread,&attr,run,&probe);
    pthread_attr_destroy(&attr);
    if (result) return 1;
    pthread_join(thread,nullptr);
    return probe.result;
}
