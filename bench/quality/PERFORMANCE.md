# F17: memória de vídeo e bancada

O H.264 usa `get_buffer2` para escrever em três imagens R8 lineares mapeadas. O pool conserva a posse do DPB,
da fila do player e dos dois quadros em voo na GPU. Uma superfície só volta ao decoder quando todos soltarem
as referências. Os descritores são criados uma vez por superfície; o desenho troca o set, sem copiar pixels.

No Xbox, `build-xbox.sh` prepara a extensão **na cópia privada** do vulkan4xenon: tipo de memória 2 fisicamente
contíguo, cacheado e não coerente, amostragem linear R8 e flush explícito de linhas de 128 bytes. O decoder faz
o flush após receber o quadro completo, fora da thread de desenho. O pitch vem de `vkGetImageSubresourceLayout`.
A alocação cobre a altura alinhada do FFmpeg e 64 bytes adicionais; a extensão da textura usa a altura visível.
No desktop, o caminho depende de suporte a imagens R8 lineares amostradas com memória host-visible/cacheada.
Se o pitch de uma resolução não satisfizer o alinhamento do codec, o decoder usa os buffers do FFmpeg e staging
nessa resolução, mantendo a reprodução local. O layout recusado fica em cache para não recriar imagens por quadro.

O upload antigo é uma cópia dos planos no decoder, outra para staging no desenho e três cópias R8 CPU/tiled
executadas pelo driver durante `vkQueueSubmit`. O desenho já usava uma submissão por quadro. A via direta
remove essas cópias; o submit ainda executa desenho, resolve e invalidação dos caches da GPU.

## Controles para o coordenador

Arquivo opcional `decode-config.txt`, ao lado do `.xex` e das fontes, com quatro inteiros:

```text
1 4 1 -1
```

| Campo | Valores |
|---|---|
| Buffers diretos | `1` habilita, `0` usa staging como controle |
| Threads H.264 | `1` a `6`; padrão `4`; perfil 1080p30 usa `6` |
| Tipo | `1` FRAME; `2` SLICE (1080p30 sempre FRAME) |
| Loop filter | `-1` ou `1` política do perfil, `0` completo |

O perfil 1080p com taxa conhecida até 30,1 fps usa `AVDISCARD_ALL`; 720p e abaixo mantêm filtro completo.
`h264_loop_filter=full` em `settings.txt` restaura o filtro também no perfil 1080p30. A política é fixada
antes de abrir o decoder, inclusive nas trocas de fonte/seek, sem mudar no meio do GOP por pressão de fila.
A redução de 60 para 30 e a reserva 1080→720 continuam funcionando. Ver [F40](../1080p-app/README.md).

No Xbox, 1080p30 usa HW `0,2,4,1,3,5`; os demais perfis seguem HW `2,4,3,5`. Apresentação/UI ficam em HW0,
demux/AAC e SDL áudio em HW1; TLS de vídeo vai para HW3. Prioridades, Nest, lounge e fundo estão no mapa F40.
A afinidade aparece em `decod-worker` e `app-thread`; `app-audio` observa o feeder PCM real.
`decod-config` mostra o tipo efetivamente aceito pelo FFmpeg. Aceite do app Xbox F40 ainda pendente.

O build mantém IDCT H.264/deblock em AltiVec e habilita qpel 16 com multiplicações por 1/5/20 substituídas por
permutações, shifts e somas inteiras exatas. Chroma/weighted prediction e o IDCT genérico com multiplicações
incompatíveis continuam escalares. `check-vmx.py` recusa esses opcodes no assembly e exige qpel/IDCT H.264
vetoriais. `-O3` já era o padrão efetivo do FFmpeg; agora é explícito, com `-mcpu=cell -mtune=cell`, sem VSX/POWER8.
O scanner não substitui uma execução no Xenon.

