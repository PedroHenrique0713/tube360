#include "nsig.hpp"
extern "C" {
#include "../../third_party/quickjs-ng/quickjs.h"
}
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace yt::nsig {
namespace {
// O que o player espera encontrar num navegador. O resto do ambiente (DOM, rede) nunca é tocado: só o fecho do construtor de URL roda.
constexpr const char* stubs = R"JS(
if (typeof globalThis.XMLHttpRequest === "undefined") globalThis.XMLHttpRequest = { prototype: {} };
globalThis.location = { hash: "", host: "www.youtube.com", hostname: "www.youtube.com", href: "https://www.youtube.com/watch?v=tube360",
  origin: "https://www.youtube.com", password: "", pathname: "/watch", port: "", protocol: "https:", search: "?v=tube360", username: "" };
if (typeof globalThis.document === "undefined") globalThis.document = Object.create(null);
if (typeof globalThis.navigator === "undefined") globalThis.navigator = Object.create(null);
if (typeof globalThis.self === "undefined") globalThis.self = globalThis;
if (typeof globalThis.window === "undefined") globalThis.window = globalThis;
)JS";
using Clock = std::chrono::steady_clock;
struct Deadline { Clock::time_point at; };
int interrupt(JSContext*, void* opaque) { return Clock::now() > static_cast<Deadline*>(opaque)->at; }
std::string describe(JSContext* ctx, const char* prefix) {
    JSValue ex = JS_GetException(ctx);
    const char* text = JS_ToCString(ctx, ex);
    std::string out = std::string(prefix) + ":" + (text ? text : "?");
    if (text) JS_FreeCString(ctx, text);
    if (std::getenv("NSIG_STACK")) {
        JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
        if (const char* st = JS_ToCString(ctx, stack)) { out += std::string(" @ ") + st; JS_FreeCString(ctx, st); }
        JS_FreeValue(ctx, stack);
    }
    JS_FreeValue(ctx, ex);
    for (auto& c : out) if (c == '\n' || c == '\r') c = ' ';
    return out.substr(0, std::getenv("NSIG_STACK") ? 2000 : 240);
}
std::string script(const Reduced& r) {
    // O construtor de URL e o gancho que dispara a transformação de `n` (o primeiro método de protótipo que não é get/set/clone).
    std::string out = stubs;
    out += "(function(){\n" + r.program + "\nglobalThis.__solve=function(n){var u=" + r.entry +
        "(\"https://youtube.com/watch?v=tube360\",\"s\",undefined);u.set(\"n\",n);var p=Object.getPrototypeOf(u),"
        "k=Object.keys(p).concat(Object.getOwnPropertyNames(p));for(var i=0;i<k.length;i++){if(k[i]!==\"constructor\"&&k[i]!==\"set\"&&"
        "k[i]!==\"get\"&&k[i]!==\"clone\"){u[k[i]]();break}}return u.get(\"n\")};\n}).call(globalThis);";
    return out;
}
}

struct Solver::Impl {
    JSRuntime* rt = nullptr; JSContext* ctx = nullptr; JSValue fn = JS_UNDEFINED; Deadline deadline{};
    ~Impl() { if (ctx) { JS_FreeValue(ctx, fn); JS_FreeContext(ctx); } if (rt) JS_FreeRuntime(rt); }
};
Solver::Solver() : impl(new Impl) {}
Solver::~Solver() = default;

std::string Solver::start(unsigned timeoutMs) {
    auto& m = *impl;
    m.rt = JS_NewRuntime();
    if (!m.rt) return "no_runtime";
    JS_SetMemoryLimit(m.rt, size_t(96) << 20);
    JS_SetMaxStackSize(m.rt, size_t(512) << 10);
    JS_SetInterruptHandler(m.rt, interrupt, &m.deadline);
    m.ctx = JS_NewContext(m.rt);
    if (!m.ctx) return "no_context";
    m.deadline.at = Clock::now() + std::chrono::milliseconds(timeoutMs);
    return {};
}

std::string Solver::finish() {
    auto& m = *impl;
    JSValue g = JS_GetGlobalObject(m.ctx);
    m.fn = JS_GetPropertyStr(m.ctx, g, "__solve");
    JS_FreeValue(m.ctx, g);
    return JS_IsFunction(m.ctx, m.fn) ? std::string() : "entry_not_callable";
}

std::string Solver::load(const Reduced& r, unsigned timeoutMs) {
    if (auto e = start(timeoutMs); !e.empty()) return e;
    const std::string text = script(r);
    if (const char* dump = std::getenv("NSIG_DUMP")) if (FILE* f = std::fopen(dump, "wb")) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
    JSValue v = JS_Eval(impl->ctx, text.data(), text.size(), "player", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) return describe(impl->ctx, "eval_failed");
    JS_FreeValue(impl->ctx, v);
    return finish();
}

std::string Solver::compile(const Reduced& r, std::string& bytecode) {
    if (auto e = start(20000); !e.empty()) return e;
    const std::string text = script(r);
    JSValue v = JS_Eval(impl->ctx, text.data(), text.size(), "player", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(v)) return describe(impl->ctx, "compile_failed");
    size_t len = 0;
    uint8_t* raw = JS_WriteObject(impl->ctx, &len, v, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(impl->ctx, v);
    if (!raw) return "write_failed";
    bytecode.assign(reinterpret_cast<const char*>(raw), len);
    js_free(impl->ctx, raw);
    return {};
}

std::string Solver::loadBytecode(const std::string& bytecode, unsigned timeoutMs) {
    if (auto e = start(timeoutMs); !e.empty()) return e;
    JSValue f = JS_ReadObject(impl->ctx, reinterpret_cast<const uint8_t*>(bytecode.data()), bytecode.size(), JS_READ_OBJ_BYTECODE);
    if (JS_IsException(f)) return describe(impl->ctx, "read_failed");
    JSValue v = JS_EvalFunction(impl->ctx, f);
    if (JS_IsException(v)) return describe(impl->ctx, "eval_failed");
    JS_FreeValue(impl->ctx, v);
    return finish();
}

std::string Solver::solve(const std::string& n, std::string& out, unsigned timeoutMs) {
    auto& m = *impl;
    out.clear();
    if (!m.ctx) return "not_loaded";
    m.deadline.at = Clock::now() + std::chrono::milliseconds(timeoutMs);
    JSValue arg = JS_NewStringLen(m.ctx, n.data(), n.size());
    JSValue res = JS_Call(m.ctx, m.fn, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(m.ctx, arg);
    if (JS_IsException(res)) return describe(m.ctx, "solve_failed");
    size_t len = 0; const char* text = JS_IsString(res) ? JS_ToCStringLen(m.ctx, &len, res) : nullptr;
    if (!text) { JS_FreeValue(m.ctx, res); return "bad_result"; }
    out.assign(text, len);
    JS_FreeCString(m.ctx, text); JS_FreeValue(m.ctx, res);
    // A transformação do player devolve o próprio `n` ou um texto com `enhanced_except_` quando algo do ambiente falta.
    if (out.empty() || out.size() > 64 || out.find("enhanced_except") != std::string::npos) return "bad_result";
    for (unsigned char c : out) if (!(std::isalnum(c) || c == '_' || c == '-')) return "bad_result";
    return out == n ? "same_value" : std::string();
}

size_t Solver::memoryBytes() const {
    if (!impl->rt) return 0;
    JSMemoryUsage u; JS_ComputeMemoryUsage(impl->rt, &u);
    return size_t(u.malloc_size);
}
}
