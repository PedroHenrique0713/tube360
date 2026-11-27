// Peças de desenho compartilhadas entre as telas: tema, botões do controle, marca, ícones, selos e duração.
#pragma once

// ---------------------------------------------------------------- tema
namespace ui {
const Color BG = rgb(0x0b0b0e), SURFACE = rgb(0x18181d), SURFACE2 = rgb(0x24242b), TEXT = rgb(0xf5f5f7),
            MUTED = rgb(0xa1a1aa), FAINT = rgb(0x6b6b74), A1 = rgb(0xff0f3c), A2 = rgb(0xff5a52), INK = rgb(0x0b0b0e);
enum { F_BODY, F_SMALL, F_CARD, F_H2, F_LOGO, F_BIG, F_KEY, F_TINY, F_HERO, F_CHAN, F_LOGOL, F_CODE, F_VIDEO_TITLE, F_META, F_DETAIL, F_CLOCK, F_BADGE };
inline float W = 1280;
constexpr float H = 720, RAIL = 96, X0 = 136;
inline bool large = false, narrow = false;
inline int columns(int preferred) { return narrow ? 2 : large ? std::min(3,preferred) : preferred; }
inline Color alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
}  // namespace ui

static std::string fmtTime(double s) {
  if (s < 0) s = 0;
  int t = (int)s, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
  char b[32];
  if (h) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, sec); else std::snprintf(b, sizeof(b), "%d:%02d", m, sec);
  return b;
}

static void memberBadge(gfx::Gfx& g, yt::Membership members, i18n::Lang lang, float x, float y) {
  if (members == yt::Membership::None) return;
  bool access = members == yt::Membership::Accessible;
  const char* label = i18n::get(lang, access ? K::Member : K::Members);
  float w = gfx::textWidth(g, ui::F_TINY, label) + 12;
  float h=ui::large ? std::max(36.f,g.fonts[ui::F_TINY].size+8) : 20;
  gfx::rect(g, x, y, w, h, access ? rgb(0x20563f) : Color{0,0,0,0.86f}, 5);
  gfx::text(g, ui::F_TINY, label, x + w / 2, y + (ui::large ? (h-g.fonts[ui::F_TINY].size)/2 : 3), access ? rgb(0xb5f3cd) : ui::TEXT, 1);
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
