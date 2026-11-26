// Player, conta TV, seleção de formatos e demuxer reais; só o transporte HTTP é substituído.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION
#define LOG(...) SDL_Log(__VA_ARGS__)
#include "player.hpp"
#include "i18n.hpp"
#include <fstream>
#include <iostream>

static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
static std::string videoBytes, failingTrack;
static int failureStatus = 403, players = 0, videos = 0, audios = 0, playlists = 0;
static bool liveFixture = false;

struct Http final : yt::Transport {
    const std::atomic_bool* stopped;
    bool aborted = false;
    explicit Http(const std::atomic_bool* flag = nullptr) : stopped(flag) {}
    bool cancelled() const override { return aborted || (stopped && *stopped); }
    void cancel() override { aborted = true; }
    yt::Response request(const std::string& url, const std::string& body, bool form) override {
        yt::Request r; r.url = url; r.body = body; r.form = form; return send(r);
    }
    yt::Response send(const yt::Request& r) override {
        if (cancelled()) return {0, {}, "cancelled"};
        if (r.url.find("/player?") != std::string::npos) {
            ++players;
            check(r.body.find("TVHTML5") != std::string::npos, "TV account player identity");
            check(std::find(r.headers.begin(), r.headers.end(), "Authorization: Bearer fake-a") != r.headers.end(), "authenticated player request");
            return {200, std::string(R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"member00001","lengthSeconds":"30","isLive":)") + (liveFixture ? "true" : "false") + R"(},"streamingData":{"expiresInSeconds":"3600",)" +
                (liveFixture ? R"("hlsManifestUrl":"https://fixture.googlevideo.com/live",)" : "") +
                R"("adaptiveFormats":[{"itag":136,"width":160,"height":90,"fps":30,"mimeType":"video/mp4; codecs=\"avc1.64001f\"","url":"https://fixture.googlevideo.com/video?n=unsolved"},{"itag":140,"bitrate":128000,"mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","url":"https://fixture.googlevideo.com/audio?n=unsolved"}]}})"};
        }
        if (r.url.find("/live") != std::string::npos) { ++playlists; return {failureStatus}; }
        bool video = r.url.find("/video?") != std::string::npos;
        check(video || r.url.find("/audio?") != std::string::npos, "only fixture media requested");
        if (video) ++videos; else ++audios;
        if ((video ? "video" : "audio") == failingTrack) return {failureStatus};
        check(video, "successful fixture is video only");
        long long begin = 0, end = 0;
        check(r.headers.size() == 1 && std::sscanf(r.headers[0].c_str(), "Range: bytes=%lld-%lld", &begin, &end) == 2, "bounded Range");
        if (begin >= static_cast<long long>(videoBytes.size())) return {416};
        end = std::min(end, static_cast<long long>(videoBytes.size())-1);
        return {206, videoBytes.substr(begin, end-begin+1), {}, "bytes " + std::to_string(begin) + "-" + std::to_string(end) + "/" + std::to_string(videoBytes.size())};
    }
};
namespace yt {
std::unique_ptr<Transport> https(const std::atomic_bool* stop) { return std::make_unique<Http>(stop); }
}

