// Só é ligado no build de bancada; não altera a seleção de qualidade.
#pragma once
namespace f33 {
struct PlaybackProbe {
  bool enabled = false, sourceOk = true, audioOk = true;
  uint64_t launched = 0, playing = 0, began = 0, sampled = 0;
  int seconds = 120, warmup = 10, firstQueue = 0, minQueue = INT32_MAX;
  long long firstShown = 0, firstLost = 0;
  size_t minFree = SIZE_MAX;
  std::vector<double> sync;
  std::vector<int> queues;
  PlaybackProbe(const std::string& assets,uint64_t now) : launched(now) {
    if (FILE* file=std::fopen((assets+"f33-playback.txt").c_str(),"r")) {
      enabled=std::fscanf(file,"%d %d",&seconds,&warmup)==2 && seconds>=10 && seconds<=600 && warmup>=0 && warmup<=30;
      std::fclose(file);
      if (enabled) { sync.reserve(seconds+1);queues.reserve(seconds+1);LOG("F33 APP START seconds=%d warmup=%d",seconds,warmup); }
    }
  }
  bool update(player::Player& p,uint64_t now) {
    if (!enabled) return false;
    if (p.state==player::FAILED || (now-launched>uint64_t(seconds+warmup+90)*1000)) {
      LOG("F33 APP FAIL playback_or_timeout state=%d",int(p.state.load()));enabled=false;return true;
    }
    if (!playing && p.firstPts>=0 && p.state==player::PLAYING) playing=now;
    if (!playing || now-playing<uint64_t(warmup)*1000 || now-sampled<1000) return false;
    sampled=now;
    SDL_LockMutex(p.mu);
    int ready=int(p.frames.size());
    long long shown=p.shownFrames,lost=p.droppedFrames;
    SDL_UnlockMutex(p.mu);
    if (!began) { began=now;firstShown=shown;firstLost=lost;firstQueue=ready; }
    double queued=p.audioQueuedSec();
    double delta=(p.lastPos-p.startAt-p.tvClock(0,queued))*1000;
    sync.push_back(std::fabs(delta));queues.push_back(ready);minQueue=std::min(minQueue,ready);
    minFree=std::min(minFree,video::titleFreeBytes());
    sourceOk=sourceOk && p.actualHeight==1080 && p.actualFps>=29 && p.actualFps<=30 && p.presentFps==30 && p.speed==1;
    audioOk=audioOk && p.hasAudio && p.audible && !p.paused && !p.refill && !p.speakerRequested && queued>0.02;
    LOG("F33 APP SAMPLE ms=%llu source=%dp%d target=%d shown=%lld lost=%lld queue=%d pcm_ms=%.1f av_ms=%.2f free_mib=%u",
        (unsigned long long)(now-began),p.actualHeight.load(),p.actualFps.load(),p.presentFps.load(),
        shown-firstShown,lost-firstLost,ready,queued*1000,delta,unsigned(minFree>>20));
    if (now-began<uint64_t(seconds)*1000) return false;
    std::sort(sync.begin(),sync.end());
    double p95=sync[size_t(std::ceil(sync.size()*.95))-1],maximum=sync.back();
    double fps=(shown-firstShown)*1000.0/(now-began);
    bool memory=minFree==SIZE_MAX || minFree>=(64u<<20);
    size_t n=std::min(size_t(10),queues.size());double first=0,last=0;
    for(size_t i=0;i<n;++i) { first+=queues[i];last+=queues[queues.size()-1-i]; }
    first/=n;last/=n;
    bool ok=sourceOk && audioOk && lost==firstLost && fps>=29.5 && p95<=50 && maximum<=100 && memory && last>=first-2;
    LOG("F33 APP DONE %s fps=%.3f lost=%lld source_ok=%d audio_ok=%d av_p95_ms=%.2f av_max_ms=%.2f queue_first=%d queue_last=%d queue_min=%d queue_head_mean=%.2f queue_tail_mean=%.2f free_min_mib=%u",
        ok ? "PASS" : "FAIL",fps,lost-firstLost,int(sourceOk),int(audioOk),p95,maximum,firstQueue,ready,minQueue,first,last,unsigned(minFree>>20));
    enabled=false;return true;
  }
};
}
