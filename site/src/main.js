(() => {
  'use strict';
  const t = JSON.parse(document.getElementById('site-i18n').textContent);
  const locale = document.documentElement.lang;
  const n = t.native;
  const $ = (selector, parent = document) => parent.querySelector(selector);
  const $$ = (selector, parent = document) => [...parent.querySelectorAll(selector)];
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const tv = $('#tv');
  const native = $('#app-native');
  const home = $('#home-screen');
  const browse = $('#browse-screen');
  const player = $('#player-screen');
  const panel = $('#player-panel');
  const rail = $('#app-sidebar');
  const status = $('#demo-status');
  const toast = $('#app-toast');
  const progress = $('#player-progress');
  const categories = $$('.category');
  const railButtons = $$('.sidebar-icon');
  const state = { page: 'home', mode: 'home', zone: 'grid', selected: 0, category: 0,
    rail: 0, option: 0, paused: false, elapsed: 7, rating: '', speed: 1, repeat: false,
    captions: false, subscribed: false, visible: true, frame: null, lastTime: 0,
    gamepadIndex: null, gamepadPrevious: {}, repeatAt: 0, direction: '', panelIndex: 0, filters: { type: 0, duration: 0, date: 0, sort: 0 }, channelTab: 0, sponsorMode: 1, sponsorUndo: false, quality: '1080p30' };
  const videos = [
  {
    "id": "68WY6G_wjoY",
    "title": "Palmeiras 8 x 1 Ind. Petrolero - Melhores Momentos",
    "channel": "SBT Sports",
    "views": "2715561 views",
    "x": 136,
    "y": 150
  },
  {
    "id": "jzeFpEF6FQ8",
    "title": "BASTIDORES DA GOLEADA HISTÓRICA | PALMEIRAS 5 X 0 SÃO PAULO | BRASILEIRO 2023",
    "channel": "TV Palmeiras",
    "views": "335248 views",
    "x": 412,
    "y": 150
  },
  {
    "id": "6ElN1CStOUE",
    "title": "BASTIDORES CIMED | PALMEIRAS 6 X 1 REMO | BRASILEIRO 2026",
    "channel": "TV Palmeiras",
    "views": "81051 views",
    "x": 688,
    "y": 150
  },
  {
    "id": "5sVEcIZQlL0",
    "title": "BASTIDORES - PALMEIRAS 6 X 2 CSA - BRASILEIRO 2019",
    "channel": "TV Palmeiras",
    "views": "93990 views",
    "x": 964,
    "y": 150
  },
  {
    "id": "JQ_uHzugp8c",
    "title": "BASTIDORES | IND. PETROLERO 0 X 5 PALMEIRAS | CONMEBOL LIBERTADORES 2022",
    "channel": "TV Palmeiras",
    "views": "68616 views",
    "x": 136,
    "y": 398
  },
  {
    "id": "fvtmO_yaXHQ",
    "title": "NOITE MÁGICA DE VIRADA HISTÓRICA | PALMEIRAS 4 X 0 LDU | CONMEBOL LIBERTADORES 2025",
    "channel": "TV Palmeiras",
    "views": "756995 views",
    "x": 412,
    "y": 398
  },
  {
    "id": "E5Yn6JdFC9A",
    "title": "PALMEIRAS 5 X 0 SÃO PAULO | MELHORES MOMENTOS | 29ª RODADA DO BRASILEIRÃO 2023 | ge.globo",
    "channel": "ge tv",
    "views": "2855410 views",
    "x": 688,
    "y": 398
  },
  {
    "id": "g-ZhIznI0eg",
    "title": "PALMEIRAS 5 X 1 VITÓRIA | MELHORES MOMENTOS | 2ª RODADA BRASILEIRÃO 2026 | ge.globo",
    "channel": "ge tv",
    "views": "941661 views",
    "x": 964,
    "y": 398
  }
];
  const demoCatalog = {"channel":[{"id":"fpQcDQJQLhg","title":"Google Antigravity em ação: Criando um guia de campo offline sobre vida selvagem.","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"WN-JK-hMAyg","title":"Google Antigravity e Gemini 4: Criando um mundo 3D interativo mapeado por MIDI","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"f2bpv9mFUeE","title":"Google Antigravity e Gemini 4: Construindo uma groovebox personalizada em um macro pad de US$ 40","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"aRIFzzr_Mi0","title":"EmbeddingGemma 2: Como funcionam os embeddings nativamente multimodais","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"vLAegkGGWrM","title":"Criando com Gemini no Google Antigravity","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"rsOiZE19RVo","title":"Acelere o transporte para ganhar escala com o Google Antigravity","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"ID6vEy94j7c","title":"Criando visão em tempo real e navegação de borda com Gemini Live","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"pQ_Dnr9Q4k8","title":"Dispositivos inteligentes funcionais usando Gemini 4 Argon e Google Antigravity","channel":"","art":"/assets/demo/catalog-art.png"}],"shorts":[{"id":"cv8tEbKtEvk","title":"Construindo um rover autônomo para plantas com o Gemini Live","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"CsF5oqVV5YE","title":"Design e replicação de voz com o Gemini 3.8 Flash TTS","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"LJ2v4-FvPcA","title":"Desenvolvedores criando com Gemini Audio","channel":"","art":"/assets/demo/catalog-art.png"},{"id":"-nmcYcO-EE8","title":"Transformando movimento em música com o Gemini","channel":"","art":"/assets/demo/catalog-art.png"}],"music":[{"id":"F49audio000","title":"Noite de jazz","channel":"Trio Horizonte","art":"/assets/demo/music-0.png","section":"Para ouvir agora"},{"id":"F49audio001","title":"Piano ao entardecer","channel":"Marina Alves","art":"/assets/demo/music-1.png","section":"Para ouvir agora"},{"id":"F49audio002","title":"Batidas leves","channel":"Estúdio Norte","art":"/assets/demo/music-2.png","section":"Para ouvir agora"},{"id":"F49audio003","title":"Acústico em casa","channel":"Duo da Serra","art":"/assets/demo/music-3.png","section":"Para ouvir agora"},{"id":"F49audio004","title":"Bossa na varanda","channel":"Trio Horizonte","art":"/assets/demo/music-4.png","section":"Jazz e instrumentais"},{"id":"F49audio005","title":"Clássicos ao piano","channel":"Marina Alves","art":"/assets/demo/music-5.png","section":"Jazz e instrumentais"},{"id":"F49audio006","title":"Jazz instrumental","channel":"Estúdio Norte","art":"/assets/demo/music-0.png","section":"Jazz e instrumentais"},{"id":"F49audio007","title":"Sessão acústica","channel":"Duo da Serra","art":"/assets/demo/music-1.png","section":"Jazz e instrumentais"},{"id":"F49audio008","title":"Sons da noite","channel":"Trio Horizonte","art":"/assets/demo/music-2.png","section":"Mais música para você"},{"id":"F49audio009","title":"Voz e violão","channel":"Marina Alves","art":"/assets/demo/music-3.png","section":"Mais música para você"},{"id":"F49audio010","title":"Café e música","channel":"Estúdio Norte","art":"/assets/demo/music-4.png","section":"Mais música para você"},{"id":"F49audio011","title":"Piano para ler","channel":"Duo da Serra","art":"/assets/demo/music-5.png","section":"Mais música para você"}],"subscriptions":[{"id":"video000001","title":"Viagem de trem pela serra","channel":"Canal de demonstração","art":"/assets/demo/catalog-art.png"},{"id":"video000002","title":"Como funciona um motor elétrico","channel":"Canal de demonstração","art":"/assets/demo/catalog-art.png"},{"id":"video000003","title":"Café, música e uma tarde tranquila","channel":"Canal de demonstração","art":"/assets/demo/catalog-art.png"},{"id":"video000004","title":"Explorando as estradas do Brasil","channel":"Canal de demonstração","art":"/assets/demo/catalog-art.png"}]};
  const icon = name => `<svg class="icon" aria-hidden="true"><use href="#i-${name}"/></svg>`;
  const options = [
    ['captions', 'cc'], ['quality', 'settings'], ['sound', 'sound'], ['timer', 'moon'],
    ['like', 'like'], ['dislike', 'like'], ['comments', 'comments'], ['speed', 'speed'],
    ['repeat', 'repeat'], ['channel', 'person']
  ];
  const ratings = new Map();
  let catalogSelection = null, musicCollection = null;
  const saved = new Set([0]);
  const positions = new Map([[1, 84]]);
  const dialog = $('#demo-dialog');
  const segments = [[5, 24], [185, 219], [394, 414]];
  const sponsorIgnored = new Set();
  let dialogBack = null, dialogMode = 'home', filterDraft = null;
  function closeDialog() {
    dialog.hidden = true; state.mode = dialogMode; filterDraft = null;
    focus(dialogBack); marker(); startLoop();
  }
  function openDialog(title, note, choices) {
    dialog.dataset.kind = ''; dialogBack = document.activeElement; dialogMode = state.mode; state.mode = 'dialog';
    $('#dialog-title').textContent = title; $('#dialog-note').textContent = note;
    $('#dialog-actions').replaceChildren(...choices.map(([label, action]) => makeButton(label, action, 'panel-choice')));
    dialog.hidden = false; fitNativeType(); focus($('#dialog-actions button')); animateIn($('.dialog-surface')); marker();
  }
  function playCard(index) {
    catalogSelection = null; state.selected = index;
    if (positions.has(index)) {
      const position = positions.get(index);
      const time = `${Math.floor(position / 60)}:${String(Math.floor(position % 60)).padStart(2, '0')}`;
      openDialog(t.resume_question, `${videos[index].title} · ${time} / 8:07`, [
        [t.continue_watching, () => { closeDialog(); openPlayer(position); }],
        [t.start_over, () => { positions.delete(index); closeDialog(); openPlayer(0); }]
      ]);
    } else openPlayer();
  }
  function cardMenu() {
    openDialog(t.save_options, (catalogSelection || videos[state.selected]).title, [
      [t.channel, () => { closeDialog(); showPage('channel'); }],
      [saved.has(state.selected) ? t.remove_video : t.save_video, () => {
        const removing = saved.has(state.selected);
        if (removing) saved.delete(state.selected); else saved.add(state.selected);
        closeDialog(); if (state.page === 'later') showPage('later');
        showToast(removing ? t.removed_local : t.saved_local);
      }], [t.back, closeDialog]
    ]);
  }
  const filterValues = {
    type: ['filter_any', 'filter_video', 'filter_channel', 'filter_playlist'],
    duration: ['filter_any', 'filter_short', 'filter_medium', 'filter_long'],
    date: ['filter_any', 'filter_today', 'filter_week', 'filter_month', 'filter_year'],
    sort: ['filter_relevance', 'filter_views']
  };
  const filterLabels = { type: 'filter_type', duration: 'filter_duration', date: 'filter_date', sort: 'filter_sort' };
  function openFilters() {
    if (state.page !== 'search') return;
    openDialog(n.SearchFilters, '', []); dialog.dataset.kind = 'filters'; filterDraft = { ...state.filters };
    const content = $('#dialog-actions');
    for (const key of Object.keys(filterValues)) {
      const button = makeButton('', () => cycleFilter(button, 1), 'settings-row'); button.dataset.filter = key; content.append(button);
    }
    content.append(makeButton(t.apply, () => {
      state.filters = { ...filterDraft }; closeDialog(); $('#search-filters').textContent = `LB ${t.filters} · ${t.filter_active}`;
      if ($('.demo-results').childElementCount) $('.demo-search').requestSubmit();
    }, 'panel-choice'));
    content.append(makeButton(t.clear_filters, () => { filterDraft = { type: 0, duration: 0, date: 0, sort: 0 }; updateFilterLabels(); }, 'panel-choice'));
    const hints = document.createElement('p'); hints.className = 'filter-native-hints'; hints.innerHTML = `<span><b class="pad-button pad-direction">↑↓</b> ${n.ChooseRow}</span><span><b class="pad-button pad-direction">◀▶</b> ${n.ChangeValue}</span><span><b class="pad-button pad-b">B</b> ${n.Back}</span>`; content.append(hints);
    updateFilterLabels(); fitNativeType(); focus(content.firstElementChild);
  }
  function updateFilterLabels() {
    $$('[data-filter]', dialog).forEach(button => { const key = button.dataset.filter; const label = document.createElement('span'), value = document.createElement('strong'); label.textContent = n[['SearchType','SearchDuration','SearchDate','SearchOrder'][Object.keys(filterValues).indexOf(key)]]; value.textContent = key === 'type' && !filterDraft[key] ? n.AllResults : t[filterValues[key][filterDraft[key]]]; button.replaceChildren(label, value); });
  }
  function cycleFilter(button, direction) {
    const key = button.dataset.filter; if (!filterDraft || !key) return;
    filterDraft[key] = (filterDraft[key] + direction + filterValues[key].length) % filterValues[key].length; updateFilterLabels(); fitNativeType();
  }
  function updateSponsor() {
    const button = $('#sponsor-skip');
    const segment = segments.find(([start, end]) => state.elapsed >= start && state.elapsed < end);
    if (segment && state.sponsorMode === 2 && !sponsorIgnored.has(segment[0])) {
      state.sponsorReturn = state.elapsed; state.sponsorSegment = segment[0]; state.elapsed = segment[1]; state.sponsorUndo = true; state.sponsorUndoUntil = Date.now() + 6000;
    }
    const undo = state.sponsorUndo && Date.now() < state.sponsorUndoUntil;
    button.hidden = state.sponsorMode === 0 || (!undo && (!segment || state.sponsorMode !== 1));
    $('#sponsor-skip span').textContent = undo ? `${t.sponsor_skipped} · ${t.sponsor_undo}` : t.sponsor_skip;
    $('.sponsor-marks').hidden = state.sponsorMode === 0;
  }
  function skipSponsor() {
    if (state.sponsorUndo && Date.now() < state.sponsorUndoUntil) {
      sponsorIgnored.add(state.sponsorSegment); state.elapsed = state.sponsorReturn; state.sponsorUndoUntil = 0;
    } else {
      const segment = segments.find(([start, end]) => state.elapsed >= start && state.elapsed < end);
      if (!segment || state.sponsorMode !== 1) { activatePlayer('quality'); return; }
      state.elapsed = segment[1];
    }
    updateProgress();
  }
  $('#sponsor-skip').addEventListener('click', skipSponsor);
  $('#player-quality-hint').addEventListener('click', () => activatePlayer('quality'));
  const focus = element => {
    element?.focus({ preventScroll: true });
    if (element?.closest('#browse-content')) { keepPageFocusVisible(element); appHints(state.page); }
  };
  function keepPageFocusVisible(element) {
    const content = $('#browse-content');
    if (state.page === 'settings') {
      const rows = $$('.settings-row', content), index = rows.indexOf(element);
      if (index < 0) return;
      const narrow = matchMedia('(max-width: 700px)').matches, visible = narrow ? 5 : 8, step = narrow ? 94 : 59;
      const scale = tv.clientWidth / (narrow ? 960 : 1280);
      const rect = element.getBoundingClientRect(), viewport = content.getBoundingClientRect();
      if (rect.top < viewport.top + scale || rect.bottom > viewport.bottom - scale) content.scrollTop = Math.max(0, Math.min(rows.length - visible, index - visible + 1)) * step;
      state.settingsIndex = index;
      appHints('settings');
      $('#settings-help').textContent = index >= 16 ? n.LocalLimit : index === 2 ? n.ManualQualityHelp : index === 9 ? n.Display1080Help : index === 11 || index === 1 ? n.LargeTextHelp : index === 4 ? n.SyncHelp : index === 3 ? n.NoSpeaker : n.SavedAutomatically;
    } else {
      const scale = tv.clientWidth / (matchMedia('(max-width: 700px)').matches ? 960 : 1280);
      const grid = element.closest('.browse-grid');
      if (grid && grid.scrollHeight > grid.clientHeight + 1) { const index = $$('.video-card', grid).indexOf(element), columns = matchMedia('(max-width: 700px)').matches ? 2 : 4; if (index >= 0) grid.scrollTop = Math.floor(index / columns) * 242; }
      const rect = element.getBoundingClientRect(), viewport = content.getBoundingClientRect();
      if (rect.top < viewport.top) content.scrollTop -= (viewport.top - rect.top) / scale;
      else if (rect.bottom > viewport.bottom) content.scrollTop += (rect.bottom - viewport.bottom) / scale;
    }
  }
  function announce(text) { status.textContent = text; }
  function animateIn(element, offset = 16) {
    if (!window.gsap || reducedMotion.matches) return;
    window.gsap.fromTo(element, { autoAlpha: 0, y: offset }, { autoAlpha: 1, y: 0, duration: .28, ease: 'power2.out', overwrite: 'auto' });
  }
  function marker() { tv.dataset.state = state.mode === 'home' ? state.page : state.mode; tv.dataset.zone = state.zone; }
  let toastTimer;
  function showToast(text) {
    clearTimeout(toastTimer); toast.hidden = true; announce(text);
  }
  function localizedViews(raw) {
    const count = Number.parseInt(raw, 10);
    return new Intl.NumberFormat(locale, { notation: 'compact', maximumFractionDigits: 1 }).format(count) + (locale === 'pt-BR' ? ' visualizações' : ' views');
  }
  function makeCard(video, index, fromHome = false) {
    const button = document.createElement('button');
    button.className = 'video-card'; button.type = 'button';
    button.setAttribute('aria-label', `${t.preview_prefix} ${video.title}, ${video.channel}`);
    button.innerHTML = '<span class="video-thumb"></span><span class="video-title"></span><span class="video-meta"></span>';
    const thumb = $('.video-thumb', button);
    thumb.style.setProperty('--thumb-x', `-${video.x}px`);
    thumb.style.setProperty('--thumb-y', `-${video.y}px`);
    if (index === 0) thumb.classList.add('first-thumb');
    if (fromHome) thumb.insertAdjacentHTML('beforeend', '<span class="video-duration">8:07</span>');
    if (positions.has(index) && !fromHome) thumb.insertAdjacentHTML('beforeend', '<span class="saved-progress"></span>');
    $('.video-title', button).textContent = video.title;
    $('.video-meta', button).textContent = video.channel;
    button.addEventListener('click', () => playCard(index));
    button.addEventListener('focus', () => {
      catalogSelection = null; state.selected = index; if (fromHome) { state.zone = 'grid'; select(index); marker(); }
    });
    return button;
  }
  videos.forEach((video, i) => $('#video-grid').append(makeCard(video, i, true)));
  const cards = $$('.video-card', home);
  function select(index, moveFocus = false) {
    state.selected = Math.max(0, Math.min(videos.length - 1, index));
    cards.forEach((card, i) => {
      card.classList.toggle('selected', i === state.selected && state.zone === 'grid');
      card.tabIndex = i === state.selected ? 0 : -1;
    });
    if (moveFocus) {
      const card = cards[state.selected], grid = $('#video-grid');
      if (card.offsetTop < grid.scrollTop) grid.scrollTop = card.offsetTop;
      else if (card.offsetTop + card.offsetHeight > grid.scrollTop + grid.clientHeight) grid.scrollTop = card.offsetTop + card.offsetHeight - grid.clientHeight;
      focus(card);
    }
  }
  function fitCategories() {
    const strip = $('.app-categories');
    categories.forEach(button => { const start = button.offsetLeft - strip.scrollLeft; button.style.visibility = start >= -1 && start + button.offsetWidth <= strip.clientWidth + 1 ? 'visible' : 'hidden'; });
  }
  function fitNativeType() {
    const elements = $$('*', native).filter(element => element instanceof HTMLElement);
    elements.forEach(element => { element.style.removeProperty('font-size'); element.style.removeProperty('line-height'); });
    const measurements = elements.map(element => { const style = getComputedStyle(element); return [element, parseFloat(style.fontSize), parseFloat(style.lineHeight)]; });
    // O atlas usa a altura ascendente–descendente da Inter; CSS dimensiona pelo em.
    const scale = 2048 / 2478;
    measurements.forEach(([element, size, line]) => { if (Number.isFinite(size)) element.style.fontSize = `${size * scale}px`; if (Number.isFinite(line)) element.style.lineHeight = `${line * scale}px`; });
    fitCategories();
  }
  function fitScreen() {
    native.style.setProperty('--app-scale', tv.clientWidth / (matchMedia('(max-width: 700px)').matches ? 960 : 1280));
    fitNativeType();
  }
  new ResizeObserver(fitScreen).observe(tv); fitScreen();
  function chooseCategory(index, moveFocus = true) {
    state.category = Math.max(0, Math.min(categories.length - 1, index)); state.zone = 'categories';
    categories.forEach((button, i) => { button.classList.toggle('active', i === state.category); button.setAttribute('aria-pressed', String(i === state.category)); button.tabIndex = i === state.category ? 0 : -1; });
    const button = categories[state.category], parent = button.parentElement;
    if (button.offsetLeft < parent.scrollLeft) parent.scrollLeft = button.offsetLeft;
    if (button.offsetLeft + button.offsetWidth > parent.scrollLeft + parent.clientWidth) parent.scrollLeft = button.offsetLeft + button.offsetWidth - parent.clientWidth;
    fitCategories(); if (moveFocus) focus(button); select(state.selected); marker();
  }
  categories.forEach((button, i) => {
    button.addEventListener('click', () => { chooseCategory(i); if (i) showToast(t.sample_videos); });
    button.addEventListener('focus', () => { state.zone = 'categories'; marker(); });
  });
  function setRail(open, index = state.rail) {
    state.rail = Math.max(0, Math.min(railButtons.length - 1, index));
    rail.classList.toggle('open', open); railButtons[0].setAttribute('aria-expanded', String(open));
    railButtons.forEach((button, i) => { button.classList.toggle('selected', open && i === state.rail); });
    if (open) { state.zone = 'rail'; focus(railButtons[state.rail]); }
    else state.zone = state.page === 'home' ? 'grid' : 'page';
    select(state.selected); marker();
  }
  railButtons.forEach((button, i) => button.addEventListener('focus', () => { if (rail.classList.contains('open')) { state.rail = i; state.zone = 'rail'; railButtons.forEach((b,j) => b.classList.toggle('selected',j===i)); marker(); } }));
  function pageButtons() { return $$('button, input', $('#browse-content')); }
  function makeButton(label, action, className = 'browse-button') {
    const button = document.createElement('button'); button.type = 'button'; button.className = className;
    button.textContent = label; button.addEventListener('click', action); return button;
  }
  function pageGrid(indices) {
    const grid = document.createElement('div'); grid.className = 'browse-grid';
    indices.forEach(index => grid.append(makeCard(videos[index], index))); return grid;
  }
  function appHints(page) {
    const hint = (button, label, color = '') => `<span><b class="pad-button ${color || (button === '↑↓' || button === '◀▶' ? 'pad-direction' : '')}">${button}</b> ${label}</span>`;
    const bottom = $('#app-hints');
    if (page === 'home' || page === 'phone') { bottom.hidden = true; return; }
    bottom.hidden = false;
    bottom.innerHTML = page === 'settings'
      ? hint('↑↓', n.ChooseRow) + hint('◀▶', n.ChangeValue) + (state.settingsIndex === 0 || state.settingsIndex >= 13 ? hint('A', n.Open, 'pad-a') : '') + hint('B', n.Back, 'pad-b')
      : page === 'search' ? hint('A', n.Type, 'pad-a') + hint('X', n.Delete, 'pad-x') + hint('☰', n.Search) + hint('B', n.Back, 'pad-b')
      : hint('A', n.Open, 'pad-a') + hint('B', n.Back, 'pad-b') + hint('X', n.Refresh, 'pad-x') + hint('Y', n.Search, 'pad-y') + (document.activeElement?.closest('.video-card') ? hint('☰', n.MoreOptions) : '');
  }
  function musicShortcutIcon(symbol) {
    const shapes = symbol === 'clock'
      ? '<circle cx="55" cy="55" r="28.6" fill="#ff5a52"/><circle cx="55" cy="55" r="24.2" fill="#33212f"/><rect x="52.8" y="38.5" width="4.4" height="18.7" rx="2" fill="#f5f5f7"/><rect x="55" y="52.8" width="15.4" height="4.4" rx="2" fill="#f5f5f7"/>'
      : symbol === 'list' ? [0,1,2].map(row => `<rect x="26.4" y="${30.8 + row * 20.9}" width="8.8" height="8.8" rx="3" fill="#ff5a52"/><rect x="44" y="${33 + row * 20.9}" width="37.4" height="4.4" rx="2" fill="#f5f5f7"/>`).join('')
      : '<path d="M55 79 30 55C9 30 40 21 55 37 70 21 101 30 80 55Z" fill="#ff5a52"/>';
    const background = symbol === 'clock' ? '#33212f' : symbol === 'list' ? '#25253e' : '#3d2027';
    return `<svg class="icon" viewBox="0 0 110 110" aria-hidden="true"><rect width="110" height="110" rx="8" fill="${background}"/>${shapes}</svg>`;
  }
  function catalogGrid(items, className = '') {
    const grid = document.createElement('div'); grid.className = 'browse-grid ' + className;
    items.forEach((item, i) => {
      const card = makeButton('', () => { catalogSelection = item; if (state.page.startsWith('music')) { musicCollection = item; showPage('music-collection'); } else openPlayer(); }, 'catalog-card video-card');
      card.addEventListener('focus', () => { catalogSelection = item; });
      const image = document.createElement('img'); image.src = item.art; image.alt = ''; image.className = 'catalog-thumb';
      const title = document.createElement('span'); title.className = 'catalog-title'; title.textContent = item.title;
      const byline = document.createElement('span'); byline.className = 'catalog-byline'; byline.textContent = item.channel;
      card.append(image, title, byline); grid.append(card);
    }); return grid;
  }
  function showPage(page, moveFocus = true) {
    if (page === 'exit') { page = 'home'; }
    dialog.hidden = true; state.page = page; state.mode = 'home'; state.panelIndex = 0; panel.hidden = true;
    home.hidden = page !== 'home'; browse.hidden = page === 'home'; player.hidden = true;
    rail.hidden = false; $('#account-photo').hidden = false; $('#app-hints').hidden = false;
    toast.hidden = true; setRail(false); stopLoop(); appHints(page);
    native.dataset.page = page;
    railButtons.forEach(button => button.classList.toggle('active', button.dataset.page === (page === 'channel' ? 'home' : page)));
    const activeIndex = railButtons.findIndex(button => button.dataset.page === page);
    if (activeIndex >= 0) state.rail = activeIndex;
    if (page === 'home') { appHints('home'); state.zone = 'grid'; select(state.selected, moveFocus); animateIn(home, 0); marker(); startLoop(); return; }
    browse.dataset.page = page;
    browse.scrollTop = 0; $('#browse-content').scrollTop = 0;
    $$('.settings-intro, .settings-help, .music-tabs, .music-account-label, .music-collection-sub', browse).forEach(element => element.remove());
    state.zone = 'page'; $('#browse-title').textContent = page === 'channel' ? 'Google for Developers' : page === 'account' ? t.demo_account : page === 'later' ? t.watch_later_console : page === 'continue' ? t.continue_console : t[page];
    const content = $('#browse-content'); content.replaceChildren();
    if (page === 'subscriptions') {
      const avatars = document.createElement('div'); avatars.className = 'channel-avatars';
      const button = makeButton('', () => showPage('channel'), 'channel-avatar');
      button.innerHTML = '<span class="channel-initial"></span><span>Canal F11</span>'; avatars.append(button);
      content.append(avatars, catalogGrid(demoCatalog.subscriptions));
    } else if (page === 'channel') {
      const heading = document.createElement('div'); heading.className = 'channel-heading';
      heading.innerHTML = '<img class="channel-art" src="/assets/demo/catalog-art.png" alt=""><p>@GoogleDevelopers · 2,69 mi de inscritos · 6,1 mil vídeos</p>';
      heading.append(makeButton(state.subscribed ? n.Subscribed || t.subscribed : n.Subscribe || t.subscribe, () => { state.subscribed = !state.subscribed; showPage('channel'); }));
      const tabs = document.createElement('div'); tabs.className = 'browse-tabs';
      [n.Videos, n.ChannelShorts, n.ChannelLive, n.Playlists].forEach((label, i) => {
        const tab = makeButton(label, () => { state.channelTab = i; showPage('channel'); }, 'category');
        tab.classList.toggle('active', state.channelTab === i); tab.setAttribute('aria-pressed', String(state.channelTab === i)); tabs.append(tab);
      });
      const items = state.channelTab === 1 ? demoCatalog.shorts : demoCatalog.channel;
      content.append(heading, tabs, catalogGrid(items, state.channelTab === 1 ? 'shorts-grid' : 'channel-grid'));
    } else if (page === 'library') {
      const grid = document.createElement('div'); grid.className = 'library-grid';
      [[n.LocalLater, 'later', 0], [n.LocalHistory, 'continue', 1], [n.History, 'account-history'], [n.WatchLater, 'account-later'], [n.Mixes, 'account-mixes'], ['Viagens e descobertas', 'account-playlist'], ['Meu mix de viagens', 'account-mixes']].forEach(([label, destination, image]) => {
        const card = makeButton('', () => showPage(destination), 'library-card');
        if (image !== undefined) { const thumb = $('.video-thumb', cards[image]).cloneNode(true); thumb.classList.remove('first-thumb'); thumb.replaceChildren(); card.append(thumb); }
        const title = document.createElement('span'); title.textContent = label; card.append(title); grid.append(card);
      }); content.append(grid);
    } else if (page.startsWith('account-')) {
      $('#browse-title').textContent = page === 'account-history' ? n.History : page === 'account-later' ? n.WatchLater : page === 'account-mixes' ? n.Mixes : 'Viagens e descobertas';
      content.append(catalogGrid(demoCatalog.subscriptions));
    } else if (page === 'later' || page === 'continue') {
      const note = document.createElement('p'); note.className = 'browse-note'; note.textContent = n.LocalOnly;
      content.append(note, pageGrid([...(page === 'later' ? saved : positions.keys())]));
    } else if (page === 'search') {
      const form = document.createElement('form'); form.className = 'demo-search';
      const input = document.createElement('input'); input.type = 'search'; input.placeholder = n.SearchPrompt; input.setAttribute('aria-label', t.search_placeholder);
      form.insertAdjacentHTML('afterbegin', '<svg class="search-field-icon icon" aria-hidden="true"><use href="#i-native-search"/></svg>');
      const submit = document.createElement('button'); submit.type = 'submit'; submit.textContent = t.search_submit;
      const filters = makeButton('', openFilters); filters.id = 'search-filters';
      filters.innerHTML = `<b class="pad-button">LB</b> ${n.SearchFilters}`;
      const workspace = document.createElement('div'); workspace.className = 'search-workspace';
      const keyboard = document.createElement('div'); keyboard.className = 'demo-keyboard';
      const suggestions = document.createElement('div'); suggestions.className = 'search-suggestions';
      function updateSuggestions() {
        suggestions.replaceChildren(); const heading = document.createElement('h3'); heading.textContent = input.value ? n.SearchSuggestions : n.RecentSearches; suggestions.append(heading);
        const query = input.value.toLocaleLowerCase(locale).trim();
        const entries = query.startsWith('astro') ? ['astronomia', 'astronomia musica', 'astronomia tony igy', 'astronomia dance', 'astronomia remix', 'astronomia para crianças', 'astronomia coffin dance', 'astronomia song'] : query ? ['Palmeiras', 'Palmeiras melhores momentos', 'SBT Sports', 'ge tv'] : ['palmeiras 4x1 corinthians', 'reações dos corintianos', 'flamengo 0x3 palmeiras'];
        entries.filter(value => !query || value.toLocaleLowerCase(locale).includes(query)).forEach(value => {
          suggestions.append(makeButton(value, () => { input.value = value; form.requestSubmit(); }, 'suggestion'));
        });
        if (suggestions.isConnected) fitNativeType();
      }
      input.addEventListener('input', () => { workspace.hidden = false; updateSuggestions(); });
      [..."abcdefghijklmnopqrstuvwxyzç-'.1234567890"].forEach(letter => keyboard.append(makeButton(letter, () => {
        input.value = letter === '⌫' ? input.value.slice(0,-1) : input.value + (letter === '␣' ? ' ' : letter.toLowerCase()); workspace.hidden = false; updateSuggestions();
      }, 'keyboard-key')));
      const results = document.createElement('div'); results.className = 'demo-results';
      form.append(input, submit); form.addEventListener('submit', event => {
        event.preventDefault(); workspace.hidden = true; const query = input.value.toLocaleLowerCase(locale).trim();
        let indices = videos.map((video, i) => video.title.toLocaleLowerCase(locale).includes(query) || video.channel.toLocaleLowerCase(locale).includes(query) ? i : -1).filter(i => i >= 0);
        const durations = [487, 420, 900, 1250, 130, 900, 450, 600];
        indices = indices.filter(i => !state.filters.duration || (state.filters.duration === 1 ? durations[i] < 180 : state.filters.duration === 2 ? durations[i] >= 180 && durations[i] <= 1200 : durations[i] > 1200));
        if (state.filters.date) indices = indices.filter(i => i < [8, 1, 3, 5, 8][state.filters.date]);
        if (state.filters.sort) indices.sort((a, b) => parseInt(videos[b].views, 10) - parseInt(videos[a].views, 10));
        if (state.filters.type >= 2) {
          const names = [...new Set(indices.map(i => videos[i].channel))];
          results.replaceChildren(...names.map(name => makeButton(`${name} · ${state.filters.type === 2 ? t.channel : t.playlists}`, () => showPage('channel'))));
        } else results.replaceChildren(indices.length ? pageGrid(indices) : document.createTextNode(t.no_results));
        state.panelIndex = 0; focus($('button', results) || filters);
      });
      const actions = document.createElement('div'); actions.className = 'keyboard-actions';
      actions.append(makeButton(n.Space, () => { input.value += ' '; updateSuggestions(); }, 'keyboard-key'), makeButton(n.Delete, () => { input.value = input.value.slice(0, -1); updateSuggestions(); }, 'keyboard-key'));
      workspace.append(keyboard, actions, suggestions); updateSuggestions(); content.append(form, filters, workspace, results);
    } else if (page === 'music-collection') {
      const item = musicCollection || demoCatalog.music[0]; $('#browse-title').textContent = item.title;
      const sub = document.createElement('p'); sub.className = 'music-collection-sub'; sub.textContent = locale === 'pt-BR' ? 'As playlists tocam até 256 faixas.' : 'Playlists play up to 256 tracks.';
      browse.append(sub); content.append(catalogGrid(demoCatalog.music.slice(0, 4), 'music-tracks'));
    } else if (page === 'music') {
      const label = document.createElement('p'); label.className = 'music-account-label'; label.textContent = n.YourMusic; browse.append(label);
      const tabs = document.createElement('div'); tabs.className = 'music-tabs';
      [n.Home, n.Charts, n.Mixes].forEach((name, i) => { const tab = makeButton(name, () => { $$('.music-tabs button').forEach((b, j) => b.classList.toggle('active', i === j)); }, 'category'); tab.classList.toggle('active', !i); tabs.append(tab); }); browse.append(tabs);
      const shelves = document.createElement('div'); shelves.className = 'music-shelves';
      const shortcutShelf = document.createElement('section'); shortcutShelf.className = 'music-shelf';
      const title = document.createElement('h3'); title.textContent = n.YourMusic; shortcutShelf.append(title);
      const shortcuts = document.createElement('div'); shortcuts.className = 'music-shortcuts';
      [[n.MusicRecent, 'clock'], [n.Playlists, 'list'], [n.MusicLiked, 'heart']].forEach(([label, symbol]) => {
        const button = makeButton('', () => { $('#browse-title').textContent = label; content.replaceChildren(catalogGrid(demoCatalog.music.slice(0, 4), 'music-tracks')); focus(pageButtons()[0]); }, 'music-shortcut');
        button.innerHTML = musicShortcutIcon(symbol); const name = document.createElement('span'); name.textContent = label; button.append(name); shortcuts.append(button);
      }); shortcutShelf.append(shortcuts); shelves.append(shortcutShelf);
      [...new Set(demoCatalog.music.map(item => item.section))].forEach(section => {
        const shelf = document.createElement('section'); shelf.className = 'music-shelf'; const heading = document.createElement('h3'); heading.textContent = section;
        shelf.append(heading, catalogGrid(demoCatalog.music.filter(item => item.section === section), 'music-shelf-grid')); shelves.append(shelf);
      }); content.append(shelves);
    } else if (page === 'settings') {
      const intro = document.createElement('p'); intro.className = 'settings-intro'; intro.textContent = n.SettingsIntro;
      const help = document.createElement('p'); help.id = 'settings-help'; help.className = 'settings-help'; help.textContent = n.SavedAutomatically;
      browse.append(intro, help);
      const rows = [
        [n.YouTubeAccount, n.SignedInAs + 'Conta de demonstração'], [n.VideosPerRow, n.Four],
        [n.DefaultQuality, n.Automatic], [n.Sound, n.TV], [n.SpeakerSync, '0' + n.Milliseconds],
        [n.Language, locale === 'pt-BR' ? n.Portuguese : n.English], [n.SleepTimer, n.SleepOff],
        [n.SleepTogether, n.SleepXbox], [n.ServerMode, n.SleepOff], [n.DisplayOutput, '1280×720'],
        [n.AudioPreference, n.AudioAlwaysOriginal], [n.LargeText, n.SleepOff], [n.VideoFit, n.VideoFitLetterbox],
        [n.SearchSpeakers, n.Open], [n.Updates, '0.3.0-dev'],
        [n.SponsorBlock, [n.SleepOff, n.SponsorButton, n.SponsorAuto][state.sponsorMode]],
        [n.ClearLocalHistory, locale === 'pt-BR' ? positions.size === 0 ? 'Nenhuma posição' : `${positions.size} ${positions.size === 1 ? 'posição' : 'posições'}` : `${positions.size} ${positions.size === 1 ? 'position' : 'positions'}`],
        [n.ClearLocalLater, locale === 'pt-BR' ? saved.size === 0 ? 'Nenhum vídeo' : `${saved.size} ${saved.size === 1 ? 'vídeo' : 'vídeos'}` : `${saved.size} ${saved.size === 1 ? 'video' : 'videos'}`]
      ];
      rows.forEach(([label, value], i) => {
        const button = makeButton('', () => {
          if (i === 15) openDialog(n.SponsorBlock, t.sponsor_warning, [n.SleepOff, n.SponsorButton, n.SponsorAuto].map((label, mode) => [label, () => { state.sponsorMode = mode; closeDialog(); showPage('settings'); focus($$('.settings-row', content)[15]); }]));
          else if (i >= 16) openDialog(label, n.LocalLimit, [[t.cancel, closeDialog], [t.apply, () => { if (i === 16) positions.clear(); else saved.clear(); closeDialog(); showPage('settings'); focus($$('.settings-row', content)[i]); }]]);
          else if (i === 0) showPage('account');
        }, 'settings-row');
        const name = document.createElement('span'), detail = document.createElement('span'); name.textContent = label; detail.textContent = value; button.append(name, detail); button.dataset.setting = i;
        button.addEventListener('focus', () => keepPageFocusVisible(button)); content.append(button);
      });
    } else if (page === 'phone') {
      home.hidden = false; setRail(true, 5); state.page = 'phone';
      const phone = document.createElement('div'); phone.className = 'phone-scrim';
      const surface = document.createElement('div'); surface.className = 'phone-panel';
      const title = document.createElement('h3'); title.innerHTML = icon('native-phone'); title.append(document.createTextNode(n.CastTitle));
      const choose = document.createElement('p'); choose.className = 'phone-choose'; choose.textContent = n.CastChoose + 'YouTube 360.';
      const path = document.createElement('p'); path.className = 'phone-path'; path.textContent = n.CastPath;
      const loading = document.createElement('p'); loading.className = 'phone-loading'; loading.textContent = n.CastLoading;
      const remember = document.createElement('p'); remember.className = 'phone-remember'; remember.textContent = n.CastRemember;
      const offline = document.createElement('p'); offline.className = 'phone-offline'; offline.textContent = n.CastOffline;
      const close = makeButton('', () => showPage('home'), 'phone-close');
      close.innerHTML = `<span><b class="pad-button pad-a">A</b> ${n.Close}</span><span><b class="pad-button pad-b">B</b> ${n.Back}</span>`;
      surface.append(title, choose, path, loading, remember, offline, close);
      phone.append(surface); content.append(phone); $('#app-hints').hidden = true;
    } else {
      content.append(makeButton(n.Back, () => showPage('home')));
    }
    fitNativeType();
    if (moveFocus) focus(page === 'search' ? $('.keyboard-key', content) : page === 'channel' || page === 'subscriptions' ? $('.catalog-card', content) : pageButtons()[0]); animateIn(browse); marker(); startLoop();
  }
  $$('[data-page]').forEach(button => button.addEventListener('click', () => showPage(button.dataset.page)));
  options.forEach(([name, symbol]) => {
    const button = document.createElement('button'); button.type = 'button'; button.dataset.playerAction = name;
    button.setAttribute('aria-label', t[name]); button.title = t[name]; button.innerHTML = `<svg class="icon native-player-icon" aria-hidden="true"><use href="#i-native-player-${name}"/></svg>`;
    if (name === 'dislike') button.classList.add('dislike-icon');
    if (name === 'quality') button.insertAdjacentHTML('beforeend', '<small class="quality-badge">1080</small>');
    if (['like','dislike','repeat','captions'].includes(name)) button.setAttribute('aria-pressed','false');
    $('#player-options').append(button);
  });
  const playerButtons = $$('[data-player-action]');
  function updateOptions(moveFocus = false) {
    playerButtons.forEach((button, i) => button.classList.toggle('selected', state.zone === 'options' && i === state.option));
    const tip = $('#player-tooltip'); tip.hidden = state.zone !== 'options';
    tip.textContent = state.option === 0 ? (state.paused ? t.resume : t.pause) : t[options[state.option-1][0]];
    if (moveFocus) focus(playerButtons[state.option]); marker();
  }
  function updatePause() {
    player.classList.toggle('paused', state.paused);
    $('#player-toggle use').setAttribute('href', '#i-native-player-' + (state.paused ? 'play' : 'pause'));
    $('#player-toggle').setAttribute('aria-label', state.paused ? t.resume_preview : t.pause_preview);
    updateOptions();
  }
  let lastProgressSecond = -1;
  function updateProgress() {
    updateSponsor();
    const position = Math.floor(state.elapsed);
    if (position === lastProgressSecond) return;
    lastProgressSecond = position;
    progress.value = String(position); progress.style.setProperty('--progress', `${position / 487 * 100}%`);
    const time = `${Math.floor(position/60)}:${String(position%60).padStart(2,'0')}`;
    $('#player-time').textContent = `${time} / 8:07`; progress.setAttribute('aria-valuetext', `${time} / 8:07`);
  }
  function openPlayer(position = 7) {
    sponsorIgnored.clear(); fitNativeType();
    state.mode = 'player'; state.zone = 'timeline'; state.elapsed = position; state.sponsorUndo = false; state.sponsorUndoUntil = 0; state.paused = false; state.option = 0;
    const current = catalogSelection || videos[state.selected];
    const image = $('.player-image'); image.src = catalogSelection?.art || image.dataset.src;
    home.hidden = true; browse.hidden = true; player.hidden = false; panel.hidden = true;
    rail.hidden = true; $('#account-photo').hidden = true; $('#app-hints').hidden = true; toast.hidden = true;
    $('#player-title').textContent = current.title; $('#player-channel').textContent = current.channel;
    state.rating = ratings.get((catalogSelection || videos[state.selected]).id) || '';
    ['like','dislike'].forEach(value => $(`[data-player-action="${value}"]`).setAttribute('aria-pressed', String(state.rating === value)));
    updatePause(); updateProgress(); focus(progress); animateIn(player, 0); announce(t.player_open); marker(); startLoop();
  }
  function closePanel() { panel.hidden = true; state.mode = 'player'; state.zone = 'options'; updateOptions(true); startLoop(); }
  function openPanel(name, choices) {
    state.mode = 'player-panel'; state.panelIndex = 0; panel.hidden = false;
    panel.dataset.panel = name;
    $('.panel-back').innerHTML = name === 'quality' ? `<span><b class="pad-button pad-direction">↑↓</b> ${n.ChooseRow}</span><span><b class="pad-button pad-a">A</b> ${t.apply}</span><span><b class="pad-button pad-b">B</b> ${n.Back}</span>` : `<b class="pad-button pad-b">B</b> ${n.Back}`;
    $('#player-panel-title').textContent = t[name]; const content = $('#player-panel-content'); content.replaceChildren(); $$('.quality-note, .quality-source', panel).forEach(element => element.remove());
    choices.forEach(([label, action]) => content.append(makeButton(label, action, 'panel-choice')));
    if (name === 'quality') { const note = document.createElement('p'); note.className = 'quality-note'; note.textContent = n.ManualQualityHelp; panel.append(note); const source = document.createElement('span'); source.className = 'quality-source'; source.textContent = '1080p30'; panel.append(source); }
    fitNativeType(); focus($('.panel-choice', content)); animateIn(panel); marker();
  }
  function activatePlayer(name) {
    if (name === 'transport') { state.paused = !state.paused; updatePause(); startLoop(); return; }
    if (name === 'like' || name === 'dislike') {
      state.rating = state.rating === name ? '' : name;
      ratings.set((catalogSelection || videos[state.selected]).id, state.rating);
      ['like','dislike'].forEach(value => $(`[data-player-action="${value}"]`).setAttribute('aria-pressed', String(state.rating === value)));
      showToast(`${state.rating ? t[name === 'like' ? 'liked' : 'disliked'] : t.rating_removed}. ${t.preview_rating_note}`); return;
    }
    if (name === 'repeat' || name === 'captions') {
      state[name] = !state[name]; $(`[data-player-action="${name}"]`).setAttribute('aria-pressed', String(state[name])); showToast(t.demo_note); return;
    }
    if (name === 'channel' || name === 'channel_live') { showPage('channel'); return; }
    if (name === 'speed') {
      openPanel(name, [.25,.5,.75,1,1.25,1.5,1.75,2].map(value => [`${value}×${value === state.speed ? ' ✓' : ''}`, () => { state.speed = value; closePanel(); showToast(`${t.speed}: ${value}×`); }])); return;
    }
    if (name === 'comments') { openPanel(name, [[`Torcedor · ${t.comment_demo}`, () => {}], [t.comment_text, () => {}]]); return; }
    const choices = name === 'quality' ? [t.automatic, '1080p30', '480p', '360p', '240p', '144p'] : name === 'sound' ? ['TV', 'Google Nest · Demo'] : [t.off, '15 min', '30 min', '1 h', '2 h'];
    openPanel(name, choices.map(value => [value === 'Automatic' ? (locale === 'pt-BR' ? 'Automático' : 'Automatic') : name === 'quality' && value === '1080p30' ? '1080p · 30 fps' : value, () => { if (name === 'quality') { state.quality = value; $('.quality-badge').textContent = value === t.automatic ? 'Auto' : value.replace(/p.*$/, ''); } closePanel(); showToast(`${t[name]}: ${value}. ${t.demo_note}`); }]));
  }
  playerButtons.forEach((button, i) => {
    button.addEventListener('click', () => { state.option = i; state.zone = 'options'; updateOptions(); activatePlayer(button.dataset.playerAction); });
    button.addEventListener('focus', () => { state.option = i; state.zone = 'options'; updateOptions(); });
  });
  progress.addEventListener('focus', () => { state.zone = 'timeline'; updateOptions(); });
  progress.addEventListener('input', () => { state.elapsed = Number(progress.value); updateProgress(); });
  function command(action) {
    if (!state.visible) return;
    if (state.page === 'phone' && (action === 'a' || action === 'b')) { showPage('home'); return; }
    if (state.mode === 'dialog') {
      if (action === 'b') { closeDialog(); return; }
      const buttons = $$('button', dialog); let index = buttons.indexOf(document.activeElement);
      if (filterDraft && (action === 'left' || action === 'right')) { cycleFilter(document.activeElement, action === 'left' ? -1 : 1); return; }
      if (action === 'a') { (buttons[index] || buttons[0])?.click(); return; }
      index = Math.max(0, Math.min(buttons.length - 1, index + (action === 'down' || action === 'right' ? 1 : action === 'up' || action === 'left' ? -1 : 0)));
      focus(buttons[index]); return;
    }
    if (action === 'lb' && state.mode === 'home' && state.page === 'search') { openFilters(); return; }
    if (action === 'menu' && state.mode === 'home' && document.activeElement?.closest('.video-card')) { cardMenu(); return; }
    if (action === 'menu' && state.mode === 'player') { activatePlayer('transport'); return; }
    if (action === 'y' && state.mode === 'player') { skipSponsor(); return; }
    if (action === 'y' && state.mode === 'home') { showPage('search'); return; }
    if (action === 'x' && state.mode === 'home') { showToast(t.refreshed); return; }
    if (state.mode === 'player-panel') {
      if (action === 'b') { closePanel(); return; }
      const choices = $$('button', panel);
      state.panelIndex = Math.max(0, Math.min(choices.length - 1, state.panelIndex + (action === 'down' || action === 'right' ? 1 : action === 'up' || action === 'left' ? -1 : 0)));
      if (action === 'a') (panel.contains(document.activeElement) ? document.activeElement : choices[state.panelIndex]).click();
      else focus(choices[state.panelIndex]);
      if (panel.dataset.panel === 'quality') $('#player-panel-content').scrollTop = Math.max(0, Math.min(2, state.panelIndex - 3)) * 58;
      return;
    }
    if (state.mode === 'player') {
      if (action === 'b') {
        if (state.zone === 'options') { state.zone = 'timeline'; focus(progress); updateOptions(); }
        else { if (!catalogSelection && state.elapsed >= 10 && state.elapsed < 462) positions.set(state.selected, state.elapsed); showPage(state.page); announce(t.home_return); } return;
      }
      if (action === 'down') { state.zone = 'options'; updateOptions(true); return; }
      if (action === 'up') { state.zone = 'timeline'; focus(progress); updateOptions(); return; }
      if (state.zone === 'options') {
        if (action === 'left' || action === 'right') { state.option = Math.max(0, Math.min(playerButtons.length-1, state.option + (action === 'right' ? 1 : -1))); updateOptions(true); }
        if (action === 'a') activatePlayer(playerButtons[state.option].dataset.playerAction);
      } else {
        if (action === 'a') activatePlayer('transport');
        if (action === 'left' || action === 'right') { state.elapsed = Math.max(0,Math.min(487,state.elapsed + (action === 'right' ? 10 : -10))); updateProgress(); }
      } return;
    }
    if (state.zone === 'rail') {
      if (action === 'up' || action === 'down') setRail(true, state.rail + (action === 'down' ? 1 : -1));
      if (action === 'a') showPage(railButtons[state.rail].dataset.page);
      if (action === 'b' || action === 'right') { setRail(false); if (state.page === 'home') select(state.selected,true); else focus(pageButtons()[state.panelIndex] || pageButtons()[0]); } return;
    }
    if (action === 'b' && state.page === 'music-collection') { showPage('music'); return; }
    if (action === 'b') { if (state.page !== 'home') showPage('home'); else setRail(true); return; }
    if (state.page === 'settings') {
      const rows = $$('.settings-row', $('#browse-content'));
      let index = rows.indexOf(document.activeElement); if (index < 0) index = state.settingsIndex || 0;
      if (action === 'left' || action === 'right') return;
      if (action === 'a') { rows[index]?.click(); return; }
      if (action === 'up' || action === 'down') focus(rows[Math.max(0, Math.min(rows.length - 1, index + (action === 'down' ? 1 : -1)))]);
      return;
    }
    if (state.page !== 'home') {
      const buttons = pageButtons(); let index = buttons.indexOf(document.activeElement);
      if (index < 0) index = state.panelIndex;
      if (action === 'a') { (buttons[index] || buttons[0])?.click(); return; }
      const current = buttons[index]?.getBoundingClientRect();
      let next = -1, distance = Infinity;
      if (current) buttons.forEach((button, i) => {
        if (i === index || !button.getClientRects().length) return;
        const rect = button.getBoundingClientRect();
        const dx = rect.x + rect.width / 2 - current.x - current.width / 2;
        const dy = rect.y + rect.height / 2 - current.y - current.height / 2;
        const primary = action === 'left' ? -dx : action === 'right' ? dx : action === 'up' ? -dy : dy;
        const cross = action === 'left' || action === 'right' ? Math.abs(dy) : Math.abs(dx);
        if (primary > 1 && cross < primary * 2 && primary + cross * 3 < distance) { next = i; distance = primary + cross * 3; }
      });
      if (next < 0 && action === 'left') { setRail(true); return; }
      state.panelIndex = next < 0 ? index : next; focus(buttons[state.panelIndex]); marker(); return;
    }
    if (state.zone === 'categories') {
      if (action === 'left') { if (state.category === 0) setRail(true); else chooseCategory(state.category-1); }
      if (action === 'right') chooseCategory(state.category+1);
      if (action === 'a' && categories[state.category].dataset.page) { showPage(categories[state.category].dataset.page); return; }
      if (action === 'down' || action === 'a') { state.zone = 'grid'; select(state.selected,true); }
    } else {
      const columns = matchMedia('(max-width: 700px)').matches ? 2 : 4;
      if (action === 'a') { playCard(state.selected); return; }
      if (action === 'left') { if (state.selected % columns === 0) { setRail(true); return; } select(state.selected-1,true); }
      if (action === 'right' && state.selected % columns < columns-1) select(state.selected+1,true);
      if (action === 'down') select(state.selected+columns,true);
      if (action === 'up') { if (state.selected < columns) chooseCategory(state.category); else select(state.selected-columns,true); }
    } marker();
  }
  $$('[data-command]').forEach(button => button.addEventListener('click', () => command(button.dataset.command)));
  const keys = { ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down',Enter:'a',' ':'a',Escape:'b',Backspace:'b',a:'a',b:'b',x:'x',y:'y', '[':'lb', F2:'lb', s:'menu', ContextMenu:'menu' };
  document.addEventListener('keydown', event => {
    if (event.key === 'Tab' && !dialog.hidden) {
      const buttons = $$('button', dialog); const index = buttons.indexOf(document.activeElement);
      event.preventDefault(); focus(buttons[(index + (event.shiftKey ? buttons.length - 1 : 1)) % buttons.length]); return;
    }
    const action = keys[event.key]; if (!action || event.altKey || event.ctrlKey || event.metaKey) return;
    const target = event.target;
    if (action === 'lb' && tv.contains(target) && state.page === 'search') { event.preventDefault(); command(action); return; }
    if (target.matches('input:not([type=range]),textarea,select,[contenteditable]')) {
      if (event.key === 'Escape' && tv.contains(target)) { event.preventDefault(); showPage('home'); } return;
    }
    if (!(target === document.body || tv.contains(target) || target === tv || target.closest('.touch-controls')) || !state.visible) return;
    event.preventDefault(); command(action);
  });
  function pollGamepad(now) {
    if (state.gamepadIndex === null || !navigator.getGamepads) return;
    let pad; try { pad = navigator.getGamepads()[state.gamepadIndex]; } catch { return; }
    if (!pad) return;
    const pressed = index => !!pad.buttons[index]?.pressed;
    for (const [index,action] of [[0,'a'],[1,'b'],[2,'x'],[3,'y'],[4,'lb'],[9,'menu']]) { const down = pressed(index); if (down && !state.gamepadPrevious[index]) command(action); state.gamepadPrevious[index] = down; }
    const horizontal = pad.axes[0] || 0, vertical = pad.axes[1] || 0;
    const direction = pressed(14) || horizontal < -.55 ? 'left' : pressed(15) || horizontal > .55 ? 'right' : pressed(12) || vertical < -.55 ? 'up' : pressed(13) || vertical > .55 ? 'down' : '';
    if (direction && (state.direction !== direction || now >= state.repeatAt)) { command(direction); state.repeatAt = now + (state.direction === direction ? 145 : 360); }
    state.direction = direction;
  }
  function loop(now) {
    state.frame = null;
    if (document.hidden || !state.visible) { state.lastTime = 0; return; }
    const delta = state.lastTime ? Math.min((now-state.lastTime)/1000,.1) : 0; state.lastTime = now;
    pollGamepad(now);
    if (state.mode === 'player' && !state.paused) {
      state.elapsed += delta * state.speed;
      if (state.elapsed >= 487) { if (state.repeat) state.elapsed = 0; else { state.elapsed = 487; state.paused = true; updatePause(); } }
      updateProgress();
    }
    startLoop();
  }
  function startLoop() {
    if (state.frame !== null || document.hidden || !state.visible) return;
    if (state.gamepadIndex === null && (state.mode !== 'player' || state.paused)) return;
    state.frame = requestAnimationFrame(loop);
  }
  function stopLoop() { if (state.frame !== null) cancelAnimationFrame(state.frame); state.frame = null; state.lastTime = 0; }
  function connectGamepad(pad) {
    if (pad.mapping !== 'standard') { $('#controller-status span').textContent = t.standard_controller; return; }
    state.gamepadIndex = pad.index; state.gamepadPrevious = {};
    $('#controller-status').hidden = true; $('#connected-prompts').hidden = false; announce(t.controller_help); startLoop();
  }
  window.addEventListener('gamepadconnected',event => connectGamepad(event.gamepad));
  window.addEventListener('gamepaddisconnected', event => { if (event.gamepad.index === state.gamepadIndex) { state.gamepadIndex = null; state.gamepadPrevious = {}; $('#controller-status').hidden = false; $('#connected-prompts').hidden = true; } });
  try { const pad = navigator.getGamepads && [...navigator.getGamepads()].find(pad => pad && pad.connected); if (pad) connectGamepad(pad); } catch {}
  new IntersectionObserver(([entry]) => { state.visible = entry.isIntersecting; if (state.visible) startLoop(); else stopLoop(); }, { threshold:.08 }).observe(tv);
  document.addEventListener('visibilitychange', () => { if (document.hidden) stopLoop(); else { state.lastTime = 0; startLoop(); } });
  select(0); marker(); appHints('home'); native.dataset.page = 'home'; fitNativeType();

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
    if (window.ScrollTrigger) window.ScrollTrigger.refresh();
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

  const sessionKey = 'tube360.github.v2';
  const statsTTL = 5 * 60 * 1000;
  let statsPending = false;
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
  function showStatsTime(timestamp, live = false) {
    const date = new Date(timestamp);
    if (!Number.isFinite(date.getTime())) return;
    $$('[data-stats-time]').forEach(label => { label.dateTime = date.toISOString(); label.textContent = date.toLocaleString(locale, { dateStyle: 'medium', timeStyle: 'short' }); });
    $$('[data-stats-status]').forEach(label => { label.textContent = live ? t.stats_live : t.stats_snapshot; });
  }
  function applyReleaseDate(release) {
    if (typeof release?.published_at !== 'string') return;
    const date = new Date(release.published_at);
    if (typeof release?.tag_name !== 'string' || !Number.isFinite(date.getTime())) return;
    $$('[data-latest-release]').forEach(label => { label.textContent = release.tag_name.slice(0, 64); });
    $$('[data-release-date]').forEach(label => { label.dateTime = date.toISOString(); label.textContent = date.toLocaleDateString(locale, { dateStyle: 'medium' }); });
  }
  showStatsTime(document.body.dataset.fetchedAt);
  function applyGithub(data) {
    const release = data?.release;
    applyReleaseDate(data?.latestRelease || release);
    const asset = release?.assets?.find?.((a) => typeof a?.name === 'string' && a.name.endsWith('.zip'));
    const url = asset && safeReleaseURL(asset.browser_download_url);
    if (release?.tag_name === document.body.dataset.version && url && typeof release.tag_name === 'string' && release.tag_name.trim() && Number.isInteger(asset.size) && asset.size > 0) {
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
    if (Number.isSafeInteger(data?.downloads) && data.downloads >= 0) {
      $$('[data-download-count]').forEach(counter => {
        counter.textContent = data.downloads.toLocaleString(locale);
        counter.dataset.downloadCount = data.downloads;
      });
      document.body.dataset.downloads = data.downloads;
      showStatsTime(data.fetchedAt, true);
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
    if (cached?.attempted) applyGithub(cached);
    if (statsPending || (cached?.attempted && Date.now() - cached.fetchedAt < statsTTL)) return;
    const retryAfter = Number(document.body.dataset.githubRetryAfter) || 0;
    if (Date.now() < retryAfter * 1000) return;
    statsPending = true;
    saveSession({ ...cached, attempted: true, fetchedAt: Date.now() });
    const [releases, repo] = await Promise.all([githubData('/releases?per_page=100&page=1'), githubData('')]);
    let downloads = null;
    let page = releases;
    if (Array.isArray(releases)) {
      downloads = 0;
      for (let number = 1; ; number++) {
        if (!Array.isArray(page)) { downloads = null; break; }
        for (const release of page) {
          if (!Array.isArray(release?.assets)) { downloads = null; break; }
          for (const asset of release.assets) {
            if (typeof asset?.name !== 'string' || !/\.(zip|7z|xex)$/i.test(asset.name)) continue;
            if (!Number.isSafeInteger(asset.download_count) || asset.download_count < 0) { downloads = null; break; }
            downloads += asset.download_count;
          }
          if (downloads === null) break;
        }
        if (downloads === null || page.length < 100) break;
        page = await githubData(`/releases?per_page=100&page=${number + 1}`);
      }
    }
    const release = releases?.find?.(release => release.tag_name === document.body.dataset.version);
    const latestRelease = releases?.filter?.(item => !item.draft && !item.prerelease && Number.isFinite(Date.parse(item.published_at))).sort((a, b) => Date.parse(b.published_at) - Date.parse(a.published_at))[0];
    const data = { attempted: true, release, latestRelease, repo, downloads, fetchedAt: Date.now() };
    statsPending = false;
    saveSession(data);
    applyGithub(data);
  }
  refreshGithub();
  setInterval(() => { if (!document.hidden) refreshGithub(); }, statsTTL);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) refreshGithub(); });

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

  if (window.gsap) {
    const gsap = window.gsap;
    const motionTargets = '[data-reveal], .hero-intro, .tv-frame, .playground-caption, #home-screen, #browse-screen, #player-screen, #player-panel, #feature-image, .dialog-surface';
    const media = gsap.matchMedia();
    if (window.ScrollTrigger) {
      const ScrollTrigger = window.ScrollTrigger;
      gsap.registerPlugin(ScrollTrigger);
      media.add('(prefers-reduced-motion: no-preference)', () => {
        const entrance = gsap.timeline({ defaults: { ease: 'power3.out' } });
        entrance.fromTo('.hero-intro', { autoAlpha: 0, y: 10 }, { autoAlpha: 1, y: 0, duration: .65 })
          .fromTo('.tv-frame', { autoAlpha: 0, y: 20, scale: .985 }, { autoAlpha: 1, y: 0, scale: 1, duration: .8 }, .12)
          .fromTo('.playground-caption', { autoAlpha: 0 }, { autoAlpha: 1, duration: .4 }, .55);
        $$('[data-reveal]').forEach(element => gsap.fromTo(element,
          { autoAlpha: 0, y: 24 },
          { autoAlpha: 1, y: 0, duration: .65, ease: 'power3.out',
            scrollTrigger: { trigger: element, start: 'top 88%', once: true } }));
        gsap.fromTo('.signal-wire i', { x: 0 }, {
          x: () => $('.signal-wire').clientWidth - 14, duration: 1.2, ease: 'power1.inOut',
          scrollTrigger: { trigger: '.signal-flow', start: 'top 85%', once: true, invalidateOnRefresh: true }
        });
      });
      document.fonts.ready.then(() => ScrollTrigger.refresh());
      window.addEventListener('load', () => ScrollTrigger.refresh(), { once: true });
    }
    media.add('(prefers-reduced-motion: reduce)', () => {
      gsap.killTweensOf(motionTargets);
      gsap.set(motionTargets, { autoAlpha: 1, y: 0, scale: 1 });
      gsap.set('.signal-wire i', { x: 0 });
    });
    window.addEventListener('beforeprint', () => {
      gsap.killTweensOf(motionTargets);
      gsap.set(motionTargets, { autoAlpha: 1, y: 0, scale: 1 });
    });
  }
  document.documentElement.dataset.tube360Ready = 'true';
})();
