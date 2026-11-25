#include "yt/catalog.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
using namespace yt::catalog;
using yt::string;
using yt::search::Filters;
static int checks=0;
static void check(bool ok,const char* label) { ++checks; if (!ok) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); } }
static string read(const string& dir,const string& file) { std::ifstream in(dir+"/"+file); check(bool(in),"fixture exists"); std::stringstream s; s<<in.rdbuf(); return s.str(); }
struct Http : yt::Transport {
    struct Step { string route, body; Object fields; };
    std::deque<Step> steps;
    yt::Response request(const string& url,const string& body={},bool=false) override { return send({url,body}); }
    yt::Response send(const yt::Request& r) override {
        check(!steps.empty(),"unexpected request fails closed"); auto step=steps.front(); steps.pop_front();
        check(r.url=="https://www.youtube.com/"+step.route,"route");
        if (!step.fields.empty()) {
            J j; check(parse(r.body,j),"request JSON");
            for (const auto& kv:step.fields) check(get(j,kv.first)==kv.second,"wire field matches independent fixture");
            check(str(get(get(j,"context"),"client"),"hl")=="pt","localized WEB context");
            check(str(get(get(j,"context"),"client"),"clientName")=="WEB","public search is WEB, not Music");
        }
        return {200,step.body,{}};
    }
};
int main(int argc,char** argv) {
    check(argc==2,"fixture directory required"); string dir=argv[1];
    auto suggestions=yt::search::suggestions(read(dir,"suggestions-pt.jsonp"),"astronomia");
    check(suggestions.size()==8 && suggestions[0]=="astronomia" && suggestions[5]=="astronomia para crianças","recorded suggestions and UTF-8, cap 8");
    check(yt::search::suggestions(read(dir,"suggestions-pt.jsonp"),"changed").empty(),"wrong query is discarded");
    check(yt::search::suggestions("[\"x\",[\"a\",\"a\",[\"b\",0],[\"bad\\ntext\"],4,[],{}]]","x")==std::vector<string>({"a","b"}),"JSON variant, dedupe, unsafe rows");
    for (const auto& bad: {"window.google.ac.h({})", "[\"x\",[]]garbage", "<html>error</html>", "[\"x\",null]"}) check(yt::search::suggestions(bad,"x").empty(),"malformed response silent");
    check(yt::search::suggestions(string(32769,'['),"x").empty(),"body bound");
    check(yt::search::suggestions(string(20,'[')+string(20,']'),"x").empty(),"depth bound");
    check(yt::search::url("مرحبا &+?","ar").find("hl=ar&gl=SA&q=%D9%85")!=string::npos,"Arabic locale and URL encoding");
    check(yt::search::encode("é &+?")=="%C3%A9%20%26%2B%3F","UTF-8 bytes and metacharacters");
    J recorded; auto bytes=read(dir,"filters-public.json"); string error; picojson::parse(recorded,bytes.begin(),bytes.end(),&error);
    check(error.empty() && recorded.is<Array>(),"recorded filter endpoints");
    const std::pair<const char*,Filters> cases[]={
        {"Vídeos",{1}}, {"Canais",{2}}, {"Playlists",{3}}, {"Ao vivo",{4}},
        {"Menos de 3 minutos",{0,1}}, {"De 3 a 20 minutos",{0,2}}, {"Com mais de 20 minutos",{0,3}},
        {"Hoje",{0,0,1}}, {"Esta semana",{0,0,2}}, {"Este mês",{0,0,3}}, {"Este ano",{0,0,4}}, {"Popularidade",{0,0,0,1}}
    };
    for (auto& c:cases) {
        string expected;
        for (const auto& entry:array(recorded)) if (text(get(entry,"label"))==c.first) expected=str(entry,"params");
        check(!expected.empty() && c.second.params()==expected,"params equals actual YouTube searchEndpoint");
    }
    check(Filters{}.params().empty(),"clear omits params");
    check(Filters{0,0,0,1}.params()=="CAM%3D","view-count order verified by coordinator on real WEB search");
    Filters sort;
    for (int direction:{1,1,-1,-1}) {
        sort.change(3,direction);
        check(sort.params()==(sort.order ? "CAM%3D" : ""),"order cycles only relevance/view count in both directions");
    }
    check(sort.order==0,"two steps return to relevance in either direction");
    Filters state; state.change(1,1); check(state.type==1 && state.duration==1,"duration selects video");
    state.change(0,1); check(state.type==2 && !state.duration && state.disabled(1) && state.disabled(2),"channel clears incompatible filters");
    for (int t=-1;t<=6;++t) for (int d=-1;d<=5;++d) for (int date=-1;date<=6;++date) for (int order=-1;order<=4;++order) {
        Filters f{t,d,date,order}; f.normalize();
        check(f.type>=0 && f.type<=4 && f.duration>=0 && f.duration<=3 && f.date>=0 && f.date<=4 && f.order>=0 && f.order<=1,"bounds");
        check((f.type!=2 && f.type!=3) || (!f.duration && !f.date),"incompatible wire filters removed");
        check(f.params().compare(0,3,"CAI")!=0,"no normalized combination emits removed date ordering");
    }
    Http h; Client client(h);
    auto publicPage=read(dir,"search-public.json");
    h.steps.push_back({"",read(dir,"home.html"),{}});
    h.steps.push_back({"youtubei/v1/search?prettyPrint=false",publicPage,{{"query",J(string("astronomia"))},{"params",J(Filters{2}.params())}}});
    std::map<string,string> query{{"kind","search"},{"q","astronomia"},{"lang","pt"},{"params",Filters{2}.params()}};
    auto page=client.browse(query);
    check(page.error.empty() && !page.items.empty(),"public search through typed catalogue");
    bool video=false,channel=false,playlist=false;
    for (const auto& item:page.items) { video|=item.type=="video"; channel|=item.type=="channel"; playlist|=item.type=="playlist"; }
    check(video && channel && playlist,"recorded video/channel/playlist keep types");
    check(!client.browse(query).items.empty() && h.steps.empty(),"cached search does not send request");
    // Continuation is explicitly synthetic, with the same wrapper used by WEB.
    J continued; check(parse(publicPage,continued),"public JSON");
    continued.get<Object>()["contents"].get<Array>().push_back(J(Object{{"continuationItemRenderer",J(Object{{"continuationEndpoint",J(Object{{"continuationCommand",J(Object{{"token",J(string("TEST_SEARCH_NEXT"))}})}})}})}}));
    query["params"]=Filters{3}.params();
    h.steps.push_back({"youtubei/v1/search?prettyPrint=false",continued.serialize(),{{"query",J(string("astronomia"))},{"params",J(query["params"])}}});
    auto nextPage=client.browse(query); check(!nextPage.continuation.empty(),"filter change partitions cache");
    query["offset"]=std::to_string(nextPage.items.size());
    h.steps.push_back({"youtubei/v1/search?prettyPrint=false","{\"contents\":[{\"videoRenderer\":{\"videoId\":\"next0000001\",\"title\":{\"simpleText\":\"next\"}}}]}",{{"continuation",J(string("TEST_SEARCH_NEXT"))}}});
    check(client.browse(query).items.size()==1,"continuation uses its filter cursor");
    query["params"]=Filters{1,1,2,1}.params();
    check(client.browse(query).error=="continuation_expired","cursor cannot cross filters");
    check(h.steps.empty(),"all requests consumed");
    std::cout<<checks<<" search checks passed\n";
}
