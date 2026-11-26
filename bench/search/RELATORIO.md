# F43 — busca com filtros e sugestões

Trabalho em `<repo>`, branch
`f43-busca`, base `f42-integracao` (`c56fd85`). Implementação exercitada no app de PC
e compilada para Xbox. Nada publicado, instalado ou executado no console.

## Revisão da ordenação — 2026-11-25

Estado atual: **Ordenar por contém somente Relevância e Visualizações**. O filtro
separado de Data de envio continua com qualquer/hoje/semana/mês/ano. O seletor e sua
normalização têm duas opções; Visualizações codifica campo 1=3 (CAM=). Foi removida
a chave obsoleta SearchNewest em pt/en/es/ar; restam 22 chaves novas da F43.

Evidência fornecida pelo coordenador: pedidos reais WEB youtubei/v1/search,
query astronomia, hl=pt, gl=BR, em 2026-11-25 às 07h informadas. CAI= repetiu a
relevância, exceto a oitava entrada. CAISAhAB (data + vídeo) retornou há 1 mês,
há 1 mês, há 7 meses, há 2 meses, há 1 ano, há 5 minutos: não cronológico. CAM=
retornou 477, 183, 89, 80, 48 milhões de visualizações, em ordem decrescente.
Isso substitui a inferência anterior baseada apenas em HTTP 200/params preservado.
A opção inoperante foi retirada em `37435df`. O relato está em docs/SEARCH.md e em
bench/search/fixtures/order-coordinator-20261125.json, identificado como medição
relatada, não como payload bruto. Não fiz novas consultas reais nesta revisão.

Validação da revisão, com os objetos reais do produto:

```sh
cmake --build client/build -j2 --target tube360 search-test arabic-text-test arabic-render-test
client/build/search-test "$PWD/bench/search/fixtures"
client/build/arabic-text-test
SDL_VIDEO_DRIVER=offscreen client/build/arabic-render-test "$PWD/client/assets"
python3 bench/search/prepare.py .work/search/order-review/ui
python3 bench/search/e2e.py .work/search/order-review/ui
```

PC: build concluído; **8.133** checagens de busca, **383** de texto árabe e **4.885**
do renderer aprovadas. O teste cobre ciclo nos dois sentidos, todos os quatro
filtros de período gravados e a ausência de CAI em combinações normalizadas.
A mutação temporária que voltava ao campo 1=2 foi recusada pelo teste de contrato.
**40 casos do app aprovados**, incluindo novo ciclo Visualizações → Relevância
preservando vídeo/curta/semana; a auditoria confirmou EgYIAxABGAQ%3D nesse caso e
CAMSBggDEAEYBA%3D%3D nos resultados por visualizações. Nenhum pedido de busca do
lote gerou a ordenação removida. Aplicar, limpar, cancelar, recentes e destinos
foram exercitados novamente. Logs em .work/search/order-review/{build-pc,contracts,
arabic-text,arabic-render,mutation,ui}.log.

Novas capturas do painel abertas individualmente, com Visualizações selecionada
e o período Esta semana preservado (caminhos absolutos):

| Idioma | Painel HD |
|---|---|
| pt | `<repo>/.work/search/order-review/ui/filters-pt/screen.png` |
| en | `<repo>/.work/search/order-review/ui/filters-en/screen.png` |
| es | `<repo>/.work/search/order-review/ui/filters-es/screen.png` |
| ar | `<repo>/.work/search/order-review/ui/filters-ar/screen.png` |

Também abri as quatro pranchas com as três telas HD e 4:3/texto grande renovadas:
`<repo>/.work/search/order-review/contact-{pt,en,es,ar}.png`.
Os oito painéis estão dentro dos limites; o resumo espanhol grande usa duas linhas.
O manifesto com os 40 caminhos absolutos é .work/search/order-review/ui/screenshots.json.

Xbox: repetido no fim, com JOBS=2, o mesmo build.env isolado, build ID 4, versão
0.3.2-f43, flock compartilhado e systemd-run em escopo MemoryMax=6G/MemorySwapMax=1G
(comando completo na seção Xbox original abaixo). O primeiro comando gerou XEX/
hashes mas retornou 143; por isso não o aceitei como término normal. A repetição
incremental retornou **0**, com `[100%] Built target tube360`. Prova do retorno e
comando: .work/search/order-review/build-xbox-status.json; log final:
.work/search/order-review/build-xbox-final.log. Nenhum XEX foi instalado ou executado.

Hashes atuais, substituindo os hashes históricos da seção 2:

| Artefato | Bytes | SHA-256 |
|---|---:|---|
| `client/build/tube360` | 8329800 | `fb4b21d4a3e4f1f3becc1140b7bec14954537f28770e23a7fbec566fcb16227d` |
| `client/build-x360/tube360.xex` | 11489280 | `f92b18dd9d27b264d84eb991cae749aa218028a0506fe298fd029cddc20dd46d` |
| `client/build-x360/tube360-launcher.xex` | 995328 | `18c315c87a7e1aa2ee93f27bed0a4d0912bf63c96b098b95e9ca4da9137f939c` |

Manifesto atual: .work/search/order-review/artifacts.json. As medições seriais de
quadros da seção 2 permanecem históricas da implementação inicial; não repeti o
benchmark serial controlado nesta correção. A rodada funcional foi feita também
sob build compartilhado e inclui picos, portanto não substitui aquele benchmark.

Integração desta revisão: apenas search.hpp, search_ui.hpp, remoção de uma chave
em cada i18n, testes/fixture e documentação. Sem alteração nova em main.cpp,
navigation.hpp ou CMake. Após integrar, conferir no console o ciclo de duas opções,
CAM= para visualizações e a permanência dos quatro períodos de envio. Aceite do
app no console, scheduler e revisão nativa do árabe continuam com o coordenador.
A funcionalidade de ordenar por data foi removida, não ficou como pendência oculta.

## 1. Entrega por item do brief

Implementação inicial: `5cbb0d3`; bancada/relatório: `9ef4a72`.
Correção da ordenação após medição real do coordenador: `37435df Remove ineffective upload-date search ordering`.
O histórico completo pedido (`git log --oneline v032..HEAD`) está em
`<repo>/.work/search/log-v032.txt`;
o recorte da F43 está em `.work/search/log-f43.txt`. Os 91 commits anteriores da
integração são a base, não trabalho atribuído à F43.

1. **Sugestões:** GET anônimo no host `suggestqueries-clients6.youtube.com`, cliente
   `youtube`, sem bootstrap/conta/cookies. Debounce de 300 ms, intervalo mínimo de
   750 ms entre inícios, cancelamento por edição/idioma/saída e geração para recusar
   respostas antigas. Limites: 1,5 s de conexão, 2,5 s totais, 32 KiB, profundidade
   12, 1.024 nós, oito sugestões e 256 bytes por texto. Rede e JSON ficam em worker
   próprio; o worker dorme sem consulta pendente. Falha deixa teclado, recentes e
   envio funcionando sem toast. Direita após a última tecla entra na lista; cima/
   baixo escolhem, A busca imediatamente, esquerda volta ao teclado, B volta à página.
2. **Filtros:** LB no teclado e nos resultados abre painel Series. Tipo: todos,
   vídeo, canal, playlist, ao vivo; duração: qualquer/curta/média/longa; envio:
   qualquer/hoje/semana/mês/ano; ordem: relevância/visualizações. O painel tem
   rascunho, aplicar, limpar e B para descartar. Resultados exibem as escolhas;
   query/params/idioma separam cache e cursor. Filtros incompatíveis ficam desativados.
   Pesquisa filtrada não cai silenciosamente em uma reserva que ignora os params.
3. **Canais/playlists:** a busca pública usa o catálogo tipado já existente, com
   abertura de canal/playlist e suas continuações. O teste exige os IDs certos e
   ausência de `player: pedido` ao abrir essas páginas.
4. **Recentes:** reutilizei o vetor de histórico da sessão, limite de seis. Uma
   lista combina sugestões e recentes, com deduplicação e selo Recente; não criei
   outro histórico nem persistência nova. Música conserva sua pesquisa separada.

São 22 chaves novas em pt/en/es/ar. Fontes, shaping e bidi são os da integração.
Revisão nativa do árabe continua pendente. Tenho menor certeza nas escolhas de
`SearchFilters` (الفلاتر), `SearchRecentTag` (الأخيرة), `SearchAny`
(أي مدة أو تاريخ) e `SearchRelevance` (مدى الصلة); a cobertura de glifos e o desenho
foram verificados, não a preferência terminológica de um falante nativo.

Contrato, fontes de pesquisa e limites: [docs/SEARCH.md](../../docs/SEARCH.md).

## 2. Provas originais da implementação inicial

Os números, hashes e caminhos desta seção pertencem a `5cbb0d3`. As provas e
capturas da correção de ordenação estão na atualização ao início deste relatório;
as imagens antigas com ordenação por data ficam apenas como histórico.

