// Conta do YouTube por código de TV. Também roda sozinho, sem DIAL nem código de Cast.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Innertube, ClientType, Log } from 'youtubei.js';

const BASE = path.dirname(fileURLToPath(import.meta.url));
const TOKEN = process.env.TUBE360_ACCOUNT_TOKEN || path.join(os.homedir(), '.cache/tube360/yt-conta.json');
const CONFIG = process.env.TUBE360_CONFIG || path.join(BASE, '../server/config.json');
const ID = /^[A-Za-z0-9_-]{11}$/;
const text = (value) => String(value ?? '').replace(/[\x00-\x1f\x7f]+/g, ' ').trim();
const fail = (status, code) => Object.assign(new Error(code), { status, code });
Log.setLevel(Log.Level.ERROR);
// O player do cliente TV desta youtubei.js (7.20250219) recebe "A página precisa ser atualizada", sem o rastreio que grava
// o histórico. Como TV Samsung, a identidade do tv_samsung do yt-dlp, vem completo (2026-11-22). Só para player e
// histórico: os feeds seguem na sessão TV normal, cujo layout de tiles o normalizeVideo conhece.
const SAMSUNG = {
  clientVersion: '5.20260707', deviceMake: 'Samsung', deviceModel: 'UKS9800', osName: 'Tizen', osVersion: '2.4.0',
  userAgent: 'Mozilla/5.0 (SMART-TV; Linux; Tizen 2.4.0) AppleWebKit/538.1 (KHTML, like Gecko) Version/2.4.0 TV Safari/538.1',
};

function language() {
  try { return JSON.parse(fs.readFileSync(CONFIG, 'utf8')).language === 'pt' ? 'pt' : 'en'; }
  catch { return 'en'; }
}

function seconds(value) {
  const s = text(value);
  return /^\d+(?::\d{2}){1,2}$/.test(s) ? s.split(':').reduce((n, v) => n * 60 + Number(v), 0) : 0;
}

const label = (value) => text(typeof value === 'string' ? value : value?.simpleText ??
  value?.runs?.map((r) => r.text).join('') ?? value?.content ?? '');

export function normalizeVideo(video, renderer = 'tileRenderer') {
  const tile = renderer === 'tileRenderer';
  if (tile && video.contentType !== 'TILE_CONTENT_TYPE_VIDEO') return null;
  const endpoint = tile ? video.onSelectCommand : video.navigationEndpoint;
  const id = video.videoId || video.contentId || endpoint?.watchEndpoint?.videoId;
  const header = video.header?.tileHeaderRenderer;
  const meta = video.metadata?.tileMetadataRenderer;
  const overlays = header?.thumbnailOverlays || video.thumbnailOverlays || [];
  const timing = overlays.map((o) => o.thumbnailOverlayTimeStatusRenderer).filter(Boolean);
  const badges = video.badges || [];
  if (!ID.test(id || '') || endpoint?.reelWatchEndpoint ||
      endpoint?.commandMetadata?.webCommandMetadata?.url?.includes('/shorts/') ||
      video.isLive || video.isUpcoming || video.upcomingEventData || video.isPlayable === false ||
      timing.some((o) => /LIVE|UPCOMING|SHORTS/.test(o.style || '')) ||
      badges.some((o) => /LIVE|UPCOMING|SHORTS/.test(o.metadataBadgeRenderer?.style || ''))) return null;
  const duration = seconds(label(video.lengthText)) || timing.map((o) => seconds(label(o.text))).find(Boolean) || 0;
  if (!duration) return null;
  const lines = (meta?.lines || []).map((line) => (line.lineRenderer?.items || [])
    .map((i) => label(i.lineItemRenderer?.text)).filter(Boolean));
  const progress = overlays.find((o) => o.thumbnailOverlayResumePlaybackRenderer)
    ?.thumbnailOverlayResumePlaybackRenderer.percentDurationWatched;
  const pos = Math.round(duration * Math.max(0, Math.min(100, Number(progress) || 0)) / 100);
  return { id, title: label(meta?.title || video.title) || id,
    channel: lines[0]?.join(' ') || label(video.shortBylineText || video.longBylineText || video.ownerText), duration,
    views_text: label(video.shortViewCountText || video.viewCountText) ||
      lines.slice(1).flat().find((v) => /view|visualiza/i.test(v)) || '', pos };
}

