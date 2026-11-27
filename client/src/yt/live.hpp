// Detecção única de transmissão ao vivo (convenção: duração 0 = ao vivo, -1 = desconhecida, > 0 = segundos).
// Entende `style`/`badgeStyle`/`iconType` com LIVE (BADGE_STYLE_TYPE_LIVE_NOW, THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE do
// lockupViewModel do cliente TV, style "LIVE" do thumbnailOverlayTimeStatusRenderer). Usada pelo catálogo, pela conta e pelos feeds.
#pragma once
#ifndef PICOJSON_USE_LOCALE
#define PICOJSON_USE_LOCALE 0
#endif
#include "picojson.h"
#include <string>

namespace yt {
inline bool liveMark(const picojson::value& v) {
  if (v.is<picojson::object>()) {
    for (const auto& kv : v.get<picojson::object>()) {
      if ((kv.first == "style" || kv.first == "badgeStyle" || kv.first == "iconType") && kv.second.is<std::string>() &&
          kv.second.get<std::string>().find("LIVE") != std::string::npos) return true;
      if (liveMark(kv.second)) return true;
    }
  } else if (v.is<picojson::array>()) for (const auto& x : v.get<picojson::array>()) if (liveMark(x)) return true;
  return false;
}
}  // namespace yt
