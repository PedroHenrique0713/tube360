// Peças de desenho compartilhadas entre as telas: botões do controle, marca, ícones, selos e duração, e os cartões
// (miniatura, andamento, duração, selo de membros e textos) da Home e das páginas de navegação. Vem depois de Thumbs,
// Worker e das páginas (navigation.hpp), que os cartões desenham.
#pragma once
#include "music_shortcut_art.hpp"
#include "controls.hpp"

static std::string fmtTime(double s) {
  if (s < 0) s = 0;
  int t = (int)s, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
  char b[32];
  if (h) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, sec); else std::snprintf(b, sizeof(b), "%d:%02d", m, sec);
  return b;
}

// Estrela de cinco pontas (leque de triângulos) e o "verificado" (círculo com o visto), pequenos, para a linha de texto do cartão.
static void starShape(gfx::Gfx& g, float cx, float cy, float r, Color c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) { float a = -1.5707963f + i * 0.6283185f, k = i % 2 ? r * .42f : r; px[i] = cx + std::cos(a) * k; py[i] = cy + std::sin(a) * k; }
  for (int i = 0; i < 10; i++) gfx::triangle(g, cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
}
static bool cardRtl(gfx::Gfx& g, const std::string& s, int font) { return g.rtlText || gfx::visualLine(g, s, font).rtl; }
// Selo de verificado como o do YouTube de TV: círculo cinza claro com um check escuro. Tudo em pixels inteiros (círculo par) e o check
// é um traço grosso feito de pontos redondos sobrepostos (a borda sai suave, sem triângulos finos e serrilhados).
static float verifiedSize(gfx::Gfx& g, int font) { return std::max(12.f, 2 * std::round(g.fonts[(size_t)font].size * .70f / 2)); }
// y do selo para o texto da fonte `font` que começa em `ty`: o selo fica centrado na altura das maiúsculas, na linha de base do nome.
static float verifiedY(gfx::Gfx& g, int font, float ty, float d) { const auto& f = g.fonts[(size_t)font]; return std::round(ty + f.ascent - f.size * .36f - d / 2); }
static void verifiedMark(gfx::Gfx& g, float x, float y, float d, Color c, Color ink) {
  d = std::max(12.f, 2 * std::round(d / 2)); x = std::round(x); y = std::round(y);
  gfx::rect(g, x, y, d, d, c, d / 2);
  const float t = std::max(1.8f, d * .14f), r = t / 2;
  auto stroke = [&](float u0, float v0, float u1, float v1) {
    float x0 = x + u0 * d, y0 = y + v0 * d, dx = (u1 - u0) * d, dy = (v1 - v0) * d;
    int n = std::max(2, int(std::hypot(dx, dy) / (t * .3f)));
    for (int i = 0; i <= n; ++i) { float k = float(i) / n; gfx::rect(g, x0 + dx * k - r, y0 + dy * k - r, t, t, ink, r); }
  };
  stroke(.28f, .52f, .44f, .67f);
  stroke(.44f, .67f, .73f, .35f);
}
static const Color MEMBER_GREEN = rgb(0x3ddc84);

// Linha "Só para membros" (verde, com estrela) sob o texto do cartão. Só para quem tem Membership; o vídeo já liberado diz "Membro".
static void memberLine(gfx::Gfx& g, yt::Membership members, i18n::Lang lang, float x, float y, float w) {
  if (members == yt::Membership::None) return;
  const char* label = i18n::get(lang, members == yt::Membership::Accessible ? K::Member : K::MembersOnly);
  float r = g.fonts[ui::F_TINY].size * .62f;
  std::string shown = gfx::ellipsize(g, ui::F_TINY, label, w - 2 * r - 8);
  if (cardRtl(g, shown, ui::F_TINY)) {  // árabe: estrela na ponta direita, texto alinhado à direita junto com título e meta
    starShape(g, x + w - r, y + g.fonts[ui::F_TINY].lineH / 2, r, MEMBER_GREEN);
    gfx::text(g, ui::F_TINY, shown, x + w - 2 * r - 6, y, MEMBER_GREEN, 2);
    return;
  }
  starShape(g, x + r, y + g.fonts[ui::F_TINY].lineH / 2, r, MEMBER_GREEN);
  gfx::text(g, ui::F_TINY, shown, x + 2 * r + 6, y, MEMBER_GREEN);
}
// ---------------------------------------------------------------- peças de desenho
static const Color XA = rgb(0x7ed957), XB = rgb(0xff5a52), XX = rgb(0x4ea8ff), XY = rgb(0xffd23f), XG = rgb(0xd4d4d8);

