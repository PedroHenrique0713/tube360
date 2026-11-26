# F53 — membros e diagnóstico do Nest

Entrega parcial na branch `f53-nest-membros`: #11 implementada; #12 diagnosticada e bloqueada pela lista de arquivos autorizados. Nenhum dispositivo real foi acessado. Não houve push.

## O que mudou e por quê

- `client/src/player_h264.hpp`: um HTTP 403 em vídeo, áudio ou HLS com `memberVideo` ativo produz `members_unsupported`, limpa `renew`/`retryConverted` e encerra com `FAILED` antes da renovação. Evita repetir o pedido autenticado e os links recusados. Vídeos comuns continuam solicitando renovação; falhas de membros que não sejam 403 seguem o tratamento anterior. O desafio `n` não foi implementado.
- `client/src/i18n.hpp`: chave `MembersXboxUnavailable` no bloco F53 ao fim da lista X, EN/PT/ES, e mapeamento no `playerError` compartilhado pelas telas. Texto PT: “Vídeos de membros ainda não tocam no Xbox.”
- `client/src/i18n_ar.hpp`: tradução árabe da mesma chave, no bloco F53.
- `tests/cast/members.cpp`, `tests/cast/members_test.py`, `tests/cast/test.cmake`: teste com Player, ConsoleAccount TVHTML5, Client::player, RangeFile/LiveFile e demuxer reais. Somente HTTP é substituído; conta e MP4 são temporários. Cobre 403 no vídeo, áudio e HLS de membros, 403 de vídeo/áudio comum, 404 de membros e tradução em quatro idiomas. Após o erro de membros chama `take()` 100 vezes e exige ausência de novos pedidos.
- Este relatório: `tests/cast/REPORT_F53.md`.

`player.hpp`, `yt/client.cpp` e `main.cpp` não precisaram de alterações: o sinal de membership e a resposta autenticada já existiam; o erro traduzido chega às duas apresentações por `i18n::playerError`.

## Testes rodados e saída real

Descoberta dos testes existentes: `rg -l` pelos nomes `player_h264.hpp`, `player.hpp`, `i18n.hpp` e `i18n_ar.hpp` em `tests`, `bench` e `client/CMakeLists.txt`. Foram executados os testes de código encontrados; README/relatórios não são executáveis.

Build PC concluída (exit 0), com bibliotecas FFmpeg instaladas explicitamente, devido à divergência de build documentada abaixo:

```sh
PATH="$HOME/.local/bin:$PATH" cmake -S client -B client/build -DFFMPEG_PREFIX="$PWD/.work/ffmpeg-pc"
cmake --build client/build -j6
cmake --build client/build -j6 --target member-playback-test player-state-test arabic-text-test arabic-render-test mp4-opening-test nest-test
```

```text
[100%] Built target tube360-cast
Built target tube360-main
Linking CXX executable tube360
Built target tube360
```

Teste novo: `python3 tests/cast/members_test.py` (exit 0):

```text
PASS member video HTTP=403 players=1 video=1 audio=0 playlists=0 renew=0 fallback=0
PASS member audio HTTP=403 players=1 video=1 audio=1 playlists=0 renew=0 fallback=0
PASS member HLS HTTP=403 players=1 video=0 audio=0 playlists=1 renew=0 fallback=0
PASS ordinary video HTTP=403 players=0 video=1 audio=0 playlists=0 renew=1 fallback=0
PASS ordinary audio HTTP=403 players=0 video=1 audio=1 playlists=0 renew=1 fallback=0
PASS member video HTTP=404 players=1 video=1 audio=0 playlists=0 renew=0 fallback=1
PASS member message EN/PT/ES/AR
```

Trecho do mesmo log que comprova o defeito original de abertura e o encerramento:

```text
console: player OK, cliente TVHTML5, 1 pedidos:  resolucao_ms=3
[mov,mp4,m4a,3gp,3g2,mj2 @ 0x3e775fc0] moov atom not found
cabecalho: video resultado=-1094995529 tamanho=-1 bytes=0 pedidos=1 header_ms=0 seek_ms=0 HTTP=403 erro=
console: MP4 90p, itags 136/140, início 0.000 abertura_ms=3
qualidade: selecao auto fixa=0 fonte=90p itag=136 disponiveis=1
audio: selecionada und-dub, 1 faixas
console: video de membros HTTP=403; encerrando sem renovar URL
```

Calibração: recompilado o teste com `player_h264.hpp` de HEAD anterior à mudança e `player.hpp` copiado para um diretório de build privado. A suíte rejeitou a primeira falha de membros (exit 1 esperado), provando que não passa com o ciclo original:

```text
FAIL member 403 is terminal and specific
PASS calibration: original player_h264.hpp rejected at first member 403
```

Testes existentes:

- `client/build/player-state-test` (exit 0):

```text
PASS paused Cast preserves frame/clock; disabled or unavailable server never starts fallback
PASS picture advances while Cast preload or PLAY confirmation is pending
```

