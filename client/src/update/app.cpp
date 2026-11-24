#include "app.hpp"
#include <algorithm>
#include <climits>
#include <cstdio>

namespace updates {
bool decodeManifest(const string& raw,Manifest& m) {
    J j; m={};
    if(raw.size()>maxDocument || !parse(raw,j) || !fields(j,{"schema","kind","build","version","url","size","sha256","notes"}) ||
        number(get(j,"schema"),1,1)!=1 || text(j,"kind")!="tube360-app" || !fields(get(j,"notes"),{"en","pt"})) return false;
    m.build=number(get(j,"build"),1,INT_MAX); m.size=number(get(j,"size"),4,64*1024*1024);
    m.version=text(j,"version");m.url=text(j,"url");m.hash=text(j,"sha256");m.notesEn=text(get(j,"notes"),"en");m.notesPt=text(get(j,"notes"),"pt");
    auto note=[](const string& s){return !s.empty() && s.size()<=1200 && std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127;});};
    return m.build>0 && m.size>=4 && ascii(m.version,40) && m.version.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-+")==string::npos &&
        updateUrl(m.url) && m.hash.size()==64 && m.hash.find_first_not_of("0123456789abcdef")==string::npos && note(m.notesEn) && note(m.notesPt);
}
AppUpdate::AppUpdate(string directory,string pub):dir(std::move(directory)),key(std::move(pub)) {
    auto raw=read(dir+"update-app-state.json",maxDocument*3);
    if(raw.empty()) {valid=!exists(dir+"update-app-state.json");return;}
    J j; Manifest m;
    valid=parse(raw,j) && fields(j,{"phase","highest","bytes","signature"});
    if(!valid)return;
    phase=text(j,"phase");bytes=text(j,"bytes");sig=text(j,"signature");
    int h=number(get(j,"highest"),1,INT_MAX);
    valid=h>0 && verify(bytes,sig,key) && decodeManifest(bytes,m) && h>=m.build &&
        (phase=="staged" || phase=="swapping" || phase=="trial" || phase=="good" || phase=="rollback");
    if(valid)highest=std::max(h,buildId);
}
bool AppUpdate::save() {return write(dir+"update-app-state.json",J(O{{"phase",J(phase)},{"highest",J(double(highest))},{"bytes",J(bytes)},{"signature",J(sig)}}).serialize());}
bool AppUpdate::checkPending(const Manifest& m) {
    auto* f=std::fopen(pending().c_str(),"rb");if(!f)return false;
    char magic[4];bool ok=std::fread(magic,1,4,f)==4 && string(magic,4)=="XEX2";
    if(std::fseek(f,0,SEEK_END)!=0 || std::ftell(f)!=m.size)ok=false;
    std::fclose(f);return ok && fileHash(pending())==m.hash;
}
bool AppUpdate::stage(const string& raw,const string& signature,string& error) {
    Manifest m;
    if(!valid){error="state";return false;}
    if(!verify(raw,signature,key)){error="signature";return false;}
    if(!decodeManifest(raw,m)){error="schema";return false;}
    if(m.build<=highest || phase=="trial" || phase=="swapping"){error="version";return false;}
    if(!checkPending(m)){error="hash";return false;}
    auto ob=bytes,os=sig,op=phase;int oh=highest;
    bytes=raw;sig=signature;phase="staged";highest=m.build;
    if(!save()){bytes=ob;sig=os;phase=op;highest=oh;error="disk";return false;}
    error.clear();return true;
}
bool AppUpdate::restore(string& error) {
    phase="rollback";if(!save()){error="journal";return false;}
    auto backup=dir+"tube360.previous.xex";
    if(exists(backup)) {
        if(exists(dir+"tube360.failed.xex") && std::remove((dir+"tube360.failed.xex").c_str())){error="remove_failed";return false;}
        if(exists(executable()) && std::rename(executable().c_str(),(dir+"tube360.failed.xex").c_str())){error="move_failed";return false;}
        if(std::rename(backup.c_str(),executable().c_str())){error="restore";return false;}
    }
    if(!exists(executable())){error="missing_backup";return false;}
    error.clear();return true;
}
string AppUpdate::prepare(string& error) {
    error.clear();
    if(!valid){
        // Journal ilegível não pode impedir a abertura da reserva já instalada, nem promover .next.
        auto backup=dir+"tube360.previous.xex";
        if(exists(backup))return backup;
        if(exists(executable()))return executable();
        error="state";return {};
    }
    if(phase=="trial" || phase=="rollback")return restore(error)?executable():"";
    if(phase=="staged" || phase=="swapping") {
        Manifest m;if(!verify(bytes,sig,key) || !decodeManifest(bytes,m)){error="signature";return {};}
        auto backup=dir+"tube360.previous.xex";
        if(phase=="staged") {
            if(!exists(executable())){error="missing_app";return {};}
            if(!checkPending(m)){
                phase="good";if(!save()){error="journal";return {};}
                std::remove(pending().c_str());error="pending";return executable();
            }
            if(exists(backup) && std::remove(backup.c_str())){error="backup";return {};}
            phase="swapping";if(!save()){error="journal";return {};}
        }
        // O journal antecede cada rename: retoma inclusive após queda entre os dois movimentos.
        if(exists(pending())) {
            if(!checkPending(m)){error="hash";return exists(backup) && restore(error)?executable():"";}
            if(!exists(backup)) {
                if(std::rename(executable().c_str(),backup.c_str())){error="backup";return {};}
            } else if(exists(executable())) {error="ambiguous_swap";return {};}
            if(std::rename(pending().c_str(),executable().c_str())){restore(error);return error.empty()?executable():"";}
        }
        if(!exists(backup) || fileHash(executable())!=m.hash){error="swap_hash";return restore(error)?executable():"";}
        phase="trial";if(!save()){restore(error);return error.empty()?executable():"";}
    }
    if(!exists(executable())){error="missing_app";return {};}
    return executable();
}
bool AppUpdate::failed(string& error) {return valid && phase=="trial" && restore(error);}
bool AppUpdate::confirm(int runningBuild) {
    if(!valid || phase!="trial")return false;
    Manifest m;if(!decodeManifest(bytes,m) || m.build!=runningBuild || fileHash(executable())!=m.hash)return false;
    phase="good";return save();
}
}
