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
  // "Só para membros" vem como selo numa linha de metadados do tile; a conta não abre esses vídeos (yt-dlp: "Seja
  // membro do canal") e a home ainda os promove como "Destaques do YouTube" (2026-11-22).
  const lineBadges = (meta?.lines || []).flatMap((line) => (line.lineRenderer?.items || [])
    .map((i) => i.lineItemRenderer?.badge?.metadataBadgeRenderer?.style).filter(Boolean));
  if (lineBadges.includes('BADGE_STYLE_TYPE_MEMBERS_ONLY')) return null;
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

// Navegação usa renderers de conteúdo: nunca extrai vídeos escondidos em menus ou previews.
const NAV_ID = /^[A-Za-z0-9_-]{2,160}$/;
const CHANNEL_ID = /^UC[A-Za-z0-9_-]{22}$/;
function findNodes(data, name) {
  const out = [];
  function walk(v) {
    if (!v || typeof v !== 'object') return;
    if (Array.isArray(v)) { v.forEach(walk); return; }
    for (const [k, c] of Object.entries(v)) {
      if (k === name) out.push(c);
      else if (!['menu', 'onLongPressCommand', 'onFocusCommand', 'frameworkUpdates'].includes(k)) walk(c);
    }
  }
  walk(data); return out;
}
function thumbnail(data, minWidth = 0) {
  const all = [...findNodes(data, 'thumbnails'), ...findNodes(data, 'sources')].flat().filter((t) => t?.url);
  if (minWidth && all.length) {
    const sorted = all.sort((a, b) => a.width - b.width);
    return (sorted.find((t) => t.width >= minWidth) || sorted.at(-1)).url;
  }
  return all.filter((t) => t.width <= 1024).sort((a, b) => Math.min(b.width, b.height || b.width) - Math.min(a.width, a.height || a.width))[0]?.url || all[0]?.url || '';
}
function browseId(data) { return findNodes(data, 'browseEndpoint').find((v) => CHANNEL_ID.test(v.browseId || ''))?.browseId || ''; }
export function parseNavigation(data, music = false) {
  const items = [], tokens = [], seen = new Set();
  function add(item) {
    if (!item || !NAV_ID.test(item.id || '') || seen.has(item.type + item.id)) return;
    seen.add(item.type + item.id); items.push(item);
  }
  function render(v, key, section) {
    const meta = v.metadata?.tileMetadataRenderer;
    const lockup = v.metadata?.lockupMetadataViewModel;
    const cols = (v.flexColumns || []).map((c) => c.musicResponsiveListItemFlexColumnRenderer?.text);
    const title = label(meta?.title || lockup?.title || v.title || cols[0]);
    const sub = lockup ? (lockup.metadata?.contentMetadataViewModel?.metadataRows || []).flatMap((r) => r.metadataParts || []).map((p) => label(p.text)).join(" · ") : label(v.subtitle || cols[1] || v.shortBylineText || v.longBylineText || v.ownerText);
    const ep = v.onSelectCommand || v.rendererContext?.commandContext?.onTap?.innertubeCommand || v.navigationEndpoint || cols[0]?.runs?.find((r) => r.navigationEndpoint)?.navigationEndpoint || {};
    const watch = ep.watchEndpoint || ep.watchPlaylistEndpoint || v.playlistItemData || {};
    const browse = ep.browseEndpoint || {};
    const id = v.videoId || (/(PLAYLIST|CHANNEL)/.test(v.contentType || "") ? browse.browseId || v.contentId || watch.playlistId : watch.videoId || browse.browseId || v.playlistId || v.channelId || v.contentId || watch.playlistId);
    const base = { id, title: title || id, channel: sub, duration: 0, image: thumbnail(v.header || v.contentImage || v.thumbnailRenderer || v.thumbnail || v),
      channel_id: browseId([cols, v.shortBylineText, v.longBylineText, meta?.lines]), section };
    const artist = cols.flatMap((c) => c?.runs || []).filter((r) => CHANNEL_ID.test(r.navigationEndpoint?.browseEndpoint?.browseId || '')).map((r) => r.text).join(' / ');
    if (music && artist) base.channel = artist;
    if (/PLAYLIST/.test(v.contentType || '')) { add({ ...base, type: 'playlist' }); }
    else if (/CHANNEL/.test(v.contentType || '')) { add({ ...base, type: music ? 'artist' : 'channel' }); }
    else if (key === 'tileRenderer' && v.contentType === 'TILE_CONTENT_TYPE_VIDEO') {
      const normal = normalizeVideo(v, key);
      const timing = v.header?.tileHeaderRenderer?.thumbnailOverlays || [];
      const live = timing.some((o) => /LIVE/.test(o.thumbnailOverlayTimeStatusRenderer?.style || ''));
      if (normal) add({ ...base, ...normal, channel: music ? base.channel || normal.channel : normal.channel, type: 'video' });
      else if (live && !ep.reelWatchEndpoint && !v.isUpcoming) add({ ...base, type: 'video' });
    } else if (['videoRenderer', 'gridVideoRenderer', 'playlistVideoRenderer', 'playlistPanelVideoRenderer'].includes(key)) {
      const normal = normalizeVideo(v, key);
      if (normal) add({ ...base, ...normal, channel: music ? base.channel || normal.channel : normal.channel, type: 'video' });
    } else if (ID.test(id || '') && !ep.reelWatchEndpoint) {
      const duration = seconds(label(v.lengthText)) || [...cols, ...(v.fixedColumns || []).map((c) => c.musicResponsiveListItemFixedColumnRenderer?.text)]
        .map((c) => label(c).split(' • ').map(seconds).find(Boolean)).find(Boolean) || 0;
      add({ ...base, duration, type: 'video' });
    } else {
      const page = browse.browseEndpointContextSupportedConfigs?.browseEndpointContextMusicConfig?.pageType || '';
      const type = /ALBUM/.test(page) ? 'album' : /ARTIST|USER_CHANNEL/.test(page) ? 'artist' :
        CHANNEL_ID.test(id || '') ? (music ? 'artist' : 'channel') : /PLAYLIST/.test(page) || /^(VL|PL|RD|OLAK)/.test(id || '') ? 'playlist' : '';
      if (type) add({ ...base, type });
    }
  }
  function walk(v, section = '') {
    if (!v || typeof v !== 'object') return;
    if (Array.isArray(v)) { v.forEach((c) => walk(c, section)); return; }
    for (const [k, c] of Object.entries(v)) {
      if (['tileRenderer','videoRenderer','gridVideoRenderer','playlistVideoRenderer','playlistPanelVideoRenderer',
        'lockupViewModel','channelRenderer','gridChannelRenderer','playlistRenderer','gridPlaylistRenderer','musicTwoRowItemRenderer','musicResponsiveListItemRenderer'].includes(k)) render(c, k, section);
      else if (k === 'tabRenderer') { if (c.selected || (music && !('selected' in c))) walk(c.content, section); }
      else if (['musicCarouselShelfRenderer','musicShelfRenderer','shelfRenderer'].includes(k)) {
        const heading = label(c.title || c.header?.musicCarouselShelfBasicHeaderRenderer?.title || c.headerRenderer?.shelfHeaderRenderer?.avatarLockup?.avatarLockupRenderer?.title);
        walk(c.contents || c.content, heading || section);
      } else if (k === 'continuationCommand' || k === 'nextContinuationData') tokens.push(c.token || c.continuation);
      else if (k === 'continuations' && music && section) continue;
      else if (!['menu','onLongPressCommand','onFocusCommand','header','thumbnail','navigationEndpoint','onSelectCommand','frameworkUpdates','playerOverlays'].includes(k)) walk(c, section);
    }
  }
  walk(data);
  const outer = music && findNodes(data, 'sectionListRenderer').concat(findNodes(data, 'sectionListContinuation'))
    .flatMap((v) => v.continuations || []).map((v) => v.nextContinuationData?.continuation).find(Boolean);
  return { items, continuation: outer || tokens.find(Boolean) || '' };
}