// Botões do controle no estilo do Xbox One/Series (pedido do Pedro: nada do padrão colorido do 360): botão escuro com
// aro fino e a letra colorida; View e Menu como ícones; LB/RB, setas e afins numa pílula escura com texto branco.
static float buttonGlyph(gfx::Gfx& g, float x, float y, const char* label, Color c) {
  const Color body = rgb(0x18181b), ring = ui::alpha(rgb(0xffffff), 0.22f);
  float d=ui::large ? std::max(36.f,g.fonts[ui::F_TINY].size+10) : 24, k=d/24;
  float textY=y+(ui::large ? (d-g.fonts[ui::F_TINY].size)/2 : 5);
  std::string l = label;
  const K buttons[] = {K::BtnA, K::BtnB, K::BtnX, K::BtnY, K::BtnView, K::BtnMenu, K::BtnShoulders, K::BtnTriggers, K::BtnHorizontal, K::BtnVertical};
  for (K key : buttons) if (l == i18n::get(i18n::Lang::EN, key)) { label = i18n::get(i18n::Lang::EN, key); break; }
  if (l == i18n::get(i18n::Lang::EN, K::BtnHorizontal)) {
    gfx::rect(g, x, y, 40*k, d, ring, d/2);
    gfx::rect(g, x + 1.2f*k, y + 1.2f*k, 37.6f*k, 21.6f*k, body, 10.8f*k);
    gfx::triangle(g, x + 9*k, y + 12*k, x + 15*k, y + 7*k, x + 15*k, y + 17*k, XG);
    gfx::triangle(g, x + 31*k, y + 12*k, x + 25*k, y + 7*k, x + 25*k, y + 17*k, XG);
    return 40*k;
  }
  if (l == "A" || l == "B" || l == "X" || l == "Y") {
    gfx::rect(g, x, y, d, d, ring, d/2);
    gfx::rect(g, x + 1.2f*k, y + 1.2f*k, 21.6f*k, 21.6f*k, body, 10.8f*k);
    gfx::text(g, ui::F_TINY, label, x + d/2, textY, c, 1);
    return d;
  }
  if (l == "View" || l == "Menu") {
    gfx::rect(g, x, y, d, d, ring, d/2);
    gfx::rect(g, x + 1.2f*k, y + 1.2f*k, 21.6f*k, 21.6f*k, body, 10.8f*k);
    Color w = rgb(0xf4f4f5);
    if (l == "Menu") for (int i = 0; i < 3; i++) gfx::rect(g, x + 7*k, y + (7.5f+i*4)*k, 10*k, 1.6f*k, w, 0.8f*k);
    else { gfx::rect(g, x + 6*k, y + 6*k, 8*k, 7*k, w, 1.2f*k); gfx::rect(g, x + 7.3f*k, y + 7.3f*k, 5.4f*k, 4.4f*k, body, 0.5f*k);
           gfx::rect(g, x + 10*k, y + 10*k, 8*k, 7*k, w, 1.2f*k); gfx::rect(g, x + 11.3f*k, y + 11.3f*k, 5.4f*k, 4.4f*k, body, 0.5f*k); }
    return d;
  }
  float w = gfx::textWidth(g, ui::F_TINY, label) + 18;
  gfx::rect(g, x, y, w, d, ring, d/2);
  gfx::rect(g, x + 1.2f, y + 1.2f, w - 2.4f, d - 2.4f, body, d/2 - 1.2f);
  gfx::text(g, ui::F_TINY, label, x + w / 2, textY, rgb(0xf4f4f5), 1);
  return w;
}
static float hint(gfx::Gfx& g, float x, float y, const char* btn, Color bc, const char* label) {
  float w = buttonGlyph(g, x, y, btn, bc);
  return x + w + 8 + gfx::text(g, ui::F_SMALL, label, x + w + 8, y + 2, ui::MUTED) + 26;
}
// Dica do mapa único (controls.hpp): botão e rótulo vêm da tabela do contexto; `text` troca o rótulo (ex.: "Tocar tudo").
// A ação sem botão no contexto não desenha nada e devolve o x. A 2ª metade de um par (RB, RT) não tem dica própria.
static float ctlHint(gfx::Gfx& g, float x, float y, ctl::Ctx c, ctl::Act a, i18n::Lang lang, K text = K::None) {
  ctl::Btn b = ctl::button(c, a);
  if (b == ctl::Btn::Count || ctl::pairSecond(a)) return x;
  const bool pairLB = b == ctl::Btn::LB, pairLT = b == ctl::Btn::LT;
  const char* glyph = pairLB ? i18n::get(i18n::Lang::EN, K::BtnShoulders) : pairLT ? i18n::get(i18n::Lang::EN, K::BtnTriggers) :
      b == ctl::Btn::A ? "A" : b == ctl::Btn::B ? "B" : b == ctl::Btn::X ? "X" : b == ctl::Btn::Y ? "Y" : b == ctl::Btn::Menu ? "Menu" :
      b == ctl::Btn::View ? "View" : b == ctl::Btn::RB ? "RB" : "RT";
  Color color = b == ctl::Btn::A ? XA : b == ctl::Btn::B ? XB : b == ctl::Btn::X ? XX : b == ctl::Btn::Y ? XY : XG;
  return hint(g, x, y, glyph, color, i18n::get(lang, text != K::None ? text : ctl::label(a)));
}
// Largura que `ctlHint` ocupa (0 se a ação não tem dica no contexto): o rodapé usa para decidir o que cabe antes de desenhar.
static float ctlHintWidth(gfx::Gfx& g, ctl::Ctx c, ctl::Act a, i18n::Lang lang, K text = K::None) {
  ctl::Btn b = ctl::button(c, a);
  if (b == ctl::Btn::Count || ctl::pairSecond(a)) return 0;
  const bool pairLB = b == ctl::Btn::LB, pairLT = b == ctl::Btn::LT;
  const char* glyph = pairLB ? i18n::get(i18n::Lang::EN, K::BtnShoulders) : pairLT ? i18n::get(i18n::Lang::EN, K::BtnTriggers) :
      b == ctl::Btn::A ? "A" : b == ctl::Btn::B ? "B" : b == ctl::Btn::X ? "X" : b == ctl::Btn::Y ? "Y" : b == ctl::Btn::Menu ? "Menu" :
      b == ctl::Btn::View ? "View" : b == ctl::Btn::RB ? "RB" : "RT";
  const float d = ui::large ? std::max(36.f, g.fonts[ui::F_TINY].size + 10) : 24;
  const bool round = b == ctl::Btn::A || b == ctl::Btn::B || b == ctl::Btn::X || b == ctl::Btn::Y || b == ctl::Btn::Menu || b == ctl::Btn::View;
  const float glyphW = round ? d : gfx::textWidth(g, ui::F_TINY, glyph) + 18;
  return glyphW + 8 + gfx::textWidth(g, ui::F_SMALL, i18n::get(lang, text != K::None ? text : ctl::label(a))) + 26;
}
// Uma fileira de dicas, na ordem dada.
[[maybe_unused]] static float ctlFooter(gfx::Gfx& g, float x, float y, ctl::Ctx c, i18n::Lang lang, std::initializer_list<ctl::Act> acts) {
  for (ctl::Act a : acts) x = ctlHint(g, x, y, c, a, lang);
  return x;
}