// A 16.0.1 não tem parser de tiles TV. Lemos só renderers de conteúdo, sem menus de ações.
export function parseTVFeed(data) {
  const items = [], continuations = [];
  function walk(value, depth = 0) {
    if (!value || typeof value !== 'object') return;
    if (Array.isArray(value)) { for (const v of value) walk(v, depth + 1); return; }
    for (const [key, child] of Object.entries(value)) {
      if (['tileRenderer', 'videoRenderer', 'gridVideoRenderer', 'playlistVideoRenderer'].includes(key)) {
        const item = normalizeVideo(child, key);
        if (item) items.push(item);
      } else if (key === 'tabRenderer') {
        if (child.selected) walk(child.content, depth + 1);
      } else if (key === 'nextContinuationData' || key === 'continuationCommand') {
        const token = child.continuation || child.token;
        if (token) continuations.push({ token, depth });
      } else if (!['onLongPressCommand', 'menu', 'header', 'onFocusCommand', 'navigationEndpoint',
        'onSelectCommand', 'frameworkUpdates'].includes(key)) walk(child, depth + 1);
    }
  }
  walk(data);
  // A continuação externa avança a lista; as de dentro avançam só uma prateleira da home.
  continuations.sort((a, b) => a.depth - b.depth);
  return { items, continuation: continuations[0]?.token || '' };
}

export class Account {
  state = 'signed_out';
  name = '';
  handle = '';
  code = '';
  url = '';
  current = null;
  feeds = new Map();
  videos = new Map();
  expiry = null;
  loginTask = null;

  status() { return { state: this.state, name: this.name, handle: this.handle, code: this.code, url: this.url }; }