## Provas locais

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360-quality-fixture video-memory-test h264-decode-bench h264-cadence-test player-state-test yt-contract-test yt-media-test
python3 bench/quality/prepare.py .work/f17/media --mode both
python3 bench/quality/performance.py .work/f17
SDL_VIDEO_DRIVER=offscreen client/build/video-memory-test .work/f17/media/1080.mp4 0 .work/f17/staging
SDL_VIDEO_DRIVER=offscreen client/build/video-memory-test .work/f17/media/1080.mp4 1 .work/f17/mapped
cmp .work/f17/staging-031.ppm .work/f17/mapped-031.ppm
SDL_VIDEO_DRIVER=offscreen client/build/video-memory-test .work/f17/media/1080.mp4 2 .work/f17/rejected
cmp .work/f17/staging-031.ppm .work/f17/rejected-031.ppm
client/build/h264-decode-bench .work/f17/media/1080.mp4 4 1 0
client/build/h264-decode-bench .work/f17/media/1080.mp4 4 1 1
```

Repita o teste de memória com `72060.mp4`. Ele exige todos os quadros apresentados e verifica que os pixels
de cada superfície ficaram intactos enquanto dez quadros aguardavam na fila. Os quadros 31, 91 e 151 são
capturados pelo shader real; os hashes e os PPMs devem coincidir entre staging e direto com o mesmo filtro.
O modo `2` força a recusa do allocator Vulkan e verifica a continuidade pelos buffers convencionais.

O bench imprime CSV: arquivo, largura, altura, threads, tipo solicitado/ativo, NONREF, pacotes, quadros,
tempo total ms, ms/quadro, ms/s de vídeo, hash e resultado. Sem argumento extra mede o loop inteiro sem pacing,
hash ou exportação entre os frames. Um sexto argumento é prefixo para exportar YUV e calcular hash no passe
visual; **não use o tempo desse passe para comparar velocidade**. Alterne controles, repita e use a mediana.

`performance.py` roda o produto sem servidor, conta, Nest ou YouTube reais, na mesma saída 1080p. Compara
staging/6t/filtro completo, direto/6t/completo, direto/6t/NONREF e direto/4t/automático. Os arquivos ficam em
pastas próprias com HOME e assets isolados. `prepare.py` usa fontes sintéticas simples; clipes reais locais
podem substituir os MP4s antes do prepare, que conserva os arquivos existentes. Declare sua procedência.

## Leitura das métricas

- `upload:`: bytes efetivamente copiados e staging devem ser zero na via direta. `submit-CPU` continua sendo
  tempo CPU do driver, distinto de GPU, fence, acquire e present.
- `pipeline:`: proporção direta, última espera de fence/acquire e gravação dos comandos. Os tempos de fence
  e acquire são amostras do último quadro, não médias da janela.
- `decod-memoria:`: diretos/total, flush na thread de decode, pacotes com filtro NONREF e tipo de threads.
- `decod-workers:`: no Xbox com FRAME, soma de tempo de parede **dentro** dos workers, incluindo espera de
  referências. Como os workers são paralelos, essa soma não é latência nem tempo puro de CPU; não a some ao
  submit. Contadores podem cruzar a fronteira da janela por alguns quadros.
- `qualidade:`: fonte, alvo, apresentados/s, perdas e fila continuam sendo o critério do produto. O antigo
  `decode ms/s de video` mede somente send/receive. Com a fila cheia, os workers trabalham enquanto o produtor
  espera espaço: o número pode ficar perto de zero. Ele não mede a CPU total consumida pelo decoder.

## Roteiro no Xbox (somente coordenador)

1. Fechar o app antes de instalar o XEX desta branch. No painel do Xbox selecionar **HDMI 1080p progressivo**.
   No Tube360: servidor desligado, som TV, saída 1080, `video-metrics.txt` com `0`. Guardar XEX/log/config anteriores.
2. Sem `decode-config.txt` (padrão `1 4 1 -1`), abrir o mesmo VOD de futebol em **1080p30 por 120 s** e um
   VOD **720p60 por 120 s**. Repetir com live 720p60. Cada teste inicia do mesmo ponto e com o mesmo perfil.
3. Conferir `display: 1920x1080 shown 1:1`, `tela 1920x1080`, fonte correta, `buffers diretos`, HW dos workers,
   `decod-memoria` com todos os quadros diretos, `staging 0.000` e proporção direta 100%. Depois dos primeiros
   5 s, anotar mediana/pior janela de apresentados/s, perdas, fila mínima, PCM, flush, submit, fence/acquire.
   O alvo é ~30/~60 **na apresentação** sem perdas sustentadas, sem troca para resolução/FPS menores.
4. A/B no mesmo vídeo, 120 s cada: `0 4 1 0` → `1 4 1 0` (só memória), depois `1 4 1 1` (só filtro).
   Reiniciar o app para ler cada config. Comparar FULL/NONREF no mesmo PTS, sobretudo grama, rede e arquibancada.
5. Se faltar folga, medir `1 2 1 1`, `1 3 1 1`, `1 4 1 1`, `1 6 1 1` e `1 4 2 1`, sempre 120 s por perfil.
   Validar o tipo ativo e o estado real de apresentação; uma fonte de fatia única pode não ganhar com SLICE.
6. Pausar/continuar, buscar ±1 min e alternar 1080p30↔720p60. A imagem deve manter cores, recorte, áudio e posição,
   sem linhas intermitentes, quadros antigos, falha de alocação ou aumento contínuo do pool. Abrir/fechar o Guide.
7. Repetir uma amostra com `video-metrics.txt=2`, se a GPU suportar timestamps, e voltar a `0` para medir o custo
   da sonda. Se continuar lento, separar worker/dependências, flush e GPU/resolve; não deduzir ganho do tempo Intel.

Não há resultado novo do Xbox nesta frente. Coerência da memória cacheada, leitura linear pelo Xenos, custo do
flush, qpel VMX e throughput sustentado de 1080p30/720p60 precisam dessa rodada no console.
