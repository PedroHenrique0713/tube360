# Prova da qualidade do modo console

Os executáveis são opcionais e não entram no `.xex`. O `tube360-quality-fixture` compila o mesmo `main.cpp`,
player e shaders do app; só substitui `yt::https` por um transporte local. Não conecta ao YouTube nem usa cookies.
`prepare.py` produz MP4/HLS sintéticos e o JSON do player. Não mede a complexidade de um vídeo real do YouTube.

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360-quality-fixture video-quality-test h264-cadence-test yt-contract-test yt-media-test
client/build/yt-contract-test bench/standalone/fixtures
client/build/yt-media-test
client/build/h264-cadence-test
cmake --build client/build -j3 --target player-state-test
client/build/player-state-test

mkdir -p .work/quality/assets .work/quality/home .work/quality/proofs
cp client/assets/inter-*.ttf .work/quality/assets/
printf 'lang=pt\nquality=720\nsound=tv\n' > .work/quality/assets/settings.txt
printf 'video_mode=1080p30\noutput_height=1080\naudio_preference=original\n' >> .work/quality/assets/settings.txt
python3 bench/quality/prepare.py .work/quality/media --mode both --audio-tracks
HOME="$PWD/.work/quality/home" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XT_CAST_DISABLE=1 \
  XT_ASSETS="$PWD/.work/quality/assets" XT_QUALITY_FIXTURES="$PWD/.work/quality/media" \
  XT_PLAY=4p_9OPKgcDE XT_SHOT="$PWD/.work/quality/proofs/player.ppm" XT_SHOT_MS=8000 \
  XT_KEYS_MS=1000 XT_KEYS=......R client/build/tube360-quality-fixture
```

`python3 bench/quality/console_e2e.py .work/quality`
exercita conta e catálogo gravados, Home/Música/canal/player nas duas saídas, áudio original/dublado no Nest
TLS falso e troca pausada com imagem, posição e volume preservados. Requer a mídia `both --audio-tracks`
em `.work/quality/media`. Nomes de casos opcionais após o diretório permitem repetir uma prova.

Repita com `video_mode=720p60` e com `prepare.py --mode live`. Exercite cada fonte com `output_height=720` e `1080`.
Para a troca automática de fonte, volte a `--mode both` e `video_mode=1080p30`,
acrescente `XT_QUALITY_SLOW_MS=2000` ao app e capture depois de 22 s. O atraso só afeta as ranges do MP4 1080p;
a reserva 720p e o AAC continuam rápidos. Procure `fila sustentada`, `video reaberto`, mudança de dimensão,
PTS que continuam avançando e ausência de novo `primeiro quadro` ou de abertura do AAC na troca.
Para examinar os PTS, escreva `1` em `assets/video-metrics.txt` antes de abrir o app.
Para pressão no consumidor, `XT_QUALITY_RENDER_MS=20` atrasa apenas o present do executável de prova.
Em `720p60`, três janelas ruins devem reduzir a apresentação para 30 sem reabrir AAC ou descartar a fila.

Com as duas faixas da fixture, `XT_KEYS=..ULAD XT_KEYS_MS=400 XT_SHOT_MS=2800` abre Áudio e destaca português;
acrescente `A` e capture depois de 5 s para aplicar. O log deve registrar `pt-auto` no ponto da troca. O original
é um tom de 1000 Hz e a dublagem, 440 Hz. `audio_preference=interface` em pt seleciona a segunda já na abertura.

## Framebuffer nativo e interface

`output_height=1080` cria swapchain 1920×1080; vértices usam escala 1,5 e fontes/ícones são rasterizados nessa
escala. Não há uma imagem de interface 720p ampliada. `XT_OUTPUT_HEIGHT=1080` faz o mesmo no `video-quality-test`.
Um tabuleiro YUV com alternância de um pixel, desenhado em 1080p, deve preservar cada pixel sem cinzas;
em 720p ele é reduzido e não pode passar nessa contraprova. A fonte 1080 em saída 1080 usa 3 leituras por pixel.

No Xbox, `tools/build-xbox.sh` prepara cópias privadas de xsdk/vulkan4xenon em `.work/native-display`.
O modo 1080 exige **HDMI e 1080p progressivo no painel do Xbox**: `VdQueryVideoMode` deve informar 1920×1080,
sem entrelaçamento. A escolha muda frontbuffers, swapchain e scaler para 1:1; não reprograma os timings HDMI.
Se o painel informar outro modo, mantém framebuffer 720p. O log deve mostrar `display: 1920x1080 shown 1:1`
e a saída HDMI 1920×1080. Teste também abrir/fechar o Guide e alternar 720↔1080 nas Configurações.

## Redução na GPU

```bash
ffmpeg -y -v error -f lavfi -i testsrc2=size=1920x1080:rate=30 -frames:v 1 -pix_fmt yuv420p -f rawvideo .work/quality/media/chart.yuv
ffmpeg -y -v error -f lavfi -i mandelbrot=size=1920x1080:rate=30 -frames:v 1 -pix_fmt yuv420p -f rawvideo .work/quality/media/fractal.yuv
for source in chart fractal; do
  SDL_VIDEO_DRIVER=offscreen client/build/video-quality-test .work/quality/media/$source.yuv 1920 1080 0 .work/quality/proofs/$source-bilinear.ppm
  SDL_VIDEO_DRIVER=offscreen client/build/video-quality-test .work/quality/media/$source.yuv 1920 1080 1 .work/quality/proofs/$source-filter.ppm
