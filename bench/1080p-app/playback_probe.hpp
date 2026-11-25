// Compilação opt-in. Mede apresentação/PCM no app; nunca força nem trava a escada.
#pragma once
namespace f40 {
struct PlaybackProbe {
  bool enabled=false,sourceOk=true,audioOk=true,policyOk=true,routeOk=true;
  uint64_t launched=0,playing=0,began=0,sampled=0;
  int seconds=120,warmup=10,itag=137,nest=0,loop=48,minQueue=INT32_MAX;
  char id[32]="4p_9OPKgcDE";
  long long firstShown=0,firstLost=0,lastShown=-1;
  unsigned firstSilent=0;
  size_t minFree=SIZE_MAX;
  std::vector<double> sync,gaps;
  uint64_t frameAt=0;
  PlaybackProbe(const std::string& assets,uint64_t now):launched(now) {
    if (FILE* f=std::fopen((assets+"f40-playback.txt").c_str(),"r")) {
      enabled=std::fscanf(f,"%d %d %d %d %d %31s",&seconds,&warmup,&itag,&nest,&loop,id)==6 &&
          seconds>=10 && seconds<=600 && warmup>=0 && warmup<=30 && itag>0 && (nest==0 || nest==1) && (loop==0 || loop==48);
      std::fclose(f);
      if(enabled) {
        sync.reserve((seconds+1)*61);gaps.reserve((seconds+1)*61);
#ifdef __XSDK__
        const char* platform="xbox";
#else
        const char* platform="pc";
#endif
        LOG("F40 APP START platform=%s seconds=%d warmup=%d itag=%d nest=%d loop=%d id=%s nest_clock=f25b_pending muted=%d",
            platform,seconds,warmup,itag,nest,loop,id,int(player::benchMute));
      }
    }
  }
  static double percentile(std::vector<double>& v,double fraction) {
    if(v.empty()) return 1e9;
    std::sort(v.begin(),v.end());return v[size_t(std::ceil(v.size()*fraction))-1];
  }
  bool update(player::Player& p,uint64_t now) {
    if(!enabled) return false;
    now=SDL_GetTicks();
    if(p.state==player::FAILED || p.state==player::ENDED || now-launched>uint64_t(seconds+warmup+90)*1000) {
      LOG("F40 APP DONE FAIL reason=playback_or_timeout state=%d console_gate=failed",int(p.state.load()));enabled=false;return true;
    }
    if(!playing && p.firstPts>=0 && p.state==player::PLAYING) playing=now;
    if(!playing || now-playing<uint64_t(warmup)*1000) return false;
    SDL_LockMutex(p.mu);
    int ready=int(p.frames.size());long long shown=p.shownFrames,lost=p.droppedFrames;
    SDL_UnlockMutex(p.mu);
    if(!began) {
      began=now;firstShown=shown;firstLost=lost;frameAt=now;
#ifdef __XSDK__
      XsdkAudioStats stats{};xsdk_audio_stats(&stats);firstSilent=stats.silent;
#endif
    }
    sourceOk=sourceOk && p.id==id && p.direct && p.original && !p.live && p.activeVideoItag==itag &&
        p.actualHeight==1080 && p.actualFps>=29 && p.actualFps<=30 && p.presentFps==30 && p.speed==1 && !p.adapted;
    policyOk=policyOk && p.activeH264Threads==6 && p.activeH264Spread && p.activeLoopFilter==loop;
    double queued=p.audioQueuedSec(),clock=0,age=0;
    if(nest) {
      SDL_LockMutex(p.nmu);
      age=SDL_GetTicksNS()/1e6-p.nestStampMs;
      clock=p.nestPos+age/1000+p.nestOffset;
      bool fresh=p.nestStampMs>0 && age>=0 && age<=1000 && p.nestState==1;
      SDL_UnlockMutex(p.nmu);
      routeOk=routeOk && p.speakerRequested && p.output==player::Player::CAST && !p.speakerFallback && fresh;
      audioOk=audioOk && !p.audible && !p.paused;
    } else {
      double wall=p.firstPts+(now-p.wallStart-p.pausedTotal)/1000.0;
      clock=p.tvClock(wall,queued);
      routeOk=routeOk && !p.speakerRequested && p.output==player::Player::LOCAL;
      audioOk=audioOk && p.hasAudio && p.audible && !p.paused && !p.refill && queued>.02;
    }
    double delta=(p.lastPos-p.startAt-clock)*1000;
    if(shown!=lastShown) {
      if(lastShown>=0) gaps.push_back(double(now-frameAt));
      frameAt=now;lastShown=shown;sync.push_back(std::fabs(delta));
    }
    if(now-sampled<1000) return false;
    sampled=now;minQueue=std::min(minQueue,ready);minFree=std::min(minFree,video::titleFreeBytes());
    unsigned silent=0,waitUs=0;
#ifdef __XSDK__
    XsdkAudioStats stats{};xsdk_audio_stats(&stats);silent=stats.silent-firstSilent;waitUs=stats.longest_wait_us;
#endif
    LOG("F40 APP SAMPLE ms=%llu itag=%d source=%dp%d target=%d shown=%lld lost=%lld queue=%d pcm_ms=%.1f av_ms=%.2f clock_age_ms=%.1f free_mib=%u silent=%u feeder_wait_us=%u source_ok=%d audio_ok=%d policy_ok=%d route_ok=%d",
        (unsigned long long)(now-began),p.activeVideoItag.load(),p.actualHeight.load(),p.actualFps.load(),p.presentFps.load(),
        shown-firstShown,lost-firstLost,ready,queued*1000,delta,age,unsigned(minFree>>20),silent,waitUs,
        int(sourceOk),int(audioOk),int(policyOk),int(routeOk));
    if(now-began<uint64_t(seconds)*1000) return false;
    double p95=percentile(sync,.95),maximum=sync.empty()?1e9:sync.back(),gap95=percentile(gaps,.95);
    double fps=(shown-firstShown)*1000.0/(now-began);
    bool memoryMeasured=minFree!=SIZE_MAX && minFree!=0;
    bool memory=memoryMeasured && minFree>=(64u<<20);
    bool ok=sourceOk && audioOk && policyOk && routeOk && lost==firstLost && fps>=29.5 && p95<=50 && (nest || !silent);
#ifdef __XSDK__
    bool qualified=ok && memory && seconds>=120 && warmup>=10 && !nest && !player::benchMute;
    const char* gate=qualified?"pass":nest && ok && memory?"pending_f25b_clock":
        ok && memory && player::benchMute?"pending_audio":"failed";
#else
    bool qualified=false;
    const char* gate="pending_console";
#endif
    LOG("F40 APP DONE %s fps=%.3f lost=%lld source_ok=%d audio_ok=%d policy_ok=%d route_ok=%d av_samples=%u av_p95_ms=%.2f av_max_ms=%.2f present_gap_p95_ms=%.2f queue_min=%d free_min_mib=%u memory_measured=%d silent=%u elapsed_ms=%llu console_gate=%s qualified=%d",
        ok && (memory || !memoryMeasured)?"PASS":"FAIL",fps,lost-firstLost,int(sourceOk),int(audioOk),int(policyOk),int(routeOk),
        unsigned(sync.size()),p95,maximum,gap95,minQueue,unsigned(minFree>>20),int(memoryMeasured),silent,
        (unsigned long long)(now-began),gate,int(qualified));
    enabled=false;return true;
  }
};
}
