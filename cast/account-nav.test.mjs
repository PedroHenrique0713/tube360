import assert from 'node:assert/strict';
import test from 'node:test';
import { Account } from './account.mjs';

function signedIn(execute) {
  const account = new Account();
  account.state = 'signed_in';
  account.current = { yt: { actions: { execute } } };
  account.guest = () => assert.fail('Unexpected guest session');
  return account;
}

const song = { musicTwoRowItemRenderer: { navigationEndpoint: { watchEndpoint: { videoId: '00000000001' } } } };
const mix = { musicTwoRowItemRenderer: { navigationEndpoint: { watchPlaylistEndpoint: { playlistId: 'RDstation' } } } };
const shelf = (title, item) => ({ musicCarouselShelfRenderer: {
  header: { musicCarouselShelfBasicHeaderRenderer: { title } }, contents: [item],
} });

for (const status of [200, 400, 401, 403, 503]) {
  test('Music usa o host Music e preserva prateleiras; resposta ' + status, async () => {
    const clients = [];
    const account = signedIn(async (route, args) => {
      assert.equal(String(route), 'https://music.youtube.com/youtubei/v1/browse');
      assert.equal(args.browseId, 'FEmusic_home');
      clients.push(args.client);
      if (status !== 200) throw new Error('Request failed with status code ' + status);
      return { data: { contents: [shelf('Mixes', mix), shelf('Ouvidos recentemente', song)] } };
    });
    let guests = 0;
    account.guest = async () => ({ actions: { execute: async (route, args) => {
      guests++;
      assert.equal(String(route), 'https://music.youtube.com/youtubei/v1/browse');
      assert.equal(args.client, 'YTMUSIC');
      return { data: { contents: [shelf('Mixes públicos', mix), shelf('Escolhas públicas', song)] } };
    } } });
    const page = await account.navigationPage('music', '', '', 'videos', 0);
    assert.deepEqual(clients, ['YTMUSIC']);
    assert.equal(guests, status === 200 ? 0 : 1);
    assert.deepEqual(page.items.map(i => [i.id, i.section]), status === 200 ?
      [['RDstation', 'Mixes'], ['00000000001', 'Ouvidos recentemente']] :
      [['RDstation', 'Mixes públicos'], ['00000000001', 'Escolhas públicas']]);
    assert.equal(account.state, 'signed_in');
  });
}

test('Recusa na continuação reinicia o Music público sem reutilizar o cursor da conta', async () => {
  const account = signedIn(async (_, args) => {
    if (args.continuation) throw new Error('Request failed with status code 400');
    return { data: { contents: [shelf('Conta', song)], continuation: { nextContinuationData: { continuation: 'private-cursor' } } } };
  });
  const publicRequests = [];
  account.guest = async () => ({ actions: { execute: async (_, args) => {
    publicRequests.push(args);
    assert.notEqual(args.continuation, 'private-cursor');
    return { data: args.continuation ? { contents: [shelf('Página pública 2', mix)] } : {
      contents: [shelf('Página pública 1', song)], continuation: { nextContinuationData: { continuation: 'public-cursor' } } } };
  } } });
  await account.navigationPage('music', '', '', 'videos', 0);
  const page = await account.navigationPage('music', '', '', 'videos', 1);
  assert.equal(page.items[0].section, 'Página pública 2');
  assert.equal(publicRequests.length, 2);
});

test('Music recusado não troca a sessão da biblioteca pela sessão pública', async () => {
  const account = signedIn(async (route) => {
    if (route instanceof URL) return { data: { error: { code: 400 } } };
    assert.equal(route, '/browse');
    return { data: { contents: [] } };
  });
  account.guest = async () => ({ actions: { execute: async (route) => {
    assert.ok(route instanceof URL);
    return { data: { contents: [shelf('Público', song)] } };
  } } });
  await account.navigationPage('music', '', '', 'videos', 0);
  await account.navigationPage('later', '', '', 'videos', 0);
  assert.equal(account.state, 'signed_in');
});

const video = id => ({ playlistVideoRenderer: { videoId: id, lengthText: { simpleText: '3:00' } } });

test('Capa na ordem da playlist usa o último vídeo da continuação', async () => {
  const account = signedIn(async (_, args) => ({ data: args.continuation ?
    { contents: [video('00000000002')] } :
    { contents: [video('00000000001'), { nextContinuationData: { continuation: 'next' } }] } }));
  assert.deepEqual(await account.playlistPreview('PLtest'), { video: '00000000002' });
});

test('Capa usa a ordenação por inclusão quando a playlist a oferece', async () => {
  const sort = { title: 'Date added (newest)', navigationEndpoint: { browseEndpoint: { params: 'recent' } } };
  const account = signedIn(async (_, args) => {
    assert.equal(args.browseId, 'VLPLtest');
    return { data: args.params === 'recent' ? { contents: [video('00000000007'), video('00000000002')] } :
      { contents: [{ sortFilterSubMenuRenderer: { subMenuItems: [sort] } }] } };
  });
  assert.deepEqual(await account.playlistPreview('PLtest'), { video: '00000000007' });
});
