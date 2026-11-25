# F33 — CPU e reprodução 1080p30

Candidato, **aguardando medição no console**. Não qualifica 1080p30 por resultado de PC.
Os logs Xbox em `../cpu-floor/results/xbox-run-*.txt` são a baseline **F30**, anterior ao F33.
`results/baseline-console.json` resume as nove repetições por carga: FULL 36,675/41,314 ms
nos sintéticos 1080p30 de 2,5/5,5 Mbit/s; filtro desligado 30,726/35,560; entropia
9,478/13,275. É preciso reduzir FULL em 23,7%/32,2% para chegar a 28 ms.

O SDK atual exige o compilador do milestone 52 (`xsdk-gcc-52`); o prefixo antigo
falha em `tests/doloop_check.c`. A identidade inclui GCC e cc1, e a troca limpa os
objetos FFmpeg, pois Make não os invalida somente por mudar CC.

## O que o candidato muda

- FFmpeg 7.1.5 LGPL: chroma MC 8/4/2 (64 fases, put/avg), qpel luma 8/4/2 (16 fases;
  avg só 8/4, como upstream), weight/biweight 16/8/4/2. Usa VMX com shifts/somas,
  sem multiplicações inteiras ausentes no Xenon. Qpel 16/IDCT/deblock já existentes continuam.
- FIR bidimensional conserva int16 antes do segundo eixo e int32 no segundo, sem clipping
  intermediário. Stores de 2/4/8 bytes preservam o bloco vizinho. Teste usa o C oficial como oráculo.
- `mask=1` ativa chroma; `3` acrescenta qpel; `7` acrescenta pesos; `0` conserva o dispatch anterior.
  O app usa 7; só a bancada altera a máscara, sempre antes de abrir/juntar os workers.
- Kernels compilados em O2: unidade de 70.996 B de texto, contra 177.960 B em O3. É redução
  estática de código no prefixo antigo; o GCC52 final tem 69.760 B. Ganho de tempo ainda depende do console.
- CMake verifica identidade das fontes/receita antes de reutilizar FFmpeg do app. Um arquivo
  `libavformat.a` antigo já não basta para esconder uma atualização dos kernels.
- O get_buffer2 mantém uma única posse compartilhada para os três planos, com liberação atômica;
  reduz três alocações de holders para uma, mantendo as três AVBufferRef exigidas pelo layout.
- Somente 1080p de até 30,1 fps: fila 18/início 12 quando o kernel informa ≥96 MiB livres.
  Demais fontes continuam em 10/6. Oito quadros extras têm orçamento de até 32 MiB e sobra
  pretendida de 64 MiB. Pool mapeado permanece limitado a 160 MiB. Ver `MEMORIA.md`.

Nenhuma política de subir/descer perfil foi alterada. NONREF já existia; o ensaio FULL é
separado do NONREF para não atribuir ao VMX um ganho do filtro. Não há GPU nova ou readback.
A decisão de avançar ao híbrido depende dos resultados CPU abaixo.

## Compilar e verificar localmente

Use os caminhos públicos do SDK, sem configuração/conta pessoal:

```bash
export XSDK_DIR=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/vendor/xsdk
export XSDK_TOOLCHAIN=/home/pedrohenriquequadro/xsdk-gcc-52
export V4X_DIR=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/vendor/vulkan4xenon
export SDL3_XENON_DIR=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/vendor/SDL-xenon/build-xsdk
export TUBE360_BUILD_ENV=/dev/null
JOBS=3 flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G bash bench/1080p/build.sh xbox
TUBE360_F33_BENCH=ON flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
python3 bench/1080p/stage-console.py
```

A F30 é herdada por arquivos, com mudanças mínimas: argumento opcional do filtro FULL,
aceitar taxa fracionária explicitamente declarada para o 137, e caminho de log configurável
no verificador. Os modos/filtros/tempos/contadores e executável F30 padrão permanecem iguais.
A instrumentação fica na biblioteca privada da bancada, nunca na do app.

```bash
PATH=/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f5-h264/.work/src/usr/bin:$PATH \
  JOBS=2 bash bench/1080p/build.sh pc
bench/1080p/build-pc/f33-cpu > bench/1080p/results/pc-run.txt
python3 bench/1080p/summarize.py bench/1080p/results/pc-run.txt bench/1080p/results/pc-summary.json
python3 bench/1080p/check-summary.py
```

