# F40 — política 1080p30 integrada; aceite Xbox pendente

25/11/2026, branch `f40-1080p-app`, base `ef2f094` (F32 + F33).

**Implementação e kit entregues. Não está qualificado no console.** Nenhum XBDM/FTP,
execução/reboot ou acesso ao Nest real foi feito. Os resultados Xbox da F39 são herdados;
a reprodução F40 com UI/áudio/rede, a memória real e o ensaio Nest dependem do coordenador.

## Entrega por item do brief

1. **Decoder 1080p30:** seis FRAME workers em `0,2,4,1,3,5`, prioridade solicitada0,
   `AVDISCARD_ALL`, definido antes de abrir o decoder. A mesma escolha é refeita quando
   outra fonte/seek abre um decoder novo. Fontes menores, taxa desconhecida ou >30,1 fps
   conservam filtro completo; não há alteração por pressão de fila no meio do GOP.
   `h264_loop_filter=full` em `settings.txt` restaura FULL também em1080p; persiste no
   salvamento dos demais ajustes. Reabrir o app após editar. Automático aplica o perfil
   quando a fonte efetiva é1080p≤30,1, independentemente do framebuffer720/1080.
2. **Mapa de threads:** UI/apresentação HW0/+2, demux/AAC/PCM HW1/+1, TLS Range/HLS
   HW3/+1, Nest/Cast v2 e controle do player HW5/+1, lounge/DIAL/conta/timer HW5/0,
   catálogo/miniaturas/atualização/descoberta/comentários/legendas HW5/−1. A implementação
   centraliza os papéis nos workers SDL e mantém os caminhos legados. `app-thread` informa
   HW atual e os status de movimento/prioridade; `app-audio` consulta uma vez o HW/prioridade
   efetivos do feeder SDL real. `decod-worker` informa a colocação por handle e seu status.
   A F34 não foi integrada: seu pedido SponsorBlock em HW5/+3 temporário deve ser preservado
   no merge; a restauração volta ao papel de fundo anterior. **Disputa real no Xenon não medida.**
3. **Escada:** previsão de subida para1080p usa22,567982 ms da F39 como piso, em vez de
   multiplicar o custo FULL de720 por2,25. Um custo maior observado na sessão prevalece.
   Send/receive quase gratuitos quando workers executam durante a espera por fila não
   apagam o piso medido. Históricos de falha, backoff, banda fresca e pressão sustentada
   continuam valendo. FULL usa a previsão conservadora anterior.
4. **Sonda F40 APP:** opt-in `TUBE360_F40_BENCH`, arquivo `f40-playback.txt`,10 s+120 s;
   exige137/1080p/~30, seis FRAME/spread/loop correto, ausência de adaptação, ≥29,5 fps,
   perdas 0, A/V p95≤50 ms, memória amostrada≥64 MiB e som/rota válidos. A/V é amostrado
   por imagem apresentada; log de1s contém fila, PCM, silêncio/espera do feeder e memória.
   PC nunca certifica Xbox. Nest mede o relógio atual mas permanece `pending_f25b_clock`.
5. **Kit e roteiro:**49 arquivos, cinco casos em pasta local nova: TV720, TV1080,
   jogo1080p30, controle FULL e Nest. Manifesto SHA-256, quatro fontes por caso e comandos
   XBDM completos no [README](README.md). O stage recusa pasta já preenchida para preservar
   eventuais journals STOP. Há um segundo vídeo de outro tipo, Assassin's Creed Revelations,
   com137/1920×1080/30 confirmado por uma consulta anônima. O trecho de480 s é candidato:
   o coordenador deve conferir ação contínua antes da medição. Não foi certificado como
   pior caso nem reproduzido no console. Forza foi descartado por oferecer299/1080p60.

Implementação: **`d9eb230` — Playback: apply the measured six-worker 1080p30 policy**.
Scripts, documentação e resultados entram no commit documental seguinte.
Histórico solicitado: [commits-before-report.txt](results/commits-before-report.txt).

