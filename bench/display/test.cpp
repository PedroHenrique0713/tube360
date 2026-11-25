#include "display_layout.hpp"
#include "settings.hpp"
#include "yt/catalog.hpp"
#include "yt/console_account.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using J=picojson::value;
using O=picojson::object;
int checks=0;
void check(bool value,const char* message) {
  ++checks; if (!value) { std::cerr<<"FAIL "<<message<<'\n'; std::exit(1); }
}
bool near(float a,float b) { return std::abs(a-b)<.01f; }
std::string read(const std::string& name) { std::ifstream in(name); return {std::istreambuf_iterator<char>(in),{}}; }
struct Http:yt::Transport {
  std::string root, language="es", region="MX";
  int posts=0;
  explicit Http(std::string dir):root(std::move(dir)) {}
  yt::Response request(const yt::string& url,const yt::string& body,bool form) override {
    yt::Request r; r.url=url; r.body=body; r.form=form; return send(r);
  }
  yt::Response send(const yt::Request& r) override {
    if (!r.body.empty()) {
      J body; check(picojson::parse(body,r.body).empty(),"valid request JSON");
      const auto& client=body.get("context").get("client");
      check(client.get("hl").get<std::string>()==language,"actual serialized hl");
      check(client.get("gl").get<std::string>()==region,"actual serialized gl");
      ++posts;
      if (r.url.find("/player?")!=std::string::npos) return {200,read(root+"/standalone/fixtures/player.json"),{}};
      if (r.url.find("music.youtube.com/")!=std::string::npos) return {200,read(root+"/catalog/fixtures/music-home.json"),{}};
      return {200,R"({"contents":[{"videoRenderer":{"videoId":"video000001","title":{"simpleText":"Video de prueba"}}}]})",{}};
    }
    if (r.url.find("/watch?")!=std::string::npos) return {200,R"(<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/test/base.js"})</script>)",{}};
    if (r.url.find("/base.js")!=std::string::npos) return {200,"var a={signatureTimestamp:20775};",{}};
    if (r.url=="https://music.youtube.com/") return {200,read(root+"/catalog/fixtures/music-context.html"),{}};
    if (r.url=="https://www.youtube.com/") return {200,read(root+"/catalog/fixtures/web-context.html"),{}};
    check(false,"unrecorded URL"); return {};
  }
};
int main(int argc,char** argv) {
  if (argc!=2) return 2;
  auto sd=display::layout(640,480);
  check(sd.narrow && sd.sd && sd.large && sd.columns(4)==2,"CRT readability without manual configuration");
  check(near(sd.width,960) && near(sd.scale,.6f) && near(sd.insetX,32) && near(sd.insetY,24),"5 percent safe area on all CRT edges");
  check(near(sd.insetX+sd.width*sd.scale,608) && near(sd.insetY+720*sd.scale,456),"safe content fits inside 640x480");
  auto wide=display::layout(848,480); check(wide.sd && !wide.narrow && wide.columns(4)==3,"widescreen SD uses fewer columns too");
  auto pal=display::layout(640,576,false,4.f/3);
  check(near(pal.width,960) && pal.narrow && pal.large && pal.columns(4)==2,"PAL kernel viewport keeps a physical 4:3 layout");
  check(near(pal.insetX+pal.width*pal.scaleX,608) && near(pal.insetY+720*pal.scale,547.2f),"non-square PAL pixels preserve safe margins on both axes");
  auto palWide=display::layout(720,576,false,16.f/9);
  check(near(palWide.width,1280) && !palWide.narrow && near(palWide.insetX+palWide.width*palWide.scaleX,684),"anamorphic PAL follows dashboard widescreen");
  auto hd=display::layout(1280,720,true); check(hd.large && hd.columns(4)==3 && hd.scale==1 && hd.insetX==0,"large text also works at 720p");
  auto comments=display::commentsPanel(sd.width,45,34,37);
  check(comments.x>=32 && comments.x+comments.w<=sd.width-32,"comments panel stays inside 4:3 safe area");
  check(comments.lines<10 && comments.bodyY+comments.lines*comments.lineH<=comments.h-70,"large comment text leaves room for controller hints");
  auto normal=display::layout(1920,1080); check(!normal.large && normal.columns(4)==4 && normal.scale==1.5f,"HD default preserved");
  auto fit=display::videoRect(640,480,1280,720,false);
  check(near(fit.x,0) && near(fit.y,60) && near(fit.w,640) && near(fit.h,360),"16:9 video has 60-pixel bars on 4:3");
  auto fill=display::videoRect(640,480,1280,720,true);
  check(near(fill.w,640) && near(fill.h,480) && near(fill.u0,.125f) && near(fill.u1,.875f),"fill crops 12.5 percent from each side without stretching");
  auto portrait=display::videoRect(1280,720,1080,1920,false); check(near(portrait.h,720) && near(portrait.w,405),"portrait preserves aspect too");
  check(i18n::language("es")==i18n::Lang::ES && i18n::cycle(i18n::Lang::PT,1)==i18n::Lang::ES && i18n::cycle(i18n::Lang::EN,-1)==i18n::Lang::ES,"three-way language selection in either direction");
  for (size_t i=1;i<size_t(i18n::Key::Count);++i) check(i18n::get(i18n::Lang::ES,i18n::Key(i))[0]!=0,"every Spanish key has text");
  check(i18n::shortDate("2026-01-24",i18n::Lang::ES)=="24 ene 2026","Spanish dates");
  std::string dir=(std::filesystem::temp_directory_path()/"tube360-display-XXXXXX").string();
  std::vector<char> path(dir.begin(),dir.end()); path.push_back(0); check(mkdtemp(path.data()),"isolated settings directory"); dir=std::string(path.data())+"/";
  Settings settings; settings.lang=i18n::Lang::ES; settings.outputHeight=480; settings.largeText=true; settings.videoFill=true;
  check(settings.save(dir),"save new settings"); Settings loaded; loaded.load(dir);
  check(loaded.lang==i18n::Lang::ES && loaded.outputHeight==480 && loaded.largeText && loaded.videoFill,"new settings survive restart");
  { std::ofstream out(dir+"settings.txt"); out<<"lang=pt\noutput_height=999\nlarge_text=off\nvideo_fit=fit\n"; }
  loaded.load(dir); check(loaded.outputHeight==720 && !loaded.largeText && !loaded.videoFill && loaded.lang==i18n::Lang::PT,"legacy and invalid settings preserve defaults");
  Http h(argv[1]); yt::Client client(h); client.setLanguage("es");
  check(!client.home().cards.empty(),"Spanish public Home uses product client");
  client.setLanguage("pt"); h.language="pt"; h.region="BR"; check(!client.search("fútbol").cards.empty(),"locale changes on reused public context");
  client.setLanguage("es"); h.language="es"; h.region="MX"; client.player("4p_9OPKgcDE"); check(h.posts>=3,"player locale serialized as well as browse/search");
  yt::catalog::Client catalog(h); check(!catalog.browse({{"kind","music"},{"lang","es"}}).items.empty(),"Spanish Music context");
  check(!catalog.browse({{"kind","search"},{"q","fútbol"},{"lang","es"}}).items.empty(),"Spanish public catalog context");
  yt::ConsoleAccount account(h,dir+"account.json");
  check(account.request("/api/feed?kind=home&lang=es",1000,10).ok(),"Spanish native TV feed");
  std::filesystem::remove_all(dir);
  std::cout<<checks<<" checks passed\n";
}