Todos os caminhos `.work/search` abaixo pertencem a este worktree. HOME/XDG/cache,
assets, configurações e conta dos testes foram isolados. O replay religa os objetos
do produto e substitui todo HTTPS, inclusive do player; não usa conta real nem a
rede doméstica. Foram quatro consultas reais ao YouTube, separadas pelo trabalho:
uma sugestão e duas páginas públicas para gravar o contrato; uma última sugestão
pelo curl/Mbed TLS de produção. Nenhuma mídia ou credencial foi coletada.

### Builds e contratos

```sh
PATH=<repo>/.work/src/usr/bin:$PATH \
  cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release \
  -DFFMPEG_PREFIX=<repo>/.work/ffmpeg-pc
cmake --build client/build -j2 --target tube360 search-test search-worker-test \
  arabic-text-test arabic-render-test display-layout-test yt-contract-test
client/build/search-test "$PWD/bench/search/fixtures"
client/build/search-worker-test
client/build/arabic-text-test
SDL_VIDEO_DRIVER=offscreen client/build/arabic-render-test "$PWD/client/assets"
client/build/display-layout-test bench
client/build/yt-contract-test bench/standalone/fixtures
```

Saídas: **5.440** checagens de busca; **5.013** do worker, incluindo 5.000 edições
enquanto uma resposta antiga fica bloqueada; **384** de texto árabe; **4.897** do
renderer real; **405** de layout; **292** do cliente YouTube. As 5.000 edições levaram
**0,741 ms** no PC (0,148 µs/edição), com transporte bloqueado. A versão temporária
sem a proteção de geração falhou em `late response is discarded`, como exigido;
o código do produto não foi alterado por essa mutação.

As quatro bancadas existentes de catálogo, OAuth Music, membros e conta foram
compiladas com C++20/O1 e religadas às bibliotecas do build de PC, usando a linha de
link de `search-test`, para não recompilar dependências. Saídas: **446**, **934**,
**74** e **260** checagens aprovadas. Fontes: `bench/catalog/{test,oauth-test,membership-test}.cpp`
e `bench/account/test.cpp`; argumentos: `bench/catalog/fixtures` e diretório de
conta descartável `.work/search/catalog`. Log: `.work/search/catalog-regression.log`.

O alvo opcional `search-live-probe`, executado uma vez com `--live`, retornou:
`production HTTPS suggestion request: 8 results (no cookies/account)`.
Log: `.work/search/live-probe.log`. Ele não integra a suíte offline.

Logs principais: `.work/search/{build-app-final,build-checks,contracts,worker,mutation,arabic-text,arabic-render,display,yt-contract}.log`.
O build final de PC não alterou o FFmpeg reaproveitado de F5.

### App de PC, controle e prints olhados

```sh
python3 bench/search/prepare.py .work/search/ui-final
python3 bench/search/e2e.py .work/search/ui-final
python3 bench/search/e2e.py .work/search/ui-final channel-open playlist-open recent-accept
```

**39 casos distintos aprovados**, com 24 capturas da busca/painel/resultados em
pt/en/es/ar, tanto HD como 4:3 com texto grande. Há ainda aceitar sugestão/recente,
busca árabe com números, falha silenciosa, envio após falha, B, cancelar, limpar,
abrir canal/playlist e medição. Os testes de destinos também exigem o browseId e
nenhuma tentativa de player. Manifesto com os 39 caminhos absolutos e números:

`<repo>/.work/search/ui-final/screenshots.json`.

As quatro pranchas foram abertas e inspecionadas. Cada uma mostra sugestões,
filtros e resultados em HD na primeira linha e 4:3/texto grande na segunda:

| Idioma | Caminho absoluto da prancha |
|---|---|
| Português | `<repo>/.work/search/contact-pt.png` |
| Inglês | `<repo>/.work/search/contact-en.png` |
| Espanhol | `<repo>/.work/search/contact-es.png` |
| Árabe | `<repo>/.work/search/contact-ar.png` |

No espanhol grande, o resumo usa duas linhas sem tocar a grade. No árabe, os blocos
estão alinhados à direita e as letras aparecem ligadas. Também abri individualmente:

- `<repo>/.work/search/ui-final/suggestions-arabic-query/screen.png`: oito linhas árabes e número misto.
- `<repo>/.work/search/ui-final/suggestions-arabic-query-crt-large/screen.png`: foco da segunda sugestão na faixa rolável de 4:3.
- `<repo>/.work/search/ui-final/offline/screen.png`: recentes disponíveis, sem alerta de sugestões.
- `<repo>/.work/search/ui-final/channel-open/screen.png`: página de canal com tabs, cabeçalho e itens do template.
- `<repo>/.work/search/ui-final/playlist-open/screen.png`: página de playlist, três faixas e Tocar tudo; sem player iniciado.

