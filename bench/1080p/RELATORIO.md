# F33 — 1080p30: candidato compilado, medição Xbox pendente

2026-11-24, branch `f33-1080p`, base `integracao-032`.

**O requisito ainda não está concluído.** Há kernels CPU implementados, prova bit a bit,
fila maior com admissão por memória, bancada A/B e app de prova compilados para Xbox.
Falta executar no console: média ≤28 ms, 120 s sem perdas, sincronismo e folga dos 512 MiB.
Não converti resultados PC nem compilação em demonstração de 1080p30 no Xbox.

Foi solicitado ao coordenador devolver primeiro os logs de `f33-kernels.xex` e
`f33-cpu.xex` com **`1 6 0`** (chroma, seis FRAME workers, FULL). O roteiro completo está
em [README.md](README.md), e o diretório pronto para copiar é:

`/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f33-1080p/bench/1080p/.work/console/`

## 1. Entrega por item do brief

### CPU primeiro

Implementado dentro do FFmpeg **7.1.5 LGPL-2.1-or-later**, com fonte e receita versionados:

| Estágio | Cobertura acrescentada | Resultado Xbox F33 |
|---|---|---|
| Chroma MC | 8/4/2, todas as 64 fases, put/avg; shifts/somas no lugar dos multiplies incompatíveis | **Pendente**, máscara 1 |
| Qpel luma | 8/4/2, 16 fases; avg 8/4 como upstream; FIR 2D int16/int32, linhas horizontais reutilizadas | **Pendente**, máscara 3 |
| Predição ponderada | weight/biweight 16/8/4/2, pesos negativos, sem saturar o produto/soma antes da hora | **Pendente**, máscara 7 |
| Workers FRAME | Bancada aceita 4/5/6, mesma afinidade 2,4,3,5,2,4; sem eleger vencedor por PC | **Pendente**, configs separadas |
| NONREF | Já existia. Testes separados de FULL para não contar como ganho novo | **Pendente**, configs separadas |
| Alocação/cópias | Um holder de superfície para os três AVBufferRef, em vez de três alocações; posse atômica até o último plano. Caminho mapeado preservado | Correção PC provada; custo Xbox **pendente** |

A máscara 0 conserva os DSPs anteriores, incluindo qpel16/IDCT/deblock VMX já presentes.
A bancada alterna baseline/candidato no **mesmo XEX, clipe, compilador, filtro e afinidade**.
Antes dos tempos, compara todos os pixels Y/U/V e os hashes de sintaxe por quadro.
Isso evita atribuir ao novo kernel diferenças de compilador ou de NONREF.

O primeiro objeto O3 tinha **177.960 B** de texto. O2 reduziu a **70.996 B** no compilador
antigo; o objeto final GCC52 tem **69.760 B**. É tamanho estático, **não speedup medido**.
O scanner do objeto final achou 5.539 instruções vetoriais e zero multiplicações da lista
incompatível. A lista não é prova universal da ISA.

O SDK atual recusa o prefixo antigo no guard de loops contados. O conjunto final usa
**`/home/pedrohenriquequadro/xsdk-gcc-52`**, ainda GCC 15.3.0. O build agora identifica a
receita e também os binários GCC/cc1; trocar o gerador limpa os objetos FFmpeg. CMake deixa
de reutilizar biblioteca antiga só porque `libavformat.a` existe. A identidade conferida é
igual no FFmpeg do app e na biblioteca privada da bancada; só esta última recebe a
instrumentação F30, que não entra no app.

### Fila maior só em 1080p30

`decodeQueue` seleciona **18 posições, início em 12**, apenas em 1080p com taxa conhecida
até 30,1 fps e pelo menos **96 MiB livres** informados pelo kernel. Outros casos mantêm 10/6.
Os PTS e o relógio de áudio permanecem os mesmos; o início do PCM espera o watermark.

Oito imagens extras: 25.067.520 B densos, orçamento arredondado de **32 MiB**. O pool GPU
continua limitado a **160 MiB / 48 superfícies**. A soma conservadora de 33 retenções de
até 4 MiB dá 132 MiB; o restante do app entra na consulta real de páginas disponíveis,
não em uma estimativa de memória de kernel/UI. [Conta e limites](MEMORIA.md).