`f33-cpu` lê `f33-config.txt` no Xbox, ou `.work/config.txt` no PC: **máscara workers nonref**.
Exemplo `1 6 0`: só chroma, seis FRAME workers, filtro completo. Limites: máscaras 0–7,
workers 4–6, nonref 0/1. Ausente: `7 6 0`. São três repetições alternadas baseline/candidato,
com duas auditorias prévias por clipe, fora dos tempos. A cada abertura todos os workers são
juntados antes da troca de máscara. RAM/demux, aquecimento/cauda e relógios são os da F30.

Mídia: preserve os cinco MP4 da F30 (hashes em `../cpu-floor/results/fixtures.json`).
`youtube137.mp4` é o arquivo original F5, **sem reencode**, vídeo `4p_9OPKgcDE`, itag 137,
1920×1080 High@4.0, 600 quadros, 30000/1001 nominal, 20,019 s, hash
`7d6bbeb0f539770abf9f42b5d620bb25a184ad1c62849bbb6f5cc3f6692664c2`.
Ele já estava no host; não houve novo download de mídia. Seus pacotes alternam 33/34 ms;
a tolerância de 1 ms é exclusiva dessa fixture declarada fracionária. Os sintéticos continuam
com tolerância de 10 µs. Copie-o como `bench/cpu-floor/media/youtube137.mp4`.

Para a prova PowerPC local, extraia `qemu-user-static-ppc` em `.work/qemu/root` (sem instalar
no sistema) e rode `bash bench/1080p/check-kernels.sh` após compilar FFmpeg. `QEMU_PPC` permite
outro caminho. O harness usa os intrínsecos reais e os templates C oficiais 7.1.5. QEMU PPC32
não executa a ABI xsdk com instruções escalares PPC64; só nessa sonda usa-se ABI SysV/PPC32.
Os XEX abaixo exercitam a ABI e o compilador reais do Xbox. Emulação não mede velocidade.

## Coordenador: execução no console

**Estes comandos não foram executados pelo F33.** Feche o app e use pastas temporárias;
não substitua o Tube360 instalado. Os XEX encerram sozinhos; log junto ao nome do XEX.
Sem `dbgprint.txt` ou `net_log.txt`: logging síncrono/UDP não participa do tempo.

```bash
F33_XBDM=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
F33_IP=192.168.1.50
F33_REMOTE='HDD:\Apps\Tube360-F33-CPU'
python3 "$F33_XBDM" "$F33_IP" mkdir "$F33_REMOTE"
python3 "$F33_XBDM" "$F33_IP" put bench/1080p/build-xbox/f33-cpu.xex "$F33_REMOTE\f33-cpu.xex"
for clip in 720p30-4000 1080p30-2500 1080p30-5500 youtube137; do
  python3 "$F33_XBDM" "$F33_IP" put "bench/cpu-floor/media/$clip.mp4" "$F33_REMOTE\$clip.mp4"
done
```

Antes de timing, rode `f33-kernels.xex` em outra pasta nova e guarde `f33-kernels.log`.
Exigir **`F33 KERNEL DONE PASS cases=1291520`**, sem FAIL/fault. A comparação cobre bytes
fora da partição, fonte intacta, desalinhamentos 0–15, strides 64/73/128/−64, 16 fases qpel,
64 chroma, pesos −128…128, denominadores 0…7 e offsets de biweight −256…254.

Meça nesta ordem, guardando o log **antes** do próximo lançamento:

1. `configs/mask1-6-full.txt` → chroma.
2. `configs/mask3-6-full.txt` → chroma + qpel.
3. `configs/mask7-6-full.txt` → todos os kernels.
4. `configs/mask7-4-full.txt`, `mask7-5-full.txt` → workers 4/5, contra os 6 já medidos.
5. `configs/mask7-{4,5,6}-nonref.txt` → política já existente do app, separada de FULL.

Exemplo de um lançamento (o stage gera os arquivos de configuração):

