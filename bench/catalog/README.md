# Catálogo e Música da conta

Execute da raiz do worktree:

```sh
cmake -S bench/catalog -B .work/catalog-build
cmake --build .work/catalog-build -j2
ctest --test-dir .work/catalog-build --output-on-failure
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2
python3 bench/catalog/check-oauth-tls.py .work/provas/f35-tls
python3 bench/catalog/check-oauth-ui.py .work/provas/f35-ui
```

O CTest cobre catálogo público, contrato OAuth/TV da Música, conteúdo de membros e
restauração/renovação/logout da conta. São transportes fechados, com tokens falsos.

O teste OAuth exige o contexto publicado pelo Music e `X-Goog-Request-Time`,
reproduz 400/401/403 do WEB_REMIX, exige a alternativa TVHTML5/`FEtopics_music`,
exercita recentes/playlists/curtidas/rádio e confere o isolamento de cursores.
429/5xx não provocam reserva pública. HTTP 200 explicitamente deslogado também
não pode ser apresentado como Música da conta. O DOM TV é uma captura pública
reduzida e sanitizada; as respostas autenticadas são simulações declaradas.
[Proveniência](fixtures/TV-MUSIC.md).

O teste TLS usa libcurl/mbedTLS de produção em HTTPS de loopback, com uma raiz
local de teste. Semeia um cookie público, exige sua ausência nos bootstraps e
POSTs OAuth e confirma a preservação do pote público. Confere Host, Origin,
identidade numérica, versão, Bearer e timestamp na recuperação WEB → TV. O
transporte continua usando um handle separado sem motor de cookies para pedidos
com `Cookie:`; só apagar o cabeçalho não impedia o curl-xenon de anexar o pote.

O teste visual desabilita Cast, usa HOME/cache/assets isolados e substitui no link
todo HTTPS do app e do player. Pedidos sem fixture falham fechado. A auditoria
exige contexto/Bearer antes de servir uma resposta de conta. Há casos em pt/en/es,
4:3/texto grande, recentes, playlists, curtidas, mixes, reserva pública, B e áudio
original, mesmo quando a dublagem está marcada como padrão. Abra os PNGs gerados:
não são prova de autorização real, Premium nem desempenho do Xbox.

Pesquisa, mudanças e roteiro para teste com o login que já está no console:
[docs/MUSIC-ACCOUNT.md](../../docs/MUSIC-ACCOUNT.md).

## Bancadas anteriores

`capture-oauth.mjs --check` verifica o registro histórico da youtubei.js 16.0.1
em `fixtures/oauth-requests.json`, com sessão e tokens falsos. Requer essa versão
em `cast/node_modules`. Ele documenta a F16; não é mais o contrato de cabeçalhos
ou contexto da implementação F35, e nunca provou aceitação de conta real.
`tv-session.txt` preserva somente índices de uma sessão pública sanitizada de
`/sw.js_data`, usada pelo oráculo antigo.

`prepare-replay.py`, `capture-ui.py`, `membership-ui.py` e `visual-integration.py`
continuam disponíveis para as provas públicas, membros e integração visual de
canal/inscrições/biblioteca/player. Conta e mídia dessas bancadas são fictícias.
