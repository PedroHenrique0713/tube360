// Utilitário da mesma conta usada pelo receptor. Sem DIAL nem código de Cast.
// node cast/conta.mjs entrar | testar (somente leitura)
import { Account } from './account.mjs';

const mode = process.argv[2] || 'testar';
if (!['entrar', 'testar'].includes(mode)) {
  console.error('Uso: node cast/conta.mjs entrar | testar');
  process.exit(1);
}
const account = new Account();
await account.restore();
if (mode === 'entrar' && account.state === 'signed_out') {
  const status = await account.login();
  if (status.state === 'pending') {
    console.log('CÓDIGO', status.code, 'em', status.url);
    while (account.state === 'pending') await new Promise((resolve) => setTimeout(resolve, 1000));
  }
}
if (account.state !== 'signed_in') {
  console.error('Conta desconectada; use o modo entrar.');
  account.reset();
  process.exit(1);
}
console.log('Conta conectada:', account.name || '(nome indisponível)');
if (mode === 'testar') {
  for (const kind of ['home', 'subs', 'history', 'later']) {
    try {
      const feed = await account.feed(kind, 0);
      console.log(kind, feed.items.length, 'vídeos;', feed.more ? 'há próxima página' : 'fim da lista');
    } catch { console.error(kind, 'indisponível'); process.exitCode = 1; }
  }
}
account.reset();
