#pragma once
#include "yt/cast.hpp"
#include "yt/thread.hpp"
#include "picojson.h"
#include <atomic>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace nest {
using J = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
inline const J& field(const J& j, const char* key) {
    static const J empty;
    return j.is<Object>() ? j.get(key) : empty;
}
inline std::string text(const J& j, const char* key) {
    const auto& v = field(j,key);
    return v.is<std::string>() ? v.get<std::string>() : "";
}
inline double number(const J& j, const char* key, double fallback = 0) {
    const auto& v = field(j,key);
    return v.is<double>() && std::isfinite(v.get<double>()) ? v.get<double>() : fallback;
}
inline bool parse(const std::string& s, J& j) {
    int depth = 0;
    bool quoted = false, escaped = false;
    if (s.size() > 60000) return false;
    for (char c : s) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if ((c == '{' || c == '[') && ++depth > 24) return false;
        else if ((c == '}' || c == ']') && --depth < 0) return false;
    }
    return !quoted && depth == 0 && picojson::parse(j,s).empty() && j.is<Object>();
}
struct Config {
    std::string host, name = "Nest";
    int port = 8009;
    bool load(const std::string& dir) {
        FILE* f = std::fopen((dir + "nest.txt").c_str(),"r");
        if (!f) return false;
        char address[254]{}, label[128]{};
        if (std::fgets(address,sizeof(address),f)) {
            std::string line = address;
            auto end = line.find_first_of(" \r\n\t");
            host = line.substr(0,end);
            if (end != std::string::npos) {
                int p = 0;
                if (std::sscanf(line.c_str()+end," %d",&p) == 1) port = p;
            }
        }
        if (std::fgets(label,sizeof(label),f)) {
            name = label; name.erase(name.find_last_not_of("\r\n")+1);
            if (name.empty()) name = "Nest";
        }
        std::fclose(f);
        if (port < 1 || port > 65535 || host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos) host.clear();
        return !host.empty();
    }
};
enum State { UNKNOWN, LOADING, PLAYING, PAUSED, BUFFERING, IDLE, ERROR, STALLED };
struct Status {
    uint64_t generation = 0;
    State state = UNKNOWN;
    double position = 0, updated = 0, rate = 1;
    bool quiet = true, connected = false;
    double volume = -1;
    bool muted = false;
};
// O worker sobrevive ao vídeo. Uma saída durante queda de rede continua tentando STOP na navegação.
class Speaker {
    struct Intent {
        uint64_t generation = 0, command = 0;
        std::string url, key;
        double position = 0, duration = 0;
        double volume = -1, fade = 1;
        bool muted = false;
        bool active = false, paused = false;
    } intent;
    Config config;
    Status status;
    SDL_Mutex* mutex = nullptr;
    SDL_Thread* thread = nullptr;
    std::atomic_bool quit{false};
    std::string ownerPath;
    void publish(const Status& value) {
        SDL_LockMutex(mutex);
        if (value.generation == intent.generation) status = value;
        SDL_UnlockMutex(mutex);
    }
    Intent wanted() { SDL_LockMutex(mutex); auto value = intent; SDL_UnlockMutex(mutex); return value; }
public:
    ~Speaker() { shutdown(); }
    const Config& settings() const { return config; }
    bool configured() const { return !config.host.empty(); }
    void init(const std::string& dir) {
        ownerPath = dir + "nest-owner.json";
        config.load(dir);
        mutex = SDL_CreateMutex();
        bool pending = false;
        if (FILE* file = std::fopen(ownerPath.c_str(),"r")) { pending = true; status.quiet = false; std::fclose(file); }
        if (configured() || pending) thread = yt::worker([](void* p) { static_cast<Speaker*>(p)->run(); return 0; },"cast-v2",this);
    }
    Status sample() {
        if (!mutex) return {};
        SDL_LockMutex(mutex); auto value = status; SDL_UnlockMutex(mutex); return value;
    }
    uint64_t play(const std::string& url, const std::string& key, double position, double duration, bool autoplay = true) {
        SDL_LockMutex(mutex);
        ++intent.generation; intent.url = url; intent.key = key; intent.position = position;
        intent.duration = duration; intent.active = true; intent.paused = !autoplay;
        status.generation = intent.generation; status.state = LOADING; status.updated = 0;
        auto generation = intent.generation;
        SDL_UnlockMutex(mutex); return generation;
    }
    void pause(bool paused) {
        SDL_LockMutex(mutex); intent.paused = paused; ++intent.command; SDL_UnlockMutex(mutex);
    }
    void volume(double level, bool muted) {
        if (!mutex || !std::isfinite(level)) return;
        SDL_LockMutex(mutex); intent.volume = std::clamp(level,0.0,1.0); intent.muted = muted; SDL_UnlockMutex(mutex);
    }
    void fade(double gain) {
        if (!mutex || !std::isfinite(gain)) return;
        SDL_LockMutex(mutex); intent.fade = std::clamp(gain,0.0,1.0); SDL_UnlockMutex(mutex);
    }
    void release() {
        if (!mutex) return;
        SDL_LockMutex(mutex);
        if (intent.active) {
            intent.active = false; ++intent.generation;
            status.generation = intent.generation; status.state = UNKNOWN;
        }
        SDL_UnlockMutex(mutex);
    }
    bool waitQuiet(int timeoutMs) {
        uint64_t until = SDL_GetTicks()+timeoutMs;
        do { if (sample().quiet) return true; SDL_Delay(10); } while (SDL_GetTicks() < until);
        return sample().quiet;
    }
    void shutdown() {
        if (!mutex) return;
        release();
        if (thread) { waitQuiet(1800); quit = true; SDL_WaitThread(thread,nullptr); thread = nullptr; }
        SDL_DestroyMutex(mutex); mutex = nullptr;
    }
private:
    void run() {
        cast::Connection channel;
        Config endpoint = config;
        struct Sent { double time; uint64_t generation; };
        std::map<int,Sent> requests;
        Status view;
        std::string token, ownedUrl, ownedKey, transport, receiverSession;
        double mediaSession = 0, advanced = 0, previousPosition = -1;
        double baseVolume = -1, sentVolume = -1;
        bool baseMuted = false, sentMuted = false, fading = false;
        uint64_t volumeAt = 0;
        bool connected = false, receiverKnown = false, receiverPresent = false, mediaKnown = false;
        bool possible = false, pending = false, acknowledged = false;
        uint64_t generation = 0, command = 0, retryAt = 0, pollAt = 0, receiverAt = 0, pingAt = 0, heardAt = 0, actionAt = 0, stopAt = 0;
        int requestId = 0, barrier = 0, stopBarrier = 0;
        const auto sender = "sender-tube360-" + std::to_string(SDL_GetPerformanceCounter());
        auto journal = [&]() {
            Object record{{"token",J(token)},{"url",J(ownedUrl)},{"key",J(ownedKey)},
                          {"host",J(endpoint.host)},{"port",J(double(endpoint.port))}};
            auto tmp = ownerPath + ".tmp";
            FILE* file = std::fopen(tmp.c_str(),"w");
            if (!file) return false;
            auto bytes = J(record).serialize();
            bool ok = std::fwrite(bytes.data(),1,bytes.size(),file) == bytes.size();
            if (std::fclose(file)) ok = false;
            return ok && std::rename(tmp.c_str(),ownerPath.c_str()) == 0;
        };
        if (FILE* file = std::fopen(ownerPath.c_str(),"r")) {
            std::string bytes; char buffer[2048]; size_t count;
            while (bytes.size() <= 65536 && (count = std::fread(buffer,1,sizeof(buffer),file))) bytes.append(buffer,count);
            std::fclose(file); J saved;
            if (parse(bytes,saved)) {
                token = text(saved,"token"); ownedUrl = text(saved,"url"); ownedKey = text(saved,"key");
                auto host = text(saved,"host");
                double port = number(saved,"port",config.port);
                if (!host.empty() && port >= 1 && port <= 65535) { endpoint.host = host; endpoint.port = int(port); }
            }
            possible = !token.empty(); view.quiet = !possible;
        }
        auto send = [&](const std::string& destination, const char* space, Object message, bool tracked = true) {
            int id = 0;
            if (tracked) {
                id = ++requestId; message["requestId"] = J(double(id));
                requests[id] = {SDL_GetTicksNS()/1e6,generation};
                while (requests.size() > 64) requests.erase(requests.begin());
            }
            if (!channel.send(sender,destination,space,J(message).serialize())) connected = false;
            return id;
        };
        auto setVolume = [&](double level, bool muted) {
            send("receiver-0",cast::receiver,{{"type",J(std::string("SET_VOLUME"))},
                 {"volume",J(Object{{"level",J(level)},{"muted",J(muted)}})}});
            if (connected) { sentVolume = level; sentMuted = muted; }
        };
        auto quiet = [&]() {
            // O fade é temporário; preserva o volume escolhido pelo celular e o mute original do receptor.
            if (fading && baseVolume >= 0) {
                auto goal = wanted();
                setVolume(goal.volume >= 0 ? goal.volume : baseVolume,goal.volume >= 0 ? goal.muted : baseMuted);
                fading = false;
            }
            possible = false; view.quiet = true; mediaSession = 0; token.clear(); ownedUrl.clear(); ownedKey.clear();
            std::remove(ownerPath.c_str());
            if (endpoint.host != config.host || endpoint.port != config.port) {
                channel.close(); connected = false; endpoint = config; retryAt = 0;
                baseVolume = sentVolume = -1;
            }
        };
        auto mediaCommand = [&](const char* type) {
            return send(transport,cast::media,{{"type",J(std::string(type))},{"mediaSessionId",J(mediaSession)}});
        };
        while (!quit) {
            uint64_t now = SDL_GetTicks();
            Intent goal = wanted();
            view.volume = goal.volume >= 0 ? goal.volume : baseVolume;
            view.muted = goal.volume >= 0 ? goal.muted : baseMuted;
            if (goal.generation != generation) {
                generation = goal.generation; command = goal.command;
                pending = goal.active; acknowledged = false; actionAt = stopAt = 0;
                stopBarrier = requestId; view.generation = generation;
                view.state = goal.active ? LOADING : UNKNOWN; view.updated = 0;
            }
            if (!goal.active && !possible) {
                view.state = IDLE; view.quiet = true; view.connected = connected; publish(view); SDL_Delay(20); continue;
            }
            if (!connected) {
                view.connected = false; publish(view);
                if (now < retryAt) { SDL_Delay(20); continue; }
                channel.close(); connected = channel.open(endpoint.host,endpoint.port); retryAt = SDL_GetTicks()+500;
                if (!connected) continue;
                receiverKnown = receiverPresent = mediaKnown = false; transport.clear(); receiverSession.clear(); mediaSession = 0;
                sentVolume = -1;
                requests.clear(); pollAt = receiverAt = pingAt = 0; heardAt = now;
                send("receiver-0",cast::connection,{{"type",J(std::string("CONNECT"))},{"origin",J(Object{})}},false);
            }
            view.connected = connected;
            if (now >= pingAt) { send("receiver-0",cast::heartbeat,{{"type",J(std::string("PING"))}},false); pingAt = now+5000; }
            if (now-heardAt > 12000) { connected = false; continue; }
            if (now >= receiverAt) {
                send("receiver-0",cast::receiver,{{"type",J(std::string("GET_STATUS"))}}); receiverAt = now+2000;
            }
            if (receiverKnown && baseVolume >= 0 && (goal.volume >= 0 || goal.fade != 1 || fading) && now >= volumeAt) {
                double level = (goal.volume >= 0 ? goal.volume : baseVolume)*goal.fade;
                bool muted = goal.volume >= 0 ? goal.muted : baseMuted;
                if (std::fabs(level-sentVolume) > .001 || muted != sentMuted) {
                    setVolume(level,muted); volumeAt = now+120;
                    fading = goal.fade != 1;
                }
            }
            if (receiverKnown && !receiverPresent && !possible && goal.active && pending && now-actionAt > 1000) {
                send("receiver-0",cast::receiver,{{"type",J(std::string("LAUNCH"))},{"appId",J(std::string(cast::app))}}); actionAt = now;
            }
            if (!transport.empty() && now >= pollAt) {
                send(transport,cast::media,{{"type",J(std::string("GET_STATUS"))}}); pollAt = now+250;
            }
            if (!transport.empty() && mediaKnown && goal.active && pending) {
                bool seek = possible && mediaSession > 0 && ownedUrl == goal.url && ownedKey == goal.key &&
                            endpoint.host == config.host && endpoint.port == config.port;
                if (possible && !seek) {
                    if (mediaSession > 0 && now-stopAt >= 500) { mediaCommand("STOP"); stopAt = now; }
                } else {
                    if (!seek) {
                        SDL_LockMutex(mutex);
                        bool current = intent.active && intent.generation == generation;
                        if (current) { possible = true; status.quiet = false; view.quiet = false; }
                        SDL_UnlockMutex(mutex);
                        if (!current) continue;
                        token = sender+"-"+std::to_string(generation); ownedUrl = goal.url; ownedKey = goal.key;
                        if (!journal()) {
                            possible = false; view.quiet = true; view.state = ERROR; pending = false;
                            publish(view); SDL_Delay(20); continue;
                        }
                        Object resource{{"contentId",J(goal.url)},{"contentType",J(std::string("audio/mp4"))},
                                        {"streamType",J(std::string("BUFFERED"))},{"customData",J(Object{{"tube360",J(token)}})}};
                        if (goal.duration > 0) resource["duration"] = J(goal.duration);
                        barrier = send(transport,cast::media,{{"type",J(std::string("LOAD"))},{"media",J(resource)},
                                        {"currentTime",J(goal.position)},{"autoplay",J(!goal.paused)}});
                    } else {
                        barrier = send(transport,cast::media,{{"type",J(std::string("SEEK"))},{"mediaSessionId",J(mediaSession)},
                                       {"currentTime",J(goal.position)},{"resumeState",J(std::string(goal.paused ? "PLAYBACK_PAUSE" : "PLAYBACK_START"))}});
                    }
                    pending = false; acknowledged = false; actionAt = now; previousPosition = -1; advanced = now;
                }
            }
            if (goal.active && !pending && command != goal.command && mediaSession > 0) {
                command = goal.command; barrier = mediaCommand(goal.paused ? "PAUSE" : "PLAY"); acknowledged = false; actionAt = now;
            }
            if (!goal.active && possible && mediaKnown && mediaSession > 0 && now-stopAt >= 500) {
                mediaCommand("STOP"); stopAt = now;
            }
            if (goal.active && !pending && actionAt && !acknowledged && now-actionAt > 4500) view.state = ERROR;
            if (goal.active && (view.state == PLAYING || view.state == BUFFERING) && now-advanced > 8000) view.state = STALLED;
            cast::Message message;
            int received = channel.read(message,20);
            if (received < 0) { connected = false; continue; }
            if (received == 0) { publish(view); continue; }
            heardAt = now;
            if (message.destination != sender && message.destination != "*") continue;
            J json;
            if (!parse(message.json,json)) { connected = false; continue; }
            auto type = text(json,"type");
            double replyNumber = number(json,"requestId");
            int replyId = replyNumber > 0 && replyNumber <= INT_MAX ? int(replyNumber) : 0;
            auto request = requests.find(replyId);
            bool fresh = request != requests.end() && request->second.generation == generation;
            double stamp = SDL_GetTicksNS()/1e6;
            bool fast = fresh && stamp-request->second.time <= 120;
            if (fast) stamp = (stamp+request->second.time)/2;
            if (message.space == cast::heartbeat) {
                if (type == "PING") send(message.source,cast::heartbeat,{{"type",J(std::string("PONG"))}},false);
            } else if (message.space == cast::connection && type == "CLOSE") connected = false;
            else if (message.space == cast::receiver && message.source == "receiver-0" && type == "RECEIVER_STATUS") {
                const auto& volume = field(field(json,"status"),"volume");
                double level = number(volume,"level",-1);
                if (baseVolume < 0 && level >= 0 && level <= 1) {
                    baseVolume = level;
                    const auto& muted = field(volume,"muted"); baseMuted = muted.is<bool>() && muted.get<bool>();
                }
                const auto& apps = field(field(json,"status"),"applications");
                if (!apps.is<Array>()) continue;
                receiverKnown = true;
                receiverPresent = false;
                std::string destination, session;
                for (const auto& entry : apps.get<Array>()) if (text(entry,"appId") == cast::app) {
                    receiverPresent = true;
                    const auto& namespaces = field(entry,"namespaces"); bool hasMedia = false;
                    if (namespaces.is<Array>()) for (const auto& ns : namespaces.get<Array>()) if (text(ns,"name") == cast::media) hasMedia = true;
                    if (hasMedia) { destination = text(entry,"transportId"); session = text(entry,"sessionId"); }
                }
                if (destination != transport || session != receiverSession) {
                    transport = destination; receiverSession = session; mediaKnown = false; mediaSession = 0;
                    if (!transport.empty()) {
                        send(transport,cast::connection,{{"type",J(std::string("CONNECT"))},{"origin",J(Object{})}},false); pollAt = 0;
                    }
                }
                if (fresh && replyId > stopBarrier && apps.get<Array>().empty() && possible && (!goal.active || pending)) quiet();
            } else if (message.space == cast::media && message.source == transport) {
                if (type == "LOAD_FAILED" || type == "LOAD_CANCELLED" || type == "INVALID_REQUEST") {
                    if (fresh && replyId >= barrier) view.state = ERROR;
                    continue;
                }
                const auto& list = field(json,"status");
                if (type != "MEDIA_STATUS" || !list.is<Array>()) continue;
                // Uma resposta nova estabelece a posse. Notificações antigas não liberam a TV.
                bool owned = false, stopped = true;
                for (const auto& entry : list.get<Array>()) {
                    stopped = stopped && text(entry,"playerState") == "IDLE";
                    double sid = number(entry,"mediaSessionId");
                    if (sid <= 0 || std::floor(sid) != sid) continue;
                    const auto& resource = field(entry,"media");
                    auto mark = text(field(resource,"customData"),"tube360");
                    auto contentId = text(resource,"contentId");
                    bool cached = sid == mediaSession && (contentId.empty() || contentId == ownedUrl);
                    bool matches = !token.empty() && (mark == token || (mark.empty() && (contentId == ownedUrl || cached)));
                    if (!matches) continue;
                    owned = true; mediaSession = sid;
                    auto state = text(entry,"playerState");
                    if (goal.active && !pending && (fresh && replyId >= barrier)) acknowledged = true;
                    if (goal.active && acknowledged && (replyId == 0 || (fresh && replyId >= barrier))) {
                        auto previousState = view.state;
                        if (!state.empty())
                            view.state = state == "PLAYING" ? PLAYING : state == "PAUSED" ? PAUSED : state == "BUFFERING" ? BUFFERING : state == "IDLE" ? IDLE : UNKNOWN;
                        if (previousState == PAUSED && (view.state == PLAYING || view.state == BUFFERING)) advanced = now;
                        double position = number(entry,"currentTime",NAN);
                        if (std::isfinite(position) && position >= 0 && (fast || replyId == 0)) {
                            if (std::fabs(position-previousPosition) > .005) { advanced = now; previousPosition = position; }
                            view.position = position; view.updated = stamp; view.rate = std::clamp(number(entry,"playbackRate",1),0.0,4.0);
                        }
                        if ((view.state == PLAYING || view.state == BUFFERING) && now-advanced > 8000) view.state = STALLED;
                    }
                }
                if (fresh) mediaKnown = true;
                if (fresh && replyId > stopBarrier && stopped &&
                    (!goal.active || pending)) quiet();
                if (goal.active && !pending && acknowledged && fresh && !owned) { view.state = IDLE; view.updated = 0; }
            }
            publish(view);
        }
        channel.close();
    }
};
}