Capas são gradientes/placeholders de teste. Replays filtrados repetem a página
pública gravada: provam UI e parâmetros, não que o conteúdo gravado satisfaça o
filtro. Destinos reutilizam templates antigos de catálogo, com UC remapeado nos
endpoints; seus títulos não são apresentados como metadados atuais do card de
origem. Corrigi na revisão um fallback permissivo da bancada que respondia uma
continuação com a página de busca. Agora browseId/cursor exigem regra explícita;
as duas capturas de destino foram refeitas e abertas após a correção.

### Digitação e quadros

Depois dos builds e sem outro teste gráfico meu em paralelo, executei três rodadas
sequenciais de quatro segundos para cada caso: parado, 30 teclas a cada 60 ms e a
mesma digitação com resposta de sugestões atrasada em 1,8 s. Mesmos assets e app.
O histograma é de períodos de quadro do loop real SDL/Vulkan, sem alocação da sonda
por quadro. Não é um microbenchmark que exclui desenho ou processamento de input.

| Caso | Quadros | Média ponderada | P95 nas três rodadas | Maior quadro | Acima de 33,3 ms |
|---|---:|---:|---:|---:|---:|
| Parado | 8.345 | 1,437 ms | 2,2–2,3 ms | 23,760 ms | 0 |
| Digitando | 8.316 | 1,442 ms | 2,2–2,4 ms | 23,574 ms | 0 |
| Digitando / rede lenta | 7.440 | 1,612 ms | 2,2–8,5 ms | 24,961 ms | 0 |

Dados: `.work/search/serial-perf-{1,2,3}-{baseline,typing-fast,typing-slow}/frames.json`,
log `.work/search/performance-serial.log` e resumo `.work/search/performance-summary.json`.
As auditorias de pedidos ao lado confirmam que a consulta atrasada foi exercitada.

Não descartei silenciosamente os primeiros resultados: lotes exploratórios tiveram
picos, inclusive um caso lento com nove quadros acima de 33 ms. Há também picos no
primeiro uso de árabe nas capturas que entram direto na busca. A origem desses picos
não foi isolada. Uma rodada minha coincidiu com outra captura gráfica: ela foi
explicitamente invalidada em `.work/search/performance-final-rejected.txt` e refeita
em sequência. A tabela acima é somente a rodada final serial, no código entregue.
Ela demonstra ausência de bloqueio da digitação por rede nesse PC, não desempenho
no Xenon nem garantia de ausência de pausas de abertura/fontes.

### Xbox

```sh
JOBS=2 TUBE360_BUILD_ENV="$PWD/.work/search/build.env" \
TUBE360_BUILD_ID=4 TUBE360_VERSION=0.3.2-f43 \
PATH=<repo>/.work/src/usr/bin:$PATH \
flock ~/.cache/tube360-frota-xbox.lock \
systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
```

**Exit 0**, `[100%] Built target tube360`. Log `.work/search/build-xbox.log`.
O arquivo de ambiente contém somente quatro caminhos de SDK/toolchain, copiados
do ambiente descartável de F36; não foi lido `~/.config/tube360`.

| Artefato | Bytes | SHA-256 |
|---|---:|---|
| `client/build/tube360` | 8.329.800 | `6dae1e988e3391fbf30d0d361940679b0814f14ff00d93242100736dd8821cf3` |
| `client/build-x360/tube360.xex` | 11.489.280 | `6a961cfe6f911b10fd358b2d70c94bd345c5a41a1cfbafac8c88bb50f4709d48` |
| `client/build-x360/tube360-launcher.xex` | 995.328 | `ccefda6ef9e65de8bab54e6066cdf2f9729dca69eabf38537263a4978877501c` |

Há warnings dos componentes existentes (FFmpeg, plataforma Mbed TLS, picojson e
Cast); não declaro build sem warnings. O scanner avisa sobre 234 instruções que
Xenia ignora; não usei Xenia como prova de execução. Manifesto: `.work/search/artifacts.json`.

## 3. Não verificado e roteiro do coordenador

Não executei no Xbox nem medi sua CPU/GPU/RAM/frame time. Não usei conta do dono,
Nest, serviços reais, cache pessoal, cookies, VPS ou assinatura privada. Não publiquei
pacote. O novo worker usa o papel existente de background, HW5/prioridade −1; a
disputa com o sexto worker H.264 da F40 precisa de medição no console.

