#include "yt/search.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv) {
    if (argc!=2 || std::strcmp(argv[1],"--live")) {
        std::fprintf(stderr,"Opt in with --live for ONE anonymous public suggestion request.\n"); return 2;
    }
    std::atomic_bool cancel{false}; auto net=yt::https(&cancel);
    auto values=yt::search::fetch(*net,"astronomia","pt");
    std::printf("production HTTPS suggestion request: %u results (no cookies/account)\n",unsigned(values.size()));
    return values.empty() ? 1 : 0;
}