// Marca: o logo "Órbita" (anel fino em degradê em volta do play) com o nome ao lado; o "360" no fim sai em peso leve.
// Padrão: "Tube360" com play em círculo. marca.txt ao lado do .xex troca: 1ª linha o nome, 2ª linha "retangulo" para o
// play num retângulo arredondado.
static bool brandRect = false;
static std::string brandName = "Tube360";
static float wordmark(gfx::Gfx& g, float x, float y, float a, Color bg) {
  const float d = 40;
  gfx::rectGrad(g, x, y, d, d, ui::alpha(rgb(0xffffff), 0.9f * a), ui::alpha(rgb(0xffffff), 0.12f * a), ui::alpha(rgb(0xff0033), 0.95f * a),
                ui::alpha(rgb(0xffffff), 0.12f * a), d / 2);
  gfx::rect(g, x + 1.5f, y + 1.5f, d - 3, d - 3, bg, d / 2 - 1.5f);
  if (brandRect) {
    float pw = 25, ph = 17.5f, px = x + (d - pw) / 2, py = y + (d - ph) / 2;
    gfx::rect(g, px, py, pw, ph, ui::alpha(rgb(0xff0033), a), 5);
    gfx::triangle(g, px + pw * 0.40f, py + ph * 0.28f, px + pw * 0.40f, py + ph * 0.72f, px + pw * 0.67f, py + ph * 0.5f, ui::alpha(rgb(0xffffff), a));
  } else {
    float c = 22, cx = x + (d - c) / 2, cy = y + (d - c) / 2;
    gfx::rectGrad(g, cx, cy, c, c, ui::alpha(ui::A2, a), ui::alpha(ui::A1, a), ui::alpha(ui::A1, a), ui::alpha(ui::A1, a), c / 2);
    gfx::triangle(g, cx + c * 0.38f, cy + c * 0.27f, cx + c * 0.38f, cy + c * 0.73f, cx + c * 0.75f, cy + c * 0.5f, ui::alpha(rgb(0xffffff), a));
  }
  float tx = x + d + 14;
  std::string main = brandName, tail;
  if (main.size() > 3 && main.compare(main.size() - 3, 3, "360") == 0) { tail = "360"; main.resize(main.size() - 3); }
  bool gap = !main.empty() && main.back() == ' ';
  while (!main.empty() && main.back() == ' ') main.pop_back();
  float w = gfx::text(g, ui::F_LOGO, main, tx, y + 4, ui::alpha(ui::TEXT, a));
  float w2 = tail.empty() ? 0 : gfx::text(g, ui::F_LOGOL, tail, tx + w + (gap ? 12 : 4), y + 4, ui::alpha(ui::TEXT, 0.85f * a), 0, gap ? 3 : 1);
  return d + 14 + w + 12 + w2;
}