O roteiro de controle e logs está em [docs/SEARCH.md, Console acceptance](../../docs/SEARCH.md#console-acceptance-coordinator-only).
Após integrar a branch, o coordenador pode repetir o build acima e preparar seu
pacote descartável com `python3 tools/package-xbox.py --version 0.3.2-f43 --output-dir .work/search/package`.
Instalação/log seguem [o procedimento rc2](../../docs/CONSOLE-032-RC2.md), em pasta nova,
com launcher, app e oito fontes. Não executei nenhum comando de XBDM/FTP.

Sequência de aceitação:

1. Y; digitar devagar e rápido, sair com B durante consulta e reabrir. Confirmar
   controle responsivo, lista atual, sem alerta na falta de rede. Procurar no log
   `app-thread: suggestions HW 5 … priority -1`.
2. Direita após a última tecla, cima/baixo, A. Confirmar busca imediata da sugestão
   e um único recente ao repetir. Em árabe, testar também RB e texto misto.
3. LB → vídeo → duração curta → semana → ordem visualizações → Aplicar → Menu. Conferir
   resumo, duração/data dos resultados e paginação. LB nos resultados: aplicar
   recarrega; B descarta; Limpar volta a todos/relevância. Repetir para canal,
   playlist, ao vivo e as outras opções de envio/duração/ordem.
4. Abrir um canal e uma playlist. Exigir `navegação channel`/`navegação playlist`,
   sem `player: pedido` até escolher vídeo/Tocar tudo. B deve preservar filtros,
   seleção e página anterior. Comparar ordenação real com as ressalvas abaixo.
5. Repetir em pt/en/es/ar, HD e 4:3/texto grande. Registrar build/hash, duração,
   quadros/perdas enquanto digita e sob rede lenta, e confirmar ausência de
   atividade residual das sugestões ao iniciar vídeo. Validar scheduler com F40.

## 4. Riscos de integração

- `main.cpp`: bloco do Worker de catálogo (reserva filtrada), include/init/close
  de SearchUI, propagação de filtros em openNav/doSearch/syncMusicToken, lógica e
  desenho de SEARCH extraídos, branch modal, LB em NAV, cache do resumo e sondas
  exclusivamente de PC. F44 altera regiões próximas de Worker, navegação e menus;
  preservar seus hooks e estes campos ao resolver conflitos.
- `navigation.hpp`: campos de filtro, path com params, assinatura de open com
  argumento opcional e desenho do resumo. O caso de busca pública perde as tabs
  antigas; Music e canal mantêm as suas. Não recolocar o caminho que converte tudo
  em vídeo ao integrar a biblioteca local da F44.
- `yt/catalog.hpp`: suporte a `kind=search`, WEB em vez de Music, params e chave
  de cursor/cache. Alteração compartilhada com canal/playlist/Música, coberta pelas
  quatro bancadas antigas. O Client de home/categorias/next continua como antes.
- `i18n.hpp` e `i18n_ar.hpp`: 22 chaves de busca, inseridas perto de RecentSearches;
  não sobrescrever traduções novas da F44. `client/CMakeLists.txt`: três alvos
  opcionais de teste/probe; nada acrescentado ao CMake do Xbox.
- Docs: READMEs, CHANGELOG, STANDALONE e SEARCH. Os dois itens adjacentes do roadmap
  provavelmente precisarão de conciliação textual com F44.

## 5. Diferenças do brief para o código/provedor

- A base efetiva é rc2/F42, incluindo árabe; preservei isso e usei `-j2` e o lock.
- Já havia recentes em memória. Não criei armazenamento novo e os filtros também
  são de sessão/página, não configuração permanente.
- O menu público atual usa **menos de 3 / 3–20 / mais de 20 minutos**. Usei os
  parâmetros observados, não os antigos valores de quatro minutos.
- A medição real enviada pelo coordenador em 2026-11-25 07h mostrou que a
  ordenação por data é ignorada/não cronológica, enquanto CAM= produz visualizações
  decrescentes. A opção por data foi removida; restam Relevância e Visualizações.
  O filtro separado de período (hoje/semana/mês/ano) foi preservado. O relato e seu
  alcance estão registrados em docs/SEARCH.md e na fixture de evidência do coordenador.
- Em 4:3 a lista tem uma linha rolável para conviver com o teclado; as oito
  sugestões continuam acessíveis. Em HD há oito linhas e recentes abaixo delas.
