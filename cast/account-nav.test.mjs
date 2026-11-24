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

for (const tvOnly of [false, true]) {
  test('Music preserva a sessão e as prateleiras, token ' + (tvOnly ? 'restrito à TV' : 'aceito'), async () => {
    const clients = [];
    const account = signedIn(async (route, args) => {
      assert.equal(route, '/browse');
      assert.equal(args.browseId, 'FEmusic_home');
      clients.push(args.client);
      if (tvOnly && args.client === 'YTMUSIC') throw new Error('Request failed with status code 403');
      return { data: { contents: [shelf('Mixes', mix), shelf('Ouvidos recentemente', song)] } };
    });
    const page = await account.navigationPage('music', '', '', 'videos', 0);
    assert.deepEqual(clients, tvOnly ? ['YTMUSIC', 'TV'] : ['YTMUSIC']);
    assert.deepEqual(page.items.map(i => [i.id, i.section]), [['RDstation', 'Mixes'], ['00000000001', 'Ouvidos recentemente']]);
  });
}

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
