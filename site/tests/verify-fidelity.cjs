const assert = require('node:assert/strict');
const fs = require('node:fs');
const { chromium } = require('./browser-env.cjs');
const root = process.env.SITE_URL || 'http://127.0.0.1:8279';
(async () => {
 const browser = await chromium.launch({ executablePath: process.env.CHROME_EXECUTABLE });
 const results = [];
 try {
  for (const lang of ['pt-BR', 'en']) for (const [width, height] of [[1366, 650], [390, 844]]) {
   const context = await browser.newContext({ viewport: { width, height }, locale: lang, reducedMotion: 'reduce', hasTouch: width === 390 });
   const page = await context.newPage(); const errors = []; page.on('pageerror', e => errors.push(e.message));
   await page.route('https://api.github.com/**', r => r.fulfill({ status: 403, json: {} }));
   await page.goto(root + (lang === 'pt-BR' ? '/pt-br/' : '/')); await page.waitForFunction(() => document.documentElement.dataset.tube360Ready === 'true');
   await page.locator('.sidebar-icon[data-page=settings]').click();
   const labels = await page.locator('#browse-content .settings-row > span:first-child').allTextContents();
   assert.equal(labels.length, 16);
   if (lang === 'pt-BR') assert.deepEqual(labels, ['Conta do YouTube', 'Vídeos por linha', 'Qualidade padrão', 'Som', 'Sincronia da caixa', 'Idioma', 'Timer de desligar', 'Saída da TV', 'Preferência de áudio', 'Texto grande', 'Tamanho da imagem', 'Procurar alto-falantes', 'Atualizações do app', 'SponsorBlock', 'Apagar histórico local', 'Apagar lista local']);
   const positions = [];
   async function visibleSelection() {
    const geometry = await page.locator('#browse-content').evaluate(viewport => {
     const selected = document.activeElement, rect = selected.getBoundingClientRect(), box = viewport.getBoundingClientRect(), hints = document.querySelector('#app-hints').getBoundingClientRect();
     return { selected: selected.dataset.setting, top: rect.top, bottom: rect.bottom, viewportTop: box.top, viewportBottom: box.bottom, hintTop: hints.top, scrollTop: viewport.scrollTop, valueColor: getComputedStyle(selected.lastElementChild).color };
    });
    assert.ok(geometry.top >= geometry.viewportTop - 1 && geometry.bottom <= geometry.viewportBottom + 1 && geometry.bottom < geometry.hintTop, JSON.stringify(geometry));
    assert.equal(geometry.valueColor, geometry.selected === '3' ? 'rgb(161, 161, 170)' : 'rgb(255, 90, 82)');
    positions.push(geometry); return geometry;
   }
   await visibleSelection();
   for (let row = 1; row < 16; row++) { await page.keyboard.press('ArrowDown'); assert.equal((await visibleSelection()).selected, String(row)); }
   assert.ok((await page.textContent('#settings-help')).includes('200'));
   assert.ok((await page.textContent('#settings-help')).includes('500'));
   const values = await page.locator('#browse-content .settings-row > span:last-child').evaluateAll(elements => elements.filter(el => el.parentElement.dataset.setting !== '3' && el.parentElement !== document.activeElement).map(el => getComputedStyle(el).color));
   assert.ok(values.every(value => value === 'rgb(245, 245, 247)'));
   assert.equal(await page.locator('#browse-content .settings-row:focus > span:last-child').evaluate(el => getComputedStyle(el).color), 'rgb(255, 90, 82)');
   // A real pointer must activate a deep row without the focus handler moving it away.
   await page.keyboard.press('Escape'); await page.locator('.sidebar-icon[data-page=settings]').click();
   await page.locator('[data-setting="13"]').click(); assert.equal(await page.locator('#dialog-actions button').count(), 3); await page.keyboard.press('Escape');
   await page.locator('[data-setting="15"]').click(); assert.equal(await page.locator('#dialog-actions button').count(), 2); await page.keyboard.press('Escape'); await visibleSelection();
   if (width === 390) {
    await page.keyboard.press('Escape'); await page.locator('.sidebar-icon[data-page=settings]').click();
    const box = await page.locator('#browse-content').boundingBox(); const before = await page.locator('#browse-content').evaluate(el => el.scrollTop);
    const cdp = await context.newCDPSession(page);
    const x = box.x + box.width / 2, y = box.y + box.height - 15;
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 1 }] });
    for (let step = 1; step <= 10; step++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x, y: y - step * 10, id: 1 }] }); await page.waitForTimeout(16); }
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await page.waitForTimeout(100); assert.ok(await page.locator('#browse-content').evaluate(el => el.scrollTop) > before);
    await page.locator('[data-setting="15"]').tap(); assert.equal(await page.locator('#dialog-actions button').count(), 2); await page.keyboard.press('Escape'); await visibleSelection();
   }
   await page.keyboard.press('Escape'); await page.locator('.sidebar-icon[data-page=music]').click();
   assert.equal(await page.locator('.music-shortcut').count(), 3);
   const musicText = await page.locator('#browse-content').innerText(); assert.ok(!/Palmeiras|SBT Sports|ge tv|Start numa faixa|preview|prévia/i.test(musicText));
   const art = await page.locator('.music-shelf-grid img').evaluateAll(images => images.map(image => image.getAttribute('src')));
   assert.equal(art.length, 12); assert.ok(art.every(src => src.startsWith('/assets/demo/music-')));
   assert.equal(await page.locator('.music-shortcut svg circle[fill="#ff5a52"]').count(), 1);
   await page.locator('.sidebar-icon[data-page=library]').click(); assert.equal(await page.locator('.library-card').count(), 7); assert.equal(await page.locator('#browse-content .browse-button').count(), 0);
   await page.locator('.sidebar-icon[data-page=phone]').click(); assert.ok((await page.locator('.phone-panel h3').textContent()).includes(lang === 'pt-BR' ? 'Transmitir do celular' : 'Cast from your phone'));
   await page.keyboard.press('Escape'); assert.equal(await page.locator('#browse-screen').isVisible(), false);
   assert.equal(await page.evaluate(() => document.documentElement.scrollWidth), width); assert.deepEqual(errors, []);
   results.push({ lang, width, height, labels, settingsPositions: positions, touchScroll: width === 390, errors }); await context.close();
  }
  fs.mkdirSync('site/.work/rc4-review', { recursive: true });
  fs.writeFileSync('site/.work/rc4-review/fidelity-results.json', JSON.stringify({ status: 'PASS', cases: results }, null, 2));
  console.log('PASS: 16 settings rows selected visibly through the last row; pointer and touch scrolling; limits/white values and red selected value; native library cards, Music art and shelves, phone modal; EN/PT desktop/mobile; zero JS errors.');
 } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
