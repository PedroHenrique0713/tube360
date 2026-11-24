#include "update/rules.hpp"
#include "update/app.hpp"
#include "yt/client.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
using namespace updates;
namespace {
int checks=0;
void check(bool v,const char* message){++checks;if(!v){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
struct Net:yt::Transport {
    std::vector<yt::Request> sent;
    bool broken=false, refused=false;
    yt::Response request(const string& u,const string& b={},bool f=false) override{return send({u,b,{},f});}
    yt::Response send(const yt::Request& r) override {
        sent.push_back(r);
        if(r.url.find("/watch?")!=string::npos)return {200,"{\"VISITOR_DATA\":\"fake\",\"jsUrl\":\"/s/player/fake/base.js\"}",{}};
        if(r.url.find("base.js")!=string::npos)return {200,"remoteTimestamp:99999;signatureTimestamp:77777;",{}};
        if(refused)return {200,R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Unsupported client"}})",{}};
        if(broken)return {200,"not json",{}};
        return {200,R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE"},"streamingData":{"formats":[{"itag":18,"width":640,"height":360,"fps":30,"mimeType":"video/mp4; codecs=\"avc1, mp4a\"","url":"https://fixture.googlevideo.com/test"}]}})",{}};
    }
    bool backoff(unsigned) override{return true;}
};
}
int main(int argc,char** argv) {
    if(argc==2 && string(argv[1])=="--github") {
        auto net=yt::https();yt::Request r;r.url="https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/rules.json";r.update=true;
        auto reply=net->send(r);check(reply.error.empty() && (reply.status==200 || reply.status==404),"GitHub HTTPS validates with embedded CA roots");
        std::cout<<"GitHub TLS verified, HTTP "<<reply.status<<"\n";return 0;
    }
    if(argc!=3)return 2;
    string dir=string(argv[1])+"/",key=read(dir+"public.txt"),error;
    auto bytes=read(dir+"rules2.json"),sig=read(dir+"rules2.json.sig");
    check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 known answer");
    check(verify("","e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),"RFC 8032 empty-message test vector");
    check(verify(bytes,sig,key),"OpenSSL Ed25519 signature accepted");
    check(!verify(bytes+" ",sig,key),"exact bytes authenticated");
    check(!verify(bytes,string(128,'0'),key),"invalid signature rejected");
    check(!verify(bytes,sig.substr(1),key),"truncated signature rejected");
    check(!verify(bytes,sig,string(64,'0')),"other public key rejected");
    auto store=std::make_shared<RuleStore>(dir,key);setRuleStore(store);
    check(store->snapshot()->version==1,"embedded fallback");
    check(store->accept(bytes,sig,error),"signed new rules activate");
    check(!store->accept(bytes,sig,error) && error=="version","equal version rejected");
    check(!store->accept(read(argv[2]),sig,error) && error=="signature","older unsigned bytes rejected");
    for(auto name:{"limit.json","domain.json","script.json"})check(!store->accept(read(dir+name),read(dir+name+".sig"),error) && error=="schema","signed invalid schema rejected");
    for(auto u:{"https://youtube.com.evil.example/","https://youtube.com@evil.example/","https://www.youtube.com:443/","http://www.youtube.com/","https://127.0.0.1/","https://www.youtube.com\\@evil.example/"})check(!youtubeUrl(u),"domain boundary, scheme and userinfo rejected");
    check(youtubeUrl("https://r1.googlevideo.com/videoplayback?x=1"),"media subdomain permitted");
    check(updateUrl("https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/app.json"),"configured public repository permitted");
    check(!updateUrl("https://raw.githubusercontent.com/attacker/tube360/rules/app.json"),"different repository rejected");
    Net net;yt::Client client(net);
    auto video=client.player("4p_9OPKgcDE");
    check(video.status=="OK" && video.muxed,"real player uses remote rules");
    check(net.sent.back().agent=="RemoteFixture/7.8" && net.sent.back().headers[0]=="X-Youtube-Client-Name: 102","remote agent and header identity reach transport");
    check(net.sent.back().body.find("\"signatureTimestamp\":99999")!=string::npos && video.playerClient=="REMOTE_PLAYER","remote timestamp marker and client name reach request");
    store->observe(2,true,false);store->observe(2,true,false);
    check(store->accept(read(dir+"rules3.json"),read(dir+"rules3.json.sig"),error),"new candidate after last good");
    store->observe(3,false,false);check(store->failureCount()==0,"network outage does not roll rules back");
    net.broken=true;
    client.player("4p_9OPKgcDE");client.player("4p_9OPKgcDE");
    check(store->snapshot()->version==3 && store->failureCount()==2,"logical failures count once despite HTTP retry");
    client.player("4p_9OPKgcDE");
    check(store->snapshot()->version==2 && store->highWater()==3,"three failures restore last good while keeping high water");
    check(!store->accept(bytes,sig,error) && error=="version","signed older version rejected after rollback");
    auto restored=std::make_shared<RuleStore>(dir,key);check(restored->snapshot()->version==2 && restored->highWater()==3,"cache preserves rollback across restart");
    check(!restored->accept(read(dir+"rules3.json"),read(dir+"rules3.json.sig"),error),"rejected version cannot loop after restart");
    check(restored->accept(read(dir+"rules4.json"),read(dir+"rules4.json.sig"),error),"strictly newer fix remains eligible");
    setRuleStore(restored);net.broken=false;client.player("4p_9OPKgcDE");
    check(net.sent[net.sent.size()-3].url.find("/watch?")!=string::npos,"existing client rebuilds session after rules change");
    auto appBytes=read(dir+"app.json"),appSig=read(dir+"app.json.sig"),payload=read(dir+"download.xex");
    auto scenario=[&](const string& name){string p=dir+name+"/";::mkdir(p.c_str(),0700);write(p+"tube360.xex","XEX2old-good");return p;};
    auto p=scenario("boot");
    AppUpdate app(p,key);write(app.pending(),payload+"corrupt");
    check(!app.stage(appBytes,appSig,error) && error=="hash","hash mismatch cannot stage executable");
    write(app.pending(),payload);
    check(!app.stage(appBytes,string(128,'0'),error) && error=="signature","manifest signature required");
    check(app.stage(appBytes,appSig,error),"signed hash stages candidate");
    check(app.prepare(error)==app.executable() && read(app.executable())==payload,"next boot swaps executable");
    check(read(p+"tube360.previous.xex")=="XEX2old-good","previous executable backed up");
    AppUpdate crash(p,key);
    check(crash.prepare(error)==app.executable() && read(app.executable())=="XEX2old-good","missing boot confirmation restores old executable on next launcher run");
    check(crash.highWater()==2 && !crash.stage(appBytes,appSig,error),"failed app build is not retried");
    p=scenario("healthy");AppUpdate healthy(p,key);write(healthy.pending(),payload);healthy.stage(appBytes,appSig,error);healthy.prepare(error);
    check(!healthy.confirm(1) && healthy.confirm(2),"only intended running build confirms boot");
    AppUpdate next(p,key);check(next.prepare(error)==next.executable() && read(next.executable())==payload,"confirmed update survives restart");
    p=scenario("interrupted");AppUpdate partial(p,key);write(partial.pending(),payload);partial.stage(appBytes,appSig,error);
    J state;parse(read(p+"update-app-state.json",maxDocument*3),state);state.get<O>()["phase"]=J(string("swapping"));write(p+"update-app-state.json",state.serialize());
    std::rename(partial.executable().c_str(),(p+"tube360.previous.xex").c_str());
    AppUpdate recovery(p,key);check(recovery.prepare(error)==recovery.executable() && read(recovery.executable())==payload,"power loss between renames resumes transaction");
    check(recovery.failed(error) && read(recovery.executable())=="XEX2old-good","launcher load failure restores backup immediately");
    p=scenario("tampered");AppUpdate tampered(p,key);write(tampered.pending(),payload);tampered.stage(appBytes,appSig,error);write(tampered.pending(),"XEX2tampered");
    check(tampered.prepare(error)==tampered.executable() && read(tampered.executable())=="XEX2old-good","pending rechecked at boot before replacing old app");
    p=dir+"late-rules/";::mkdir(p.c_str(),0700);RuleStore late(p,key);late.accept(bytes,sig,error);
    for(int i=0;i<3;i++)late.observe(2,true,false);
    late.observe(2,false,true);late.observe(2,false,true);
    RuleStore lateRestart(p,key);lateRestart.observe(2,false,true);
    check(lateRestart.snapshot()->version==1 && lateRestart.highWater()==2,"backup remains available after early successes and counters survive restart");
    p=dir+"refused-rules/";::mkdir(p.c_str(),0700);auto refused=std::make_shared<RuleStore>(p,key);refused->accept(bytes,sig,error);setRuleStore(refused);
    Net refusedNet;refusedNet.refused=true;yt::Client refusedClient(refusedNet);
    for(int i=0;i<3;i++)refusedClient.player("4p_9OPKgcDE");
    check(refused->snapshot()->version==1 && refused->highWater()==2,"valid JSON with all clients refused also triggers rollback");
    setRuleStore(nullptr);
    std::cout<<checks<<" signed update checks passed\n";
}
