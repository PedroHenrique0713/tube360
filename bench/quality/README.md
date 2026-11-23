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

mkdir -p .work/quality/assets .work/quality/home .work/quality/proofs
cp client/assets/inter-*.ttf .work/quality/assets/
printf 'lang=pt\nquality=720\nsound=tv\nserver=off\n' > .work/quality/assets/settings.txt
python3 bench/quality/prepare.py .work/quality/media --mode 1080
HOME="$PWD/.work/quality/home" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
  XT_ASSETS="$PWD/.work/quality/assets" XT_QUALITY_FIXTURES="$PWD/.work/quality/media" \
  XT_PLAY=4p_9OPKgcDE XT_SHOT="$PWD/.work/quality/proofs/player.ppm" XT_SHOT_MS=8000 \
  XT_KEYS_MS=1000 XT_KEYS=......R client/build/tube360-quality-fixture
```

Repita `prepare.py` e o app com `--mode 60` e `--mode live`. Para a troca automática, volte a `--mode 1080`,
acrescente `XT_QUALITY_SLOW_MS=2000` ao app e capture depois de 35 s. O atraso só afeta as ranges do MP4 1080p;
a reserva 720p e o AAC continuam rápidos. Procure `fila sustentada`, `video reaberto`, mudança de dimensão,
PTS que continuam avançando e ausência de novo `primeiro quadro` ou de abertura do AAC na troca.
Para examinar os PTS, escreva `1` em `assets/video-metrics.txt` antes de abrir o app.

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

- `0` ou ausente: resumos de decode e upload a cada 5 s, sem sondas GPU;
- `1`: também PTS, dimensões, fila, cópia de planos/staging e tempos CPU de submit/present por quadro;
- `2`: também timestamps GPU para transferências e desenho, lidos depois da fence, quando suportados.

No Xenos os timestamps posteriores ao topo esperam o trabalho terminar. Meça com `2` e repita com `0` para
quantificar o custo da sonda. `desenho` inclui a interface e o resolve. Staging, CPU de `vkQueueSubmit`, espera
de present e GPU são medidas distintas; não some tempos CPU/GPU sobrepostos como se fossem sequenciais.
As estatísticas `decode ms/s de video` incluem send/receive e esperas dos workers do FFmpeg, excluem rede,
espera por espaço na fila e cópia dos planos, informada separadamente. Filtrados de 60→30 não são perdidos.

Sem servidor, qualidade 720p, som na TV: reproduza **2 minutos cada** de um VOD com itag 137 (1080p30),
um VOD com apenas 720p60 e uma live 720p60. Confirme a fonte no primeiro log e no status da imagem.
Anote fila mínima, perdas por janela, PCM, ms/s de vídeo, staging, CPU de submit/present e GPU.
No VOD 1080p, duas janelas consecutivas de pressão provocam a troca para a reserva sem reiniciar AAC/relógio.
Pause e continue durante o ensaio: uma pausa deve zerar a sequência de pressão. A troca preserva os quadros
prontos e descarta o pré-roll da nova fonte antes do ponto de entrega. A rede/seek e a recriação de texturas
ainda podem esgotar a reserva; a continuidade imperceptível precisa ser conferida no console.
Não há apresentação experimental a 60 Hz: ela depende de provar folga de decode + upload + desenho no Xbox.