// Ícones do Phosphor (icons.hpp): atlas branco com o alfa do SVG, tingido na hora. big = 48 px, senão 24 px; desenhados
// no tamanho exato do atlas e em pixel inteiro, para não borrar.
static gfx::Tex iconTex;
static void loadIcons(gfx::Gfx& g) {
  std::vector<unsigned char> rgba((size_t)icons::W * icons::H * 4, 255);
  for (int i = 0; i < icons::W * icons::H; i++) rgba[(size_t)i * 4 + 3] = icons::ALPHA[i];
  iconTex = gfx::makeImage(g, icons::W, icons::H, VK_FORMAT_R8G8B8A8_UNORM, true);
  gfx::upload(g, iconTex, rgba.data(), rgba.size());
}
static void glyph(gfx::Gfx& g, int which, bool big, float x, float y, Color c) {
  int row=(g.scale > 1 ? 2 : 0) + (big ? 0 : 1), top=0;
  for (int i=0;i<row;++i) top+=icons::SIZES[i];
  const float s = (float)icons::SIZES[row], u = (float)(which * icons::CELL), v = (float)top;
  gfx::imagePart(g, iconTex, std::round(x), std::round(y), s / std::max(1.f,g.scale), s / std::max(1.f,g.scale), u / icons::W, v / icons::H, (u + s) / icons::W, (v + s) / icons::H, c);
}

// O UV usa a linha do peso escolhido; o tamanho do quad é o da rasterização.
static void playerIcon(gfx::Gfx& g, icons::PlayerIcon iconId, float cx, bool focused, bool marked = false) {
  int level = g.scale > 1 ? 1 : 0;
  const auto& sprite = icons::PLAYER[level][iconId];
  float size = sprite.size / std::max(1.f,g.scale), y = focused || marked ? icons::FILL_Y[level] : icons::REGULAR_Y[level];
  if (focused) gfx::rect(g, cx - 22, 644, 44, 44, rgb(0xf4f4f5), 22);
  gfx::imagePart(g, iconTex, cx - size / 2, 666 - size / 2, size, size,
      float(sprite.x) / icons::W, y / icons::H, float(sprite.x + sprite.size) / icons::W, (y + sprite.size) / icons::H,
      focused ? ui::INK : rgb(0xf4f4f5));
}

static void playerTip(gfx::Gfx& g, float cx, const PlayerText& label, const PlayerText& value, bool transport) {
  float w = std::max(label.width, value.width) + 24, h = transport ? (ui::large ? 38 : 26) : value.text.empty() ? (ui::large ? 44 : 31) : (ui::large ? 76 : 48);
  float x = std::clamp(cx - w / 2, 32.f, ui::W - 32 - w), y = (transport ? 636 : 632) - h;
  gfx::rect(g, x, y, w, h, ui::alpha(rgb(0x18181b), 0.94f), 10);
  gfx::text(g, ui::F_CARD, label.text, x + w / 2, y + (transport ? 4 : 7), ui::TEXT, 1);
  if (!value.text.empty()) gfx::text(g, ui::F_DETAIL, value.text, x + w / 2, y + (ui::large ? 42 : 26), ui::MUTED, 1);
}

// Ícones desenhados com formas (24 px), centrados em (cx, cy).
static void icon(gfx::Gfx& g, int which, float cx, float cy, Color c, Color bg) {
  if (which == 0) {  // início: casa
    gfx::triangle(g, cx - 12, cy - 1, cx, cy - 12, cx + 12, cy - 1, c);
    gfx::rect(g, cx - 8, cy - 2, 16, 13, c, 2);
    gfx::rect(g, cx - 3, cy + 3, 6, 8, bg, 1);
  } else if (which == 1) {  // buscar: lupa
    gfx::rect(g, cx - 11, cy - 11, 17, 17, c, 8.5f);
    gfx::rect(g, cx - 8, cy - 8, 11, 11, bg, 5.5f);
    gfx::triangle(g, cx + 2, cy + 5, cx + 5, cy + 2, cx + 12, cy + 9, c);
    gfx::triangle(g, cx + 2, cy + 5, cx + 12, cy + 9, cx + 9, cy + 12, c);
  } else if (which == 2) {  // celular
    gfx::rect(g, cx - 8, cy - 12, 16, 24, c, 4);
    gfx::rect(g, cx - 5.5f, cy - 9, 11, 16, bg, 2);
    gfx::rect(g, cx - 2, cy + 8, 4, 2, bg, 1);
  } else if (which == 3) {  // engrenagem: aro e oito dentes
    for (int i = 0; i < 8; i++) {
      float a = i * 6.2831853f / 8;
      gfx::rect(g, cx + std::cos(a) * 10 - 3, cy + std::sin(a) * 10 - 3, 6, 6, c, 1.5f);
    }
    gfx::rect(g, cx - 10, cy - 10, 20, 20, c, 10);
    gfx::rect(g, cx - 4, cy - 4, 8, 8, bg, 4);
  } else if (which == 5) {
    gfx::rect(g, cx + 4, cy - 12, 3, 20, c, 1); gfx::rect(g, cx - 4, cy - 12, 11, 4, c, 1);
    gfx::rect(g, cx - 8, cy + 2, 14, 9, c, 4.5f);
  } else if (which == 6) {
    for (int i = 0; i < 3; i++) gfx::rect(g, cx - 12 + i * 8, cy - 11, 5, 22, c, 1);
  } else if (which == 7) {
    gfx::rect(g, cx - 11, cy - 7, 22, 17, c, 3); gfx::rect(g, cx - 7, cy - 12, 14, 2, c, 1);
    gfx::triangle(g, cx - 3, cy - 3, cx - 3, cy + 6, cx + 5, cy + 1.5f, bg);
  } else {  // sair: liga/desliga
    gfx::rect(g, cx - 11, cy - 10, 22, 22, c, 11);
    gfx::rect(g, cx - 8, cy - 7, 16, 16, bg, 8);
    gfx::rect(g, cx - 4.5f, cy - 13, 9, 9, bg, 0);
    gfx::rect(g, cx - 1.5f, cy - 13, 3, 11, c, 1.5f);
  }
}

