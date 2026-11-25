(() => {
  'use strict';
  const section = document.querySelector('#community');
  const t = JSON.parse(document.querySelector('#site-i18n').textContent);
  const locale = document.documentElement.lang;
  const number = value => value.toLocaleString(locale);
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  const canvas = section.querySelector('canvas');
  const ctx = canvas.getContext('2d');
  const total = section.querySelector('[data-community-downloads]');
  const progress = section.querySelector('.star-progress');
  const bars = section.querySelector('.version-downloads');
  let data = JSON.parse(document.querySelector('#community-data').textContent);
  let visible = false, entered = false, drawnStars = null, shownDownloads = null;
  let skyTween, countTween, barTween;
  const light = { glow: 0 };

  function goalFor(count) {
    const fixed = [25, 50, 100, 250, 500].find(goal => count < goal);
    if (fixed) return fixed;
    const scale = 10 ** (String(count).length - 1);
    return [1, 2.5, 5, 10].map(multiple => scale * multiple).find(goal => goal > count);
  }

  function paintSky() {
    if (!ctx || !visible) return;
    const width = canvas.clientWidth, height = canvas.clientHeight;
    if (!width || !height) return;
    const ratio = Math.min(devicePixelRatio || 1, 2);
    if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
      canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
    }
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    ctx.clearRect(0, 0, width, height);
    // The index gives each real star a stable position, including after a resize.
    for (let index = 0; index < data.stars; index++) {
      const x = (0.07 + ((index * 0.61803398875 + 0.19) % 1) * 0.86) * width;
      const y = (0.12 + ((index * 0.41421356237 + 0.23) % 1) * 0.61) * height;
      const newest = index === data.stars - 1;
      const radius = data.stars > 1000 ? 0.65 : 1.25 + (index % 3) * 0.35;
      ctx.fillStyle = index % 4 === 0 ? '#ffb4a9' : '#f5f5f7';
      ctx.globalAlpha = 0.55 + (index % 4) * 0.15;
      if (data.stars <= 1000) {
        ctx.shadowColor = newest ? '#ff5a52' : '#f5f5f7';
        ctx.shadowBlur = newest ? 12 + light.glow * 20 : 7;
        ctx.beginPath(); ctx.arc(x, y, radius + (newest ? light.glow * 2 : 0), 0, Math.PI * 2); ctx.fill();
        if (index % 3 === 0 || newest) {
          ctx.globalAlpha = 0.35 + (newest ? light.glow * 0.5 : 0);
          ctx.fillRect(x - 5, y - 0.4, 10, 0.8); ctx.fillRect(x - 0.4, y - 5, 0.8, 10);
        }
      } else ctx.fillRect(x, y, radius, radius);
    }
    ctx.shadowBlur = 0; ctx.globalAlpha = 1;
    canvas.dataset.points = data.stars;
  }

  function odometer(animate) {
    countTween?.kill();
    total.dataset.final = data.downloads;
    total.setAttribute('aria-label', `${number(data.downloads)} ${t.downloads_label}`);
    total.textContent = number(data.downloads);
    if (!animate || !window.gsap || motion.matches) { delete total.dataset.animating; return; }
    const fragment = document.createDocumentFragment();
    const reels = [];
    for (const character of number(data.downloads)) {
      const digit = document.createElement('span'); digit.setAttribute('aria-hidden', 'true');
      if (/\d/.test(character)) {
        digit.className = 'odometer-digit';
        const reel = document.createElement('span'); reel.className = 'odometer-reel';
        for (let i = 0; i < 20; i++) { const cell = document.createElement('span'); cell.textContent = i % 10; reel.append(cell); }
        digit.append(reel); reels.push({ reel, target: -(10 + Number(character)) * 5 });
      } else { digit.className = 'odometer-separator'; digit.textContent = character; }
      fragment.append(digit);
    }
    total.replaceChildren(fragment); total.dataset.animating = 'true';
    countTween = window.gsap.timeline({ onComplete: () => { total.textContent = number(data.downloads); delete total.dataset.animating; } });
    reels.forEach(({ reel, target }, index) => countTween.fromTo(reel, { yPercent: 0 }, { yPercent: target, duration: 1.25, ease: 'power3.out' }, index * 0.06));
  }

  function renderVersions(animate) {
    barTween?.kill();
    const versions = Array.isArray(data.versions) && data.versions.every(row => typeof row.tag_name === 'string' && Number.isSafeInteger(row.downloads) && row.downloads >= 0) && data.versions.reduce((sum, row) => sum + row.downloads, 0) === data.downloads ? data.versions : [];
    const largest = Math.max(0, ...versions.map(row => row.downloads));
    const fragment = document.createDocumentFragment();
    versions.forEach(row => {
      const item = document.createElement('li'); item.dataset.version = row.tag_name;
      const label = document.createElement('div'), tag = document.createElement('span'), count = document.createElement('strong');
      tag.textContent = row.tag_name; count.textContent = number(row.downloads); label.append(tag, count);
      const track = document.createElement('div'), fill = document.createElement('i');
      track.className = 'version-track'; track.setAttribute('aria-hidden', 'true'); fill.style.setProperty('--fill', largest ? row.downloads / largest : 0);
      track.append(fill); item.append(label, track); fragment.append(item);
    });
    bars.replaceChildren(fragment);
    section.querySelector('.version-pending').hidden = versions.length > 0;
    if (animate && window.gsap && !motion.matches) {
      barTween = window.gsap.fromTo(bars.querySelectorAll('i'), { scaleX: 0 }, { scaleX: (_, el) => Number(el.style.getPropertyValue('--fill')), duration: 1, stagger: 0.08, ease: 'power2.out', clearProps: 'transform' });
    }
  }

  function render(animate = true) {
    if (!visible) return;
    const goal = goalFor(data.stars);
    section.querySelector('[data-star-goal]').textContent = number(goal);
    section.querySelector('[data-star-remaining]').textContent = t.stars_remaining.replace('{count}', number(goal - data.stars));
    progress.setAttribute('aria-valuemax', goal); progress.setAttribute('aria-valuenow', data.stars);
    progress.querySelector('i').style.setProperty('--fill', data.stars / goal);
    renderVersions(animate && !entered);
    if (shownDownloads !== data.downloads || !animate) odometer(animate);
    shownDownloads = data.downloads;
    if (drawnStars !== data.stars) {
      skyTween?.kill(); light.glow = 0;
      if (animate && window.gsap && !motion.matches && data.stars > 0 && (drawnStars === null || data.stars > drawnStars)) {
        skyTween = window.gsap.to(light, { glow: 1, duration: 0.55, repeat: 1, yoyo: true, ease: 'sine.inOut', onUpdate: paintSky });
      }
      drawnStars = data.stars;
    }
    paintSky(); entered = true; section.dataset.communityReady = 'true';
  }

  function finish() {
    skyTween?.kill(); countTween?.kill(); barTween?.kill(); light.glow = 0;
    if (entered) { odometer(false); renderVersions(false); paintSky(); }
  }
  new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting;
    if (visible) render(); else finish();
  }).observe(section);
  new ResizeObserver(() => paintSky()).observe(canvas);
  document.addEventListener('tube360:stats', event => { data = event.detail; render(); });
  motion.addEventListener('change', () => { if (motion.matches) finish(); });
  document.addEventListener('visibilitychange', () => { if (document.hidden) finish(); });
  window.addEventListener('beforeprint', () => { visible = true; render(false); finish(); });
})();
