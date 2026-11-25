# F30 — piso da CPU H.264

Bancada independente do player, FFmpeg **7.1.5 LGPL**, cinco fontes sintéticas, seis threads
de quadro e três modos. [Relatório e decisão](RELATORIO.md) · [Linha de montagem proposta](PIPELINE.md).
O executável não reproduz vídeo nem decodifica áudio. O modo `entropy` produz **pixels inválidos**;
não os lê, desenha, exporta como imagem ou usa para verificar conformidade visual.

## Gerar e executar no PC

Na raiz do worktree:

```bash
python3 bench/cpu-floor/generate.py
JOBS=3 systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=512M \
  bench/cpu-floor/build.sh pc
mkdir -p bench/cpu-floor/.work/trace-pc
bench/cpu-floor/build-pc/cpu-floor --trace bench/cpu-floor/.work/trace-pc \
  > bench/cpu-floor/results/pc-run.txt 2>&1
python3 bench/cpu-floor/summarize.py bench/cpu-floor/results/pc-run.txt \
  bench/cpu-floor/results/pc-summary.json
python3 bench/cpu-floor/check.py
```

Requer CMake, compiladores C/C++, Python, NASM e o `ffmpeg`/`ffprobe` de sistema com libx264.
`build.sh` usa o NASM já indicado no COMUM.md; fora desta máquina, instale-o no PATH.
O encoder externo GPL gera arquivos; não é ligado ao decoder LGPL.
Os comandos completos, metadados, bitrates efetivos e hashes estão em `results/fixtures.json`.

| Arquivo | Perfil/nível | Fonte | Taxa nominal | GOP / B / refs solicitadas |
|---|---|---|---|---|
| `720p30-4000.mp4` | High@3.1 | testsrc2 1280×720, 30 fps, 6 s | 4 Mbit/s | 60 / 3 / 3 |
| `1080p30-2500.mp4` | High@4.0 | testsrc2 1920×1080, 30 fps, 6 s | 2,5 Mbit/s | 60 / 3 / 3 |
| `1080p30-5500.mp4` | High@4.0 | mesma cena/tamanho/duração | 5,5 Mbit/s | 60 / 3 / 3 |
| `1080p60-2500.mp4` | High@4.2 | testsrc2 1920×1080, 60 fps, 6 s | 2,5 Mbit/s | 120 / 3 / 3 |
| `1080p60-5500.mp4` | High@4.2 | mesma cena/tamanho/duração | 5,5 Mbit/s | 120 / 3 / 3 |

Todos 8-bit YUV420 progressivos, preset medium, CABAC, transformada 8×8, uma slice/quadro,
`b-adapt=0`, B-pyramid normal, **direct temporal**, predição ponderada P/B, scenecut desligado,
VBV de 2 s, HRD CBR, duas threads do encoder. B-pyramid leva o SPS a quatro referências:
`ffprobe refs=4`, `has_b_frames=2`. São cargas com características de AVC do YouTube, não
amostras extraídas do YouTube nem uma afirmação sobre os parâmetros exatos do encoder dele.
Em apenas 6 s, o reservatório VBV permite média acima da taxa nominal. Por isso a carga alta
usa alvo de 5,5 Mbit/s; o gerador exige bitrate efetivo entre 2,5 e 6 Mbit/s nos quatro clips
1080p. Consulte os bitrates efetivos da geração final em `results/fixtures.json`.

Não usa o FFmpeg do sistema para medir. `build.sh` copia **sem alterações** o script do app,
`tools/build-ffmpeg-x360.sh`, e seus ajustes VMX para `bench/cpu-floor/.work/app-build/`.
Compila a configuração original, instrumenta somente a árvore privada e recompila quatro arquivos.
O SHA-256 do tarball é o mesmo do app. Configuração, bibliotecas, fonte modificada e produto
ficam nessa raiz; as bibliotecas do app não são alteradas. Alterar a instrumentação invalida
seu carimbo de build. GPL, version3, nonfree, autodetect e rede continuam desligados.

## O que é medido

