# Revisão adversarial F52 — 0.3.3

Revisão executada em 26/11/2026, na worktree `f52-busca-shorts`.
Base: `d7587783b9b2478d3f4dda338bdb72ca2352c292`.
Produto revisado: `995eaa85284e759425b41b05b27187cbf4bc27b7`.
Commits: `00c34e7` e `995eaa8`.

Não foi comprovada regressão de produto nesses commits. Nenhum arquivo de
produto foi corrigido. Existe uma divergência anterior sobre `cols=3` na busca,
descrita abaixo. A revisão acrescenta testes executáveis e sua documentação.

## O que mudou nesta revisão e por quê

- `tests/f52/adversarial.py`: 65 execuções adicionais do app de PC. Usa o objeto
  `main.cpp` de produção, SDL/Vulkan, `XT_KEYS` e `XT_SHOT`; troca somente o
  transporte HTTPS no link. Pedidos sem fixture falham fechado. Os oráculos
  conferem seleção, tela, decodificação, continuação, persistência e presença de
  arte nos pixels. A avaliação de sobreposição continua sendo visual.
- `tests/f52/README.md`: comando e alcance do novo runner.
- `tests/f52/REVIEW.md`: este relatório.

Cada caso tem assets, HOME, XDG, settings e replay próprios. Reexecuções arquivam
o estado anterior em `previous/`; o reinício deliberado das preferências conserva
o mesmo diretório. Nenhuma alteração em `client/src/main.cpp` ou em outra região
de produto foi necessária. Não houve commit de correção de produto nem push.

Os artefatos desta execução estão em `tests/f52/results/review/`, ignorado pelo
Git. [Inventário dos testes encontrados](results/review/test-discovery.txt).

## Ataques e observações

| Ataque executado | Resultado observado |
| --- | --- |
| Shorts antes, entre e depois dos vídeos, em busca e Home | Fileira visível e navegável nas três posições. |
| Só Shorts; um único Short; um Short entre vídeos; nenhum Short | Nenhum travamento ou seleção inválida. O único cartão continua visível. |
| Cima/baixo atravessando a fileira | A coluna regular 2 retorna à coluna 2; a posição horizontal de Shorts também é lembrada. |
| Direita repetida além do último cartão | Para no índice 28 da lista mista e no índice 12 da lista só de Shorts. |
| Esquerda repetida até o começo | Retorna ao primeiro Short e depois abre o menu lateral, sem índice negativo. |
| Abrir o terceiro Short e voltar do player | AVC/AAC sintéticos foram decodificados; busca e Home voltaram ao índice 6, o mesmo cartão `f52vid00018`. |
| Continuação depois de descer nove/dez linhas | Uma página criou a fileira tardia; outra ampliou a primeira fileira. Seleções finais 40 e 41, com o cursor confirmado no pedido real. |
| `row → hide` com foco num Short e feed carregado | Ocultou a fileira; o movimento seguinte alcançou um vídeo regular no índice 18. |
| `hide → row` no mesmo feed | Reapareceu a fileira; cima/direita selecionou o Short no índice 5. |
| Home pública e com conta; Histórico; Mais tarde; Inscrições; Biblioteca | Runner original de feeds passou em 19 casos; os contratos também cobrem paginação e renderers TV. |
| Títulos e bylines enormes; canais misturados; 3/4 colunas; texto grande | Capturas revistas sem sobreposição ou corte do cartão em foco. Três colunas são efetivas na Home e, com texto grande, na busca. Ver divergência da preferência abaixo. |
| Árabe/RTL; 640×480 com área segura; 1920×1080 | Textos e fileira visíveis. Não foi visto conteúdo de Shorts invadindo a margem segura. |
| Busca com filtros LB abertos em cinco perfis | Modal e textos legíveis, sem fileira desenhada sobre o modal. |
| `server=on/auto` × `sleep_what=notebook/together/xbox_notebook` | Seis migrações e seis reinícios passaram; gravação normalizou para `server=off` e `sleep_what=xbox`. |
| Valores inválidos e chaves ausentes | Defaults `shorts=row` e `error_reports=on`; navegação/gravação continuaram funcionando. |
| Relatórios de erro off/on e reinício | Arquivo gravou exatamente `error_reports=off` e `error_reports=on`; reinício seguido de nova troca confirmou a leitura. |

Os 65 casos não são um produto cartesiano de todos os perfis. Os testes de
navegação rodam em busca e Home; os de layout combinam cinco perfis de idioma,
colunas, escala e resolução. Os feeds de conta recebem a cobertura adicional
dos contratos e do runner original.