```bash
python3 "$F33_XBDM" "$F33_IP" put bench/1080p/.work/console/configs/mask1-6-full.txt "$F33_REMOTE\f33-config.txt"
python3 "$F33_XBDM" "$F33_IP" run "$F33_REMOTE\f33-cpu.xex"
# Esperar encerrar; reservar até 10 min por lançamento, sem reboot automático.
python3 "$F33_XBDM" "$F33_IP" get "$F33_REMOTE\f33-cpu.log" bench/1080p/results/xbox-mask1-6-full.txt
python3 bench/1080p/summarize.py bench/1080p/results/xbox-mask1-6-full.txt bench/1080p/results/xbox-mask1-6-full.json
```

Exigir quatro auditorias (incluindo todos os pixels dos 600 quadros do 137), 24 medições, afinidades
**2,4,3,5,2,4** até o número de workers solicitado, status zero, nenhum FAIL,
`F33 DONE PASS audits=4 results=24`. O resumo exige matriz completa e três repetições;
recusa contagem, máscara, plataforma e afinidades misturadas, NaN ou auditoria ausente.
`cpu_gate=target_met` exige **todas** as repetições 1080p com média ≤28 ms e blocos de 30
quadros ≤33,333 ms. O PC sempre retorna `pending_console`. Isso ainda não qualifica o app.

A régua original também foi compilada em `bench/cpu-floor/build-xbox/cpu-floor.xex`;
rode como descrito no README F30, com os cinco clips, e compare seu modo **full** ao F30
anterior. O `stage-console.py` reúne tudo em `.work/console/floor`.

## Coordenador: reprodução do app

O stage gera `.work/console/app`, com as quatro fontes, XEX, `autoplay.txt` contendo
**`4p_9OPKgcDE 31`**, perfil 1080p30, saída 1080, som TV, sem servidor/conta/Nest.
Use uma terceira pasta nova. Exemplo de transferência:

```bash
F33_APP='HDD:\Apps\Tube360-F33-App'
python3 "$F33_XBDM" "$F33_IP" mkdir "$F33_APP"
for file in bench/1080p/.work/console/app/*; do
  python3 "$F33_XBDM" "$F33_IP" put "$file" "$F33_APP\${file##*/}"
done
python3 "$F33_XBDM" "$F33_IP" run "$F33_APP\tube360.xex"
# 10 s de aquecimento + 120 s medidos, depois sai sozinho; timeout total inclui abertura.
python3 "$F33_XBDM" "$F33_IP" get "$F33_APP\tube360.log" bench/1080p/results/xbox-app-full.txt
```

`decode-config.txt` inicia em `1 6 1 0` (mapeado, seis FRAME, FULL). Ajuste workers conforme a
bancada. Depois repita com `1 <workers> 1 1` para NONREF. São **duas aberturas** públicas,
sem conta/cookies; espaçar e não repetir extrações em laço. O vídeo foi escolhido por sua
fixture 137 preservada; disponibilidade anônima atual deve ser confirmada no lançamento.

Exigir fonte/itag **137 1920×1080 ~29,97/30**, sem redução de perfil; ≥29,5 fps efetivos,
zero quadros perdidos após aquecimento, PCM contínuo, filas sem queda sustentada, superfícies
mapeadas, memória livre e `F33 APP DONE PASS`. A sonda reporta diferença entre PTS de vídeo
e relógio PCM (p95 ≤50 ms, máximo ≤100 ms); isso não substitui ouvir/ver sincronismo labial
na TV, nem mede a latência externa da TV. Memória precisa manter ≥64 MiB livres amostrados.
Para saída 1080 nativa: HDMI/dashboard em 1080p progressivo e log `display` correspondente.

O build normal mantém `TUBE360_F33_BENCH=OFF`. A sonda só atua num build ON com
`f33-playback.txt` presente (`120 10` = medição/aquecimento). Não altera a escada de qualidade.
A escada pode baixar por pressão: se baixar, o ensaio **falha** e o log é devolvido para otimizar.
Não desabilite a escada para fazer a prova passar.

Se CPU não fechar, devolver os logs por estágio para priorizar o híbrido F30. Não há evidência
para escolher GPU antes dessa medição; não foi copiado/traduzido código dos projetos GPL.
