# F47 — busca com conta, qualidade e atalhos da Música

Base: `af792a2` (`f45-integracao`, v0.3.2-rc3). Trabalho exclusivo no worktree
`<repo>`, branch
`f47-busca-conta`. Provas em
`<repo>/.work/provas/f47/`.

## Entrega por item

1. **Busca com conta:** `WEB/search` e continuações não recebem Bearer TV; o
   bootstrap e os POSTs da busca suprimem o pote de cookies. A busca de vídeos é
   deliberadamente pública, inclusive sem filtros: não usa a rota personalizada
   da conta. Recomendações, biblioteca, inscrições e Música continuam usando suas
   rotas existentes. O worker mantém essa decisão mesmo com servidor PC habilitado
   e não encaminha falhas para uma busca que descarte filtros. Params, idioma,
   cache e propriedade dos cursores são preservados.
2. **Diagnóstico:** todas as falhas do catálogo que chegam à navegação registram
   rota interna, HTTP e código local; a operação remota acrescenta o código API
   numérico ou códigos de transporte/TLS. Bootstrap, JSON inválido, DOM inválido,
   cursor expirado, recusa recuperada WEB_REMIX → TV e HTTP 200 com erro também são
   diagnosticados. Não entram token, query, cursor, corpo, mensagem remota nem o
   texto bruto do erro de transporte. O logger do catálogo é ligado ao `LOG` do app
   antes de qualquer pedido; os testes podem inspecioná-lo sem capturar stdout.
3. **Qualidade:** a classe da largura rotula 1920×800/24 como `1080p24` no detalhe
   do player, no badge e no cabeçalho do menu Y. O perfil 1080p fica disponível
   para essa oferta recortada. Avisos de qualidade fixa usam o mesmo cálculo de
   exibição. O formato continua com `height=800`, 24 fps, quatro workers e loop
   filter completo. Tetos, seleção de formato, correspondência de altura fixa,
   deblocking, memória e recuperação usam as dimensões reais. Nenhum arquivo
   `player*.hpp` foi alterado.
4. **Música:** os atalhos autenticados têm relógio/lista/coração próprios em
   superfícies escuras e destaque do app. São primitivas vetoriais, sem fonte de
   imagem externa, download, logo ou textura adicional. As rotas e o comportamento
   A/B permanecem. A revisão visual encontrou uma sobreposição preexistente do
   título árabe com o rótulo de conta em 4:3; a caixa do título da Home de Música
   agora respeita a área reservada aos demais elementos.

Commits: `3001f26` — implementação; `HEAD` (`Add isolated account search regression proofs`) — suíte, evidências reproduzíveis e documentação. Os hashes exatos da entrega ficam no log abaixo. A lista completa solicitada por
`git log --oneline v032..HEAD` fica em `.work/provas/f47/git-log.txt`; ela inclui
integrações anteriores herdadas da rc3. O intervalo da F47 é `af792a2..HEAD`.

## Causa e limites da conclusão

O log fornecido `rc3-busca-com-conta.log` tem respostas de 40 bytes e zero itens
(linhas 108–109, 115–116 e 194–195); o status real do YouTube foi descartado. Não
há como recuperar daquele log se o servidor respondeu 400 ou erro no corpo.

O defeito reproduzido é a associação do Bearer TV ao cliente WEB na busca. Na rc3,
400 aborta sem tentar anônimo; 401/403 tentam anônimo uma vez, mas ainda enviam
credencial incompatível e alteram o estado de recusa do cliente; 200 com erro só
falha no decode. O worker então substitui o motivo por `BadResponse`, e ainda
imprime `music=tv` herdado do pedido de Música anterior. A nova busca independe
inteiramente dessas respostas ao Bearer e limpa esse diagnóstico de Música.
Uma falha do pedido anônimo continua sendo falha e ganha diagnóstico completo.

## Provas executadas

- `cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release` e
  `cmake --build client/build -j2 --target tube360 search-test search-worker-test manual-quality-test h264-policy-test`.
  App de PC compilado. NASM pelo PATH indicado no COMUM. As dependências vieram
  dos arquivos fixados pelos scripts do projeto; não foram feitos pedidos ao
  YouTube para investigar a conta.
