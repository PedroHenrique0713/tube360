# F52 — busca, Shorts e Configurações

Na raiz da worktree:

```sh
PATH="$HOME/.local/bin:$PATH" cmake -S client -B client/build -DFFMPEG_PREFIX="$PWD/.work/ffmpeg-pc"
cmake --build client/build -j6
python3 tests/f52/run.py
```

O prefixo explícito usa o FFmpeg já instalado e evita que o configure recompilhe
a `.work` compartilhada. O teste novo inclui o `main.cpp` real e liga as mesmas
bibliotecas do app. As 153 checagens executam mesmo com `NDEBUG`: persistência e
migração, quatro idiomas, identificação dos dois renderers de Shorts, protocolo,
linhas e foco em 2/3/4 colunas, continuação e aba Shorts do canal. A prova de desenho
inspeciona os vértices gerados por `Navigation::draw`, com métricas de fonte
sintéticas; não inicializa GPU nem transporte de rede. Confere duas fileiras,
cartões 112×199,111, recorte de textura sem deformação e área segura em CRT.

`python3 tests/f52/run.py --all` também executa os contratos existentes. O teste
`reddit-contract` ainda exige `Settings{}.server == true`, preferência removida
na F52; falha com `auto preference without address is console-only`. Esse teste
antigo não foi editado, pois o escopo permite apenas testes novos. É possível selecionar nomes, por exemplo
`python3 tests/f52/run.py search catalog-membership display arabic`.

O caso `h264-policy` recebe duas mídias sintéticas com ao menos 60 quadros, em
`results/contracts/1080.mp4` e `720.mp4`. Não é um benchmark do console.

Capturas com Vulkan/SDL e entrada de controle reais, sem HTTPS externo:

```sh
python3 bench/search/prepare.py tests/f52/results/after
python3 tests/f52/visual.py tests/f52/results/after after
```

Os resultados são sintéticos e declarados como `F52 fixture`. O transporte de
prova existente fecha requisições não previstas; HOME, cache, conta e assets são
isolados e Cast fica desabilitado. São onze casos: grade e ↓, Shorts e onze →,
ocultação, persistência das duas opções, CRT, árabe e timer. O timer usa um player
sem mídia disponível para exercitar só o painel; não representa playback real.
Os prints anteriores vieram do executável base (HEAD inicial `d758778`), antes da
recompilação das alterações, com os mesmos fixtures. As evidências selecionadas ficam em
`evidence/`; as demais e os logs ficam em `results/` (ignorado pelo Git).

`python3 tests/f52/oauth_tls.py` executa a bancada TLS existente em loopback. A
bancada original encontra duas definições de `yt::https` no link atual; este
adaptador remove `https.cpp.o` de uma cópia local do arquivo estático e deixa o
objeto TLS da própria bancada substituí-lo. As verificações originais não mudam.

Shorts na Home e nos feeds nativos de conta continuam pendentes: os parsers
`yt/client.cpp` e `yt/console_account.cpp` descartam esses renderers ou não
transportam a marcação; `Video`/`Feed`, o worker e o desenho/controle da Home estão
fora das regiões F52. A grade nova atende as páginas que recebem o protocolo do
catálogo. Nenhum desses outros parsers ou trechos da Home foi alterado.
