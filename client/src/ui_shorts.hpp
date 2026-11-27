// Player de Shorts: vertical (altura toda), título/canal embaixo, ações ao lado (curtir, não gostei, comentários, canal)
// e ↑ ↓ para o Short anterior/seguinte. Abre no mesmo player (`playing.shortForm`); os painéis (comentários) são os do player.
#pragma once
#include "ui_context.hpp"

// Botão redondo de ação, no mesmo sprite dos ícones do player (cy = centro vertical).
static void shortsIcon(gfx::Gfx& g, icons::PlayerIcon iconId, float cx, float cy, bool focused, bool marked) {
  int level = g.scale > 1 ? 1 : 0;
  const auto& sprite = icons::PLAYER[level][iconId];
  float size = sprite.size / std::max(1.f, g.scale), y = focused || marked ? icons::FILL_Y[level] : icons::REGULAR_Y[level];
  gfx::rect(g, cx - 29, cy - 29, 58, 58, focused ? rgb(0xf5f5f7) : ui::SURFACE2, 29);
  gfx::imagePart(g, iconTex, cx - size / 2, cy - size / 2, size, size,
      float(sprite.x) / icons::W, y / icons::H, float(sprite.x + sprite.size) / icons::W, (y + sprite.size) / icons::H,
      focused ? ui::INK : rgb(0xf4f4f5));
}

// Posição do Short atual na fila (−1 se não está).
inline int UiContext::shortsIndex() const {
  for (size_t i = 0; i < shortsQueue.size(); ++i) if (shortsQueue[i].id == playing.id) return (int)i;
  return -1;
}

inline void UiContext::shortsStep(int direction) {
  int at = shortsIndex(), to = at + direction;
  if (at >= 0 && to >= 0 && to < (int)shortsQueue.size()) { Video v = shortsQueue[(size_t)to]; shelf.shortsFocus = 0; startVideo(v, 0, false); }
}

inline void UiContext::shortsInput(uint64_t now) {
  int vertical = in.hit(Input::DOWN) ? 1 : in.hit(Input::UP) ? -1 : 0;
  int horizontal = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
  constexpr int actions = 4;  // curtir, não gostei, comentários, canal
  int& focus = shelf.shortsFocus;  // 0 = vídeo, 1..4 = ações
  const int focusBefore = focus;
  if (horizontal > 0 && !focus) focus = 1;
  else if (horizontal < 0 && focus) focus = 0;
  if (vertical) {
    if (focus) focus = std::clamp(focus + vertical, 1, actions);
    else shortsStep(vertical);
  }
  if (in.hit(Input::RB)) shortsStep(1);
  if (in.hit(Input::LB)) shortsStep(-1);
  if (in.hit(Input::A) || in.hit(Input::START)) {
    if (!focus) { if (!pl.waiting() || pl.paused) pl.setPaused(!pl.paused); }
    else if (focus <= 2) {
      if (account.signedIn()) {
        const char* want = focus == 1 ? "LIKE" : "DISLIKE";
        control.add({5, "/api/video/" + playing.id + "/rate?r=" + (likeState == want ? "none" : focus == 1 ? "like" : "dislike"), 0});
      } else { toast = tr(K::RateSignIn); toastUntil = now + 3500; }
    } else if (focus == 3) { playerUI.panel = PlayerUI::COMMENTS_PANEL; if (!extras.loaded) extras.request(); }
    else openPlayerChannel(captions.channelId, playing.id);
  }
  if (in.hit(Input::X)) playerWatchLater(now);
  in.hit(Input::BACK); in.hit(Input::Y); in.hit(Input::LT); in.hit(Input::RT);
  if (in.hit(Input::B)) { if (focus) focus = 0; else stopVideo(); }
  if (focus != focusBefore) LOG("shorts: foco %d", focus);
  if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
    nestSkip = true; toast = tr(K::SpeakerFallback); toastUntil = now + 5000;
    startVideo(playing, pl.position(), playerFromRemote);
  }
}