- `cmake -S bench/catalog -B .work/catalog-build`, build `-j2` e
  `ctest --test-dir .work/catalog-build --output-on-failure`: **5/5**.
  `.work/catalog-build/search-account-test`: **157 verificações, zero falhas**.
- `client/build/search-test bench/search/fixtures`: **8.133 verificações**.
  `client/build/search-worker-test`: **5.013 verificações**, incluindo 5.000
  edições com resposta atrasada que ignora cancelamento.
- `client/build/manual-quality-test "$PWD/.work/provas/f47/manual-quality"`:
  **63 verificações** de fontes reais, ofertas esparsas, settings/idiomas e HLS
  fixo. Uma primeira chamada sem argumento recusou a execução; a invocação acima
  é a executada corretamente.
- `client/build/h264-policy-test .work/provas/f47/policy1080.mp4 .work/provas/f47/ui/replay/video.mp4 "$PWD/.work/provas/f47/policy-settings/"`:
  decodificação real de **90 quadros 1080p** com seis workers e filtro reduzido,
  **90 quadros 1080p** com filtro completo e **120 quadros 720p** com quatro
  workers; política, limites, backoff, banda e settings passaram. Os dois clipes
  são sintéticos locais. A primeira tentativa sem os três argumentos foi
  recusada pelo teste; o comando completo foi executado em seguida.
- `python3 bench/catalog/check-oauth-tls.py .work/provas/f47/tls`: transporte
  libcurl/mbedTLS real em HTTPS loopback; Music WEB_REMIX 401 → TV 200 e busca
  WEB anônima com conta carregada. Host, Origin, versão, cliente, filtros,
  ausência de cookies/Bearer na busca e preservação do pote público confirmados.
- `python3 bench/search-account/prepare.py .work/provas/f47/ui` e
  `python3 bench/search-account/e2e.py .work/provas/f47/ui`: **37 capturas**
  iniciais. Mais `search-server` e `search-server-failure` exercitam servidor PC
  de loopback com porta alocada pelo SO, sem permitir `/api/nav` no PC: **39
  casos** no manifesto final. A conta é fictícia; todo HTTPS do app/player é
  substituído no link e pedidos sem fixture falham fechado.
- `python3 bench/search-account/baseline.py .work/provas/f47`: a mesma prova na
  rc3 registra **33 falhas**; capturas da rc3 reproduzem a busca vazia com conta,
  atalhos sem arte e `800p24`. O fonte anterior é extraído por `git archive`; não
  houve checkout/switch ou modificação de outro worktree. A execução medida
  religou objetos rc3 já compilados (`--reuse-objects`); os comandos completos de
  compilação estão no script e em `.work/provas/f47/baseline-build.log`.
- `python3 bench/catalog/check-oauth-ui.py .work/provas/f47/music-regression recent library liked shortcut-back shelf-audio`:
  recentes, playlists, curtidas, A/B e áudio original passaram; áudio AAC presente,
  sem iniciar vídeo ou pedir a dublagem.
- Build do Xbox: **concluído**, app e launcher gerados. Executado sob lock
  compartilhado e scope com MemoryMax=6G/MemorySwapMax=1G, com arquivo de ambiente
  próprio contendo apenas os caminhos de toolchain. A instalação pessoal de
  configuração/chave não foi lida. FFmpeg estático da base foi copiado para este
  worktree e o script o recompilou quando a identidade da receita exigiu.
  Comando:

  ```sh
  TUBE360_BUILD_ENV="$PWD/.work/build.env" JOBS=2 TUBE360_VERSION=0.3.2-f47-test \
    flock ~/.cache/tube360-frota-xbox.lock \
    systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
  ```

  SHA-256:

  ```text
  889aac080d225f6e95560cf193006816d774f79df9e1147009f9e4cc78ce2c23  client/build-x360/tube360.xex
  1dc06570e8c1673625657311ea36e1016af01156465a040c88e494fe924cd127  client/build-x360/tube360-launcher.xex
  ```

