const assert = require('node:assert/strict');
const fs = require('node:fs');
const { chromium } = require('./browser-env.cjs');
const root = process.env.SITE_URL || 'http://127.0.0.1:8279';
const out = 'site/.work';
const settle = p => p.waitForFunction(() => !window.gsap?.isTweening('.hero-intro, .tv-frame, .playground-caption, #browse-screen, #player-screen, #player-panel, .dialog-surface'));
(async () => {
 const browser = await chromium.launch({ executablePath: process.env.CHROME_EXECUTABLE });
 const results = [];
 try {
  for (const lang of ['en', 'pt-BR']) for (const [width, height] of [[1366, 650], [1920, 1080], [390, 844]]) {
   const context = await browser.newContext({ viewport: { width, height }, locale: lang, hasTouch: width === 390, reducedMotion: 'reduce' });
   const page = await context.newPage(); page.setDefaultTimeout(10000);
   const errors = []; page.on('pageerror', error => errors.push(error.message));
   await page.route('https://api.github.com/**', r => r.fulfill({ status: 403, json: {} }));
   await page.goto(root + (lang === 'en' ? '/' : '/pt-br/'));
   await page.waitForFunction(() => document.documentElement.dataset.tube360Ready === 'true');
   await page.evaluate(() => document.fonts.ready); await settle(page);
   const metrics = await page.evaluate(() => ({
    width: document.documentElement.scrollWidth,
    captionBottom: document.querySelector('.playground-caption').getBoundingClientRect().bottom,
    bytes: performance.getEntriesByType('resource').filter(r => r.name.startsWith(location.origin)).reduce((sum, r) => sum + r.encodedBodySize, 0),
    release: document.querySelector('[data-release-date]').dateTime,
    counterFont: parseFloat(getComputedStyle(document.querySelector('[data-download-count]')).fontSize)
   }));
   assert.equal(metrics.width, width); assert.ok(metrics.counterFont >= 30);
   assert.equal(Date.parse(metrics.release), Date.parse(JSON.parse(fs.readFileSync('site/src/github.json')).published_at));
   assert.ok(metrics.bytes <= (width === 390 ? 976191 : 1267192), JSON.stringify(metrics));
   if (height === 650) assert.ok(metrics.captionBottom <= height);
   await page.screenshot({ path: `${out}/${lang}-${width}-home.png` });
   const shot = async name => page.screenshot({ path: `${out}/${lang}-${width}-${name}.png` });
   // Search suggestions and a filter draft that B must discard.
   await page.locator('#tv').focus(); await page.keyboard.press('y');
   await page.locator('.demo-search input').fill('Palmeiras');
   assert.equal(await page.locator('.suggestion').count(), 2); await shot('search');
   await page.keyboard.press('F2'); assert.equal(await page.getAttribute('#tv', 'data-state'), 'dialog');
   await page.keyboard.press('ArrowRight'); await page.keyboard.press('Escape');
   await page.locator('#search-filters').click();
   assert.ok((await page.locator('[data-filter=type] strong').textContent()).match(/All|Tudo/));
   await page.keyboard.press('ArrowRight'); // Video
   await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowRight'); // under 3 min
   await shot('filters');
   await page.locator('#dialog-actions > .panel-choice').first().click();
   await page.locator('.demo-search button[type=submit]').click();
   assert.equal(await page.locator('.demo-results .video-card').count(), 1);
   await page.keyboard.press('Escape');
   // Start offers a separate console action; confirm through a real pointer/tap.
   await page.locator('#video-grid .video-card').nth(2).focus(); await page.keyboard.press('s');
   await shot('card-menu'); await page.locator('#dialog-actions button').nth(1).click();
   await page.locator('.sidebar-icon[data-page=library]').click();
   await page.locator('#browse-content .library-card').first().click();
   assert.ok((await page.locator('#browse-title').textContent()).includes('console'));
   assert.equal(await page.locator('#browse-content .video-card').count(), 2); await shot('later');
   await page.keyboard.press('Escape');
   await page.locator('.sidebar-icon[data-page=library]').click();
   await page.locator('#browse-content .library-card').nth(1).click();
   await page.locator('#browse-content .video-card').first().click();
   assert.equal(await page.getAttribute('#tv', 'data-state'), 'dialog'); await shot('resume');
   // Modal focus stays inside the dialog, and B returns without playing.
   await page.keyboard.press('Shift+Tab'); assert.ok(await page.locator('#demo-dialog').evaluate(el => el.contains(document.activeElement)));
   await page.keyboard.press('Escape'); assert.equal(await page.getAttribute('#tv', 'data-state'), 'continue');
   await page.locator('#browse-content .video-card').first().click(); await page.locator('#dialog-actions button').first().click();
   assert.ok(Number(await page.inputValue('#player-progress')) >= 84);
   await page.keyboard.press('Escape'); // options -> timeline if focus had changed
   if (await page.getAttribute('#tv', 'data-state') === 'player') await page.keyboard.press('Escape');
   await page.keyboard.press('Escape'); // Home
   // A new video begins inside a synthetic sponsor segment; Y skips, then opens Quality.
   await page.locator('#video-grid .video-card').first().click();
   await page.keyboard.press('Enter'); // pause
   assert.equal(await page.locator('#sponsor-skip').isVisible(), true); await shot('sponsor');
   const before = Number(await page.inputValue('#player-progress'));
   await page.keyboard.press('y'); assert.ok(Number(await page.inputValue('#player-progress')) > before);
   await page.keyboard.press('y'); assert.equal(await page.getAttribute('#tv', 'data-state'), 'player-panel');
   const quality = await page.locator('#player-panel .panel-choice').allTextContents();
   for (const value of ['1080p30', '480p', '360p', '240p', '144p']) assert.ok(quality.some(label => label.replace(/[ ·]/g, '').includes(value)));
   const qualityBox = await page.locator('#player-panel').boundingBox(), tvBox = await page.locator('#tv').boundingBox();
   assert.ok(qualityBox.x >= tvBox.x && qualityBox.x + qualityBox.width <= tvBox.x + tvBox.width + 1, JSON.stringify({ qualityBox, tvBox }));
   await shot('quality'); await page.locator('#player-panel .panel-choice').nth(2).click();
   assert.equal(await page.textContent('.quality-badge'), '480');
   await page.keyboard.press('Escape'); await page.keyboard.press('Escape');
   // Current Music shortcut artwork and channel Shorts navigation.
   await page.locator('.sidebar-icon[data-page=music]').click();
   assert.equal(await page.locator('.music-shortcut').count(), 3); await shot('music');
   assert.equal(await page.locator('.music-shortcut svg[viewBox="0 0 110 110"]').count(), 3);
   await page.locator('.music-shortcut').first().click(); assert.equal(await page.locator('.browse-grid .video-card').count(), 4);
   await page.locator('.sidebar-icon[data-page=subscriptions]').click(); await page.locator('.channel-avatar').first().click();
   await page.locator('.browse-tabs button').nth(1).click(); assert.equal(await page.locator('.browse-tabs button').nth(1).getAttribute('aria-pressed'), 'true');
   assert.equal(await page.locator('.shorts-grid .video-card').count(), 4); await shot('shorts');
   await page.keyboard.press('Escape');
   // Automatic skip and undo, selected through Settings, use the same synthetic marks.
   await page.locator('.sidebar-icon[data-page=settings]').click(); await page.locator('#browse-content .settings-row').nth(15).click();
   await page.locator('#dialog-actions button').nth(2).click(); await page.keyboard.press('Escape');
   await page.locator('#video-grid .video-card').first().click();
   if (await page.getAttribute('#tv', 'data-state') === 'dialog') await page.locator('#dialog-actions button').nth(1).click();
   await page.locator('#player-progress').focus(); await page.keyboard.press('ArrowRight');
   await page.keyboard.press('Enter'); assert.ok(Number(await page.inputValue('#player-progress')) >= 24);
   assert.ok((await page.locator('#sponsor-skip').textContent()).match(/Go back|Voltar/));
   await page.keyboard.press('y'); assert.ok(Number(await page.inputValue('#player-progress')) < 24);
   for (let move = 0; move < 18; move++) await page.keyboard.press('ArrowRight');
   assert.equal(Number(await page.inputValue('#player-progress')), 219);
   await page.keyboard.press('y'); assert.ok(Number(await page.inputValue('#player-progress')) < 219);
   await page.keyboard.press('ArrowRight'); assert.ok(Number(await page.inputValue('#player-progress')) < 219);
   // All reveal sections and both community counters remain available after scrolling.
   for (const element of await page.locator('[data-reveal]').all()) await element.evaluate(el => el.scrollIntoView({ behavior: 'instant', block: 'center' }));
   await page.locator('#community').evaluate(el => el.scrollIntoView({ behavior: 'instant', block: 'center' }));
   await page.waitForFunction(() => document.querySelector('#community').dataset.communityReady === 'true' && !document.querySelector('[data-community-downloads]').dataset.animating);
   await shot('community');
   assert.equal(await page.locator('#community [data-community-downloads]').textContent(), await page.locator('.hero-metrics [data-download-count]').textContent());
   await page.locator('#star').evaluate(el => el.scrollIntoView({ behavior: 'instant', block: 'center' })); await shot('star');
   assert.equal(await page.locator('#star [data-stars]').count(), 1);
   assert.equal(await page.evaluate(() => document.documentElement.scrollWidth), width);
   assert.deepEqual(errors, []); results.push({ lang, width, height, metrics, errors });
   await context.close();
  }
  // Refresh after an expired session; release date follows the newest stable release.
  const context = await browser.newContext({ reducedMotion: 'reduce', locale: 'en' }); const page = await context.newPage(); let calls = 0;
  await page.addInitScript(() => sessionStorage.setItem('tube360.github.v2', JSON.stringify({ attempted: true, fetchedAt: Date.now() - 301000, downloads: 3, repo: { stargazers_count: 2 } })));
  await page.route('https://api.github.com/**', route => {
   calls++;
   return route.fulfill({ json: route.request().url().includes('/releases?') ? [
    { tag_name: 'v0.4.0-rc1', prerelease: true, published_at: '2026-11-27T00:00:00Z', assets: [{ name: 'app.zip', download_count: 4 }] },
    { tag_name: 'v0.3.2', published_at: '2026-11-26T00:00:00Z', assets: [{ name: 'app.ZIP', download_count: 996 }] }
   ] : { stargazers_count: 1234 } });
  });
  await page.goto(root); await page.waitForFunction(() => document.body.dataset.downloads === '1000');
  assert.equal(calls, 2); assert.equal(await page.textContent('[data-latest-release]'), 'v0.3.2');
  assert.ok((await page.textContent('[data-release-date]')).includes('26'));
  const starCounts = await page.locator('span[data-stars]').allTextContents(); assert.deepEqual(starCounts, ['1,234', '1,234', '1,234']);
  await context.close();
  // A standard Gamepad API fixture exercises A, B, Y, LB and Start through real polling.
  const game = await browser.newContext({ reducedMotion: 'reduce' }); const gp = await game.newPage();
  await gp.addInitScript(() => {
   window.pad = { index: 0, connected: true, mapping: 'standard', axes: [0, 0], buttons: Array.from({ length: 16 }, () => ({ pressed: false })) };
   Object.defineProperty(navigator, 'getGamepads', { value: () => [window.pad] });
  });
  await gp.route('https://api.github.com/**', route => route.fulfill({ status: 403, json: {} }));
  await gp.goto(root); await gp.waitForFunction(() => document.documentElement.dataset.tube360Ready === 'true');
  async function press(index) { await gp.evaluate(i => { pad.buttons[i].pressed = true; }, index); await gp.waitForTimeout(60); await gp.evaluate(i => { pad.buttons[i].pressed = false; }, index); await gp.waitForTimeout(60); }
  await press(3); assert.equal(await gp.getAttribute('#tv', 'data-state'), 'search'); await press(4); assert.equal(await gp.getAttribute('#tv', 'data-state'), 'dialog');
  await press(1); await press(1); assert.equal(await gp.getAttribute('#tv', 'data-state'), 'home');
  await press(9); assert.equal(await gp.getAttribute('#tv', 'data-state'), 'dialog'); await press(1); await press(0); assert.equal(await gp.getAttribute('#tv', 'data-state'), 'player');
  await game.close();
  fs.writeFileSync(`${out}/v032-results.json`, JSON.stringify({ status: 'PASS', matrix: results, expiredCache: true, latestStableDate: true, standardGamepadFixture: true }, null, 2));
  console.log('PASS: v0.3.2 demo flows × EN/PT × 3 sizes; counters/date/expired cache; Start, LB, filters/discard, local lists/resume, SponsorBlock skip/auto/undo, quality, Shorts, Music; Gamepad API fixture; zero JS errors.');
 } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
