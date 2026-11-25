#pragma once
#include "i18n.hpp"
#include "yt/client.hpp"
#include "playback_mode.hpp"

inline std::string audioTrackLabel(const yt::AudioTrack& track, i18n::Lang lang) {
  using K=i18n::Key;
  auto tr=[&](K key){ return i18n::get(lang,key); };
  std::string code=track.language.substr(0,track.language.find('-'));
  std::string language=code=="en" ? tr(K::AudioEnglish) : code=="pt" ? tr(K::AudioPortuguese) : code=="es" ? tr(K::AudioSpanish) : code=="fr" ? tr(K::AudioFrench) : code=="de" ? tr(K::AudioGerman) : code=="ja" ? tr(K::AudioJapanese) : track.language;
  if (track.original) return std::string(tr(K::AudioOriginal))+(language.empty() ? "" : " ("+language+")");
  if (language.empty()) language=track.name.empty() ? tr(K::AudioUnknown) : track.name;
  return language+" ("+tr(track.automatic ? K::AudioAutoDub : K::AudioDub)+")";
}

inline i18n::Key videoModeLabel(int mode) {
  using K=i18n::Key;
  return mode==playback::FAST?K::Video720Fast:mode==playback::DETAIL?K::Video1080Detail:mode==playback::FIXED480?K::Video480:mode==playback::FIXED360?K::Video360:mode==playback::FIXED240?K::Video240:mode==playback::FIXED144?K::Video144:K::Automatic;
}
