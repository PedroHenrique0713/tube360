#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "i18n.hpp"
#include "playback_mode.hpp"
#include "sponsorblock.hpp"

struct Settings {
  i18n::Lang lang = i18n::Lang::EN;
  int cols = 4, quality = 720, syncMs = 0;
  int videoMode = playback::AUTO, outputHeight = 720;
  int sponsorBlock = sponsorblock::BUTTON;
  bool originalAudio = true, largeText = false, videoFill = false;
  bool h264FullFilter = false;
  bool server = false, nest = false, sleepNotebook = false, captions = false;
  bool hideShorts = false, errorReports = true;
  std::string captionLang;

  void load(const std::string& dir) {
    *this = Settings{};
    if (FILE* f = std::fopen((dir + "settings.txt").c_str(), "r")) {
      char line[256];
      while (std::fgets(line, sizeof(line), f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
        size_t eq = s.find('='); if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq), val = s.substr(eq + 1);
        if (key == "lang") lang = i18n::language(val);
        else if (key == "sponsorblock") sponsorBlock = val == "off" ? sponsorblock::OFF : val == "auto" ? sponsorblock::AUTO : sponsorblock::BUTTON;
        else if (key == "large_text") largeText = val == "on";
        else if (key == "video_fit") videoFill = val == "fill";
        else if (key == "h264_loop_filter") h264FullFilter = val == "full";
        else if (key == "server") server = false;
        else if (key == "shorts") hideShorts = val == "hide";
        else if (key == "error_reports") errorReports = val != "off";
        else if (key == "video_mode") videoMode = playback::modeFromName(val.c_str());
        else if (key == "audio_preference") originalAudio = val != "interface";
        else if (key == "captions") captions = val == "on";
        else if (key == "caption_lang" && val.size() <= 32 && val.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos) captionLang = val;
        else if (key == "sleep_what") sleepNotebook = false;
        else if (key == "sound") { if (val == "nest" || val == "tv") nest = val == "nest"; }
        else {
          char* end = nullptr; long n = std::strtol(val.c_str(), &end, 10);
          if (val.empty() || *end) continue;
          if (key == "cols" && (n == 3 || n == 4)) cols = (int)n;
          if (key == "quality" && (n == 720 || n == 480 || n == 360)) quality = (int)n;
          if (key == "sync_ms") syncMs = (int)std::clamp(n, -2000L, 2000L);
          if (key == "output_height" && (n == 480 || n == 720 || n == 1080)) outputHeight = (int)n;
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
    bool ok = std::fprintf(f, "lang=%s\ncols=%d\nquality=%d\nsound=%s\nsync_ms=%d\nsleep_what=xbox\ncaptions=%s\ncaption_lang=%s\nserver=off\n", i18n::code(lang), cols, quality, nest ? "nest" : "tv", syncMs, captions ? "on" : "off", captionLang.c_str()) > 0;
    ok = ok && std::fprintf(f,"video_mode=%s\noutput_height=%d\naudio_preference=%s\n",playback::modeName(videoMode),outputHeight,originalAudio ? "original" : "interface") > 0;
    ok = ok && std::fprintf(f,"large_text=%s\nvideo_fit=%s\n",largeText ? "on" : "off",videoFill ? "fill" : "fit") > 0;
    ok = ok && std::fprintf(f,"sponsorblock=%s\n",sponsorBlock == sponsorblock::OFF ? "off" : sponsorBlock == sponsorblock::AUTO ? "auto" : "button") > 0;
    ok = ok && std::fprintf(f,"h264_loop_filter=%s\n",h264FullFilter ? "full" : "auto") > 0;
    ok = ok && std::fprintf(f,"shorts=%s\nerror_reports=%s\n",hideShorts ? "hide" : "row",errorReports ? "on" : "off") > 0;
    if (std::fclose(f) != 0) ok = false;
    if (ok) ok = std::rename(tmp.c_str(), dest.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
  }
};
