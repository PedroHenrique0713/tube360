// Player e sender Cast reais; só a mídia HTTP é substituída por arquivos locais.
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

namespace {
std::string videoBytes, audioBytes;
class Media final : public yt::Transport {
    const std::atomic_bool* stop;
    std::atomic_bool aborted{false};
public:
    explicit Media(const std::atomic_bool* cancel) : stop(cancel) {}
    bool cancelled() const override { return aborted || (stop && *stop); }
    void cancel() override { aborted = true; }
    yt::Response request(const std::string&, const std::string&, bool) override { return {0, {}, "unexpected_request"}; }
    yt::Response send(const yt::Request& r) override {
        if (cancelled()) return {0, {}, "cancelled"};
        const auto& bytes = r.url == "https://fixture.googlevideo.com/video" ? videoBytes : audioBytes;
        if (r.url != "https://fixture.googlevideo.com/video" && r.url != "https://fixture.googlevideo.com/audio") return {404};
        long long begin = 0, end = 0;
        if (r.headers.size() != 1 || std::sscanf(r.headers[0].c_str(), "Range: bytes=%lld-%lld", &begin, &end) != 2 || begin < 0) return {400};
        if (begin >= static_cast<long long>(bytes.size())) return {416};
        end = std::min(end, static_cast<long long>(bytes.size())-1);
        return {206, bytes.substr(begin, end-begin+1), {}, "bytes " + std::to_string(begin) + "-" + std::to_string(end) + "/" + std::to_string(bytes.size())};
    }
};
std::string read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
}
namespace yt {
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) { return std::make_unique<Media>(cancel); }
}

int main(int argc, char** argv) {
    if (argc != 3 || !SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS)) return 1;
    videoBytes = read(std::string(argv[2]) + "/video.mp4");
    audioBytes = read(std::string(argv[2]) + "/audio.m4a");
    if (videoBytes.empty() || audioBytes.empty()) return 2;
    net::serverEnabled = false;
    player::Player p;
    p.mu = SDL_CreateMutex(); p.cv = SDL_CreateCondition(); p.nmu = SDL_CreateMutex();
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    p.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!p.audio) return 3;
    p.directSpeaker.init(std::string(argv[1]) + "/");
    p.media.id = "F53fixture0"; p.media.status = "OK"; p.media.duration = 60; p.media.ttl = 3600;
    p.media.video = {"https://fixture.googlevideo.com/video", 136, 90, 30, 0, 160};
    p.media.audio = {"https://fixture.googlevideo.com/audio", 140}; p.media.speakerAudio = p.media.audio;
    p.cachedQuality = 720; p.cachedVideoMode = playback::AUTO; p.cachedAudio = "original";
    p.validUntil = SDL_GetTicks() + 120000;
    uint64_t began = SDL_GetTicks();
    p.start({"127.0.0.1", 1}, p.media.id, 0, "F53", 60, 720, true);
    unsigned notices = 0;
    std::string line;
    while (std::getline(std::cin, line) && line != "quit") {
        if (line != "sample") return 4;
        if (auto* frame = p.take()) p.recycle(frame);
        if (p.speakerFallback.exchange(false)) ++notices;
        auto status = p.directSpeaker.sample();
        nest::Object out{
            {"elapsedMs", nest::J(double(SDL_GetTicks()-began))},
            {"route", nest::J(double(p.output.load()))}, {"audible", nest::J(p.audible.load())},
            {"gain", nest::J(double(SDL_GetAudioStreamGain(p.audio)))},
            {"notices", nest::J(double(notices))},
            {"noticeText", nest::J(std::string(i18n::get(i18n::Lang::PT, i18n::Key::SpeakerFallback)))},
            {"frames", nest::J(double(p.shownFrames))}, {"pcm", nest::J(double(SDL_GetAtomicInt(&p.pushed)))},
            {"state", nest::J(double(p.state.load()))},
            {"generation", nest::J(double(status.generation))}, {"speakerState", nest::J(double(status.state))},
            {"quiet", nest::J(status.quiet)}, {"clockReady", nest::J(status.clockReady)}
        };
        std::cout << nest::J(out).serialize() << std::endl;
    }
    p.halt(); p.directSpeaker.shutdown();
    SDL_DestroyAudioStream(p.audio);
    SDL_DestroyCondition(p.cv); SDL_DestroyMutex(p.mu); SDL_DestroyMutex(p.nmu);
    SDL_Quit();
}