export class Account {
  state = 'signed_out';
  name = '';
  handle = '';
  avatar = '';
  code = '';
  url = '';
  current = null;
  feeds = new Map();
  videos = new Map();
  navigation = new Map();
  expiry = null;
  loginTask = null;

  status() { return { state: this.state, name: this.name, handle: this.handle, avatar: this.avatar, code: this.code, url: this.url }; }

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
    this.name = this.handle = this.avatar = this.code = this.url = '';
    this.feeds.clear();
    this.videos.clear();
    this.navigation.clear();
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
      this.avatar = selected?.account_photo?.[0]?.url || '';
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

  async navigationPage(kind, id, query, tab, page, fresh = false, filter = 0) {
    const privateList = ['subscriptions', 'library', 'history', 'later'].includes(kind) || id === 'WL';
    if (privateList && this.state !== 'signed_in') throw fail(401, 'signed_out');
    const music = ['music','music_search','album','artist','radio','music_playlist'].includes(kind);
    const signedIn = this.state === 'signed_in';
    const yt = signedIn ? this.current.yt : await this.guest();
    const owner = this.current;
    const key = JSON.stringify([kind, id, query, tab, filter, this.state]);
    let cache = this.navigation.get(key);
    if (!cache || (page === 0 && (fresh || Date.now() - cache.created > 300000))) {
      if (this.navigation.size >= 24) this.navigation.delete(this.navigation.keys().next().value);
      cache = { pages: [], seen: new Set(), tokens: new Set(), created: Date.now(), task: Promise.resolve(), header: {} };
      this.navigation.set(key, cache);
    }
    const task = cache.task.catch(() => {}).then(async () => {
      while (cache.pages.length <= page) {
        if ((privateList || signedIn) && (owner !== this.current || this.state !== 'signed_in')) throw fail(401, 'signed_out');
        const previous = cache.pages.at(-1);
        if (previous && !previous.continuation) return { ...cache.header, items: [], more: false };
        let route = '/browse', args = { client: cache.client || (music ? 'YTMUSIC' : 'TV') };
        if (previous) { args.continuation = previous.continuation; if (kind === 'search' || kind === 'music_search') route = '/search'; else if (kind === 'radio') route = '/next'; }
        else if (kind === 'search' || kind === 'music_search') {
          route = '/search'; args.query = query;
          // Mesmo protobuf SearchFilter da biblioteca 16.0.1: tipo em filters.type ou musicSearchType.
          if (filter) args.params = encodeURIComponent(Buffer.from(kind === 'search' ? [18,2,16,filter] :
            [18,5,138,1,2,({1:8,2:24,3:32})[filter],1]).toString('base64'));
        }
        else if (kind === 'radio') { route = '/next'; args.videoId = id; args.playlistId = 'RDAMVM' + id; }
        else args.browseId = { subscriptions: 'FEsubscriptions', library: 'FEplaylist_aggregation', history: 'FEhistory', later: 'VLWL',
          music: query === 'charts' ? 'FEmusic_explore' : 'FEmusic_home' }[kind] || (['playlist','music_playlist'].includes(kind) && !id.startsWith('VL') ? 'VL' + id : id);
        let data;
        try {
          ({ data } = await yt.actions.execute(route, args));
          if ([401, 403].includes(data.error?.code)) throw fail(data.error.code, 'music_auth');
        } catch (error) {
          // Alguns tokens de pareamento só são aceitos com a identidade TV. Mesmo feed e mesma conta.
          if (!signedIn || previous || args.browseId !== 'FEmusic_home' ||
              !(error.status === 401 || error.status === 403 || /status code (401|403)\b/.test(error.message))) throw error;
          ({ data } = await yt.actions.execute(route, { ...args, client: 'TV' }));
          cache.client = 'TV';
        }
        if (data.error) throw fail(502, 'youtube_unavailable');
        if (!previous && kind === 'music' && query === 'charts') {
          const endpoint = findNodes(data, 'musicNavigationButtonRenderer').map((v) => v.clickCommand?.browseEndpoint)
            .find((v) => v?.browseId === 'FEmusic_charts');
          if (!endpoint) throw fail(502, 'charts_unavailable');
          ({ data } = await yt.actions.execute('/browse', { ...endpoint, client: 'YTMUSIC' }));
        }
        if (!previous && kind === 'channel') {
          const header = findNodes(data, 'channelHeaderRenderer')[0] || findNodes(data, 'c4TabbedHeaderRenderer')[0] || findNodes(data, 'interactiveTabbedHeaderRenderer')[0] || {};
          const subscribe = findNodes(header, 'subscribeButtonRenderer')[0];
          const subtitle = header.subtitle?.lineRenderer?.items?.map((i) => label(i.lineItemRenderer?.text)).join(' ') || label(header.subscriberCountText);
          cache.header = { title: label(header.title), subtitle, image: thumbnail(header.avatar || header.boxArt),
            banner: thumbnail(header.banner || header.tvBanner || findNodes(data, 'tvBannerRenderer')[0] ||
              findNodes(data, 'tvSurfaceContentBackgroundRenderer')[0] || data.background, 1184), channel_id: id, subscribed: subscribe?.subscribed || false };
          const pattern = tab === 'live' ? /live|vivo|transmiss/i : tab === 'playlists' ? /playlists/i : /^videos$|^vídeos$|^uploads$/i;
          const shelves = findNodes(data, 'shelfRenderer');
          const shelf = shelves.find((v) => pattern.test(label(v.headerRenderer?.shelfHeaderRenderer?.avatarLockup?.avatarLockupRenderer?.title)));
          // A TV devolve a página inteira mesmo com params da aba; a continuação da prateleira é o que pagina só a aba.
          data = shelf ? { shelfRenderer: shelf } : { contents: [] };
        }
        if (!previous && !cache.header.title) {
          const header = findNodes(data, 'musicDetailHeaderRenderer')[0] || findNodes(data, 'musicResponsiveHeaderRenderer')[0] ||
            findNodes(data, 'musicImmersiveHeaderRenderer')[0] || findNodes(data, 'musicVisualHeaderRenderer')[0] || findNodes(data, 'playlistHeaderRenderer')[0] || findNodes(data, 'overlayPanelHeaderRenderer')[0] || {};
          cache.header = { title: label(header.title) || query, subtitle: label(header.subtitle) || (header.metadataLines || []).flatMap((l) => l.lineRenderer?.items || []).map((i) => label(i.lineItemRenderer?.text)).filter(Boolean).join(' '), image: thumbnail(header) };
        }
        const parsed = parseNavigation(data, music);
        if (!previous && kind === 'library') {
          // FEplaylist_aggregation não inclui a capa dos dois atalhos fixos.
          const previews = await Promise.allSettled(['history', 'later'].map((list) => this.feed(list, 0, fresh)));
          for (const [i, list] of ['history', 'later'].entries())
            cache.header[list + '_video'] = previews[i].value?.items[0]?.id || '';
        }
        if (kind === 'music' && query === 'mixes') parsed.items = parsed.items.filter((v) => v.type === 'playlist');
        if (kind === 'library') parsed.items = parsed.items.filter((v) => v.id !== 'WL' && v.id !== 'VLWL');
        if (kind === 'subscriptions' && !previous && !parsed.items.some((v) => v.type === 'channel')) {
          const channels = await yt.actions.execute('/browse', { browseId: 'FEchannels', client: 'TV' });
          parsed.items.unshift(...parseNavigation(channels.data).items.filter((v) => v.type === 'channel'));
        }
        const items = parsed.items.filter((v) => { const k = v.type + v.id; if (cache.seen.has(k)) return false; cache.seen.add(k); return true; });
        const continuation = cache.tokens.has(parsed.continuation) ? '' : parsed.continuation;
        if (continuation) cache.tokens.add(continuation);
        if ((privateList || signedIn) && (owner !== this.current || this.state !== 'signed_in')) throw fail(401, 'signed_out');
        // Limite de páginas evita manter listas inteiras de milhares de itens na RAM do servidor.
        cache.pages.push({ items, continuation: cache.pages.length < 40 ? continuation : '' });
      }
      return { ...cache.header, ...cache.pages[page], more: Boolean(cache.pages[page].continuation) };
    });
    cache.task = task; return task;
  }

