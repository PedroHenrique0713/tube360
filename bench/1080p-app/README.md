# F40 — 1080p30 no aplicativo (aceite Xbox pendente)

O app está preparado para a rodada; **compilação e PC não qualificam o console**. Não houve
acesso a Xbox, Nest real, conta/cookies, configuração/cache pessoal ou serviços do dono.
Resultados F39 são herdados, copiados em `results/f39/`. A mediana de três execuções do 137
com máscara 7/ALL/6 FRAME/spread é **22,567982 ms**; maior p95 de bloco30 **26,423334 ms**.
Isso é decoder isolado. O app precisa passar os testes abaixo com UI, áudio e rede reais.

## Política do produto

Uma fonte H.264 **1080 linhas e taxa conhecida até 30,1 fps**, inclusive quando escolhida pelo
Automático, abre **6 FRAME workers, HW 0,2,4,1,3,5, loop ALL (48)**. Fontes menores, cadência
alta ou desconhecida mantêm filtro completo e workers configurados (default 4). A política
é definida **antes de `avcodec_open2`**, reaplicada na abertura de outra fonte ou no seek que
reabre o decoder. Não há mudança por fila/pacote no meio do GOP. Helpers de bancada que
não habilitam a política explicitamente continuam com o decoder original.

Em `settings.txt`, `h264_loop_filter=full` mantém filtro completo inclusive em 1080p.
`auto` (default) usa ALL apenas no perfil acima. Feche e reabra o app depois de editar.
A opção persiste ao salvar os outros ajustes. A omissão do deblock altera referências até
o próximo IDR (cerca de 6 s no 137 da F39); conferir nitidez/blocos no HDMI. O override FULL
é uma escolha de qualidade e pode não sustentar 30 fps. A escada continua livre para descer.

`decode-config.txt` continua diagnóstico: quatro campos mapeado/workers/tipo/filtro;
`0` no último campo também força FULL. `-1` ou `1` deixam a política do perfil decidir,
sem reativar o antigo NONREF dinâmico. O perfil 1080p30 sempre usa 6 FRAME, mesmo que esse
arquivo peça 4/SLICE. Não inclua esse arquivo nos casos de aceite.

A previsão para subir de 720 a 1080 deixa de multiplicar o filtro completo por 2,25:
usa **22,567982 ms/quadro** da F39 (67,7% a 30 fps) como piso, elevado pelo custo
observado na sessão. A espera com fila cheia dá tempo aos workers: send/receive quase
gratuitos no produtor não podem apagar o custo Xenon calibrado.
Falha de CPU da mesma sessão substitui a previsão otimista; pausa/seek não apagam esse custo
nem o histórico de degraus rejeitados. Banda fresca, 20 s estáveis, fila, cooldown e pressão
real seguem obrigatórios. FULL conserva a previsão por pixels/cadência.

## Mapa de threads e o que medir

Três núcleos físicos: pares HW0/1, HW2/3, HW4/5. Prioridades são pedidos relativos do xsdk;
o kernel pode limitar. O wrapper solicita prioridade **0** dos workers de vídeo sem
depender de herança; o criador demux/AAC solicita +1.

| HW | Trabalho e prioridade solicitada | Evidência no app |
|---|---|---|
| 0 | Apresentação/UI +2; worker AVC #1 em 0 | `app-thread: presentation`, `decod-worker` |
| 1 | Demux/AAC/resample/PCM +1; SDL PCM (xsdk pelo menos +1); AVC #4 em 0 | `app-thread: youtube`, `app-audio`, `F40 ... silent/feeder_wait_us` |
| 2 | AVC #2 em 0 (Music AAC +1 quando só áudio) | `decod-worker`, `music-aac` |
| 3 | AVC #5 em 0; as duas lanes TLS Range/HLS/playlists +1 | `app-thread: range/hls/playlists`; `rede: Range` |
| 4 | AVC #3 em 0 | `decod-worker` |
| 5 | AVC #6 em 0; Nest/Cast v2 e controle Nest +1; lounge/DIAL/conta/timer em 0; catálogo/miniaturas/comentários/legendas/atualização/descoberta em −1 | `app-thread: cast-v2/nest/cast-rpc/cast-post/cast-dial/atualizações/...` |

Antes, as lanes TLS herdavam HW1 e várias tarefas de fundo herdavam HW0. O alimentador
ficou em HW1, como na F39. Não há um HW sem AVC neste perfil: a proteção é priorizar tarefas
curtas, fazer rede/áudio esperarem em bloqueio e manter os trabalhos de fundo longe da UI.
Os helpers MPEG-1 #1/#2/#3 seguem HW3/1/4 mas dormem durante H.264. Rede/decoder MPEG-1
seguem HW1/2; o controle do servidor opcional fica HW5. Não há alteração no agendamento
interno do driver/kernel de rede, GPU ou áudio.