static void spinner(gfx::Gfx& g, float cx, float cy, float r, double t) {
  for (int i = 0; i < 12; i++) {
    float a = (float)(i / 12.0 * 6.2831853 + t * 4.5);
    float k = (float)i / 11.f;
    Color c = ui::mix(ui::A1, ui::A2, k); c.a = 0.15f + 0.85f * k;
    gfx::rect(g, cx + std::cos(a) * r - 4, cy + std::sin(a) * r - 4, 8, 8, c, 4);
  }
}

// ---------------------------------------------------------------- cartões
// Miniatura recortada para o formato do cartão (retrato para Shorts, quadrada para música); sem imagem, um bloco liso.
static void artwork(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const std::string& id, uint64_t frame, float x, float y, float w, float h, bool square = false, bool round = false, bool direct = false, bool portrait = false, float bottom = 1e9f) {
  float visibleH=std::min(h,bottom-y);
  if (visibleH <= 0) return;
  if (visibleH == h && musicShortcutArt(g,id,x,y,w,h)) return;
  float radius = round ? w / 2 : square ? 6 : 12;
  const auto* s = id.empty() ? nullptr : thumbs.get(worker, id, frame, direct);
  if (s) {
    float crop = portrait ? (float)thumbs.height / thumbs.width * 9/16 : square ? (float)thumbs.height / thumbs.width : 1;
    if (visibleH < h) gfx::imagePart(g,s->tex,x,y,w,visibleH,.5f-crop/2,0,.5f+crop/2,visibleH/h,{1,1,1,1});
    else gfx::image(g, s->tex, x, y, w, h, radius, {1,1,1,1}, crop);
  } else gfx::rect(g, x, y, w, visibleH, ui::SURFACE2, radius);
}

// ---- Cartão de vídeo (API da F61, usada por Home, coleções, busca, canal e player) ----
// CardMeta é tudo o que um cartão mostra. Campos que a tela não tem ficam no padrão e a peça não desenha nada para eles.
//   duration: segundos (0 ou menos = desconhecida, sem selo); live: selo vermelho "AO VIVO", só com a marca de live
//   progress: 0 a 1 (barra vermelha no pé da miniatura; 0 = sem barra)
//   members:  selo "Só para membros" verde com estrela na LINHA DE BAIXO, nunca na miniatura (decisão do Pedro)
//   verified: marca de verificado ao lado do canal (a F62 preenche); date: "há 2 dias" etc. (vazio = some)
struct CardMeta {
  std::string title, channel, views, date;
  int duration = 0;   // segundos; 0 ou menos = sem selo de duração
  bool live = false;  // só com marca de transmissão ao vivo (duração 0 é "desconhecida", não "ao vivo")
  float progress = 0;
  yt::Membership members = yt::Membership::None;
  bool verified = false;
};

// Selo de duração (ou AO VIVO) no canto da miniatura; (ix,iy,w,h) é o retângulo da miniatura.
static void cardDuration(gfx::Gfx& g, i18n::Lang lang, int duration, bool live, float ix, float iy, float w, float h) {
  if ((duration <= 0 && !live) || iy + h > 646) return;
  std::string d = live ? i18n::get(lang, K::Live) : fmtTime(duration);
  float dw = gfx::textWidth(g, ui::F_TINY, d) + 12, badgeH = ui::large ? 36 : 20, badgeY = iy + h - badgeH - 7;
  gfx::rect(g, ix + w - dw - 7, badgeY, dw, badgeH, live ? ui::A1 : Color{0,0,0,.8f}, 5);
  gfx::text(g, ui::F_TINY, d, ix + w - dw / 2 - 7, badgeY + (badgeH - g.fonts[ui::F_TINY].size) / 2, rgb(0xffffff), 1);
}

