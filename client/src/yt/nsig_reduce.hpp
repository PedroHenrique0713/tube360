// Reduz o JS do player do YouTube ao que o desafio `n` precisa, sem parser completo.
// O player (2,5 MB) é um IIFE de ~13 mil instruções de topo. Tokeniza, separa as instruções, descarta as de
// efeito colateral (chamadas, if, try, como o yt-dlp-ejs) e mantém só o fecho de dependências do construtor de
// URL (a função que faz `x.set("alr","yes")`). O resultado é um programa menor que o motor JS avalia.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yt::nsig {

struct Reduced {
    std::string program, entry;
    size_t units = 0, kept = 0, tokens = 0;
    double tokenizeMs = 0, splitMs = 0, closureMs = 0;
};

namespace detail {
enum : uint8_t { IDENT, NUMBER, STRING, REGEX, TEMPLATE, PUNCT };
struct Tok { uint32_t start; uint32_t len : 24; uint32_t type : 8; };
static_assert(sizeof(Tok) == 8);

inline bool idStart(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '$' || c == '_' || c >= 0x80; }
inline bool idPart(unsigned char c) { return idStart(c) || (c >= '0' && c <= '9'); }

struct Lexer {
    const char* s; size_t n;
    std::vector<Tok> toks;
    explicit Lexer(std::string_view src) : s(src.data()), n(src.size()) { toks.reserve(n / 2); }

    bool regexAllowed() const {
        if (toks.empty()) return true;
        const Tok& p = toks.back();
        switch (p.type) {
        case NUMBER: case STRING: case REGEX: case TEMPLATE: return false;
        case IDENT: {
            std::string_view w(s + p.start, p.len);
            static const char* const kw[] = {"return","typeof","instanceof","in","of","new","delete","void","throw","case","do","else","yield","await"};
            for (auto k : kw) if (w == k) return true;
            return false;
        }
        default: { char c = s[p.start]; return !(p.len == 1 && (c == ')' || c == ']')); }
        }
    }
    size_t skipString(size_t i) const {  // i em cima da aspa; devolve o índice depois da aspa final
        char q = s[i++];
        while (i < n && s[i] != q) i += s[i] == '\\' ? 2 : 1;
        return std::min(n, i + 1);
    }
    size_t skipTemplate(size_t i) const {  // i em cima da crase
        ++i;
        while (i < n && s[i] != '`') {
            if (s[i] == '\\') { i += 2; continue; }
            if (s[i] == '$' && i + 1 < n && s[i + 1] == '{') {
                i += 2;
                for (int depth = 1; i < n && depth;) {
                    char c = s[i];
                    if (c == '"' || c == '\'') { i = skipString(i); continue; }
                    if (c == '`') { i = skipTemplate(i); continue; }
                    if (c == '{') ++depth; else if (c == '}') --depth;
                    ++i;
                }
                continue;
            }
            ++i;
        }
        return std::min(n, i + 1);
    }
    void push(uint8_t type, size_t a, size_t b) { toks.push_back(Tok{uint32_t(a), uint32_t(b - a), type}); }
    size_t operatorLength(size_t i) const {
        // Só os operadores que contêm '=', '.', '?' ou repetem o caractere importam: o resto vira um token de 1 char.
        auto at = [&](size_t k) -> char { return i + k < n ? s[i + k] : 0; };
        const char c = s[i], c1 = at(1), c2 = at(2);
        switch (c) {
        case '=': return c1 == '>' ? 2 : c1 == '=' ? (c2 == '=' ? 3 : 2) : 1;
        case '!': return c1 == '=' ? (c2 == '=' ? 3 : 2) : 1;
        case '<': return c1 == '<' ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1;
        case '>': if (c1 == '>') return c2 == '>' ? (at(3) == '=' ? 4 : 3) : c2 == '=' ? 3 : 2; return c1 == '=' ? 2 : 1;
        case '+': case '-': return c1 == c || c1 == '=' ? 2 : 1;
        case '*': return c1 == '*' ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1;
        case '&': case '|': return c1 == c ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1;
        case '?': return c1 == '?' ? (c2 == '=' ? 3 : 2) : c1 == '.' && !(c2 >= '0' && c2 <= '9') ? 2 : 1;
        case '.': return c1 == '.' && c2 == '.' ? 3 : 1;
        case '/': case '%': case '^': return c1 == '=' ? 2 : 1;
        default: return 1;
        }
    }
    void run() {
        size_t i = 0;
        while (i < n) {
            unsigned char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
            if (c == '/') {
                char d = i + 1 < n ? s[i + 1] : 0;
                if (d == '/') { while (i < n && s[i] != '\n') ++i; continue; }
                if (d == '*') { auto e = std::string_view(s, n).find("*/", i + 2); i = e == std::string_view::npos ? n : e + 2; continue; }
                if (regexAllowed()) {
                    size_t j = i + 1; bool cls = false;
                    for (; j < n; ++j) {
                        char e = s[j];
                        if (e == '\\') { ++j; continue; }
                        if (e == '[') cls = true; else if (e == ']') cls = false; else if (e == '/' && !cls) break;
                    }
                    ++j; while (j < n && idPart(s[j])) ++j;
                    push(REGEX, i, std::min(j, n)); i = std::min(j, n); continue;
                }
            }
            if (c == '"' || c == '\'') { size_t j = skipString(i); push(STRING, i, j); i = j; continue; }
            if (c == '`') { size_t j = skipTemplate(i); push(TEMPLATE, i, j); i = j; continue; }
            if (idStart(c)) { size_t j = i + 1; while (j < n && idPart(s[j])) ++j; push(IDENT, i, j); i = j; continue; }
            if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9')) {
                size_t j = i + 1; bool hex = c == '0' && j < n && (s[j] == 'x' || s[j] == 'X');
                while (j < n) {
                    unsigned char e = s[j];
                    if (idPart(e) || e == '.') ++j;
                    else if ((e == '+' || e == '-') && !hex && (s[j - 1] | 32) == 'e') ++j;
                    else break;
                }
                push(NUMBER, i, j); i = j; continue;
            }
            size_t l = operatorLength(i); push(PUNCT, i, i + l); i += l;
        }
    }
};
}  // namespace detail

