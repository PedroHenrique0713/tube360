// g++ client/src/player_ui_test.cpp -o /tmp/tube360-f4-test $(pkg-config --cflags --libs sdl3)
#include "player_ui.hpp"
#include "captions.hpp"
#include "settings.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>

int main() {
  PlayerUI ui;
  ui.reset(100);
  assert(ui.visible(5099) && !ui.visible(5100));
  for (int direction : {-1, 1}) {
    ui.until = 0;
    assert(ui.direction(6000, direction, 0) == 0);
    assert(!ui.options && ui.visible(6000));
    assert(ui.direction(6100, direction, 0) == direction);
  }
  ui.until = 0;
  assert(ui.direction(7000, 0, 1) == 0 && !ui.options);
  assert(ui.direction(7100, 0, 1) == 0 && ui.options);
  int old = ui.selected;
  assert(ui.direction(7200, 1, 0) == 0 && ui.selected != old);
  for (int count : {PlayerUI::EDGE, PlayerUI::COUNT}) {
    ui.tracks = true;
    ui.selected = PlayerUI::TRANSPORT;
    for (int i = 1; i < count; ++i) {
      assert(ui.direction(7300, 1, 0, count) == 0);
      assert(ui.selected == i);
    }
    ui.direction(7400, 1, 0, count);
    assert(ui.selected == PlayerUI::TRANSPORT);
    ui.direction(7500, -1, 0, count);
    assert(ui.selected == count - 1);
  }
  assert(ui.visible(60000));
  ui.panel = PlayerUI::CAPTIONS;
  assert(ui.back(60100) && ui.panel == PlayerUI::NONE && ui.options);
  assert(ui.back(60200) && !ui.options && ui.visible(60200));
  assert(ui.back(60300) && !ui.visible(60300));
  assert(!ui.back(60400));
  ui.reset(70000); ui.options = true;
  ui.direction(70100, 0, -1);
  assert(!ui.options);
  std::cout << "focus: reveal, seek, row, held visibility, four Back levels, Down OK\n";

  cc::Data data;
  data.title = "Title"; data.tracks.push_back({"en", "English", false});
  data.cues.push_back({1, 2, "First"}); data.cues.push_back({3, 5, "Second"});
  assert(data.at(0.99) == -1 && data.at(1) == 0 && data.at(1.99) == 0);
  assert(data.at(2) == -1 && data.at(3) == 1 && data.at(5) == -1);
  assert(data.at(1.2) == 0);  // busca de volta
  assert(data.at(1.2) == 0);  // pausa: mesmo relógio, mesma frase
  assert(data.title == "Title" && data.tracks.size() == 1);
  std::cout << "captions: clock boundaries, gaps, reverse seek, pause, metadata OK\n";

  auto root = std::filesystem::temp_directory_path() / ("tube360-f4-" + std::to_string(getpid()));
  std::filesystem::create_directory(root);
  Settings settings; settings.captions = true; settings.captionLang = "pt-BR";
  assert(settings.save(root.string() + "/"));
  Settings loaded; loaded.load(root.string() + "/");
  assert(loaded.captions && loaded.captionLang == "pt-BR");
  loaded.captions = false; assert(loaded.save(root.string() + "/"));
  settings.load(root.string() + "/");
  assert(!settings.captions);
  settings.videoMode=playback::FAST; settings.outputHeight=1080; settings.originalAudio=false;
  assert(settings.save(root.string()+"/")); loaded.load(root.string()+"/");
  assert(loaded.videoMode==playback::FAST && loaded.outputHeight==1080 && !loaded.originalAudio);
  ui.tracks=false; ui.selected=PlayerUI::AUDIO; ui.options=true; ui.direction(80000,1,0);
  assert(ui.selected==PlayerUI::TIMER);
  ui.tracks=true; ui.selected=PlayerUI::AUDIO; ui.direction(80001,1,0);
  assert(ui.selected==PlayerUI::TRACK);
  std::filesystem::remove_all(root);
  std::cout << "settings: CC language, on/off and shared sleep target persisted OK\n";
}