  async playlistPreview(id) {
    if (this.state !== 'signed_in') throw fail(401, 'signed_out');
    const owner = this.current, browseId = id.startsWith('VL') ? id : 'VL' + id;
    const seen = new Set(), until = Date.now() + 7000;
    let args = { browseId }, newest = false, last = '';
    while (Date.now() < until && seen.size <= 40) {
      const { data } = await owner.yt.actions.execute('/browse', { ...args, client: 'TV' });
      if (owner !== this.current) throw fail(401, 'signed_out');
      if (data.error) throw fail(502, 'playlist_unavailable');
      if (!seen.size && !newest) {
        const sort = findNodes(data, 'sortFilterSubMenuRenderer').flatMap((m) => m.subMenuItems || [])
          .find((m) => /date added.*newest|adi[cç].*recent/i.test(label(m.title)));
        const endpoint = sort?.navigationEndpoint || sort?.serviceEndpoint;
        if (sort?.selected) newest = true;
        else if (endpoint?.browseEndpoint || endpoint?.continuationCommand?.token) {
          newest = true;
          args = endpoint.browseEndpoint ? { browseId, ...endpoint.browseEndpoint } : { continuation: endpoint.continuationCommand.token };
          continue;
        }
      }
      const parsed = parseNavigation(data);
      const videos = parsed.items.filter((item) => item.type === 'video');
      if (videos.length) {
        if (newest || /^(VL)?RD/.test(id)) return { video: videos[0].id };
        last = videos.at(-1).id;
      }
      if (!parsed.continuation) return { video: last };
      if (seen.has(parsed.continuation)) break;
      seen.add(parsed.continuation); args = { continuation: parsed.continuation };
    }
    return { video: '' };
  }

