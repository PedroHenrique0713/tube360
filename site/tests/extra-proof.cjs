const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require('./browser-env.cjs');
(async()=>{const b=await chromium.launch({executablePath:process.env.CHROME_EXECUTABLE});const measurements=[];
const settle=page=>page.waitForFunction(()=>!window.gsap?.isTweening('.hero-intro, .tv-frame, .playground-caption, #home-screen, #browse-screen, #player-screen, #player-panel, #feature-image'));
try {
 for(const [lang,width,height] of [['en',1920,1080],['pt-BR',1366,650],['pt-BR',390,844]]) {
  const page=await b.newPage({viewport:{width,height},locale:lang});page.setDefaultTimeout(10000);const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('https://api.github.com/**',r=>r.abort());await page.goto((process.env.SITE_URL || 'http://127.0.0.1:8279')+(lang==='en'?'/':'/pt-br/'));await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');await page.evaluate(()=>document.fonts.ready);assert.equal(await page.evaluate(()=>window.gsap?.version),'3.15.0');await settle(page);
  const resources=await page.evaluate(()=>performance.getEntriesByType('resource').filter(r=>r.name.startsWith(location.origin)).map(r=>({file:new URL(r.name).pathname,bytes:r.encodedBodySize})));
  measurements.push({lang,width,height,resources,totalResourceBytes:resources.reduce((sum,r)=>sum+r.bytes,0),errors});
  if(width===1920) await page.screenshot({path:'site/.work/en-wide-home.png'});
  // Load lazy gallery images by visiting each section, then capture the complete page.
  for(const element of await page.locator('[data-reveal]').all()) {
   await element.evaluate(el=>el.scrollIntoView({behavior:'instant',block:'center'}));
   await page.waitForFunction(el=>getComputedStyle(el).visibility==='visible'&&Number(getComputedStyle(el).opacity)===1,await element.elementHandle());
  }
  if(lang==='pt-BR') {
   await page.locator('#features .section-heading').evaluate(el=>el.scrollIntoView({behavior:'instant',block:'center'}));
   await page.screenshot({path:`site/.work/pt-BR-${width}-scroll-revealed.png`});
  }
  await page.evaluate(()=>{for(const image of document.images) image.loading='eager'});
  await page.waitForFunction(()=>[...document.images].every(image=>image.complete));
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),width);
  if(lang==='pt-BR') await page.screenshot({path:`site/.work/pt-BR-${width}-full.png`,fullPage:true});
  await page.evaluate(()=>scrollTo({top:0,behavior:'instant'}));await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));await page.locator('#tv').focus();await page.keyboard.press('Enter');await settle(page);await page.keyboard.press('ArrowDown');
  for(let i=0;i<5;i++) await page.keyboard.press('ArrowRight');await page.keyboard.press('Enter');
  await page.screenshot({path:`site/.work/${lang}-${width}-direct-like.png`});
  await page.keyboard.press('ArrowRight');await page.keyboard.press('ArrowRight');await page.keyboard.press('Enter');
  await settle(page);await page.screenshot({path:`site/.work/${lang}-${width}-comments.png`});
  assert.equal(await page.getAttribute('#tv','data-state'),'player-panel');assert.deepEqual(errors,[]);await page.close();
 }
 // Sample real frames to prove motion, then exercise print and a preference change during a demo transition.
 const motion=await b.newPage({viewport:{width:1366,height:650},locale:'pt-BR',reducedMotion:'no-preference'});
 motion.setDefaultTimeout(10000);const motionErrors=[];motion.on('pageerror',error=>motionErrors.push(error.message));
 await motion.addInitScript(()=>{
  window.motionFrames=[];
  document.addEventListener('DOMContentLoaded',()=>{
   const original=window.ScrollTrigger.refresh;window.refreshCalls=0;window.ScrollTrigger.refresh=function(...args){window.refreshCalls++;return original.apply(this,args)};
   function sample(){
    const values=['.hero-intro','.tv-frame','.playground-caption'].map(selector=>{const style=getComputedStyle(document.querySelector(selector));return {opacity:Number(style.opacity),y:new DOMMatrixReadOnly(style.transform).m42}});
    window.motionFrames.push(values);
    if(window.motionFrames.length<180&&(!values.every(value=>value.opacity===1&&value.y===0)||window.motionFrames.length<2)) requestAnimationFrame(sample);
   }
   requestAnimationFrame(sample);
  },{once:true});
 });
 await motion.route('https://api.github.com/**',r=>r.abort());
 await motion.goto((process.env.SITE_URL || 'http://127.0.0.1:8279')+'/pt-br/',{waitUntil:'domcontentloaded'});
 await motion.waitForFunction(()=>window.motionFrames?.length>1&&window.motionFrames.at(-1).every(value=>value.opacity===1&&value.y===0));
 const motionResult=await motion.evaluate(()=>({frames:motionFrames.length,minOpacity:[0,1,2].map(index=>Math.min(...motionFrames.map(frame=>frame[index].opacity))),maxY:[0,1].map(index=>Math.max(...motionFrames.map(frame=>frame[index].y))),triggers:ScrollTrigger.getAll().length,reveals:document.querySelectorAll('[data-reveal]').length}));
 assert.ok(motionResult.minOpacity.every(value=>value<1),JSON.stringify(motionResult));assert.ok(motionResult.maxY.every(value=>value>0));assert.equal(motionResult.triggers,motionResult.reveals+1);
 await motion.waitForFunction(()=>window.refreshCalls>0);motionResult.loadRefreshCalls=await motion.evaluate(()=>window.refreshCalls);
 await motion.locator('.signal-flow').evaluate(el=>el.scrollIntoView({behavior:'instant',block:'center'}));
 await motion.waitForFunction(()=>Math.abs(Number(gsap.getProperty('.signal-wire i','x'))-(document.querySelector('.signal-wire').clientWidth-14))<.01);
 motionResult.wire=await motion.evaluate(()=>({x:Number(gsap.getProperty('.signal-wire i','x')),width:document.querySelector('.signal-wire').clientWidth}));
 await motion.locator('.feature-layout').evaluate(el=>el.scrollIntoView({behavior:'instant',block:'center'}));
 await motion.waitForFunction(()=>getComputedStyle(document.querySelector('.feature-layout')).opacity==='1');
 await motion.locator('#feature-search').click();assert.ok((await motion.getAttribute('#feature-image','src')).endsWith('/search.webp'));await settle(motion);
 await motion.evaluate(()=>scrollTo({top:0,behavior:'instant'}));await motion.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 await motion.locator('#tv').focus();await motion.keyboard.press('Enter');
 motionResult.demoTween=await motion.evaluate(()=>gsap.getTweensOf('#player-screen').length);assert.ok(motionResult.demoTween>0);
 await motion.emulateMedia({reducedMotion:'reduce'});await settle(motion);
 const targets='[data-reveal], .hero-intro, .tv-frame, .playground-caption, #home-screen, #browse-screen, #player-screen, #player-panel, #feature-image';
 assert.equal(await motion.locator(targets).evaluateAll(nodes=>nodes.every(el=>getComputedStyle(el).visibility==='visible'&&getComputedStyle(el).opacity==='1')),true);
 await motion.emulateMedia({reducedMotion:'no-preference'});await settle(motion);
 await motion.waitForFunction(()=>[...document.querySelectorAll('[data-reveal]')].some(el=>getComputedStyle(el).opacity==='0'));
 await motion.evaluate(()=>window.dispatchEvent(new Event('beforeprint')));
 assert.equal(await motion.locator(targets).evaluateAll(nodes=>nodes.every(el=>getComputedStyle(el).visibility==='visible'&&getComputedStyle(el).opacity==='1'&&new DOMMatrixReadOnly(getComputedStyle(el).transform).m42===0)),true);
 assert.deepEqual(motionErrors,[]);motionResult.printVisible=true;motionResult.reducedMotionDuringTransition=true;motionResult.errors=motionErrors;
 await motion.close();
 // Core loaded, ScrollTrigger unavailable: the page and demo still work.
 const partial=await b.newPage({viewport:{width:1366,height:650},locale:'en'});partial.setDefaultTimeout(10000);
 await partial.route('https://cdn.jsdelivr.net/**/ScrollTrigger.min.js',r=>r.abort());await partial.route('https://api.github.com/**',r=>r.abort());
 await partial.goto((process.env.SITE_URL || 'http://127.0.0.1:8279')+'/');await partial.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');
 assert.equal(await partial.evaluate(()=>window.gsap?.version),'3.15.0');assert.equal(await partial.evaluate(()=>typeof window.ScrollTrigger),'undefined');
 assert.equal(await partial.locator('[data-reveal], .hero-intro, .tv-frame, .playground-caption').evaluateAll(nodes=>nodes.every(el=>getComputedStyle(el).visibility==='visible'&&getComputedStyle(el).opacity==='1')),true);
 await partial.locator('#tv').focus();await partial.keyboard.press('Enter');await settle(partial);assert.equal(await partial.getAttribute('#tv','data-state'),'player');await partial.close();
 motionResult.pluginUnavailableFallback=true;fs.writeFileSync('site/.work/motion-results.json',JSON.stringify(motionResult,null,2));

 // Blocked storage must not prevent a usable demo or the build fallback.
 const page=await b.newPage({viewport:{width:390,height:844}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{Object.defineProperty(window,'sessionStorage',{get(){throw new Error('blocked')}});Object.defineProperty(window,'localStorage',{get(){throw new Error('blocked')}})});
 await page.route('https://api.github.com/**',r=>r.abort());await page.goto((process.env.SITE_URL || 'http://127.0.0.1:8279')+'/');await page.waitForFunction(()=>document.documentElement.dataset.tube360Ready==='true');await settle(page);await page.locator('#tv').focus();await page.keyboard.press('Enter');await settle(page);assert.equal(await page.getAttribute('#tv','data-state'),'player');assert.deepEqual(errors,[]);await page.close();
 fs.writeFileSync('site/.work/performance.json',JSON.stringify(measurements,null,2));console.log('PASS: 1920×1080, scroll reveals, full-page galleries/installation/footer without horizontal overflow, direct Like/comments, real motion frames, print, reduced-motion switch, partial CDN and blocked storage fallback.');
}finally{await b.close()}})().catch(e=>{console.error(e);process.exitCode=1})