| Modo | Reconstrução | Filtro |
|---|---|---|
| `full` | completa | `AVDISCARD_DEFAULT` |
| `no_loop_filter` | completa, com referências sem filtro | `AVDISCARD_ALL` |
| `entropy` | retorno antecipado em `ff_h264_hl_decode_mb`; conserva list_counts e limpeza do scratch | pulado também em `loop_filter` |

O corte mantém CABAC/CAVLC, SPS/PPS, DPB/POC, predição/escrita de MVs e modos, NNZ, coeficientes,
**a dequantização já feita pelo parser**, alocações de AVFrame, caches de metadados e sincronização
de progresso para direct temporal. Não equivale ao CABAC aritmético isolado nem mede exportar
comandos para a GPU. Só as fixtures 8-bit 4:2:0 progressivas/CABAC/uma slice são qualificadas.
Não retire `decode_finish_row` ou `await_reference_mb_row` do direct: referências de metadados
ainda precisam estar prontas mesmo quando ninguém usa seus pixels.

Cada lançamento padrão faz 15 auditorias separadas e 45 rodadas cronometradas: 3 repetições ×
3 modos × 5 clips. A ordem roda `full,no_loop_filter,entropy`, depois `no_loop_filter,entropy,full`,
depois `entropy,full,no_loop_filter`. Arquivos são lidos e demuxados em RAM antes de medir;
abrir/fechar o decoder também fica fora. Pacotes, registros e vetores são pré-alocados.
Alocações internas do FFmpeg continuam existindo, como no app.

Todos os quadros, inclusive flush, são contados. Para o throughput, excluem-se 30 quadros de
aquecimento e 12 de cauda; sobram 138/318 amostras. Sem sleeps, log por quadro ou timers por MB.
Há dois relógios por slice (resolução de 1 µs) e contadores leves por MB em **todos** os modos.
O retorno de cada AVFrame recebe um timestamp monotônico. A auditoria pesada de hashes e MD5
fica em outra rodada e não entra nos tempos.

| Campo | Significado e limite |
|---|---|
| `mean_ms` | tempo de parede / quadro entregue, em regime; métrica principal de capacidade |
| `completion_p50_ms`, `completion_p95_ms` | intervalo entre entregas de AVFrames; seis workers podem terminar em rajadas; não é latência de um quadro |
| `slice_p50_ms`, `slice_p95_ms` | serviço da única slice por quadro no worker; inclui esperas por referências e preempção; seis desses trabalhos se sobrepõem |
| `block30_p50_ms`, `block30_p95_ms` | tempo / quadro em blocos de 30 entregas; só 4/10 blocos por repetição, insuficiente para caudas longas |
| `total_ms` | enviar todos os pacotes + receber + flush; inclui aquecimento/cauda, exclui abrir/fechar |

O resumo informa a mediana das três repetições de cada estatística e seu mínimo/máximo;
mediana de p95s não é p95 combinado. Nunca divida o p95 de serviço por seis para prometer fps.
Os contadores são por quadro: 3.600 MB em 720p, **8.160 MB em 1920×1088**. Em `entropy`, zero
reconstruídos é condição de PASS. O parse precisa ter mesmo PTS/tipo/hash de tipo/QP, NNZ,
coeficientes e predições nos três modos. Saída corrompida, erro de decode, contagem divergente,
falta de progresso ou EOF incompleto falham. `check.py` compara os pixels visíveis dos outros
dois modos com `framemd5` do FFmpeg externo e testa rejeição de mídia/log incompletos.

## Compilar o XEX

Não precisa SDL, GPU, fontes, configuração pessoal ou rede do console:

```bash
export XSDK_DIR=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/vendor/xsdk
export XSDK_TOOLCHAIN=/home/pedrohenriquequadro/xsdk-gcc
JOBS=3 flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=512M \
  bench/cpu-floor/build.sh xbox > bench/cpu-floor/results/build-xbox.txt 2>&1
sha256sum bench/cpu-floor/build-xbox/cpu-floor.xex
```

Usa o mesmo `ffmpeg-libc.c` e wrapper de afinidade do app. O alimentador roda no HW 1;
os seis workers são colocados em **2,4,3,5,2,4**: seis threads de software nos quatro HW
reservados pelo player, não seis núcleos físicos. Os `decod-worker` do log precisam confirmar
afinidades com status de sucesso. Não mude afinidade e compare como se fosse o mesmo ensaio.

