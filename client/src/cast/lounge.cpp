#include <SDL3/SDL_log.h>
#include "lounge.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace cast {
namespace {
const string base = "https://www.youtube.com/api/lounge/";
picojson::value parse(const string& s, size_t limit = 256 * 1024) {
    if (s.size() > limit) return {};
    int depth = 0, nodes = 0; bool quoted = false, escaped = false;
    for (char c : s) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == '{') { if (++depth > 64 || ++nodes > 40000) return {}; }
        else if (c == ']' || c == '}') { if (--depth < 0) return {}; }
        else if (c == ',' && ++nodes > 40000) return {};
    }
    picojson::value v;
    try { if (!picojson::parse(v, s).empty()) return {}; }
    catch (const std::exception&) { return {}; }
    return v;
}
string scalar(const picojson::value& v) {
    if (v.is<string>()) return v.get<string>();
    if (v.is<double>() || v.is<bool>()) return v.serialize();
    return {};
}
double number(const picojson::value& v, const string& key, double fallback = 0) {
    string s = text(v, key); char* end = nullptr;
    double n = std::strtod(s.c_str(), &end);
    return !s.empty() && end && !*end && std::isfinite(n) ? n : fallback;
}
Object values(const Fields& f) {
    Object o;
    for (const auto& p : f) o[p.first] = picojson::value(p.second);
    return o;
}
}
string text(const picojson::value& v, const string& key) { return v.is<Object>() ? scalar(v.get(key)) : string{}; }
string clean(const string& s, size_t limit) {
    string out;
    for (unsigned char c : s) if (c >= 32 && c != 127) out += char(c);
    if (out.size() > limit) {
        size_t end = limit;
        while (end && (static_cast<unsigned char>(out[end]) & 0xc0) == 0x80) --end;
        out.resize(end);
    }
    return out;
}
bool videoId(const string& s) {
    return s.size() == 11 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == string::npos;
}
string form(const Fields& fields) {
    static const char hex[] = "0123456789ABCDEF";
    auto encode = [&](const string& s) {
        string out;
        for (unsigned char c : s) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
        return out;
    };
    string out;
    for (const auto& p : fields) { if (!out.empty()) out += '&'; out += encode(p.first) + '=' + encode(p.second); }
    return out;
}
bool Frames::feed(const char* data, size_t size, const std::function<bool(const string&)>& deliver) {
    if (size > 512 * 1024 - pending.size()) return false;
    pending.append(data, size);
    for (;;) {
        auto line = pending.find('\n');
        if (line == string::npos) return pending.size() < 12;
        string count = pending.substr(0, line);
        if (count.empty()) { pending.erase(0, 1); continue; }
        if (count.size() > 9 || count.find_first_not_of("0123456789") != string::npos) return false;
        auto n = std::strtoul(count.c_str(), nullptr, 10);
        if (!n || n > 256 * 1024) return false;
        if (pending.size() - line - 1 < n) return true;
        string frame = pending.substr(line + 1, n);
        pending.erase(0, line + 1 + n);
        if (!deliver(frame)) return false;
    }
}
Lounge::Lounge(yt::Transport& t, string identity, string savedScreen)
    : transport(t), device(std::move(identity)), screen(std::move(savedScreen)) {}
