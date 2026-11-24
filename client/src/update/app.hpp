#pragma once
#include "common.hpp"
#ifndef TUBE360_BUILD_ID
#define TUBE360_BUILD_ID 1
#endif
#ifndef TUBE360_VERSION
#define TUBE360_VERSION "0.3.0-dev"
#endif
namespace updates {
inline constexpr int buildId=TUBE360_BUILD_ID;
inline constexpr const char* appVersion=TUBE360_VERSION;
struct Manifest {
    int build=0, size=0;
    string version,url,hash,notesEn,notesPt;
};
bool decodeManifest(const string&,Manifest&);
class AppUpdate {
    string dir,key,bytes,sig,phase;
    int highest=buildId;
    bool valid=true;
    bool save();
    bool checkPending(const Manifest&);
    bool restore(string&);
public:
    AppUpdate(string directory,string publicKey);
    string pending() const {return dir+"tube360.next.xex";}
    string executable() const {return dir+"tube360.xex";}
    int highWater() const {return highest;}
    bool stagedManifest(Manifest& m) const {return valid && phase=="staged" && decodeManifest(bytes,m);}
    bool stage(const string& manifest,const string& signature,string& error);
    // Só o launcher chama prepare. Uma tentativa sem confirmação volta ao backup no próximo início.
    string prepare(string& error);
    bool failed(string& error);
    bool confirm(int runningBuild);
};
}