## Roteiro exato do coordenador — XBDM

**Comandos abaixo são para o coordenador. Não foram executados por esta frente.**
Feche o player. Use pasta nova no HDD, sem substituir o instalado. No shell do PC, na raiz
deste worktree (confirme o IP e o nome do drive do console; neste ambiente, `HDD:`):

```bash
F30_XBDM=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
F30_IP=192.168.1.50
F30_REMOTE='HDD:\Apps\Tube360-F30-CPU'
python3 "$F30_XBDM" "$F30_IP" mkdir "$F30_REMOTE"
python3 "$F30_XBDM" "$F30_IP" put bench/cpu-floor/build-xbox/cpu-floor.xex "$F30_REMOTE\\cpu-floor.xex"
for clip in bench/cpu-floor/media/*.mp4; do
  python3 "$F30_XBDM" "$F30_IP" put "$clip" "$F30_REMOTE\\${clip##*/}"
done
python3 "$F30_XBDM" "$F30_IP" run "$F30_REMOTE\\cpu-floor.xex"
```

O XEX abre `game:\<clip>.mp4`, grava **`cpu-floor.log` ao lado do XEX** pelo xsdk e sai sozinho
ao terminar, sem controle/TV. Se renomear o XEX para `default.xex`, o log será `default.log`.
Não crie `dbgprint.txt`: DbgPrint síncrono afeta os tempos. Não precisa `net_log.txt`.
Estimativa de duração do roteiro: reserve **5 minutos** por lançamento; é timeout operacional,
não tempo medido. Se continuar crescendo, acompanhe o log, sem reboot automático.

```bash
python3 "$F30_XBDM" "$F30_IP" get "$F30_REMOTE\\cpu-floor.log" bench/cpu-floor/results/xbox-run-1.txt
python3 bench/cpu-floor/summarize.py bench/cpu-floor/results/xbox-run-1.txt \
  bench/cpu-floor/results/xbox-summary-1.json
```

Exigir `F30 START ... platform=xbox ffmpeg=7.1.5 threads=6 repeats=3 audit=1`, LGPL,
seis afinidades válidas por abertura, 15 `F30 AUDIT`, 45 `F30 RESULT`, nenhuma `F30 FAIL`,
**`F30 DONE PASS audits=15 results=45`**. O resumo recusa execução parcial, plataforma misturada,
modo/clip/repetição ausente. Um log PC permanece com decisão `pending_console`.

Repita `run/get/summarize` mais duas vezes, salvando como `xbox-run-2/3.txt` antes de relançar,
porque o título recria o log. Guarde os três logs, o hash do XEX e `fixtures.json` juntos.
Não misture resultados de mídias regeneradas. Pode remover depois somente essa pasta temporária.

Para 1080p30, `mean_ms >= 33,333` em `entropy` reprova o offload sozinho para a carga medida.
Até 29 ms e blocos abaixo de 33,333 apenas autorizam o próximo protótipo; faltam empacotamento,
flush de cache, áudio, rede, UI e toda a GPU. Entre 29 e 33,333: medir mais antes de investir.
Para 60 fps, os limites correspondentes são 16,667 ms e alvo de 13 ms. Clips sintéticos curtos
não provam um piso universal nem p95 de reprodução. Não some tempos de seis workers.

## Fonte e relink

Código de bancada sob MIT do repo; modificações aplicadas ao FFmpeg são LGPL-2.1-or-later.
Não há código HG-ERIK/TubeBox360 nem módulos/shaders Microsoft. A fonte exata é recriada pelo
tarball fixado + ajustes do app + `prepare.py`, com seu `f30_stats.h`. Aviso LGPL original em
`tools/ffmpeg-x360/COPYING.LGPLv2.1`. O CMake relinka o XEX com a biblioteca privada recompilada;
para distribuir o binário fora deste experimento, acompanhar esses arquivos/scripts, configurações
e fontes/objetos de relink segundo o processo de licença do projeto. Não publicar só o XEX.
