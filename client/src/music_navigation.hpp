#pragma once
#include "navigation.hpp"
#include "music_queue.hpp"

struct MusicNavigation : Navigation {
  bool localQueue = false;
  size_t queueIndex = 0;
  std::string queueStart;
  bool nativePage() const { return !pages.empty() && pages.back().native; }

  // Tocando agora. Zonas do foco: botões, barra de tempo, lista da fila, abas da direita, seletor Música/Vídeo.
  enum Zone { ZoneButtons, ZoneSeek, ZoneQueue, ZoneTabs, ZoneMode };
  enum Button { BtnPrev, BtnBack10, BtnPlay, BtnForward30, BtnNext, BtnLike, BtnDislike, BtnRepeat, BtnShuffle, BtnCount };
  int queueSel = 0, queueTop = 0, tab = 0;      // tab: 0 = A seguir, 1 = Letra, 2 = Relacionados
  int relatedSel = 0, relatedTop = 0, rowsTab = -1;
  bool autoplay = true, autoFocus = false, continuing = false, relatedLoaded = false;  // autoplay: ao fim da fila, segue com a rádio
  std::vector<NavItem> related; std::string relatedId;
  int repeat = musicqueue::OFF;
  bool shuffled = false, moving = false, videoMode = false;
  bool seekPending = false; double seekTarget = 0; uint64_t seekAt = 0;
  std::vector<NavItem> queueSaved;              // ordem antes do aleatório
  std::string rating;                           // LIKE, DISLIKE ou vazio, copiado do `likeState` do main a cada quadro
  std::vector<std::string> lyricWrap; std::string lyricWrapSig;  // letra quebrada na largura da coluna
  std::vector<std::string> rowTitle, rowArtist; // textos já cortados da janela da fila
  int rowsFor = -1; float rowsWidth = 0; uint64_t rowsRev = 0, queueRev = 1;
  float builtWidth = -1; bool builtLarge = false;  // layout para o qual título e artista foram cortados

  void clearLocalQueue() { localQueue = false; ownsQueue = false; queue.clear(); queueStart.clear(); resetNow(); }
  void resetNow() {
    queueSel = queueTop = tab = relatedSel = relatedTop = 0; autoFocus = false; related.clear(); relatedId.clear(); relatedLoaded = false; repeat = musicqueue::OFF; shuffled = moving = videoMode = seekPending = false;
    queueSaved.clear(); nowZone = 0; nowSel = BtnPlay; ++queueRev; lyricWrap.clear(); lyricWrapSig.clear();
  }
  void queueChanged() { ++queueRev; }
  // Reordenar à mão desfaz o "voltar à ordem original" do aleatório (a ordem original já não vale).
  void saveOrderBroken() { queueSaved.clear(); shuffled = false; }
  bool advance(size_t& to) const { return localQueue && musicqueue::advance(queueIndex, queue.size(), repeat, to); }
  bool skipNext(size_t& to) const { return localQueue && musicqueue::next(queueIndex, queue.size(), repeat, to); }
  bool skipPrevious(double position, size_t& to) const { return localQueue && musicqueue::previous(queueIndex, queue.size(), repeat, position, to); }
  void toggleShuffle(uint32_t seed) {
    if (!localQueue || queue.size() < 3) return;
    if (shuffled) { musicqueue::unshuffle(queue, queueSaved, queueIndex); shuffled = false; }
    else { musicqueue::shuffle(queue, queueSaved, queueIndex, seed); shuffled = true; }
    queueSel = (int)queueIndex; queueChanged();
  }
  void prepareNow(gfx::Gfx& g, Video& playing, i18n::Lang lang);
  void drawNow(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const Video& playing, double pos, double duration, bool paused, bool waiting, K error, uint64_t frame, i18n::Lang lang);
};
