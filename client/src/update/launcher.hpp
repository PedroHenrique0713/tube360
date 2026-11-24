#pragma once
#include "app.hpp"
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
};
int launchApp(const string& directory,const string& key,LauncherHost&);
}
