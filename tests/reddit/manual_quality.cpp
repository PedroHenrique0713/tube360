#include "yt/client.hpp"
#include "yt/media.hpp"
#include "settings.hpp"
#include "audio_track_label.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
static int checks;
static void check(bool good,const char* why){++checks;if(!good){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Offline:yt::Transport {
  yt::Response request(const yt::string&,const yt::string&,bool)override{check(false,"parser test must not access network");return {};}
};
static std::string format(int h,int tag,const char* codec="avc1",bool direct=true){
  return "{\"itag\":"+std::to_string(tag)+",\"height\":"+std::to_string(h)+",\"width\":"+std::to_string(h*16/9)+",\"fps\":"+(tag==298?"60":"30")+",\"bitrate\":1000000,\"mimeType\":\"video/mp4; codecs=\\\""+codec+"\\\"\""+(direct?",\"url\":\"https://fixture.googlevideo.com/"+std::to_string(tag)+".mp4\"":"")+"}";
}
static const std::string aac=R"({"itag":140,"mimeType":"audio/mp4; codecs=\"mp4a\"","bitrate":128000,"url":"https://fixture.googlevideo.com/a.m4a"})";
static std::string reply(const std::string& formats){return R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE","lengthSeconds":"240"},"streamingData":{"expiresInSeconds":"3600","adaptiveFormats":[)"+formats+"]}}";}
struct Hls:yt::Transport {
  yt::Response request(const yt::string& url,const yt::string&,bool)override {
    if(url.ends_with("master"))return {200,"#EXTM3U\n#EXT-X-STREAM-INF:CODECS=\"avc1,mp4a\",RESOLUTION=854x480,FRAME-RATE=30\n480.m3u8\n#EXT-X-STREAM-INF:CODECS=\"avc1,mp4a\",RESOLUTION=640x360,FRAME-RATE=30\n360.m3u8\n"};
    if(url.ends_with("m3u8"))return {200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:2,\nseg.ts\n#EXT-X-ENDLIST\n"};
    return {200,"synthetic segment"};
  }
};
int main(int argc,char** argv){
  check(argc==2,"isolated output directory required");
  Offline transport;yt::Client client(transport);
  std::string all=aac;
  for(auto [height,tag]:{std::pair{1080,137},{720,298},{720,136},{480,135},{360,134},{240,133},{144,160}})all+=","+format(height,tag);
  for(int mode=0;mode<playback::COUNT;++mode){
    auto video=client.player("4p_9OPKgcDE",720,nullptr,mode,"original",reply(all));
    check(video.status=="OK" && video.qualityModes==127,"all seven choices are derived from valid H264/AAC offers");
    if(playback::manual(mode)){
      check(video.video.height==playback::fixedHeight(mode) && video.lowerVideo.empty() && video.videoLadder.size()==1,"manual picks exact height and has no automatic source ladder");
    }else check(video.video.height==(mode==playback::FAST?720:1080) && !video.lowerVideo.empty(),"existing three adaptive profiles retain original source and ladder");
    Settings settings;settings.videoMode=mode;
    std::string path=std::string(argv[1])+"/";std::filesystem::create_directories(path);
    check(settings.save(path),"write isolated settings");Settings loaded;loaded.load(path);
    check(loaded.videoMode==mode && std::string(playback::modeName(mode))==playback::modeName(loaded.videoMode),"every mode survives settings roundtrip");
    for(auto lang:{i18n::Lang::PT,i18n::Lang::EN,i18n::Lang::ES})check(*i18n::get(lang,videoModeLabel(mode)),"quality label translated");
  }
  auto sparse=reply(aac+","+format(480,135)+","+format(144,160)+","+format(240,242,"vp9")+","+format(360,134,"avc1",false));
  auto missing=client.player("4p_9OPKgcDE",720,nullptr,playback::FIXED240,"original",sparse);
  check(missing.video.url.empty() && missing.reason=="manual_quality_unavailable","missing manual tier never substitutes 144p/480p");
  check(missing.qualityModes==((1u<<playback::AUTO)|(1u<<playback::FIXED480)|(1u<<playback::FIXED144)),"VP9 and non-direct AVC do not appear in availability");
  auto noAudio=client.player("4p_9OPKgcDE",720,nullptr,playback::FIXED240,"original",reply(format(240,133)));
  check(noAudio.qualityModes==1 && noAudio.reason=="manual_quality_unavailable","video without AAC is unavailable rather than a broken automatic rules response");
  playback::QualityChoices choices(missing.qualityModes);
  check(choices.count==3 && choices.cycle(playback::FIXED480,1)==playback::FIXED144 && choices.cycle(playback::AUTO,-1)==playback::FIXED144,"sparse UI modes use indices independently from persisted mode IDs");
  check(playback::modeFromName("garbage")==playback::AUTO,"invalid setting remains Automatic");
  check(i18n::playerError("manual_quality_unavailable")==i18n::Key::ManualQualityMissing,"unavailable tier has a localized recovery message");
  SDL_Init(0);
  for(int wanted:{480,240}){
    Hls lists,segments;std::atomic_bool stop{false};yt::LiveFile live(lists,segments,stop);
    bool opened=live.open("https://fixture.googlevideo.com/master",wanted,false,"original",true);
    check(opened==(wanted==480),"manual HLS requires the exact height");
    check(live.qualityModes==((1u<<playback::AUTO)|(1u<<playback::FIXED480)|(1u<<playback::FIXED360)),"HLS reports its own AVC variants");
    check(!live.requestLower() && live.lowerRemaining==0,"pinned HLS cannot request an adaptive switch");
    if(opened)check(live.height==480 && live.refill()==0 && live.refill()==AVERROR_EOF,"selected HLS stream is actually consumed");
  }
  std::cout<<"PASS manual quality: "<<checks<<" checks, exact source, sparse offers, settings/locales and pinned HLS\n";
}
