(() => {
  'use strict';
  const t = JSON.parse(document.getElementById('site-i18n').textContent);
  const locale = document.documentElement.lang;
  const $ = (selector, parent = document) => parent.querySelector(selector);
  const $$ = (selector, parent = document) => [...parent.querySelectorAll(selector)];
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
    gamepadIndex: null, gamepadPrevious: {}, repeatAt: 0, direction: '', panelIndex: 0 };
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
  const icon = name => `<svg class="icon" aria-hidden="true"><use href="#i-${name}"/></svg>`;
  const options = [
    ['captions', 'cc'], ['quality', 'settings'], ['sound', 'sound'], ['timer', 'moon'],
    ['like', 'like'], ['dislike', 'like'], ['comments', 'comments'], ['speed', 'speed'],
    ['repeat', 'repeat'], ['channel', 'person'], ['channel_live', 'live']
  ];
  const ratings = new Map();
  const focus = element => element?.focus({ preventScroll: true });
  function announce(text) { status.textContent = text; }
  function marker() { tv.dataset.state = state.mode === 'home' ? state.page : state.mode; tv.dataset.zone = state.zone; }
  let toastTimer;
  function showToast(text) {
    clearTimeout(toastTimer); toast.textContent = text; toast.hidden = false;
    announce(text); toastTimer = setTimeout(() => { toast.hidden = true; }, 2800);
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
    $('.video-title', button).textContent = video.title;
    $('.video-meta', button).textContent = `${video.channel} · ${localizedViews(video.views)}`;
    button.addEventListener('click', () => { state.selected = index; openPlayer(); });
    button.addEventListener('focus', () => {
      if (fromHome) { state.zone = 'grid'; select(index); marker(); }
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
  function fitScreen() {
    native.style.setProperty('--app-scale', tv.clientWidth / (matchMedia('(max-width: 700px)').matches ? 960 : 1280));
  }
  new ResizeObserver(fitScreen).observe(tv); fitScreen();
  function chooseCategory(index, moveFocus = true) {
    state.category = Math.max(0, Math.min(categories.length - 1, index)); state.zone = 'categories';
    categories.forEach((button, i) => { button.classList.toggle('active', i === state.category); button.setAttribute('aria-pressed', String(i === state.category)); button.tabIndex = i === state.category ? 0 : -1; });
    const button = categories[state.category], parent = button.parentElement;
    if (button.offsetLeft < parent.scrollLeft) parent.scrollLeft = button.offsetLeft;
    if (button.offsetLeft + button.offsetWidth > parent.scrollLeft + parent.clientWidth) parent.scrollLeft = button.offsetLeft + button.offsetWidth - parent.clientWidth;
    if (moveFocus) focus(button); select(state.selected); marker();
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
  function showPage(page, moveFocus = true) {
    if (page === 'exit') { page = 'home'; }
    state.page = page; state.mode = 'home'; state.panelIndex = 0; panel.hidden = true;
    home.hidden = page !== 'home'; browse.hidden = page === 'home'; player.hidden = true;
    rail.hidden = false; $('#account-photo').hidden = false; $('#app-hints').hidden = false;
    toast.hidden = true; setRail(false); stopLoop();
    railButtons.forEach(button => button.classList.toggle('active', button.dataset.page === page));
    const activeIndex = railButtons.findIndex(button => button.dataset.page === page);
    if (activeIndex >= 0) state.rail = activeIndex;
    if (page === 'home') { state.zone = 'grid'; select(state.selected, moveFocus); marker(); startLoop(); return; }
    state.zone = 'page'; $('#browse-title').textContent = page === 'channel' ? 'TV Palmeiras' : page === 'account' ? t.demo_account : t[page];
    const content = $('#browse-content'); content.replaceChildren();
    if (page === 'subscriptions') {
      const avatars = document.createElement('div'); avatars.className = 'channel-avatars';
      ['TV Palmeiras', 'SBT Sports', 'ge tv'].forEach((name, i) => {
        const button = makeButton('', () => showPage('channel'), 'channel-avatar');
        const art = document.createElement('span'); art.className = i === 0 ? 'channel-art' : 'channel-initial'; art.textContent = i ? (i === 1 ? 'SBT' : 'ge') : '';
        const label = document.createElement('span'); label.textContent = name;
        button.append(art, label); avatars.append(button);
      }); content.append(avatars, pageGrid([1, 2, 3, 4]));
    } else if (page === 'channel') {
      const heading = document.createElement('div'); heading.className = 'channel-heading';
      heading.innerHTML = '<span class="channel-art"></span>';
      heading.append(makeButton(state.subscribed ? t.subscribed : t.subscribe, () => {
        state.subscribed = !state.subscribed; showPage('channel'); showToast(t.demo_note);
      })); content.append(heading, pageGrid([1, 2, 3, 4]));
    } else if (page === 'library') {
      [t.watch_later, t.history, t.playlists, t.liked_videos].forEach(label => content.append(makeButton(label, () => { content.replaceChildren(pageGrid([0, 1, 5, 6])); state.panelIndex = 0; focus(pageButtons()[0]); })));
    } else if (page === 'search') {
      const form = document.createElement('form'); form.className = 'demo-search';
      const input = document.createElement('input'); input.type = 'search'; input.placeholder = t.search_placeholder; input.setAttribute('aria-label', t.search_placeholder);
      const submit = document.createElement('button'); submit.type = 'submit'; submit.textContent = t.search_submit;
      const results = document.createElement('div'); results.className = 'demo-results';
      form.append(input, submit); form.addEventListener('submit', event => {
        event.preventDefault(); const query = input.value.toLocaleLowerCase(locale).trim();
        const indices = videos.map((video,i) => video.title.toLocaleLowerCase(locale).includes(query) || video.channel.toLocaleLowerCase(locale).includes(query) ? i : -1).filter(i => i >= 0);
        results.replaceChildren(indices.length ? pageGrid(indices) : document.createTextNode(t.no_results));
        state.panelIndex = 0; if (indices.length) focus($('button', results));
      }); content.append(form);
      const keyboard = document.createElement('div'); keyboard.className = 'demo-keyboard';
      [...'QWERTYUIOPASDFGHJKLZXCVBNM', '⌫', '␣'].forEach(letter => keyboard.append(makeButton(letter, () => { input.value = letter === '⌫' ? input.value.slice(0,-1) : input.value + (letter === '␣' ? ' ' : letter.toLowerCase()); }, 'keyboard-key')));
      content.append(keyboard, results);
    } else if (page === 'music') {
      const note = document.createElement('p'); note.className = 'browse-note'; note.textContent = t.sample_videos;
      content.append(note, pageGrid([1, 2, 5, 7]));
    } else if (page === 'settings') {
      [[t.demo_account, 'Demo'], [t.language, locale === 'pt-BR' ? 'Português' : 'English'], [t.columns, '4'], [t.quality, '720p60 / 1080p30'], [t.output, '1280 × 720'], [t.sound, 'TV'], [t.captions, t.off]].forEach(([label, value], i) => {
        const button = makeButton('', () => i === 0 ? showPage('account') : showToast(t.demo_note), 'settings-row');
        const name = document.createElement('span'), detail = document.createElement('span'); name.textContent = label; detail.textContent = value; button.append(name, detail); content.append(button);
      });
    } else {
      const note = document.createElement('p'); note.className = 'browse-note'; note.textContent = page === 'phone' ? t.phone_demo : t.account_demo;
      const code = document.createElement('p'); code.className = 'demo-code'; code.textContent = page === 'phone' ? '123 456 789 012' : 'DEMO-CODE';
      const disclaimer = document.createElement('p'); disclaimer.className = 'browse-note'; disclaimer.textContent = t.demo_note;
      content.append(note, code, disclaimer, makeButton(t.back, () => showPage('home')));
    }
    if (moveFocus) focus(pageButtons()[0]); marker(); startLoop();
  }
  $$('[data-page]').forEach(button => button.addEventListener('click', () => showPage(button.dataset.page)));
  options.forEach(([name, symbol]) => {
    const button = document.createElement('button'); button.type = 'button'; button.dataset.playerAction = name;
    button.setAttribute('aria-label', t[name]); button.title = t[name]; button.innerHTML = icon(symbol);
    if (name === 'dislike') button.classList.add('dislike-icon');
    if (name === 'quality') button.insertAdjacentHTML('beforeend', '<small class="quality-badge">720</small>');
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
    $('#player-toggle').setAttribute('aria-label', state.paused ? t.resume_preview : t.pause_preview);
    updateOptions();
  }
  let lastProgressSecond = -1;
  function updateProgress() {
    const position = Math.floor(state.elapsed);
    if (position === lastProgressSecond) return;
    lastProgressSecond = position;
    progress.value = String(position); progress.style.setProperty('--progress', `${position / 487 * 100}%`);
    const time = `${Math.floor(position/60)}:${String(position%60).padStart(2,'0')}`;
    $('#player-time').textContent = `${time} / 8:07`; progress.setAttribute('aria-valuetext', `${time} / 8:07`);
  }
  function openPlayer() {
    state.mode = 'player'; state.zone = 'timeline'; state.elapsed = 7; state.paused = false; state.option = 0;
    home.hidden = true; browse.hidden = true; player.hidden = false; panel.hidden = true;
    rail.hidden = true; $('#account-photo').hidden = true; $('#app-hints').hidden = true; toast.hidden = true;
    $('#player-title').textContent = videos[state.selected].title; $('#player-channel').textContent = videos[state.selected].channel;
    state.rating = ratings.get(videos[state.selected].id) || '';
    ['like','dislike'].forEach(value => $(`[data-player-action="${value}"]`).setAttribute('aria-pressed', String(state.rating === value)));
    updatePause(); updateProgress(); focus(progress); announce(t.player_open); marker(); startLoop();
  }
  function closePanel() { panel.hidden = true; state.mode = 'player'; state.zone = 'options'; updateOptions(true); startLoop(); }
  function openPanel(name, choices) {
    state.mode = 'player-panel'; state.panelIndex = 0; panel.hidden = false;
    $('#player-panel-title').textContent = t[name]; const content = $('#player-panel-content'); content.replaceChildren();
    choices.forEach(([label, action]) => content.append(makeButton(label, action, 'panel-choice')));
    focus($('.panel-choice', content)); marker();
  }
  function activatePlayer(name) {
    if (name === 'transport') { state.paused = !state.paused; updatePause(); startLoop(); return; }
    if (name === 'like' || name === 'dislike') {
      state.rating = state.rating === name ? '' : name;
      ratings.set(videos[state.selected].id, state.rating);
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
    const choices = name === 'quality' ? ['Automatic', '720p60', '1080p30'] : name === 'sound' ? ['TV', 'Google Nest · Demo'] : [t.off, '15 min', '30 min', '1 h', '2 h'];
    openPanel(name, choices.map(value => [value === 'Automatic' ? (locale === 'pt-BR' ? 'Automático' : 'Automatic') : value, () => { closePanel(); showToast(`${t[name]}: ${value}. ${t.demo_note}`); }]));
  }
  playerButtons.forEach((button, i) => {
    button.addEventListener('click', () => { state.option = i; state.zone = 'options'; updateOptions(); activatePlayer(button.dataset.playerAction); });
    button.addEventListener('focus', () => { state.option = i; state.zone = 'options'; updateOptions(); });
  });
  progress.addEventListener('focus', () => { state.zone = 'timeline'; updateOptions(); });
  progress.addEventListener('input', () => { state.elapsed = Number(progress.value); updateProgress(); });
  function command(action) {
    if (!state.visible) return;
    if (action === 'y' && state.mode === 'home') { showPage('search'); return; }
    if (action === 'x' && state.mode === 'home') { showToast(t.refreshed); return; }
    if (state.mode === 'player-panel') {
      if (action === 'b') { closePanel(); return; }
      const choices = $$('button', panel);
      state.panelIndex = Math.max(0, Math.min(choices.length - 1, state.panelIndex + (action === 'down' || action === 'right' ? 1 : action === 'up' || action === 'left' ? -1 : 0)));
      if (action === 'a') (panel.contains(document.activeElement) ? document.activeElement : choices[state.panelIndex]).click();
      else focus(choices[state.panelIndex]); return;
    }
    if (state.mode === 'player') {
      if (action === 'b') {
        if (state.zone === 'options') { state.zone = 'timeline'; focus(progress); updateOptions(); }
        else { showPage(state.page); announce(t.home_return); } return;
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
    if (action === 'b') { if (state.page !== 'home') showPage('home'); else setRail(true); return; }
    if (state.page !== 'home') {
      if (action === 'left') { setRail(true); return; }
      const buttons = pageButtons(); let index = buttons.indexOf(document.activeElement);
      if (index < 0) index = state.panelIndex;
      if (action === 'a') { (buttons[index] || buttons[0])?.click(); return; }
      index = Math.max(0, Math.min(buttons.length-1, index + (action === 'down' || action === 'right' ? 1 : action === 'up' ? -1 : 0)));
      state.panelIndex = index; focus(buttons[index]); marker(); return;
    }
    if (state.zone === 'categories') {
      if (action === 'left') { if (state.category === 0) setRail(true); else chooseCategory(state.category-1); }
      if (action === 'right') chooseCategory(state.category+1);
      if (action === 'down' || action === 'a') { state.zone = 'grid'; select(state.selected,true); }
    } else {
      const columns = matchMedia('(max-width: 700px)').matches ? 2 : 4;
      if (action === 'a') { openPlayer(); return; }
      if (action === 'left') { if (state.selected % columns === 0) { setRail(true); return; } select(state.selected-1,true); }
      if (action === 'right' && state.selected % columns < columns-1) select(state.selected+1,true);
      if (action === 'down') select(state.selected+columns,true);
      if (action === 'up') { if (state.selected < columns) chooseCategory(state.category); else select(state.selected-columns,true); }
    } marker();
  }
  $$('[data-command]').forEach(button => button.addEventListener('click', () => command(button.dataset.command)));
  const keys = { ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down',Enter:'a',' ':'a',Escape:'b',Backspace:'b',a:'a',b:'b',x:'x',y:'y' };
  document.addEventListener('keydown', event => {
    const action = keys[event.key]; if (!action || event.altKey || event.ctrlKey || event.metaKey) return;
    const target = event.target;
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
    for (const [index,action] of [[0,'a'],[1,'b'],[2,'x'],[3,'y']]) { const down = pressed(index); if (down && !state.gamepadPrevious[index]) command(action); state.gamepadPrevious[index] = down; }
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
  select(0); marker();

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
    const data = { attempted: true, release, repo, downloads };
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

  document.documentElement.dataset.tube360Ready = 'true';
})();