Logs ficam em `.work/provas/f47/`: `catalog-tests-last.log`,
`search-account-final.log`, `search-tests.log`, `quality-tests.log`, `tls-final.log`,
`ui-all.log`, `server-search.log`, `music-regression.log`, `baseline-final.log`,
`ui-layout-final.log`, `build-pc-layout-final.log` e `build-xbox.log`. Após o último ajuste de largura, as oito capturas de Música e a falha de busca foram repetidas; catálogo 5/5 e HTTPS real de loopback também foram executados novamente com o fonte final. Os logs de capturas individuais
contêm as dimensões reais, modo, máscara de ofertas e estado do player.

### Capturas revisadas

Todos os caminhos abaixo são relativos à raiz absoluta das provas indicada no
início. O manifesto `ui/screenshots.json` lista os caminhos absolutos.

| Idioma | Busca com filtro | Atalhos | Player | Menu Y | Aviso | Música 4:3/texto grande |
|---|---|---|---|---|---|---|
| pt | [search-filtered-pt](<repo>/.work/provas/f47/ui/search-filtered-pt/screen.png) | [music-pt](<repo>/.work/provas/f47/ui/music-pt/screen.png) | [player-pt](<repo>/.work/provas/f47/ui/player-pt/screen.png) | [quality-pt](<repo>/.work/provas/f47/ui/quality-pt/screen.png) | [warning-pt](<repo>/.work/provas/f47/ui/warning-pt/screen.png) | [music-crt-pt](<repo>/.work/provas/f47/ui/music-crt-pt/screen.png) |
| en | [search-filtered-en](<repo>/.work/provas/f47/ui/search-filtered-en/screen.png) | [music-en](<repo>/.work/provas/f47/ui/music-en/screen.png) | [player-en](<repo>/.work/provas/f47/ui/player-en/screen.png) | [quality-en](<repo>/.work/provas/f47/ui/quality-en/screen.png) | [warning-en](<repo>/.work/provas/f47/ui/warning-en/screen.png) | [music-crt-en](<repo>/.work/provas/f47/ui/music-crt-en/screen.png) |
| es | [search-filtered-es](<repo>/.work/provas/f47/ui/search-filtered-es/screen.png) | [music-es](<repo>/.work/provas/f47/ui/music-es/screen.png) | [player-es](<repo>/.work/provas/f47/ui/player-es/screen.png) | [quality-es](<repo>/.work/provas/f47/ui/quality-es/screen.png) | [warning-es](<repo>/.work/provas/f47/ui/warning-es/screen.png) | [music-crt-es](<repo>/.work/provas/f47/ui/music-crt-es/screen.png) |
| ar | [search-filtered-ar](<repo>/.work/provas/f47/ui/search-filtered-ar/screen.png) | [music-ar](<repo>/.work/provas/f47/ui/music-ar/screen.png) | [player-ar](<repo>/.work/provas/f47/ui/player-ar/screen.png) | [quality-ar](<repo>/.work/provas/f47/ui/quality-ar/screen.png) | [warning-ar](<repo>/.work/provas/f47/ui/warning-ar/screen.png) | [music-crt-ar](<repo>/.work/provas/f47/ui/music-crt-ar/screen.png) |

Há ainda busca sem filtros nos quatro idiomas, convidados, as quatro recusas
adversariais e o erro 200 anônimo em [search-failure](<repo>/.work/provas/f47/ui/search-failure/screen.png).
`languages.png` reúne as quatro línguas; `warnings.png` e `crt.png` reúnem os avisos e a disposição 4:3; `before-after.png` e os PNGs em
`baseline/{search-pt,music-pt,player-pt}/` mostram a comparação com a rc3. Os avisos
vêm de sobrecarga real induzida somente no transporte/decoder de fixture e
conservam a fonte manual 480p. Imagens não medem desempenho de Xbox.

## Roteiro exato para o coordenador no console

Esta frente não acessou o console, a conta real, o Nest, a porta 8091, serviços
Tube360 ou a VPS. Não publicou nem fez push.

1. Integrar os commits F47 à frente F46 e compilar o conjunto pelo procedimento
   de release do coordenador. O XEX de prova tem versão de desenvolvimento
   `0.3.2-f47-test`; não é pacote assinado para publicação.
2. Com o app fechado, usar pasta temporária no próprio console. Copiar XEX do
   conjunto integrado, launcher como `default.xex` e as oito fontes. Conservar a
   conta apenas no console, seguindo a cópia local descrita em
   `docs/MUSIC-ACCOUNT.md`; não exportar o JSON ou token ao notebook. Settings:

   ```text
   lang=pt
   cols=4
   server=off
   sound=tv
   video_mode=auto
   ```