  async channelForVideo(id) {
    const yt = this.state === 'signed_in' ? await this.player(this.current) : await this.guest();
    const channel = this.state === 'signed_in' ? (await yt.getBasicInfo(id)).basic_info.channel_id :
      (await yt.actions.execute('/player', { videoId: id, client: 'TV' })).data.videoDetails?.channelId;
    if (!CHANNEL_ID.test(channel || '')) throw fail(502, 'channel_unavailable');
    return { id: channel };
  }

  async navigationAction(action, id, value) {
    if (this.state !== 'signed_in') throw fail(401, 'signed_out');
    const yt = this.current.yt;
    let response;
    if (action === 'subscribe') response = await yt.actions.execute(value ? '/subscription/subscribe' : '/subscription/unsubscribe',
      { channelIds: [id], params: value ? 'EgIIAhgA' : 'CgIIAhgA', client: 'TV' });
    else response = await yt.actions.execute('/browse/edit_playlist', { playlistId: 'WL', actions: [{ action: 'ACTION_ADD_VIDEO', addedVideoId: id }], client: 'TV' });
    if (!response.success || response.data?.error) throw fail(502, 'action_failed');
    this.navigation.clear(); this.feeds.delete(action === 'subscribe' ? 'subs' : 'later');
    return { ok: true };
  }