// Miniatura + barra de andamento + duração. A miniatura é pedida em HD e reduzida ao tamanho do cartão pelo Worker.
static void cardThumb(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, i18n::Lang lang, const std::string& id, const CardMeta& m,
                      bool direct, bool shorts, float ix, float iy, float w, float h) {
  artwork(g, thumbs, worker, id, frame, ix, iy, w, h, false, false, direct, shorts, 646);
  if (m.progress > 0 && iy + h <= 646) {
    gfx::rect(g, ix + 4, iy + h - 5, w - 8, 4, {0,0,0,.6f}, 2);
    gfx::rect(g, ix + 4, iy + h - 5, std::max(4.f, (w - 8) * std::clamp(m.progress, 0.f, 1.f)), 4, ui::A1, 2);
  }
  if (!shorts) cardDuration(g, lang, m.duration, m.live, ix, iy, w, h);
}

// Texto do cartão: título (até 2 linhas), canal (+ verificado), visualizações · data e a linha de membros. Devolve o y logo abaixo.
// ty = topo do título; w = largura; titleFont/metaFont = fontes (Shorts usam as menores).
static float cardText(gfx::Gfx& g, i18n::Lang lang, const CardMeta& m, float x, float ty, float w, int titleFont, int metaFont, bool focused, bool shorts) {
  for (const auto& line : gfx::wrap(g, titleFont, m.title, w, 2)) {
    if (ty + g.fonts[titleFont].lineH <= 646) gfx::textBox(g, titleFont, line, x, ty, w, focused ? ui::TEXT : rgb(0xe4e4e7));
    ty += ui::large ? (shorts ? 34 : 31) : shorts ? 27 : 23;
  }
  ty += 3;
  float lh = g.fonts[metaFont].lineH;
  if (ty + lh <= 646) {
    const float markD = verifiedSize(g, metaFont), mark = m.verified ? markD + 6 : 0;
    std::string tail = m.views.empty() ? "" : m.views;
    if (!m.date.empty()) tail += (tail.empty() ? "" : "  \xC2\xB7  ") + m.date;
    std::string head = m.channel;
    if (!shorts && !tail.empty() && !head.empty()) {
      // canal e "visualizações · data" na mesma linha quando cabem; senão o canal ocupa a linha e o resto desce
      std::string both = head + "  \xC2\xB7  " + tail;
      if (gfx::textWidth(g, metaFont, both) + mark <= w) { head = both; tail.clear(); }
    } else if (shorts && !tail.empty()) { head += (head.empty() ? "" : "  \xC2\xB7  ") + tail; tail.clear(); }
    std::string shown = gfx::ellipsize(g, metaFont, head, w - mark);
    gfx::textBox(g, metaFont, shown, x, ty, w, ui::MUTED);
    if (m.verified) {
      float shownW = gfx::textWidth(g, metaFont, shown), mx = cardRtl(g, shown, metaFont) ? x + w - shownW - 6 - markD : x + shownW + 6;
      verifiedMark(g, mx, verifiedY(g, metaFont, ty, markD), markD, ui::MUTED, ui::BG);
    }
    ty += lh + 2;
    if (!tail.empty() && ty + lh <= 646) { gfx::textBox(g, metaFont, gfx::ellipsize(g, metaFont, tail, w), x, ty, w, ui::MUTED); ty += lh + 2; }
  }
  if (m.members != yt::Membership::None && ty + g.fonts[ui::F_TINY].lineH <= 646) { memberLine(g, m.members, lang, x, ty, w); ty += g.fonts[ui::F_TINY].lineH + 2; }
  return ty;
}

// Altura que o texto de um cartão ocupa no pior caso (título de 2 linhas, canal e "views · data" em linhas separadas, linha de membros):
// as fileiras reservam isso para o selo de membros nunca ser cortado, em nenhum modo de fonte.
static float cardTextBlock(gfx::Gfx& g, bool shorts) {
  int metaFont = shorts ? ui::F_TINY : ui::F_SMALL;
  float title = 2 * (ui::large ? (shorts ? 34.f : 31.f) : shorts ? 27.f : 23.f);
  return (shorts ? 16.f : 14.f) + title + 3 + (shorts ? 1 : 2) * (g.fonts[metaFont].lineH + 2) + g.fonts[ui::F_TINY].lineH + 2 + 8;
}
// O mesmo para o cartão das páginas de navegação (título F_KEY em 2 linhas, byline, membros).
static float navTextBlock(gfx::Gfx& g) {
  return 16.f + 2 * (ui::large ? 34.f : 27.f) + 4 + g.fonts[ui::F_CHAN].lineH + 2 + g.fonts[ui::F_TINY].lineH + 2 + 8;
}

