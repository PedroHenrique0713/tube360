# Player extras: bancada isolada

O executável de prova reutiliza `tube360-main` (o main, o player e os shaders do produto).
Só o transporte HTTPS é substituído. Nenhum pedido chega ao YouTube, à conta do dono,
ou a um Nest real. Os receptores Cast são TLS em loopback.

```sh
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j4 --target tube360 player-extras-test player-extras-worker-test player-extras-bench tube360-extras-fixture h264-cadence-test player-state-test
client/build/player-extras-test
client/build/player-extras-test --shape bench/player-extras/fixtures/public-shape-sanitized.json
client/build/player-extras-worker-test
client/build/h264-cadence-test
client/build/player-state-test
python3 bench/quality/prepare.py .work/extras-media --mode 60 --seconds 24
python3 bench/player-extras/e2e.py comments-pt comments-en comments-scroll comments-page2 comments-error
python3 bench/player-extras/e2e.py dislikes dislikes-offline speed-panel speed-150 speed-paused loop base-100
python3 bench/player-extras/e2e.py comments-long comments-back nest-speed nest-roundtrip
python3 bench/player-extras/e2e.py comments-es comments-sd comments-sd-long comments-sd-error comments-large speed-es speed-sd speed-large loop-timer
```

Para a live sintética, copie `72060.mp4`, `720.mp4`, `1080.mp4` e `audio.m4a` da pasta de mídia
para `.work/extras-live/`, rode `python3 bench/quality/prepare.py .work/extras-live --mode live --seconds 24`
e então `python3 bench/player-extras/e2e.py live-disabled`. O título `F15` da mídia sintética
vem do gerador compartilhado; a interface e os recursos são os desta branch.

As capturas ficam em `.work/extras-e2e/*.png`, com logs por caso e `results.json` acumulado.
O teste de áudio usa tons estéreo independentes de 440/660 Hz, verifica duração e frequência
nas oito velocidades e rejeita PCM inválido. O teste de worker exercita troca de vídeo durante
pedido bloqueado, comandos separados de cabeçalho/corpo e recuperação de falha na continuação.
O teste de Cast confirma STOP ao mudar para 1,5x e um novo LOAD ao voltar para 1x.
A rodada combinada verifica idioma/região dos comentários em espanhol, dimensões 640×480,
retomada audível na TV, pausa preservada e prioridade do timer nativo sobre repetição ligada.
`loop-timer` termina pelo timer (sem captura e sem desligar o PC); os demais casos geram PNGs.

`public-shape-sanitized.json` conserva a forma de uma resposta pública observada em 2026-11-24,
com cabeçalho e corpo usando o mesmo `targetId`; identificadores, autores, textos e tokens foram
substituídos. Os demais arquivos são dados inventados. Sem credenciais ou cookies.

Consulta pública opcional, **não faz parte da suíte**: `client/build/player-extras-test --public <id>`.
Faz poucos pedidos anônimos pelo transporte TLS do produto. `XT_EXTRAS_TRACE=<arquivo>` salva
somente a resposta pública de `/next` para diagnóstico local; não versionar esse arquivo bruto.
Esquema do token: [YouTube.js](https://github.com/LuanRT/YouTube.js/blob/main/protos/misc/params.proto).
Contagem estimada e atribuição: [Return YouTube Dislike](https://github.com/Anarios/return-youtube-dislike#api-documentation).

## CPU

```sh
client/build/player-extras-bench .work/extras-media/72060.mp4 .work/extras-media/audio.m4a 1
client/build/player-extras-bench .work/extras-media/72060.mp4 .work/extras-media/audio.m4a 1.5
```

O bench decodifica H.264 720p60 com quatro threads e AAC 44,1 kHz estéreo e usa a mesma WSOLA
aplicada pelo player. Aplica o descarte de apresentação do produto e `AVDISCARD_NONREF` aos
pacotes dispensáveis. Conta CPU do processo e das threads por `getrusage`, sem rede, desenho,
pacing ou hash de todos os pixels ocultando o trabalho do codec. `cpu_percent_one_core` divide
a CPU pelo tempo real necessário para apresentar os 24 segundos na velocidade escolhida;
100% representa um núcleo, podendo passar disso. A mídia ultrafast tem referências em todos os
quadros; não representa todos os GOPs do YouTube. O resultado do PC não estima a CPU do Xenon.
