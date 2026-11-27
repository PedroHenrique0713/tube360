// Vigia do player para o relatório de erro: a interface viva com o player parado (decodificação travada, abertura que
// nunca termina) não gera relatório sozinha. Parte pura, sem SDL: o main.cpp amostra o player a cada quadro.
// Conservador de propósito: pausa, live, Nest, busca em andamento e fora do player nunca contam como travado.
#pragma once
#include <cmath>
#include <cstdint>

struct PlayerWatch {
  enum class Phase { Idle, Connecting, Buffering, Playing };
  struct Sample {
    uint64_t now = 0;
    bool inPlayer = false, paused = false, live = false, nest = false, seeking = false;
    // Atividade real do pipeline: bytes lidos e quadros decodificados (qualquer mudança conta) e fim de stream.
    uint64_t bytes = 0, frames = 0;
    bool streamEnded = false;
    Phase phase = Phase::Idle;
    double position = 0;
  };
  static constexpr uint64_t connectLimitMs = 120000, bufferLimitMs = 180000, frozenLimitMs = 45000;

  bool stuck = false;
  // Devolve true quando `stuck` mudou neste quadro (quem chama grava no log e avisa o marcador de sessão).
  bool update(const Sample& s) {
    bool was = stuck;
    stuck = evaluate(s);
    return stuck != was;
  }

 private:
  Phase phase_ = Phase::Idle;
  uint64_t since_ = 0, progressAt_ = 0, lastBytes_ = 0, lastFrames_ = 0;
  double lastPosition_ = 0;
  bool armed_ = false;

  void reset(const Sample& s) { phase_ = s.phase; since_ = progressAt_ = s.now; lastPosition_ = s.position; lastBytes_ = s.bytes; lastFrames_ = s.frames; }

  bool evaluate(const Sample& s) {
    if (!s.inPlayer || s.phase == Phase::Idle) { armed_ = false; return false; }
    if (!armed_ || s.phase != phase_) { armed_ = true; reset(s); return false; }
    // Qualquer sinal de vida do lado do usuário ou do som no alto-falante zera a contagem.
    if (s.paused || s.seeking || s.nest || s.streamEnded || (s.live && s.phase == Phase::Playing)) { reset(s); return false; }
    // Só conta como parado sem posição nova E sem pacote lido nem quadro decodificado: fim de stream, áudio mais curto
    // que o vídeo ou fila cheia esperando o relógio não são travamento.
    if (std::fabs(s.position - lastPosition_) > 0.001) { lastPosition_ = s.position; progressAt_ = s.now; }
    if (s.bytes != lastBytes_ || s.frames != lastFrames_) { lastBytes_ = s.bytes; lastFrames_ = s.frames; progressAt_ = s.now; }
    switch (s.phase) {
      case Phase::Connecting: return s.now - progressAt_ >= connectLimitMs;
      case Phase::Buffering: return s.now - progressAt_ >= bufferLimitMs;
      case Phase::Playing: return s.now - progressAt_ >= frozenLimitMs;
      default: return false;
    }
  }
};
