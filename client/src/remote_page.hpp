// Controle pelo celular: a página única (HTML, CSS e JS inline, sem CDN) servida pelo console. Os textos seguem o idioma do app.
#pragma once
#include <string>
#include "i18n.hpp"

namespace phonectl {

// Ordem: título, instrução do código, entrar, código errado, bloqueado, buscar (placeholder), buscar (botão), nada tocando, desconectado, pausado, pareamento fechado
inline const char* const kPageText[4][11] = {
  /* EN */ {"Tube360 remote", "Type the code shown on the TV", "Connect", "Wrong code", "Too many tries. Wait a moment and use the new code on the TV", "Search YouTube", "Search", "Nothing playing", "Disconnected. Pair again", "Paused", "Open the pairing screen on the TV first: Settings, Privacy, Phone as remote"},
  /* PT */ {"Controle do Tube360", "Digite o código que aparece na TV", "Conectar", "Código errado", "Muitas tentativas. Espere um pouco e use o código novo da TV", "Buscar no YouTube", "Buscar", "Nada tocando", "Desconectado. Pareie de novo", "Pausado", "Abra a tela de pareamento na TV antes: Configurações, Privacidade, Celular como controle"},
  /* ES */ {"Control de Tube360", "Escribe el código que aparece en la TV", "Conectar", "Código incorrecto", "Demasiados intentos. Espera un momento y usa el código nuevo de la TV", "Buscar en YouTube", "Buscar", "Nada en reproducción", "Desconectado. Vuelve a emparejar", "En pausa", "Abre antes la pantalla de emparejamiento en la TV: Configuración, Privacidad, Móvil como control"},
  /* AR */ {"ريموت Tube360", "اكتب الرمز الظاهر على التلفاز", "اتصال", "رمز خاطئ", "محاولات كثيرة. انتظر قليلًا واستخدم الرمز الجديد على التلفاز", "ابحث في يوتيوب", "بحث", "لا شيء قيد التشغيل", "انقطع الاتصال. اقترن من جديد", "متوقف مؤقتًا", "افتح شاشة الاقتران على التلفاز أولًا: الإعدادات، الخصوصية، الهاتف كريموت"},
};

inline std::string page(i18n::Lang lang) {
  int l = lang == i18n::Lang::PT ? 1 : lang == i18n::Lang::ES ? 2 : lang == i18n::Lang::AR ? 3 : 0;
  const char* const* t = kPageText[l];
  std::string js = "const T=[";
  for (int i = 0; i < 11; ++i) { js += "\""; js += t[i]; js += "\","; }
  js += "];";
  std::string html = R"HTML(<!doctype html><html lang=")HTML" + std::string(i18n::code(lang)) + R"HTML(" dir=")HTML" + (l == 3 ? "rtl" : "ltr") + R"HTML("><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><meta name="theme-color" content="#0b0b0e">
<title>Tube360</title><style>
:root{--bg:#0b0b0e;--s1:#18181d;--s2:#24242b;--tx:#f5f5f7;--mu:#9a9aa6;--red:#ff0f3c}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;touch-action:manipulation}
body{margin:0;background:var(--bg);color:var(--tx);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;min-height:100vh;display:flex;justify-content:center}
main{width:100%;max-width:420px;padding:16px;display:flex;flex-direction:column;gap:16px}
h1{font-size:20px;margin:4px 0 0;letter-spacing:.2px}h1 b{color:var(--red)}
.card{background:var(--s1);border-radius:20px;padding:16px}
.now{color:var(--mu);font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.now.on{color:var(--tx)}
input{width:100%;background:var(--s2);color:var(--tx);border:2px solid transparent;border-radius:14px;padding:14px;font-size:18px;outline:0}
input:focus{border-color:var(--red)}
#code{text-align:center;letter-spacing:.35em;font-size:28px}
button{font:inherit;color:var(--tx);background:var(--s2);border:0;border-radius:16px;min-height:56px;font-weight:600;user-select:none;-webkit-user-select:none}
button:active,button.hold{background:#3a3a44}
.go{background:var(--red);width:100%;margin-top:12px}.go:active{background:#c80a2e}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.pad button{min-height:76px;font-size:28px}
.pad .ok{background:#2f9e5b}.pad .ok:active{background:#237a45}
.row{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.row button{font-size:20px;min-height:64px}
.b{color:#ff6b6b}
.sr{display:flex;gap:10px}.sr input{flex:1}.sr button{padding:0 18px}
.err{color:#ff6b6b;min-height:1.4em;margin-top:8px;font-size:14px}
.hide{display:none}
</style></head><body><main>
<h1><b>Tube</b>360</h1>
<section id="pair" class="card"><p id="ask"></p><input id="code" inputmode="numeric" pattern="[0-9]*" maxlength="6" autocomplete="off"><button class="go" id="join"></button><div class="err" id="perr"></div></section>
<section id="ctl" class="hide">
<div class="card"><div class="now" id="now"></div></div>
<div class="pad" style="margin:16px 0"><span></span><button data-a="up" data-r="1">&#9650;</button><span></span>
<button data-a="left" data-r="1">&#9664;</button><button class="ok" data-a="ok">A</button><button data-a="right" data-r="1">&#9654;</button>
<span></span><button data-a="down" data-r="1">&#9660;</button><span></span></div>
<div class="row"><button class="b" data-a="back">B</button><button data-a="playpause">&#9199;</button><span></span>
<button data-a="voldown" data-r="1" data-v="1">&#128265; &minus;</button><span></span><button data-a="volup" data-r="1" data-v="1">&#128266; +</button></div>
<div class="card" style="margin-top:16px"><div class="sr"><input id="q" maxlength="100" enterkeyhint="search" autocomplete="off"><button id="find"></button></div><div class="err" id="cerr"></div></div>
</section></main><script>)HTML" + js + R"HTML(
const $=id=>document.getElementById(id);let tok=null;try{tok=sessionStorage.getItem("t")}catch(e){}
$("ask").textContent=T[1];$("join").textContent=T[2];$("q").placeholder=T[5];$("find").textContent=T[6];document.title=T[0];
function show(p){$("pair").classList.toggle("hide",!p);$("ctl").classList.toggle("hide",p)}
async function api(m,p,b){const h={"X-T360":tok||"pair"};if(b)h["Content-Type"]="application/x-www-form-urlencoded";
 const r=await fetch(p,{method:m,headers:h,body:b,cache:"no-store"});if(r.status===401&&tok){tok=null;try{sessionStorage.removeItem("t")}catch(e){}show(true);$("perr").textContent=T[8]}return r}
$("join").onclick=async()=>{$("perr").textContent="";try{const r=await api("POST","/pair","code="+encodeURIComponent($("code").value.trim()));
 if(r.status===200){tok=(await r.json()).token;try{sessionStorage.setItem("t",tok)}catch(e){}$("code").value="";show(false);poll()}
 else $("perr").textContent=r.status===429?T[4]:r.status===423?T[10]:T[3]}catch(e){$("perr").textContent=T[8]}};
function send(a,q){const b="a="+a+(q!==undefined?"&q="+encodeURIComponent(q):"");return api("POST","/cmd",b).then(r=>{$("cerr").textContent=r.status===429?"…":""}).catch(()=>{})}
document.querySelectorAll("[data-a]").forEach(el=>{let t=null,d=null;const a=el.dataset.a,rep=el.dataset.r;
 const stop=()=>{clearTimeout(t);clearInterval(d);el.classList.remove("hold")};
 el.addEventListener("pointerdown",e=>{e.preventDefault();send(a);if(rep){el.classList.add("hold");t=setTimeout(()=>{d=setInterval(()=>send(a),130)},400)}});
 ["pointerup","pointerleave","pointercancel"].forEach(n=>el.addEventListener(n,stop));el.addEventListener("contextmenu",e=>e.preventDefault())});
function find(){const v=$("q").value.trim();if(v){send("search",v);$("q").value="";$("q").blur()}}
$("find").onclick=find;$("q").addEventListener("keydown",e=>{if(e.key==="Enter")find()});
async function poll(){if(!tok)return;try{const r=await api("GET","/state");if(r.status===200){const s=await r.json();const n=$("now");
 n.textContent=s.playing?(s.title||"")+(s.paused?" · "+T[9]:""):T[7];n.classList.toggle("on",!!s.playing);document.querySelectorAll("[data-v]").forEach(e=>e.style.visibility=s.playing?"visible":"hidden")}}catch(e){}setTimeout(poll,2000)}
if(tok){show(false);poll()}else show(true);
</script></body></html>)HTML";
  return html;
}

}  // namespace phonectl
