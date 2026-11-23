#include "yt/console_account.hpp"
#include "yt/catalog.hpp"
#include "picojson.h"
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

using yt::string;
using J=picojson::value;
int checks=0;
void check(bool ok,const char* message) {
    ++checks;
    if (!ok) { std::cerr<<"FAIL "<<message<<'\n'; std::exit(1); }
}
string read(const string& path) {
    std::ifstream f(path); std::ostringstream s; s<<f.rdbuf(); return s.str();
}
struct Fake:yt::Transport {
    struct Step { string url, contains, body, bearer; long status; };
    std::deque<Step> steps;
    std::vector<yt::Request> sent;
    void add(string url,string contains,string body,long status=200,string bearer={}) {
        steps.push_back({url,contains,body,bearer,status});
    }
    yt::Response request(const string& url,const string& body,bool form) override {
        yt::Request r; r.url=url; r.body=body; r.form=form; return send(r);
    }
    yt::Response send(const yt::Request& r) override {
        check(!steps.empty(),"unexpected network request");
        auto step=steps.front(); steps.pop_front(); sent.push_back(r);
        if (r.url.find(step.url)==string::npos) std::cerr<<r.url<<" expected "<<step.url<<'\n';
        check(r.url.find(step.url)!=string::npos,"endpoint");
        if (r.body.find(step.contains)==string::npos) std::cerr<<r.body<<" expected "<<step.contains<<'\n';
        check(r.body.find(step.contains)!=string::npos,"payload");
        if (!step.bearer.empty()) {
            bool found=false;
            for (auto& h:r.headers) found|=h=="Authorization: Bearer "+step.bearer;
            check(found,"TV authorization header");
        }
        return {step.status,step.body,{}};
    }
};
const string video="video000001";
const string chan="UC0000000000000000000001";
const string token=R"({"access_token":"fake-a","refresh_token":"fake-r","token_type":"Bearer","expires_in":3600})";
void seed(const string& path,int expiry=5000) {
    std::ofstream f(path);
    f<<R"({"access_token":"fake-a","refresh_token":"fake-r","expires_at":)"<<expiry<<R"(,"client":{"client_id":"fake-client","client_secret":"fake-secret"},"name":"Conta de teste"})";
}
void login(const string& path) {
    Fake f; yt::ConsoleAccount a(f,path);
    f.add("/tv","",R"(<script>ytcfg.set({"INNERTUBE_CLIENT_VERSION":"fixture-tv"})</script><script id="base-js" src="/s/tv/fixture/base.js"></script>)");
    f.add("/s/tv/fixture/base.js","",R"(var a={clientId:"fake-client",clientSecret:"fake-secret"};)");
    f.add("/o/oauth2/device/code","youtube-paid-content",R"({"device_code":"fake-device","user_code":"TEST-F11","verification_url":"https://www.google.com/device","expires_in":90,"interval":5})");
    auto r=a.request("/api/account/login",1000,10);
    check(a.accessToken().empty() && a.tokenRevision()==0,"pending login does not expose credentials");
    check(r.ok() && r.body.find("pending\t\tTEST-F11")!=string::npos,"TV device login shown");
    auto& start=f.sent.back(); J body;
    check(picojson::parse(body,start.body).empty() && body.get("device_model").get<string>()=="ytlr::","TV device model");
    check(!start.form && body.get("scope").get<string>()=="http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content","TV OAuth JSON and writable scopes");
    auto n=f.sent.size(); a.request("/api/account",1001,11);
    check(f.sent.size()==n,"poll interval enforced");
    f.add("/o/oauth2/token","oauth.net",R"({"error":"slow_down"})",400);
    a.request("/api/account",1005,15);
    n=f.sent.size(); a.request("/api/account",1010,20);
    check(f.sent.size()==n,"slow down enforced");
    f.add("/o/oauth2/token",R"("code":"fake-device")",token);
    f.add("/account/accounts_list",R"("clientName":"TVHTML5")",R"({"contents":[{"accountItem":{"accountName":{"simpleText":"Outra"},"isSelected":false}},{"accountItem":{"accountName":{"simpleText":"Conta F11"},"isSelected":true}}]})",200,"fake-a");
    r=a.request("/api/account",1015,25);
    check(r.ok() && r.body.find("signed_in\tConta F11")!=string::npos,"login completed with selected identity");
    check(a.accessToken()=="fake-a" && a.tokenRevision()==1,"completed login publishes Music token");
    J saved; check(picojson::parse(saved,read(path)).empty(),"persisted JSON");
    check(saved.get("expires_at").get<double>()==4615,"expiry persisted as wall clock");
    struct stat info{}; check(stat(path.c_str(),&info)==0 && (info.st_mode&0777)==0600,"token file private");
    check(f.steps.empty(),"login exchanges consumed");
}
void refresh(const string& path) {
    seed(path,1001);
    Fake f; yt::ConsoleAccount a(f,path);
    f.add("/o/oauth2/token",R"("refresh_token":"fake-r")",R"({"access_token":"fake-b","token_type":"Bearer","expires_in":3600})");
    f.add("/account/accounts_list",R"("clientName":"TVHTML5")",R"({"contents":[]})",200,"fake-b");
    check(a.request("/api/account",1000,10).ok(),"restore and refresh");
    check(read(path).find("fake-b")!=string::npos && read(path).find("fake-r")!=string::npos,"refreshed token persisted preserving refresh");
    check(a.accessToken()=="fake-b","refresh publishes new Music access token");
    seed(path,1001); Fake retry; yt::ConsoleAccount b(retry,path);
    retry.add("/o/oauth2/token","",R"({})",503);
    check(!b.request("/api/account",1000,10).ok(),"transient refresh fails visibly");
    auto n=retry.sent.size();
    check(!b.request("/api/account",1001,11).ok() && retry.sent.size()==n,"refresh retry backoff");
    retry.add("/o/oauth2/token","",R"({"error":"invalid_grant"})",400);
    auto r=b.request("/api/account",1031,41);
    check(r.ok() && r.body.find("signed_out")!=string::npos && access(path.c_str(),F_OK)!=0,"revocation clears token and session");
    check(b.accessToken().empty(),"revocation removes Music credentials");
}
string tile(const string& id,const string& title) {
    return R"({"tileRenderer":{"contentType":"TILE_CONTENT_TYPE_VIDEO","contentId":")"+id+R"(","onSelectCommand":{"watchEndpoint":{"videoId":")"+id+R"("}},"metadata":{"tileMetadataRenderer":{"title":{"simpleText":")"+title+R"("},"lines":[]}},"header":{"tileHeaderRenderer":{"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"text":{"simpleText":"10:00"},"style":"DEFAULT"}},{"thumbnailOverlayResumePlaybackRenderer":{"percentDurationWatched":50}}]}}}})";
}
void feeds(const string& path) {
    seed(path); Fake f; yt::ConsoleAccount a(f,path);
    string cards;
    for (int i=0;i<26;++i) { if (i) cards+=","; char id[12]; std::snprintf(id,sizeof(id),"vid%08d",i); cards+=tile(id,"Título"); }
    auto page=R"({"contents":{"tabs":[{"tabRenderer":{"selected":true,"content":{"items":[)"+cards+R"(],"continuation":{"nextContinuationData":{"continuation":"PAGE2"}}}}},{"tabRenderer":{"selected":false,"content":)"+tile("hidden00001","OCULTO")+R"(}}],"menu":)"+tile("hidden00002","MENU")+R"(}})";
    f.add("/browse",R"("browseId":"FEwhat_to_watch")",page,200,"fake-a");
    auto r=a.request("/api/feed?kind=home",1000,10);
    check(r.ok() && r.body.find("vid00000023")!=string::npos && r.body.find("vid00000024")==string::npos,"first 24 feed items");
    check(r.body.find("OCULTO")==string::npos && r.body.find("MENU")==string::npos,"menus and unselected tabs excluded");
    check(r.body.find("\t600\t")!=string::npos && r.body.find("\t300\n")!=string::npos,"duration and resume position");
    f.add("/browse",R"("continuation":"PAGE2")",R"({"onResponseReceivedActions":[{"appendContinuationItemsAction":{"continuationItems":[)"+tile("vid00000024","Duplicado")+","+tile("vid00000026","Novo")+R"(]}}]})",200,"fake-a");
    r=a.request("/api/feed?kind=home&offset=24",1001,11);
    check(r.ok() && r.body.find("vid00000024")!=string::npos && r.body.find("Duplicado")==string::npos && r.body.find("Novo")!=string::npos,"continuation preserves offset and deduplicates");
    check(r.body.find("ok\thome\t\t0\n")==0,"pagination end advertised");
    f.add("/browse",R"("browseId":"FEsubscriptions")",R"({"contents":[)"+tile(video,"Feed")+R"(]})",200,"fake-a");
    f.add("/browse",R"("browseId":"FEchannels")",R"({"contents":[{"channelRenderer":{"channelId":")"+chan+R"(","title":{"simpleText":"Canal de teste"}}}]})",200,"fake-a");
    r=a.request("/api/nav?kind=subscriptions",1001,11);
    check(r.ok() && r.body.find("channel\t"+chan)!=string::npos && r.body.find("video\t"+video)!=string::npos,"subscriptions include channels and videos");
    f.add("/browse",R"("browseId":"FEplaylist_aggregation")",R"({"contents":[{"gridPlaylistRenderer":{"playlistId":"PLteste","title":{"simpleText":"Minha lista"}}},{"tileRenderer":{"contentType":"TILE_CONTENT_TYPE_PLAYLIST","contentId":"RDmix","onSelectCommand":{"watchEndpoint":{"videoId":"video000001","playlistId":"RDmix"}},"metadata":{"tileMetadataRenderer":{"title":{"simpleText":"Meu mix"}}}}}]})",200,"fake-a");
    r=a.request("/api/nav?kind=library",1002,12);
    check(r.ok() && r.body.find("playlist\tPLteste")!=string::npos && r.body.find("playlist\tRDmix")!=string::npos,"library playlists and mixes");
    f.add("/browse",R"("browseId":"VLWL")",R"({"contents":[)"+tile(video,"Mais tarde")+R"(]})",200,"fake-a");
    check(a.request("/api/nav?kind=later",1003,13).body.find("Mais tarde")!=string::npos,"watch later library");
    check(a.request("/api/nav/play?kind=later",1004,14).body.find("video\t"+video)!=string::npos,"playlist play uses cached native queue");
    check(f.steps.empty(),"feed exchanges consumed");
}
void concurrency(const string& path) {
    seed(path,1001); Fake f; yt::ConsoleAccount a(f,path);
    f.add("/o/oauth2/token","refresh_token",token);
    f.add("/browse",R"("browseId":"FEhistory")",R"({"contents":[)"+tile(video,"Histórico")+R"(]})",200,"fake-a");
    yt::AccountReply one,two;
    std::thread first([&]{one=a.request("/api/nav?kind=history",1000,10);});
    std::thread second([&]{two=a.request("/api/nav?kind=history",1000,10);});
    first.join(); second.join();
    check(one.ok() && two.ok() && one.body==two.body && f.steps.empty(),"concurrent workers share one refresh and continuation");
    seed(path);
    Fake retry; yt::ConsoleAccount b(retry,path);
    retry.add("/browse","","{}",401,"fake-a");
    retry.add("/o/oauth2/token","refresh_token",R"({"access_token":"fake-b","token_type":"Bearer","expires_in":3600})");
    retry.add("/browse",R"("browseId":"FEhistory")",R"({"contents":[]})",200,"fake-b");
    check(b.request("/api/nav?kind=history",1000,10).ok() && retry.steps.empty(),"read retries once after early token expiry");
}
void actions(const string& path) {
    seed(path); Fake f; yt::ConsoleAccount a(f,path);
    for (auto& rating:std::vector<std::pair<string,string>>{{"like","like/like"},{"dislike","like/dislike"},{"none","like/removelike"}}) {
        f.add("/"+rating.second,R"("target":{"videoId":")"+video+R"("})","{}",200,"fake-a");
        check(a.request("/api/video/"+video+"/rate?r="+rating.first,1000,10).ok(),"rating action");
    }
    for (auto& action:std::vector<std::pair<string,string>>{{"1","subscribe"},{"0","unsubscribe"}}) {
        f.add("/subscription/"+action.second,R"("channelIds":[")"+chan+R"("])","{}",200,"fake-a");
        check(a.request("/api/nav/action?action=subscribe&id="+chan+"&value="+action.first,1000,10).ok(),"subscription action");
    }
    f.add("/browse/edit_playlist",R"("playlistId":"WL")","{}",200,"fake-a");
    check(a.request("/api/nav/action?action=later&id="+video,1000,10).ok(),"watch later add");
    f.add("/like/like","",R"({"error":{"code":400}})",200,"fake-a");
    check(!a.request("/api/video/"+video+"/rate?r=like",1000,10).ok(),"200 with API error rejected");
    f.add("/like/like","","{}",401,"fake-a");
    check(!a.request("/api/video/"+video+"/rate?r=like",1000,10).ok(),"failed mutation not retried");
    f.add("/o/oauth2/revoke","token=fake-r","{}");
    check(a.request("/api/account/logout",1000,10).ok() && access(path.c_str(),F_OK)!=0,"logout removes persisted token");
    auto n=f.sent.size();
    check(a.request("/api/nav?kind=library",1001,11).error=="signed_out" && f.sent.size()==n,"private library requires account");
    check(a.request("/api/video/"+video+"/rate?r=like",1001,11).error=="signed_out","rating requires account");
}
void history(const string& path) {
    seed(path); Fake f; yt::ConsoleAccount a(f,path);
    f.add("/player",R"("clientVersion":"5.20260707")",R"({"playbackTracking":{"videostatsPlaybackUrl":{"baseUrl":"https://s.youtube.com/api/stats/playback?ei=fake"},"videostatsWatchtimeUrl":{"baseUrl":"https://s.youtube.com/api/stats/watchtime?ei=fake"}}})",200,"fake-a");
    f.add("https://www.youtube.com/api/stats/playback?","","",204,"fake-a");
    f.add("/api/stats/watchtime?","","",204,"fake-a");
    check(a.request("/api/account/watched?id="+video+"&pos=12.5",1000,10).ok(),"native playback and progress recorded");
    check(f.sent.back().url.find("cmt=12.500")!=string::npos && f.sent.back().url.find("final=1")!=string::npos,"watchtime query matches playback position");
    f.add("/api/stats/watchtime?","","",204,"fake-a");
    check(a.request("/api/account/watched?id="+video+"&pos=30&progress=1",1030,40).ok(),"progress reuses tracking without duplicate start");
    f.add("/player","",R"({"playbackTracking":{"videostatsPlaybackUrl":{"baseUrl":"https://www.youtube.com/api/stats/playback?ei=new"},"videostatsWatchtimeUrl":{"baseUrl":"https://www.youtube.com/api/stats/watchtime?ei=new"}}})",200,"fake-a");
    f.add("/api/stats/playback?","","",204,"fake-a");
    auto before=f.sent.size();
    check(a.request("/api/account/watched?id="+video+"&pos=0",1031,41).ok() && f.sent.size()==before+2,"restarting the same video opens fresh playback tracking");
    f.add("/player","",R"({"playbackTracking":{"videostatsPlaybackUrl":{"baseUrl":"https://evil.example/api/stats/playback"},"videostatsWatchtimeUrl":{"baseUrl":"https://evil.example/api/stats/watchtime"}}})",200,"fake-a");
    check(a.request("/api/account/watched?id=video000002",1030,40).error=="tracking_unavailable","tracking origin allowlist");
    check(f.steps.empty(),"history exchanges consumed");
}
const string tvSession=R"SESSAO()]}'
[[null,null,[[["pt","BR",null,"203.0.113.16",null,null,null,null,null,null,null,"","","CgtmaXh0dXJlMDEyMyjQ7P6-BjIKCgJCUhIEGgAgWg%3D%3D",null,null,"2.20261123.00.00","X11","",null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,["fixture-app-install"],null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,"UTC",null,null,null,null,null,null,"Chrome","150.0.0.0",null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,"fixture-device-experiment",null,null,null,"fixture-rollout"]],"fixture-api-key"]]])SESSAO";
void music(const string& path) {
    seed(path,1100); Fake f; yt::ConsoleAccount a(f,path); yt::catalog::Client catalog(f);
    auto browse=[&](const string& bearer) {
        // Logado, a Música abre a sessão pelo sw.js_data, como a youtubei.js (F16); deslogado, pela página do Music.
        if (!bearer.empty()) f.add("https://www.youtube.com/sw.js_data","",tvSession);
        else f.add("https://music.youtube.com/","",R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB_REMIX","clientVersion":"fixture"}}})");
        f.add(bearer.empty() ? "https://music.youtube.com/youtubei/v1/browse" : "https://www.youtube.com/youtubei/v1/browse",
              R"("browseId":"FEmusic_home")",R"({"contents":{}})",200,bearer);
        check(catalog.setToken(a.accessToken()),"account token accepted by Music");
        check(catalog.browse({{"kind","music"}}).error.empty(),"account Music Home loads");
        bool authorized=false;
        for (auto& h:f.sent.back().headers) authorized |= h.compare(0,14,"Authorization:")==0;
        check(authorized==!bearer.empty(),"Music bearer removed when signed out");
    };
    f.add("/account/accounts_list","","{}",200,"fake-a");
    check(a.request("/api/account",1000,10).ok(),"restore Music account");
    browse("fake-a");
    auto revision=a.tokenRevision(), sent=f.sent.size();
    check(a.request("/api/account",1001,11).ok() && a.tokenRevision()==revision,"unchanged account keeps token revision");
    check(catalog.browse({{"kind","music"}}).error.empty() && f.sent.size()==sent,"unchanged Music token retains cache");
    f.add("/o/oauth2/token","refresh_token",R"({"access_token":"fake-b","expires_in":3600,"token_type":"Bearer"})");
    check(a.request("/api/account",1050,60).ok() && a.tokenRevision()>revision,"refresh advances Music revision");
    browse("fake-b");
    f.add("/o/oauth2/revoke","token=fake-r","{}");
    check(a.request("/api/account/logout",1051,61).ok() && a.accessToken().empty(),"logout clears Music token");
    browse("");
    check(f.steps.empty(),"Music reloads session and Home after refresh and logout");
}
int main(int argc,char** argv) {
    if (argc!=2) return 1;
    string path=string(argv[1])+"/test-token.json";
    unlink(path.c_str());
    login(path); refresh(path); feeds(path); concurrency(path); actions(path); history(path); music(path);
    unlink(path.c_str());
    std::cout<<checks<<" checks passed\n";
}
