# CRT, texto grande e espanhol

A interface deriva a largura lógica da extensão real da swapchain. Em SD (até 576 linhas),
reserva 5% de cada borda e amplia as fontes automaticamente. 4:3 usa duas colunas;
texto grande em HD usa no máximo três. O vídeo ocupa o framebuffer inteiro e conserva
sua proporção; `video_fit=fill` recorta os lados ou o topo, sem deformar a imagem.

Configuração persistida em `settings.txt` (também disponível pelo controle):

```ini
lang=es
output_height=480
large_text=on
video_fit=fit
server=off
```

`large_text=off` mantém o tamanho normal em HD; SD sempre amplia o texto. `video_fit=fill`
ativa o recorte. Saídas: 480, 720 e 1080. O Xbox consulta `VdQueryVideoMode`: painel SD ou
4:3 seleciona framebuffer 640×480 (848×480 se o painel informa SD widescreen); 1080 nativo
exige HDMI e 1080p progressivo. 480i/480p segue o sinal escolhido no painel do Xbox.
O app conserva os timings e o entrelaçamento do painel, alterando framebuffer e scaler.
O PC usa janela 640×480 ao selecionar 480.

```sh
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360 display-layout-test yt-contract-test
client/build/display-layout-test bench
client/build/yt-contract-test bench/standalone/fixtures
python3 bench/catalog/prepare-replay.py .work/f22/replay
python3 bench/quality/prepare.py .work/f22/media --mode both --seconds 10
python3 bench/catalog/build-replay.py .work/f22
python3 bench/display/e2e.py .work/f22
PYTHONUSERBASE=/home/pedrohenriquequadro/.local /home/pedrohenriquequadro/.pyenv/versions/3.12.7/bin/python3 bench/display/server_e2e.py .work/f22
```

As capturas ficam em `.work/f22/<caso>/screen.png`. O replay substitui somente o transporte;
main, fontes, geometria, player e shaders são os do produto. Fixtures contêm metadados em
português/inglês; esses títulos e nomes são dados, não textos da interface. Não há conta real.
`server_e2e.py` usa exclusivamente 8152/8252, HOME/config próprios e encerra o servidor criado.

Conferir no console na integração: painel em 480i 4:3, 480p 4:3, SD widescreen e 720p;
log `display: dashboard ... interlaced/progressive ... framebuffer ...` seguido de
`tela ...`, `layout: 960x720 ... margem 32x24`. Verificar as quatro bordas, estabilidade
entre campos, Guide, grade e menus por controle; tocar vídeo 16:9 com faixas e preencher,
legendas e pausa. Em 720p, alternar texto grande e confirmar três colunas. Comparar 2 min
com a base na mesma mídia e registrar decode, perdas, upload e apresentação; PC não mede Xenos.