bool Lounge::request(const string& route, const Fields& fields, string& body) {
    auto r = transport.request(base + route, form(fields), !fields.empty());
    if (!r.error.empty() || r.status < 200 || r.status >= 300) return false;
    body = std::move(r.body);
    return true;
}
string Lounge::query(const string& kind) {
    Fields f{{"device","LOUNGE_SCREEN"},{"id",device},{"name","YouTube 360"},{"app","ytcr"},
        {"theme",theme},{"capabilities","dsp,mic,dpa,ntb"},{"cst","m"},{"mdxVersion","2"},
        {"loungeIdToken",token},{"VER","8"},{"v","2"},{"zx",device.substr(0,8) + std::to_string(rid)},{"t","1"}};
    if (kind == "init") {
        f["CVER"] = "1";
        f["deviceInfo"] = picojson::value(values({{"brand","Microsoft"},{"model","Xbox 360"},{"os","Xbox"},
            {"clientName","TVHTML5"},{"dialAdditionalDataSupportLevel","unsupported"},
            {"mdxDialServerType","MDX_DIAL_SERVER_TYPE_UNKNOWN"}})).serialize();
    } else {
        f["SID"] = sid; f["gsessionid"] = gsession; f["AID"] = std::to_string(std::max(0, aid));
    }
    if (kind == "rpc") { f["RID"] = "rpc"; f["CI"] = "0"; f["TYPE"] = "xmlhttp"; }
    else f["RID"] = std::to_string(rid++);
    return form(f);
}
bool Lounge::begin(uint64_t now, const string& selectedTheme) {
    sid.clear(); gsession.clear(); token.clear(); code.clear(); aid = -1; offset = 0;
    theme = selectedTheme;
    string body;
    if (screen.empty()) {
        if (!request("pairing/generate_screen_id", {}, body)) return false;
        screen = clean(body, 128);
        screen.erase(std::remove_if(screen.begin(), screen.end(), [](char c) { return c == ' ' || c == '\r' || c == '\n'; }), screen.end());
        if (screen.empty()) return false;
    }
    if (!request("pairing/get_lounge_token_batch", {{"screen_ids",screen}}, body)) return false;
    auto response = parse(body);
    if (!response.is<Object>()) return false;
    auto screens = response.get("screens");
    if (!screens.is<picojson::array>() || screens.get<picojson::array>().empty()) { screen.clear(); return false; }
    const auto& s = screens.get<picojson::array>().front();
    token = text(s, "loungeToken");
    if (token.empty()) { screen.clear(); return false; }
    double ttl = number(s, "expiration", 2400);
    tokenUntil = now + uint64_t(std::clamp(ttl - 60, 30.0, 3600.0) * 1000);
    if (!request("bc/bind?" + query("init"), {{"count","0"}}, body)) return false;
    Frames frames;
    if (!frames.feed(body.data(), body.size(), [&](const string& f) { return receive(f); })) return false;
    return !sid.empty() && !gsession.empty();
}
string Lounge::streamUrl() { return base + "bc/bind?" + query("rpc"); }
bool Lounge::pair(const string& pairing) {
    string body;
    return request("pairing/register_pairing_code", {{"access_type","permanent"},{"app","ytcr"},
        {"pairing_code",pairing},{"screen_id",screen},{"screen_name","YouTube 360"},{"device_id",device}}, body);
}
bool Lounge::pairingCode() {
    string body;
    if (!request("pairing/get_pairing_code?ctx=pair", {{"access_type","permanent"},{"app","ytcr"},
        {"lounge_token",token},{"screen_id",screen},{"screen_name","YouTube 360"},{"device_id",device}}, body)) return false;
    body = clean(body, 64);
    body.erase(std::remove(body.begin(), body.end(), ' '), body.end());
    if (body.size() != 12 || body.find_first_not_of("0123456789") != string::npos) return false;
    code.clear();
    for (size_t i = 0; i < body.size(); ++i) { if (i && i % 3 == 0) code += ' '; code += body[i]; }
    return true;
}
void Lounge::emit(const string& name, Object payload) {
    // As notificações são estados completos. A mais recente substitui a anterior ainda não enviada.
    for (auto& m : outgoing) if (m.name == name) { m.payload = std::move(payload); return; }
    outgoing.push_back({name, std::move(payload)});
}
void Lounge::report() {
    Object state;
    if (!current.empty()) {
        state = values({{"videoId",current},{"listId",list},{"currentIndex",std::to_string(listIndex)},
            {"cpn",cpn},{"state",std::to_string(actual.state)},{"currentTime",std::to_string(actual.position)},
            {"duration",std::to_string(actual.duration)},{"loadedTime",std::to_string(actual.duration)},
            {"seekableStartTime","0"},{"seekableEndTime",std::to_string(actual.duration)}});
        if (!params.empty()) state["params"] = picojson::value(params);
        if (!ctt.empty()) state["ctt"] = picojson::value(ctt);
    } else state = values({{"state","-1"},{"currentTime","0"},{"duration","0"}});
    emit("nowPlaying", state); emit("onStateChange", state);
    emit("onHasPreviousNextChanged", values({{"hasPrevious",index > 0 ? "true" : "false"},
        {"hasNext",index >= 0 && index + 1 < int(queue.size()) ? "true" : "false"}}));
    emit("onAutoplayModeChanged", values({{"autoplayMode",supportsAutoplay ? (autoplay ? "ENABLED" : "DISABLED") : "UNSUPPORTED"}}));
}
void Lounge::select(int target, double position) {
    if (target < 0 || target >= int(queue.size())) return;
    listIndex += target - index;
    index = target; current = queue[size_t(index)]; cpn = device.substr(0,8) + std::to_string(rid);
    seen = false; actual = {current, position, 0, 3, ++serial};
    commands.push_back({"play",current,std::to_string(position),"","cast",std::to_string(serial)});
    report();
}
void Lounge::incoming(int seq, const picojson::array& data) {
    if (data.empty() || !data[0].is<string>()) return;
    string name = data[0].get<string>();
    if (name == "c" && data.size() > 1) { sid = scalar(data[1]); return; }
    if (name == "S" && data.size() > 1) { gsession = scalar(data[1]); return; }
    if (seq <= aid) { SDL_Log("celular: evento repetido %s seq %d", name.c_str(), seq); return; }
    const picojson::value p = data.size() > 1 && data[1].is<Object>() ? data[1] : picojson::value(Object{});
    SDL_Log("celular: %s seq %d video=%s lista=%.60s indice=%s", name.c_str(), seq, text(p,"videoId").c_str(),
            text(p,"videoIds").c_str(), text(p,"currentIndex").c_str());
    if (name == "loungeStatus") {
        auto devices = parse(text(p, "devices"));
        if (!devices.is<picojson::array>()) return;
        bool previouslyConnected = !senders.empty();
        senders.clear(); supportsAutoplay = true;
        for (const auto& d : devices.get<picojson::array>()) if (text(d,"type") == "REMOTE_CONTROL") {
            auto id = text(d,"id");
            if (id.empty() || senders.size() >= 16) continue;
            auto user = text(d,"user"), name = text(d,"name");
            if (name.empty()) name = text(d,"clientName");
            if (name.empty()) continue;
            senders[id] = clean(user.empty() ? name : user);
            string caps = "," + text(d,"capabilities") + ",";
            supportsAutoplay = supportsAutoplay && caps.find(",atp,") != string::npos;
        }
        who = senders.empty() ? "" : senders.begin()->second;
        supportsAutoplay = supportsAutoplay && !senders.empty();
        if (!previouslyConnected && supportsAutoplay) autoplay = true;
        auto details = parse(text(p,"connectionEventDetails"));
        if (previouslyConnected && senders.empty() && text(details,"ui") == "true") {
            current.clear(); queue.clear(); index = listIndex = -1; actual = {}; commands.push_back({"stop"});
        }
        report();
    } else if (name == "getNowPlaying") report();
    else if (name == "setPlaylist" || name == "updatePlaylist") {
        string ids = text(p,"videoIds"), selected = text(p,"videoId");
        std::vector<string> next;
        size_t from = 0;
        while (from < ids.size() && next.size() < 256) {
            auto end = ids.find(',',from); auto id = ids.substr(from,end-from);
            if (!videoId(id)) return;
            next.push_back(id);
            if (end == string::npos) break;
            from = end + 1;
        }
        if (name == "setPlaylist" && next.empty() && videoId(selected)) next.push_back(selected);
        if (name == "setPlaylist" && !selected.empty() && !videoId(selected)) return;
        auto chosen = std::find(next.begin(),next.end(), name == "setPlaylist" ? selected : current);
        if (name == "setPlaylist" && chosen == next.end() && !next.empty()) return;
        int target = int(chosen - next.begin());
        int requested = int(std::clamp(number(p,"currentIndex",target),0.0,1000000.0));
        if (name == "setPlaylist" && requested < int(next.size()) && next[size_t(requested)] == selected) target = requested;
        queue = std::move(next); list = clean(text(p,"listId"),256);
        if (target >= int(queue.size())) { current.clear(); actual = {}; index = -1; commands.push_back({"stop"}); report(); }
        else if (name == "setPlaylist") {
            params = clean(text(p,"params"),4096); ctt = clean(text(p,"ctt"),4096);
            listIndex = requested - target + index;
            if (selected != current || target != index) select(target, std::clamp(number(p,"currentTime"),0.0,864000.0));
            else { listIndex = requested; report(); }
        }
        else { listIndex += target - index; index = target; report(); }
    } else if (name == "next") select(index + 1);
    else if (name == "previous") select(index - 1);
    else if (name == "pause" && !current.empty()) commands.push_back({"pause"});
    else if (name == "play" && !current.empty()) commands.push_back({"resume"});
    else if (name == "seekTo" && !current.empty()) {
        double to = std::clamp(number(p,"newTime",-1), -1.0, 864000.0);
        if (to >= 0) select(index,to);
    } else if (name == "stopVideo") {
        current.clear(); queue.clear(); index = -1; actual = {}; commands.push_back({"stop"}); report();
    } else if (name == "setAutoplayMode") { autoplay = text(p,"autoplayMode") == "ENABLED"; report(); }
    else if (name == "getVolume") emit("onVolumeChanged", values({{"volume",std::to_string(volume)},{"muted",muted ? "true" : "false"}}));
    else if (name == "setVolume") {
        double level = number(p,"volume",-1);
        string mute = text(p,"muted");
        if (level >= 0 && level <= 100 && (mute.empty() || mute == "true" || mute == "false")) {
            volume = level; muted = mute == "true"; volumeControlled = true;
            auto value = std::to_string(level);
            commands.push_back({"volume",value,mute == "true" ? "1" : "0"});
            emit("onVolumeChanged", values({{"volume",value},{"muted",mute == "true" ? "true" : "false"}}));
        }
    }
}
bool Lounge::receive(const string& frame) {
    auto v = parse(frame);
    if (!v.is<picojson::array>()) return false;
    for (const auto& row : v.get<picojson::array>()) {
        if (!row.is<picojson::array>()) return false;
        const auto& r = row.get<picojson::array>();
        if (r.size() != 2 || !r[0].is<double>() || !r[1].is<picojson::array>()) return false;
        double seq = r[0].get<double>();
        if (seq < 0 || seq > std::numeric_limits<int>::max() || std::floor(seq) != seq) return false;
        const auto& data = r[1].get<picojson::array>();
        if (!data.empty() && data[0].is<string>() && (data[0].get<string>() == "stop" || data[0].get<string>() == "close")) return false;
        incoming(int(seq),data); aid = std::max(aid,int(seq));
        if (commands.size() > 256) return false;
    }
    return true;
}
bool Lounge::flush() {
    if (outgoing.empty()) return true;
    Fields f{{"count",std::to_string(outgoing.size())},{"ofs",std::to_string(offset)}};
    int n = 0;
    for (const auto& m : outgoing) {
        string prefix = "req" + std::to_string(n++) + "_";
        f[prefix + "_sc"] = m.name;
        for (const auto& p : m.payload) f[prefix + p.first] = scalar(p.second);
    }
    string body;
    if (!request("bc/bind?" + query("send"),f,body)) return false;
    offset += n; outgoing.clear(); return true;
}
void Lounge::observe(const Playback& p) {
    if (!volumeControlled && p.volume >= 0 && p.volume <= 100 && (volume != p.volume || muted != p.muted)) {
        volume = p.volume; muted = p.muted;
        emit("onVolumeChanged",values({{"volume",std::to_string(volume)},{"muted",muted ? "true" : "false"}}));
    }
    if (current.empty()) return;
    if (seen && !p.serial && !p.id.empty()) {
        current.clear(); list.clear(); queue.clear(); commands.clear(); actual = {};
        index = listIndex = -1; seen = false; report(); return;
    }
    if (p.serial != serial) return;
    if (p.id == current) {
        bool changed = !seen || p.state != actual.state || p.duration != actual.duration;
        seen = p.state == 1 || p.state == 2 || seen;
        actual = p;
        if (changed) report();

    } else if (seen && p.id.empty()) {
        current.clear(); queue.clear(); actual = {}; index = -1; seen = false; report();
    }
}
void Lounge::ended(const string& id, uint64_t endedSerial) {
    if (id.empty() || id != current || endedSerial != serial) return;
    if (index + 1 < int(queue.size())) select(index + 1);
    else { current.clear(); actual = {}; index = -1; report(); }
}
bool Lounge::needsNext() const { return (autoplay || !list.empty()) && !current.empty() && index + 1 >= int(queue.size()) && queue.size() < 256; }
string Lounge::nextInPlaylist() {
    if (list.empty() || current.empty()) return {};
    Object client = values({{"clientName","TVHTML5"},{"clientVersion","7.20230405.08.01"},{"hl","en"}});
    Object context{{"client",picojson::value(client)}};
    if (!ctt.empty()) {
        Object transfer = values({{"scope","VIDEO"},{"token",ctt}});
        Object user{{"credentialTransferTokens",picojson::value(picojson::array{picojson::value(transfer)})}};
        context["user"] = picojson::value(user);
    }
    Object body = values({{"videoId",current},{"playlistId",list}});
    body["context"] = picojson::value(context);
    body["playlistIndex"] = picojson::value(double(listIndex));
    body["enableMdxAutoplay"] = picojson::value(true); body["isMdxPlayback"] = picojson::value(true);
    if (!params.empty()) body["params"] = picojson::value(params);
    yt::Request request;
    request.url = "https://www.youtube.com/youtubei/v1/next?prettyPrint=false";
    request.body = picojson::value(body).serialize();
    request.agent = "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/75.0.3770.142 Safari/537.36; SMART-TV; Tizen 4.0,gzip(gfe)";
    request.headers = {"X-Youtube-Client-Name: 7","X-Youtube-Client-Version: 7.20230405.08.01"};
    auto response = transport.send(request);
    if (response.status != 200 || !response.error.empty()) return {};
    auto result = parse(response.body,3 * 1024 * 1024);
    auto field = [](const picojson::value& value, const string& key) -> const picojson::value& {
        static const picojson::value empty;
        return value.is<Object>() ? value.get(key) : empty;
    };
    const auto* sets = &result;
    for (const auto* key : {"contents","singleColumnWatchNextResults","autoplay","autoplay","sets"}) sets = &field(*sets,key);
    if (!sets->is<picojson::array>() || sets->get<picojson::array>().empty()) return {};
    const auto& renderer = field(sets->get<picojson::array>().front(),"nextVideoRenderer");
    const auto& next = field(field(renderer,"autoplayEndpointRenderer"),"endpoint");
    string id = text(field(next,"watchEndpoint"),"videoId");
    return videoId(id) && id != current ? id : string{};
}
void Lounge::offerNext(const string& id) {
    if (videoId(id) && std::find(queue.begin(),queue.end(),id) == queue.end()) { queue.push_back(id); report(); }
}
bool Lounge::pop(Command& c) {
    if (commands.empty()) return false;
    c = std::move(commands.front()); commands.pop_front(); return true;
}
void Lounge::reset(bool clearPlayer) {
    code.clear(); who.clear(); senders.clear();
    if (clearPlayer) {
        commands.clear(); outgoing.clear(); queue.clear(); current.clear(); actual = {};
        index = listIndex = -1; seen = false; autoplay = supportsAutoplay = false;
    }
}
}