**Ainda não foi provado que cabe junto com o app nos 512 MiB.** O aceite exige o log Xbox
com pitches/alocações reais, mapeamento sem fallback e ≥64 MiB livres amostrados durante
120 s. O limiar de admissão não substitui essa execução nem captura todo pico entre amostras.
No PC, o ensaio utilizou 27 superfícies/80,84 MiB, mas isso não descreve o layout Xenos.

### GPU condicional

Não foi implementado novo offload. O brief manda decidir **depois** da medição CPU.
Até receber os logs, não há base para concluir que a CPU falhou ou escolher o estágio GPU.
O projeto F30 e a evidência F28 de 4,08 ms continuam referências, não resultados novos.
Não houve cópia/tradução de HG-ERIK/TubeBox360, readback ou alteração de shaders do produto.

### Bancadas e reprodução

- `f33-kernels.xex`: teste C/VMX na ABI real; log ao lado e saída automática.
- `f33-cpu.xex`: régua FULL F30, máscaras 1/3/7, workers 4/5/6, FULL/NONREF explícitos;
  três repetições pareadas, quatro auditorias e 24 resultados quando o 137 está presente.
- `cpu-floor.xex`: régua F30 original recompilada, mesmos cinco sintéticos/três modos.
- `tube360.xex`: build opcional `TUBE360_F33_BENCH=ON`; `f33-playback.txt` ativa 10 s de
  aquecimento + 120 s de medição e encerramento automático. Mede perdas, fonte, PCM,
  diferença PTS/relógio de áudio, fila e páginas livres, sem modificar a escada de qualidade.
- Autoplay: **`4p_9OPKgcDE 31`**, perfil 1080p30, som TV, sem servidor/conta/Nest.
  O 137 preservado desse vídeo tem 600 quadros High@4.0/1920×1080, ~29,97 fps, sem reencode.
  A disponibilidade anônima atual ainda depende da abertura no console. Uma tentativa de
  consultar oEmbed foi bloqueada por throttling do serviço de navegação; nenhuma mídia nova
  foi baixada do YouTube por esta frente.

O resumo recusa logs parciais, plataformas/afinidades/máscaras misturadas, auditoria faltando,
contagens inválidas e tempos não finitos. PC permanece `cpu_gate=pending_console`, mesmo que
os tempos numéricos sejam baixos. O gate CPU Xbox exige todas as repetições 1080p ≤28 ms e
blocos de 30 entregas ≤33,333 ms; não qualifica sozinho a reprodução.

## 2. Provas e números

### Baseline já medida pelo coordenador — F30, não F33

Medianas das nove repetições nos três logs herdados `xbox-run-1/2/3.txt`:

| Carga | FULL ms/quadro | Sem loop filter | Entropia/parse | p95 de intervalo de entrega FULL |
|---|---:|---:|---:|---:|
| 1080p30 2,5 M | 36,674819 | 30,726300 | 9,478351 | 161,180030 ms |
| 1080p30 5,5 M | 41,314496 | 35,560334 | 13,274656 | 160,868632 ms |

[Dados, extremos e hashes dos logs](results/baseline-console.json). p95 de entrega não é
latência individual do worker. Para atingir 28 ms, as reduções de FULL são 23,7%/32,2%.
O baseline de referência foi compilado antes do GCC52; a prova causal dos kernels é o A/B
no mesmo binário F33, não apenas subtrair a medição antiga da nova.

### Correção PowerPC

```bash
XSDK_TOOLCHAIN=/home/pedrohenriquequadro/xsdk-gcc-52 \
FFMPEG_SRC="$PWD/.work/src/ffmpeg-7.1.5" \
FFMPEG_CONFIG="$PWD/bench/1080p/.work/config" \
bash bench/1080p/check-kernels.sh
```

**`F33 KERNEL DONE PASS cases=1291520`** — [log GCC52](results/kernels-ppc-gcc52.txt).
Templates C oficiais 7.1.5, intrínsecos VMX reais, mesmos pixels, preservação de fonte e bytes
fora da partição. Todas as fases, alinhamentos 0–15, strides 64/73/128/−64, denom 0–7,
pesos −128…128 e offsets biweight −256…254. Emulação usa ABI SysV/PPC32 porque o QEMU
PPC32 não executa as instruções escalares PPC64 da ABI xsdk; o teste XEX ainda deve ser rodado.
Não se usou emulação para atribuir tempo ao Xenon.

