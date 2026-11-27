// Leitores para AVIO. Somente o demuxer move o cursor da sua faixa.
#pragma once
#include "client.hpp"
#include "thread.hpp"
#include "../net.hpp"
#include "../playback_mode.hpp"
extern "C" {
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <cstdio>
#include <deque>
#include <cstring>
#include <cmath>
#include <map>
#include <SDL3/SDL.h>

namespace yt {
inline Response mediaGet(Transport& net, string& url, std::vector<string> headers = {}, bool fresh = false) {
    Request req; req.url = url; req.headers = std::move(headers); req.freshConnection=fresh; req.media=true;
    long connections = 0;
    for (int hop = 0; ; hop++) {
        auto r = net.send(req);
        connections += r.newConnections; r.newConnections = connections;
        if (r.status < 300 || r.status > 399 || hop == 3 || !r.error.empty()) return r;
        const auto& next = r.location;
        auto slash = next.find('/', 8);
        if (!next.starts_with("https://") || slash == string::npos ||
            !next.substr(8,slash-8).ends_with(".googlevideo.com") ||
            next.find_first_of("@\\ \r\n\t") != string::npos) return {0,{},"invalid_media_redirect"};
        req.url = url = next;
    }
}
struct RangeFile {
    static constexpr int chunk = 512 * 1024;
    struct Window {
        int64_t base = 0, total = -1;
        long status = 0;
        Failure failure = Failure::NONE;
        string error;
        string bytes;
        bool contains(int64_t offset) const { return offset >= base && offset-base < int64_t(bytes.size()); }
    };
    struct Lane {
        RangeFile* owner = nullptr;
        std::unique_ptr<Transport> net;
        string url;
        Window window;
        SDL_Thread* worker = nullptr;
        std::atomic<uint64_t> bytes{0}, transferMs{0};
    } lanes[2];
    int laneCount = 1;
    int64_t pos = 0, length = -1;
    long status = 0;
    bool failed = false;
    bool aheadEnabled = true;
    Failure failure = Failure::NONE;
    string error;
    Window current;
    SDL_AtomicInt* received;
    std::atomic<unsigned> requests{0}, resumes{0}, connections{0};
    std::atomic<uint64_t> bytes{0}, transferMs{0};
    uint64_t waitMs = 0;
    std::atomic<int> receiveBuffer{0}, sendBuffer{0};
    RangeFile(std::unique_ptr<Transport> connection, string address, SDL_AtomicInt* count,
              std::unique_ptr<Transport> parallel = {}) : received(count) {
        lanes[0].net=std::move(connection);
        if (parallel) { lanes[1].net=std::move(parallel); laneCount=2; }
        for (int i=0; i<laneCount; ++i) { lanes[i].owner=this; lanes[i].url=address; }
    }
    ~RangeFile() {
        for (int i=0; i<laneCount; ++i) lanes[i].net->cancel();
        join();
    }
    static void join(Lane& lane) {
        if (lane.worker) { SDL_WaitThread(lane.worker,nullptr); lane.worker=nullptr; }
    }
    void join() { for (int i=0; i<laneCount; ++i) join(lanes[i]); }
    void fetch(Lane& lane) {
        auto& w=lane.window;
        int64_t end=w.base+std::min<int64_t>(chunk-1,INT64_MAX-w.base);
        if (w.total>=0) end=std::min(end,w.total-1);
        for (int attempt=0; attempt<4 && !lane.net->cancelled(); ++attempt) {
            int64_t begin=w.base+int64_t(w.bytes.size());
            Request req; req.media=true; req.freshConnection=attempt>0;
            req.url=lane.url;
            req.headers={"Range: bytes="+std::to_string(begin)+"-"+std::to_string(end)};
            auto r=mediaGet(*lane.net,lane.url,req.headers,req.freshConnection);
            requests++; connections+=r.newConnections;
            transferMs+=uint64_t(r.seconds*1000);
            lane.transferMs+=uint64_t(r.seconds*1000);
            receiveBuffer=r.receiveBuffer; sendBuffer=r.sendBuffer;
            w.status=r.status;
            w.failure=failureOf(r); w.error=r.error;
            long long first=0,last=0,total=0;
            bool valid=r.status==206 &&
                std::sscanf(r.contentRange.c_str()," bytes %lld-%lld/%lld",&first,&last,&total)==3 &&
                first==begin && last>=first && last<=end && total>last &&
                (w.total<0 || total==w.total) && int64_t(r.body.size())<=last-first+1 &&
                (!r.error.empty() || int64_t(r.body.size())==last-first+1);
            if (valid && (r.error.empty() || retryable(r))) {
                w.total=total;
                end=std::min(end,int64_t(total-1));
                if (!r.body.empty()) {
                    bytes+=r.body.size();
                    lane.bytes+=r.body.size();
                    if (received) SDL_AddAtomicInt(received,int(r.body.size()));
                    w.bytes+=r.body;
                }
                if (r.error.empty() && int64_t(r.body.size())==last-first+1) return;
                if (w.base+int64_t(w.bytes.size())>end) return;
            } else if (r.status==206 || !retryable(r)) {
                if (r.status==206 && !valid) { w.failure=Failure::HTTP; w.error="invalid_content_range"; }
                break;
            }
            if (attempt==3 || !lane.net->backoff(150u << attempt)) break;
            resumes++;
        }
        // Nunca publica uma janela incompleta como EOF: o demuxer deve receber erro na posição exata.
        w.bytes.clear();
    }
    void prefetch() {
        int64_t next=current.base+int64_t(current.bytes.size());
        for (int i=0; i<laneCount && next<length; ++i) {
            bool exists=false;
            for (int j=0; j<laneCount; ++j)
                if (lanes[j].window.base==next && (lanes[j].worker || !lanes[j].window.bytes.empty())) exists=true;
            if (!exists) for (int j=0; j<laneCount; ++j) {
                auto& lane=lanes[j];
                if (lane.worker || !lane.window.bytes.empty()) continue;
                lane.window={}; lane.window.base=next; lane.window.total=length;
                lane.worker=yt::worker([](void* p) {
                    auto& lane=*static_cast<Lane*>(p); lane.owner->fetch(lane); return 0;
                },"range",&lane);
                // Falha de criação de thread mantém a leitura síncrona disponível.
                if (!lane.worker) lane.window={};
                break;
            }
            next+=chunk;
        }
    }
    int read(uint8_t* dst, int size) {
        if (size<=0) return AVERROR(EINVAL);
        if (lanes[0].net->cancelled()) return AVERROR_EXIT;
        if (failed) return AVERROR(EIO);
        if (length>=0 && pos>=length) return AVERROR_EOF;
        if (!current.contains(pos)) {
            bool found=false;
            for (int i=0; i<laneCount; ++i) {
                auto& lane=lanes[i];
                // base não muda enquanto o worker está ativo; bytes só são lidos após join.
                if ((lane.worker || !lane.window.bytes.empty()) && lane.window.base==pos) {
                    uint64_t began=SDL_GetTicks(); join(lane); waitMs+=SDL_GetTicks()-began;
                    current=std::move(lane.window); lane.window={}; found=true; break;
                }
            }
            if (!found) {
                uint64_t began=SDL_GetTicks(); join(); waitMs+=SDL_GetTicks()-began;
                for (int i=0; i<laneCount; ++i) lanes[i].window={};
                auto& lane=lanes[0]; lane.window.base=pos; lane.window.total=length;
                began=SDL_GetTicks(); fetch(lane); waitMs+=SDL_GetTicks()-began;
                current=std::move(lane.window); lane.window={};
            }
            if (!current.contains(pos)) {
                status=current.status; failure=current.failure; error=current.error; failed=true; return AVERROR(EIO);
            }
            length=current.total;
        }
        size_t off=size_t(pos-current.base), n=std::min(size_t(size),current.bytes.size()-off);
        std::memcpy(dst,current.bytes.data()+off,n); pos+=n;
        if (aheadEnabled) prefetch();
        return int(n);
    }
    static int pull(void* p, uint8_t* dst, int size) { return static_cast<RangeFile*>(p)->read(dst, size); }
    static int64_t seek(void* p, int64_t offset, int whence) {
        auto& f = *static_cast<RangeFile*>(p);
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) return f.length >= 0 ? f.length : AVERROR(ENOSYS);
        int64_t from = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f.pos : whence == SEEK_END ? f.length : -1;
        if (from < 0 || offset < -from || offset > INT64_MAX - from) return AVERROR(EINVAL);
        return f.pos = from + offset;
    }
};

inline string hlsUrl(const string& base, const string& ref) {
    if (ref.starts_with("https://")) return ref;
    if (ref.empty() || ref.starts_with("//") || ref.find(':') != string::npos) return {};
    if (ref.front() == '/') return base.substr(0, base.find('/', 8)) + ref;
    return base.substr(0, base.find_last_of('/') + 1) + ref;
}
inline string attribute(const string& line, const string& name) {
    auto at = line.find(name + "=");
    if (at == string::npos) return {};
    at += name.size() + 1;
    if (line[at] == '"') { ++at; return line.substr(at, line.find('"', at) - at); }
    return line.substr(at, line.find(',', at) - at);
}
struct LiveFile {
    Transport& lists;
    Transport& media;
    const std::atomic_bool& stop;
    string playlist, audioPlaylist, segment, sound;
    struct List {
        int64_t first = 0;
        std::vector<string> urls;
        std::vector<double> stamps, durations;
        bool ended = false;
        int64_t last() const { return first + int64_t(urls.size()) - 1; }
    };
    struct Pair { string video, audio; double stamp = -1, duration = 0; };
    static constexpr int reserve = 3;
    int startBack = reserve;   // quantos segmentos antes da borda a abertura começa (1 = o mais novo, atraso mínimo)
    // Relógio UTC do PROGRAM-DATE-TIME (-1 sem ele): início do 1º segmento entregue, fim do mais novo da playlist e fim do último planejado.
    std::atomic<double> firstStamp{-1}, edgeEnd{-1}, plannedEnd{-1}, fetchedEnd{-1}, segmentSec{0};
    std::deque<Pair> pending;
    SDL_Mutex* mu = SDL_CreateMutex();
    SDL_Condition* cv = SDL_CreateCondition();
    SDL_Thread* planner = nullptr;
    std::atomic_bool closing{false};
    int finished = 0;
    std::atomic_long status{0};
    int height = 0, delay = 1000;
    double frameRate = 0;
    unsigned qualityModes = 1;
    bool pinned = false;
    struct Variant { string url, group; int height = 0; double fps = 0; };
    std::vector<Variant> lowerVariants;
    std::atomic_bool lowerRequested{false};
    std::atomic<int> lowerRemaining{0}, selectedHeight{0}, selectedRate{0};
    std::atomic<unsigned> switches{0};
    std::vector<AudioTrack> audioTracks;
    string selectedAudio;
    unsigned segments = 0, connections = 0;
    uint64_t bytes = 0, transferMs = 0;

    LiveFile(Transport& playlists, Transport& segments, const std::atomic_bool& cancel)
        : lists(playlists), media(segments), stop(cancel) {}
    ~LiveFile() {
        closing = true;
        if (planner) { SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu); SDL_WaitThread(planner,nullptr); }
        SDL_DestroyCondition(cv); SDL_DestroyMutex(mu);
    }
    bool fetch(Transport& connection, string url, string& body) {
        auto r = mediaGet(connection,url);
        if (r.status != 200 || !r.error.empty()) { if (r.status == 403) status = 403; return false; }
        if (&connection == &media) connections += r.newConnections;
        body = std::move(r.body);
        return true;
    }
    bool open(const string& url, int ceiling, bool preferFast = false, const string& audioChoice = "original",bool fixed = false) {
        pinned=fixed;
        string master, attrs, group;
        int bestRank = 0;
        std::map<string,string> groups;
        std::map<string,std::vector<AudioTrack>> tracks;
        std::vector<Variant> variants;
        if (!mu || !cv || !fetch(lists,url, master) || !master.starts_with("#EXTM3U")) return false;
        for (auto& line : net::split(master, '\n')) {
            if (line.starts_with("#EXT-X-MEDIA:") && attribute(line,"TYPE") == "AUDIO") {
                groups[attribute(line,"GROUP-ID")] = hlsUrl(url, attribute(line,"URI"));
                string language=attribute(line,"LANGUAGE"),name=attribute(line,"NAME"),lower=name;
                std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){ return char(std::tolower(c)); });
                bool original=lower.find("original")!=string::npos, automatic=lower.find("auto")!=string::npos;
                tracks[attribute(line,"GROUP-ID")].push_back({audioCode(language,original,automatic),language,name,original,automatic,attribute(line,"DEFAULT")=="YES",{groups[attribute(line,"GROUP-ID")]}});
            }
            if (line.starts_with("#EXT-X-STREAM-INF:")) attrs = line;
            else if (!attrs.empty() && !line.empty() && line.front() != '#') {
                int w = 0, h = 0;
                auto fps = std::atof(attribute(attrs, "FRAME-RATE").c_str());
                auto codecs = attribute(attrs, "CODECS");
                // Os itags HLS 93–95 têm teto de 30 fps mesmo nos manifestos sem FRAME-RATE.
                bool legacy = line.find("/itag/93/") != string::npos || line.find("/itag/94/") != string::npos || line.find("/itag/95/") != string::npos;
                std::sscanf(attribute(attrs,"RESOLUTION").c_str(), "%dx%d", &w, &h);
                int rank = preferFast && fps>30 ? 4 : fps > 0 && fps <= 30 ? 3 : fps == 0 && legacy ? 2 : 1;
                if(w>0 && w<=1280 && h>0 && std::isfinite(fps) && fps>=0 && fps<=60.01 && codecs.find("avc1")!=string::npos && codecs.find("mp4a")!=string::npos)
                    qualityModes|=playback::offeredMode(h,fps,w);
                if (w>0 && w<=1280 && h>0 && h<=ceiling && std::isfinite(fps) && fps>=0 && fps<=60.01 &&
                    codecs.find("avc1")!=string::npos && codecs.find("mp4a")!=string::npos)
                    variants.push_back({hlsUrl(url,line),attribute(attrs,"AUDIO"),h,fps});
                if (w > 0 && w <= 1280 && h > 0 && h <= ceiling && (fixed?h==ceiling:(ceiling < 720 || h == 720)) &&
                    std::isfinite(fps) && fps >= 0 && fps <= 60.01 &&
                    (rank > bestRank || (rank == bestRank && h > height)) &&
                    codecs.find("avc1") != string::npos && codecs.find("mp4a") != string::npos) {
                    playlist = hlsUrl(url, line); height = h; group = attribute(attrs,"AUDIO");
                    frameRate = fps; bestRank = rank;
                }
                attrs.clear();
            }
        }
        audioPlaylist = groups[group];
        audioTracks=tracks[group];
        if (!audioTracks.empty()) {
            const AudioTrack* chosen=&audioTracks.front();
            for (const auto& track : audioTracks) if (track.preferred) { chosen=&track; break; }
            for (const auto& track : audioTracks) if (track.original) { chosen=&track; break; }
            string wanted=audioChoice.starts_with("lang:") ? audioChoice.substr(5) : audioChoice=="original" ? "" : audioChoice.substr(0,audioChoice.rfind('-'));
            for (const auto& track : audioTracks) if (track.code==audioChoice || (!wanted.empty() && track.language.substr(0,track.language.find('-'))==wanted.substr(0,wanted.find('-')))) { chosen=&track; break; }
            audioPlaylist=chosen->format.url; selectedAudio=chosen->code;
        }
        if (playlist.empty() || (!group.empty() && audioPlaylist.empty())) return false;
        for (const auto& v : variants) if (!pinned && v.group==group && v.url!=playlist &&
            (v.height<height || (v.height==height && v.fps>0 && v.fps<=30.1 && frameRate>30.1))) lowerVariants.push_back(v);
        std::sort(lowerVariants.begin(),lowerVariants.end(),[](const Variant& a,const Variant& b) {
            return a.height!=b.height ? a.height>b.height : a.fps<b.fps;
        });
        lowerVariants.erase(std::unique(lowerVariants.begin(),lowerVariants.end(),[](const Variant& a,const Variant& b) {
            return a.height==b.height;
        }),lowerVariants.end());
        lowerRemaining=int(lowerVariants.size()); selectedHeight=height; selectedRate=int(std::lround(frameRate*1000));
        planner = yt::worker([](void* p) {
            auto& f = *static_cast<LiveFile*>(p);
            int result = f.plan();
            SDL_LockMutex(f.mu); f.finished = result; SDL_BroadcastCondition(f.cv); SDL_UnlockMutex(f.mu);
            return 0;
        }, "playlists", this);
        return planner != nullptr;
    }
    static double timestamp(const string& text) {
        int y=0,m=0,d=0,h=0,min=0,used=0; double sec=0;
        if (std::sscanf(text.c_str(),"%d-%d-%dT%d:%d:%lf%n",&y,&m,&d,&h,&min,&sec,&used)!=6 ||
            y<1970 || y>9999 || m<1 || m>12 || d<1 || d>31 || h<0 || h>23 || min<0 || min>59 || !std::isfinite(sec) || sec<0 || sec>=61) return -1;
        int zone=0;
        if (text.substr(used)!="Z") {
            int zh=0,zm=0,n=0;
            if (used>=int(text.size()) || (text[used]!='+' && text[used]!='-') ||
                std::sscanf(text.c_str()+used+1,"%d:%d%n",&zh,&zm,&n)!=2 || zh<0 || zh>23 || zm<0 || zm>59 || used+1+n!=int(text.size())) return -1;
            zone=(zh*60+zm)*60*(text[used]=='+' ? 1 : -1);
        }
        y-=m<=2;
        int era=y/400, year=y-era*400, day=(153*(m+(m>2 ? -3 : 9))+2)/5+d-1;
        int64_t days=int64_t(era)*146097+year*365+year/4-year/100+day-719468;
        return double(days*86400+h*3600+min*60-zone)+sec;
    }
    bool update(const string& url, List& list) {
        string body;
        if (!fetch(lists,url,body) || !body.starts_with("#EXTM3U")) return false;
        list = {};
        double stamp=-1, duration=0;
        for (const auto& line : net::split(body,'\n')) {
            if (line.starts_with("#EXT-X-MEDIA-SEQUENCE:")) list.first = std::strtoll(line.c_str()+22,nullptr,10);
            else if (line.starts_with("#EXT-X-TARGETDURATION:")) delay = std::clamp(std::atoi(line.c_str()+22)*500,500,5000);
            else if (line == "#EXT-X-ENDLIST") list.ended = true;
            else if (line.starts_with("#EXT-X-PROGRAM-DATE-TIME:")) stamp=timestamp(line.substr(25));
            else if (line.starts_with("#EXTINF:")) duration=std::atof(line.c_str()+8);
            else if (line.starts_with("#EXT-X-MAP:") || line.starts_with("#EXT-X-KEY:")) return false;
            else if (!line.empty() && line[0] != '#') {
                list.urls.push_back(hlsUrl(url,line)); list.stamps.push_back(stamp); list.durations.push_back(duration);
                stamp=stamp>=0 && std::isfinite(duration) && duration>0 ? stamp+duration : -1;
                duration=0;
            }
        }
        return !list.urls.empty();
    }
    bool requestLower() {
        if(pinned)return false;
        if (lowerRemaining<=0 || closing || stop) return false;
        bool expected=false;
        if (!lowerRequested.compare_exchange_strong(expected,true)) return false;
        SDL_LockMutex(mu); SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
        return true;
    }
    bool switchPlaylist(List& video, int64_t sequence, int64_t& offset) {
        // Media sequence não identifica o mesmo instante em variantes diferentes. Só troca com relógio UTC comum.
        int64_t at=sequence-video.first;
        double target=at>=0 && at<int64_t(video.stamps.size()) ? video.stamps[size_t(at)] :
            at==int64_t(video.stamps.size()) && at>0 && video.stamps.back()>=0 && video.durations.back()>0 ?
                video.stamps.back()+video.durations.back() : -1;
        if (target<0) { lowerRemaining=0; return false; }
        while (!lowerVariants.empty()) {
            auto candidate=lowerVariants.front(); lowerVariants.erase(lowerVariants.begin());
            List next;
            long previous=status.load();
            if (!update(candidate.url,next)) { status=previous; continue; }
            for (size_t i=0;i<next.stamps.size();++i) if (next.stamps[i]>=0 && std::fabs(next.stamps[i]-target)<.05) {
                offset=next.first+int64_t(i)-sequence; next.first-=offset;
                playlist=candidate.url; video=std::move(next);
                selectedHeight=candidate.height; selectedRate=int(std::lround(candidate.fps*1000));
                lowerRemaining=int(lowerVariants.size()); switches++;
                return true;
            }
        }
        lowerRemaining=0; return false;
    }
    int plan() {
        List video, audio;
        bool paired = !audioPlaylist.empty();
        int64_t sequence = -1;
        int64_t videoOffset = 0;
        uint64_t since = SDL_GetTicks();
        while (!stop && !closing) {
            if (lowerRequested.exchange(false) && sequence>=0) switchPlaylist(video,sequence,videoOffset);
            if (sequence < 0 || sequence > video.last()) {
                if (video.ended) return AVERROR_EOF;
                if (!update(playlist,video)) return AVERROR(EIO);
                video.first-=videoOffset;
            }
            if (paired && (sequence < 0 || sequence > audio.last())) {
                if (audio.ended) return AVERROR_EOF;
                if (!update(audioPlaylist,audio)) return AVERROR(EIO);
            }
            int64_t first = paired ? std::max(video.first,audio.first) : video.first;
            int64_t edge = paired ? std::min(video.last(),audio.last()) : video.last();
            if (!video.stamps.empty() && video.stamps.back() >= 0 && video.durations.back() > 0) {
                edgeEnd = video.stamps.back() + video.durations.back(); segmentSec = video.durations.back();
            }
            if (sequence < 0) sequence = std::max(first,edge - startBack + 1);
            if (sequence < first) return AVERROR_INVALIDDATA;
            if (sequence <= edge) {
                SDL_LockMutex(mu);
                while (pending.size() >= reserve && !stop && !closing && !lowerRequested) SDL_WaitConditionTimeout(cv,mu,100);
                if (lowerRequested) { SDL_UnlockMutex(mu); continue; }
                if (!stop && !closing) pending.push_back({video.urls[sequence-video.first],paired ? audio.urls[sequence-audio.first] : "",
                    size_t(sequence-video.first) < video.stamps.size() ? video.stamps[size_t(sequence-video.first)] : -1,
                    size_t(sequence-video.first) < video.durations.size() ? video.durations[size_t(sequence-video.first)] : 0});
                if (const auto at = size_t(sequence-video.first); at < video.stamps.size() && video.stamps[at] >= 0) {
                    if (firstStamp.load() < 0) firstStamp = video.stamps[at];
                    if (video.durations[at] > 0) plannedEnd = video.stamps[at] + video.durations[at];
                }
                SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
                ++sequence; since = SDL_GetTicks();
            } else {
                if (SDL_GetTicks()-since > 20000) return AVERROR(ETIMEDOUT);
                for (int i=0; i<delay && !stop && !closing; i+=50) SDL_Delay(50);
            }
        }
        return AVERROR_EXIT;
    }
    int refill() {
        SDL_LockMutex(mu);
        while (pending.empty() && !finished && !stop) SDL_WaitConditionTimeout(cv,mu,100);
        if (stop || pending.empty()) {
            int r = stop ? AVERROR_EXIT : finished;
            SDL_UnlockMutex(mu); return r;
        }
        auto pair = std::move(pending.front()); pending.pop_front();
        SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
        uint64_t start = SDL_GetTicks();
        if (!fetch(media,pair.video,segment) || segment.empty()) return AVERROR(EIO);
        if (!pair.audio.empty() && (!fetch(media,pair.audio,sound) || sound.empty())) return AVERROR(EIO);
        transferMs += SDL_GetTicks() - start;
        bytes += segment.size()+sound.size(); ++segments;
        if (pair.stamp >= 0 && pair.duration > 0) fetchedEnd = pair.stamp + pair.duration;   // fim do que já foi baixado (a reserva de verdade)
        return 0;
    }

};

