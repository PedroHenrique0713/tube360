const assert = require('node:assert/strict');
const fs = require('node:fs');
const { chromium } = require('./browser-env.cjs');
const root = process.env.SITE_URL || 'http://127.0.0.1:8279';
const out = 'site/.work/f51';
fs.mkdirSync(out, { recursive: true });
const snapshot = JSON.parse(fs.readFileSync('site/src/github.json'));
const baseline = fs.existsSync(`${out}/baseline-browser.json`) ? JSON.parse(fs.readFileSync(`${out}/baseline-browser.json`)) : [];
const ready = page => page.waitForFunction(() => document.documentElement.dataset.tube360Ready === 'true');
const settled = page => page.waitForFunction(() => document.querySelector('#community').dataset.communityReady === 'true' && !document.querySelector('[data-community-downloads]').dataset.animating);
const reveal = async page => {
  await page.locator('#community').evaluate(el => el.scrollIntoView({ block: 'start', behavior: 'instant' }));
  await settled(page);
};
const expire = async page => page.evaluate(() => {
  const cache = JSON.parse(sessionStorage.getItem('tube360.github.v2'));
  cache.fetchedAt = 0; sessionStorage.setItem('tube360.github.v2', JSON.stringify(cache));
  document.dispatchEvent(new Event('visibilitychange'));
});

