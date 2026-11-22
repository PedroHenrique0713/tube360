'use strict';
const $ = selector => document.querySelector(selector);
const clock = seconds => {
  seconds = Math.max(0, Math.floor(Number(seconds) || 0));
  const h = Math.floor(seconds / 3600), m = Math.floor(seconds % 3600 / 60), s = seconds % 60;
  return h ? `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}` : `${m}:${String(s).padStart(2, '0')}`;
};
let selectedVideo, currentState, feedController, currentQuery = '', toastTimer, renderedQueue = '';
const knownVideos = new Map();

function toast(message, error = false) {
  clearTimeout(toastTimer);
  $('#toast').textContent = message;
  $('#toast').classList.toggle('error', error);
  $('#toast').hidden = false;
  toastTimer = setTimeout(() => { $('#toast').hidden = true; }, 4200);
}

async function send(command) {
  const response = await fetch('/api/remote/cmd', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(command) });
  if (!response.ok) throw new Error(response.status === 429 ? 'A fila está cheia.' : 'Não foi possível enviar. Tente novamente.');
  await updateState();
}

function thumbnail(id, width = 640, height = 360) {
  return `/api/thumb/${encodeURIComponent(id)}?w=${width}&h=${height}`;
}

function openVideo(video) {
  selectedVideo = video;
  $('#dialog-title').textContent = video.title;
  $('#dialog-channel').textContent = video.channel || 'Vídeo do link';
  $('#dialog-thumb').src = thumbnail(video.id);
  $('#video-dialog').showModal();
}

