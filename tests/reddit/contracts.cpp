#include "quality_recovery.hpp"
#include "seek_hold.hpp"
#include "connection_error.hpp"
#include "net.hpp"
#include "settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
  Settings defaults;net::Server server;
  check(defaults.server && !net::effectiveServer(server,defaults.server),"auto preference without address is console-only");
  for(bool requested:{true,false,true})check(!net::effectiveServer(server,requested),"settings toggle never enables an unconfigured server");
  server.ip="127.0.0.1";
  check(net::effectiveServer(server,true) && !net::effectiveServer(server,false),"configured server follows explicit preference");
  std::puts("PASS server: default auto without address, repeated toggle and configured/off modes");
  playback::QualityPressure pressure;
  check(!pressure.sample(3800,1000,true,true,true,true) && !pressure.sample(7700,1000,true,true,true,true),"reported 3.8/7.7 second startup windows cannot trigger source drops");
  pressure.changed(0);
  for(int t=1000;t<13000;t+=1000)check(!pressure.sample(t,1000,true,true,true,true),"warmup then five seconds of fresh pressure");
  check(pressure.sample(13000,1000,true,true,true,true),"sustained 1080 overload allows one rung");pressure.changed(13000);
  playback::QualityLoad healthy720(.020*30,.002*30,false,true,true,1000,4.2);
  check(!healthy720.overloaded,"20 ms decode plus 2 ms copy fits 720p30 despite stale/empty queue signals");
  check(!playback::presentationPressure(720,30,true,.01),"slow UI with cheap 720p30 does not prove video overload");
  check(playback::presentationPressure(720,60,true,.01) && playback::presentationPressure(1080,30,true,.01),"high cadence or resolution can try one cheaper rung");
  check(playback::presentationPressure(720,30,true,.20),"measured video copy cost can justify a resolution drop");
  for(int t=14000;t<=60000;t+=1000)check(!pressure.sample(t,1000,true,true,true,healthy720.overloaded),"healthy 720p30 never drops to 480 from queue debt");
  check(playback::cadenceBeforeResolution(720,60,60,360),"missing itag136: keep itag298 at 30 presentation before 360");
  check(!playback::cadenceBeforeResolution(720,60,30,480),"already measured 30 fps rung can proceed only with fresh pressure");
  check(!playback::cadenceBeforeResolution(720,60,60,720),"real 720p30 format is next source rung");
  playback::QualityLoad limited(.3,.05,false,true,true,2.5,4.2);
  check(limited.overloaded && limited.networkLimited,"measured insufficient bandwidth remains a valid reason to lower");
  pressure.changed(60000);
  for(int t=61000;t<=72000;t+=1000)check(!pressure.sample(t,1000,true,true,true,limited.overloaded),"next source must settle before pressure is eligible");
  check(pressure.sample(73000,1000,true,true,true,limited.overloaded),"next rung can fall under persistent measured network pressure");
  check(!pressure.sample(74000,1000,false,true,true,true),"pause resets downward decision");
  std::puts("PASS pressure: console opening timings, one rung, settling, healthy 720p30 floor, missing 720p30 cadence and real bandwidth pressure");
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
  double pixels=playback::qualityWorkScale(1280,720,30,1920,1080,30);
  check(std::abs(pixels-2.25)<.0001,"exact pixel ratio of 720p30 to 1080p30");
  playback::QualityRecovery console;
  console.rejected(44400,137,30,true);console.changed(44400,false);
  check(console.retryAfter(137,30)==164400,"first measured CPU failure remembered for 120 seconds");
  auto consoleSample=[&](uint64_t t,double ms){return console.sample(t,1000,true,7,false,0,playback::qualityDecodeFraction(ms,30),pixels,1000,16,true,console.retryAfter(137,30));};
  check(!consoleSample(95800,8),"reported 95.8-second retry is blocked even with a cheap current sample");
  for(uint64_t t=96800;t<=220800;t+=1000)check(!consoleSample(t,19),"720p30 at 19ms predicts 42.75ms/1080 frame; cannot oscillate even after retry time");
  for(uint64_t t=221400;t<240400;t+=1000)check(!consoleSample(t,8),"twenty fresh headroom windows required");
  check(consoleSample(240400,8),"real improvement to 8ms predicts 54 percent and can recover after hold");
  console.changed(240400,true);console.rejected(300400,137,30,true);console.changed(300400,false);
  check(console.retryAfter(137,30)==540400,"failed source remembered even when upgrade lasted longer than 45 seconds");
  console.rejected(550000,137,30,true);check(console.retryAfter(137,30)==1030000,"third CPU failure backs off 480 seconds");
  console.rejected(560000,136,60,true);check(console.retryAfter(137,30)==1030000 && console.retryAfter(136,30)==0,"failures are per itag and cadence");
  console.rejected(600000,137,30,true);console.rejected(700000,137,30,true);
  check(console.retryAfter(137,30)==1660000,"backoff capped at 960 seconds");
  check(!playback::qualityWorkScale(0,720,30,1920,1080,30),"unknown dimensions never authorize an upgrade");
  std::puts("PASS recovery console: 44.4/95.8 seconds, 19ms -> 42.75ms projection, per-rung 120/240/480/960s memory and genuine improvement");
  check(connection::message(yt::Failure::TLS_DATE)==i18n::Key::ClockWrong,"date classification survives to UI");
  check(connection::message(yt::Failure::TLS)!=connection::message(yt::Failure::TLS_DATE),"other TLS errors do not blame clock");
  for(auto lang:{i18n::Lang::EN,i18n::Lang::PT,i18n::Lang::ES})check(*i18n::get(lang,connection::message(yt::Failure::DNS)),"translated connection errors");
  std::puts("PASS seek: acceleration, both limits, single release, pause, cancel, variable fps");
  std::puts("PASS recovery: 1080 headroom, cooldown, backoff, pressure, pause, dropped frames, stale bandwidth");
}
