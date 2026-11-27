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
    string code;   // bytecode do player atual, só quando o disco não deixou gravar (cache somente leitura ou cheio)
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
    if (std::fclose(f) != 0) ok = false;
    if (ok && std::rename(tmp.c_str(), path.c_str()) == 0) return true;
    std::remove(tmp.c_str());
    return false;
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
double ms(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
// Garante a versão do player (id + signatureTimestamp) e, se `needSolver`, um motor carregado. Chamar com o mutex tomado.
// O motor só sobe quando um `n` precisa ser resolvido; o timestamp sai do índice em disco, sem carregar nada.
string ensure(State& st, Transport& net, const updates::Rules& rules, const string& base, bool needSolver) {
    using Clock = std::chrono::steady_clock;
    if (rules.n("nsig") == 0) return "disabled";
    const auto began = Clock::now();
    const auto now = began;
    const bool fresh = st.haveChecked && st.cacheBase == base && now - st.checked < std::chrono::hours(6);
    if (fresh && st.sts && (!needSolver || st.solver)) return {};
    string id = fresh ? st.id : fetchId(net, rules);
    string cachedId; int cachedSts = 0;
    readIndex(base, cachedId, cachedSts);
    if (id.empty()) id = !st.id.empty() ? st.id : cachedId;
    if (!validId(id)) return "player_id";
    if (net.cancelled()) return "cancelled";
    // Mesma versão do que já sabemos: nada a baixar. Só o motor pode faltar (liberado depois do uso).
    const bool known = st.sts && st.id == id && st.cacheBase == base;
    const bool onDisk = cachedId == id && cachedSts > 0 && !base.empty();
    if ((known || onDisk) && !needSolver) {
        if (!known) { st.id = id; st.sts = cachedSts; st.cacheBase = base; st.memo.clear(); }
        st.checked = now; st.haveChecked = true; return {};
    }
    if (known || onDisk) {
        auto code = onDisk ? file(codePath(base, id)) : string{};
        if (code.empty() && known) code = st.code;
        auto next = std::make_unique<Solver>();
        if (!code.empty() && next->loadBytecode(code).empty()) {
            ++st.counters.bytecodeLoads;
            if (!known) { st.memo.clear(); }
            st.solver = std::move(next); st.id = id; st.sts = known ? st.sts : cachedSts; st.cacheBase = base; st.checked = now; st.haveChecked = true;
            std::fprintf(stderr, "yt: nsig bytecode %s carregado em %.0f ms\n", id.c_str(), ms(began));
            return {};
        }
    }
    // Versão nova (ou cache ilegível): baixa, reduz, compila e grava. Cada fase olha o cancelamento.
    auto next = std::make_unique<Solver>();
    Request req; req.url = rules.s("webOrigin") + replaceId(rules.s("tvPlayerPath"), id); req.agent = rules.s("browserAgent");
    req.maxBytes = size_t(6) << 20;
    auto js = net.send(req);
    if (!js.error.empty() || js.status != 200) return js.error.empty() ? "download_" + std::to_string(js.status) : "download_" + js.error.substr(0, 40);
    if (js.body.size() < 100000) return "download_short";
    if (net.cancelled()) return "cancelled";
    ++st.counters.downloads;
    const double downloaded = ms(began);
    int sts = 0;
    for (const auto& marker : rules.timestampMarkers) { sts = digitsAfter(js.body, marker.c_str()); if (sts) break; }
    if (!sts) return "reduce_timestamp";
    Reduced red;
    if (auto e = reduce(js.body, red); !e.empty()) return "reduce_" + e.substr(0, 40);
    js.body.clear(); js.body.shrink_to_fit();
    if (net.cancelled()) return "cancelled";
    const double reduced = ms(began);
    string code;
    if (Solver compiler; compiler.compile(red, code).empty() && !net.cancelled() && next->loadBytecode(code).empty()) {
        st.code.clear();
        if (base.empty() || !store(codePath(base, id), code)) st.code = code;   // sem disco: o programa fica na memória da sessão
        else {
            const string tmp = indexPath(base) + ".tmp";
            if (FILE* idx = std::fopen(tmp.c_str(), "wb")) {
                bool ok = std::fprintf(idx, "%s\n%d\n", id.c_str(), sts) > 0;
                if (std::fclose(idx) == 0 && ok && std::rename(tmp.c_str(), indexPath(base).c_str()) == 0) {
                    if (!cachedId.empty() && cachedId != id) std::remove(codePath(base, cachedId).c_str());
                } else std::remove(tmp.c_str());
            }
        }
    } else {
        if (net.cancelled()) return "cancelled";
        next = std::make_unique<Solver>();
        if (auto e = next->load(red); !e.empty()) return "load_" + e.substr(0, 60);
    }
    st.solver = std::move(next); st.id = id; st.sts = sts; st.cacheBase = base; st.checked = now; st.haveChecked = true;
    st.memo.clear();
    std::fprintf(stderr, "yt: nsig player %s novo: baixar %.0f ms, reduzir %.0f ms, total %.0f ms, bytecode %zu bytes, timestamp %d\n", id.c_str(), downloaded, reduced - downloaded, ms(began), code.size(), sts);
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
    // Todos os valores já resolvidos nesta sessão (renovação da mesma URL): nem o player nem o motor são necessários.
    bool allKnown = rules.n("nsig") != 0 && st.haveChecked && st.cacheBase == base;
    for (auto* u : urls) if (allKnown && u) { auto n = nValue(*u); if (!n.empty() && !st.memo.count(n)) allKnown = false; }
    if (!allKnown) if (auto e = ensure(st, net, rules, base, true); !e.empty()) return e;
    std::map<string,string> solved;
    for (auto* u : urls) {
        if (!u) continue;
        auto n = nValue(*u);
        if (n.empty() || solved.count(n)) continue;
        auto memo = st.memo.find(n);
        if (memo != st.memo.end()) { solved[n] = memo->second; ++st.counters.memoHits; continue; }
        string out;
        const auto t = std::chrono::steady_clock::now();
        if (auto e = st.solver->solve(n, out); !e.empty()) { st.solver.reset(); return "solve_" + e.substr(0, 60); }
        std::fprintf(stderr, "yt: nsig n %s -> %s em %.1f ms\n", n.c_str(), out.c_str(), ms(t));
        ++st.counters.solves;
        if (st.memo.size() >= 64) st.memo.clear();
        st.memo[n] = solved[n] = out;
    }
    for (auto* u : urls) if (u) { auto n = nValue(*u); if (!n.empty()) *u = withN(*u, solved[n]); }
    // O motor ocupa ~5 MB e só serve nestas chamadas: libera. Voltar custa o bytecode do cache (~100 ms no Xbox).
    if (st.solver && (!st.cacheBase.empty() || !st.code.empty())) st.solver.reset();
    return {};
}
int timestamp(Transport& net, const updates::Rules& rules, const string& base) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mu);
    return ensure(st, net, rules, base, false).empty() ? st.sts : 0;
}
void resetForTest() { auto& st = state(); std::lock_guard<std::mutex> lock(st.mu); st.solver.reset(); st.id.clear(); st.code.clear(); st.memo.clear(); st.haveChecked = false; st.counters = {}; st.sts = 0; }
Counters counters() { auto& st = state(); std::lock_guard<std::mutex> lock(st.mu); return st.counters; }
}