`check-vmx.py` sobre o build Xbox completo terminou com código 0; [scanner final](results/vmx-final.txt).
Um objeto PowerPC contendo `vmladduhm` foi realmente montado e recusado com saída não zero,
calibrando o detector: [rejeição](results/vmx-rejection.txt).

### Bancadas PC e oráculo independente

```bash
JOBS=2 bash bench/1080p/build.sh pc
bench/1080p/build-pc/f33-cpu > bench/1080p/results/pc-run.txt
python3 bench/1080p/summarize.py bench/1080p/results/pc-run.txt bench/1080p/results/pc-summary.json
python3 bench/1080p/check-summary.py
bench/cpu-floor/build-pc/cpu-floor --trace bench/cpu-floor/.work/trace-pc \
  > bench/cpu-floor/results/f33-pc-run.txt
python3 bench/cpu-floor/check.py --log bench/cpu-floor/results/f33-pc-run.txt \
  --output bench/1080p/results/floor-verification-pc.json
```

- F33: **4 auditorias / 24 medições**, incluindo os 600 quadros do 137. No PC, ambos os
  lados executam os DSPs x86; valida o harness, não mede os novos kernels PowerPC.
- F30: **15 auditorias / 45 medições**. Oráculo FFmpeg 8.1.3: **2.520 imagens** reconstruídas,
  zero diferenças por MD5, 15 traces de sintaxe; seis logs inválidos e mídia truncada recusados.
- Resumo F33: **10 mutantes de log recusados**, PC incapaz de certificar Xbox.
- Identidade: estável; alteração no código ou no gerador GCC/cc1 modifica o hash.
- Notebook compartilhado e ensaios funcionais concomitantes: **não atribuir speedup aos tempos PC**.

### App, memória e clocks no PC

Build com `cmake --build client/build -j2 --target tube360-quality-fixture player-state-test
h264-cadence-test video-memory-test`, FFmpeg 7.1.5 limpo da F5 para o app, biblioteca F30
instrumentada somente nas bancadas. HOME/cache/assets isolados e transporte gravado.

- `player-state-test`: passou, incluindo o novo gate de 12 imagens, memória insuficiente,
  exclusão de 720p/60 fps, preservação de pausa/PTS e testes anteriores de Cast.
- `h264-cadence-test`: contagens corretas de 24/25/29,97/30/50/59,94/60 fps.
- App: **29,943 fps**, **zero perdas**, 10 s de regime após 2 s de aquecimento; fila 18→18,
  p95/máximo da diferença PTS/PCM **20,63 ms**. Vídeo 137 real preservado; áudio **sintético**
  para testar o relógio. [Log](results/app-pc.txt). Não é prova de sincronismo labial em TV.
- `video-memory-test`: modos mapeado, staging e alocador recusado, **600 imagens em cada**,
  todas apresentadas; hash YUV **`e9de7564bd5b36fc`** igual. As nove capturas RGB dos quadros
  31/91/151 também são idênticas. [Comparação](results/video-memory-comparison.json).
- Capturas olhadas: `/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f33-1080p/.work/quality/proofs/app-1080p.png`
  mostra o player com badge 1080 e a fixture identificada; `mapped-091.png` na mesma pasta
  mostra o quadro completo sem corrupção de planos/strides. Os PPM e controles estão ao lado.

A sentinela PC `4294967295 MiB` não é memória medida. Não foi usada como prova dos 512 MiB.

### Build Xbox final

Comandos de build e transferência no README; builds sob a trava compartilhada, escopo
MemoryMax=6G/SwapMax=1G. Saídas finais com código **0**:
[bench](results/build-bench-xbox-gcc52.txt), [app](results/build-app-xbox-gcc52.txt).
Configurações verificadas: FFmpeg 7.1.5, AltiVec/pthreads ligados; GPL/nonfree/version3 desligados.
O CMake de app/F33/F30 aponta efetivamente ao GCC52, conferido nos arquivos de compilador.

| Artefato | SHA-256 |
|---|---|
| `client/build-x360/tube360.xex` | `d949be6e243ee1d4a8d2781796a7450d8e744d1c9820b88162fed75e2223c544` |
| `bench/1080p/build-xbox/f33-cpu.xex` | `9e1c442ff9a236df56c083959088205d907669606f9203a8cf5767c372515c38` |
| `bench/1080p/build-xbox/f33-kernels.xex` | `59e26089d068e004ef5c5867f63b55db7f9c5c6c3a335f51eb89d3f083365bd3` |
| `bench/cpu-floor/build-xbox/cpu-floor.xex` | `3ae42dbc9bc30f68f6071b4639c4f7b9e73913775ac339c8894c1654cbb7e53f` |

