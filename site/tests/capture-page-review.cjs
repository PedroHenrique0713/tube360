const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('./browser-env.cjs');
const root = process.env.SITE_URL || 'http://127.0.0.1:8279';
const output = process.env.REVIEW_OUT || 'site/.work/adendo-1/after';
(async () => {
 const browser = await chromium.launch({executablePath:process.env.CHROME_EXECUTABLE});
 fs.mkdirSync(output,{recursive:true}); const report=[];
 try {
  for (const lang of ['en','pt-BR']) for (const [width,height] of [[1366,650],[1920,1080],[390,844]]) {
   const page=await browser.newPage({viewport:{width,height},locale:lang,reducedMotion:'reduce'});
   const errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.route('https://api.github.com/**',r=>r.fulfill({status:403,json:{}}));
   await page.goto(root+(lang==='en'?'/':'/pt-br/'));
   await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');await page.evaluate(()=>document.fonts.ready);
   const prefix=path.join(output,`${lang}-${width}`);
   await page.screenshot({path:prefix+'-top.png'});
   for (const section of await page.locator('main > section').all()) {
    await section.evaluate(el=>el.scrollIntoView({behavior:'instant',block:'start'}));
    await page.waitForTimeout(50);
   }
   await page.evaluate(()=>{for(const img of document.images)img.loading='eager'});
   await page.waitForFunction(()=>[...document.images].every(img=>!img.getAttribute('src')||(img.complete&&img.naturalWidth>0)));
   for (const id of ['latest-news','features','releases','install','star','credits']) {
    const section=page.locator('#'+id);if(!await section.count())continue;
    await section.evaluate(el=>el.scrollIntoView({behavior:'instant',block:'start'}));
    await page.screenshot({path:prefix+'-'+id+'.png'});
   }
   await page.evaluate(()=>scrollTo({top:0,behavior:'instant'}));
   await page.screenshot({path:prefix+'-full.png',fullPage:true});
   const geometry=await page.evaluate(()=>({width:document.documentElement.scrollWidth,height:document.documentElement.scrollHeight,images:[...document.querySelectorAll('#features img')].map(i=>({src:i.getAttribute('src'),loaded:i.naturalWidth>0}))}));
   assert.equal(geometry.width,width);assert.deepEqual(errors,[]);report.push({lang,width,height,geometry,errors});
   await page.close();console.log(lang,width,'captured');
  }
  fs.writeFileSync(path.join(output,'results.json'),JSON.stringify(report,null,2));
 } finally {await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