// HLS Packed Audio: PRIV fixa a origem MPEG de 90 kHz e cada quadro ADTS informa sua própria duração.
struct PackedAudio {
    string bytes;
    size_t cursor = 0;
    int64_t timestamp = 0, samples = 0;
    bool begin(string segment) {
        bytes = std::move(segment); samples = 0;
        if (bytes.size() < 10 || bytes.compare(0,3,"ID3") != 0) return false;
        size_t length = 0;
        for (int i=6; i<10; ++i) {
            unsigned n = uint8_t(bytes[i]);
            if (n >= 128) return false;
            length = (length<<7) | n;
        }
        cursor = length+10;
        constexpr char owner[] = "com.apple.streaming.transportStreamTimestamp";
        auto at = bytes.find(owner,10);
        if (cursor > bytes.size() || at == string::npos || at+sizeof(owner)+8 > cursor ||
            at < 10 || bytes.compare(at-10,4,"PRIV") || bytes[at+sizeof(owner)-1] != 0) return false;
        timestamp = 0;
        for (int i=0; i<8; ++i) timestamp = ((timestamp<<8) | uint8_t(bytes[at+sizeof(owner)+i])) & 0x1ffffffffLL;
        return true;
    }
    int packet(AVPacket* packet) {
        size_t remaining = bytes.size()-cursor;
        auto* h = reinterpret_cast<const uint8_t*>(bytes.data()+cursor);
        if (remaining < 7 || h[0]!=0xff || (h[1]&0xf6)!=0xf0) return AVERROR_INVALIDDATA;
        static constexpr int rates[] = {96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
        unsigned rateIndex = (h[2]>>2)&15, size = ((h[3]&3)<<11) | (h[4]<<3) | (h[5]>>5);
        if (rateIndex >= 13 || size < 7 || size > remaining) return AVERROR_INVALIDDATA;
        int r = av_new_packet(packet,size);
        if (r < 0) return r;
        std::memcpy(packet->data,h,size);
        packet->pts = packet->dts = timestamp + av_rescale(samples,90000,rates[rateIndex]);
        packet->stream_index = 0; samples += 1024*((h[6]&3)+1); cursor += size;
        return 0;
    }
};
}
