# Testes de busca e feeds

Execute da raiz da worktree, usando o FFmpeg já instalado:

```sh
PATH="$HOME/.local/bin:$PATH" cmake -S client -B client/build -DFFMPEG_PREFIX="$PWD/.work/ffmpeg-pc"
cmake --build client/build -j6
python3 tests/f52/run.py --all
```

Sem `--all`, o runner executa somente o contrato F52. Também aceita nomes,
por exemplo `python3 tests/f52/run.py f52 account standalone reddit-contract`.
O caso H.264 gera mídias sintéticas com FFmpeg caso ainda não existam.

O contrato F52 inclui o `main.cpp` real e liga as bibliotecas do app. Cobre
persistência, migração, idiomas, os dois renderers de Shorts, feeds WEB e TV,
protocolo, paginação, seleção, área segura e a aba Shorts do canal. Os transportes
de conta usam exclusivamente tokens fictícios e recusam pedidos não previstos.

Capturas com SDL/Vulkan e entrada de controle reais:

```sh
python3 tests/f52/feeds_visual.py tests/f52/results/feeds-ui
python3 bench/search/prepare.py tests/f52/results/search-ui
python3 tests/f52/visual.py tests/f52/results/search-ui after
python3 tests/f52/adversarial.py tests/f52/results/adversarial
```

`feeds_visual.py` verifica nos pixels a largura dos cartões, a compactação em
`hide` e a margem segura em CRT; cobre Home pública, Home de conta, Inscrições,
Histórico, Assistir mais tarde, primeira página oculta, lista vazia e alteração
da configuração com o feed já carregado. Aceita nomes de casos após a pasta.
O fixture compartilhado fica em `fixtures/feeds.json`.

`adversarial.py` acrescenta 65 execuções do app: Shorts no início/meio/fim,
limites horizontais, preservação de coluna, retorno ao mesmo cartão depois de
decodificar mídia sintética, continuação tardia, títulos longos e canais mistos,
texto grande, árabe, 480/720/1080, filtros abertos e migração/reinício das
preferências. Aceita nomes de casos após a pasta. A saída inclui os logs,
capturas e `manifest.json`; os casos de layout também exigem inspeção visual.

`visual.py` cobre busca, foco, Configurações e timer. O timer usa um player sem
mídia disponível para exercitar somente o painel.

`python3 tests/f52/oauth_tls.py` executa a bancada TLS existente em loopback,
substituindo o objeto HTTPS do arquivo estático por aquele compilado pela bancada
com sua raiz de teste. As verificações originais são preservadas.

Logs, imagens, manifestos, tokens fictícios e binários ficam em `results/`,
ignorado pelo Git. As fontes e fixtures de teste são versionados.
