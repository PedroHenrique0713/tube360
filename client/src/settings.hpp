#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "i18n.hpp"

struct Settings {
  i18n::Lang lang = i18n::Lang::EN;
  int cols = 4, quality = 720, syncMs = 0;
  bool nest = false;

  void load(const std::string& dir) {
    *this = Settings{};
    if (FILE* f = std::fopen((dir + "settings.txt").c_str(), "r")) {
      char line[256];
      while (std::fgets(line, sizeof(line), f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
        size_t eq = s.find('='); if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq), val = s.substr(eq + 1);
        if (key == "lang") { if (val == "pt") lang = i18n::Lang::PT; else if (val == "en") lang = i18n::Lang::EN; }
        else if (key == "sound") { if (val == "nest" || val == "tv") nest = val == "nest"; }
        else {
          char* end = nullptr; long n = std::strtol(val.c_str(), &end, 10);
          if (val.empty() || *end) continue;
          if (key == "cols" && (n == 3 || n == 4)) cols = (int)n;
          if (key == "quality" && (n == 720 || n == 480 || n == 360)) quality = (int)n;
          if (key == "sync_ms") syncMs = (int)std::clamp(n, -2000L, 2000L);
        }
      }
      std::fclose(f); return;
    }
    // Arquivos legados identificam os consoles que já usavam português.
    if (FILE* f = std::fopen((dir + "quality.txt").c_str(), "r")) {
      lang = i18n::Lang::PT; int q = 0;
      if (std::fscanf(f, "%d", &q) == 1 && (q == 720 || q == 480 || q == 360)) quality = q;
      std::fclose(f);
    }
    if (FILE* f = std::fopen((dir + "audio.txt").c_str(), "r")) {
      lang = i18n::Lang::PT; char mode[16] = {}; int ms = 0;
      if (std::fscanf(f, "%15s %d", mode, &ms) >= 1) { nest = std::string(mode) == "nest"; syncMs = std::clamp(ms, -2000, 2000); }
      std::fclose(f);
    }
  }
  bool save(const std::string& dir) const {
    std::string tmp = dir + "settings.tmp", dest = dir + "settings.txt";
    FILE* f = std::fopen(tmp.c_str(), "w"); if (!f) return false;
    bool ok = std::fprintf(f, "lang=%s\ncols=%d\nquality=%d\nsound=%s\nsync_ms=%d\n", i18n::code(lang), cols, quality, nest ? "nest" : "tv", syncMs) > 0;
    if (std::fclose(f) != 0) ok = false;
    if (ok) ok = std::rename(tmp.c_str(), dest.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
  }
};
