# F30 — piso da CPU e decoder híbrido 1080p

2026-11-24, branch `f30-hibrido`, a partir de `f28-gpu-h264`.

## Situação e recomendação

**Bancada pronta, exercitada no PC e compilada para Xbox. Piso do Xenon ainda não medido.**
O brief proíbe esta frente de acessar o console e atribui a execução ao coordenador. Não há
um número novo de CPU do console neste relatório, nem substituição desse número por escala do PC.
O próximo passo executável é o [roteiro XBDM do README](README.md#roteiro-exato-do-coordenador--xbdm).

**Seguir com a medição no console; não iniciar o decoder completo nem prometer 1080p30 antes dela.**
Se a entropia entregue em regime ficar em ≥33,333 ms/quadro em algum clip 1080p30, o offload
dos pixels sozinho não atende essa carga. Até 29 ms/quadro, com blocos abaixo de 33,333 ms,
seguir apenas para um protótipo de exportação e de ondas intra/deblock. Entre esses valores,
medir clips mais longos e otimizar o parse/packing antes de investir na GPU inteira.

Mesmo um piso CPU favorável não fecha a outra metade: o desenho conservador com o backend
atual chega a **3.068 passes/resolve** no pior I4×4. É preciso reduzir essa serialização ou
alterar o backend e medir o resultado. O teste F28 do coordenador (**4,08 ms**, meio-pixel
horizontal da luma em 1920×1088) é evidência de um kernel; não mede a linha de montagem inteira.
**1080p60 continua sem demonstração**; piso CPU teria de caber em 16,667 ms, com alvo de 13 ms
antes de packing/áudio/UI. A decisão automática permanece `pending_console` nos resultados PC.

## 1. Bancada entregue

Tudo em `bench/cpu-floor/`, sem alterar player, FFmpeg do app ou outras bancadas:

- `build.sh` executa cópia byte a byte de `tools/build-ffmpeg-x360.sh` numa raiz privada,
  com os mesmos ajustes VMX e opções do app; depois aplica `prepare.py` e recompila.
  FFmpeg **7.1.5**, tarball SHA-256
  `de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`.
  Build Xbox: `HAVE_ALTIVEC=1`, `HAVE_PTHREADS=1`, `CONFIG_GPL=0`, `CONFIG_NONFREE=0`,
  `CONFIG_VERSION3=0`; scanner VMX novamente após a instrumentação.
- Três modos no mesmo executável e nas mesmas bibliotecas: reconstrução/filtro completos;
  reconstrução com `AVDISCARD_ALL` no filtro; retorno antecipado em `ff_h264_hl_decode_mb`
  com filtro pulado e pixels inválidos. O runtime exige FFmpeg 7.1.5/LGPL e seis threads de quadro.
- No corte preservam-se `list_counts`, limpeza de coeficientes, CABAC/CAVLC, DPB/POC,
  metadados de MVs/modos e progresso por linha exigido pelo direct temporal dos B-frames.
  Retirar só `hl_decode_mb` sem limpar o scratch não é um experimento válido.
  A dequantização feita dentro do parser permanece; não a contamos como trabalho removido.
- Auditoria separada compara PTS/tipo e quatro hashes canônicos por quadro: tipo/QP, NNZ,
  coeficientes, modos intra/MVs/referências usados. Contadores exigem todos os MB parseados e
  **zero reconstruídos** em entropy. Auditoria/MD5 não entram nas rodadas de tempo.
- Leitura e demux em RAM antes de medir; decoder aberto fora da janela. Três repetições de
  cada modo, com ordem rotativa; 30 frames de aquecimento e 12 de cauda excluídos do throughput.
  Relata média de entrega, p50/p95 de intervalos de entrega, p50/p95 do serviço da slice no worker
  e p50/p95 de blocos de 30 entregas. As unidades e diferenças estão no README.
- XEX sem SDL, GPU, áudio, rede ou interação: abre mídia em `game:\`, log junto ao XEX pelo xsdk,
  termina sozinho. Mesma afinidade do app: alimentador HW 1; workers **2,4,3,5,2,4**.
  São seis threads de software nos quatro HW reservados à decodificação pelo player.

### Clipes

`generate.py` usa FFmpeg de sistema com libx264, sem YouTube, mídia pessoal ou credenciais.
`testsrc2`, 6 s, High, YUV420 8-bit progressivo, CABAC, 8×8 DCT, uma slice, GOP de 2 s,
3 B, B-pyramid normal, 3 refs solicitadas, direct temporal, pesos P/B, preset medium,
VBV de 2 s/HRD CBR, scenecut/b-adapt desligados, duas threads do encoder.
O SPS anuncia quatro refs devido ao B-pyramid; foram verificados I, P e B nas saídas.

| Fonte final | Nível | Quadros | Alvo Mbit/s | Bitrate efetivo Mbit/s |
|---|---|---:|---:|---:|
| 720p30-4000 | 3.1 | 180 | 4,0 | 4,312801 |
| 1080p30-2500 | 4.0 | 180 | 2,5 | 2,642606 |
| 1080p30-5500 | 4.0 | 180 | 5,5 | 5,847084 |
| 1080p60-2500 | 4.2 | 360 | 2,5 | 2,711668 |
| 1080p60-5500 | 4.2 | 360 | 5,5 | 5,733696 |

Os quatro clips 1080p estão efetivamente entre **2,5 e 6 Mbit/s**, verificado pelo gerador.
Escolhi alvo alto de 5,5 porque o reservatório VBV em clips curtos permite média superior ao
alvo nominal. Metadados, parâmetros completos e hashes dos arquivos finais em
[`results/fixtures.json`](results/fixtures.json); conservar essa mídia com os logs, pois regenerar
comandos não garante identidade dos bytes de um encoder multithread/versão diferente.
Clipes, builds e fonte expandida são ignorados no Git e ficam neste worktree.

### Resultados PC — não escalar para o Xbox

Intel i5-1035G1, notebook compartilhado. O build PC usa o mesmo script do app **com SIMD x86
habilitado**, diferentemente do perfil C sem assembly da F28. Isso e a carga concorrente tornam
inválida a comparação direta dos tempos absolutos entre F28/F30 ou uma multiplicação por pixels.
Snapshot de ambiente em `results/host.json`; não é registro contínuo de temperatura/frequência.

Valores em ms, mediana das três repetições de cada estatística:

| Clip | Modo | Média/quadro entregue | Entrega p50 / p95 | Slice no worker p50 / p95 |
|---|---|---:|---:|---:|
| 720p30, 4,0 M | completo | 3,2562 | 2,5872 / 9,2255 | 15,983 / 24,981 |
| 720p30, 4,0 M | sem filtro | 2,4835 | 1,8152 / 7,7950 | 12,203 / 19,383 |
| 720p30, 4,0 M | entropia | 1,6457 | 1,1015 / 4,7754 | 7,295 / 11,461 |
| 1080p30, 2,5 M | completo | 2,5966 | 2,0296 / 8,0099 | 14,244 / 20,002 |
| 1080p30, 2,5 M | sem filtro | 2,3189 | 1,4773 / 6,9276 | 12,535 / 18,461 |
| 1080p30, 2,5 M | entropia | 1,2535 | 0,8285 / 3,6543 | 5,529 / 9,257 |
| 1080p30, 5,5 M | completo | 2,6912 | 2,3427 / 6,5119 | 14,649 / 19,526 |
| 1080p30, 5,5 M | sem filtro | 2,1672 | 1,9412 / 5,6554 | 12,102 / 14,912 |
| 1080p30, 5,5 M | entropia | 1,4203 | 0,9062 / 4,9282 | 7,338 / 11,140 |
| 1080p60, 2,5 M | completo | 3,1890 | 2,1258 / 10,2955 | 16,395 / 26,527 |
| 1080p60, 2,5 M | sem filtro | 2,3522 | 1,5584 / 7,4499 | 12,243 / 18,567 |
| 1080p60, 2,5 M | entropia | 1,1159 | 0,6820 / 3,6088 | 4,763 / 8,040 |
| 1080p60, 5,5 M | completo | 3,7335 | 2,7373 / 11,4933 | 19,665 / 30,652 |
| 1080p60, 5,5 M | sem filtro | 2,7976 | 1,9738 / 8,9715 | 14,370 / 21,926 |
| 1080p60, 5,5 M | entropia | 1,7114 | 1,0057 / 5,3744 | 7,379 / 11,858 |

O serviço de seis workers se sobrepõe; seu p95 não é o tempo por quadro em regime. A entrega
também tem rajadas, por isso o intervalo p95 não é latência de decode. Não dividir serviço por
seis nem tratar mediana de p95s como p95 combinado. O controle 720p saiu mais lento que alguns
1080p, evidenciando carga do host/custo de conteúdo; isso não é uma curva de escala confiável.
Valores individuais, mínimo/máximo e blocos em [`results/pc-run.txt`](results/pc-run.txt) e
[`results/pc-summary.json`](results/pc-summary.json).

### Piso CPU no console — pendente do coordenador

| Carga | completo | sem filtro | só entropia/parse |
|---|---|---|---|
| 720p30 controle | não medido | não medido | não medido |
| 1080p30, 2,5 / 5,5 M | não medido | não medido | não medido |
| 1080p60, 2,5 / 5,5 M | não medido | não medido | não medido |

O executável gera esses números com p50/p95; `summarize.py` só recomenda com `platform=xbox`.
Precisam de três lançamentos com 15 auditorias/45 resultados cada, sem erro de afinidade,
guardados separadamente. Roteiro com mkdir/put/run/get, pasta temporária e nomes de log no README.

## 2. Linha de montagem

[Desenho completo, ABI compilável, passes, memória e relógios](PIPELINE.md).
Números principais, todos de projeto:

- 1920×1088 = **120×68 = 8.160 MB**. Exportar no início de `hl_decode_mb`, antes de IDCT/MC,
  e extrair levels antes de `level*qmul` somente quando mover também a dequantização.
- Descritor por MB **320 B** + coeficientes **800 B** int16 + cabeçalho de quadro **64 KiB**:
  **9.204.736 B/quadro**, **276,142 MB/s de stores** a 30 fps, sem custo de flush/leitura GPU.
  Alternativa int32: **15.732.736 B/quadro**, 471,982 MB/s. Nenhum desses stores está medido no piso.
- Atlas RGBA8 com quatro amostras nos canais: **3.133.440 B** lógicos, reserva de **4 MiB**.
  É uma proposta diferente do F28, que repetia a luma em RGBA. Packing/ranges ainda precisam de QA.
- Pipeline conservador: **8 + 2×W_intra + 4×W_deblock** passes. Intra luma 4×4 em
  `t=x+2y`, até **1.022 ondas**; deblock por MB no mesmo escalonamento, até **254 ondas**.
  Pior I4×4: **3.068 passes**; tudo inter: até **1.024**. Evitar feedback de attachment com
  ping-pong/cópias desenhadas, mantendo ordem e bordas normativas. Não fazer readback.
- Resolve do atlas completo em cada um dos 3.068 passes escreveria **9,613 GB/quadro**,
  **288,4 GB/s a 30 fps**; a versão ingênua não é um caminho de performance. Áreas parciais
  e outro agendamento precisam ser construídos/medidos. Compute não está implementado no backend.
- **9 slots de comandos**, seis em parse + dois esperando + um na GPU; objetivo de até três
  períodos extras pós-parse (**100 ms a 30 fps**). Backpressure e fences para reutilização.
- Protótipo int16 conservador: **265 MiB**, reservar teto **288 MiB**, incluindo planos CPU ainda
  duplicados e margem. Int32: **328 MiB**, teto 352. Removendo os planos CPU: 201/264 MiB.
  Medir disponibilidade real dos 512 MiB unificados; o pool atual de 160 MiB não cobre tudo isso.
- PTS originais preservados. Liberar quadro só GPU-ready; segurar o início do PCM até o watermark
  existente de seis quadros. `tvClock` continua seguindo áudio efetivamente consumido.
  Retenção extra de 100 ms PCM custa **35.280 B**. Nest exige iniciar/resumir áudio após preparar
  vídeo; `nestOffset` do usuário não deve compensar o pipeline. Detalhes de pausa/seek/underrun no desenho.

Não foi escrito nem copiado shader H.264 novo nesta frente, nem código dos projetos GPL.
A matemática/ordem do desenho vêm da norma e do FFmpeg LGPL; não há módulo Microsoft.

## 3. Provas executadas

Comandos na raiz do worktree (saídas completas em `results/`):

```bash
python3 bench/cpu-floor/generate.py
JOBS=3 systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=512M bench/cpu-floor/build.sh pc
bench/cpu-floor/build-pc/cpu-floor --trace bench/cpu-floor/.work/trace-pc
python3 bench/cpu-floor/summarize.py bench/cpu-floor/results/pc-run.txt bench/cpu-floor/results/pc-summary.json
python3 bench/cpu-floor/check.py
```

Resultado da execução final: **15 auditorias, 45 medições**, `F30 DONE PASS audits=15 results=45`.
Cada clip é decodificado 12 vezes: 3 auditorias + 9 medições, total **15.120 quadros** entre os
cinco clips. A validação de pixels externa cobre os dois modos com reconstrução, **2.520 quadros**,
zero diferenças de MD5 por quadro em relação ao FFmpeg 8.1.3 de sistema, incluindo I/P/B.
O filtro desligado muda pixels, como esperado; os quatro hashes de parse permanecem iguais.
Entropy tem zero MB reconstruídos e nenhum MD5 de pixels (não lê memória inválida).

```text
F30 CHECK PASS pixel_frames=2520 mismatches=0 parse_traces=15 rejected_logs=6 truncated_media_rejected=1
```

O checker também prova que o resumo rejeita: log sem DONE, sem auditoria, plataforma misturada,
FAIL explícito, duas execuções concatenadas e repetição ausente. O binário rejeitou um MP4
truncado, sem produzir DONE PASS. Evidência em `results/verification-pc.json`, incluindo hashes
das 15 traces; as traces brutas estão em `.work/trace-pc/` e são ignoradas no Git.

Não há print: é bancada sem tela. A evidência visual é a comparação de todos os pixels visíveis
de 2.520 quadros com oracle externo, não uma captura selecionada. Não testei apresentação na TV.

Build Xbox executado com trava compartilhada e limite de memória:

```bash
XSDK_DIR=<forcepush-360>/vendor/xsdk \
XSDK_TOOLCHAIN=~/xsdk-gcc JOBS=3 \
flock ~/.cache/tube360-frota-xbox.lock \
systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=512M \
bench/cpu-floor/build.sh xbox
```

Terminou com código **0**, `[100%] Built target cpu-floor`. FFmpeg original mais instrumentação
compilados com PowerPC GCC 15.3.0; xsdk `8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28`.
O build preserva warnings upstream de GCC/FFmpeg e o scanner Xenia avisa **56 instruções que
o emulador ignora**. Isso não é execução em Xenia nem prova de instrução inválida no console.
O verificador VMX final encontrou zero multiplicações incompatíveis nos objetos avaliados.

```text
Artefato: bench/cpu-floor/build-xbox/cpu-floor.xex
Tamanho: 4.386.816 bytes
SHA-256: 454aa4c4c6af5f05e663b00a7736b30f1ef5a0c8eba0d0340d6417b91fcf390a
ELF: 32-bit MSB, PowerPC, estaticamente ligado
XEX: Microsoft Xbox 360 executable
```

Metadados em [`results/xbox-artifact.json`](results/xbox-artifact.json). Binário ignorado no Git,
presente no caminho absoluto
`<repo>/bench/cpu-floor/build-xbox/cpu-floor.xex`.
Se integrar só commits em outro lugar, recompilar com o README. Não foi instalado no console.

Checagens auxiliares: `bash -n`, `python3 -m py_compile` dos quatro scripts, compilação C++
com `-Wall -Wextra -Werror`, asserts de ABI nos dois alvos e `git diff --check`.
Os snapshots de config removem apenas whitespace ao fim de linha; os originais do build
continuam intactos na raiz privada. Não rodei as suítes de produto alheias à bancada.

## 4. Não verificado e limites

- Execução/tempos/p95 no Xbox; o item de **piso medido no console** depende do coordenador.
- Exportação/flush dos 9,2–15,7 MB por quadro, reconstrução GPU completa, correção Xenos dos
  algoritmos propostos, latência de ondas/resolve, uso de memória e integração A/V/Nest.
- CABAC de fixtures sintéticas curtas não qualifica CAVLC, MBAFF, FMO, múltiplas slices, PCM,
  quantizações extremas, todos os scaling lists ou YouTube real. O corte existe nos caminhos
  CABAC/CAVLC por compartilhar `hl_decode_mb`; apenas o domínio exercitado recebe PASS.
- A auditoria verifica a preservação dos dados parseados entre modos; não é uma suíte normativa
  de decoder GPU. Uma slice/quadro é exigida para evitar confundir soma de serviços paralelos
  com latência de um frame e para não criar contadores compartilhados de slices sem sincronização.
- Sem medição de energia/temperatura em série e com outras frentes no notebook, os números PC
  mostram funcionamento da bancada, não capacidade de Xenon ou perfil universal de conteúdo.
- Não houve acesso a console/XBDM/FTP, Nest, conta YouTube, configuração/chaves privadas,
  serviços do produto, cache real ou VPS. O código de XBDM foi somente lido para escrever o roteiro.

## 5. Integração, divergências e commits

**Somente `bench/cpu-floor/` foi alterado.** Nenhum ponto de `main.cpp`, `i18n.hpp`, CMake do
cliente, scripts do app, arquivos de licença raiz ou deploy foi modificado. Risco de integração
baixo para as outras frentes; o novo CMake é independente. Builds expandem fontes próprias,
não reutilizam/modificam builds de outras worktrees. Sem push/publicação.

Divergências constatadas:

1. “Seis threads como o app”: `Player::h264Threads` é **4 por padrão**, configurável até 6.
   A bancada usa 6 conforme pedido, e o wrapper de afinidade do app, que reserva quatro HW.
2. O player usa filtro adaptativo `AVDISCARD_NONREF` quando relaxa, especialmente em 1080p;
   esta bancada usa ligado completo e desligado completo, como controles explícitos do brief.
3. “Só entropia” neste FFmpeg ainda faz dequantização nos parsers e mantém metadata/progresso,
   além de alocar frames e limpar scratch. Isso evita atribuir ganho a código que não foi retirado.
4. Foi possível medir a bancada completa no PC e gerar XEX; medir CPU no console seria violar
   a proibição do próprio brief. A entrega desse número está marcada pendente, com roteiro pronto.
5. A API gráfica atual não oferece compute/SSBO para eliminar naturalmente as ondas; não há
   suporte pronto equivalente a um decoder moderno de hardware para importar.

Commit desta frente de implementação/evidências: **`176650d`** —
`Bench: isolate H.264 CPU floor and validate parse-only decoding`.
O relatório é salvo em commit de documentação posterior. Histórico completo solicitado:

```bash
git log --oneline v032..HEAD
```

Inclui os três commits herdados da F28 (`59dfa09`, `fa0a638`, `fc147d8`), que não foram
reimplementados nesta frente, mais os commits F30. Nenhum código GPL foi portado/traduzido.
