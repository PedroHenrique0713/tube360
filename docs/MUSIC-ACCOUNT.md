# Música da conta no console (F35)

A Música tenta `WEB_REMIX` com o Bearer já obtido pelo código de TV. Se o servidor
recusar com 400/401/403, tenta **TVHTML5 → `FEtopics_music` em `www.youtube.com`**,
com o mesmo token. Só depois oferece o catálogo público. A tela identifica
“Música da sua conta” ou “Música pública”; o log informa `music=web|tv|public`.
O cliente TV é a seção de música do YouTube na TV: **não há garantia de que a
seleção/ordem seja idêntica ao início do aplicativo YouTube Music**.

Não há cookie de navegador, novo cadastro, senha ou segundo login. A reprodução
continua no player AAC nativo, com preferência pelo áudio original, rádio e fila
local existentes. Deslogado, as rotas públicas permanecem as mesmas.

## O que mudou

- Contexto OAuth mínimo, com nome, **versão atual extraída da página do cliente**,
  idioma, região e `visitorData` quando publicado. O contexto antigo transplantava
  dados de `/sw.js_data` e fixava `WEB_REMIX` em `1.20250219.01.00`.
- Bearer e `X-Goog-Request-Time` em segundos; cabeçalhos `X-Youtube-Client-Name`
  e `X-Youtube-Client-Version` coerentes com o corpo. `X-Goog-Visitor-Id` acompanha
  o visitante do mesmo bootstrap. Origin e host mudam juntos ao passar para TV.
- Bootstrap e pedidos autenticados usam o handle sem pote de cookies (`Cookie:`).
  Não se acrescenta `X-Goog-AuthUser`, seletor de conta de autenticação por cookies.
- Uma resposta HTTP 200 com `loggedOut: true` ou `GFEEDBACK.logged_in=0` também
  é tratada como recusa: conteúdo explicitamente deslogado não ganha o rótulo de conta.
- Cada lista guarda o cliente que emitiu seu cursor. Recusa de uma continuação
  falha com `account_music_unavailable`; não encaminha o cursor privado ao público
  nem a outro cliente. Letras também conservam o cliente nas duas etapas.
- Renovação/logout invalidam contextos e páginas. 429/5xx/erros de rede não
  desclassificam a conta nem provocam tentativas adicionais com outros clientes.
- A Home autenticada oferece atalhos para recentes, playlists e curtidas. Na TV:
  `FEmusic_last_played`, `FEplaylist_aggregation` e `VLLM`, respectivamente. A
  lista de playlists TV é a biblioteca de playlists do YouTube, podendo conter
  playlists que não são de música. Não se substitui histórico musical por
  `FEhistory`. Os atalhos não aparecem no fallback público.

## Pesquisa e limites da conclusão

Fontes consultadas em 2026-11-24:

- [OAuth no ytmusicapi](https://ytmusicapi.readthedocs.io/en/1.12.4/setup/oauth.html):
  cliente Google Cloud do tipo **TVs and Limited Input devices**, com ID/secret
  próprios. Isso descreve uma categoria de aplicação OAuth; não torna um grant
  intercambiável entre identidades InnerTube.
- ytmusicapi `7122aed7ed6331b5597d1b8d337d90a311dceec1`:
  [credenciais OAuth](https://github.com/sigma67/ytmusicapi/blob/7122aed7ed6331b5597d1b8d337d90a311dceec1/ytmusicapi/auth/oauth/credentials.py),
  [escopo](https://github.com/sigma67/ytmusicapi/blob/7122aed7ed6331b5597d1b8d337d90a311dceec1/ytmusicapi/constants.py),
  [contexto/cabeçalhos](https://github.com/sigma67/ytmusicapi/blob/7122aed7ed6331b5597d1b8d337d90a311dceec1/ytmusicapi/helpers.py),
  [Bearer e request-time](https://github.com/sigma67/ytmusicapi/blob/7122aed7ed6331b5597d1b8d337d90a311dceec1/ytmusicapi/ytmusic.py).
  O fluxo pede `https://www.googleapis.com/auth/youtube`, usa o grant legado de
  dispositivo em `oauth2.googleapis.com`, conserva o cliente no refresh e usa
  `WEB_REMIX`, visitante do Music e Origin do Music. O Tube360 já tem um grant
  diferente, do aplicativo de TV, com escopos `http://gdata.youtube.com` e
  `youtube-paid-content`. Acrescentar escopo ao corpo do browse não altera esse
  grant. O cabeçalho `content-encoding: gzip` do helper Python não é reproduzido:
  o C++ envia JSON sem compressão e declara `Content-Type: application/json`.
- [Issue 813](https://github.com/sigma67/ytmusicapi/issues/813) e
  [experimento TV ≥ 7](https://github.com/sigma67/ytmusicapi/issues/813#issuecomment-3359537317):
  o próprio projeto registra a quebra de OAuth no Music web, mesmo com grants
  próprios. O resultado TV usa outro DOM. O [PR iOS 817](https://github.com/sigma67/ytmusicapi/pull/817)
  continua uma tentativa sem resultado reproduzível para todas as contas; por isso
  não se acrescentou uma sequência especulativa de clientes Android/iOS.
- [SmartTube / MediaServiceCore](https://github.com/yuliskov/MediaServiceCore/blob/09a1e13d2aeb7fb5f275c0d1f434cd56775da66a/youtubeapi/src/main/java/com/liskovsoft/youtubeapi/browse/v2/BrowseApiHelper.kt)
  usa `FEtopics_music` e `VLLM` no cliente TV. O guia TV público consultado aqui
  também publicou `FEtopics_music`.

**Observado sem conta:** Bearer deliberadamente inválido no Music devolveu 401;
TV `FEmusic_home`/`FEmusic` devolveram 400; o guia TV e `FEtopics_music` devolveram
200. A página TV publicou `7.20261123.19.00`. A resposta de música TV trouxe
`shelfRenderer`/`tileRenderer`, faixas, artistas e mixes. O parser passou a ler a
linha do artista e a duração do tile. A arte de mixes aceita somente o endpoint
`music.youtube.com/image/mixart`, além dos hosts de imagem já permitidos. As gravações versionadas removem sessão,
tracking e menus e substituem nomes, IDs e imagens por marcadores Demo.

**Não observado:** a causa exata do 401 do grant do dono, a aceitação desse grant
pelo Music TV, a biblioteca particular ou igualdade com o início do YouTube Music.
O 401 de um token inválido comprova somente a recusa de um token inválido. A troca
de identidade e a correção do contexto são caminhos fundamentados para testar no
console, não prova de Premium. A/B, região e políticas do YouTube podem afetá-los.

## Provas no PC (sem rede externa na execução do app)

```sh
cmake -S bench/catalog -B .work/catalog-build
cmake --build .work/catalog-build -j2
ctest --test-dir .work/catalog-build --output-on-failure
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2
python3 bench/catalog/check-oauth-tls.py .work/provas/f35-tls
python3 bench/catalog/check-oauth-ui.py .work/provas/f35-ui
```

O teste TLS usa HTTPS em loopback com a implementação libcurl/mbedTLS de produção,
semeia um cookie anônimo, responde 401 ao WEB_REMIX e 200 à TV, confere
Host/Origin/identidade/Bearer/timestamp, ausência de Cookie/AuthUser e preservação
do pote público. Não compara mais o pedido com a youtubei.js antiga como prova de
aceitação: `oauth-requests.json` continua sendo somente um registro histórico.

O replay usa o formato TV público gravado, com nomes de conta **inventados**, e
um grant explicitamente falso. O transporte de toda conexão HTTPS do app/player
é substituído no link; pedido sem gravação falha fechado. A auditoria exige
Bearer e contexto corretos antes de entregar as respostas de conta. A prova de
áudio oferece original e dublagem marcada como padrão, e exige pedido à mídia
original, AAC reproduzido e zero quadro de vídeo. Prints não provam autorização
real nem desempenho do Xbox.

## Roteiro para o coordenador — somente no console

Não copiar a sessão do console para o notebook. Não testar na pasta principal.
Fechar o app antes de manipular os arquivos. Usar o gerenciador de arquivos do
Aurora/XeXMenu para as cópias **dentro do próprio console**.

1. Criar `Hdd1:\Games\Tube360-F35-test\`. Enviar para ela os binários novos
   `client/build-x360/tube360.xex` e `client/build-x360/tube360-launcher.xex`
   (este último com nome `default.xex`), e as quatro fontes `client/assets/inter-*.ttf`.
2. Copiar exatamente **`Hdd1:\Games\YouTube 360\yt-console-account.json` →
   `Hdd1:\Games\Tube360-F35-test\yt-console-account.json`**, usando o gerenciador
   do console. Se a instalação principal tiver outro nome, ajustar somente esse
   prefixo. Não abrir, fotografar ou exportar o conteúdo. Não copiar `server.txt`,
   `nest.txt`, logs, cookies, chaves de atualização ou identidades de Cast.
3. Criar `settings.txt` na pasta temporária com:

   ```text
   lang=pt
   cols=4
   quality=720
   server=off
   sound=tv
   audio_preference=original
   ```

4. Abrir `Tube360-F35-test\default.xex`. A conta deve estar conectada sem novo
   código. Abrir Música. Procurar “Música da sua conta”; no log local, procurar
   `catalogo console: /api/nav, ... music=tv` (ou `music=web` se aceito). Se aparecer
   `music=public`, anotar somente a classe/status de `conta recusada`, sem token.
5. Comparar com a conta no celular: presença de mixes pessoais, recentes,
   playlists e músicas curtidas. Confirmar que os itens são particulares, não só
   títulos populares. Registrar divergências de seleção/ordem entre Music e TV.
   Abrir cada atalho, um mix e sua continuação; B deve retornar à mesma prateleira.
6. Escolher uma faixa de uma prateleira e pressionar A, depois A em Reproduzir
   tudo no rádio. Confirmar áudio original, avanço da fila, B para parar/voltar e
   ausência de PC. Medir início e eventuais pausas; o log deve mostrar
   `musica: primeiro audio` e `video 0 quadros`.
7. Testar 4:3/texto grande e pt/en/es. A captura de PC cobre disposição; falta
   confirmar legibilidade física no televisor e estabilidade do Xbox.
8. Fechar normalmente. **Não usar Sair da conta na cópia de teste**: revogaria o
   grant compartilhado com a instalação principal. Se o app renovou a sessão,
   conservar a versão atualizada por cópia de volta ao arquivo principal, também
   dentro do console e com ambos os apps fechados. Apagar **toda** a pasta
   `Tube360-F35-test`, inclusive `yt-console-account.json` e eventual `.tmp`.
   Reabrir a instalação principal e confirmar que continua conectada.

Se recentes/playlists/curtidas retornarem `account_music_unavailable`, não tratar
como biblioteca vazia ou Premium confirmado. A validação está pendente e precisa
ser investigada no console; não pedir cookies nem refazer o login do dono como
substituto para esse diagnóstico.