// Cartão de vídeo completo: realce de foco, miniatura, andamento, duração e texto. (x,y) é o canto da miniatura em repouso,
// artW×artH o tamanho em repouso, an a animação de foco (0 a 1). Quem desenha em grade usa este; a F62 pode montar o dela com
// cardThumb + cardText.
static void videoCard(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, i18n::Lang lang, const std::string& id, const CardMeta& m, bool direct,
                      bool shorts, bool focused, float x, float y, float artW, float artH, float an) {
  float s=shorts ? 1 : 1+.045f*an, w=artW*s, h=artH*s, ix=x-(w-artW)/2, iy=y-(h-artH)/2;
  if (shorts && focused && iy-5<646) gfx::rect(g,ix-5,iy-5,w+10,std::min(h+10,646-iy+5),ui::TEXT,12);
  else if (!shorts && an>.02f && iy-4<646) {
    if (iy-24>=140 && iy+h+32<=646) gfx::shadow(g,ix,iy+10,w,h,14,22,.6f*an);
    gfx::rect(g,ix-4,iy-4,w+8,std::min(h+8,646-iy+4),ui::alpha(rgb(0xffffff),an),16);
  }
  cardThumb(g, thumbs, worker, frame, lang, id, m, direct, shorts, ix, iy, w, h);
  cardText(g, lang, m, x, y+artH+(shorts ? 16 : 14), artW, shorts ? ui::F_SMALL : ui::F_CARD, shorts ? ui::F_TINY : ui::F_SMALL, focused, shorts);
}

// Cartão de vídeo da Home (fina camada sobre videoCard). saved = registro do histórico local (andamento e duração), se a lista é local.
static void homeCard(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, i18n::Lang lang, const Video& v, bool direct, bool shorts,
                     bool focused, float x, float y, float artW, float artH, float an, const local::Entry* saved) {
  CardMeta m; m.title = v.title; m.channel = v.channel; m.views = v.views; m.date = v.date; m.members = v.members;
  m.duration = std::max(0, v.dur); m.live = v.live;
  double pos = saved ? saved->position : v.pos, dur = saved ? saved->duration : v.dur;
  m.progress = pos > 0 && dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
  videoCard(g, thumbs, worker, frame, lang, v.id, m, direct, shorts, focused, x, y, artW, artH, an);
}

// Medidas da grade de uma página de navegação; ao calcular, já monta as linhas e anima a rolagem.
struct NavMetrics { int cols; float cw, art, h, rowH, shortW, shortH, shortHeading, shortRowH, groupH; };
// Altura de uma fileira da grade, com o título do grupo (dia do Histórico) quando ela abre um.
static float navRowHeight(const NavMetrics& m, const ItemGrid::GridRow& row) { return (row.shorts ? m.shortRowH : m.rowH) + (row.heading.empty() ? 0.f : m.groupH); }
static NavMetrics navMetrics(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  NavMetrics m;
  m.cols=ui::columns(d.nav.columnsSetting); m.cw=std::floor((ui::W-200-(m.cols-1)*24)/m.cols); m.art=p.music ? std::min(m.cw,246.f) : m.cw; m.h=p.music ? m.art : m.cw*9/16;
  m.rowH=m.h+(p.kind=="library" ? 28 : navTextBlock(g));
  p.grid(m.cols,d.hideShorts);
  m.shortW=std::min(m.cw*.5f,112.f); m.shortH=m.shortW*16/9; m.shortHeading=std::max(30.f,g.fonts[ui::F_KEY].lineH+8); m.shortRowH=m.shortHeading+m.shortH+(ui::large ? 124 : 100); m.groupH=std::max(40.f,g.fonts[ui::F_KEY].lineH+14);
  float want = 0;
  if (p.zone == 3) for (int row=0; row<p.gridRow(); ++row) want += navRowHeight(m,p.rows[row]);
  p.scroll += (want - p.scroll) * 0.24f;
  return m;
}

// Cartão da grade de navegação (coleções, canal, busca e música). A Biblioteca usa o ladrilho com o título sobre a capa.
static void navCard(NavDraw& d, const NavMetrics& m, const NavItem& item, bool shorts, bool focused, float x, float y, float artW, float artH) {
  auto& g = d.g; auto& p = d.p; auto& nav = d.nav;
  auto tr = [&](K k) { return d.tr(k); };
  const float cw = m.cw, art = m.art, h = m.h;
  if (focused && y-5 < 646) gfx::rect(g, x - 5, y - 5, artW + 10, std::min(artH+10,646-y+5), ui::TEXT, 12);
  artwork(g, d.thumbs, d.worker, item.image, d.frame, x, y, artW, artH, p.music || item.type == "channel" || item.type == "artist", item.type == "channel" || item.type == "artist", p.direct, shorts, 646);
  if (item.type == "video" && !shorts && !p.music && p.kind != "library") cardDuration(g, d.lang, item.duration, item.duration == 0, x, y, artW, artH);
  const auto* saved=nav.local && (!nav.signedIn || p.kind=="local_later" || p.kind=="local_history")?local::Library::find(nav.local->history,item.id):nullptr;
  double pos=saved?saved->position:0,duration=saved?saved->duration:item.duration;
  if(item.type=="video" && pos>0 && duration>0 && y+artH <= 646){gfx::rect(g,x+4,y+artH-6,artW-8,4,ui::SURFACE2,2);gfx::rect(g,x+4,y+artH-6,(artW-8)*float(std::clamp(pos/duration,0.0,1.0)),4,ui::A1,2);}
  if (p.kind == "library" && !shorts) {
    gfx::vgrad(g, x, y, art, h, {0,0,0,0}, {0,0,0,0.94f}, 12);
    float labelY = y + h - 12 - 27 * item.titleLines.size();
    for (const auto& line : item.titleLines) { gfx::textBox(g, ui::F_KEY, line, x + 12, labelY, art-24, ui::TEXT); labelY += 27; }
    return;
  }
  if (item.image.empty()) {
    K k = item.type == "history" ? K::History : item.type == "later" ? K::WatchLater : item.type == "mixes" ? K::Mixes : K::Playlists;
    if (y+h/2-12+g.fonts[ui::F_H2].lineH <= 646) gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, tr(k), art - 32), x + 16, y + h / 2 - 12, ui::MUTED);
  }
  float ty = y + artH + 16, textW=shorts ? m.shortW : cw;
  int titleFont=shorts ? ui::F_SMALL : ui::F_KEY, bylineFont=shorts ? ui::F_TINY : ui::F_CHAN;
  for (const auto& line : item.titleLines) {
    if (ty+g.fonts[titleFont].lineH <= 646) gfx::textBox(g, titleFont, line, x, ty, textW, ui::TEXT);
    ty += ui::large ? 34 : 27;
  }
  if (ty+4+g.fonts[bylineFont].lineH <= 646) gfx::textBox(g, bylineFont, item.byline, x, ty + 4, textW, ui::MUTED);
  ty += 4 + g.fonts[bylineFont].lineH + 2;
  if (item.members != yt::Membership::None && ty + g.fonts[ui::F_TINY].lineH <= 646) memberLine(g, item.members, d.lang, x, ty, textW);
}