  async lyrics(id) {
    const yt = await this.guest();
    try { const result = await yt.music.getLyrics(id); return { lyrics: text(result?.description?.toString()) }; }
    catch { return { lyrics: '' }; }
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
        retrieve_player: false, enable_session_cache: false, fetch: (input, init) => fetch(input, { ...init, signal: AbortSignal.timeout(20000) }) });
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
      else if (req.method === 'GET' && url.pathname === '/nav') {
        const kind = url.searchParams.get('kind') || '', id = url.searchParams.get('id') || '';
        const query = text(url.searchParams.get('q')).slice(0, 200), tab = url.searchParams.get('tab') || 'videos';
        const page = Number(url.searchParams.get('page') || 0);
        if (!['channel','subscriptions','library','playlist','history','later','search','music','music_search','album','artist','radio','music_playlist'].includes(kind) ||
            !Number.isInteger(page) || page < 0 || page > 40 || (id && !NAV_ID.test(id)) ||
            (['channel','artist'].includes(kind) && !CHANNEL_ID.test(id)) || (kind === 'radio' && !ID.test(id)) ||
            (['playlist','music_playlist','album'].includes(kind) && !id) || !['videos','live','playlists'].includes(tab)) throw fail(400, 'invalid_navigation');
        result = await account.navigationPage(kind, id, query, tab, page, url.searchParams.get('fresh') === '1', Math.max(0, Math.min(3, Number(url.searchParams.get('filter')) || 0)));
      } else if (req.method === 'GET' && url.pathname === '/playlist_preview') {
        const id = url.searchParams.get('id') || '';
        if (!NAV_ID.test(id)) throw fail(400, 'invalid_playlist');
        result = await account.playlistPreview(id);
      } else if (req.method === 'GET' && ['/channel_for_video','/lyrics'].includes(url.pathname)) {
        const id = url.searchParams.get('id') || ''; if (!ID.test(id)) throw fail(400, 'invalid_video');
        result = url.pathname === '/lyrics' ? await account.lyrics(id) : await account.channelForVideo(id);
      } else if (req.method === 'POST' && url.pathname === '/nav/action') {
        const action = url.searchParams.get('action'), id = url.searchParams.get('id') || '';
        if (!(action === 'subscribe' ? CHANNEL_ID.test(id) : action === 'later' && ID.test(id))) throw fail(400, 'invalid_action');
        result = await account.navigationAction(action, id, url.searchParams.get('value') === '1');
      }
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
