#include "service.hpp"
#include "public_key.hpp"
#include "yt/thread.hpp"
#include "test_transport.hpp"
#include <algorithm>
#include <cstdio>
#ifdef __XSDK__
#include <xsdk/thread.h>
#include <xsdk/log.h>
#endif

namespace updates {
static std::unique_ptr<yt::Transport> transport(const std::atomic_bool* cancel) {
#ifdef TUBE360_UPDATE_TEST_CONFIG
    return std::make_unique<TestTransport>(cancel);
#else
    return yt::https(cancel);
#endif
}
Service::Service(string directory):dir(std::move(directory)) {
    base="https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/";
#ifdef TUBE360_UPDATE_TEST_CONFIG
    base=TUBE360_UPDATE_TEST_BASE;
    event("TUBE360-DISPOSABLE-UPDATE-TEST");
#endif
    auto configured=read(dir+"updates-url.txt",2048);
    while(!configured.empty() && (configured.back()=='\n' || configured.back()=='\r'))configured.pop_back();
    if(!configured.empty() && configured.back()=='/' && updateUrl(configured))base=configured;
    store=std::make_shared<RuleStore>(dir,publicKeyHex);setRuleStore(store);
    net=transport(&stop);
    thread=yt::worker(run,"atualizações",this);
}
Service::~Service(){stop=true;abortDownload=true;if(thread)SDL_WaitThread(thread,nullptr);}
void Service::event(const string& message) {
    auto line="update: "+message;
    if(auto* f=std::fopen((dir+"update.log").c_str(),"a")){std::fprintf(f,"%s\n",line.c_str());std::fclose(f);}
#ifdef __XSDK__
    xsdk_log_write("%s",line.c_str());
#endif
}
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
    Status before;{std::lock_guard<std::mutex> l(mu);before=state;}
    AppUpdate cached(dir,publicKeyHex);Manifest pending;
    bool staged=cached.stagedManifest(pending);
    if(staged){Status s;s.state=State::READY;s.app=pending;publish(s);}
    else {Status s;s.state=State::CHECKING;publish(s);}
    event("check inicio");
    auto r=fetch(base+"rules.json"),s=fetch(base+"rules.json.sig",129);
    string error;
    if(r.status==200 && r.error.empty() && s.status==200 && s.error.empty())store->accept(r.body,s.body,error);
    if(stop || staged)return;
    r=fetch(base+"app.json");s=fetch(base+"app.json.sig",129);
    Manifest m;AppUpdate app(dir,publicKeyHex);
    bool authenticated=r.status==200 && r.error.empty() && s.status==200 && s.error.empty() && verify(r.body,s.body,publicKeyHex) && decodeManifest(r.body,m);
    if(authenticated && m.build>app.highWater()) {
        manifestBytes=r.body;manifestSig=s.body;
        Status st;st.state=State::AVAILABLE;st.app=m;publish(st);
        event("available build="+std::to_string(m.build)+" version="+m.version+" bytes="+std::to_string(m.size));
    } else {
        if(before.app.build>app.highWater())publish(before);
        else {Status st;if(!authenticated && r.status!=404)st.state=State::CHECK_FAILED;publish(st);}
        event("check fim: sem candidato novo validado");
    }
}
void Service::install() {
    Status st;{std::lock_guard<std::mutex> l(mu);st=state;}
    if(st.state!=State::AVAILABLE && st.state!=State::ERROR)return;
    Manifest m;
    if(!verify(manifestBytes,manifestSig,publicKeyHex) || !decodeManifest(manifestBytes,m))return;
    st.state=State::DOWNLOADING;st.error.clear();st.progress=0;publish(st);
    event("download inicio build="+std::to_string(m.build));
    AppUpdate app(dir,publicKeyHex);
    if(!exists(app.executable())){st.state=State::ERROR;st.error="setup";publish(st);return;}
    // Um candidato já rejeitado não precisa ocupar uma quarta cópia no próximo download.
    std::remove((dir+"tube360.failed.xex").c_str());
    string part=app.pending()+".part",url=m.url;
    // Flag independente: B interrompe também uma requisição em curso, sem cancelar as futuras checagens.
    auto transfer=transport(&abortDownload);
    auto* f=std::fopen(part.c_str(),"wb");
    bool ok=f!=nullptr;int offset=0;
    while(ok && offset<m.size && !stop && !abortDownload) {
        int last=std::min(m.size-1,offset+512*1024-1);
        yt::Response res;
        for(int hop=0;hop<5 && !stop && !abortDownload;hop++) {
            if(!updateUrl(url)){ok=false;break;}
            yt::Request r;r.url=url;r.update=true;r.maxBytes=size_t(last-offset+1);
            r.headers={"Cookie:","Range: bytes="+std::to_string(offset)+"-"+std::to_string(last)};
            res=transfer->send(r);
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
    if(stop || abortDownload){std::remove(part.c_str());st.state=State::AVAILABLE;publish(st);event("download cancelado");return;}
    string error;
    if(ok && offset==m.size && fileHash(part)==m.hash && !abortDownload && !stop &&
        (!exists(app.pending()) || std::remove(app.pending().c_str())==0) &&
        std::rename(part.c_str(),app.pending().c_str())==0 && app.stage(manifestBytes,manifestSig,error)) {
        st.state=State::READY;event("staged build="+std::to_string(m.build)+" sha256="+m.hash);
    } else {
        st.state=abortDownload?State::AVAILABLE:State::ERROR;st.error=error.empty()?"download":error;
        std::remove(part.c_str());std::remove(app.pending().c_str());event("download fim erro="+st.error);
    }
    publish(st);
}
void Service::work() {
#ifdef __XSDK__
    xsdk_thread_move(5);
#endif
    uint64_t next=0;bool confirmed=false;
    while(!stop) {
        auto now=SDL_GetTicks();
        // Confirma antes de qualquer espera de rede: um GitHub lento não deve rejeitar um boot saudável.
        if(!confirmed) {
            if(!healthy){SDL_Delay(100);continue;}
#if defined(TUBE360_UPDATE_TEST_CONFIG) && defined(TUBE360_UPDATE_TEST_FAIL_BOOT)
            event("TEST boot confirmation intentionally withheld build="+std::to_string(buildId));
#else
            AppUpdate app(dir,publicKeyHex,[&](const string& s){event(s);});
            if(app.confirm(buildId))event("boot confirmed build="+std::to_string(buildId));
#endif
            confirmed=true;
        }
        if(download.exchange(false))install();
        if(refresh.exchange(false) || (home && now>=next)){check();next=SDL_GetTicks()+uint64_t(store->snapshot()->n("checkHours"))*3600000;}
        SDL_Delay(100);
    }
}
}