function youtubeId(text) {
  if (/^[\w-]{11}$/.test(text)) return text;
  let url;
  try { url = new URL(/^https?:\/\//i.test(text) ? text : `https://${text}`); } catch { return null; }
  const host = url.hostname.replace(/^(www\.|m\.|music\.)/, '');
  if (!['youtu.be', 'youtube.com'].includes(host)) return null;
  const parts = url.pathname.split('/');
  const id = host === 'youtu.be' ? parts[1] : url.pathname === '/watch' ? url.searchParams.get('v') : ['shorts', 'embed', 'live'].includes(parts[1]) ? parts[2] : '';
  return /^[\w-]{11}$/.test(id || '') ? id : null;
}

function videoCard(video) {
  knownVideos.set(video.id, video);
  const button = document.createElement('button');
  button.className = 'video-card';
  button.type = 'button';
  button.setAttribute('aria-label', `Abrir ações: ${video.title}`);
  const imageBox = document.createElement('div');
  imageBox.className = 'video-image';
  const image = new Image(640, 360);
  image.src = thumbnail(video.id);
  image.alt = '';
  image.loading = 'lazy';
  imageBox.append(image);
  const badge = document.createElement('span');
  badge.className = 'duration-badge';
  badge.textContent = video.duration ? clock(video.duration) : 'Vídeo';
  imageBox.append(badge);
  const title = document.createElement('h3');
  title.textContent = video.title;
  const channel = document.createElement('p');
  channel.textContent = video.channel;
  const views = document.createElement('p');
  views.className = 'video-views';
  views.textContent = video.views;
  button.append(imageBox, title, channel, views);
  button.addEventListener('click', () => openVideo(video));
  return button;
}

async function loadFeed(query = '') {
  currentQuery = query;
  if (feedController) feedController.abort();
  const controller = new AbortController();
  feedController = controller;
  $('#feed-message').textContent = query ? 'Buscando vídeos…' : 'Buscando boas escolhas para a sua sessão…';
  $('#feed-message').hidden = false;
  $('#retry').hidden = true;
  $('#result-count').textContent = '';
  $('#results').replaceChildren();
  $('#results').setAttribute('aria-busy', 'true');
  $('#feed-title').textContent = query ? `Busca por “${query}”` : 'Para assistir hoje';
  try {
    const params = new URLSearchParams({ kind: query ? 'search' : 'home', n: '24' });
    if (query) params.set('q', query);
    const response = await fetch(`/api/feed?${params}`, { signal: controller.signal });
    if (!response.ok) throw new Error('O servidor não respondeu. Tente novamente.');
    const lines = (await response.text()).split('\n').filter(Boolean);
    const header = lines.shift().split('\t');
    if (header[0] !== 'ok') throw new Error(header[1] || 'Não foi possível carregar os vídeos.');
    const videos = lines.map(line => {
      const [id, duration, channel, views, title] = line.split('\t');
      return { id, duration: Number(duration), channel, views, title };
    });
    $('#results').replaceChildren(...videos.map(videoCard));
    $('#result-count').textContent = `${videos.length} vídeos`;
    $('#feed-message').hidden = videos.length > 0;
    if (!videos.length) $('#feed-message').textContent = 'Nenhum vídeo por aqui. Tente outra busca.';
  } catch (error) {
    if (error.name === 'AbortError') return;
    $('#feed-message').textContent = error.message === 'Failed to fetch' ? 'Sem conexão com o servidor. Confira o Wi-Fi e tente novamente.' : error.message;
    $('#retry').hidden = false;
  } finally {
    if (feedController === controller) $('#results').setAttribute('aria-busy', 'false');
  }
}

function renderState(state) {
  currentState = state;
  const active = Boolean(state.playing);
  $('#connection').textContent = state.connected ? 'Console conectado' : 'Aguardando console';
  $('#connection').classList.toggle('online', state.connected);
  $('#player-empty').hidden = active;
  $('#player-info').hidden = !active;
  if (active) {
    const info = knownVideos.get(state.playing);
    $('#now-title').textContent = info?.title || state.title || state.playing;
    if ($('#now-thumb').dataset.id !== state.playing) {
      $('#now-thumb').src = thumbnail(state.playing, 320, 180);
      $('#now-thumb').dataset.id = state.playing;
    }
    $('#play-status').textContent = !state.connected ? 'Aguardando conexão da TV' : state.paused ? 'Pausado na TV' : 'Reproduzindo na TV';
  }
  $('#position').textContent = clock(state.pos);
  $('#duration').textContent = clock(state.dur);
  $('#progress').max = state.dur || 1;
  $('#progress').value = Math.min(state.pos, state.dur || 1);
  document.querySelectorAll('.transport button').forEach(button => { button.disabled = !active || !state.connected; });
  $('#next').disabled = !state.queue.length || !state.connected;
  const toggle = $('#toggle-play');
  toggle.dataset.action = state.paused ? 'resume' : 'pause';
  toggle.setAttribute('aria-label', state.paused ? 'Continuar' : 'Pausar');
  toggle.querySelector('use').setAttribute('href', state.paused ? '#i-play' : '#i-pause');
  $('#queue-count').textContent = state.queue.length;
  $('#queue-empty').hidden = Boolean(state.queue.length);
  const signature = JSON.stringify(state.queue);
  if (signature !== renderedQueue) {
    renderedQueue = signature;
    $('#queue-list').replaceChildren(...state.queue.map((video, index) => {
      const item = document.createElement('li');
      item.className = 'queue-item';
      const number = document.createElement('span');
      number.textContent = index + 1;
      const image = new Image(68, 39);
      image.src = thumbnail(video.id, 320, 180);
      image.alt = '';
      image.loading = 'lazy';
      const title = document.createElement('p');
      title.textContent = knownVideos.get(video.id)?.title || video.title;
      item.append(number, image, title);
      return item;
    }));
  }
}

async function updateState() {
  try {
    const response = await fetch('/api/remote/state', { cache: 'no-store' });
    if (!response.ok) throw new Error('Estado indisponível');
    renderState(await response.json());
  } catch {
    $('#connection').textContent = 'Servidor desconectado';
    $('#connection').classList.remove('online');
    document.querySelectorAll('.transport button').forEach(button => { button.disabled = true; });
  }
}

$('#search-form').addEventListener('submit', event => {
  event.preventDefault();
  const text = $('#search').value.trim();
  const id = youtubeId(text);
  if (id) { openVideo(knownVideos.get(id) || { id, title: 'Pronto para tocar na sua TV', channel: 'Vídeo do link' }); return; }
  if (/^(https?:\/\/|(?:www\.|m\.)?(?:youtube\.com|youtu\.be)\/)/i.test(text)) {
    toast('Confira o link de um vídeo do YouTube.', true);
    return;
  }
  document.querySelectorAll('.topics button').forEach(button => button.classList.toggle('selected', button.dataset.query === text));
  loadFeed(text);
});
document.querySelectorAll('.topics button').forEach(button => button.addEventListener('click', () => {
  $('#search').value = button.dataset.query;
  document.querySelectorAll('.topics button').forEach(item => item.classList.toggle('selected', item === button));
  loadFeed(button.dataset.query);
}));
$('#retry').addEventListener('click', () => loadFeed(currentQuery));
$('#close-dialog').addEventListener('click', () => $('#video-dialog').close());
$('#video-dialog').addEventListener('click', event => {
  if (event.target !== $('#video-dialog')) return;
  const rect = event.target.getBoundingClientRect();
  if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) event.target.close();
});
for (const [selector, action] of [['#play-video', 'play'], ['#queue-video', 'queue']]) {
  $(selector).addEventListener('click', async () => {
    $(selector).disabled = true;
    try {
      await send({ action, id: selectedVideo.id });
      $('#video-dialog').close();
      toast(action === 'queue' ? 'Vídeo adicionado à fila.' : currentState?.connected ? 'Enviado para a TV.' : `Enviado. Abra o ${document.title.split(' · ')[0]} no Xbox.`);
    } catch (error) { toast(error.message, true); }
    finally { $(selector).disabled = false; }
  });
}
document.querySelectorAll('.transport button').forEach(button => button.addEventListener('click', async () => {
  const command = { action: button.dataset.action };
  if (button.dataset.delta) command.delta = Number(button.dataset.delta);
  try { await send(command); toast('Comando enviado para a TV.'); }
  catch (error) { toast(error.message, true); }
}));
document.addEventListener('visibilitychange', () => { if (!document.hidden) updateState(); });
// Uma consulta por vez, inclusive em conexão lenta.
async function watchState() {
  await updateState();
  setTimeout(watchState, document.hidden ? 3000 : 900);
}
watchState();
loadFeed();
