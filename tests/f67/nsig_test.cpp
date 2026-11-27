// Serviço do desafio `n`: URL, versão do player, solver com um player sintético, cache por valor, bytecode em disco,
// chave de desligamento e os códigos de falha.   f67-nsig-test
#include "yt/nsig_service.hpp"
#include "update/rules.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
using namespace yt;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL %s:%d %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::puts(""); } } while (0)

// Um player mínimo na forma que o redutor espera: IIFE, construtor de URL com `.set("alr","yes");` e uma transformação de `n`.
static string fakePlayer(const string& salt) {
    string js = "(function(g){\n\"use strict\";\nvar CFG={signatureTimestamp:20779,x:1};\n"
        "function U(u){this.m={};this.u=u}\n"
        "U.prototype.set=function(k,v){this.m[k]=v};\nU.prototype.get=function(k){return this.m[k]};\nU.prototype.clone=function(){return this};\n"
        "U.prototype.t=function(){var n=this.m.n;if(n)this.m.n=n.split(\"\").reverse().join(\"\")+\"" + salt + "\"};\n"
        "function B(a,b,c){var x=new U(a);x.set(\"alr\",\"yes\");return x}\n"
        "var PAD=\"" + string(120000, 'p') + "\";\nconsole_noise(1);\n}).call(this);\n";
    return js;
}
struct Fake final : Transport {
    string player; unsigned iframe = 0, downloads = 0; int playerStatus = 200; string id = "deadbeef";
    Response request(const string& url, const string&, bool) override { Request r; r.url = url; return send(r); }
    Response send(const Request& r) override {
        Response res; res.status = 200;
        if (r.url.find("/iframe_api") != string::npos) { ++iframe; res.body = "var a='https:\\/\\/www.youtube.com\\/s\\/player\\/" + id + "\\/www-widgetapi.vflset\\/www-widgetapi.js'"; }
        else if (r.url.find("tv-player-ias") != string::npos) { ++downloads; res.status = playerStatus; if (playerStatus == 200) res.body = player; }
        else res.status = 404;
        return res;
    }
};

