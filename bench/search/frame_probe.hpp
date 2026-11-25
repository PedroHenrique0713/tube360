#pragma once
#include <array>
namespace searchbench {
struct Frames {
    const char* path=std::getenv("XT_SEARCH_METRICS");
    std::array<unsigned,2001> histogram{};
    uint64_t previous=0;
    unsigned count=0, overBudget=0;
    double sum=0, maximum=0;
    bool wasSearch=false;
    void tick(bool search) {
        if (!path) return;
        auto now=SDL_GetPerformanceCounter();
        if (previous && search && wasSearch) {
            double ms=1000.*(now-previous)/SDL_GetPerformanceFrequency();
            ++histogram[std::min(2000,int(ms*10))]; ++count; sum+=ms; maximum=std::max(maximum,ms);
            if (ms>1000./30) ++overBudget;
        }
        previous=now; wasSearch=search;
    }
    double percentile(double fraction) const {
        unsigned total=0;
        for (size_t i=0;i<histogram.size();++i) { total+=histogram[i]; if (total>=count*fraction) return (i+1)/10.; }
        return 0;
    }
    ~Frames() {
        if (!path || !count) return;
        if (auto* f=std::fopen(path,"w")) {
            std::fprintf(f,"{\"frames\":%u,\"mean_ms\":%.4f,\"p95_ms\":%.1f,\"p99_ms\":%.1f,\"max_ms\":%.4f,\"over_33ms\":%u}\n",count,sum/count,percentile(.95),percentile(.99),maximum,overBudget);
            std::fclose(f);
        }
    }
};
}
