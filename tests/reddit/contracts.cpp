#include "quality_recovery.hpp"
#include "seek_hold.hpp"
#include "connection_error.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
  playback::SeekHold hold;
  check(!hold.update(1000,1,50,1000,false) && hold.active && hold.target==60,"trigger previews first 10 seconds");
  hold.update(1300,1,50,1000,false);check(hold.target==60,"initial hold threshold");
  hold.update(3000,1,50,1000,false);check(hold.target==77,"2 seconds: +27 seconds");
  hold.update(6000,1,50,1000,false);check(hold.target==167,"5 seconds: +117 seconds");
  hold.update(7000,1,50,1000,false);check(hold.target==287,"6 seconds: +237 seconds");
  check(hold.update(7100,0,50,1000,true) && !hold.wasPaused && !hold.active,"release commits once and remembers running state");
  check(!hold.update(7200,0,50,1000,false),"release does not repeat");
  hold.update(0,-1,5,100,true);check(hold.target==0 && hold.wasPaused,"rewind clamps and preserves pause");
  hold.update(10000,1,5,100,true);hold.update(30000,1,5,100,true);check(hold.target==97,"forward clamps to duration minus 3");
  hold.cancel(true);check(!hold.active,"B cancels preview");
  hold.update(31000,1,5,100,false);check(!hold.active,"B does not restart while trigger remains held");
  hold.update(32000,0,5,100,false);hold.update(33000,1,5,100,false);check(hold.active,"new trigger press works after cancellation and release");
  playback::SeekHold fine,coarse;
  fine.update(0,1,0,10000,false);coarse.update(0,1,0,10000,false);
  for(int t=10;t<=6000;t+=10)fine.update(t,1,0,10000,true);
  coarse.update(6000,1,0,10000,true);check(std::abs(fine.target-coarse.target)<1e-6,"acceleration independent of render fps");
  playback::QualityRecovery q;q.changed(0,false);
  auto sample=[&](uint64_t now,double decode=.15,double mbps=30,bool running=true,int queue=7,int drops=0,bool fresh=true){return q.sample(now,1000,running,queue,false,drops,decode,2.25,mbps,5,fresh);};
  for(int t=1000;t<30000;t+=1000)check(!sample(t),"cooldown before first raise");
  check(sample(30000),"1080 restored after stable decode and network headroom");q.changed(30000,true);
  q.changed(33000,false);check(q.holdUntil==153000,"early regression backs off 120 seconds");
  for(int t=34000;t<153000;t+=1000)check(!sample(t),"no oscillation after failed probe");
  check(sample(153000),"retry after backoff");
  check(!sample(154000,.5),"insufficient projected decoder headroom prevents up");
  check(!sample(155000,.15,5),"network capacity, not nominal fibre speed");
  check(!sample(156000,.15,30,false),"pause resets recovery");
  check(!sample(157000,.15,30,true,1),"empty image queue resets recovery");
  check(!sample(158000,.15,30,true,7,5),"lost frames reset recovery");
  check(!sample(159000,.15,30,true,7,0,false),"stale network measurements rejected");
  check(connection::message(yt::Failure::TLS_DATE)==i18n::Key::ClockWrong,"date classification survives to UI");
  check(connection::message(yt::Failure::TLS)!=connection::message(yt::Failure::TLS_DATE),"other TLS errors do not blame clock");
  for(auto lang:{i18n::Lang::EN,i18n::Lang::PT,i18n::Lang::ES})check(*i18n::get(lang,connection::message(yt::Failure::DNS)),"translated connection errors");
  std::puts("PASS seek: acceleration, both limits, single release, pause, cancel, variable fps");
  std::puts("PASS recovery: 1080 headroom, cooldown, backoff, pressure, pause, dropped frames, stale bandwidth");
}