done
python3 bench/quality/compare.py .work/quality/media .work/quality/proofs
```

O controle bilinear e o filtro usam a mesma GPU, matriz BT.709 e quadro. O script compara RGB planar contra
`ffmpeg scale=lanczos`, na imagem inteira e no recorte central 640×360. O filtro faz quatro leituras bilineares
de Y, afastadas ±0,375 texel em cada eixo, e uma leitura de Cb e Cr: **6 leituras por pixel**. Sem redução são 3.
A crominância 4:2:0 já é menor que 1280×720. A intensidade do filtro é constante por desenho; o compilador Xenos
pode saltar o braço das quatro leituras quando a fonte é 720p. A matriz e a faixa continuam vindo de cada quadro.

`XT_QUALITY_FRAMES=120 XT_QUALITY_GPU=2` no `video-quality-test` mede staging, submissão CPU e timestamps de
upload/desenho por quadro. Repita com 720p e 1080p na mesma rodada. O programa informa a GPU usada. Medidas
do PC não estimam o Xenon/Xenos, sobretudo se o FFmpeg local estiver sem assembly ou houver builds concorrentes.

## Instrumentação do produto e ensaio no Xbox

Na pasta das fontes e do `.xex`, um `video-metrics.txt` opcional contém:

- `0` ou ausente: resumos de decode, apresentação, fila, perdas e upload a cada segundo, sem sondas GPU;
- `1`: também PTS, dimensões, fila, cópia de planos/staging e tempos CPU de submit/present por quadro;
- `2`: também timestamps GPU para transferências e desenho, lidos depois da fence, quando suportados.

No Xenos os timestamps posteriores ao topo esperam o trabalho terminar. Meça com `2` e repita com `0` para
quantificar o custo da sonda. `desenho` inclui a interface e o resolve. Staging, CPU de `vkQueueSubmit`, espera
de present e GPU são medidas distintas; não some tempos CPU/GPU sobrepostos como se fossem sequenciais.
As estatísticas `decode ms/s de video` incluem send/receive e esperas dos workers do FFmpeg, excluem rede,
espera por espaço na fila e cópia dos planos, informada separadamente. Filtrados de 60→30 não são perdidos.

Sem servidor, som na TV: reproduza **2 minutos cada** de futebol 1080p30 no perfil 1080p30,
um VOD 720p60 e uma live 720p60 no perfil 720p60. Repita os três em **cada saída**, 720 e 1080 (12 minutos).
Confirme a fonte e a saída no primeiro log e no status da imagem. No perfil rápido, confira ~60 apresentados/s,
e não apenas 60 decodificados/s. O relógio TV interpola somente dentro do bloco de PCM em reprodução;
um SDL com callback de 1024 amostras não pode quantizar o vídeo a 43 fps.
Anote fila mínima, perdas por janela, PCM, ms/s de vídeo, staging, CPU de submit/present e GPU.
No VOD 1080p, três janelas consecutivas de pressão provocam a troca para a reserva sem reiniciar AAC/relógio.
Pause e continue durante o ensaio: uma pausa deve zerar a sequência de pressão. A troca preserva os quadros
prontos e descarta o pré-roll da nova fonte antes do ponto de entrega. A rede/seek e a recriação de texturas
ainda podem esgotar a reserva; a continuidade imperceptível precisa ser conferida no console.
A apresentação a 60 Hz está ativa no perfil rápido e em Automático quando a fonte oferecer; a confirmação
de folga de decode + upload + desenho depende do Xbox. Não extrapole os tempos da GPU Intel para o Xenos.
Teste um vídeo com dublagem automática: original por padrão, troca nos dois sentidos no ponto atual,
pausa preservada, preferência por idioma da interface e a mesma faixa no Nest (nativo e servidor).
