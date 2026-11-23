// Leitores para AVIO. Somente o demuxer move o cursor da sua faixa.
#pragma once
#include "client.hpp"
#include "thread.hpp"
#include "../net.hpp"
extern "C" {
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <SDL3/SDL.h>

namespace yt {
inline Response mediaGet(Transport& net, string& url, std::vector<string> headers = {}) {
    Request req; req.url = url; req.headers = std::move(headers);
    for (int hop = 0; ; hop++) {
        auto r = net.send(req);
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
    struct Window {
        int64_t base = 0, total = -1;
        long status = 0;
        string bytes;
        bool contains(int64_t offset) const { return offset >= base && offset-base < int64_t(bytes.size()); }
    };
    std::unique_ptr<Transport> net;
    string url;
    int64_t pos = 0, length = -1;
    long status = 0;
    bool failed = false;
    Window current, next;
    SDL_Thread* worker = nullptr;
    SDL_AtomicInt* received;
    RangeFile(std::unique_ptr<Transport> connection, string address, SDL_AtomicInt* count)
        : net(std::move(connection)), url(std::move(address)), received(count) {}
    ~RangeFile() { join(); }
    void join() { if (worker) { SDL_WaitThread(worker, nullptr); worker = nullptr; } }
    void fetch(Window& w) {
        auto end = w.base + (1024 * 1024 - 1);
        if (length > 0) end = std::min(end,length-1);
        auto r = mediaGet(*net, url, {"Range: bytes=" + std::to_string(w.base) + "-" + std::to_string(end)});
        w.status = r.status;
        long long first, last, total;
        if (!r.error.empty() || r.status != 206 ||
            std::sscanf(r.contentRange.c_str(), " bytes %lld-%lld/%lld", &first, &last, &total) != 3 ||
            first != w.base || last < first || last > end || total <= last ||
            (length >= 0 && total != length) || last-first+1 != int64_t(r.body.size())) return;
        w.total = total; w.bytes = std::move(r.body);
        SDL_AddAtomicInt(received, int(w.bytes.size()));
    }
    int read(uint8_t* dst, int size) {
        if (failed) return AVERROR(EIO);
        if (length >= 0 && pos >= length) return AVERROR_EOF;
        if (!current.contains(pos)) {
            if (worker) {
                join();
                if (next.base == pos || next.contains(pos)) current = std::move(next);
            }
            if (!current.contains(pos) && (current.base != pos || current.status == 0)) {
                current = {}; current.base = pos; fetch(current);
            }
            if (!current.contains(pos)) { status = current.status; failed = true; return AVERROR(EIO); }
            length = current.total;
        }
        size_t off = size_t(pos-current.base);
        size_t n = std::min(size_t(size), current.bytes.size()-off);
        std::memcpy(dst,current.bytes.data()+off,n); pos += n;
        // Só antecipa após o consumo de parte da janela: a sondagem do índice MP4 pode saltar imediatamente.
        if (!worker && off+n >= current.bytes.size()/3 && current.base+int64_t(current.bytes.size()) < length) {
            next = {}; next.base = current.base + int64_t(current.bytes.size());
            worker = yt::worker([](void* data) {
                auto& f = *static_cast<RangeFile*>(data); f.fetch(f.next); return 0;
            }, "range", this);
        }
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
    Transport& net;
    const std::atomic_bool& stop;
    string playlist, audioPlaylist, segment, sound;
    struct List {
        int64_t first = 0;
        std::vector<string> urls;
        bool ended = false;
        int64_t last() const { return first + int64_t(urls.size()) - 1; }
    } video, audio;

    int64_t sequence = -1;
    long status = 0;
    int height = 0, delay = 1000;
    bool highRate720 = false;

    bool fetch(string url, string& body) {
        auto r = mediaGet(net,url);
        status = r.status;
        if (status != 200 || !r.error.empty()) return false;
        body = std::move(r.body);

        return true;
    }
    bool open(const string& url, int ceiling) {
        string master, attrs, group;
        std::map<string,string> groups;
        if (!fetch(url, master) || !master.starts_with("#EXTM3U")) return false;
        for (auto& line : net::split(master, '\n')) {
            if (line.starts_with("#EXT-X-MEDIA:") && attribute(line,"TYPE") == "AUDIO")
                groups[attribute(line,"GROUP-ID")] = hlsUrl(url, attribute(line,"URI"));
            if (line.starts_with("#EXT-X-STREAM-INF:")) attrs = line;
            else if (!attrs.empty() && !line.empty() && line.front() != '#') {
                int w = 0, h = 0;
                auto fps = std::atof(attribute(attrs, "FRAME-RATE").c_str());
                auto codecs = attribute(attrs, "CODECS");
                // Os itags HLS 93–95 têm teto de 30 fps mesmo nos manifestos sem FRAME-RATE.
                bool legacy = line.find("/itag/93/") != string::npos || line.find("/itag/94/") != string::npos || line.find("/itag/95/") != string::npos;
                std::sscanf(attribute(attrs,"RESOLUTION").c_str(), "%dx%d", &w, &h);
                if (h == 720 && fps > 30 && codecs.find("avc1") != string::npos) highRate720 = true;
                if (w > 0 && w <= 1280 && h <= ceiling && h > height && ((fps > 0 && fps <= 30) || (fps == 0 && legacy)) &&
                    codecs.find("avc1") != string::npos && codecs.find("mp4a") != string::npos) {
                    playlist = hlsUrl(url, line); height = h; group = attribute(attrs,"AUDIO");
                }
                attrs.clear();
            }
        }
        audioPlaylist = groups[group];
        return !playlist.empty() && (group.empty() || !audioPlaylist.empty());
    }
    bool update(const string& url, List& list) {
        string body;
        if (!fetch(url,body) || !body.starts_with("#EXTM3U")) return false;
        list = {};
        for (const auto& line : net::split(body,'\n')) {
            if (line.starts_with("#EXT-X-MEDIA-SEQUENCE:")) list.first = std::strtoll(line.c_str()+22,nullptr,10);
            else if (line.starts_with("#EXT-X-TARGETDURATION:")) delay = std::clamp(std::atoi(line.c_str()+22)*500,500,5000);
            else if (line == "#EXT-X-ENDLIST") list.ended = true;
            else if (line.starts_with("#EXT-X-MAP:") || line.starts_with("#EXT-X-KEY:")) return false;
            else if (!line.empty() && line[0] != '#') list.urls.push_back(hlsUrl(url,line));
        }
        return !list.urls.empty();
    }
    int refill() {
        bool paired = !audioPlaylist.empty();
        uint64_t since = SDL_GetTicks();
        while (!stop) {
            if (sequence < 0 || sequence > video.last()) {
                if (video.ended) return AVERROR_EOF;
                if (!update(playlist,video)) return AVERROR(EIO);
            }
            if (paired && (sequence < 0 || sequence > audio.last())) {
                if (audio.ended) return AVERROR_EOF;
                if (!update(audioPlaylist,audio)) return AVERROR(EIO);
            }
            int64_t edge = paired ? std::min(video.last(),audio.last()) : video.last();
            if (sequence < 0) sequence = edge;
            if (sequence < video.first || (paired && sequence < audio.first)) return AVERROR_INVALIDDATA;
            if (sequence <= edge) {
                if (!fetch(video.urls[sequence-video.first],segment) || segment.empty()) return AVERROR(EIO);
                if (paired && (!fetch(audio.urls[sequence-audio.first],sound) || sound.empty())) return AVERROR(EIO);
                ++sequence; return 0;
            }
            if (SDL_GetTicks()-since > 20000) return AVERROR(ETIMEDOUT);
            for (int i=0; i<delay && !stop; i+=50) SDL_Delay(50);
        }
        return AVERROR_EXIT;
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
