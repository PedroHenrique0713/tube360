#include "h264.hpp"
#include <cstdio>
#include <cstdlib>

int main() {
    for (double rate : {24.,25.,29.97,30.,50.,59.94,60.}) {
        int frames=int(std::lround(rate*10)), kept=0;
        for (int i=0;i<frames;++i) if (!h264::dropAt30(i,rate)) ++kept;
        int expected=int(std::lround(std::min(rate,30.)*10));
        if (kept!=expected) { std::fprintf(stderr,"%.3f fps: %d, expected %d\n",rate,kept,expected); return 1; }
        if (rate>59) for (int i=0;i<frames;++i)
            if (h264::dropAt30(i,rate)!=(i%2!=0)) return 1;
        std::printf("%.3f fps: %d / %d frames presented in 10 source seconds\n",rate,kept,frames);
    }
}