3. Abrir pelo launcher, com a conta já conectada. Y → digitar `astronomia` →
   Menu/Pesquisar. Esperado: itens, `catalogo console: /api/nav, ... music=`
   (sem `tv` herdado) e `navegação search: N itens`, N > 0. Não é busca
   personalizada. Se falhar, guardar somente linhas `catalogo falha:`: elas
   discriminam `WEB/bootstrap`/`WEB/search`, HTTP e `api_code`; não coletar corpos
   ou credenciais.
4. LB → Tipo Vídeo → Duração curta → Esta semana → Visualizações → Aplicar;
   pesquisar novamente, descer até pedir continuação, abrir um resultado/canal/
   playlist e voltar com B. Conferir filtros preservados e itens adicionais.
   Repetir com filtro limpo. Não usar número fixo de resultados como aceitação.
5. Fechar o app e renomear o arquivo de conta **somente dentro da pasta temporária
   no console**, sem abrir seu conteúdo; iniciar como convidado e repetir 3–4.
   Fechar e restaurar o nome original. Não usar Sair da conta: revoga o grant.
6. Com conta, abrir Música e conferir relógio, lista e coração nos três cartões.
   Abrir Recentes, Playlists e Curtidas com A; B deve retornar à mesma prateleira.
   Essas rotas seguem autenticadas; a busca pública não altera a conta.
7. Abrir `R6MlUcmOul8` em Automático e selecionar a engrenagem: esperado
   `1080p24` para fonte real 1920×800, badge 1080 e menu Y com perfil 1080p.
   O diagnóstico técnico deve continuar `fonte=800`/`800p 24.000 fps` e conservar
   política de decodificação para 800 linhas. Trocar perfil, buscar no vídeo e
   voltar; avaliar áudio/continuidade junto com a F46. Não esperar o perfil de
   seis workers/deblocking reduzido exclusivo de altura real 1080.
8. Repetir as telas em en/es/ar e conferir 4:3/texto grande. A validação de
   legibilidade física e desempenho é do console; as capturas são do PC.
9. Sair normalmente. Preservar no console eventual conta renovada durante o teste
   conforme o procedimento já existente e remover a pasta temporária. Não
   revogar o grant compartilhado nem publicar estas provas/contas fictícias como
   release.

## Integração e divergências

- `main.cpp`: uma ligação de logger no Worker, decisão de rota de busca no mesmo
  bloco e pontos de exibição de qualidade (badge, detalhe, cabeçalho Y e aviso).
  Não foram tocadas abertura, filas, áudio, callbacks ou recuperação da F46.
- `navigation.hpp`: referência das três artes locais, interceptação do desenho
  antes de solicitar thumbnail e limite de largura do título da Home de Música.
- `yt/catalog.hpp`: cabeçalhos da busca pública, validação de erro 200 e logger;
  as recusas de canal/playlist não foram removidas nem alteradas para uma nova
  política de conta.
- `playback_mode.hpp`, `yt/client.cpp` e `yt/media.hpp`: somente máscara de oferta
  dos perfis adaptativos por classe visual; formatos, alturas fixas e decisões de
  seleção usam as condições anteriores. A mudança em `client.cpp` é uma chamada
  com a largura adicional; integrar com cuidado se a F46 mexer nesse trecho.
- Novos headers `quality_label.hpp` e `music_shortcut_art.hpp`. Nenhuma alteração
  em `i18n.hpp`, `i18n_ar.hpp`, `client/CMakeLists.txt`, shaders, fontes ou assets.
  `bench/catalog/CMakeLists.txt` acrescenta um alvo de teste. Docs de busca,
  Música e changelog têm acréscimos pequenos.
- O COMUM fala de base v032 e build PC -j3; o brief desta frente define rc3 e -j2,
  que foram usados. O catálogo de Música já existia na rc3. Não foram necessárias
  chaves novas de idioma, logo ou imagens geradas por IA.
- A resolução usada nas capturas é sintética: não houve download/reprodução do
  vídeo público nem inferência de que o PC prova a aceitação no console real.