## Provas executadas

Os comandos completos de build, sonda e console estão no README. PC sempre `-j2`;
Xbox sob `flock` compartilhado e escopo MemoryMax6G/SwapMax1G, GCC52.

| Prova | Resultado |
|---|---|
| Build PC final `cmake --build client/build -j2 --target tube360-quality-fixture` | exit0; [log](results/build-pc-final.txt) |
| Build Xbox final `... TUBE360_F40_BENCH=ON JOBS=2 tools/build-xbox.sh` | exit0; [log](results/build-xbox.txt) |
| Decoder real `h264-policy-test <1080> <720> <settings isolados/>` |600 imagens ALL/6t,600 FULL/6t,180 FULL720/4t; filtro estável através dos GOPs; opção persistida e projeções verificadas |
| Wrapper C do produto com kernel/pthread de teste | ordem dos seis HW exclusivos, legado, reset, thread fora do decoder e falha de criação |
| Player-state, cadence, reddit-contract, manual-quality | passaram; último com 63 verificações e pasta isolada |
| Resumo da sonda |3 controles sintéticos válidos;18 logs falsos/truncados recusados; nenhum log sintético usado como prova Xbox |
| App PC final120 s |**29,860 fps,0 perdas, A/V p95 16,76 ms**, máximo53,81 ms; fonte137 mantida, fila mínima18; `pending_console` |
| Opção FULL no app PC10 s |29,892 fps,0 perdas, p95 17,33 ms; decoder6t/loop0 confirmado |
| Contraprova app com só136/720p10 s |~29,86 fps e0 perdas, mas sonda **FAIL** por fonte/política incorretas; não há falso aceite pelo FPS |
| CPU lenta no player real170 s |1080/44 ms→720/19 ms uma vez, sem subida após cooldown; seek preservou rejeição; custo projetado134,3% |
| Kit local |49 hashes iguais,5 casos isolados; reuso de stage recusado com exit2 |

App final reproduzível:

```bash
python3 bench/1080p-app/run-pc.py \
  /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f33-1080p/.work/quality/media \
  --work .work/f40-final --case primary
# repetir com --case full e --case lower-source
python3 bench/1080p-app/check-summary.py
```

[Resumo final](results/verification.json), [app](results/app-pc-primary.json),
[contraprova](results/app-pc-lower-source.json), [projeção sob pressão](results/cpu-pressure-validation.json).
A primeira rodada120 s também passou:30,000 fps, perdas 0, A/V p95 16,75 ms; preservada em
`results/app-pc.txt/json`. A rodada final usa o binário posterior ao piso conservador.

O PC usa o137 real preservado de20 s repetido por **remux**, AAC **sintético**, transporte
fixture e SDL dummy/offscreen. HOME/cache/assets são próprios. O FFmpeg PC7.1.5 foi
copiado da bancada F5 para `.work/ffmpeg-pc` e selecionado por `FFMPEG_PREFIX`; não fornece
medição dos kernels PowerPC. A memória PC é sentinela, `memory_measured=0`, nunca prova
512 MiB. Os campos PCM/clock não substituem escuta de sincronismo labial na TV/Nest.

Captura aberta e inspecionada:
`/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f40-1080p-app/.work/f40-pc/screen.png`.
Mostra o futebol137 reduzido para1280×720, imagem completa, cores/planos e geometria íntegros.
[Log de captura](results/screenshot-pc.txt). Não é captura HDMI do Xbox.

XEX final: **`a3d53de3f39c6def402c7e0d3f22b0fc8d85496fe324058c5f228033303c9d89`**.
Símbolos de ambos os wrappers confirmados no ELF. FFmpeg AltiVec/pthreads ligados;
GPL/nonfree/version3 desligados. Receita instalada confere com `source-id.py` e GCC52.
O scanner emitir avisos sobre Xenia não demonstra reprodução nele nem no hardware.

