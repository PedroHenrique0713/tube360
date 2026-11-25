const fs = require('node:fs');
let playwright;
if (process.env.PLAYWRIGHT_MODULE) playwright = require(process.env.PLAYWRIGHT_MODULE);
else {
  try { playwright = require('playwright'); }
  catch { throw new Error('Install Playwright outside site/dist, or set PLAYWRIGHT_MODULE to an existing installation.'); }
}
fs.mkdirSync('site/.work', { recursive: true });
module.exports = playwright;
