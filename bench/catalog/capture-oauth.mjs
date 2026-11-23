// Oráculo offline: executa a biblioteca real com sessão sanitizada e OAuth falso.
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { Innertube, ClientType } from '../../cast/node_modules/youtubei.js/dist/src/platform/node.js';

process.env.TZ = 'UTC';
assert.equal(JSON.parse(await readFile(new URL('../../cast/node_modules/youtubei.js/package.json', import.meta.url))).version, '16.0.1');
const fixtures = new URL('./fixtures/', import.meta.url);
const agent = 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36';
const session = await readFile(new URL('tv-session.txt', fixtures), 'utf8');
const requests = [], bootstrap = [];
const fetcher = async (input, init = {}) => {
  const url = String(input.url || input);
  if (url === 'https://www.youtube.com/sw.js_data' || url === 'https://www.youtube.com/youtubei/v1/config?prettyPrint=false') {
    const headers = Object.fromEntries(new Headers(init.headers));
    if (headers.cookie) headers.cookie = headers.cookie.replace(/VISITOR_INFO1_LIVE=[^;]+/, 'VISITOR_INFO1_LIVE=fixture0123');
    bootstrap.push({ url, method: init.method || 'GET', headers, ...(init.body ? { body: JSON.parse(init.body) } : {}) });
    return url.endsWith('/sw.js_data') ? new Response(session) : Response.json({});
  }
  assert.match(url, /^https:\/\/www\.youtube\.com\/youtubei\/v1\/(browse|search|next)\?prettyPrint=false&alt=json$/);
  requests.push({ url, method: input.method, headers: Object.fromEntries(new Headers(init.headers)), body: JSON.parse(init.body) });
  return Response.json({ contents: {} });
};
const yt = await Innertube.create({ client_type: ClientType.TV, lang: 'pt', location: 'BR',
  retrieve_player: false, enable_session_cache: false, user_agent: agent, timezone: 'UTC', fetch: fetcher });
await yt.session.signIn({ access_token: 'fixture-token', refresh_token: 'fixture-refresh',
  expiry_date: '2099-01-01T00:00:00.000Z' });
for (const [route, args] of [
  ['/browse', { browseId: 'FEmusic_home' }],
  ['/browse', { continuation: 'CONT_music_home' }],
  ['/browse', { browseId: 'FEmusic_explore' }],
  ['/browse', { browseId: 'FEmusic_charts', params: 'sgYPRkVtdXNpY19leHBsb3Jl' }],
  ['/search', { query: 'jazz instrumental' }],
  ['/next', { videoId: 'F12audio001', playlistId: 'RDAMVMF12audio001' }],
  ['/next', { videoId: 'F12audio001' }],
  ['/browse', { browseId: 'MPLYfixture' }],
]) await yt.actions.execute(route, { ...args, client: 'YTMUSIC' });
assert.equal(requests.length, 8);
assert.equal(bootstrap.length, 2);
for (const r of requests) {
  assert.equal(r.headers.authorization, 'Bearer fixture-token');
  assert.equal(r.headers.cookie, undefined);
  assert.equal(r.headers['x-goog-authuser'], undefined);
  assert.equal(r.body.context.client.clientName, 'WEB_REMIX');
  assert.equal(r.body.isAudioOnly, true);
}
const output = new URL('oauth-requests.json', fixtures);
const captured = JSON.stringify({ library: 'youtubei.js 16.0.1', bootstrap, requests }, null, 2) + '\n';
if (process.argv.includes('--check')) assert.equal(await readFile(output, 'utf8'), captured);
else await writeFile(output, captured);
console.log(`${requests.length} OAuth requests captured offline: ${fileURLToPath(output)}`);