- `client/build/arabic-text-test` (exit 0):

```text
100000 prepared cache hits: 44643 us; additional coverage checks: 0
423 checks passed; 100000 cached layouts: 28138 us
```

- `SDL_VIDEO_DRIVER=offscreen LP_NUM_THREADS=2 client/build/arabic-render-test "$PWD/client/assets"` (exit 0):

```text
tela 1280x720
Arabic fonts: 1432 glyphs, atlas 2048x4096 = 8388608 bytes GPU; temporary pixels 8388608 + TTF 616628 bytes
Arabic fonts: 1432 glyphs, atlas 2048x4096 = 8388608 bytes GPU; temporary pixels 8388608 + TTF 616628 bytes
5502 renderer checks passed
```

- `python3 bench/opening/fixtures.py client/build/f53-opening` e `client/build/mp4-opening-test "$PWD/client/build/f53-opening"` (exit 0):

```text
PASS long25 length=2831220803 requests=3 bytes=1572864 seek=120,10000 decoded=1
PASS hdr60 length=2831220808 requests=3 bytes=1572864 seek=120,10000 decoded=1
PASS opening failure -> lower source; next video -> original source, no performance penalty
```

- `bench/catalog/membership-test.cpp` e `bench/console-decode/quality.cpp`: compilados em `client/build/f53-native` usando os includes e as bibliotecas gerados pelo CMake para `member-playback-test`. Executados `catalog-membership-test bench/catalog/fixtures` e `decode-quality sample.mpg`; o MPEG foi gerado localmente por ffmpeg (2 s, 320×240, 30 fps). Ambos exit 0:

```text
74 membership checks passed
cpu /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f53-nest-membros/client/build/f53-native/sample.mpg cpu_ms=1.1033
decode /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f53-nest-membros/client/build/f53-native/sample.mpg frames=60 mean_ms=1.1197 p95_ms=2.2230 max_ms=5.6440
```

- `python3 bench/standalone/cast_test.py client/build/nest-test CastTests.test_transport_pause_seek_and_stop CastTests.test_preload_waits_for_play CastTests.test_silence_tolerates_more_than_eight_seconds CastTests.test_partial_media_status_keeps_the_clock_observation` (exit 0):

```text
test_transport_pause_seek_and_stop (__main__.CastTests.test_transport_pause_seek_and_stop) ... ok
test_preload_waits_for_play (__main__.CastTests.test_preload_waits_for_play) ... ok
test_silence_tolerates_more_than_eight_seconds (__main__.CastTests.test_silence_tolerates_more_than_eight_seconds) ... ok
test_partial_media_status_keeps_the_clock_observation (__main__.CastTests.test_partial_media_status_keeps_the_clock_observation) ... ok

----------------------------------------------------------------------
Ran 4 tests in 16.479s

OK
```

`git diff --check`: exit 0, sem saída.

## Nest: diagnóstico, bloqueio e próxima alteração necessária

**#12 não foi corrigida.** A regra “Se precisar mexer fora, pare e descreva no relatório” impede alterar os dois arquivos que controlam o problema: `client/src/nest.hpp` e `client/src/player_nest.hpp`. Foi solicitada ampliação de escopo; não houve resposta até esta entrega.

O limite atual de 8 s em `nest.hpp:248` exige `!possible`. Um `nest-owner.json` de sessão anterior mantém `possible=true`. Com BluetoothIot ativo, a ausência do nosso app não libera a posse porque a lista de applications não está vazia. Assim não há LAUNCH nem timeout nesse caminho. No player, `player_nest.hpp:111` ainda compartilha 25 s para “nunca começou”; a liberação também pode esperar silêncio indefinidamente se o receptor segue conectado sem responder. A regra de 25 s durante reprodução não foi alterada.

Diagnóstico contra o worker real `client/build/nest-test`, importando o Receiver TLS/Protobuf existente de `bench/standalone/cast_test.py`; todos os endpoints eram loopback, com `XT_NEST_NETWORK=127.77.53.1/255.255.255.252`. Quatro cenários: app BluetoothIot que responde RECEIVER_STATUS e recusa nosso LAUNCH; receptor que não responde a nenhum comando; cada um com e sem journal anterior. Depois de 9,2 s, `state=6` é ERROR; `state=1` é LOADING. Saída real (diagnóstico do comportamento existente, não prova de correção):

```json
{"mode": "busy", "stale_owner": false, "elapsed": 9.208, "state": 6, "quiet": true, "connected": true, "launches": 7, "loads": 0}
{"mode": "busy", "stale_owner": true, "elapsed": 9.209, "state": 1, "quiet": false, "connected": true, "launches": 0, "loads": 0}
{"mode": "silent", "stale_owner": false, "elapsed": 9.223, "state": 6, "quiet": true, "connected": true, "launches": 0, "loads": 0}
{"mode": "silent", "stale_owner": true, "elapsed": 9.269, "state": 1, "quiet": false, "connected": true, "launches": 0, "loads": 0}
DIAGNOSTIC: existing 8s startup limit is bypassed with a previous owner journal; Nest files unchanged
```

