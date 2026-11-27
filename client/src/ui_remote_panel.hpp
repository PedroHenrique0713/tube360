// Controle pelo celular: o painel com endereço e código de pareamento (desenho; a lógica está em ui_remote.hpp).
#pragma once
#include "ui_context.hpp"
#include "ui_remote.hpp"

// Painel (aberto pela linha das Configurações): endereço e código. A e B fecham (tratado no bloco F66 do laço do main.cpp).
inline void UiContext::remoteDraw(uint64_t now) {
  if (now >= remoteAddressAt) { remoteAddress = phonectl::localAddress(); remoteAddressAt = now + 2000; }
  gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.72f});
  float pw = 820, ph = ui::large ? 500 : 418, px = (ui::W - pw) / 2, py = (ui::H - ph) / 2;
  gfx::shadow(g, px, py + 10, pw, ph, 28, 40, 0.6f);
  gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
  gfx::text(g, ui::F_BIG, tr(K::RemoteTitle), px + 48, py + 40, ui::TEXT);
  float ty = py + 104;
  for (auto& line : gfx::wrap(g, ui::F_BODY, tr(K::RemoteSteps), pw - 96, 2)) { gfx::textBox(g, ui::F_BODY, line, px + 48, ty, pw - 96, ui::TEXT); ty += ui::large ? 32 : 25; }
  const int port = remoteNet.port;
  std::string url = remoteAddress.empty() || !port ? std::string() : "http://" + remoteAddress + ":" + std::to_string(port);
  if (url.empty()) gfx::text(g, ui::F_BODY, tr(remoteAddress.empty() ? K::RemoteNoNetwork : K::Loading), px + 48, ty + 18, ui::FAINT);
  else gfx::text(g, ui::F_CHAN, url, px + 48, ty + 12, ui::A2);
  std::string code = remoteCore.code();
  gfx::text(g, ui::F_CHAN, tr(K::RemoteCode), px + 48, py + (ui::large ? 250 : 206), ui::MUTED);
  gfx::textGrad(g, ui::F_CODE, code.substr(0, 3) + " " + code.substr(3), px + 48, py + (ui::large ? 282 : 236), ui::A1, ui::A2);
  gfx::textBox(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, tr(K::RemoteSafe), pw - 96), px + 48, py + (ui::large ? 400 : 322), pw - 96, ui::MUTED);
  ctlHint(g, px + 48, py + ph - 52, ctl::Ctx::Panel, ctl::Act::Open, settings.lang, K::Close);
}
