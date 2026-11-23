# Catálogo e OAuth do Music

Execute da raiz do worktree:

```sh
node bench/catalog/capture-oauth.mjs --check
cmake -S bench/catalog -B .work/catalog-build
cmake --build .work/catalog-build -j2
ctest --test-dir .work/catalog-build --output-on-failure
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j4
python3 bench/catalog/check-oauth-tls.py .work/provas/oauth-tls
python3 bench/catalog/check-oauth-ui.py .work/provas/oauth-ui
```

`capture-oauth.mjs` usa a **youtubei.js 16.0.1 instalada em `cast/node_modules`**,
uma sessão TV e tokens explicitamente falsos. O fetch responde exclusivamente
com fixtures; nenhum pedido sai para a internet, nem no login. Sem `--check`,
o script regenera `fixtures/oauth-requests.json`. O agente e o fuso são fixados
para tornar a comparação determinística: Chrome 150 e UTC. A biblioteca normalmente
escolhe um agente de desktop aleatório e o fuso do processo.

`tv-session.txt` deriva de uma única resposta pública de `/sw.js_data` capturada
em 2026-11-23, sem login. Só preserva os índices necessários do vetor de dispositivo;
IP é TEST-NET, visitante, chave, rollout, experimento e app-install são marcadores
falsos. Não contém dados de conta. A captura inclui também os dois pedidos de
bootstrap da biblioteca; o cookie aleatório de visitante foi substituído por
`fixture0123` nessa gravação.

O teste C++ compara o JSON completo e todos os cabeçalhos de oito pedidos reais da
biblioteca: Home, continuação, Explore, Charts, busca, rádio e as duas etapas da
letra. Usa outro token falso e exige que essa seja a única diferença de conteúdo
entre os pedidos. Ordem de chaves JSON e capitalização de cabeçalhos não importam.
Também cobre recusa 401/403, renovação, logout, erros transitórios, sessão inválida
e independência da recusa pelo cliente WEB.

O teste TLS compila `https.cpp` de produção com a raiz de um servidor de loopback,
liga o mesmo libcurl/mbedTLS do app e captura o HTTP real. Semeia um cookie anônimo,
exige que ele não apareça no bootstrap nem no POST OAuth e confirma que continua
disponível na navegação pública. O cabeçalho `Cookie:` vazio normalmente é uma
instrução de supressão ([contrato de CURLOPT_HTTPHEADER](https://curl.se/libcurl/c/CURLOPT_HTTPHEADER.html)),
mas o curl-xenon desta revisão continua anexando o pote. Por isso `https.cpp`
seleciona um segundo handle, sem motor de cookies, quando o pedido especifica
`Cookie:` (vazio ou com o PREF do bootstrap). Ele reutiliza suas próprias conexões
e não armazena Set-Cookie. A prova falhou com o transporte anterior, acusando
vazamento de `YSC` no bootstrap e no POST, e exige que o pote público continue
intacto. `Cookie:` vazio não aparece no tráfego. O teste usa porta efêmera e limpa
o servidor em `finally`.

O teste visual roda com `server=off`, HOME e assets próprios, transportes de conta
e catálogo gravados e Cast desabilitado. Produz `account.png`, `guest.png` e
`charts-account.png`; **abra cada imagem**. Os títulos “Faixa da conta” e “Conta
Premium fictícia” são sintéticos para distinguir a origem da resposta, não prova
de acesso ao Premium real. A auditoria exige pedidos OAuth aceitos nos casos com
conta. Os testes não acessam a conta do dono nem o Xbox.

## Diferenças medidas

| Campo | Catálogo anterior com token | youtubei.js TV + `client: 'YTMUSIC'` |
|---|---|---|
| Host/Origin | `music.youtube.com` | `www.youtube.com` |
| Query | `prettyPrint=false` | `prettyPrint=false&alt=json` |
| Cliente | `WEB_REMIX` / `67` | `WEB_REMIX` / `67` (continua assim, não TVHTML5) |
| Versão | Extraída da página Music | `1.20250219.01.00` |
| Corpo | Contexto HTML anônimo do Music | Contexto de sessão de `/sw.js_data` + `isAudioOnly: true` |
| Visitante | Da página Music | Da sessão base do YouTube; enviado no contexto e em `X-Goog-Visitor-Id` |
| Cookie no POST | Pote anônimo do libcurl | Ausente |
| `X-Goog-AuthUser` | Ausente | Ausente com OAuth; a biblioteca só o acrescenta ao autenticar com SAPISID |
| Autorização | Bearer | Bearer |

O console agora usa esse contrato para pedidos Music com conta. A consulta
`/config` da biblioteca não é necessária aqui: ela só acrescenta `configInfo`,
que o ajuste para YTMUSIC remove antes de enviar o pedido. A reserva pública
reconstrói seu próprio pedido, contexto e cookies em caso de 401/403.

Essas provas demonstram paridade do pedido, usando a mesma resposta de sessão.
Aceitação do token real, recomendações pessoais e vantagens Premium precisam
ser confirmadas na integração pelo coordenador/dono no console.
