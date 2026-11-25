#pragma once
#include <string>
namespace yt {
inline const char* localeLanguage(const std::string& language) {
  return language == "ar" ? "ar" : language == "pt" ? "pt" : language == "es" ? "es" : "en";
}
inline const char* localeRegion(const std::string& language) {
  return language == "ar" ? "SA" : language == "pt" ? "BR" : language == "es" ? "MX" : "US";
}
}
