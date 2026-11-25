#pragma once
#include "yt/client.hpp"
#include "i18n.hpp"
#include <ctime>

namespace connection {
inline i18n::Key message(yt::Failure failure) {
  using K=i18n::Key;
  switch (failure) {
    case yt::Failure::TLS_DATE: return K::ClockWrong;
    case yt::Failure::TLS: return K::TLSFailed;
    case yt::Failure::DNS: return K::DNSFailed;
    case yt::Failure::NETWORK: return K::NetworkFailed;
    case yt::Failure::HTTP: case yt::Failure::REFUSAL: return K::YouTubeRefused;
    default: return K::BadResponse;
  }
}
inline std::string clock() {
  time_t now=std::time(nullptr); tm value{};
  char text[64]{};
  if (gmtime_r(&now,&value)) std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S UTC",&value);
  return text;
}
}
