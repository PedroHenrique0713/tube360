# F35 — proveniência das respostas

Captura pública em 2026-11-24, sem login, cookie ou token real:

- `/tv` publicou contexto TVHTML5 `7.20261123.19.00`. `tv-context.html`
  preserva somente nome/versão; o visitante foi substituído por `fixture-tv-visitor`.
- `/youtubei/v1/guide` em `www.youtube.com`, contexto TVHTML5, publicou a rota
  `FEtopics_music`. O POST de browse nessa rota retornou HTTP 200 e 75.804 bytes.
- `tv-music-public.json` é o DOM dessa resposta, reduzido a três itens por
  prateleira. Foram removidos responseContext/tracking, menus e acessibilidade;
  nomes, IDs de conteúdo, URLs e cursores foram substituídos por marcadores Demo.
  A estrutura de prateleira/tile, tipos e durações permanecem gravados, não
  inventados a partir do parser. Nenhum item corresponde à biblioteca do dono.
- `music-invalid-bearer.json` é o erro HTTP 401 literal de WEB_REMIX no host
  Music, enviado com `Bearer deliberately-invalid-fixture-token`. Não contém
  tokens ou IDs de sessão. Não representa o grant do console.

O replay visual usa esse DOM TV com nomes adicionais explicitamente fictícios.
Respostas privadas HTTP 200 e recusas específicas são **simulações**, não
capturas autenticadas. A autorização para a conta verdadeira permanece sem
verificação. O teste de cursor adiciona um cursor sintético ao DOM público.

A pesquisa e o roteiro do console estão em `docs/MUSIC-ACCOUNT.md`.
