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

## Contrato após a integração visual/Música

O helper `musicEndpoint` em `cast/account.mjs` fornece uma URL absoluta em
`music.youtube.com` à `actions.execute`. O `HTTPClient` da 16.0.1 preserva assim
o ajuste YTMUSIC, o contexto da sessão TV e o Bearer, usando o Origin do Music.
`music.getHomeFeed()` por si só usa o host padrão `www.youtube.com`, que devolveu
400 na produção. O catálogo C++ e a captura offline agora usam o host corrigido.
400/401/403 reservam a navegação pública sem encerrar a conta do YouTube.
No Node, a reserva reinicia a paginação para não enviar o cursor privado à sessão
pública. Os testes TLS conferem Host, Origin e ausência de cookies no fio.

`visual-integration.py <pasta>` produz canal, inscrições, biblioteca, Música,
player aprovado, qualidade e áudio em pt/en e 1280×720/1920×1080. Use imagens
públicas opcionais em `<pasta>/art/{avatar.png,banner.jpg,thumb.jpg}` para conferir
o recorte. A conta e a mídia de reprodução são fictícias. `visual-results.json`
registra dimensões físicas e o consumo de GPU das miniaturas.

## Diferenças medidas antes da correção de host

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

O console usa esse corpo e esses cabeçalhos com o host/Origin corrigidos acima. A consulta
`/config` da biblioteca não é necessária aqui: ela só acrescenta `configInfo`,
que o ajuste para YTMUSIC remove antes de enviar o pedido. A reserva pública
reconstrói seu próprio pedido, contexto e cookies em caso de 401/403.

Essas provas demonstram paridade do pedido, usando a mesma resposta de sessão.
Aceitação do token real, recomendações pessoais e vantagens Premium precisam
ser confirmadas na integração pelo coordenador/dono no console.
