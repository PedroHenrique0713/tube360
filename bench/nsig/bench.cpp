// Mede o caminho do desafio `n`: reduzir o JS do player, avaliar no QuickJS e transformar valores.
//   nsig-bench <player.js> <n> [<n>...]    (sai com 0 só se todos os `n` viraram um valor novo)
#include "yt/nsig.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#ifdef __linux__
#include <sys/resource.h>
#endif
#ifdef YT_XBOX
#include <pthread.h>
#include <xsdk/log.h>
#define REPORT xsdk_log_write
#else
#define REPORT(...) do { std::printf(__VA_ARGS__); std::puts(""); } while (0)
#endif

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
static long peakKb() {
#ifdef __linux__
    rusage u{}; getrusage(RUSAGE_SELF, &u); return u.ru_maxrss;
#else
    return -1;
#endif
}
static int bench_main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s player.js n...", argv[0]); return 2; }
    std::string js;
    if (FILE* f = std::fopen(argv[1], "rb")) {
        char block[1 << 16]; size_t got;
        while ((got = std::fread(block, 1, sizeof block, f)) > 0) js.append(block, got);
        std::fclose(f);
    }
    if (js.empty()) { REPORT("cannot read %s", argv[1]); return 2; }
    REPORT("player_bytes %zu rss_kb_start %ld", js.size(), peakKb());
    yt::nsig::Reduced red;
    auto t = Clock::now();
    auto e = yt::nsig::reduce(js, red);
    REPORT("reduce_ms %.1f (tokenize %.1f split %.1f closure+emit %.1f) error=%s entry=%s tokens=%zu units=%zu kept=%zu program_bytes=%zu rss_kb %ld", since(t), red.tokenizeMs, red.splitMs, red.closureMs, e.c_str(),
        red.entry.c_str(), red.tokens, red.units, red.kept, red.program.size(), peakKb());
    if (!e.empty()) return 1;
    yt::nsig::Solver solver;
    t = Clock::now();
    e = solver.load(red);
    REPORT("load_ms %.1f error=%s engine_heap_bytes %zu rss_kb %ld", since(t), e.c_str(), solver.memoryBytes(), peakKb());
    if (!e.empty()) return 1;
    {   // arranque a quente: bytecode do QuickJS guardado de uma abertura anterior
        yt::nsig::Solver compiler; std::string code;
        t = Clock::now(); e = compiler.compile(red, code);
        REPORT("compile_ms %.1f error=%s bytecode_bytes %zu", since(t), e.c_str(), code.size());
        yt::nsig::Solver warm; t = Clock::now(); e = warm.loadBytecode(code);
        std::string out; auto e2 = warm.solve(argv[2], out);
        REPORT("bytecode_load_ms %.1f error=%s first_solve %s %s", since(t), e.c_str(), out.c_str(), e2.c_str());
    }
    int bad = 0;
    for (int i = 2; i < argc; ++i) {
        std::string out; t = Clock::now();
        e = solver.solve(argv[i], out);
        REPORT("solve_ms %.2f %s -> %s %s", since(t), argv[i], out.c_str(), e.c_str());
        bad += !e.empty();
    }
    REPORT("engine_heap_bytes_after %zu rss_kb %ld", solver.memoryBytes(), peakKb());
    return bad ? 1 : 0;
}

#ifdef YT_XBOX
// No console: lê game:\\tv-player.js (o JS do player TV, ao lado do XEX) e transforma dois `n` conhecidos. O resultado vai para o log.
static void* run(void*) {
    char a[] = "nsig-bench", f[] = "game:\\tv-player.js", n1[] = "abcdefghijklmnopqrstuvwxyz0123", n2[] = "lyaKaXHEsI_nkDGLu";
    char* av[] = {a, f, n1, n2};
    static int rc; rc = bench_main(4, av); return &rc;
}
int main() {
    pthread_t th; pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (pthread_create(&th, &attr, run, nullptr) != 0) return 1;
    void* r = nullptr; pthread_join(th, &r);
    return r ? *static_cast<int*>(r) : 1;
}
#else
int main(int argc, char** argv) { return bench_main(argc, argv); }
#endif
