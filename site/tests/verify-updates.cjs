const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require('./browser-env.cjs');
const source=fs.readFileSync('CHANGELOG.md','utf8').split('\n');
const expected=[...source.join('\n').matchAll(/^## \[(\d+\.\d+\.\d+)\](?:\s*[—-]\s*(\d{4}-\d{2}-\d{2}))?\s*$/gm)].slice(0,3).map(m=>({version:m[1],date:m[2]||null}));
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROME_EXECUTABLE});const results=[];
try{
 for(const lang of ['en','pt-BR'])for(const [width,height]of [[1366,650],[1920,1080],[390,844]]){
  const page=await browser.newPage({viewport:{width,height},locale:lang,reducedMotion:'reduce'});const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('https://api.github.com/**',r=>r.fulfill({status:403,json:{}}));await page.goto((process.env.SITE_URL||'http://127.0.0.1:8279')+(lang==='en'?'/':'/pt-br/'));await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');
  assert.equal(await page.locator('#latest-news li').count(),3);
  const entries=await page.locator('.release-entry').evaluateAll(nodes=>nodes.map(n=>({version:n.dataset.version,date:n.querySelector('time')?.getAttribute('datetime')||null,link:n.querySelector('a').href,items:[...n.querySelectorAll('li')].map(li=>({text:li.textContent,line:Number(li.dataset.changelogLine)}))})));
  assert.deepEqual(entries.map(({version,date})=>({version,date})),expected);
  for(const entry of entries){assert.ok(entry.items.length>=3&&entry.items.length<=6);for(const item of entry.items){assert.ok(source[item.line-1].startsWith('- '));assert.ok(item.text.length>20&&!/[<>]/.test(item.text))}assert.equal(entry.link,`https://github.com/PedroHenrique0713/tube360/releases${entry.date?'/tag/v'+entry.version:''}`)}
  await page.locator('#latest-news a').click();assert.equal(new URL(page.url()).hash,'#releases');assert.ok(await page.locator('#releases-title').isVisible());
  const playback=await page.locator('.console-node p').textContent();assert.ok(playback.includes('H.264')&&!playback.includes('{'));
  for(const [i,name]of ['library','search','phone','music'].entries()){
   const tab=page.locator('.feature-option').nth(i);await tab.click();assert.equal(await tab.getAttribute('aria-selected'),'true');assert.equal(await page.locator('#feature-image').getAttribute('src'),`/assets/rc4/${lang}/${name}.webp`);await page.waitForFunction(()=>document.querySelector('#feature-image').complete&&document.querySelector('#feature-image').naturalWidth===1280);
  }
  await page.locator('.feature-option').first().focus();await page.keyboard.press('ArrowRight');assert.equal(await page.locator('.feature-option').nth(1).getAttribute('aria-selected'),'true');
  await page.locator('#credits .footer-links a').first().focus();await page.keyboard.press('Tab');assert.notEqual(await page.evaluate(()=>getComputedStyle(document.activeElement).outlineStyle),'none');
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),width);assert.deepEqual(errors,[]);results.push({lang,width,height,versions:entries.map(e=>({version:e.version,date:e.date,items:e.items.length})),errors});await page.close();
 }
 fs.mkdirSync('site/.work/adendo-1',{recursive:true});fs.writeFileSync('site/.work/adendo-1/updates-results.json',JSON.stringify({status:'PASS',matrix:results},null,2));console.log('PASS: CHANGELOG versions/dates and 3–6 sourced summaries; pending/public release links, teaser anchor, native localized images and keyboard tabs, footer focus, console copy; EN/PT × 3 sizes, no overflow or JS errors.');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
