#include "nsig_service.hpp"
#include "nsig.hpp"
#include "update/rules.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

namespace yt::nsig {
namespace {
using std::string;
// Muda quando o redutor ou o programa gerado mudam: o bytecode antigo deixa de valer.
constexpr const char* programTag = "r1";
struct State {
    std::mutex mu;
    std::unique_ptr<Solver> solver;
    string id, cacheBase;
    int sts = 0;
    std::chrono::steady_clock::time_point checked{};
    bool haveChecked = false;
    std::map<string,string> memo;
    Counters counters;
};
State& state() { static State s; return s; }

string file(const string& path) {
    string out;
    if (auto* f = std::fopen(path.c_str(), "rb")) {
        char block[1 << 14]; size_t got;
        while ((got = std::fread(block, 1, sizeof block, f)) > 0 && out.size() < (size_t(16) << 20)) out.append(block, got);
        std::fclose(f);
    }
    return out;
}
bool store(const string& path, const string& data) {
    const string tmp = path + ".tmp";
    auto* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0 || !ok) return false;
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
string indexPath(const string& base) { return base + ".nsig." + programTag + ".idx"; }
string codePath(const string& base, const string& id) { return base + ".nsig." + programTag + "." + id + ".bc"; }
bool validId(const string& id) {
    return id.size() == 8 && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c); });
}
int digitsAfter(const string& s, const char* key) {
    auto at = s.find(key);
    if (at == string::npos) return 0;
    long long n = 0; size_t i = at + std::char_traits<char>::length(key), d = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i]) && d < 10) { n = n * 10 + (s[i] - '0'); ++i; ++d; }
    return n > 0 && n < 100000000 ? int(n) : 0;
}
string replaceId(string path, const string& id) {
    auto at = path.find("{id}");
    if (at != string::npos) path.replace(at, 4, id);
    return path;
}
// Lê "<id> <sts>" do índice.
bool readIndex(const string& base, string& id, int& sts) {
    char word[16]{}; int n = 0;
    auto* f = base.empty() ? nullptr : std::fopen(indexPath(base).c_str(), "rb");
    if (!f) return false;
    bool ok = std::fscanf(f, "%15s%d", word, &n) == 2;
    std::fclose(f);
    id = word; sts = n;
    return ok && validId(id) && n > 0;
}
string fetchId(Transport& net, const updates::Rules& rules) {
    Request api; api.url = rules.s("webOrigin") + "/iframe_api"; api.agent = rules.s("browserAgent");
    auto r = net.send(api);
    return r.error.empty() && r.status == 200 ? playerIdFrom(r.body) : string{};
}
// Garante um programa carregado para a versão atual do player. Chamar com o mutex tomado.
string ensure(State& st, Transport& net, const updates::Rules& rules, const string& base) {
    using Clock = std::chrono::steady_clock;
    if (rules.n("nsig") == 0) return "disabled";
    const auto now = Clock::now();
    if (st.solver && st.cacheBase == base && st.haveChecked && now - st.checked < std::chrono::hours(6)) return {};
    string id = fetchId(net, rules);
    string cachedId; int cachedSts = 0;
    readIndex(base, cachedId, cachedSts);
    if (id.empty()) id = st.solver ? st.id : cachedId;
    if (!validId(id)) return "player_id";
    if (st.solver && st.id == id) { st.checked = now; st.haveChecked = true; st.cacheBase = base; return {}; }
    auto next = std::make_unique<Solver>();
    int sts = 0;
    string loaded;
    if (cachedId == id && cachedSts > 0 && !base.empty()) {
        auto code = file(codePath(base, id));
        if (!code.empty() && next->loadBytecode(code).empty()) { sts = cachedSts; loaded = "bytecode"; ++st.counters.bytecodeLoads; }
        else next = std::make_unique<Solver>();
    }
    if (loaded.empty()) {
        Request req; req.url = rules.s("webOrigin") + replaceId(rules.s("tvPlayerPath"), id); req.agent = rules.s("browserAgent");
        req.maxBytes = size_t(6) << 20;
        auto js = net.send(req);
        if (!js.error.empty() || js.status != 200) return js.error.empty() ? "download_" + std::to_string(js.status) : "download_" + js.error.substr(0, 40);
        if (js.body.size() < 100000) return "download_short";
        ++st.counters.downloads;
        for (const auto& marker : rules.timestampMarkers) { sts = digitsAfter(js.body, marker.c_str()); if (sts) break; }
        if (!sts) return "reduce_timestamp";
        Reduced red;
        if (auto e = reduce(js.body, red); !e.empty()) return "reduce_" + e.substr(0, 40);
        js.body.clear(); js.body.shrink_to_fit();
        string code;
        if (Solver compiler; compiler.compile(red, code).empty() && next->loadBytecode(code).empty()) {
            if (!base.empty() && store(codePath(base, id), code)) {
                FILE* idx = std::fopen((indexPath(base) + ".tmp").c_str(), "wb");
                if (idx) {
                    bool ok = std::fprintf(idx, "%s\n%d\n", id.c_str(), sts) > 0;
                    if (std::fclose(idx) == 0 && ok) std::rename((indexPath(base) + ".tmp").c_str(), indexPath(base).c_str());
                }
                if (!cachedId.empty() && cachedId != id) std::remove(codePath(base, cachedId).c_str());
            }
        } else {
            next = std::make_unique<Solver>();
            if (auto e = next->load(red); !e.empty()) return "load_" + e.substr(0, 60);
        }
    }
    st.solver = std::move(next); st.id = id; st.sts = sts; st.cacheBase = base; st.checked = now; st.haveChecked = true;
    st.memo.clear();
    return {};
}
}