// Fileiras da grade de uma página de navegação (a partir do y = top), com a fileira de Shorts quando existe.
static void navGrid(NavDraw& d, float top, const NavMetrics& m) {
  auto& g = d.g; auto& p = d.p;
  float rowY = top-p.scroll;
  for (const auto& row : p.rows) {
    float y=rowY, artW=row.shorts ? m.shortW : m.art, artH=row.shorts ? m.shortH : m.h;
    rowY += navRowHeight(m,row);
    // Pré-carrega as miniaturas das duas fileiras logo abaixo da tela, para já estarem prontas ao rolar (#28).
    if (y >= 646 && y < 646 + 2 * m.rowH && !row.shorts)
      for (int i : row.items) if (!p.items[i].image.empty() && p.items[i].image.rfind("local:",0)) d.thumbs.get(d.worker, p.items[i].image, d.frame, p.direct);
    if (y < top-2 || y >= 646 - std::min(60.f, m.h * .4f)) continue;  // sem tira de miniatura sobre a barra de dicas
    if (!row.heading.empty()) {
      if (y+g.fonts[ui::F_KEY].lineH <= 646) gfx::textBox(g,ui::F_KEY,gfx::ellipsize(g,ui::F_KEY,row.heading,ui::W-200),136,y,ui::W-200,ui::TEXT);
      y += m.groupH;
    }
    if (p.kind == "library" && !row.shorts && y+m.h > 656) continue;
    int first=0, visible=(int)row.items.size();
    if (row.shorts) {
      if (y+g.fonts[ui::F_KEY].lineH <= 646) gfx::text(g,ui::F_KEY,d.tr(K::ChannelShorts),136,y,ui::TEXT);
      y+=m.shortHeading;
      visible=std::max(1,(int)((ui::W-200+24)/(m.shortW+24)));
      int selected=p.gridRow()>=0 && p.rows[p.gridRow()].shorts ? p.gridPosition() : p.shortColumn;
      first=std::clamp(selected-visible+1,0,std::max(0,(int)row.items.size()-visible));
    }
    for (int col=first; col<std::min((int)row.items.size(),first+visible); ++col) {
      int i=row.items[col];
      float x = 136 + (col-first) * ((row.shorts ? m.shortW : m.cw) + 24);
      bool f = d.contentFocus && p.zone == 3 && (int)i == p.sel;
      navCard(d, m, p.items[i], row.shorts, f, x, y, artW, artH);
    }
  }
}

// Abas de uma página (Vídeos/Shorts/Ao vivo/Playlists, Início/Paradas/Mixes...): a ativa sublinhada, a focada em branco.
static void navTabs(NavDraw& d, const K* keys, int count, float x, float tabY) {
  auto& g = d.g; auto& p = d.p;
  for (int i = 0; i < count; i++) {
    K key = keys[i];
    float w = gfx::textWidth(g, ui::F_KEY, d.tr(key)) + 40;
    bool active = i == p.tabSel, focus = d.contentFocus && active && p.zone == 1;
    gfx::rect(g, x, tabY, w, 42, focus ? ui::TEXT : active ? ui::SURFACE2 : ui::BG, 23);
    gfx::text(g, ui::F_KEY, d.tr(key), x + w / 2, tabY + 11, focus ? ui::INK : ui::TEXT, 1);
    if (active && !focus) gfx::hgrad(g, x + 20, tabY + 43, w - 40, 3, ui::A1, ui::A2, 1.5f);
    x += w + 14;
  }
}