Foram abertas as montagens [base](results/review/baseline-montage.jpg) e
[retratos/filtros](results/review/portrait-montage.jpg) e
[Music/busca com conta](results/review/music-montage.jpg), além das imagens de árabe
480p em tamanho original. As miniaturas da linha seguinte podem aparecer
parcialmente antes do rodapé: isso preserva a compactação exigida por #21. O
cartão em foco foi verificado depois que a rolagem assentou.

Exemplos de saída real:

```text
PASS search-middle-column  sel=2
PASS search-middle-down  sel=19
PASS search-end-boundary  sel=28
PASS search-player-back  sel=6
PASS home-player-back  sel=6
PASS continuation-new-row  sel=40
PASS continuation-extend-row  sel=41
PASS home-toggle-focused-short  sel=18
PASS home-toggle-row-again  sel=5
```

No playback de prova:

```text
primeiro quadro: 41 ms, pts 0.000, live 0, som TV
player: f52vid00018 pos 0.7 quadros 18 perdidos 0
navegacao: Player -> Navegacao
captura: frame 2059, tempo 4981 ms, tela 4, vídeo f52vid00018, posição 1.96
captura navegação: search sel=6 canal=0 zona=3 pilha=1 menu=0 prateleira=0
```

Esse trecho é da bancada `confirmed-player`; os testes finais também exigem
primeiro quadro e posição maior que zero antes de aceitar o retorno.

## Testes e saída

Todos os comandos partem da raiz da worktree. `R` abaixo representa
`tests/f52/results/review`.

| Comando | Saída/evidência |
| --- | --- |
| `PATH="$HOME/.local/bin:$PATH" cmake -S client -B client/build -DFFMPEG_PREFIX="$PWD/.work/ffmpeg-pc"` e `cmake --build client/build -j6` | `[100%] Built target tube360`; [configure](results/review/pc-configure.log), [build](results/review/pc-build.log). |
| `python3 tests/f52/run.py --all` | `18/18 executables passed`; [log completo](results/review/contracts.log). |
| `python3 tests/f52/feeds_visual.py R/feeds-ui` | 19 casos `OK`; [log](results/review/feeds-ui.log). |
| `python3 bench/search/prepare.py R/search-ui` e `python3 tests/f52/visual.py R/search-ui after` | 11 casos `OK`; [log](results/review/search-ui.log). |
| `python3 tests/f52/adversarial.py R/adversarial-confirmed` | `65/65 adversarial app cases passed`, `EXIT adversarial 0`, com checagem de imagem vazia e isolamento de reexecução; [log](results/review/adversarial-confirmed.log), [manifesto](results/review/adversarial-confirmed/manifest.json). A rodada completa anterior também passou em 65/65: [log](results/review/adversarial-final.log). |
| `python3 tests/f52/adversarial.py R/pixels-final search-only-one home-only-one search-left-end home-toggle-focused-short` | `4/4 adversarial app cases passed`; [log](results/review/pixels-final.log). |
| `python3 tests/f52/oauth_tls.py` | `12 Music items and anonymous account search via production TLS`; recuperação WEB_REMIX 401 → TV 200 com cabeçalhos/cookies conferidos; [log](results/review/oauth-tls.log). |
| `python3 bench/search/prepare.py R/search-original` e `python3 bench/search/e2e.py R/search-original` | 40 casos `OK`, reexecução com `EXIT search-e2e 0`; [log inicial](results/review/search-original.log), [reexecução](results/review/search-original-rerun.log). Ver tentativas abaixo. |
| `python3 bench/search-account/prepare.py R/search-account` e `python3 bench/search-account/e2e.py R/search-account` | 39 casos `PASS`; [log](results/review/search-account.log). |
| `python3 bench/catalog/check-oauth-ui.py R/music-ui` | 14 casos, incluindo conta/visitante, TV, CRT grande, recentes, biblioteca, curtidas e áudio; [log](results/review/music-ui.log). |
| `python3 tests/update/service-test.py --output R/update-service` | `PASS production trust and file-trigger isolation; 9 worker HTTP scenarios`; [log](results/review/update-service.log). |
| Contrato `tests/update/contracts.cpp` ligado às bibliotecas de produção; `TMPDIR="$PWD/R/tmp" python3 tests/update/test.py R/update-contract` | `83 signed update checks passed`; [build](results/review/update-contract-build.log), [log](results/review/update-contract.log). Substituir `R` pelo caminho indicado acima. |
| Build de `reddit-tls-probe`; `TMPDIR="$PWD/R/tmp" python3 tests/reddit/tls.py client/build/reddit-tls-probe R/reddit-tls` | `PASS expired tls_date`, `PASS future tls_date`, `PASS untrusted tls`; [log](results/review/reddit-tls.log). |