Tentativas recusadas estão separadas: alvo PC pedido antes da reconfiguração; manual-quality
sem argumento isolado; primeiro link Xbox gerado antes da inclusão do wrap de áudio,
recusado por `__real_xsdk_audio_next_frame` ausente. O build final reconfigurou e incluiu
ambos os wraps. Uma sessão de build PC interrompida não conta como build final.
O harness de pressão completou os asserts e imprimiu PASS, mas o monitor externo devolveu143;
seu log final foi revalidado com exit0 e hash em `cpu-pressure-validation.json`. Não há
conversão desses códigos em prova de console nem alegação de CI completo.

## Pendências e execução pelo coordenador

**Não rodar durante o uso do dono.** Quando ele liberar, usar os comandos exatos do README;
não instalar sobre o app existente, não rebootar nem usar magicboot automaticamente.

Pasta pronta:
`/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f40-1080p-app/bench/1080p-app/.work/console/`.
Transferir `tv137` para `HDD:\Apps\Tube360-F40-tv137`, executar e recuperar `tube360.log`;
resumir por `summarize.py`. Repetir `tv137-1080`, `motion137`, controle `full137` e `nest137`.
Exigir todos os critérios do item4, status de afinidade/prioridade válidos, som contínuo,
comandos responsivos e inspeção visual do filtro. O teste de512 MiB e a comparação de
saída720/1080 ainda faltam; não inferir folga real pelo orçamento de fila.

Para Nest, criar `nest.txt` somente na pasta temporária com o host autorizado e deixar
confirmar STOP ao sair. Não remover pasta/journal com STOP pendente. O kit contém só
exemplo sem IP real. Não houve conexão ao alto-falante nesta frente.

Faltam **`74b7649`** (clock filtrado/RTT e slew de imagem) e **`1884448`** (testes de
jitter/cadência) de `f25b-nest-salvo`, incluindo `cast/clock.hpp`, ajustes em `nest.hpp`,
`player_nest.hpp`, `player.hpp` e as bancadas de jitter/Wi-Fi. Não foram cherry-picked.
O coordenador deve reconciliar esses pontos, executar os testes F25b, adaptar a sonda à
validade/RTT filtrados e só então habilitar o aceite Nest. Até lá, mesmo números bons
no Nest geram `pending_f25b_clock`; o relógio bruto não certifica50 ms.

## Integração e divergências

- `main.cpp`: include/instância/tick condicionais F40, leitura da opção do settings,
  prioridade/HW da apresentação e default HW5 do worker de catálogo.13 linhas adicionadas,
  uma alterada; **`i18n.hpp` intacto**, pois foi escolhida a opção por arquivo autorizada.
- `player.hpp`/`player_h264.hpp`/`h264.hpp`: política na abertura, telemetria atômica para
  sonda, custo da sessão preservado após seek e reabertura dos degraus. Reconciliar F25b
  nas regiões de relógio sem perder esses campos.
- `yt/thread.hpp`: afeta todos os workers nativos no Xbox; testar F34 e Cast/lounge no
  console após merge. Threads da biblioteca/kernel não foram reimplementadas.
- CMake PC/Xbox: opção F40 e alvo de política; Xbox acrescenta wrap do feeder PCM.
  `build-xbox.sh` repassa a opção e respeita `JOBS` em vez de `nproc` no app.
- Docs tocados: `docs/STANDALONE.md`, `bench/quality/PERFORMANCE.md` e esta bancada.
- A política também vale para1080p30 selecionado pelo Automático; usar FULL continua
  possível. Não se omite filtro em720p por fila vazia como no código anterior.
- F34 e clock F25b não estão na base: seus testes reais em conjunto não foram simulados
  nem alegados como concluídos. O segundo vídeo tem metadados verificados, não um ensaio
  de120 s já executado. O número22,57 ms é herança F39, não medição nova do aplicativo.

Sem push/deploy, alteração de outro worktree, conta/cookies/config privada/cache real,
serviços do produto, VPS, console ou Nest real. Nenhum processo de teste/build próprio
fica rodando ao encerrar a entrega.
