# F36 — Árabe

Do repositório, com SDL3/Vulkan e FFmpeg de desenvolvimento (ou `FFMPEG_PREFIX` apontando a instalação local):

```sh
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 arabic-text-test arabic-render-test display-layout-test
client/build/arabic-text-test
SDL_VIDEO_DRIVER=offscreen client/build/arabic-render-test "$PWD/client/assets"
client/build/display-layout-test bench
python3 bench/arabic/prepare.py .work/arabic
python3 bench/catalog/build-replay.py .work/arabic
python3 bench/arabic/e2e.py .work/arabic
```

A máquina da frota reutilizou o FFmpeg já compilado de `f5-h264` em `FFMPEG_PREFIX` para poupar compilação;
nenhum arquivo desse outro worktree foi alterado. Todas as demais dependências/builds ficam neste worktree.
Os testes não leem `~/.config/tube360`, cache real, contas nem console. O executável de replay religa os objetos
do **produto** e substitui somente HTTPS. Pedidos sem gravação falham fechados. HOME e XDG são isolados.

- `arabic-text-test`: formas contextuais/isoladas, quatro lam-alef, quebra de junção, harakat, bidi misto,
  parênteses, IPv4/porta, números arábico-indianos, isoladores, UTF-8 inválido, locale, datas, traduções e cache.
- `arabic-render-test`: Vulkan/stb reais, carga sob demanda, fallback por caractere, medição de ligadura/marca,
  cobertura dos glifos de todas as traduções, corte/quebra dentro da largura e igualdade de desenho/medição.
- `display-layout-test`: regressão de SD/HD, espanhol, persistência e locale serializado; agora inclui árabe
  no cliente nativo reutilizado. A quarta opção do ciclo altera o retorno EN←ES para EN←AR.
- `e2e.py`: Home/Configurações/busca/player/erro em árabe, 720p/texto grande/4:3/1080p, três idiomas com títulos
  árabes, troca de teclado RB, B para voltar, troca de idioma e persistência, fonte ausente e controle sem árabe.

Capturas: `.work/arabic/<caso>/screen.png`; logs ao lado. Pode executar apenas casos nomeados após o diretório.
A lista da execução fica em `screenshots.json`. Abra as imagens: as asserções de log não certificam aparência.
O teste negativo exige que `latin-only` não carregue Noto; `missing-font` deve exibir aviso em inglês legível.

`fixtures/titles.json` contém três títulos públicos conferidos nas páginas YouTube indicadas em 2026-11-24,
com números, latim e árabe, e uma quarta linha sintética explícita para harakat/lam-alef. Não se trata de feed
anônimo real. Duração, capas em degradê e vídeo de teste são sintéticos; não se usa a mídia dos vídeos públicos.
O player usa somente barras de teste e tom de 440 Hz gerados localmente com FFmpeg (8 segundos).
Emojis e símbolos sem glifo são omitidos durante a preparação do cache por texto/fonte. O teste cobre
título só de emoji, árabe com emoji/ZWJ/ZWNJ, pergunta literal, corte/reticência/quebra e recarga da fonte.
A cobertura das traduções árabes é conferida contra o shaping cru, antes do filtro: a omissão não pode
mascarar uma letra árabe que falte no atlas. Há 100 mil hits sem nova consulta de cobertura e 10 mil
medições/ajustes de título sem nova preparação do texto.

A compilação do console usa o lock compartilhado (não executa nem instala no Xbox):

```sh
TUBE360_BUILD_ENV="$PWD/.work/arabic/build.env" \
  flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
python3 tools/package-xbox.py --version 0.3.2-f36 --output-dir .work/arabic/package
python3 tests/update/package-test.py .work/arabic/package/tube360-v0.3.2-f36-xbox360.zip
```

O arquivo de ambiente local contém somente caminhos de toolchain/SDK, nunca a chave privada do usuário.
Ver [limites, memória e roteiro do console](../../docs/ARABIC.md).