O runner `--all` efetivamente compilou e executou:

```text
284 F52 checks passed
8133 search checks passed
5013 worker checks passed; 5000 edits 1.168 ms (0.234 us/edit)
446 checks passed
934 OAuth Music checks passed
74 membership checks passed
260 checks passed
157 checks, 0 failures
52 channel artwork checks passed
431 checks passed
428 checks passed; 100000 cached layouts: 1375 us
PASS 117 checks: public API contract, privacy, bounds, categories, cache, skip/undo, settings, pt/en/es
PASS manual quality: 63 checks, exact source, sparse offers, settings/locales and pinned HLS
PASS server: default off with or without address, repeated toggle and legacy configured/off modes
PASS pressure: console opening timings, one rung, settling, healthy 720p30 floor, missing 720p30 cadence and real bandwidth pressure
5506 renderer checks passed
PASS actual decode height=1080 full=0 workers=6 loop=48 frames=63
PASS actual decode height=1080 full=1 workers=6 loop=0 frames=63
PASS actual decode height=720 full=0 workers=4 loop=0 frames=63
292 checks passed
PASS comments: legacy/entity metadata, initial/next pages, reply/related token isolation, language, errors; votes TTL/zero/invalid; controller
18/18 executables passed
```

Isso inclui `tests/reddit/contracts.cpp`, busca/worker, catálogo, catálogo OAuth,
membros, conta, busca com conta, Music, idiomas e renderização. `EXIT ... 0` de
cada executável está no log completo; não houve seleção vazia de testes.
O retorno com playback foi reexecutado no mesmo diretório e passou em `1/1`,
com o estado anterior arquivado. Um seletor inexistente retornou exit 1 com
`No matching adversarial cases`, sem aprovar uma bateria vazia.

## Build de console

O script original preparava e compilava o SDK em `.work/native-display` e tentava
refazer o FFmpeg. Para cumprir o isolamento, foi usada cópia por reflink do SDK
em `R/xbox-work/native-display`, montada somente no namespace do build com
`bwrap`. O restante da árvore compartilhada ficou somente leitura. O lock de
fetch e o `/tmp` usados pelo build também foram direcionados a arquivos privados
na worktree. Não foi alterado o symlink `.work` nem o script de build.

O cache local `client/build-x360` recebeu
`-DFFMPEG_PREFIX="$PWD/.work/ffmpeg-xbox"`, reutilizando o FFmpeg já instalado.
O comando final, depois dessa configuração, foi:

```sh
task_work_real=$(readlink -f .work)
PATH="$HOME/.local/bin:$PATH" flock /tmp/tube360-build-xbox.lock \
  systemd-run --user --scope -q -p MemoryMax=6G \
  bwrap --ro-bind / / --dev-bind /dev /dev \
    --bind "$PWD" "$PWD" \
    --bind "$PWD/tests/f52/results/review/xbox-work/tmp" /tmp \
    --bind "$PWD/tests/f52/results/review/xbox-work/fetch.lock" "$task_work_real/src/.fetch.lock" \
    --bind "$PWD/tests/f52/results/review/xbox-work/native-display" "$task_work_real/native-display" \
    tools/build-xbox.sh
```

Resultado: exit 0. [Log completo](results/review/xbox-build-final.log).

```text
[100%] Built target tube360
34d6f17afb798ec432b8dad1a8323f92e47bf849ad4f9d8e119e3904d4d157f9  build-x360/tube360.xex
e567bc737a54c0a45bde04f863fb7c124cdf6d081968d2372ff0c7f77e436c5f  build-x360/tube360-launcher.xex
```

O log inclui avisos de `picojson`, parâmetro não utilizado em Cast e o scanner
informando 227 instruções não suportadas pelo Xenia. Não houve execução no Xenia
nem no Xbox. A comprovação é de compilação/link/geração dos XEX com xsdk e
dependências instaladas, sem reconstrução do FFmpeg compartilhado.
O cache usa os metadados padrão `TUBE360_VERSION=0.3.0-dev` e
`TUBE360_BUILD_ID=1`: os XEX gerados são prova de compilação, não um pacote de
release 0.3.3.

## Tentativas que falharam e correções da bancada

Não há correção de produto escondida por estes resultados:

1. A primeira rodada adversarial terminou `50/52`: os dois retornos do player
   usavam um único B e não tinham a cadeia completa de fixtures de playback.
   O app ficou em `tela 2`, erro `unrecorded_request`. O primeiro B esconde os
   controles; a bancada passou a usar `B.B`. Foram acrescentados watch, base.js,
   resposta player e mídia AVC/AAC sintética, exigindo decodificação real.
   [Tentativa](results/review/adversarial.log).