[Manifesto do stage, 31 arquivos](results/artifacts.json). O scanner SDK informa instruções
que o Xenia ignora em bibliotecas; o build não é prova de reprodução no emulador ou console.
As primeiras tentativas que falharam estão identificadas nos logs, separadas da prova final:
protótipos ausentes (corrigidos), compilador antigo recusado pelo SDK e target de teste sem link
com `tube360-cast` (corrigido). Não se suprimiu o guard para conseguir o XEX.

## 3. O que falta verificar

1. Console: rodar kernels; medir máscaras 1,3,7 em ordem; eleger FRAME 4/5/6 e medir NONREF
   separado, com o procedimento exato do README. Guardar logs antes de novo lançamento.
2. Devolver o log do `cpu-floor.xex` em modo **full** como régua de compatibilidade.
3. App público: 10 s + 120 s, fonte 137/1080p30 sem redução, zero perdas, som contínuo/labial,
   fila estável, superfícies mapeadas, páginas livres e saída HDMI. Repetir FULL/NONREF.
4. Se CPU ainda não fechar ≤28 ms, usar os números por etapa para decidir o híbrido F30,
   mantendo referências na GPU e sem readback. Isso não foi substituído por uma estimativa.

Nenhum acesso ao console/XBDM/FTP foi feito por esta frente. Não houve acesso a conta/cookies,
Nest real, cache/configuração privada, serviços do produto ou VPS; não houve push/deploy.
Todos os processos iniciados para testes/build terminaram.

## 4. Integração e commits

Commits próprios de implementação:

- **`235d5f8`** — H264: add Xenon-safe DSP kernels and paired CPU probes.
- **`9dba055`** — Player: budget the 1080p queue and add timed playback evidence.

Este relatório/evidência de compilador entra em commit posterior. Histórico solicitado,
`git log --oneline v032..HEAD`, em [commits-before-report.txt](results/commits-before-report.txt).

Pontos de colisão possíveis:

- `main.cpp`: apenas include/instância/tick da sonda sob `TUBE360_F33_BENCH`, nove linhas;
  `i18n.hpp` não foi alterado. Não há alteração de UI/strings de produto.
- `player_h264.hpp`: criação do orçamento na primeira imagem/mudança de altura e telemetria;
  nenhuma linha da adaptação `lowerVideo`/subir/descer perfil foi alterada. Coordenar a região
  com F32. `Player` acrescenta watermark, reset e gate inicial; não muda o relógio.
- `h264.hpp`: holder compartilhado dos planos. `gfx_video_memory.hpp`/shaders não mudam.
- CMake do PC/Xbox: flag opcional da sonda; link necessário de `player-state-test` com Cast.
  `client/ffmpeg.cmake`/build FFmpeg: identidade de fonte/compilador e invalidação de objetos.
- `tools/ffmpeg-x360/`: fontes novas LGPL, hooks nos inicializadores, scanner. Preservar no kit
  de fonte/relink e no aviso THIRD_PARTY. Nenhum arquivo GPL foi incorporado.
- `bench/cpu-floor/`: herdado de F30; se integrar sobre F30, manter apenas os pequenos ajustes
  de driver/configuração/validação, sem duplicar os relatórios baseline como novos resultados.

## 5. Divergências relevantes

- App ainda usa **quatro workers por padrão**. As configurações de bancada são explícitas;
  a escolha do default de 1080p deve seguir o vencedor no console.
- NONREF e superfícies diretas já existiam: não foram anunciados como otimizações novas.
- Há AVBufferRef/alocações internas FFmpeg ainda por quadro. Reduziram-se holders 3→1;
  não se afirma ter removido toda alocação ou toda cópia do fallback.
- O build atual do SDK exige GCC52, divergindo do prefixo padrão do COMUM. O conjunto foi
  recompilado; os números da F30 continuam baseline histórico e o A/B atual controla essa diferença.
- A meta exige número de console e cabe somente ao coordenador coletá-lo pelas regras lidas.
  **1080p30 no Xbox continua pendente, não declarado pronto.**