// Devolve "" em sucesso, ou um código curto do que faltou. `js` precisa ficar vivo só durante a chamada.
inline std::string reduce(std::string_view js, Reduced& out) {
    using namespace detail;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
    out = {};
    const auto t0 = Clock::now();
    Lexer lx(js); lx.run();
    out.tokenizeMs = ms(t0);
    const auto& tk = lx.toks;
    const char* s = js.data();
    out.tokens = tk.size();
    auto text = [&](size_t k) { return std::string_view(s + tk[k].start, tk[k].len); };
    auto punct = [&](size_t k, char c) { return k < tk.size() && tk[k].type == PUNCT && tk[k].len == 1 && s[tk[k].start] == c; };
    auto word = [&](size_t k, std::string_view w) { return k < tk.size() && tk[k].type == IDENT && text(k) == w; };
    auto endOf = [&](size_t k) { return size_t(tk[k].start) + tk[k].len; };

    // Corpo do IIFE: `(function(...){ ... })` nos primeiros tokens.
    size_t fn = 0;
    for (; fn < std::min<size_t>(tk.size(), 64); ++fn) if (word(fn, "function") && fn && punct(fn - 1, '(')) break;
    if (fn >= std::min<size_t>(tk.size(), 64)) return "iife_missing";
    size_t open = fn; while (open < tk.size() && !punct(open, '{')) ++open;
    size_t close = tk.size();
    {
        int depth = 0;
        for (size_t k = open; k < tk.size(); ++k) {
            if (tk[k].type != PUNCT || tk[k].len != 1) continue;
            char c = s[tk[k].start];
            if (c == '{' || c == '(' || c == '[') ++depth;
            else if (c == '}' || c == ')' || c == ']') { if (--depth == 0) { close = k; break; } }
        }
    }
    if (close >= tk.size()) return "iife_unbalanced";
    const size_t bodyStart = open + 1, bodyEnd = close;

    // Identificadores internados e as unidades (declarador, função, classe ou atribuição de topo).
    std::unordered_map<std::string_view, uint32_t> ids; ids.reserve(1 << 17);
    std::vector<std::string_view> idText;
    auto intern = [&](std::string_view w) {
        auto it = ids.find(w);
        if (it != ids.end()) return it->second;
        uint32_t id = uint32_t(idText.size()); ids.emplace(w, id); idText.push_back(w); return id;
    };
    struct Unit { uint32_t a, b, def, refA, refB; uint8_t kind; };  // kind: 0 fn/class/assign, 1 var, 2 let, 3 const, 4 sempre
    std::vector<Unit> units; std::vector<uint32_t> refs; std::vector<uint32_t> stamp;
    auto collect = [&](size_t a, size_t b, uint32_t unit) {
        auto mark = [&](std::string_view w) {
            uint32_t id = intern(w);
            if (stamp.size() <= id) stamp.resize(size_t(id) * 2 + 16, UINT32_MAX);
            if (stamp[id] != unit) { stamp[id] = unit; refs.push_back(id); }
        };
        for (size_t q = a; q < b; ++q) {
            if (tk[q].type == TEMPLATE) {
                auto t = text(q);
                for (size_t i = 0; i + 1 < t.size(); ++i) if (t[i] == '$' && t[i + 1] == '{') {
                    int depth = 1; size_t j = i + 2;
                    for (; j < t.size() && depth; ++j) {
                        if (t[j] == '{') ++depth; else if (t[j] == '}') --depth;
                        else if (idStart(t[j]) && (j == i + 2 || !idPart(t[j - 1])) && (j == 0 || t[j - 1] != '.')) {
                            size_t e = j; while (e < t.size() && idPart(t[e])) ++e;
                            mark(t.substr(j, e - j)); j = e - 1;
                        }
                    }
                    i = j;
                }
                continue;
            }
            if (tk[q].type != IDENT || (q > 0 && punct(q - 1, '.'))) continue;
            mark(text(q));
        }
    };
    auto addUnit = [&](uint32_t a, uint32_t b, uint32_t def, size_t ta, size_t tb, uint8_t kind) {
        uint32_t u = uint32_t(units.size());
        uint32_t r0 = uint32_t(refs.size());
        if (ta < tb) collect(ta, tb, u);
        units.push_back({a, b, def, r0, uint32_t(refs.size()), kind});
    };

    size_t q = bodyStart;
    while (q < bodyEnd) {
        if (punct(q, ';')) { ++q; continue; }
        const size_t st = q;
        const bool blockKw = tk[q].type == IDENT && (word(q, "function") || word(q, "if") || word(q, "for") || word(q, "while") ||
            word(q, "try") || word(q, "class") || word(q, "switch") || word(q, "with"));
        size_t end = bodyEnd; int depth = 0;
        for (; q < bodyEnd; ++q) {
            if (tk[q].type != PUNCT || tk[q].len != 1) continue;
            char c = s[tk[q].start];
            if (c == '{' || c == '(' || c == '[') ++depth;
            else if (c == '}' || c == ')' || c == ']') {
                if (--depth == 0 && c == '}' && blockKw) {
                    if (q + 1 < bodyEnd && (word(q + 1, "else") || word(q + 1, "catch") || word(q + 1, "finally"))) continue;
                    end = ++q; break;
                }
            } else if (c == ';' && depth == 0) { end = ++q; break; }
        }
        if (q >= bodyEnd) end = bodyEnd;
        const size_t last = punct(end - 1, ';') ? end - 1 : end;  // fim do texto, sem o ';'
        if (tk[st].type == STRING) { addUnit(tk[st].start, uint32_t(endOf(end - 1)), UINT32_MAX, 0, 0, 4); continue; }
        if ((word(st, "function") || word(st, "class")) && st + 1 < end && tk[st + 1].type == IDENT) {
            addUnit(tk[st].start, uint32_t(endOf(last - 1)), intern(text(st + 1)), st + 2, last, 0); continue;
        }
        if (word(st, "var") || word(st, "let") || word(st, "const")) {
            const uint8_t kind = word(st, "var") ? 1 : word(st, "let") ? 2 : 3;
            size_t ds = st + 1; int d = 0;
            auto flush = [&](size_t de) {
                if (de <= ds || tk[ds].type != IDENT) return;
                bool init = de > ds + 1 && punct(ds + 1, '=');
                addUnit(tk[ds].start, uint32_t(endOf(de - 1)), intern(text(ds)), init ? ds + 2 : 0, init ? de : 0, kind);
            };
            for (size_t k = st + 1; k < last; ++k) {
                if (tk[k].type != PUNCT || tk[k].len != 1) continue;
                char c = s[tk[k].start];
                if (c == '{' || c == '(' || c == '[') ++d; else if (c == '}' || c == ')' || c == ']') --d;
                else if (c == ',' && d == 0) { flush(k); ds = k + 1; }
            }
            flush(last);
            continue;
        }
        // atribuição: ident(.ident | [..])* '=' ...
        if (tk[st].type == IDENT) {
            size_t k = st + 1;
            while (k < last) {
                if (punct(k, '.') && k + 1 < last && tk[k + 1].type == IDENT) k += 2;
                else if (punct(k, '[')) { int d = 0; do { if (punct(k, '[')) ++d; else if (punct(k, ']')) --d; ++k; } while (d && k < last); }
                else break;
            }
            if (k < last && punct(k, '=')) {
                addUnit(tk[st].start, uint32_t(endOf(last - 1)), intern(text(st)), st, last, 0);
                continue;
            }
        }
        // chamadas, if, for, try, rótulos, sequências: descartados (o yt-dlp-ejs também). Os `var` que eles declaram
        // (`for(var a={},b;...)`) continuam existindo no escopo do IIFE; sem a declaração, uma atribuição `a=...` em
        // modo estrito falha com ReferenceError. Declara os nomes, sem o valor.
        for (size_t k = st; k < last; ++k) {
            if (word(k, "function")) {  // pula parâmetros e corpo: `var` de dentro não é do escopo de topo
                while (k < last && !punct(k, '{')) ++k;
                for (int d = 0; k < last; ++k) { if (punct(k, '{')) ++d; else if (punct(k, '}') && --d == 0) break; }
                continue;
            }
            if (!word(k, "var")) continue;
            for (size_t m = k + 1; m < last;) {
                if (tk[m].type != IDENT) break;
                addUnit(tk[m].start, uint32_t(endOf(m)), intern(text(m)), 0, 0, 1);
                int d = 0; ++m;
                for (; m < last; ++m) {  // até a próxima vírgula de topo da lista
                    if (tk[m].type == PUNCT && tk[m].len == 1) {
                        char c = s[tk[m].start];
                        if (c == '(' || c == '[' || c == '{') ++d;
                        else if (c == ')' || c == ']' || c == '}') { if (d == 0) break; --d; }
                        else if (c == ';' && d == 0) break;
                        else if (c == ',' && d == 0) { ++m; break; }
                    } else if (d == 0 && (word(m, "in") || word(m, "of"))) break;
                }
                if (m >= last || !punct(m - 1, ',')) break;
            }
        }
    }
    out.units = units.size();
    out.splitMs = ms(t0) - out.tokenizeMs;
    const auto t1 = Clock::now();

    // Ponto de entrada: a função que chama `x.set("alr","yes");` direto no corpo.
    constexpr std::string_view needle = ".set(\"alr\",\"yes\");";
    std::string_view body(s + tk[bodyStart].start, endOf(bodyEnd - 1) - tk[bodyStart].start);
    uint32_t entry = UINT32_MAX;
    for (size_t at = body.find(needle); at != std::string_view::npos && entry == UINT32_MAX; at = body.find(needle, at + 1)) {
        size_t p = at; while (p && idPart(body[p - 1])) --p;
        if (p == at || p == 0 || (body[p - 1] != '{' && body[p - 1] != ';')) continue;
        const uint32_t pos = uint32_t(body.data() - s + at);
        auto it = std::upper_bound(units.begin(), units.end(), pos, [](uint32_t v, const Unit& u) { return v < u.a; });
        if (it == units.begin()) continue;
        --it;
        if (pos < it->b && it->def != UINT32_MAX) entry = it->def;
    }
    if (entry == UINT32_MAX) return "entry_missing";
    out.entry = std::string(idText[entry]);

    // Fecho de dependências: lista encadeada nome -> unidades que o definem.
    std::vector<int32_t> head(idText.size(), -1), nextUnit(units.size(), -1);
    for (uint32_t u = 0; u < units.size(); ++u) if (units[u].def != UINT32_MAX) { nextUnit[u] = head[units[u].def]; head[units[u].def] = int32_t(u); }
    std::vector<uint8_t> need(idText.size(), 0), keep(units.size(), 0);
    std::vector<uint32_t> work{entry}; need[entry] = 1;
    while (!work.empty()) {
        uint32_t name = work.back(); work.pop_back();
        for (int32_t u = head[name]; u >= 0; u = nextUnit[u]) {
            if (keep[u]) continue;
            keep[u] = 1; ++out.kept;
            for (uint32_t r = units[u].refA; r < units[u].refB; ++r) {
                uint32_t id = refs[r];
                if (id < need.size() && !need[id] && head[id] >= 0) { need[id] = 1; work.push_back(id); }
            }
        }
    }
    static const char* const kw[] = {"", "var ", "let ", "const ", ""};
    for (uint32_t u = 0; u < units.size(); ++u) {
        if (!keep[u] && units[u].kind != 4) continue;
        out.program += kw[units[u].kind];
        out.program.append(s + units[u].a, units[u].b - units[u].a);
        out.program += ";\n";
    }
    out.closureMs = ms(t1);
    return {};
}

}  // namespace yt::nsig