Para completar #12, a frente precisa incluir os dois headers. Proposta: orçamento de 9 s para abertura/LAUNCH/LOAD e retorno à TV, incluindo o caminho de liberação, sem renovar a tentativa automaticamente naquele vídeo. O prazo cobre o TLS lento já previsto pelos testes (2 s), sem herdar os 25 s reservados à reprodução. Exigir nos fakes a TV audível e aviso até esse prazo, com/sem journal; cobrir também LAUNCH aceito mas LOAD mudo. Preservar os casos de RECEIVER_STATUS sem applications e IDLE com extendedStatus LOADING e verificar a tolerância de 25 s já em reprodução. Esses novos comportamentos ainda não foram implementados nem aprovados pelos testes.

## O que não foi verificado e como verificar

- Não houve execução em Xbox, Nest real ou Bluetooth, conforme proibição. A prova do console é de compilação/link; não prova execução Xenon nem som real.
- A mensagem de membros foi exercitada pelo erro do Player e pela tradução/renderização de todos os glifos árabes; não foi capturada uma tela do fluxo real de membros. Quando o console estiver liberado para outra tarefa autorizada, abrir um vídeo de membros e confirmar uma única tentativa/403, a mensagem específica e retorno normal à navegação; abrir um vídeo comum para conferir o comportamento anterior.
- Nest ocupado/mudo: reproduzido o bloqueio no worker, mas sem correção, teste do prazo de TV ou aviso novo. A integração completa de fallback permanece pendente dos arquivos fora do escopo.
- A build PC emitiu warning em FriBidi (`-Wstringop-overflow`). A build xsdk emitiu warnings em mbedTLS, curl, picojson e no parâmetro `remote` do receiver; ver resultado final abaixo.

## Divergências entre este prompt e o código real

1. `client/src/cast/*` contém sobretudo o receiver Cast do celular, descoberta e filtro de relógio. O sender do Nest está em `client/src/nest.hpp`; sua integração com o Player está em `client/src/player_nest.hpp`. Nenhum dos dois consta na lista autorizada. A entrega não fecha #12 por esse motivo.
2. O fake TLS de Nest reutilizado pelos testes de integração está em `bench/standalone/cast_test.py`, importado por `tests/cast/integration.py` e por testes de `tests/nest`; não há um fake Nest independente em `tests/cast`.
3. Já existe timeout de 8 s antes de LAUNCH, mas condicionado a não haver posse anterior. Portanto o defeito não se resolve só escolhendo outra constante. O prazo de 25 s permanece nos caminhos de reprodução e “nunca iniciou”.
4. O configure PC literal disparou automaticamente `tools/build-ffmpeg-x360.sh pc` porque a identidade do prefixo instalado não coincide com a esperada nesta worktree. Eu deveria ter verificado isso antes. Interrompi imediatamente a árvore de processos criada (PIDs 432684, 432811, 436367 e compiladores descendentes) ao identificar a compilação compartilhada. O script chegou a reconfigurar/compilar objetos em `.work/build-pc`; não tentei apagar nem restaurar arquivos compartilhados. A build posterior usou `-DFFMPEG_PREFIX` para ler as bibliotecas já instaladas. É uma limitação/incidente desta execução, não uma mudança de produto.
5. Para a prova xsdk, o cache privado `client/build-x360` foi pré-configurado com `-DFFMPEG_PREFIX="$PWD/.work/ffmpeg-xbox"`, evitando a mesma recompilação automática. A primeira configuração isolada acusou falta de `shaders_bundle.c`, gerado em seguida pelo script oficial. O script oficial foi executado uma vez com flock e MemoryMax. Antes de executá-lo, `make -n` confirmou que os arquivos compartilhados de xsdk e vulkan4xenon já estavam atualizados; a execução confirmou “is up to date”.

## Build do console

Comando executado uma vez, após o pré-configure do cache privado descrito acima; exit 0:

```sh
flock /tmp/tube360-build-xbox.lock systemd-run --user --scope -q -p MemoryMax=6G tools/build-xbox.sh
```

Saída real final:

```text
xenia_scan: 227 instructions Xenia skips; Xenia results through these functions are unreliable
[100%] Built target tube360
7c0df56475d543d3351e04f796b6e97f2a0d05df1279acefb089d74d27e4bedd  build-x360/tube360.xex
56015d93ff6de29e3d2605f529584e9f240b365aab0ef07e8523c6c567edaedf  build-x360/tube360-launcher.xex
```

O scanner de compatibilidade alertou sobre instruções que o Xenia ignora; não foi executado Xenia. Isso não é falha de link nem prova de comportamento em hardware. Os `.xex` ficaram apenas na build local; não houve instalação no console.

Logs completos desta execução estão em `/tmp/tube360-f53-*.log`. Os resultados relevantes foram transcritos acima para que não dependam da permanência de `/tmp`.