A **F34 SponsorBlock não está nesta base**: `sponsorblock_thread.hpp` coloca HW5 e eleva
só durante o pedido para +3, depois restaura. Preservar isso ao juntar F34; a F40 deixa
AVC #6 em 0 e fundo em −1 nesse HW. Não integrar o recurso aqui nem inventar medição dele.

Os logs registram HW **atual** dos workers SDL após o movimento, status de afinidade e
prioridade pedidos. `app-audio` consulta HW e prioridade efetiva no feeder PCM real, após
`xsdk_audio_next_frame`; o hook é uma observação única, sem log/alocação por bloco.
`decod-worker` registra a colocação solicitada ao kernel e seu status por handle. Compare
FPS/perdas, p95 de A/V, p95 do intervalo apresentado, PCM `silent` e maior espera, banda,
fila e páginas livres. **Ainda não há medição F40 de disputa no Xenon.**

O worker `SponsorBlock` herdará aqui o papel padrão de fundo (−1); a elevação temporária
continua +3 e restaura a prioridade anterior. Conferir os testes F34 de restauração no merge.

## Builds e verificações locais

```bash
PATH=/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f5-h264/.work/src/usr/bin:$PATH \
  JOBS=2 cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release -DTUBE360_F40_BENCH=ON
cmake --build client/build -j2 --target tube360-quality-fixture h264-policy-test player-state-test h264-cadence-test manual-quality-test reddit-contract-test
python3 bench/1080p-app/check-summary.py
cc -std=c11 -Wall -Wextra -Werror -Ibench/1080p-app/stub \
  bench/1080p-app/thread_test.c client/x360/ffmpeg-libc.c -pthread -o .work/thread-policy-test
.work/thread-policy-test
python3 bench/1080p-app/run-pc.py /caminho/da/F33/.work/quality/media --case primary
python3 bench/1080p-app/run-pc.py /caminho/da/F33/.work/quality/media --case full
python3 bench/1080p-app/run-pc.py /caminho/da/F33/.work/quality/media --case lower-source
```

`h264-policy-test <1080.mp4> <720.mp4> <pasta-vazia-de-settings/>` abre FFmpeg real e
decodifica vários GOPs em ALL/FULL; testa política, opção persistida, previsão/custo de falha,
cooldown/banda e papéis de thread. O stub C executa o wrapper real de `pthread_create`,
mas **não mede nem simula tempo do kernel Xbox**. O resumo é calibrado com logs sintéticos
rotulados; rejeita evidência truncada, falsa plataforma/afinidade/áudio/contadores/memória.

Build Xbox (configuração local `.work/build.env`, apenas caminhos públicos de toolchain):

```bash
flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G \
  env TUBE360_BUILD_ENV="$PWD/.work/build.env" JOBS=2 TUBE360_F40_BENCH=ON \
      TUBE360_F33_BENCH=OFF tools/build-xbox.sh
python3 bench/1080p-app/stage-console.py
```

O build normal usa `TUBE360_F40_BENCH=OFF`. O ON não faz nada sem `f40-playback.txt`:
`<medição-s> <aquecimento-s> <itag> <nest-0-ou-1> <loop-0-ou-48> <id>`.
Não se usa `XT_H264_THREADS`, fonte falsa ou filtro forçado para obter o aceite.

## Coordenador: copiar e executar quando o dono liberar

O stage fica em `bench/1080p-app/.work/console/`; o manifesto tem hashes e receita FFmpeg.
Tudo é novo e isolado; não copiar por cima do app instalado. Não há servidor, conta,
identidade Cast ou `nest-owner.json` no kit. As identidades geradas durante a execução
ficam apenas na pasta temporária. Conferir no painel saída HDMI antes dos testes.

```bash
F40_XBDM=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
F40_IP=192.168.101.39
F40_CASE=tv137
F40_REMOTE="HDD:\\Apps\\Tube360-F40-$F40_CASE"
python3 "$F40_XBDM" "$F40_IP" mkdir "$F40_REMOTE"
for file in bench/1080p-app/.work/console/"$F40_CASE"/*; do
  python3 "$F40_XBDM" "$F40_IP" put "$file" "$F40_REMOTE\\${file##*/}"
done
python3 "$F40_XBDM" "$F40_IP" run "$F40_REMOTE\\tube360.xex"
# Aguardar saída normal: 10 s aquecimento + 120 s medidos + abertura.
# Timeout total da sonda: 220 s após lançamento. Sem reboot/magicboot automático.
python3 "$F40_XBDM" "$F40_IP" get "$F40_REMOTE\\tube360.log" "bench/1080p-app/results/xbox-$F40_CASE.txt"
python3 bench/1080p-app/summarize.py "bench/1080p-app/results/xbox-$F40_CASE.txt" "bench/1080p-app/results/xbox-$F40_CASE.json"
```

