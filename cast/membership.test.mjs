import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import { membership } from './membership.mjs';
import { normalizeVideo, parseTVFeed, parseNavigation } from './account.mjs';
const fixture = name => JSON.parse(fs.readFileSync(new URL('../bench/catalog/fixtures/' + name + '.json', import.meta.url)));

for (const lang of ['pt', 'en']) test('Resposta pública real de membros: ' + lang, () => {
  const data = fixture('members-' + lang);
  assert.equal(parseNavigation(data).items[0].membership, 'members');
  assert.equal(parseTVFeed(data).items[0].membership, 'members');
});
for (const c of fixture('members-cases').cases) test(c.name, () => {
  assert.equal(membership(c.node), c.expected);
  const data = { contents: [{ [c.renderer]: c.node }] };
  assert.equal(parseNavigation(data, true).items[0].membership, c.expected);
  if (!c.renderer.startsWith('music')) assert.equal(normalizeVideo(c.node, c.renderer).membership, c.expected);
});
