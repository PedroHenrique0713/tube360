# Provas do site

Execute da raiz do repositório. Os testes usam o navegador sem perfil pessoal e gravam resultados/prints em `site/.work/`. APIs do GitHub são simuladas nas provas de navegador; apenas o build online consulta a API real. Os scripts GSAP são os mesmos do site e as provas também exercitam o CDN bloqueado.

```sh
python3 site/build.py
python3 site/build.py --offline
python3 site/tests/verify-build.py
python3 -m http.server 8279 --bind 127.0.0.1 --directory site/dist
```

Em outro terminal, com uma instalação existente do Playwright e Chromium:

```sh
export PLAYWRIGHT_MODULE=/caminho/para/node_modules/playwright
export CHROME_EXECUTABLE=/caminho/para/chrome-headless-shell
node site/tests/verify-browser.cjs
node site/tests/extra-proof.cjs
node site/tests/verify-v032.cjs
```

`SITE_URL` permite mudar a porta/origem (sem barra final). Se Playwright e o navegador padrão já estiverem instalados, as duas variáveis de caminho são opcionais. Pare o servidor no final.

- `verify-build.py`: soma/paginação de pacotes, exclusão de checksums, contagens inválidas, offline sem rede, 403, valores explícitos e data de publicação.
- `verify-browser.cjs`: provas herdadas da F27, EN/PT em desktop/mobile, teclado/mouse/toque, CDN carregado/bloqueado, paginação de releases, sessão compartilhada e soma parcial recusada.
- `extra-proof.cjs`: 1920×1080, página inteira, scroll e movimento medido em quadros, impressão, mudança de preferência e indisponibilidade de storage/ScrollTrigger.
- `verify-v032.cjs`: matriz EN/PT × 1366×650, 1920×1080, 390×844; busca/filtros, Start, listas locais/retomada, qualidade, SponsorBlock, Shorts e Música. Mede a primeira tela contra a F27. Exercita expiração do cache e Gamepad API com controle simulado; não comprova controle físico.

Os prints do app de PC são referências separadas de produção, obtidas com `SDL_VIDEO_DRIVER=offscreen`, `XT_SHOT`, transportes de bancada e assets isolados. As capturas e a procedência estão no relatório F49 em `site/.work/F49-relatorio.md`. A demo HTML usa os sprites locais da F27, capas fictícias de música e conteúdo ilustrativo; não estabelece conexão com conta, console ou player remoto.

## Revisão de fidelidade da rc4

```sh
node site/tests/verify-fidelity.cjs
node site/tests/capture-comparisons.cjs
python3 site/tests/make-comparisons.py
```

`verify-fidelity.cjs` testa cada uma das 18 linhas de Configurações, os limites do viewport/dicas, clique numa linha distante e gesto de toque com eventos reais do Chromium. Também verifica rótulos, valores brancos fora do foco e vermelhos na seleção (Som indisponível fica cinza), cartões da Biblioteca e capas/conteúdo de Música sem futebol. As provas F27/F49 foram adaptadas ao foco nativo de Inscrições, aos cartões e à linha correta do SponsorBlock.

`capture-comparisons.cjs` fotografa a demo em PT-BR na resolução lógica de 1280×720; só para essa captura, posiciona a TV num ponto inteiro da viewport, remove a escala externa e reserva altura no hero para outras seções não cobrirem o screenshot. Isso não altera os estados, cores ou visibilidade do app. As provas comuns continuam usando a página normal em 1366×650, 1920×1080 e 390×844. `make-comparisons.py` compõe os PNGs sem retoque: app real à esquerda, demo à direita. Entrega `site/.work/rc4-review/pranchas/index.html` e quinze pranchas, com hashes e diferenças RGB em `comparisons.json`.

As capturas de referência vêm do executável rc4 com transportes de replay, conta fictícia, HOME/XDG isolados e Cast desligado:

```sh
python3 site/tests/native-reference.py
# Ou somente os casos necessários:
python3 site/tests/native-reference.py settings-top settings-middle settings-bottom
```

O script usa `site/.work/app/search-account/tube360-replay` e suas gravações, copiadas da bancada de integração; `NATIVE_REPLAY_DIR` permite indicar outra cópia compatível. Nenhum perfil pessoal, token real, Xbox, Nest ou servidor real é acessado. As fontes vêm de `client/assets`. As capas e miniaturas de bancada são usadas igualmente no renderer nativo e na demo. As capas de música são gráficos locais fictícios, com títulos/artistas de exemplo; `catalog-art.png` é uma ilustração de circuito usada como imagem de catálogo nas gravações. Os SVGs do player vêm dos Phosphor Icons MIT já presentes em `client/third_party/phosphor`.

Os MP4/AAC de comparação do player são fixtures de 487 segundos derivadas do frame local anterior, com qualidades 1080p/480p/360p/240p/144p; ficam apenas em `site/.work/rc4-review`. A página não carrega esses vídeos. Não são prova de acesso a conta real nem de desempenho no Xbox. O executável PC herdado registra `0.3.0-dev` como versão de compilação, embora sua árvore de código seja a rc4; a demo preserva esse valor da referência em Configurações. A versão pública do botão de download continua vindo do GitHub.


## Adendo 1: novidades e revisão da página

`site/build.py` lê as três primeiras versões do `CHANGELOG.md`. Os tópicos reconhecidos selecionam mudanças existentes, com redação de usuário reutilizável em `release_summaries` EN/PT. Versão, ordem, data, presença de cada item e linha de procedência vêm do CHANGELOG; o mesmo modelo gera o resumo e o histórico. Não há uma segunda lista manual por versão no site. Se houver menos de três tópicos traduzidos numa nova versão, o build falha para pedir cobertura editorial. Versão sem data aparece como “Em preparação” e aponta para a lista de releases; versões datadas apontam para suas tags. O build rejeita dicionários/listas em tokens de texto do template.

```sh
python3 site/tests/verify-build.py
node site/tests/verify-updates.cjs
# Guardar antes de alterar os fontes (servidor deve servir o dist anterior):
REVIEW_OUT=site/.work/adendo-1/before node site/tests/capture-page-review.cjs
# Depois do build, com servidor servindo o dist novo:
REVIEW_OUT=site/.work/adendo-1/after node site/tests/capture-page-review.cjs
python3 site/tests/make-page-review.py
```

`verify-updates.cjs` compara versões/datas com o arquivo de origem, verifica 3–6 itens por versão e linhas de procedência, links de release/publicação pendente, âncora do resumo, imagens no idioma correto, abas pelo teclado, foco no rodapé e a descrição de reprodução. Matriz EN/PT × 1366×650, 1920×1080 e 390×844, sem overflow nem erros de script. `verify-build.py` também remove uma mudança da fonte e altera versão/data para provar que o resultado acompanha o CHANGELOG.

`capture-page-review.cjs` guarda topo, página inteira e estados das seções em seis combinações. Não troca conteúdo nem estilos. `make-page-review.py` entrega pranchas do topo, originais completos lado a lado em HTML e hashes em `comparison-manifest.json`. Não são imagens de produção nem entram em `dist/`.

As imagens `/assets/rc4/{en,pt-BR}` são capturas do renderer nativo com dados fictícios, convertidas para WebP sem retoque. Para refazer as fontes em inglês:

```sh
NATIVE_LANG=en python3 site/tests/native-reference.py search library music phone player quality settings-bottom
```

As fontes PNG ficam em `site/.work/rc4-review/native-en`; PT usa `native`. O script também regenera as seis capas gráficas sem texto usadas na demo e no catálogo de bancada. A versão de compilação 0.3.0-dev visível nas capturas continua sendo metadado do executável PC herdado; não é a versão do download público.