static void run(const std::string& directory, bool members, const char* track, int status, bool hls = false, int mode = playback::AUTO) {
    failingTrack = track; failureStatus = status; liveFixture = hls;
    players = videos = audios = playlists = 0;
    player::Player p;
    p.videoMode = mode;
    p.mu = SDL_CreateMutex(); p.cv = SDL_CreateCondition(); p.nmu = SDL_CreateMutex();
    p.direct = p.original = true; p.memberVideo = members; p.id = "member00001";
    p.state = player::BUFFERING; p.output = player::Player::LOCAL;
    p.openedMs = SDL_GetTicks();
    Http accountTransport;
    if (members) {
        p.memberAccount = std::make_shared<yt::ConsoleAccount>(accountTransport, directory + "/account.json");
        p.memberAccount->select(true);
        check(p.memberAccount->active(), "isolated account active");
    } else {
        // Mesmo formato já resolvido, preservando o caminho comum sem conta.
        yt::Client client(accountTransport);
        auto reply = accountTransport.send(yt::Request{
            .url="https://youtubei.googleapis.com/youtubei/v1/player?key=fixture",
            .body="TVHTML5", .headers={"Authorization: Bearer fake-a"}});
        p.media = client.player(p.id, 720, nullptr, 0, "original", reply.body);
        players = 0;
        p.cachedQuality = 720; p.cachedVideoMode = mode; p.cachedAudio = "original";
        p.validUntil = SDL_GetTicks() + 60000;
    }
    if (failingTrack == "audio") {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
        p.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        check(p.audio, "dummy audio stream");
    }
    p.decodeOriginal();
    bool terminal = members && status == 403;
    if (terminal && (p.state != player::FAILED || p.error != "members_unsupported"))
        std::cerr << "member HTTP=" << status << " mode=" << playback::modeName(mode) << " state=" << p.state << " error=" << p.error << '\n';
    if (terminal) {
        check(p.state == player::FAILED && p.error == "members_unsupported", "member 403 is terminal and specific");
        check(!p.renew && !p.retryConverted, "no URL renewal or server fallback");
        int requests = players + videos + audios + playlists;
        for (int i = 0; i < 100; ++i) check(!p.take(), "failed player has no frame");
        check(requests == players + videos + audios + playlists && players == 1, "first failure makes no repeated player/media requests");
    } else if (hls && playback::manual(mode) && status != 403) {
        check(p.state == player::FAILED && p.error == "manual_quality_unavailable", "non-403 fixed HLS failure keeps quality error");
        check(!p.renew && !p.retryConverted, "non-403 fixed HLS failure does not renew");
    } else {
        check(p.error != "members_unsupported", "ordinary error is preserved");
        check(status == 403 ? p.renew.load() : p.retryConverted.load(), "existing renewal/fallback retained");
    }
    check((hls ? playlists : failingTrack == "video" ? videos : audios) == 1, "one failed media request");
    std::cout << "PASS " << (members ? "member" : "ordinary") << ' ' << (hls ? "HLS" : track)
              << " mode=" << playback::modeName(mode) << " HTTP=" << status << " players=" << players << " video=" << videos << " audio=" << audios
              << " playlists=" << playlists << " renew=" << p.renew << " fallback=" << p.retryConverted << '\n';
    p.halt();
    if (p.audio) SDL_DestroyAudioStream(p.audio);
    SDL_DestroyMutex(p.mu); SDL_DestroyCondition(p.cv); SDL_DestroyMutex(p.nmu);
}
int main(int argc, char** argv) {
    check(argc == 2 && SDL_Init(SDL_INIT_AUDIO), "fixture directory and SDL audio required");
    std::ifstream input(std::string(argv[1]) + "/video.mp4", std::ios::binary);
    videoBytes.assign(std::istreambuf_iterator<char>(input), {});
    check(!videoBytes.empty(), "real MP4 fixture exists");
    net::serverEnabled = false;
    run(argv[1], true, "video", 403);
    run(argv[1], true, "audio", 403);
    run(argv[1], true, "video", 403, true);
    for (int mode : {playback::FIXED480, playback::FIXED360, playback::FIXED240, playback::FIXED144}) {
        run(argv[1], true, "video", 403, true, mode);
        run(argv[1], false, "video", 403, true, mode);
        run(argv[1], true, "video", 404, true, mode);
    }
    run(argv[1], false, "video", 403);
    run(argv[1], false, "audio", 403);
    run(argv[1], true, "video", 404);
    check(i18n::playerError("members_unsupported") == i18n::Key::MembersXboxUnavailable, "UI uses specific member message");
    for (auto lang : {i18n::Lang::EN, i18n::Lang::PT, i18n::Lang::ES, i18n::Lang::AR})
        check(std::strlen(i18n::get(lang, i18n::Key::MembersXboxUnavailable)) > 0, "translated message exists");
    std::cout << "PASS member message EN/PT/ES/AR\n";
    SDL_Quit();
}
