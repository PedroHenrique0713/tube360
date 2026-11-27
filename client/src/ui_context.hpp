// Contexto das telas: referências ao estado que o run() em main.cpp possui, para que o desenho e a entrada de cada
// tela morem no próprio arquivo (ui_*.hpp) como métodos daqui. Cada tela acrescenta as suas referências na sua seção.
#pragma once
#include <functional>
#include <optional>

enum Screen { HOME, SEARCH, PLAYER, SETTINGS, NAV };

// Dados do último erro da Home desenhado, só para o log da captura de teste.
struct ErrorShot { const Feed* feed = nullptr; K title = K::None, help = K::None; bool server = false; };

struct UiContext {
  // ---- compartilhado
  gfx::Gfx& g;
  Settings& settings;
  Input& in;
  Thumbs& thumbs;
  Worker& thumbsWorker;
  MusicNavigation& nav;
  LocalLibraryUI& localUI;
  Account& account;
  uint64_t& frame;
  bool& quit;
  std::string& toast;
  uint64_t& toastUntil;
  bool& railFocus;
  int& railSel;
  float& scroll;
  SleepState& sleep;
  Color& ambient;
  std::function<void()> openSearch;
  std::function<void()> goHome;
  std::function<void()> accountPhoto;
  std::function<void(const std::string&, const std::string&, const std::string&, bool, yt::search::Filters)> openNavFn;
  std::function<void(const Video&, double)> requestVideoFn;
  music::Player& pl;
  Worker& control;
  Video& playing;
  bool& playerFromRemote;
  Screen& screen;
  std::function<void(const Video&, double, bool, std::string)> startVideoFn;
  Worker& navWorker;
  float& railOpen;
  net::Server& srv;
  std::shared_ptr<yt::ConsoleAccount>& consoleAccount;
  std::function<void()> stopVideo;
  std::function<void(const std::string&)> openChannelForVideo;
  // ---- Início
  int& zone;
  bool& inResults;
  int& cat;
  std::vector<Shelf>& cats;
  Shelf& results;
  const std::string& serverAddress;
  std::string& castWho;
  std::function<void(Shelf&, int, bool, int, bool)> requestFeedFn;
  // ---- Casca: painéis, conta e timer
  int& sleepChoice;
  bool& accountBusy;
  bool& accountKnown;
  bool& accountFailed;
  bool& logoutPanel;
  std::function<void()> applySleep;
  std::function<void()> beginLogin;
  std::function<std::string(int)> sleepLabel;
  uint64_t& accountAt;
  uint64_t& nestInfoAt;
  bool& phonePanel;
  uint64_t& phonePlaybackSerial;
  std::function<int()> railPage;
  uint64_t& shutdownAt;
  bool& sleepPanel;
  std::string& castCode;
  cast::Status& phoneStatus;
  bool& loginPanel;
  K& loginError;
  int& sleepPending;
  std::function<void(const char*)> requestAccount;
  std::function<void(const std::string&)> requestSleep;
  // ---- Busca
  bool& latinKeyboard;
  std::string& query;
  Screen& searchBack;
  bool& searchMusic;
  SearchUI& searchUI;
  std::function<void(std::string)> doSearch;
  std::function<void()> navBack;
  // ---- Configurações
  sponsorblock::UI& sponsorUI;
  SpeakerPicker& speakerPicker;
  std::function<void()> saveSettings;
  updates::UI& updateUI;
  updates::Service& updater;
  int& settingsRow;
  nest::Config& speakerChoice;
  bool& nestAvailable;
  bool& nestChecked;
  std::string& nestName;
  int& nestOffsetMs;
  bool& nestOut;
  bool& nestSkip;
  std::function<bool()> loadDisplay;
  std::function<void()> rebuildCats;
  // ---- Player
  playback::QualityChoices& offeredQualities;
  playback::QualityChoices& defaultQualities;
  bool& qualityKnown;
  PlayerText& overlayTitle;
  PlayerText& overlayMeta;
  PlayerText& overlayChapter;
  PlayerText& tipLabel;
  PlayerText& tipValue;
  std::string& metaChannel;
  int& clockSecond;
  std::vector<yt::AudioTrack>& audioTracks;
  int& captionIndex;
  std::string& captionInterface;
  std::string& captionVideo;
  std::vector<std::string>& captionLines;
  float (&captionLineWidths)[2];
  cc::Loader& captionLoader;
  cc::Data& captions;
  float& captionWidth;
  int& clockDuration;
  std::string& clockLabel;
  float& clockWidth;
  PlayerText& commentAuthor;
  PlayerText& commentMeta;
  int& commentIndex;
  std::vector<std::string>& commentLines;
  std::string& commentPosition;
  PlayerExtras& extras;
  const char*& lastMode;
  int& lastOutput;
  int& lastQuality;
  int& lastSourceFps;
  int& lastTargetFps;
  std::string& likeState;
  std::vector<std::string>& manualNoticeLines;
  uint64_t& manualNoticeUntil;
  std::string& metaDate;
  i18n::Lang& metaLang;
  uint64_t& nextAt;
  PlayerUI& playerUI;
  uint64_t& overlayUntil;
  std::vector<std::string>& qualityHelpLines;
  std::string& qualityLabel;
  std::string& resolutionLabel;
  std::string& seekLabel;
  bool& ratePanel;
  int& rateSel;
  playback::SeekHold& seekHold;
  int& seekSecond;
  std::string& selectedAudio;
  std::string& selectedTrackLabel;
  sponsorblock::Playback& sponsors;
  std::vector<std::string>& trackLabels;
  i18n::Lang& trackLang;
  Video& upNext;
  gfx::Video& videoTex;
  uint64_t& nextDeadline;
  bool& endedSent;
  bool& livePaused;
  bool& nextPending;
  std::function<void(const std::string&, const std::string&)> openPlayerChannel;
  std::function<void()> openQuality;
  std::function<void()> resumeLive;
  std::function<void(sponsorblock::Action)> sponsorSeek;
  uint64_t& manualNoticeFont;
  i18n::Lang& manualNoticeLang;
  float& manualNoticeWidth;