int main() {
    // partes puras
    CHECK(nsig::nValue("https://r.googlevideo.com/videoplayback?a=1&n=AbC_-9&c=TVHTML5") == "AbC_-9", "n in the middle");
    CHECK(nsig::nValue("https://r.googlevideo.com/videoplayback?n=Zz&a=1") == "Zz", "n first");
    CHECK(nsig::nValue("https://r.googlevideo.com/videoplayback?a=1&xn=Q") == "", "xn is not n");
    CHECK(nsig::withN("https://h/v?a=1&n=old&c=2", "new") == "https://h/v?a=1&n=new&c=2", "replace in the middle");
    CHECK(nsig::withN("https://h/v?a=1&n=old", "new") == "https://h/v?a=1&n=new", "replace at the end");
    CHECK(nsig::withN("https://h/v?a=1", "new") == "https://h/v?a=1", "no n stays equal");
    CHECK(nsig::playerIdFrom("x s\\/player\\/5ec65609\\/www y") == "5ec65609" && nsig::playerIdFrom("nada").empty() && nsig::playerIdFrom("s\\/player\\/zz\\/").empty(), "player id");

    auto rules = updates::rules();
    CHECK(rules->n("nsig") == 1 && rules->s("tvPlayerPath").find("{id}") != string::npos, "default rules carry the switch and the TV player path");
    auto dir = std::filesystem::temp_directory_path() / ("f67nsig" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const string base = (dir / "cache").string();

    Fake net; net.player = fakePlayer("Z");
    string a = "https://r1.googlevideo.com/videoplayback?live=1&n=abcdef&c=TVHTML5", b = "https://r1.googlevideo.com/videoplayback?n=abcdef&itag=140", c = "https://r1.googlevideo.com/videoplayback?n=ghijkl&itag=18", none = "https://r1.googlevideo.com/videoplayback?itag=1";
    nsig::resetForTest();
    auto e = nsig::rewrite(net, *rules, base, {&a, &b, &c, &none});
    CHECK(e.empty(), "rewrite failed: %s", e.c_str());
    CHECK(nsig::nValue(a) == "fedcbaZ" && nsig::nValue(b) == "fedcbaZ" && nsig::nValue(c) == "lkjihgZ", "solved values %s %s %s", nsig::nValue(a).c_str(), nsig::nValue(b).c_str(), nsig::nValue(c).c_str());
    CHECK(none == "https://r1.googlevideo.com/videoplayback?itag=1" && a.find("&live=1") == string::npos && a.find("live=1") != string::npos, "other parameters untouched");
    auto k = nsig::counters();
    CHECK(k.downloads == 1 && k.solves == 2, "one download, two distinct values solved (%u/%u)", k.downloads, k.solves);
    // cache por valor
    string again = "https://r1.googlevideo.com/videoplayback?n=abcdef"; nsig::rewrite(net, *rules, base, {&again});
    CHECK(nsig::counters().memoHits == 1 && nsig::counters().solves == 2 && nsig::nValue(again) == "fedcbaZ", "memo by n value");
    CHECK(nsig::timestamp(net, *rules, base) == 20779, "signatureTimestamp of the TV player");
    CHECK(net.iframe == 1, "the player version is asked once, then trusted for hours (%u)", net.iframe);

    // bytecode em disco: depois de esquecer a memória, nada de baixar de novo
    nsig::resetForTest();
    CHECK(nsig::timestamp(net, *rules, base) == 20779 && nsig::counters().bytecodeLoads == 0 && net.downloads == 1, "the timestamp comes from the index: no engine loaded, no download");
    string d = "https://r1.googlevideo.com/videoplayback?n=abcdef";
    e = nsig::rewrite(net, *rules, base, {&d});
    CHECK(e.empty() && nsig::nValue(d) == "fedcbaZ", "bytecode cache solve: %s", e.c_str());
    CHECK(net.downloads == 1 && nsig::counters().bytecodeLoads == 1, "second start uses the bytecode, no download (%u downloads)", net.downloads);
    CHECK(nsig::counters().bytecodeLoads == 1, "engine loaded once for the solve");

    // versão nova do player: baixa de novo e troca o bytecode
    nsig::resetForTest(); net.id = "cafef00d"; net.player = fakePlayer("Y");
    string f = "https://r1.googlevideo.com/videoplayback?n=abcdef";
    e = nsig::rewrite(net, *rules, base, {&f});
    CHECK(e.empty() && nsig::nValue(f) == "fedcbaY" && net.downloads == 2, "new player version re-downloads (%s %u)", e.c_str(), net.downloads);
    int files = 0; for (auto& p : std::filesystem::directory_iterator(dir)) if (p.path().extension() == ".bc") ++files;
    CHECK(files == 1, "old bytecode removed (%d .bc)", files);

    // chave de desligamento, falhas e o que não pode mudar
    updates::Rules off = *rules; off.limits["nsig"] = 0;
    nsig::resetForTest();
    string g = "https://r1.googlevideo.com/videoplayback?n=abcdef";
    CHECK(nsig::rewrite(net, off, base, {&g}) == "disabled" && g == "https://r1.googlevideo.com/videoplayback?n=abcdef", "switch off leaves the URL alone");
    Fake broken; broken.playerStatus = 403; nsig::resetForTest();
    auto cold = (dir / "cold").string();
    string h = "https://r1.googlevideo.com/videoplayback?n=abcdef";
    e = nsig::rewrite(broken, *rules, cold, {&h});
    CHECK(e == "download_403" && h == "https://r1.googlevideo.com/videoplayback?n=abcdef", "download failure code %s", e.c_str());
    Fake noapi; noapi.id = "xx"; nsig::resetForTest();
    CHECK(nsig::rewrite(noapi, *rules, (dir / "none").string(), {&h}) == "player_id", "bad player id");
    Fake junk; junk.player = string(150000, 'j'); nsig::resetForTest();
    e = nsig::rewrite(junk, *rules, (dir / "junk").string(), {&h});
    CHECK(e.compare(0, 6, "reduce") == 0, "unreadable player is a reduce failure (%s)", e.c_str());
    string noN = "https://r1.googlevideo.com/videoplayback?itag=1"; Fake never; nsig::resetForTest();
    CHECK(nsig::rewrite(never, *rules, base, {&noN}).empty() && never.iframe == 0, "URLs without n do not even touch the network");

    // Client::player() no caminho da conta: o `n` sai resolvido, a live por segmentos é marcada, e a falha deixa o vídeo sem URL.
    {
        auto formats = [](const string& n) {
            string url = "https://r1.googlevideo.com/videoplayback?live=1\\u0026n=" + n;
            return string("{\"playabilityStatus\":{\"status\":\"OK\"},\"videoDetails\":{\"videoId\":\"abcdefghijk\",\"title\":\"t\",\"lengthSeconds\":\"0\",\"isLive\":true},")
                + "\"streamingData\":{\"expiresInSeconds\":\"21540\",\"adaptiveFormats\":[{\"itag\":136,\"url\":\"" + url + "\",\"mimeType\":\"video/mp4; codecs=\\\"avc1.4D401F\\\"\",\"height\":720,\"width\":1280,\"fps\":30,\"bitrate\":2000000},"
                + "{\"itag\":140,\"url\":\"" + url + "\",\"mimeType\":\"audio/mp4; codecs=\\\"mp4a.40.2\\\"\",\"bitrate\":128000}]}}";
        };
        Fake ok; ok.player = fakePlayer("Z"); nsig::resetForTest();
        Client c(ok, (dir / "client").string());
        auto v = c.player("abcdefghijk", 720, nullptr, 0, "original", formats("abcdef"));
        CHECK(v.status == "OK" && v.liveSeq && v.nsigSolved && v.hls.empty(), "live of members by segments (%s liveSeq=%d solved=%d)", v.status.c_str(), int(v.liveSeq), int(v.nsigSolved));
        CHECK(nsig::nValue(v.video.url) == "fedcbaZ" && nsig::nValue(v.audio.url) == "fedcbaZ" && v.speakerAudio.url.empty(), "n solved on both tracks, no direct audio for the Nest");
        CHECK(c.nsigTimestamp() == 20779, "client exposes the TV signatureTimestamp");
        Fake bad; bad.playerStatus = 403; nsig::resetForTest();
        Client c2(bad, (dir / "client2").string());
        auto w = c2.player("abcdefghijk", 720, nullptr, 0, "original", formats("abcdef"));
        CHECK(w.status == "OK" && w.video.url.empty() && w.audio.url.empty() && w.hls.empty() && !w.liveSeq && !w.nsigSolved && w.reason == "nsig:download_403",
              "failure leaves no URL, so the player shows the members message (%s)", w.reason.c_str());
    }

    std::filesystem::remove_all(dir);
    std::puts(failures ? "RESULT FAILED" : "RESULT OK");
    return failures ? 1 : 0;
}
