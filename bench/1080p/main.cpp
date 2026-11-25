// Mesma régua FULL F30, com A/B de kernels no mesmo binário e workers configuráveis.
#define main f30_main
#include "../cpu-floor/main.cpp"
#undef main
extern "C" {
#include <libavutil/cpu.h>
#ifdef __XSDK__
void ff_tube360_set_vmx(unsigned mask);
#endif
}
static void setMask(unsigned mask) {
#ifdef __XSDK__
    ff_tube360_set_vmx(mask);
#else
    (void)mask;
#endif
}
int main(int argc,char** argv) {
    std::string directory="bench/cpu-floor/media",config="bench/1080p/.work/config.txt";
#ifdef __XSDK__
    directory="game:\\";config="game:\\f33-config.txt";
    if (xsdk_thread_move(1)<0) fail("F33 main affinity");
#endif
    if (argc==3) { directory=argv[1];config=argv[2]; }
    else if (argc!=1) fail("usage: f33-cpu [MEDIA CONFIG]");
    int mask=7,threads=6,nonref=0;
    FILE* cfg=std::fopen(config.c_str(),"r");
    if (cfg) {
        if (std::fscanf(cfg,"%d %d %d",&mask,&threads,&nonref)!=3) fail("F33 config");
        std::fclose(cfg);
    }
    if (mask<0 || mask>7 || threads<4 || threads>6 || nonref<0 || nonref>2) fail("F33 config range");
    av_log_set_level(AV_LOG_ERROR);
    if (std::strcmp(av_version_info(),"7.1.5") || !std::strstr(avcodec_license(),"LGPL") ||
        std::strstr(avcodec_configuration(),"--enable-gpl")) fail("F33 FFmpeg identity");
    LOG("F33 START schema=1 platform=%s mask=%d threads=%d filter=%s cpu_flags=%x ffmpeg=%s repeats=3",
        PLATFORM,mask,threads,nonref==2 ? "all" : nonref ? "nonref" : "full",av_get_cpu_flags(),av_version_info());
    AVDiscard filter=nonref==2 ? AVDISCARD_ALL : nonref ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
    unsigned results=0,audits=0;
    const Spec real{"youtube137",1920,1080,30,600,3533,40,30000.0/1001};
    FILE* media=std::fopen(joined(directory,"youtube137.mp4").c_str(),"rb");
    bool haveReal=media!=nullptr;if(media) std::fclose(media);
    LOG("F33 MEDIA youtube137=%d",int(haveReal));
    for (int index=0;index<(haveReal ? 4 : 3);++index) {
        const auto& spec=index==3 ? real : SPECS[index];Clip clip;
        clip.load(joined(directory,std::string(spec.name)+".mp4"),spec);
        setMask(0);Result ref=decode(clip,spec,0,threads,true,filter);
        setMask(mask);Result got=decode(clip,spec,0,threads,true,filter);
        for (size_t i=0;i<ref.frames.size();++i) {
            const auto& a=ref.frames[i];const auto& b=got.frames[i];
            if (a.pts!=b.pts || a.type!=b.type || a.pixels!=b.pixels ||
                a.stats.syntax_hash!=b.stats.syntax_hash || a.stats.nnz_hash!=b.stats.nnz_hash ||
                a.stats.coeff_hash!=b.stats.coeff_hash || a.stats.prediction_hash!=b.stats.prediction_hash)
                fail("F33 C/VMX pixel or syntax mismatch");
        }
        LOG("F33 AUDIT clip=%s frames=%u pixels_equal=1 syntax_equal=1 PASS",spec.name,unsigned(got.frames.size()));
        ++audits;
        for (int repeat=0;repeat<3;++repeat) {
            // Baseline/candidato alternados no mesmo lançamento: não comparar builds/afinidades diferentes.
            for (int step=0;step<2;++step) {
                bool candidate=((repeat+step)&1)!=0;
                int active=candidate ? mask : 0;
                setMask(active);Result result=decode(clip,spec,0,threads,false,filter);
                LOG("F33 CASE role=%s mask=%d threads=%d filter=%s",candidate ? "candidate" : "baseline",active,threads,nonref==2 ? "all" : nonref ? "nonref" : "full");
                report(result,spec,0,repeat+1,threads);++results;
            }
        }
    }
    LOG("F33 DONE PASS audits=%u results=%u",audits,results);
    return 0;
}
