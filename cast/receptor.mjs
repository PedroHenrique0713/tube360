// Receptor de "Transmitir" do app oficial do YouTube para o YouTube 360 (Xbox 360).
// O celular (logado no Premium) vê "YouTube 360" como uma TV. Cada comando vira um comando para o console pelo canal do
// controle remoto do servidor (server/, /api/remote/cmd); o estado volta pelo /api/remote/state (o console informa
// vídeo, posição, duração e pausa a cada ~0,5 s). Volume vai para o Nest. Sem descoberta automática na rede, o pareamento
// é pelo código de "Assistir na TV", gravado em ~/.cache/tube360/cast-code.txt (o console mostra no painel Celular).
import { startAccountService } from './account.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import YouTubeCastReceiver, { Player, PLAYER_STATUSES, LOG_LEVELS } from 'yt-cast-receiver';

const API = process.env.YT360_API || 'http://127.0.0.1:8091';
const CODE_FILE = path.join(os.homedir(), '.cache/tube360/cast-code.txt');
const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);

async function cmd(body) {
  const r = await fetch(`${API}/api/remote/cmd`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
  if (!r.ok) throw new Error(`cmd ${body.action}: HTTP ${r.status}`);
}
async function state() {
  const r = await fetch(`${API}/api/remote/state`);
  return r.json();
}

class XboxPlayer extends Player {
  constructor() {
    super();
    this.videoId = '';
    this.volume = { level: 70, muted: false };
    this.lastEndedSeq = -1;
    this.sawPlaying = false;
    this.quietUntil = 0;   // depois de um comando do celular, o Xbox leva ~1 s para aplicar: não "desfazer" nesse meio
    this.startAt = 0;      // posição pedida no último play (o Xbox ainda não informou a dele)
  }
  hush() { this.quietUntil = Date.now() + 3000; }

  async doPlay(video, position) {
    log('tocar', video.id, 'em', position);
    this.videoId = video.id;
    this.sawPlaying = false;
    this.startAt = Math.max(0, position || 0);
    this.hush();
    await cmd({ action: 'play', id: video.id, t: Math.max(0, position || 0) });
    return true;
  }
  async doPause() { log('pausar'); this.hush(); await cmd({ action: 'pause' }); return true; }
  async doResume() { log('continuar'); this.hush(); await cmd({ action: 'resume' }); return true; }
  async doStop() { log('parar'); this.videoId = ''; await cmd({ action: 'stop' }); return true; }
  async doSeek(position) {
    // o fluxo do console recomeça no ponto pedido (o servidor converte a partir de t)
    if (!this.videoId) return false;
    log('pular para', position);
    this.startAt = Math.max(0, position);
    this.hush();
    await cmd({ action: 'play', id: this.videoId, t: Math.max(0, position) });
    return true;
  }
  async doSetVolume(volume) {
    this.volume = volume;
    const level = volume.muted ? 0 : volume.level;
    fetch(`${API}/api/nest/volume?v=${level}`).catch(() => {});
    return true;
  }
  async doGetVolume() { return this.volume; }
  // Enquanto o Xbox ainda não está no vídeo pedido, informa o ponto pedido e duração desconhecida (0), nunca a do anterior.
  async doGetPosition() { try { const s = await state(); return s.playing === this.videoId ? (s.pos || 0) : this.startAt; } catch { return this.startAt; } }
  async doGetDuration() { try { const s = await state(); return s.playing === this.videoId ? (s.dur || 0) : 0; } catch { return 0; } }

  // Acompanha o console: fim do vídeo (pede o próximo da fila), pausa e volta pelo controle do Xbox.
  async watch() {
    let s;
    try { s = await state(); } catch { return; }
    if (this.status !== this.lastLogged) { log('estado do player:', this.status, '| Xbox:', s.playing || '(grade)', s.paused ? 'pausado' : '', Math.round(s.pos)); this.lastLogged = this.status; }
    if (this.lastEndedSeq < 0) this.lastEndedSeq = s.ended_seq;
    const ours = this.videoId && s.playing === this.videoId;
    if (ours && !this.sawPlaying && s.dur > 0) {
      // o Xbox começou o vídeo: manda ao celular a duração e a posição de verdade
      this.sawPlaying = true;
      await this.notifyExternalStateChange();
      this.lastSync = Date.now();
    }
    if (ours && Date.now() - (this.lastSync || 0) > 15000) { this.lastSync = Date.now(); await this.notifyExternalStateChange(); }
    if (s.ended_seq !== this.lastEndedSeq) {
      this.lastEndedSeq = s.ended_seq;
      if (this.videoId && s.ended === this.videoId && this.status !== PLAYER_STATUSES.IDLE) {
        log('fim de', this.videoId, '→ próximo da fila');
        try { await this.pause(); await this.next(); } catch (e) { log('próximo falhou:', e.message); }
        return;
      }
    }
    if (Date.now() < this.quietUntil) return;
    if (ours && s.paused && this.status === PLAYER_STATUSES.PLAYING) await this.notifyExternalStateChange(PLAYER_STATUSES.PAUSED);
    else if (ours && !s.paused && this.status === PLAYER_STATUSES.PAUSED) await this.notifyExternalStateChange(PLAYER_STATUSES.PLAYING);
    else if (this.sawPlaying && !s.playing && s.connected && (this.status === PLAYER_STATUSES.PLAYING || this.status === PLAYER_STATUSES.PAUSED)) {
      // voltou para a grade pelo controle (B): no celular, a reprodução para
      this.sawPlaying = false;
      log('Xbox voltou para a grade → parado no celular');
      await this.notifyExternalStateChange(PLAYER_STATUSES.STOPPED);
    }
  }
}

const accountService = await startAccountService();
const player = new XboxPlayer();
const receiver = new YouTubeCastReceiver(player, {
  device: { name: process.env.TUBE360_NAME || 'Tube360', screenName: process.env.TUBE360_NAME || 'Tube360', brand: 'Microsoft', model: 'Xbox 360' },
  dial: { port: Number(process.env.YT360_DIAL_PORT || 8098) },
  logLevel: process.env.YT360_LOG === 'debug' ? LOG_LEVELS.DEBUG : LOG_LEVELS.INFO,
});
// avisa o servidor, que mostra no Xbox ("Celular conectado: celular")
const nome = (sender) => sender.user?.name || sender.name || 'celular';
const evento = (kind, sender) => fetch(`${API}/api/cast/event`, { method: 'POST', headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ kind, name: nome(sender) }) }).catch(() => {});
receiver.on('senderConnect', (sender) => { log('conectado:', nome(sender), sender.device?.model || ''); evento('connect', sender); });
receiver.on('senderDisconnect', (sender, implicit) => { log('desconectado:', nome(sender), implicit ? '(caiu)' : ''); evento('disconnect', sender); });
receiver.on('error', (error) => log('erro:', error.message));
receiver.on('terminate', (error) => { log('encerrado:', error?.message); process.exit(1); });

await receiver.start();
log('receptor no ar (DIAL na porta', process.env.YT360_DIAL_PORT || 8098, ')');

const pairing = receiver.getPairingCodeRequestService();
pairing.on('response', (code) => {
  const pretty = String(code).replace(/(\d{3})(?=\d)/g, '$1 ');
  fs.mkdirSync(path.dirname(CODE_FILE), { recursive: true });
  fs.writeFileSync(CODE_FILE, pretty + '\n');
  log('código de "Assistir na TV":', pretty);
});
pairing.on('error', (error) => { log('código falhou:', error.message); setTimeout(() => pairing.start(), 30000); });
pairing.start();

setInterval(() => player.watch().catch((e) => log('watch:', e.message)), 1000);
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, async () => { accountService.close(); pairing.stop(); await receiver.stop().catch(() => {}); process.exit(0); });