string nValue(const string& url) {
    for (const char* key : {"&n=", "?n="}) {
        auto at = url.find(key);
        if (at == string::npos) continue;
        auto from = at + 3, end = url.find_first_of("&#", from);
        return url.substr(from, end == string::npos ? string::npos : end - from);
    }
    return {};
}
string withN(const string& url, const string& value) {
    for (const char* key : {"&n=", "?n="}) {
        auto at = url.find(key);
        if (at == string::npos) continue;
        auto from = at + 3, end = url.find_first_of("&#", from);
        return url.substr(0, from) + value + (end == string::npos ? string{} : url.substr(end));
    }
    return url;
}
string playerIdFrom(const string& body) {
    // O iframe_api traz a versão com barras escapadas: s\/player\/<versão>\/.
    const string mark = "s\\/player\\/";
    auto at = body.find(mark);
    if (at == string::npos) return {};
    auto from = at + mark.size(), end = from;
    while (end < body.size() && end - from < 16 && std::isalnum((unsigned char)body[end])) ++end;
    auto id = body.substr(from, end - from);
    return validId(id) ? id : string{};
}
string rewrite(Transport& net, const updates::Rules& rules, const string& base, const std::vector<string*>& urls) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mu);
    bool needed = false;
    for (auto* u : urls) if (u && !nValue(*u).empty()) { needed = true; break; }
    if (!needed) return {};
    if (auto e = ensure(st, net, rules, base); !e.empty()) return e;
    std::map<string,string> solved;
    for (auto* u : urls) {
        if (!u) continue;
        auto n = nValue(*u);
        if (n.empty() || solved.count(n)) continue;
        auto memo = st.memo.find(n);
        if (memo != st.memo.end()) { solved[n] = memo->second; ++st.counters.memoHits; continue; }
        string out;
        if (auto e = st.solver->solve(n, out); !e.empty()) return "solve_" + e.substr(0, 60);
        ++st.counters.solves;
        if (st.memo.size() >= 64) st.memo.clear();
        st.memo[n] = solved[n] = out;
    }
    for (auto* u : urls) if (u) { auto n = nValue(*u); if (!n.empty()) *u = withN(*u, solved[n]); }
    return {};
}
int timestamp(Transport& net, const updates::Rules& rules, const string& base) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mu);
    return ensure(st, net, rules, base).empty() ? st.sts : 0;
}
void resetForTest() { auto& st = state(); std::lock_guard<std::mutex> lock(st.mu); st.solver.reset(); st.id.clear(); st.memo.clear(); st.haveChecked = false; st.counters = {}; st.sts = 0; }
Counters counters() { auto& st = state(); std::lock_guard<std::mutex> lock(st.mu); return st.counters; }
}
