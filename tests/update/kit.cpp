#include "update/app.hpp"
#include "update/public_key.hpp"
#include <cstdlib>
#include <iostream>
using namespace updates;
static void check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL kit: "<<why<<'\n';std::exit(1);}}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    string publicDir=string(argv[1])+"/",dir=string(argv[2])+"/",error;
    for(const char* label:{"good","fail"}) {
        auto bytes=read(publicDir+label+".json"),sig=read(publicDir+label+".json.sig");Manifest m;
        check(verify(bytes,sig,publicKeyHex) && decodeManifest(bytes,m),"actual manifest signature/schema");
        auto payload=read(publicDir+label+".xex",64*1024*1024);
        check(int(payload.size())==m.size && sha256(payload)==m.hash,"actual candidate size/hash");
        AppUpdate staged(dir,publicKeyHex);check(write(staged.pending(),payload),"copy real candidate");
        check(staged.stage(bytes,sig,error),"stage real candidate");
        AppUpdate launcher(dir,publicKeyHex);check(launcher.prepare(error)==launcher.executable(),"promote real candidate");
        if(string(label)=="good") {
            check(launcher.confirm(m.build),"confirm real candidate bytes");
            AppUpdate again(dir,publicKeyHex);
            check(again.prepare(error)==again.executable() && fileHash(again.executable())==m.hash,"confirmed real candidate persists");
        } else {
            AppUpdate reboot(dir,publicKeyHex);
            check(reboot.prepare(error)==reboot.executable(),"unconfirmed actual failure restores backup");
            Manifest good;check(decodeManifest(read(publicDir+"good.json"),good),"good manifest");
            check(fileHash(reboot.executable())==good.hash && reboot.highWater()==m.build,"real rollback returns good hash and keeps rejected high water");
            check(!reboot.stage(bytes,sig,error) && error=="version","failed build cannot be restaged");
        }
    }
    std::cout<<"PASS actual Xbox payload artifacts: signed candidates staged, good confirmed, fail rolled back; no XEX executed on PC\n";
}