(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROME_EXECUTABLE });
  const results = { matrix: [], cases: [] };
  try {
    // All six screenshots use the real build snapshot, never invented public totals.
    for (const lang of ['en', 'pt-BR']) for (const [width, height] of [[1366, 650], [1920, 1080], [390, 844]]) {
      const page = await browser.newPage({ viewport: { width, height }, locale: lang, reducedMotion: 'reduce' });
      const errors = []; page.on('pageerror', error => errors.push(error.message));
      await page.route('https://api.github.com/**', route => route.fulfill({ status: 403, json: { message: 'rate limit' } }));
      await page.goto(root + (lang === 'en' ? '/' : '/pt-br/')); await ready(page); await page.evaluate(() => document.fonts.ready);
      assert.equal(await page.locator('#community canvas').getAttribute('data-points'), null);
      assert.equal(await page.locator('[data-community-downloads]').textContent(), snapshot.download_count.toLocaleString(lang));
      const first = await page.evaluate(() => ({ documentBytes: performance.getEntriesByType('navigation')[0].encodedBodySize, resources: performance.getEntriesByType('resource').filter(r => r.name.startsWith(location.origin)).map(r => ({ path: new URL(r.name).pathname, bytes: r.encodedBodySize })), caption: document.querySelector('.playground-caption').getBoundingClientRect().bottom }));
      assert.ok(!first.resources.some(r => /community\.(js|css)$/.test(r.path)), 'community assets loaded before scrolling');
      await page.screenshot({ path: `${out}/after-${lang}-${width}-home.png` });
      await reveal(page);
      const block = await page.locator('#community').evaluate(el => {
        const bar = el.querySelector('.star-progress'), cta = el.querySelector('.community-cta');
        return { height: el.offsetHeight, width: document.documentElement.scrollWidth, points: Number(el.querySelector('canvas').dataset.points), goal: Number(bar.getAttribute('aria-valuemax')), progress: Number(bar.getAttribute('aria-valuenow')), cta: cta.getBoundingClientRect().toJSON(), bars: [...el.querySelectorAll('.version-downloads li')].map(row => ({ version: row.dataset.version, count: row.querySelector('strong').textContent })) };
      });
      assert.equal(block.width, width); assert.equal(block.points, snapshot.stargazers_count);
      assert.equal(block.progress, snapshot.stargazers_count); assert.ok(block.goal > block.progress);
      assert.equal(block.bars.length, snapshot.version_downloads.length);
      assert.deepEqual(block.bars.map(row => row.count), snapshot.version_downloads.map(row => row.downloads.toLocaleString(lang)));
      assert.ok(block.cta.x >= 0 && block.cta.right <= width && block.cta.height >= 44);
      assert.equal(await page.locator('.community-cta').getAttribute('href'), 'https://github.com/revive360/tube360');
      await page.screenshot({ path: `${out}/${lang}-${width}-community-viewport.png` });
      await page.locator('#community').screenshot({ path: `${out}/${lang}-${width}-community.png` });
      if (snapshot.version_downloads.length > 3) {
        const summary = page.locator('.version-downloads summary');
        await summary.focus(); await page.keyboard.press('Enter');
        assert.equal(await page.locator('.version-downloads details').getAttribute('open'), '');
        assert.equal(await page.locator('.version-downloads details li').first().isVisible(), true);
        if (lang === 'pt-BR' && width === 390) await page.locator('#community').screenshot({ path: `${out}/pt-BR-390-earlier-releases.png` });
        await summary.press('Enter');
      }
      const before = baseline.find(row => row.lang === lang && row.width === width);
      const firstBytes = first.documentBytes + first.resources.reduce((sum, row) => sum + row.bytes, 0);
      const beforeBytes = before ? before.bytes + before.documentBytes : null;
      if (beforeBytes) assert.ok(firstBytes <= beforeBytes, `first screen grew: ${firstBytes} > ${beforeBytes}`);
      assert.equal(before ? first.caption : 0, before ? before.caption : 0, 'hero geometry changed');
      assert.deepEqual(errors, []);
      results.matrix.push({ lang, width, height, firstBytes, beforeBytes, deltaBytes: before ? firstBytes - beforeBytes : null, first, block, errors });
      await page.close();
    }

    // Real runtime fetches supply deterministic fixtures for data and motion proofs.
    const context = await browser.newContext({ viewport: { width: 390, height: 844 }, locale: 'pt-BR', reducedMotion: 'no-preference' });
    const page = await context.newPage(); const errors = []; page.on('pageerror', e => errors.push(e.message));
    let stars = 24, blocked = false, count = 103, requests = 0;
    const releases = () => [
      { tag_name: 'v0.3.2', published_at: '2026-11-25T12:00:00Z', assets: [{ name: 'app.zip', download_count: count - 63 }, { name: 'checksum.sha256', download_count: 999 }] },
      { tag_name: 'v0.3.1', published_at: '2026-11-24T12:00:00Z', assets: [{ name: 'app.zip', download_count: 10 }, { name: 'app.7z', download_count: 20 }, { name: 'app.xex', download_count: 30 }] },
      { tag_name: 'v0.3.0', published_at: '2026-11-23T12:00:00Z', assets: [{ name: 'APP.ZIP', download_count: 3 }] }
    ];
    await page.route('https://api.github.com/**', route => {
      requests++;
      return route.fulfill(blocked ? { status: 403, json: {} } : { json: route.request().url().includes('/releases?') ? releases() : { stargazers_count: stars } });
    });
    await page.goto(root + '/pt-br/'); await ready(page); await page.waitForFunction(() => document.body.dataset.downloads === '103');
    assert.equal(requests, 2);
    await page.locator('#community').evaluate(el => el.scrollIntoView({ block: 'start', behavior: 'instant' }));
    await page.waitForFunction(() => document.querySelector('[data-community-downloads]').dataset.animating === 'true');
    const frames = await page.evaluate(() => new Promise(resolve => {
      const samples = [];
      const sample = () => {
        const reel = document.querySelector('.odometer-reel');
        samples.push({ reel: reel && getComputedStyle(reel).transform, canvas: document.querySelector('#community canvas').toDataURL() });
        if (samples.length < 90) requestAnimationFrame(sample); else resolve({ frames: samples.length, reelStates: new Set(samples.map(s => s.reel)).size, skyStates: new Set(samples.map(s => s.canvas)).size });
      };
      requestAnimationFrame(sample);
    }));
    assert.ok(frames.reelStates > 10 && frames.skyStates > 10, JSON.stringify(frames)); await settled(page);
    assert.equal(await page.locator('[data-star-remaining]').textContent(), 'faltam 1');
    assert.equal(await page.locator('.version-downloads li[data-version="v0.3.2"] strong').textContent(), '40');
    assert.equal(await page.locator('.version-downloads li[data-version="v0.3.1"] strong').textContent(), '60');
    assert.match(await page.locator('#community [data-release-date]').getAttribute('datetime'), /^2026-11-25/);
    const idles = await page.evaluate(async () => {
      const c = document.querySelector('#community canvas'), a = c.toDataURL();
      await new Promise(resolve => { let ticks = 0; function step() { if (++ticks === 30) resolve(); else requestAnimationFrame(step); } step(); });
      return a === c.toDataURL();
    }); assert.equal(idles, true, 'sky keeps moving after the entry');

    for (const [value, goal] of [[25, 50], [50, 100], [100, 250], [250, 500], [500, 1000], [1000, 2500], [0, 25]]) {
      stars = value; await expire(page);
      await page.waitForFunction(value => document.querySelector('#community canvas').dataset.points === String(value), value);
      assert.equal(await page.locator('.star-progress').getAttribute('aria-valuemax'), String(goal));
      assert.equal(await page.locator('[data-star-remaining]').textContent(), `faltam ${(goal - value).toLocaleString('pt-BR')}`);
    }
    stars = 26; count = 1234; await expire(page);
    await page.waitForFunction(() => document.querySelector('[data-community-downloads]').dataset.animating === 'true');
    await page.emulateMedia({ reducedMotion: 'reduce' }); await settled(page);
    assert.equal(await page.locator('[data-community-downloads]').textContent(), '1.234');
    assert.equal(await page.locator('[data-community-downloads]').getAttribute('aria-label'), '1.234 downloads');
    assert.equal(await page.locator('.odometer-reel').count(), 0);
    const goodCache = await page.evaluate(() => JSON.parse(sessionStorage.getItem('tube360.github.v2')));
    blocked = true; await expire(page); await page.waitForLoadState('networkidle');
    assert.equal(await page.locator('[data-community-downloads]').textContent(), '1.234');
    assert.equal(await page.evaluate(() => JSON.parse(sessionStorage.getItem('tube360.github.v2')).verifiedAt), goodCache.verifiedAt);
    const beforeLanguage = requests; await page.goto(root); await ready(page); await reveal(page);
    assert.equal(requests, beforeLanguage, 'EN/PT session cache caused another fetch');
    assert.equal(await page.locator('[data-community-downloads]').textContent(), '1,234');
    assert.equal(await page.locator('.star-progress').getAttribute('aria-valuenow'), '26');
    await page.evaluate(() => window.dispatchEvent(new Event('beforeprint')));
    assert.equal(await page.locator('[data-community-downloads]').textContent(), '1,234');
    assert.deepEqual(errors, []); results.cases.push({ runtime: true, motion: frames, idleSky: idles, milestones: true, liveReleaseDate: true, zeroStars: true, reducedMotionSwitch: true, cachedFailure: true, sessionLanguage: true, print: true, errors });
    await context.close();

    // 404 migration fallback, no dependencies, no JS, blocked storage and broken pagination.
    for (const mode of ['migration', 'cdn-blocked', 'storage-blocked', 'no-js', 'partial-page']) {
      const page = await browser.newPage({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce', javaScriptEnabled: mode !== 'no-js' });
      const errors = []; page.on('pageerror', error => errors.push(error.message)); const requested = [];
      if (mode === 'cdn-blocked') await page.route('https://cdn.jsdelivr.net/**', r => r.abort());
      if (mode === 'storage-blocked') await page.addInitScript(() => { Object.defineProperty(window, 'sessionStorage', { get() { throw new Error('disabled'); } }); });
      await page.route('https://api.github.com/**', route => {
        const url = route.request().url(); requested.push(url);
        if (mode === 'migration') return route.fulfill(url.includes('/revive360/') ? { status: 404, json: {} } : { json: url.includes('/releases?') ? releases() : { stargazers_count: 12 } });
        if (mode === 'partial-page' && url.includes('page=1')) return route.fulfill({ json: Array.from({ length: 100 }, (_, i) => ({ tag_name: `v${i}`, assets: [{ name: 'app.zip', download_count: 999 }] })) });
        return route.fulfill({ status: 403, json: {} });
      });
      await page.goto(root);
      if (mode !== 'no-js') { await ready(page); await reveal(page); }
      else await page.locator('#community').scrollIntoViewIfNeeded();
      if (mode === 'migration') {
        await page.waitForFunction(() => document.body.dataset.stars === '12');
        assert.ok(requested.some(url => url.includes('/PedroHenrique0713/')));
        assert.equal(await page.locator('.version-downloads li').count(), 3);
      } else {
        assert.equal(await page.locator('[data-community-downloads]').textContent(), snapshot.download_count.toLocaleString('en'));
        assert.equal(await page.locator('.version-downloads li').count(), snapshot.version_downloads.length);
      }
      assert.equal(await page.locator('#community').evaluate(() => document.documentElement.scrollWidth), 390);
      assert.deepEqual(errors, []); results.cases.push({ mode, requests: requested.length, errors }); await page.close();
    }
    fs.writeFileSync(`${out}/community-results.json`, JSON.stringify({ status: 'PASS', ...results }, null, 2));
    console.log('PASS: F51 six EN/PT viewports; real snapshot screenshots; lazy assets; one point per star, milestone edges/zero, real odometer/twinkle frames then idle; version/package sums, live date, session/TTL/failure, migration 404, reduced motion, CDN/storage/no-JS/partial-page fallback; zero JS errors.');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