  // ---- F61 Início e coleção: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp
  int homeShelves(const Shelf& cur) const;  // ui_home.hpp: 2 = prateleiras do YouTube da conta (Início), 0 = lista única

  // ---- F62 Busca e canal: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp
  void channelSortInput(NavPage& p); // definido em ui_channel.hpp
  void searchChipSelected(NavPage& p); // definido em ui_search.hpp

  // ---- F63 Player e Shorts: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp
  std::vector<Video>& related;
  PlayerShelf& shelf;
  std::vector<Video>& shortsQueue;
  std::vector<Video>& playedBefore;
  bool& historyBack;
  std::string& shortTitle;
  i18n::Lang& shortLang;
  std::vector<std::string>& shortLines;

  // ---- F64 Configurações e controle: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp

  // ---- F65 Música: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp

  // ---- F66 controle pelo celular: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp

  // ---- F67 membros tocando: campos novos desta frente só aqui, na mesma ordem do `UiContext screens{...}` do main.cpp

  // Métodos: declarados aqui, definidos no arquivo de cada tela (ui_*.hpp).
  ErrorShot errorShot;
  const char* tr(K key) const { return i18n::get(settings.lang, key); }
  void openNav(const std::string& kind, const std::string& id = "", const std::string& q = "", bool music = false, yt::search::Filters filters = {}) { openNavFn(kind, id, q, music, filters); }
  void startVideo(const Video& v, double t, bool fromRemote, std::string track = {}) { startVideoFn(v, t, fromRemote, std::move(track)); }
  void requestVideo(const Video& v, double t = 0) { requestVideoFn(v, t); }
  void requestFeed(Shelf& shelf, int kind, bool fresh = false, int offset = 0, bool front = false) { requestFeedFn(shelf, kind, fresh, offset, front); }
  // ambiente: brilho grande e suave na cor do vídeo em foco, por cima do fundo e do destaque (assim não há emenda)
  void glow() {
    gfx::rect(g, -160, -220, 860, 600, ui::alpha(ambient, 0.26f), 300, 360);
    gfx::rect(g, 820, 380, 560, 420, ui::alpha(ambient, 0.10f), 210, 300);
  }

  // ---- Casca (ui_shell.hpp)
  void railInput();
  void railDraw(bool remoteOnline);
  void overlaysDraw(uint64_t now, double t, bool sleepWarning, double sleepLeft);
  void sleepWarningInput();
  void sleepPanelInput(uint64_t now);
  void loginInput(uint64_t now);
  void logoutInput();
  void phoneInput();
  void filtersInput();
  void localInput(uint64_t now);
  void videoMenuInput(uint64_t now);
  void unsubscribeInput();

  // ---- Busca (ui_search.hpp)
  void searchInput(const char* const* keys);
  void searchDraw(const char* const* keys, double t);

  // ---- Páginas de navegação: entrada genérica em ui_collection.hpp, ganchos por tipo em ui_channel, ui_music e ui_search
  void navInput(uint64_t now);
  void channelHeaderPress(NavPage& p, uint64_t now);
  void channelTabSelected(NavPage& p);
  void playlistHeaderPress(NavPage& p);
  void subscriptionStripInput(NavPage& p, bool left, bool right, bool up, bool down, bool a);
  void musicHomeMove(NavPage& p, bool left, bool right, bool up, bool down);
  void musicTabSelected(NavPage& p);
  void searchFiltersPress(NavPage& p);

  // ---- Player (ui_player.hpp)
  void playerPanelInput(uint64_t now, const display::CommentsPanel& commentPanel);
  void playerNextInput();
  void ratePanelInput(uint64_t now);
  void playerNoticeDraw(uint64_t now);
  void playerInput(uint64_t now, int optionCount);
  void playerShelfInput(uint64_t now);
  void playerShelfDraw(uint64_t now);
  void playerWatchLater(uint64_t now);
  bool playerStep(int direction);
  void shortsEnded();
  void adjustVolume(uint64_t now, int direction);
  void volumeHud(uint64_t now);
  int shortsIndex() const;
  void shortsStep(int direction);
  void shortsInput(uint64_t now);
  void shortsDraw(uint64_t now, double t);
  void playerUpdate(uint64_t now);
  void playerDraw(uint64_t now, double t, int optionCount, bool behindLive, const display::CommentsPanel& commentPanel,
                  bool sleepWarning, double sleepLeft);

  // ---- Música (ui_music.hpp)
  void musicNowInput(uint64_t now);
  void musicNowDraw();
  void musicSleepDraw(double sleepLeft);

  // ---- Configurações (ui_settings.hpp)
  std::optional<int> settingsInput(uint64_t now);
  void settingsDraw();

  // ---- Início (ui_home.hpp)
  void homeInput(uint64_t now);
  void homeDraw(Shelf& cur, double t, double sleepLeft);
};