  save(credentials) {
    fs.mkdirSync(path.dirname(TOKEN), { recursive: true, mode: 0o700 });
    const tmp = `${TOKEN}.${process.pid}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify(credentials), { mode: 0o600 });
    fs.chmodSync(tmp, 0o600);
    fs.renameSync(tmp, TOKEN);
  }

  reset() {
    if (this.current) this.current.cancelled = true;
    this.current = null;
    clearTimeout(this.expiry);
    this.state = 'signed_out';
    this.name = this.handle = this.code = this.url = '';
    this.feeds.clear();
    this.videos.clear();
  }

  async client() {
    const lang = language();
    const context = { cancelled: false, deadline: Infinity, yt: null };
    this.current = context;
    const fetcher = async (input, init) => {
      const url = String(input?.url || input);
      const polling = url.includes('/o/oauth2/token') && String(init?.body).includes('device/1.0');
      const expired = () => new Response(JSON.stringify({ error: 'expired_token' }), { status: 400 });
      if (polling && (context.cancelled || Date.now() >= context.deadline)) return expired();
      try {
        const response = await fetch(input, { ...init, signal: AbortSignal.timeout(20000) });
        // A biblioteca guarda o timer do polling internamente; expired_token o encerra no próximo tick.
        if (polling && (context.cancelled || Date.now() >= context.deadline)) return expired();
        return response;
      } catch (error) {
        // O callback de setInterval da biblioteca não captura falhas de rede.
        if (polling) return new Response(JSON.stringify({ error: 'authorization_pending' }));
        throw error;
      }
    };
    const yt = await Innertube.create({ client_type: ClientType.TV, lang, location: lang === 'pt' ? 'BR' : 'US',
      retrieve_player: false, enable_session_cache: false, fetch: fetcher });
    context.yt = yt;
    const active = () => this.current === context && !context.cancelled;
    yt.session.on('auth-pending', (data) => {
      if (!active()) return;
      this.state = 'pending';
      this.code = text(data.user_code);
      this.url = /^https:\/\//.test(data.verification_url) ? data.verification_url : 'https://www.google.com/device';
      context.deadline = Date.now() + (Number(data.expires_in) || 600) * 1000;
      this.expiry = setTimeout(() => { if (active() && this.state === 'pending') this.reset(); }, context.deadline - Date.now());
      this.ready?.();
    });
    yt.session.on('auth', ({ credentials }) => {
      if (!active()) return;
      this.save(credentials);
      clearTimeout(this.expiry);
      this.state = 'signed_in';
      this.code = this.url = '';
      this.feeds.clear();
      this.ready?.();
      // signIn atualiza logged_in depois do evento auth.
      context.identity = Promise.resolve().then(() => this.identity(context));
    });
    yt.session.on('update-credentials', ({ credentials }) => { if (active()) this.save(credentials); });
    yt.session.on('auth-error', () => { if (!active()) return; this.reset(); this.ready?.(); });
    return context;
  }

  async identity(context) {
    try {
      const info = await context.yt.account.getInfo();
      const accounts = info.contents?.contents || [];
      const selected = accounts.find((a) => a.is_selected) || accounts.find((a) => a.type === 'AccountItem');
      if (this.current !== context) return;
      this.name = text(selected?.account_name);
      this.handle = text(selected?.channel_handle);
    } catch { console.warn('Conta: nome indisponível.'); }
  }

  async restore() {
    if (!fs.existsSync(TOKEN)) return;
    try {
      const credentials = JSON.parse(fs.readFileSync(TOKEN, 'utf8'));
      const context = await this.client();
      await context.yt.session.signIn(credentials);
      await context.identity;
    } catch { this.reset(); console.warn('Conta: não foi possível restaurar o token.'); }
  }

  async login() {
    if (this.state === 'signed_in' || this.state === 'pending') return this.status();
    if (this.loginTask) return this.loginTask;
    this.loginTask = (async () => {
      const ready = new Promise((resolve) => { this.ready = resolve; });
      try {
        this.reset();
        const context = await this.client();
        context.yt.session.signIn().catch(() => {
          if (this.current !== context) return;
          this.reset();
          this.ready?.();
        });
        await ready;
        return this.status();
      } finally { this.loginTask = null; this.ready = null; }
    })();
    return this.loginTask;
  }

  async logout() {
    const yt = this.current?.yt;
    this.reset();
    this.ready?.();
    fs.rmSync(TOKEN, { force: true });
    if (yt?.session.logged_in) {
      try { await yt.session.signOut(); } catch { console.warn('Conta: revogação indisponível; token local apagado.'); }
    }
    return this.status();
  }

  async feed(kind, page, fresh = false) {
    if (this.state !== 'signed_in') throw fail(401, 'signed_out');
    const context = this.current;
    let cache = this.feeds.get(kind);
    if (!cache || (page === 0 && (fresh || Date.now() - cache.created > 300000))) {
      cache = { pages: [], seen: new Set(), tokens: new Set(), task: Promise.resolve(), created: Date.now() };
      this.feeds.set(kind, cache);
    }
    // Duas requisições simultâneas não consomem a mesma continuação duas vezes.
    const task = cache.task.catch(() => {}).then(async () => {
      while (cache.pages.length <= page) {
        if (this.current !== context) throw fail(401, 'signed_out');
        const previous = cache.pages.at(-1);
        if (previous && !previous.continuation) return { items: [], more: false };
        const browseId = { home: 'FEwhat_to_watch', subs: 'FEsubscriptions', history: 'FEhistory', later: 'VLWL' }[kind];
        const response = await context.yt.actions.execute('/browse', {
          ...(previous ? { continuation: previous.continuation } : { browseId }), client: 'TV',
        });
        if (response.data.error) throw fail(502, 'youtube_unavailable');
        const feed = parseTVFeed(response.data);
        if (this.current !== context) throw fail(401, 'signed_out');
        const items = [];
        for (const item of feed.items) {
          if (item && !cache.seen.has(item.id)) { items.push(item); cache.seen.add(item.id); }
        }
        const continuation = cache.tokens.has(feed.continuation) ? '' : feed.continuation;
        if (continuation) cache.tokens.add(continuation);
        cache.pages.push({ continuation, items });
      }
      const result = cache.pages[page];
      return { items: result.items, more: Boolean(result.continuation) };
    });
    cache.task = task;
    return task;
  }

  // Sessão à parte com a identidade de TV Samsung e as mesmas credenciais, criada no primeiro uso.
  player(context) {
    if (context.player) return context.player;
    context.player = (async () => {
      const fetcher = (input, init = {}) => {
        const headers = new Headers(init.headers || input?.headers);
        if (headers.has('user-agent')) headers.set('user-agent', SAMSUNG.userAgent);
        return fetch(input, { ...init, headers, signal: AbortSignal.timeout(20000) });
      };
      const lang = language();
      const yt = await Innertube.create({ client_type: ClientType.TV, lang, location: lang === 'pt' ? 'BR' : 'US',
        retrieve_player: false, enable_session_cache: false, user_agent: SAMSUNG.userAgent, fetch: fetcher });
      Object.assign(yt.session.context.client, SAMSUNG);
      await yt.session.signIn(JSON.parse(fs.readFileSync(TOKEN, 'utf8')));
      return yt;
    })();
    const job = context.player;
    job.catch(() => { if (context.player === job) context.player = null; });
    return job;
  }

  // Sem conta, o "a seguir" sai de uma sessão TV anônima.
  guest() {
    if (!this.guestYt) {
      const lang = language();
      this.guestYt = Innertube.create({ client_type: ClientType.TV, lang, location: lang === 'pt' ? 'BR' : 'US',
        retrieve_player: false, enable_session_cache: false });
      this.guestYt.catch(() => { this.guestYt = null; });
    }
    return this.guestYt;
  }

  // Like do vídeo e o próximo da reprodução automática: a mesma resposta /next que a TV pede ao abrir o vídeo.
  async watchNext(id) {
    const signedIn = this.state === 'signed_in';
    const yt = signedIn ? this.current.yt : await this.guest();
    const { data } = await yt.actions.execute('/next', { videoId: id, client: 'TV' });
    const buttons = data.transportControls?.transportControlsRenderer?.buttons || [];
    const like = signedIn ? buttons.map((b) => b.button?.likeButtonRenderer?.likeStatus).find(Boolean) || '' : '';
    const next = data.playerOverlays?.playerOverlayRenderer?.autoplay?.playerOverlayAutoplayRenderer;
    const nextId = next?.videoId;
    return { like, next: ID.test(nextId || '') && nextId !== id
      ? { id: nextId, title: label(next.videoTitle) || nextId, channel: label(next.byline) } : null };
  }

  // interact.like da 16.0.1 manda o alvo como texto e leva 400; a API quer { videoId }.
  async rate(id, rating) {
    if (this.state !== 'signed_in') throw fail(401, 'signed_out');
    const route = { like: '/like/like', dislike: '/like/dislike', none: '/like/removelike' }[rating];
    if (!route) throw fail(400, 'invalid_rating');
    const { success } = await this.current.yt.actions.execute(route, { target: { videoId: id }, client: 'TV' });
    if (!success) throw fail(502, 'rate_failed');
    return { ok: true, like: { like: 'LIKE', dislike: 'DISLIKE', none: 'INDIFFERENT' }[rating] };
  }

  async watched(id, pos, progressOnly = false) {
    if (this.state !== 'signed_in') throw fail(401, 'signed_out');
    const context = this.current;
    if (!progressOnly || !this.videos.has(id)) {
      if (this.videos.size >= 16) this.videos.delete(this.videos.keys().next().value);
      const job = this.player(context).then((yt) => yt.getBasicInfo(id)).then(async (info) => {
        if (this.current !== context) throw fail(401, 'signed_out');
        if (!info.page[0].playback_tracking) throw fail(502, 'tracking_unavailable');
        if (!progressOnly && !(await info.addToWatchHistory('TVHTML5', SAMSUNG.clientVersion)).ok) throw fail(502, 'history_failed');
        return info;
      });
      this.videos.set(id, job);
      job.catch(() => { if (this.videos.get(id) === job) this.videos.delete(id); });
    }
    const info = await this.videos.get(id);
    if (this.current !== context) throw fail(401, 'signed_out');
    if (pos > 0 && !(await info.updateWatchTime(pos, 'TVHTML5', SAMSUNG.clientVersion)).ok) throw fail(502, 'progress_failed');
    this.feeds.delete('history');
    return { ok: true };
  }
}

export async function startAccountService() {
  const account = new Account();
  const restored = account.restore();
  const server = http.createServer(async (req, res) => {
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    res.setHeader('Cache-Control', 'no-store');
    try {
      await restored;
      // A API local não é destinada a formulários de outras origens.
      if (req.headers.origin) throw fail(403, 'invalid_origin');
      const url = new URL(req.url, 'http://127.0.0.1');
      let result;
      if (req.method === 'GET' && url.pathname === '/status') result = account.status();
      else if (req.method === 'POST' && url.pathname === '/login') result = await account.login();
      else if (req.method === 'POST' && url.pathname === '/logout') result = await account.logout();
      else if (req.method === 'GET' && url.pathname === '/feed') {
        const kind = url.searchParams.get('kind') || 'home';
        const page = Number(url.searchParams.get('page') || 0);
        if (!['home', 'subs', 'history', 'later'].includes(kind) || !Number.isInteger(page) || page < 0 || page > 1000) throw fail(400, 'invalid_feed');
        result = await account.feed(kind, page, url.searchParams.get('fresh') === '1');
      } else if (req.method === 'POST' && ['/watched', '/progress'].includes(url.pathname)) {
        let body = '';
        for await (const chunk of req) {
          body += chunk;
          if (Buffer.byteLength(body) > 4096) throw fail(413, 'body_too_large');
        }
        let data;
        try { data = JSON.parse(body); } catch { throw fail(400, 'invalid_body'); }
        if (!data || typeof data !== 'object' || Array.isArray(data)) throw fail(400, 'invalid_body');
        if (url.pathname === '/progress' && data.pos === undefined) throw fail(400, 'invalid_video');
        if (!ID.test(data.id || '') || (data.pos !== undefined && (!Number.isFinite(data.pos) || data.pos < 0 || data.pos > 86400 * 30))) throw fail(400, 'invalid_video');
        result = await account.watched(data.id, data.pos || 0, url.pathname === '/progress');
      } else if (['/next', '/rate'].includes(url.pathname) && req.method === (url.pathname === '/next' ? 'GET' : 'POST')) {
        const id = url.searchParams.get('id') || '';
        if (!ID.test(id)) throw fail(400, 'invalid_video');
        result = url.pathname === '/next' ? await account.watchNext(id) : await account.rate(id, url.searchParams.get('rating'));
      } else throw fail(404, 'not_found');
      res.end(JSON.stringify(result));
    } catch (error) {
      res.statusCode = error.status || 502;
      res.end(JSON.stringify({ error: error.code || 'youtube_unavailable' }));
    }
  });
  const port = Number(process.env.TUBE360_ACCOUNT_PORT || 8099);
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
  console.log(`Conta: HTTP local na porta ${port}`);
  return { account, server, close: () => { account.reset(); server.close(); } };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const service = await startAccountService();
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => { service.close(); process.exit(0); });
}