2. A bancada de playback chegou a reproduzir vídeo e voltar ao cartão correto,
   mas a asserção procurava `fonte 360p`; o log real usa `fonte=360p`. Foi
   corrigida somente a asserção e acrescentada a exigência de posição > 0.
   [Evidência com decodificação](results/review/confirmed-player/search-player-back/app.log).
3. O primeiro roteiro para reabrir Configurações escolhia Home no menu lateral.
   A sequência foi corrigida para navegar novamente até Configurações. A troca
   inversa passou com seleção final 5. [Tentativa](results/review/targeted.log).
4. A primeira execução de `bench/search/e2e.py` reutilizou o diretório que
   `tests/f52/visual.py` havia trocado por vídeos sintéticos. O teste de abrir
   canal encontrou uma lista sem canais e falhou. A suíte foi preparada em
   `search-original`, sem compartilhar fixtures mutáveis com a F52.
   [Tentativa](results/review/search-e2e.log).
5. A primeira execução no diretório limpo registrou os 40 casos, mas o processo
   superior retornou 143. Não foi identificado o emissor desse SIGTERM. Foi
   feita uma reexecução independente para obter também o status de encerramento;
   não se considerou apenas a existência das capturas como aprovação do comando.
6. A primeira configuração de console tentou extrair/recompilar FFmpeg e foi
   bloqueada pela montagem somente leitura (`tar: ... Read-only file system`).
   Depois do prefixo explícito, a configuração ainda tentou abrir o lock de
   fetch compartilhado. Foram isolados esse lock e `/tmp`. Só então houve uma
   compilação completa do app. [Primeira tentativa](results/review/xbox-build.log),
   [segunda configuração](results/review/xbox-build-prefix.log).

## Suspeitas, limites e como verificar

- **Preferência de colunas na busca:** comportamento comprovado, anterior à F52.
  Em `cols=3`, texto normal e 1280×720, a busca desenhou quatro cartões de 252 px;
  a Home desenhou três de 344 px. Medição nos pixels em y=570:
  [saída](results/review/columns.txt). A `main` anterior já usa `ui::columns(4)`
  na navegação; com texto grande a busca usa três efetivamente. Não foi tratado
  como regressão desses dois commits. Se #31 pretende aplicar a preferência
  também à busca/catálogo, esse contrato continua pendente.
- **F54:** foi comprovado o contrato de arquivo `error_reports=off/on` e sua
  leitura pela própria F52 após reiniciar. O consumidor F54 não está nesta árvore;
  não foi comprovado o envio real de relatórios nem o consumo integrado pelo
  launcher F54. Na integração, rodar o leitor da F54 contra os dois arquivos
  produzidos pelos casos de migração e `reports-on`.
- **480i físico:** os prints são 640×480 offscreen com margem segura, não uma
  prova de sinal entrelaçado em CRT. Depois que o console estiver liberado, o
  coordenador deve repetir os roteiros de navegação/layout em 480i e 1080p.
- **YouTube/conta reais:** todo transporte das capturas foi fechado, com tokens
  fictícios. Não se comprova mudança futura de DOM, personalização real,
  autorização OAuth ou desempenho de console com estes resultados.

## Divergências entre este prompt e o código real

1. Não foi fornecido um caminho de saída `-o`. O relatório foi salvo neste
   arquivo, dentro da frente autorizada.
2. A lista de regiões próprias de `main.cpp` foi mencionada no contexto comum,
   mas não veio enumerada. Nenhuma região de produto foi modificada.
3. O script de console usa `.work/native-display` e pode reconstruir FFmpeg,
   enquanto o prompt proíbe reconstruir a `.work` compartilhada. Foi necessária
   a execução isolada acima. Duas configurações falharam antes da compilação
   completa; não se repetiu uma compilação bem-sucedida do console.
4. `cols=3` não configura a busca com texto normal na implementação existente;
   foi exercitado e documentado, além de testar as três colunas efetivas por
   `large_text` e as três/quatro colunas da Home.
5. O consumidor F54 não está presente nesta branch; o contrato off/on foi
   verificado no arquivo e no reinício da F52.
6. B no player fecha primeiro os controles visíveis. O roteiro de retorno usa
   dois B para alcançar a grade e verificar a seleção.
7. O script não assume versão de release 0.3.3 por padrão; os metadados do build
   de prova ficaram `0.3.0-dev`/`1`.
