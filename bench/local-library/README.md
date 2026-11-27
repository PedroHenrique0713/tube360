# F44 — biblioteca local e miniaturas

Da raiz do worktree:

```sh
JOBS=2 cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 arabic-text-test
cmake -S bench/local-library -B .work/local-build
cmake --build .work/local-build -j2
ctest --test-dir .work/local-build --output-on-failure
client/build/arabic-text-test
python3 bench/local-library/e2e.py .work/provas/f44/ui
```

O teste de armazenamento faz I/O real em uma pasta descartável, injeta ENOSPC depois de uma escrita
parcial, erro em fsync e falha no rename e exige a preservação byte a byte do arquivo anterior. Testa
roundtrip, limites, LRU, arquivo inválido/truncado/grande, limpeza e fronteiras de retomada. O teste do
worker disputa 300 snapshots com um clear final e relê o arquivo depois do join.

O teste de canal usa Vídeos/Ao vivo/Playlists de `bench/catalog/fixtures` e a nova gravação de Shorts.
Também roda os quatro testes anteriores de catálogo/OAuth/membros/conta, com transportes falsos.

O E2E religa os objetos de produção do app trocando somente HTTPS pelo replay fechado. HOME, XDG,
assets, conta fictícia e cache ficam sob o diretório informado. Cast está desabilitado. Todos os pedidos não gravados falham fechados. Nunca aponte esse teste para uma pasta real.
As capturas contêm arte em gradiente e mídia testsrc/senoide de 120 s, sem imagens/áudio dos vídeos públicos.
Há 39 casos nos quatro idiomas, 480p/4:3/texto grande, conta fictícia com ações WL/local separadas no Menu, limpeza confirmada/cancelada,
arquivo corrompido, falha de escrita, checkpoint de 30 s, retomada no player e início do zero. A captura encerra normalmente o aplicativo,
exercitando o flush final. `results.json` relaciona as imagens; logs ficam junto de cada PNG.

## Proveniência e limites

`fixtures/channel-shorts.json` foi reduzido de uma única consulta HTTPS pública, sem cookies/login,
a `https://www.youtube.com/@GoogleDevelopers/shorts`, em 2026-11-25. Preserva quatro cartões com o nesting
real `richGridRenderer → richItemRenderer → shortsLockupViewModel`, os IDs, títulos e fontes de capa.
Foram removidos responseContext, visitor/session IDs, tracking, menus, sequências de reprodução e outros
cartões. O parâmetro de navegação foi substituído por `SHORTS_FIXTURE`; o replay o usa somente localmente.
Os parâmetros das URLs públicas de imagem são os originais. Não contêm credenciais da conta.

Os outros três DOMs vêm das gravações públicas sanitizadas já versionadas pela bancada de catálogo.
URLs `//`, WebP e imagens indisponíveis são simulações separadas. A resposta real nova é JPEG e não
prova o relato original de miniaturas brancas nas três abas antigas. O teste exige endereço aceito por
cartão e o E2E exige upload de textura; uma textura de avatar/banner sozinha não prova a capa do cartão,
por isso os PNGs também precisam ser inspecionados.

## Roteiro para o coordenador no Xbox

Não executar em cima da pasta/conta real. Copiar o pacote completo compilado para uma pasta descartável
usando o procedimento habitual do coordenador. Manter `nest.txt` e arquivos de conta fora
dessa pasta; usar `sound=tv` e `sponsorblock=off`. Não substituir o lançador pelo app.

1. Abrir `default.xex`. Home deve oferecer Assistir mais tarde e Continuar assistindo sem login.
   Menu num VOD público → lista local. Abrir Biblioteca e conferir o atalho/miniatura.
2. Assistir um VOD conhecido por 40 s. View volta à grade; reabrir mostra retomada. A continua,
   baixo/A começa do zero, B cancela. Verificar `player: pedido <id> inicio=...` e a barra vermelha.
3. Fechar normalmente, abrir novamente e conferir lista/posição. Esperar 35 s sem sair, forçar a saída
   apenas na pasta descartável e verificar o último checkpoint. Medir fluidez/áudio durante o checkpoint.
   Log: `local: save revision=... ok=1 later=... history=...`; não deve aparecer por quadro.
4. Concluir o VOD, voltar e verificar que a posição saiu. Live e Music só áudio não devem gerar retomada.
5. Configurações: abrir cada limpeza, B cancela; reabrir, baixo/A confirma. Reiniciar e verificar que só
   o conjunto escolhido foi apagado. Repetir em pt/en/es/ar, 720p e 480i/480p 4:3/texto grande.
6. Só se o coordenador tiver uma conta descartável autorizada: entrar e conferir WL separado dos atalhos
   locais; verificar que os vídeos locais não foram enviados. Esta frente não usou a conta real.
7. Visitar @GoogleDevelopers, abas Vídeos/Shorts/Ao vivo/Playlists, abrir/voltar e verificar capas,
   resolução, enquadramento e duração. Uma comunidade pode não oferecer todas as abas. Observar
   `miniatura: JPEG fallback ok=1` para conteúdo não decodificável e ausência de retries por quadro.
8. Em cópia descartável, arquivo local truncado deve abrir com aviso. Para erro de escrita, usar uma
   pasta/mídia de teste sem espaço (não encher o HDD real) e conferir erro visível e arquivo anterior
   preservado. Validar que rename substitui o destino no FATX e medir o tempo da gravação.

Compilação do XEX e testes no PC não medem Xenos, FATX, perda de energia nem desempenho do decode no Xbox.

## Revisão das contagens e glifos

```sh
python3 bench/local-library/e2e.py .work/provas/f44-revisao/ui --review
```

São 28 capturas novas: 24 combinações de pt/en/es/ar × 720p/480p 4:3 com texto grande ×
3 vídeos/12 posições, 1/1 e zero/zero. O foco na última linha deixa as duas ações de limpeza visíveis.
O valor vem do arquivo semeado para cada caso; o rodapé apresenta limites explícitos. As outras quatro
capturas mostram Shorts gravados e títulos sintéticos com emoji, só emoji e árabe com emoji em HD/SD.
A ausência de título no cartão só de emoji é intencional; a legenda Demo identifica o caso.
Os arquivos originais mantêm os títulos completos: a omissão pertence à preparação visual, não altera
os dados nem o protocolo. As suítes `arabic-text-test`, `arabic-render-test` e `display-layout-test`
conferem cobertura, composição, bidi, fitting, cache e troca de tamanho da fonte.