inline void UiContext::shortsDraw(uint64_t now, double t) {
  gfx::rect(g, 0, 0, ui::W, ui::H, rgb(0x000000), 0);
  float scale = g.scale, scaleX = g.scaleX, insetX = g.insetX, insetY = g.insetY;
  g.scaleX = g.scale = 1; g.insetX = g.insetY = 0;
  gfx::rect(g, 0, 0, g.extent.width, g.extent.height, rgb(0x000000));
  gfx::videoQuad(g, 0, 0, g.extent.width, g.extent.height, 1, false);
  g.scale = scale; g.scaleX = scaleX; g.insetX = insetX; g.insetY = insetY;

  const float vw = std::floor(ui::H * 9 / 16), vx = (ui::W - vw) / 2;
  const int focus = shelf.shortsFocus;
  const std::string& title = !captions.title.empty() ? captions.title : pl.title.empty() ? playing.title : pl.title;
  const std::string& channel = captions.channel.empty() ? playing.channel : captions.channel;
  gfx::vgrad(g, vx, ui::H - 220, vw, 220, {0, 0, 0, 0}, {0, 0, 0, 0.85f});
  if (shortTitle != title || shortLang != settings.lang) {
    shortTitle = title; shortLang = settings.lang;
    shortLines = gfx::wrap(g, ui::F_BODY, title, vw - 44, 2);
  }
  float y = ui::H - 36 - (float)shortLines.size() * (g.fonts[ui::F_BODY].lineH + 2);
  for (const auto& line : shortLines) { gfx::textBox(g, ui::F_BODY, line, vx + 22, y, vw - 44, ui::TEXT); y += g.fonts[ui::F_BODY].lineH + 2; }
  if (!channel.empty()) gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, channel, vw - 44), vx + 22, y - g.fonts[ui::F_H2].lineH - 54 + 24, ui::TEXT);
  double dur = pl.duration, pos = pl.position();
  float frac = dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
  gfx::rect(g, vx, ui::H - 4, vw, 4, {1, 1, 1, 0.25f});
  if (frac > 0) gfx::rect(g, vx, ui::H - 4, vw * frac, 4, ui::A1);
  if (pl.paused && !pl.waiting()) {
    gfx::rect(g, ui::W / 2 - 36, ui::H / 2 - 36, 72, 72, {0, 0, 0, 0.6f}, 36);
    gfx::rect(g, ui::W / 2 - 14, ui::H / 2 - 17, 9, 34, rgb(0xffffff), 3);
    gfx::rect(g, ui::W / 2 + 5, ui::H / 2 - 17, 9, 34, rgb(0xffffff), 3);
  }
  // Ações ao lado, como no celular.
  const float cx = vx + vw + 56, top = ui::H / 2 - 150;
  const bool liked = likeState == "LIKE", disliked = likeState == "DISLIKE";
  struct Action { icons::PlayerIcon icon; const char* label; bool marked; };
  const Action actions[] = {{icons::THUMBS_UP, tr(K::Like), liked}, {icons::THUMBS_DOWN, tr(K::Dislike), disliked},
                            {icons::CHAT_CIRCLE_TEXT, tr(K::Comments), false}, {icons::USER_CIRCLE, tr(K::Channel), false}};
  for (int i = 0; i < 4; ++i) {
    float cy = top + (float)i * 92;
    shortsIcon(g, actions[i].icon, cx, cy, focus == i + 1, actions[i].marked);
    gfx::text(g, ui::F_TINY, actions[i].label, cx, cy + 34, focus == i + 1 ? ui::TEXT : ui::MUTED, 1);
  }
  // Dicas à esquerda, fora do vídeo.
  float hy = ui::H / 2 - 90, hx = 40;
  hint(g, hx, hy, tr(K::BtnVertical), XG, focus ? tr(K::ChooseRow) : tr(K::ShortsStep));
  hint(g, hx, hy + 44, "A", XA, tr(focus ? K::Open : pl.paused ? K::Resume : K::Pause));
  hint(g, hx, hy + 88, "B", XB, tr(focus ? K::Close : K::Back));
  if (pl.waiting()) {
    spinner(g, ui::W / 2, ui::H / 2, 26, t);
  }
  if (pl.state == player::FAILED) {
    gfx::rect(g, vx + 20, ui::H / 2 - 60, vw - 40, 120, ui::SURFACE, 18);
    gfx::text(g, ui::F_H2, tr(K::VideoFailed), ui::W / 2, ui::H / 2 - 34, ui::TEXT, 1);
    float ly = ui::H / 2 + 2;
    for (const auto& line : gfx::wrap(g, ui::F_SMALL, tr(i18n::playerError(pl.error)), vw - 80, 2)) { gfx::text(g, ui::F_SMALL, line, ui::W / 2, ly, ui::MUTED, 1); ly += g.fonts[ui::F_SMALL].lineH + 2; }
  }
  (void)now;
}
