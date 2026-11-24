#include "service.hpp"
#include "public_key.hpp"
#include "yt/thread.hpp"
#include <algorithm>
#include <cstdio>
#ifdef __XSDK__
#include <xsdk/thread.h>
#endif

namespace updates {
Service::Service(string directory):dir(std::move(directory)) {
    base="https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/";
    auto configured=read(dir+"updates-url.txt",2048);
    while(!configured.empty() && (configured.back()=='\n' || configured.back()=='\r'))configured.pop_back();
    if(!configured.empty() && configured.back()=='/' && updateUrl(configured))base=configured;
    store=std::make_shared<RuleStore>(dir,publicKeyHex);setRuleStore(store);
    net=yt::https(&stop);
    thread=yt::worker(run,"atualizações",this);
}
Service::~Service(){stop=true;if(thread)SDL_WaitThread(thread,nullptr);}
int Service::run(void* p){static_cast<Service*>(p)->work();return 0;}
void Service::publish(Status s){std::lock_guard<std::mutex> l(mu);state=std::move(s);++revision;}
unsigned Service::poll(Status& out,unsigned previous){auto r=revision.load();if(r!=previous){std::lock_guard<std::mutex> l(mu);out=state;}return r;}
yt::Response Service::fetch(const string& original,size_t limit) {
    string url=original;
    for(int hop=0;hop<5 && !stop;hop++) {
        if(!updateUrl(url))return {0,{},"update_url"};
        yt::Request r;r.url=url;r.update=true;r.maxBytes=limit;r.headers={"Cookie:"};
        auto res=net->send(r);
        if(res.status!=301 && res.status!=302 && res.status!=303 && res.status!=307 && res.status!=308)return res;
        url=res.location;
    }
    return {0,{},"update_redirect"};
}
void Service::check() {
    AppUpdate cached(dir,publicKeyHex);Manifest pending;
    if(cached.stagedManifest(pending)){Status s;s.state=State::READY;s.app=pending;publish(s);}
    auto r=fetch(base+"rules.json"),s=fetch(base+"rules.json.sig",129);
    string error;
    if(r.status==200 && r.error.empty() && s.status==200 && s.error.empty())store->accept(r.body,s.body,error);
    if(stop)return;
    r=fetch(base+"app.json");s=fetch(base+"app.json.sig",129);
    Manifest m;AppUpdate app(dir,publicKeyHex);
    if(r.status==200 && r.error.empty() && s.status==200 && s.error.empty() && verify(r.body,s.body,publicKeyHex) && decodeManifest(r.body,m) && m.build>app.highWater()) {
        manifestBytes=r.body;manifestSig=s.body;
        Status st;st.state=State::AVAILABLE;st.app=m;publish(st);
    }
}
void Service::install() {
    Status st;{std::lock_guard<std::mutex> l(mu);st=state;}
    if(st.state!=State::AVAILABLE && st.state!=State::ERROR)return;
    Manifest m;
    if(!verify(manifestBytes,manifestSig,publicKeyHex) || !decodeManifest(manifestBytes,m))return;
    st.state=State::DOWNLOADING;st.error.clear();st.progress=0;publish(st);
    AppUpdate app(dir,publicKeyHex);
    if(!exists(app.executable())){st.state=State::ERROR;st.error="setup";publish(st);return;}
    string part=app.pending()+".part",url=m.url;
    auto* f=std::fopen(part.c_str(),"wb");
    bool ok=f!=nullptr;int offset=0;
    while(ok && offset<m.size && !stop && !abortDownload) {
        int last=std::min(m.size-1,offset+512*1024-1);
        yt::Response res;
        for(int hop=0;hop<5;hop++) {
            if(!updateUrl(url)){ok=false;break;}
            yt::Request r;r.url=url;r.update=true;r.maxBytes=size_t(last-offset+1);
            r.headers={"Cookie:","Range: bytes="+std::to_string(offset)+"-"+std::to_string(last)};
            res=net->send(r);
            if(res.status==301 || res.status==302 || res.status==303 || res.status==307 || res.status==308){url=res.location;continue;}
            break;
        }
        string range="bytes "+std::to_string(offset)+"-"+std::to_string(last)+"/"+std::to_string(m.size);
        auto actual=res.contentRange;actual.erase(0,actual.find_first_not_of(" \t"));actual.erase(actual.find_last_not_of(" \r\n\t")+1);
        bool whole=offset==0 && last==m.size-1 && res.status==200;
        ok=ok && res.error.empty() && (whole || (res.status==206 && actual==range)) && res.body.size()==size_t(last-offset+1);
        if(ok)ok=std::fwrite(res.body.data(),1,res.body.size(),f)==res.body.size();
        if(ok){offset=last+1;st.progress=int(int64_t(offset)*100/m.size);publish(st);}
    }
    if(f && std::fclose(f))ok=false;
    if(stop || abortDownload){std::remove(part.c_str());st.state=State::AVAILABLE;publish(st);return;}
    string error;
    if(ok && offset==m.size && fileHash(part)==m.hash && std::rename(part.c_str(),app.pending().c_str())==0 && app.stage(manifestBytes,manifestSig,error))st.state=State::READY;
    else {st.state=State::ERROR;st.error=error.empty()?"download":error;std::remove(part.c_str());}
    publish(st);
}
void Service::work() {
#ifdef __XSDK__
    xsdk_thread_move(5);
#endif
    uint64_t next=0;bool confirmed=false;
    while(!stop) {
        auto now=SDL_GetTicks();
        if(!confirmed && healthy){AppUpdate app(dir,publicKeyHex);app.confirm(buildId);confirmed=true;}
        if(download.exchange(false))install();
        if(home && now>=next){check();next=SDL_GetTicks()+uint64_t(store->snapshot()->n("checkHours"))*3600000;}
        SDL_Delay(100);
    }
}
}
