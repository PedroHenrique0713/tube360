#pragma once
#include "app.hpp"
#include "i18n.hpp"
#include "launcher_reports.hpp"
#include <cstdio>

namespace updates {
class LauncherLog {
    FILE* file;
    Trace network;
    uint64_t started;
public:
    LauncherLog(const string& directory,Trace sink = {});
    ~LauncherLog();
    void line(const string& message);
};
struct LauncherHost {
    virtual ~LauncherHost() = default;
    virtual void log(const string&) = 0;
    virtual bool screen() = 0;
    virtual bool closeScreen() = 0;
    virtual int launch(const string&) = 0;
    virtual string preflight(const string&) {return {};}
    virtual yt::Response postReport(const yt::Request&) {return {};}
    virtual bool randomBytes(unsigned char*,size_t) {return false;}
    virtual void reportCode(const string&,unsigned) {}
    virtual void fail(i18n::Key,const string& detail,i18n::Key) {log("startup failure: "+detail);}
};
int launchApp(const string& directory,const string& key,LauncherHost&);
}
