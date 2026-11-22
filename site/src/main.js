(() => {
  'use strict';
  const t = JSON.parse(document.getElementById('site-i18n').textContent);
  const locale = document.documentElement.lang;
  const $ = (selector, parent = document) => parent.querySelector(selector);
  const $$ = (selector, parent = document) => [...parent.querySelectorAll(selector)];
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const tv = $('#tv');
  const native = $('#app-native');
  const home = $('#home-screen');
  const player = $('#player-screen');
  const ratingPanel = $('#rating-panel');
  const nextPanel = $('#up-next');
  const progress = $('#player-progress');
  const progressKnob = $('#player-knob');
  const status = $('#demo-status');
  const toast = $('#app-toast');
  const state = { selected: 0, mode: 'home', zone: 'grid', category: 0, paused: false, elapsed: 0, nextCancelled: false, rating: 0, frame: null, lastTime: 0, visible: true, gamepadIndex: null, gamepadPrevious: {}, repeatAt: 0, direction: '' };
  // Data from the demo feed used to capture the current docs/img/home.jpg.
  const videos = [
  {
    "id": "-k_oFUujRSs",
    "title": "CORINTHIANS FANS' REACTIONS - PALMEIRAS 4-1 CORINTHIANS - BRASILEIRÃO 2026 - LET'S LAUGH!",
    "channel": "A Voz da Torcida",
    "views": "1.1M views",
    "x": 136,
    "y": 150
  },
  {
    "id": "glbLUBtrL8E",
    "title": "PALMEIRAS 4 X 1 CORINTHIANS | HIGHLIGHTS | ROUND 30 | 2026 BRASILEIRÃO | ge.globo",
    "channel": "ge tv",
    "views": "1.9M views",
    "x": 412,
    "y": 150
  },
  {
    "id": "CWFlRPmg9-8",
    "title": "FLAMENGO 0 X 3 PALMEIRAS | HIGHLIGHTS | 2026 BRASILEIRÃO MATCHDAY 17 | ge.globo",
    "channel": "ge tv",
    "views": "4.1M views",
    "x": 688,
    "y": 150
  },
  {
    "id": "tDxIr5s3pTQ",
    "title": "REACT PALMEIRAS 4-1 CORINTHIANS - 2026 BRASILEIRÃO (MATCHDAY 30) - 10/11/2026",
    "channel": "A Quarta Força",
    "views": "159.9K views",
    "x": 964,
    "y": 150
  },
  {
    "id": "4p_9OPKgcDE",
    "title": "PALMEIRAS 4 X 1 CORINTHIANS | HIGHLIGHTS | ROUND 30 | 2026 BRASILEIRÃO | ge tv",
    "channel": "ge tv",
    "views": "1.8M views",
    "x": 136,
    "y": 398
  },
  {
    "id": "O-6GDlg98x8",
    "title": "REACT FLAMENGO 0-3 PALMEIRAS !!! SILENCED THE MARACA !!!",
    "channel": "Hugão Corintiano",
    "views": "131.9K views",
    "x": 412,
    "y": 398
  },
  {
    "id": "sCmtNwJhHCc",
    "title": "REACT PALMEIRAS 4X1 CORINTHIANS - WHAT A ROUT! PALMEIRAS FINALLY SANK CORINTHIANS!",
    "channel": "VICTOR HUGO REACT",
    "views": "205K views",
    "x": 688,
    "y": 398
  },
  {
    "id": "me4LFY_epK0",
    "title": "TIMÃO FAZ GOL CONTRA BIZARRO, GIAY EM NOITE ARTILHEIRA E VERDÃO ATROPELA | PALMEIRAS 4X1 CORINTHIANS",
    "channel": "TNT Sports Brasil",
    "views": "83.4K views",
    "x": 964,
    "y": 398
  }
];
  const upcomingVideo = videos[2];
  function localizedViews(raw) {
    if (locale !== 'pt-BR') return raw;
    // Same thresholds and precision as server localized_views / views_text for pt.
    const [, number, unit] = raw.match(/^([\d.]+)([KMB]?) views$/);
    const count = Number(number) * ({ K: 1000, M: 1000000, B: 1000000000 }[unit] || 1);
    if (count >= 1000000) return (count / 1000000).toFixed(1).replace('.', ',') + ' mi de visualizações';
    if (count >= 1000) return (count / 1000).toFixed(1).replace('.', ',') + ' mil visualizações';
    return count + (count === 1 ? ' visualização' : ' visualizações');
  }
  const grid = $('#video-grid');
  videos.forEach((video, index) => {
    const button = document.createElement('button');
    button.className = `video-card${index === 0 ? ' selected' : ''}`;
    button.type = 'button';
    button.tabIndex = index === 0 ? 0 : -1;
    button.setAttribute('aria-label', `${t.preview_prefix} ${video.title}, ${video.channel}`);
    button.innerHTML = `<span class="video-thumb" style="background-position:-${video.x}px -${video.y}px" role="img" aria-label="${t.thumbnail} ${video.channel}"></span><span class="video-title"></span><span class="video-meta"></span>`;
    $('.video-title', button).textContent = video.title;
    $('.video-meta', button).textContent = `${video.channel}  ·  ${localizedViews(video.views)}`;
    button.addEventListener('click', () => { select(index); openPlayer(); });
    button.addEventListener('focus', () => { state.zone = 'grid'; select(index); });
    grid.append(button);
  });
  const cards = $$('.video-card');
  const categories = $$('.category');
  function fitCategories() {
    const rail = categories[0].parentElement;
    const selected = categories[state.category];
    let first = 0;
    while (categories[first].offsetLeft < selected.offsetLeft + selected.offsetWidth - rail.clientWidth) first++;
    rail.scrollLeft = categories[first].offsetLeft;
    categories.forEach((category, i) => {
      category.style.visibility = category.offsetLeft >= rail.scrollLeft - 1 && category.offsetLeft + category.offsetWidth <= rail.scrollLeft + rail.clientWidth + 1 ? 'visible' : 'hidden';
      category.tabIndex = i === state.category ? 0 : -1;
    });
    $('#category-more').hidden = categories.at(-1).style.visibility === 'visible';
    $('#category-previous').hidden = first === 0;
  }
  document.fonts.ready.then(fitCategories);

  function fitScreen() { native.style.setProperty('--app-scale', tv.clientWidth / 1280); }
  new ResizeObserver(fitScreen).observe(tv);
  fitScreen();

  function announce(text) { status.textContent = text; }
  let toastTimer;
  function showToast(text) {
    clearTimeout(toastTimer);
    toast.textContent = text;
    toast.hidden = false;
    announce(text);
    toastTimer = setTimeout(() => { toast.hidden = true; }, 2700);
  }
  function select(index) {
    state.selected = Math.max(0, Math.min(cards.length - 1, index));
    cards.forEach((card, i) => {
      card.classList.toggle('selected', i === state.selected);
      card.tabIndex = i === state.selected ? 0 : -1;
    });
  }
  function chooseCategory(index, focus = false) {
    state.category = (index + categories.length) % categories.length;
    state.zone = 'categories';
    categories.forEach((category, i) => {
      category.classList.toggle('active', i === state.category);
      category.setAttribute('aria-pressed', String(i === state.category));
    });
    fitCategories();
    if (focus) categories[state.category].focus({ preventScroll: true });
  }
  categories.forEach((category, index) => category.addEventListener('click', () => {
    chooseCategory(index);
    if (index !== 0) showToast(t.sample_videos);
  }));

  function animateIn(element, offset = 16) {
    if (!window.gsap || reducedMotion.matches) return;
    gsap.fromTo(element, { autoAlpha: 0, y: offset }, { autoAlpha: 1, y: 0, duration: .28, ease: 'power2.out', overwrite: 'auto' });
  }
  function openPlayer() {
    state.mode = 'player';
    state.elapsed = 0;
    lastProgressSecond = -1;
    state.paused = false;
    state.nextCancelled = false;
    state.lastTime = performance.now();
    $('#player-title').textContent = videos[state.selected].title;
    const upcoming = upcomingVideo;
    $('#up-next h3').textContent = upcoming.title;
    $('#next-channel').textContent = upcoming.channel;

    home.hidden = true;
    player.hidden = false;
    ratingPanel.hidden = true;
    nextPanel.hidden = true;
    toast.hidden = true;
    updatePause();
    updateProgress();
    animateIn(player, 0);
    tv.focus({ preventScroll: true });
    announce(t.player_open);
    startLoop();
  }
  function goHome() {
    state.mode = 'home';
    state.zone = 'grid';
    player.hidden = true;
    home.hidden = false;
    ratingPanel.hidden = true;
    nextPanel.hidden = true;
    animateIn(home, 0);
    cards[state.selected].focus({ preventScroll: true });
    announce(t.home_return);
    startLoop();
  }
  function updatePause() {
    player.classList.toggle('paused', state.paused);
    $('#player-toggle').setAttribute('aria-label', state.paused ? t.resume_preview : t.pause_preview);
    $('#pause-label').textContent = state.paused ? t.resume : t.pause;
  }
  let lastProgressSecond = -1;
  function updateProgress() {
    // Compress the preview's opening, then show the final eight seconds in real time.
    const position = Math.min(487, Math.floor(state.elapsed < 24
      ? 7 + state.elapsed / 24 * 472
      : 479 + state.elapsed - 24));
    const percentage = Math.max(1.5, Math.min(100, position / 487 * 100));
    $('#next-progress-fill').style.transform = `scaleX(${Math.max(0, Math.min(1, (state.elapsed - 24) / 8))})`;
    progress.style.transform = `scaleX(${percentage / 100})`;
    progressKnob.style.transform = `translateX(${1136 * percentage / 100 - 7.5}px)`;
    const second = Math.floor(state.elapsed);
    if (second !== lastProgressSecond) {
      $('.progress-track').setAttribute('aria-valuenow', String(Math.round(percentage)));
      $('#player-time').textContent = `${Math.floor(position / 60)}:${String(position % 60).padStart(2, '0')}`;
      $('#next-count').textContent = String(Math.max(1, Math.ceil(32 - state.elapsed)));
      $('.next-progress').setAttribute('aria-valuenow', String(Math.round(Math.max(0, Math.min(100, (state.elapsed - 24) / 8 * 100)))));
      lastProgressSecond = second;
    }
  }
  function updateRating() {
    $$('.rating-choice').forEach((choice, index) => choice.classList.toggle('selected', index === state.rating));
  }
  function rate(index) {
    state.rating = index;
    ratingPanel.hidden = true;
    state.mode = 'player';
    showToast(`${index === 0 ? t.liked : t.disliked}. ${t.preview_rating_note}`);
    startLoop();
  }
  function nextVideo() { select(videos.indexOf(upcomingVideo)); openPlayer(); announce(t.next_open); }
  function command(action) {
    if (!state.visible) return;
    if (action === 'x' && state.mode === 'home') { showToast(t.refreshed); return; }
    if (action === 'y') { showToast(t.search_note); return; }
    if (state.mode === 'rating') {
      if (action === 'left' || action === 'right') { state.rating = 1 - state.rating; updateRating(); }
      if (action === 'a') rate(state.rating);
      if (action === 'b' || action === 'up') { ratingPanel.hidden = true; state.mode = 'player'; startLoop(); }
      return;
    }
    if (state.mode === 'home') {
      if (action === 'a') {
        if (state.zone === 'categories') { if (state.category !== 0) showToast(t.sample_videos); state.zone = 'grid'; cards[state.selected].focus({ preventScroll: true }); }
        else openPlayer();
        return;
      }
      if (action === 'b') { chooseCategory(0); state.zone = 'grid'; cards[state.selected].focus({ preventScroll: true }); return; }
      if (state.zone === 'categories') {
        if (action === 'left') chooseCategory(state.category - 1, true);
        if (action === 'right') chooseCategory(state.category + 1, true);
        if (action === 'down') { state.zone = 'grid'; cards[state.selected].focus({ preventScroll: true }); }
      } else {
        if (action === 'left' && state.selected % 4 > 0) select(state.selected - 1);
        if (action === 'right' && state.selected % 4 < 3) select(state.selected + 1);
        if (action === 'down') select(Math.min(7, state.selected + 4));
        if (action === 'up') { if (state.selected < 4) chooseCategory(state.category, true); else select(state.selected - 4); }
        if (state.zone === 'grid') cards[state.selected].focus({ preventScroll: true });
      }
      return;
    }
    if (!nextPanel.hidden && (action === 'a' || action === 'b')) {
      if (action === 'a') nextVideo();
      else { state.nextCancelled = true; nextPanel.hidden = true; announce(t.autoplay_cancel); }
      return;
    }
    if (action === 'b') { goHome(); return; }
    if (action === 'a') { state.paused = !state.paused; updatePause(); announce(state.paused ? t.paused_note : t.resumed_note); startLoop(); }
    if (action === 'down') { state.mode = 'rating'; ratingPanel.hidden = false; updateRating(); animateIn(ratingPanel); announce(t.rating_help); }
    if (action === 'left' || action === 'right') { state.elapsed = Math.min(31.5, Math.max(0, state.elapsed + (action === 'left' ? -4 : 4))); updateProgress(); }
    if (action === 'up') showToast(t.sleep_note);
  }
  $$('.rating-choice').forEach((choice, index) => choice.addEventListener('click', () => rate(index)));
  $$('[data-command]').forEach(button => button.addEventListener('click', () => command(button.dataset.command)));
  $('#player-toggle').addEventListener('click', () => command('a'));
  const keys = { ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down', Enter: 'a', ' ': 'a', Escape: 'b', Backspace: 'b', a: 'a', b: 'b', x: 'x', y: 'y' };
  document.addEventListener('keydown', event => {
    const action = keys[event.key];
    if (!action || event.altKey || event.ctrlKey || event.metaKey) return;
    const target = event.target;
    if (target.closest('input, textarea, select, [contenteditable]')) return;
    const inDemo = target === document.body || target === tv || tv.contains(target) || !!target.closest('.touch-controls');
    if (!inDemo || !state.visible) return;
    event.preventDefault();
    command(action);
  });

  function pollGamepad(now) {
    if (state.gamepadIndex === null || !navigator.getGamepads) return;
    let pad;
    try { pad = navigator.getGamepads()[state.gamepadIndex]; } catch { return; }
    if (!pad) return;
    const pressed = index => !!pad.buttons[index]?.pressed;
    for (const [index, action] of [[0, 'a'], [1, 'b'], [2, 'x'], [3, 'y']]) {
      const down = pressed(index);
      if (down && !state.gamepadPrevious[index]) command(action);
      state.gamepadPrevious[index] = down;
    }
    const horizontal = pad.axes[0] || 0;
    const vertical = pad.axes[1] || 0;
    const direction = pressed(14) || horizontal < -.55 ? 'left' : pressed(15) || horizontal > .55 ? 'right' : pressed(12) || vertical < -.55 ? 'up' : pressed(13) || vertical > .55 ? 'down' : '';
    if (direction && (state.direction !== direction || now >= state.repeatAt)) {
      command(direction);
      state.repeatAt = now + (state.direction === direction ? 145 : 360);
    }
    state.direction = direction;
  }
  function loop(now) {
    if (document.hidden || !state.visible) { state.frame = null; state.lastTime = 0; return; }
    const delta = state.lastTime ? Math.min((now - state.lastTime) / 1000, .1) : 0;
    state.lastTime = now;
    pollGamepad(now);
    if (state.mode === 'player' && !state.paused) {
      state.elapsed += delta;
      updateProgress();
      if (state.elapsed >= 24 && !state.nextCancelled && nextPanel.hidden) {
        nextPanel.hidden = false;
        animateIn(nextPanel);
        announce(t.next_help);
      }
      if (state.elapsed >= 32) {
        if (state.nextCancelled) { state.paused = true; updatePause(); }
        else nextVideo();
      }
    }
    state.frame = null;
    if (state.frame === null && (state.gamepadIndex !== null || (state.mode === 'player' && !state.paused))) state.frame = requestAnimationFrame(loop);
  }
  function startLoop() {
    if (state.frame !== null || document.hidden || !state.visible) return;
    if (state.gamepadIndex === null && (state.mode !== 'player' || state.paused)) return;
    state.lastTime = 0;
    state.frame = requestAnimationFrame(loop);
  }
  function stopLoop() {
    if (state.frame !== null) cancelAnimationFrame(state.frame);
    state.frame = null;
    state.lastTime = 0;
  }
  function connectGamepad(pad) {
    if (pad.mapping !== 'standard') {
      $('#controller-status span').textContent = t.standard_controller;
      return;
    }
    state.gamepadIndex = pad.index;
    state.gamepadPrevious = {};
    $('#controller-status span').textContent = t.controller_connected;
    $('#controller-status').hidden = true;
    $('#connected-prompts').hidden = false;
    announce(t.controller_help);
    startLoop();
  }
  window.addEventListener('gamepadconnected', event => connectGamepad(event.gamepad));
  window.addEventListener('gamepaddisconnected', event => {
    if (event.gamepad.index !== state.gamepadIndex) return;
    state.gamepadIndex = null;
    state.gamepadPrevious = {};
    $('#controller-status').hidden = false;
    $('#controller-status span').textContent = t.connect_controller;
    $('#connected-prompts').hidden = true;
  });
  try {
    const existing = navigator.getGamepads && [...navigator.getGamepads()].find(pad => pad && pad.connected);
    if (existing) connectGamepad(existing);
  } catch { /* Keyboard and touch remain available when the API is restricted. */ }
  new IntersectionObserver(([entry]) => {
    state.visible = entry.isIntersecting;
    if (state.visible) startLoop(); else stopLoop();
  }, { threshold: .08 }).observe(tv);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stopLoop(); else startLoop(); });
  reducedMotion.addEventListener('change', () => { stopLoop(); startLoop(); });

  const featureData = {
    account: { image: 'signin', alt: t.account_alt, caption: t.account_caption },
    search: { image: 'search', alt: t.search_alt, caption: t.search_caption },
    cast: { image: 'cast', alt: t.cast_alt, caption: t.cast_caption },
    sleep: { image: 'sleep-timer', alt: t.sleep_alt, caption: t.sleep_caption }
  };
  const featureTabs = $$('.feature-option');
  function selectFeature(button) {
    const data = featureData[button.dataset.feature];
    featureTabs.forEach(tab => { const active = tab === button; tab.classList.toggle('active', active); tab.setAttribute('aria-selected', String(active)); tab.tabIndex = active ? 0 : -1; });
    $('#feature-image').src = `/assets/img/${data.image}.jpg`;
    $('#feature-image').alt = data.alt;
    $('#feature-caption').textContent = data.caption;
    $('#feature-panel').setAttribute('aria-labelledby', button.id);
    animateIn($('#feature-image'), 8);
    if (window.ScrollTrigger) ScrollTrigger.refresh();
  }
  featureTabs.forEach((button, index) => {
    button.addEventListener('click', () => selectFeature(button));
    button.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowDown' || event.key === 'ArrowRight') next = (index + 1) % featureTabs.length;
      if (event.key === 'ArrowUp' || event.key === 'ArrowLeft') next = (index + featureTabs.length - 1) % featureTabs.length;
      if (event.key === 'Home') next = 0;
      if (event.key === 'End') next = featureTabs.length - 1;
      if (next !== undefined) { event.preventDefault(); featureTabs[next].focus(); selectFeature(featureTabs[next]); }
    });
  });

  const sessionKey = 'tube360.github.v1';
  let memorySession = null;
  const safeReleaseURL = value => {
    try { const url = new URL(value); return url.protocol === 'https:' && url.hostname === 'github.com' && url.pathname.startsWith('/PedroHenrique0713/tube360/releases/download/') ? url.href : null; } catch { return null; }
  };
  function readSession() {
    try { return JSON.parse(sessionStorage.getItem(sessionKey)); } catch { return memorySession; }
  }
  function saveSession(value) {
    memorySession = value;
    try { sessionStorage.setItem(sessionKey, JSON.stringify(value)); } catch { /* Storage may be disabled. */ }
  }
  function applyGithub(data) {
    const release = data?.release;
    const asset = release?.assets?.[0];
    const url = asset && safeReleaseURL(asset.browser_download_url);
    if (url && typeof release.tag_name === 'string' && release.tag_name.trim() && Number.isInteger(asset.size) && asset.size > 0) {
      const version = release.tag_name.slice(0, 64);
      $$('[data-download]').forEach(link => { link.href = url; });
      $$('[data-release]').forEach(label => {
        label.textContent = `${version} · ${(asset.size / 1000000).toLocaleString(locale, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} MB ZIP`;
        label.dataset.version = version;
        label.dataset.size = asset.size;
      });
      Object.assign(document.body.dataset, { version, downloadUrl: url, size: String(asset.size) });
    }
    const count = data?.repo?.stargazers_count;
    if (Number.isInteger(count) && count >= 0) {
      $$('span[data-stars]').forEach(counter => {
        counter.textContent = count.toLocaleString(locale);
        counter.dataset.stars = count;
      });
      document.body.dataset.stars = count;
    }
  }
  async function githubData(path) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 4500);
    try {
      const response = await fetch(`https://api.github.com/repos/PedroHenrique0713/tube360${path}`, { signal: controller.signal, headers: { Accept: 'application/vnd.github+json' } });
      return response.ok ? await response.json() : null;
    } catch { return null; } finally { clearTimeout(timeout); }
  }
  async function refreshGithub() {
    const cached = readSession();
    if (cached?.attempted) { applyGithub(cached); return; }
    const retryAfter = Number(document.body.dataset.githubRetryAfter) || 0;
    if (Date.now() < retryAfter * 1000) return;
    saveSession({ attempted: true });
    const [release, repo] = await Promise.all([githubData('/releases/latest'), githubData('')]);
    const data = { attempted: true, release, repo };
    saveSession(data);
    applyGithub(data);
  }
  refreshGithub();

  function updateLanguageLinks() {
    $$('[data-language]').forEach(link => { link.hash = window.location.hash; });
  }
  updateLanguageLinks();
  window.addEventListener('hashchange', updateLanguageLinks);
  // Also preserve a section reached by scrolling without a hash.
  const sectionObserver = new IntersectionObserver(entries => {
    if (window.location.hash) return;
    for (const entry of entries) {
      if (!entry.isIntersecting) continue;
      const hash = entry.target.id === 'top' ? '' : `#${entry.target.id}`;
      $$('[data-language]').forEach(link => { link.hash = hash; });
    }
  }, { rootMargin: '-10% 0px -55% 0px', threshold: 0 });
  $$('section[id], footer[id]').forEach(section => sectionObserver.observe(section));
  const notice = $('#language-notice');
  let dismissed = false;
  try { dismissed = localStorage.getItem('tube360.language-dismissed') === '1'; } catch {}
  if (locale === 'en' && navigator.language.toLowerCase().startsWith('pt') && !dismissed) notice.hidden = false;
  $('#dismiss-language').addEventListener('click', () => {
    notice.hidden = true;
    try { localStorage.setItem('tube360.language-dismissed', '1'); } catch {}
  });
  $$('[data-copy]').forEach(button => button.addEventListener('click', async () => {
    const code = document.getElementById(button.dataset.copy);
    const value = code.textContent;
    let copied = false;
    try {
      if (navigator.clipboard?.writeText) { await navigator.clipboard.writeText(value); copied = true; }
    } catch {}
    if (!copied) {
      const input = document.createElement('textarea');
      input.value = value;
      input.className = 'clipboard-fallback';
      document.body.append(input);
      input.select();
      try { copied = document.execCommand('copy'); } catch {}
      input.remove();
      button.focus({ preventScroll: true });
    }
    button.textContent = copied ? t.copied : t.copy;
    $('#copy-status').textContent = copied ? t.copied : t.copy_error;
    setTimeout(() => { button.textContent = t.copy; }, 2000);
  }));

  if (window.gsap && window.ScrollTrigger) {
    gsap.registerPlugin(ScrollTrigger);
    const media = gsap.matchMedia();
    media.add('(prefers-reduced-motion: no-preference)', () => {
      const entrance = gsap.timeline({ defaults: { ease: 'power3.out' } });
      entrance.fromTo('.hero-intro', { autoAlpha: 0, y: 10 }, { autoAlpha: 1, y: 0, duration: .65 })
        .fromTo('.tv-frame', { autoAlpha: 0, y: 20, scale: .985 }, { autoAlpha: 1, y: 0, scale: 1, duration: .8 }, .12)
        .fromTo('.playground-caption', { autoAlpha: 0 }, { autoAlpha: 1, duration: .4 }, .55);
      gsap.fromTo('.signal-wire i', { x: 0 }, { x: () => $('.signal-wire').clientWidth - 14, duration: 1.2, ease: 'power1.inOut', scrollTrigger: { trigger: '.signal-flow', start: 'top 85%', once: true } });
    });
    media.add('(prefers-reduced-motion: reduce)', () => { gsap.set('[data-reveal], .hero-intro, .tv-frame, .playground-caption', { autoAlpha: 1, y: 0, scale: 1 }); });
    document.fonts.ready.then(() => ScrollTrigger.refresh());
    window.addEventListener('load', () => ScrollTrigger.refresh(), { once: true });
    window.addEventListener('beforeprint', () => { gsap.set('[data-reveal]', { autoAlpha: 1, y: 0 }); });
  }
  document.documentElement.dataset.tube360Ready = 'true';
})();