Rode por ordem, com pausas entre aberturas públicas (sem laço de extração):

1. **`tv137`**: `4p_9OPKgcDE 31`, perfil 1080p30, saída720, TV. Fonte exatamente137
   1920×1080 ~29,97/30; seis FRAME espalhados e loop48. Nenhuma adaptação. ≥29,5 fps,
   perdas0 após10s, A/V p95≤50ms, PCM sem silêncio, livres≥64MiB amostrados e resumo
   `console_gate=pass`. A/V mede PTS contra relógio PCM, não latência acústica da TV.
2. **`tv137-1080`**: repetir com dashboard HDMI1080p progressivo e framebuffer1080,
   log de `display: 1920x1080 shown 1:1`. Compara disputa de apresentação entre saídas.
3. **`motion137`**: `83OlZMzqQJc 480`, Assassin's Creed Revelations, jogo/ação;
   metadados anônimos iOS confirmaram137/1920×1080/30 em25-11 (~4,93Mbit/s anunciado).
   [Vídeo do autor](https://www.youtube.com/watch?v=83OlZMzqQJc). Repetir os mesmos limites.
   O coordenador deve conferir que o trecho de8min contém ação contínua; se for menu/cutscene,
   escolher um trecho movimentado, registrar o início e repetir120s. Não foi baixado/examinado
   o vídeo completo, nem certificado como pior caso universal. O candidato Forza
   `cHWvPohcs-4` foi **descartado**: descrição30, mas fonte299/1080p60, sem137.
4. **`full137`**: controle com `h264_loop_filter=full`, loop0 e seis workers. Registrar
   qualidade visual e tempo; uma eventual queda é resultado válido do controle, não aceite
   ALL. Se descer, confirmar filtro completo e workers4 em720p. Testar B, seek, pause e
   menu de qualidade em outra rodada sem a sonda (remova `f40-playback.txt`).
5. **`nest137`**: só com autorização para o Nest nessa rodada. Criar `nest.txt` na pasta
   temporária **antes de copiar/rodar**, com o endereço liberado pelo dono (não há IP real
   no kit). O `.example` não é lido. Manter fonte137 e exigir rotaCAST, statusPLAYING fresco
   ≤1s, TV inaudível, mesmos120s/FPS/perdas/A/V/memória. Registrar também jitter/idade.

No Nest, não encerrar por magicboot nem remover a pasta/journal enquanto houver STOP
pendente. Deixar o app sair normalmente, confirmar silêncio e guardar o log. Se necessário,
B primeiro e aguardar confirmação. Uma falha de rede pode deixar o Nest tocando; a liberação
precisa ser confirmada pelo coordenador, sem derrubar o app à força durante o som.

### Por que o Nest ainda não pode qualificar

Esta base **não inclui** `74b7649` (filtragem de jitter Cast e limite de slew) e `1884448`
(testes de jitter/cadência/sincronismo) de `f25b-nest-salvo`, nem o respectivo
`client/src/cast/clock.hpp`. O Nest atual usa relógio bruto extrapolado; a sonda registra
`nest_clock=f25b_pending`, calcula A/V com esse relógio e **sempre** devolve
`pending_f25b_clock` quando os demais números passam. Isso não é prova de50ms acústicos.

O coordenador precisa juntar/reconciliar `nest.hpp`, `player_nest.hpp`, `player.hpp`,
`cast/clock.hpp`, CMake e testes de `74b7649`/`1884448` (e suas dependências de clock),
preservando os hooks F40 e as prioridades, executar os testes de jitter/atraso da F25b,
alterar a sonda para usar a validade/RTT do relógio filtrado e só então habilitar gate Nest.
O merge não foi feito silenciosamente nesta frente. Repetir console com Nest após integrar.

## Captura e limites da evidência PC

A execução PC usa fonte137 real de20s repetida por remux, sem reencode, e AAC **sintético**,
transporte fixture, SDL dummy/offscreen e HOME/cache/assets próprios. Confirma player,
política, filas, relógio e sonda durante120s; não mede TLS/Xenon/Nest/HDMI real. A memória
PC é sentinela, reportada `memory_measured=0`, nunca aceita como RAM de512MiB.
`results/app-pc.json` conserva `pending_console`, mesmo se `F40 APP DONE PASS` funcional.
Não desabilite a escada para satisfazer a sonda: troca de fonte é falha do caso.
