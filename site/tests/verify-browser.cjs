const assert = require('node:assert/strict');
const fs = require('node:fs');
const { chromium } = require('./browser-env.cjs');
const root = (process.env.SITE_URL || 'http://127.0.0.1:8279');
const out = 'site/.work';
const report = [];
(async () => {
 const browser = await chromium.launch({headless:true,executablePath:process.env.CHROME_EXECUTABLE});
 try {
 for (const cdn of ['loaded','blocked']) for (const lang of ['en','pt-BR']) for (const [device,viewport] of [['desktop',{width:1366,height:650}],['mobile',{width:390,height:844}]]) {
  const context = await browser.newContext({viewport,locale:lang,hasTouch:device==='mobile',reducedMotion:cdn==='loaded'?'reduce':'no-preference'});
  const page = await context.newPage(); page.setDefaultTimeout(10000); const errors=[]; const external=[];
  if(cdn==='blocked') await page.route('https://cdn.jsdelivr.net/**',r=>r.abort());
  page.on('pageerror',e=>errors.push(e.message)); page.on('request',r=>{if(!r.url().startsWith(root)) external.push(r.url())});
  await page.route('https://api.github.com/**',r=>r.fulfill({status:403,json:{message:'rate limit'}}));
  const response = await page.goto(root + (lang==='en'?'/':'/pt-br/')); assert.equal(response.status(),200);
  await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true'); await page.evaluate(()=>document.fonts.ready);
  assert.equal(await page.getAttribute('html','lang'),lang);
  assert.equal(await page.evaluate(()=>window.gsap?.version || null),cdn==='loaded'?'3.15.0':null);
  assert.equal(await page.evaluate(()=>window.ScrollTrigger?.version || null),cdn==='loaded'?'3.15.0':null);
  assert.equal(await page.locator('[data-reveal], .hero-intro, .tv-frame, .playground-caption').evaluateAll(nodes=>nodes.every(el=>getComputedStyle(el).visibility==='visible'&&Number(getComputedStyle(el).opacity)===1)),true);
  if(cdn==='loaded') assert.equal(await page.evaluate(()=>gsap.isTweening('.hero-intro, .tv-frame, .playground-caption, [data-reveal]')),false);
  const dimensions=await page.evaluate(()=>({width:innerWidth,client:document.documentElement.clientWidth,scroll:document.documentElement.scrollWidth,tv:document.querySelector('#tv').getBoundingClientRect().toJSON(),caption:document.querySelector('.playground-caption').getBoundingClientRect().toJSON(),count:document.querySelector('[data-download-count]').textContent}));
  assert.equal(dimensions.width,viewport.width); assert.equal(dimensions.client,viewport.width); assert.equal(dimensions.scroll,viewport.width);
  if(device==='desktop') assert.ok(dimensions.caption.bottom<=650,JSON.stringify(dimensions));
  assert.ok(Number(dimensions.count)>0); assert.equal(external.filter(url=>!url.startsWith('https://api.github.com/')&&!['https://cdn.jsdelivr.net/npm/gsap@3.15.0/dist/gsap.min.js','https://cdn.jsdelivr.net/npm/gsap@3.15.0/dist/ScrollTrigger.min.js'].includes(url)).length,0);
  if(cdn==='loaded') await page.screenshot({path:`${out}/${lang}-${device}-home.png`});
  if(cdn==='blocked'&&lang==='pt-BR'&&device==='mobile') await page.screenshot({path:`${out}/pt-BR-mobile-cdn-off.png`});
  await page.locator('#tv').focus(); await page.keyboard.press('ArrowLeft');
  await page.waitForFunction(()=>document.querySelector('#app-sidebar').classList.contains('open'));
  assert.equal(await page.getAttribute('#tv','data-zone'),'rail');
  if(cdn==='loaded') await page.screenshot({path:`${out}/${lang}-${device}-sidebar.png`});
  await page.keyboard.press('Escape'); await page.keyboard.press('Enter');
  await page.waitForFunction(()=>document.querySelector('#tv').dataset.state==='player');
  await page.keyboard.press('Enter'); // Pause, so the proof has a fixed position.
  if(cdn==='loaded') await page.screenshot({path:`${out}/${lang}-${device}-player.png`});
  await page.keyboard.press('ArrowDown'); // Transport, then five moves to direct Like.
  for(let i=0;i<5;i++) await page.keyboard.press('ArrowRight');
  assert.equal(await page.locator('[data-player-action=like]').evaluate(el=>el===document.activeElement),true);
  await page.keyboard.press('Enter'); assert.equal(await page.getAttribute('[data-player-action=like]','aria-pressed'),'true');
  assert.equal(await page.locator('#player-panel').isVisible(),false);
  await page.keyboard.press('ArrowRight'); await page.keyboard.press('Enter');
  assert.equal(await page.getAttribute('[data-player-action=dislike]','aria-pressed'),'true'); assert.equal(await page.getAttribute('[data-player-action=like]','aria-pressed'),'false');
  await page.keyboard.press('ArrowRight'); await page.keyboard.press('Enter'); assert.equal(await page.getAttribute('#tv','data-state'),'player-panel');
  await page.keyboard.press('Escape'); assert.equal(await page.getAttribute('#tv','data-state'),'player');
  await page.keyboard.press('ArrowRight'); await page.keyboard.press('Enter'); assert.equal(await page.getAttribute('#tv','data-state'),'player-panel');
  await page.keyboard.press('Escape'); await page.keyboard.press('Escape'); assert.equal(await page.getAttribute('#tv','data-zone'),'timeline');
  const before=await page.inputValue('#player-progress'); await page.keyboard.press('ArrowRight'); assert.equal(Number(await page.inputValue('#player-progress')),Number(before)+10);
  await page.keyboard.press('Escape'); assert.equal(await page.getAttribute('#tv','data-state'),'home');
  await page.keyboard.press('ArrowLeft'); await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowDown'); await page.keyboard.press('Enter');
  assert.equal(await page.getAttribute('#tv','data-state'),'subscriptions'); assert.equal(await page.locator('.channel-avatar').count(),1);
  if(cdn==='loaded') await page.screenshot({path:`${out}/${lang}-${device}-subscriptions.png`});
  await page.keyboard.press('ArrowUp'); await page.keyboard.press('Enter'); assert.equal(await page.getAttribute('#tv','data-state'),'channel');
  await page.keyboard.press('Escape'); await page.keyboard.press('y'); assert.equal(await page.getAttribute('#tv','data-state'),'search');
  await page.locator('.demo-search input').fill('REMO'); await page.keyboard.press('Enter'); assert.equal(await page.locator('.demo-results .video-card').count(),1);
  await page.locator('.demo-results .video-card').click(); assert.equal(await page.getAttribute('#tv','data-state'),'player');
  assert.equal(await page.getAttribute('[data-player-action=dislike]','aria-pressed'),'false');
  await page.locator('[data-player-action=transport]').evaluate(el=>el.click());
  await page.keyboard.press('Escape'); // options -> timeline
  await page.keyboard.press('Escape'); // search
  await page.keyboard.press('Escape'); // home
  assert.equal(await page.getAttribute('#tv','data-state'),'home');
  if(device==='mobile') {
   await page.locator('.touch-controls [data-command=left]').tap(); assert.equal(await page.getAttribute('#tv','data-zone'),'rail');
   await page.locator('.touch-controls [data-command=b]').tap(); await page.locator('.touch-controls [data-command=a]').tap(); assert.equal(await page.getAttribute('#tv','data-state'),'player');
   const sizes=await page.locator('.touch-controls button').evaluateAll(buttons=>buttons.map(b=>({w:b.getBoundingClientRect().width,h:b.getBoundingClientRect().height})));
   assert.ok(sizes.every(({w,h})=>w>=44&&h>=44),JSON.stringify(sizes));
  }
  await page.locator('#feature-search').click();assert.ok((await page.getAttribute('#feature-image','src')).endsWith('/search.jpg'));
  assert.deepEqual(errors,[]); report.push({cdn,lang,device,dimensions,errors,external}); await context.close();
 }
 // Exercise real browser runtime parsing, pagination and the shared EN/PT session cache.
 const context = await browser.newContext({viewport:{width:1366,height:650},locale:'en',reducedMotion:'reduce'}); const page=await context.newPage(); page.setDefaultTimeout(10000); let requests=0;
 const assets=[{name:'app.zip',download_count:10},{name:'app.7z',download_count:20},{name:'app.xex',download_count:30},{name:'app.zip.sha256',download_count:999},{name:'source.tar.gz',download_count:999}];
 await page.route('https://api.github.com/**',async r=>{requests++; const url=r.request().url(); let data;
  if(new URL(url).searchParams.get('page')==='1') data=Array.from({length:100},(_,i)=>({tag_name:`v${i}`,assets:i===0?assets:[]}));
  else if(new URL(url).searchParams.get('page')==='2') data=[{assets:[{name:'APP.ZIP',download_count:7}]}]; else data={stargazers_count:1234};
  await r.fulfill({json:data});
 });
 await page.goto(root); await page.waitForFunction(()=>document.body.dataset.downloads==='67'); assert.equal(requests,3);
 await page.goto(root+'/pt-br/'); await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');
 assert.equal(await page.textContent('[data-download-count]'),'67'); assert.equal(requests,3); assert.equal(await page.textContent('span[data-stars]'),'1.234');
 await context.close();
 // A failed second page must preserve the complete build total, never display a partial sum.
 const failure=await browser.newContext(); const failPage=await failure.newPage();
 await failPage.route('https://api.github.com/**',r=>r.fulfill(new URL(r.request().url()).searchParams.get('page')==='1'?{json:Array.from({length:100},()=>({assets:[{name:'app.zip',download_count:999}]}))}:{status:403,json:{message:'blocked'}}));
 await failPage.goto(root); await failPage.waitForFunction(()=>JSON.parse(sessionStorage.getItem('tube360.github.v2'))?.attempted && JSON.parse(sessionStorage.getItem('tube360.github.v2')).downloads===null);
 assert.equal(await failPage.textContent('[data-download-count]'),String(JSON.parse(fs.readFileSync('site/src/github.json')).download_count));
 await failure.close();
 fs.writeFileSync(`${out}/browser-results.json`,JSON.stringify({status:'PASS',cases:report,runtimePagination:true,sessionLanguageCache:true,partialFailureFallback:true,cdnLoadedAndBlocked:true,reducedMotion:true},null,2));
 console.log('PASS: CDN loaded/blocked × EN/PT × desktop/mobile, reduced motion, Home/sidebar/player/subscriptions; keyboard, mouse, touch, direct rating, panels, search, seek; runtime pagination, session cache, failed-page fallback; zero JS errors.');
 } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1});
