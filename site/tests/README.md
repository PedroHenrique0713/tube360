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

Os prints do app de PC são referências separadas de produção, obtidas com `SDL_VIDEO_DRIVER=offscreen`, `XT_SHOT`, transportes de bancada e assets isolados. As capturas e a procedência estão no relatório F49 em `site/.work/F49-relatorio.md`. A demo HTML usa os sprites locais da F27 e conteúdo ilustrativo; não estabelece conexão com conta, console ou player remoto.
