#define main f52ProductMain
#include "../../client/src/main.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <iostream>

static int f52Checks=0;
static void require(bool ok,const char* label) {
  ++f52Checks;
  if (!ok) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); }
}
#include "feed_contracts.hpp"
static NavItem card(int index,bool shortForm=false) {
  NavItem item; item.type="video"; item.id="f52vid"+std::to_string(10000+index);
  item.title="Video "+std::to_string(index); item.image=item.id; item.shortForm=shortForm;
  return item;
}
static void settingsContract(const std::string& dir) {
  std::filesystem::remove(dir+"settings.txt");
  Settings s; s.load(dir);
  require(!s.hideShorts && s.errorReports,"missing file defaults to row/on");
  for (auto legacy:{"notebook","together","xbox_notebook","xbox","invalid"}) {
    { std::ofstream out(dir+"settings.txt"); out<<"server=auto\nsleep_what="<<legacy<<"\nshorts=hide\nerror_reports=off\n"; }
    s.load(dir);
    require(!s.server && !s.sleepNotebook && s.hideShorts && !s.errorReports,"legacy PC settings normalize to Xbox; new keys load");
    require(s.save(dir),"settings save succeeds");
    Settings restarted; restarted.load(dir);
    require(restarted.hideShorts && !restarted.errorReports && !restarted.server && !restarted.sleepNotebook,"hide/off survive restart");
    std::ifstream file(dir+"settings.txt"); std::string text((std::istreambuf_iterator<char>(file)),{});
    require(text.find("shorts=hide\n")!=text.npos && text.find("error_reports=off\n")!=text.npos && text.find("sleep_what=xbox\n")!=text.npos,"launcher wire keys and Xbox target persisted");
  }
  { std::ofstream out(dir+"settings.txt"); out<<"shorts=unknown\nerror_reports=unknown\n"; }
  s.load(dir); require(!s.hideShorts && s.errorReports,"unknown values keep row/on defaults");
  s.hideShorts=true; s.errorReports=false;
  { std::ofstream out(dir+"settings.txt"); out<<"lang=pt\n"; }
  s.load(dir); require(!s.hideShorts && s.errorReports,"legacy file resets new settings to defaults");
  require(s.save(dir),"row/on save succeeds");
  Settings restarted; restarted.load(dir); require(!restarted.hideShorts && restarted.errorReports,"row/on survive restart");
  require(!s.save(dir+"missing/"),"unwritable save is reported");
  for (auto lang:{i18n::Lang::EN,i18n::Lang::PT,i18n::Lang::ES,i18n::Lang::AR})
    for (auto key:{K::SettingShowShorts,K::SettingShortsRow,K::SettingShortsHide,K::SettingErrorReports,K::SettingOn,K::SettingOff})
      require(*i18n::get(lang,key) && (lang!=i18n::Lang::AR || i18n::arabic(key)),"F52 strings translated in all four languages");
  std::cout<<"PASS settings: defaults, row/hide and on/off restart, migration, save failure, four languages\n";
}
static void parserContract() {
  using namespace yt::catalog;
  J data;
  require(parse(R"({"contents":[{"videoRenderer":{"videoId":"f52vid00001","title":{"simpleText":"Short normal video"},"lengthText":{"simpleText":"0:15"}}},{"reelItemRenderer":{"videoId":"f52vid00002","headline":{"simpleText":"Legacy Short"}}},{"shortsLockupViewModel":{"videoId":"f52vid00003","onTap":{"innertubeCommand":{"reelWatchEndpoint":{"videoId":"f52vid00003"}}},"overlayMetadata":{"primaryText":{"content":"Modern Short"}}}}]})",data),"typed renderer fixture parses");
  auto page=decode(data,false);
  require(page.items.size()==3,"all three renderer variants yield a playable video");
  require(!page.items[0].shortForm && page.items[1].shortForm && page.items[2].shortForm,"only Short renderers are marked; no duration/title heuristic");
  require(page.items[2].title=="Modern Short","modern title retained even when videoId is present");
  require(page.items[1].title=="Legacy Short","reel headline is retained");
  for (const auto& it:page.items) require(it.type=="video","Shorts retain video actions and queue compatibility");
  NavPage nav; nav.kind="search";
  require(Navigation::parse(wire(page),nav),"wire parses in actual navigation");
  require(nav.offset==3 && nav.items.size()==3 && nav.items[1].shortForm && nav.items[2].shortForm,"Short marker survives catalog to UI");
  auto legacy=page; legacy.items.resize(1); legacy.items[0].members=yt::Membership::Only;
  require(wire(legacy).find("\tmembers\n")!=std::string::npos,"ordinary wire remains backwards compatible");
  require(Navigation::parse(wire(legacy),nav,true) && !nav.items.back().shortForm,"old nine-field rows still parse");
  std::cout<<"PASS parser: reel/shortsLockup markers, modern titles, old wire, membership\n";
}
static void layoutContract() {
  for (int cols:{2,3,4}) {
    NavPage p; p.kind="search";
    for (int i=0;i<17;++i) p.items.push_back(card(i));
    p.grid(cols,false);
    require(p.rows.size()==size_t((17+cols-1)/cols),"ordinary rows are packed without empty lines");
    for (size_t i=0;i<p.rows.size();++i) require(!p.rows[i].items.empty() && p.rows[i].items.front()==int(i)*cols,"row index matches rendered order");
    p.sel=1; p.moveGrid(0,1); require(p.sel==cols+1,"down advances exactly one ordinary row");
    p.moveGrid(0,-1); require(p.sel==1,"up returns to original column");
    require(!p.moveGrid(0,-1),"top row returns focus to header");
    p.items.insert(p.items.begin()+cols,card(100,true));
    p.items.push_back(card(101,true)); p.grid(cols,false);
    require(p.rows[1].shorts && p.rows[1].items==std::vector<int>({cols,18}),"all Shorts share one row at first Short");
    p.sel=1; p.moveGrid(0,1); require(p.sel==cols,"down enters Short row");
    p.moveGrid(1,0); require(p.sel==18,"right visits the next Short across source rows");
    p.moveGrid(0,1); require(p.sel==cols+2,"down leaves Short row and restores normal column");
    p.moveGrid(0,-1); require(p.sel==18,"up restores horizontal Short selection");
    p.grid(cols,true); require(p.gridRow()>=0 && !p.items[p.sel].shortForm,"hide repairs selection when focused Short vanishes");
    int count=0;
    for (const auto& row:p.rows) { require(!row.shorts,"hide removes Short row"); count+=(int)row.items.size(); }
    require(count==17 && p.offset==0,"hide keeps all ordinary videos and raw pagination state");
    p.kind="channel"; p.tab="shorts"; p.grid(cols,true);
    require(p.rows[1].shorts,"channel Shorts tab remains available in hide mode");
    p.items.push_back(card(102,true)); p.grid(cols,true);
    require(p.rows[1].items.size()==3,"continuation appends Shorts to original row");
    p.items.clear(); p.grid(cols,false); require(p.rows.empty() && p.sel==0,"empty layout resets focus");
    p.kind="search"; p.items={card(0,true),card(1,true)}; p.grid(cols,true);
    require(p.rows.empty(),"all hidden items leave empty state, no empty grid lines");
  }
  NavPage p; p.kind="search"; p.items={card(0),card(1,true),card(2),card(3,true),card(4)};
  p.grid(4,false);
  require(p.rows.size()==3 && p.rows[0].items==std::vector<int>{0} && p.rows[1].items==std::vector<int>({1,3}) && p.rows[2].items==std::vector<int>({2,4}),"partial rows around first Short retain ordering");
  std::cout<<"PASS layout: 2/3/4 columns, packed rows, controller, hidden focus, channel tab, continuation\n";
}
static void drawContract() {
  SDL_Init(0);
  gfx::Gfx g; g.fonts.resize(ui::F_BADGE+1);
  for (auto& font:g.fonts) { font.size=20; font.lineH=24; for (unsigned cp=32;cp<127;++cp) font.glyphs[cp].adv=10; }
  Thumbs thumbs; Worker worker; Navigation nav;
  NavPage p; p.kind="search"; p.loading=false;
  for (int i=0;i<12;++i) p.items.push_back(card(i));
  nav.pages.push_back(p); nav.format(nav.page(),g,i18n::Lang::EN);
  auto artRows=[&]() {
    std::vector<float> ys;
    for (const auto& cmd:g.cmds) if (cmd.pipe==gfx::SHAPE && std::abs(cmd.params[0]-252)<.01 && std::abs(cmd.params[1]-141.75)<.01)
      for (unsigned i=0;i<cmd.count;i+=6) { float y=g.verts[cmd.first+i].y; if(std::find(ys.begin(),ys.end(),y)==ys.end()) ys.push_back(y); }
    return ys;
  };
  nav.draw(g,thumbs,worker,1,1,i18n::Lang::EN,true);
  auto rows=artRows(); require(rows==std::vector<float>({140,381.75}),"actual renderer draws second thumbnail row even when its text crosses footer (search grid now starts under the chips, y=140)");
  std::cout<<"PASS draw: thumbnail rows at y="<<rows[0]<<", "<<rows[1]<<"; old whole-card cull suppressed row 2\n";
  {  // a busca desenha nas mesmas colunas em que a entrada anda (BAIXO do 1º cartão vai ao 5º, logo abaixo dele)
    std::vector<std::pair<float,float>> at;
    for (const auto& cmd:g.cmds) if (cmd.pipe==gfx::SHAPE && std::abs(cmd.params[0]-252)<.01 && std::abs(cmd.params[1]-141.75)<.01)
      for (unsigned i=0;i<cmd.count;i+=6) at.push_back({g.verts[cmd.first+i].x,g.verts[cmd.first+i].y});
    require(nav.page().gridCols==ui::columns(4),"search draws with the columns the input moves in");
    // o cartão em foco (item 0) cresce e fica fora desta lista: at[0..2] = itens 1..3, at[3] = item 4
    require(at.size()>=4 && at[0].second==at[2].second && at[3].second>at[0].second && at[3].first==136 && at[0].first>136,"cards sit in a 4-column grid: item 4 is back in the first column, one row below");
    auto green=[&]() {
      for (const auto& v:g.verts) if (std::abs(v.r-0x3d/255.f)<.01 && std::abs(v.g-0xdc/255.f)<.01 && std::abs(v.b-0x84/255.f)<.01) return true;
      return false;
    };
    require(!green(),"no members line without members");
    nav.page().items[0].members=yt::Membership::Only; g.verts.clear(); g.cmds.clear();
    nav.draw(g,thumbs,worker,9,9,i18n::Lang::EN,true);
    require(green(),"search result for members-only video carries the green members line");
    nav.page().items[0].members=yt::Membership::None;
    std::cout<<"PASS draw: search grid columns match the input, members line on search cards\n";
  }
  nav.page().items={card(100,true),card(101,true)}; nav.page().gridSize=-1;
  nav.page().sel=0; nav.page().scroll=0; nav.format(nav.page(),g,i18n::Lang::EN);
  g.verts.clear(); g.cmds.clear(); nav.draw(g,thumbs,worker,2,2,i18n::Lang::EN,true);
  int portrait=0;
  for (const auto& cmd:g.cmds) if (cmd.pipe==gfx::SHAPE && std::abs(cmd.params[0]-112)<.01 && std::abs(cmd.params[1]-112.f*16/9)<.01) portrait+=cmd.count/6;
  require(portrait==2,"actual Short cards are smaller 9:16 portraits");
  std::cout<<"PASS draw: 2 Short thumbnails at 112 x "<<112.f*16/9<<" (9:16)\n";
  Thumbs::Slot slot; slot.id=nav.page().items[0].image;
  thumbs.byId[slot.id]=0; thumbs.slots.push_back(slot);
  g.verts.clear(); g.cmds.clear(); nav.draw(g,thumbs,worker,3,3,i18n::Lang::EN,true);
  bool cropped=false;
  for (const auto& cmd:g.cmds) if (cmd.pipe==gfx::IMAGE && std::abs(cmd.params[0]-112)<.01)
    cropped=std::abs(cmd.params[3]-81.f/256)<.001;
  require(cropped,"portrait crops landscape thumbnail slot without stretching");
  std::cout<<"PASS draw: portrait texture crop = 81/256\n";
  ui::W=960; ui::narrow=ui::large=true;
  nav.page().items={card(100,true),card(101,true),card(2),card(3),card(4),card(5)};
  nav.format(nav.page(),g,i18n::Lang::EN);
  g.verts.clear(); g.cmds.clear(); nav.draw(g,thumbs,worker,4,4,i18n::Lang::EN,true);
  require(std::all_of(g.verts.begin(),g.verts.end(),[](const auto& v){return v.y<=720;}),"partial rows never leak below logical safe area on CRT");
  std::cout<<"PASS draw: CRT/large partial thumbnails stay inside logical 720px safe area\n";
  ui::W=1280; ui::narrow=ui::large=false;
  nav.page().kind="library"; nav.page().items={card(100,true),card(101,true)};
  nav.format(nav.page(),g,i18n::Lang::EN);
  g.verts.clear(); g.cmds.clear(); nav.draw(g,thumbs,worker,5,5,i18n::Lang::EN,true);
  require(std::none_of(g.cmds.begin(),g.cmds.end(),[](const auto& cmd){return cmd.pipe==gfx::SHAPE && std::abs(cmd.params[0]-252)<.01 && std::abs(cmd.params[1]-141.75)<.01;}),"library Shorts do not receive a landscape playlist overlay");
  std::cout<<"PASS draw: library Shorts retain compact portrait artwork and labels\n";
  SDL_DestroyCondition(worker.cv); SDL_DestroyMutex(worker.mu); SDL_Quit();
}
int main(int argc,char** argv) {
  require(argc==2,"isolated directory required");
  std::filesystem::create_directories(argv[1]);
  settingsContract(std::string(argv[1])+"/"); parserContract(); layoutContract(); drawContract(); feedContract(std::string(argv[1])+"/");
  std::cout<<f52Checks<<" F52 checks passed\n";
}
