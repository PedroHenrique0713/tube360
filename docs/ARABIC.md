# Texto árabe

Em Configurações → Idioma, escolha العربية (`lang=ar`). O código nativo do YouTube envia `hl=ar`,
`gl=SA`, inclusive após trocar o idioma sem reiniciar. Títulos e nomes árabes passam pelo mesmo
renderizador em pt/en/es; a direção do parágrafo vem do primeiro caractere forte do conteúdo.

## Composição e direção

O núcleo GNU FriBidi 1.0.16 (Unicode 16, LGPL-2.1+) resolve bidi, parênteses, números, trechos latinos,
isoladores e formas contextuais. Ativamos as formas de apresentação e as quatro variantes de lam-alef;
HarfBuzz/ICU não entram no binário. As tabelas e os fontes C necessários estão no repositório,
sem download de dependência em tempo de execução. Licença/proveniência: `client/third_party/fribidi/`.

stb_truetype rasteriza os glifos resultantes. Harakat conservam avanço zero, são centralizados no glifo-base
e ganham separação vertical quando combinados com shadda. É uma composição mínima: não implementa todas
as âncoras GPOS, caligrafia ou ligaduras opcionais de uma fonte OpenType. As formas de FB50–FBFF, FE70–FEFC,
ligaduras de palavras FDF0–FDFF e árabe básico/suplementar/estendido disponível na fonte entram no atlas.
Ligaduras opcionais FC00–FDEF e emoji não têm cobertura garantida. Caracteres sem glifo no atlas da fonte
são omitidos, sem substituição por `?`; um ponto de interrogação literal continua aparecendo.

Na preparação do cache por texto/fonte, o renderizador consulta a cobertura real do atlas e remove
caracteres sem glifo. ZWJ/ZWNJ e controles bidi são preservados até o FriBidi compor/reordenar; portanto
a omissão de emoji não elimina os controles de junção árabe. O resultado lógico e visual fica em cache.
Texto composto só por emoji sem cobertura fica vazio, sem largura nem geometria de substituição.
Medição, desenho, degradê, alinhamento, reticências e quebra usam essa mesma preparação.
O corte e a quebra usam texto lógico, medem novamente as formas da borda e só depois reordenam cada linha.
Não se corta uma sequência UTF-8 nem se separa harakat de sua base. Medição e desenho usam a mesma sequência
composta. Strings compostas e quebras têm caches limitados a 512 KiB cada (mais estruturas de contêiner),
invalidados quando a fonte/resolução é recarregada; o texto estável não recompõe letras nem recalcula quebras a cada quadro.

Mantemos a posição da barra lateral, grade, botões, transportes e comandos do controle. Os blocos de títulos,
metadados, busca, configurações e texto corrido recebem alinhamento à direita; botões e avisos centrais
continuam centrados. Isso preserva a orientação espacial do controle e o B para voltar. Não espelhamos vídeo,
barra de tempo, código de login, marca ou números. No teclado, as setas acompanham a posição física das teclas;
RB alterna entre árabe básico (30 letras/sinais e números 0–9) e latim. Variantes adicionais de hamza, alef
maqṣūra e diacríticos podem ser introduzidas pelo teclado físico do PC; não há editor de seleção/cursor bidi.

## Fonte, instalação e memória

Distribuímos Noto Sans Arabic 2.012 (Light 300, Regular 400, SemiBold 600, Bold 700), sob SIL OFL 1.1, sem
modificações. A altura de rasterização é 1,60 vez a da Inter, compensando as métricas verticais da Noto;
o peso acompanha o peso original de cada estilo. A Inter continua sendo usada por caractere para latim,
números ocidentais e pontuação da interface, com a mesma linha de base.

Fonte oficial: https://github.com/notofonts/arabic/releases/tag/NotoSansArabic-v2.012
Arquivos `googlefonts/ttf/` da distribuição, renomeados para nomes simples:

| Peso | Arquivo | Bytes |
|---|---|---:|
| 300 | `noto-sans-arabic-300.ttf` | 281988 |
| 400 | `noto-sans-arabic-400.ttf` | 292600 |
| 600 | `noto-sans-arabic-600.ttf` | 310824 |
| 700 | `noto-sans-arabic-700.ttf` | 324028 |
| Total | quatro fontes | 1209440 |

No ZIP do pacote auditado, as quatro fontes somam **509136 bytes de payload comprimido**.
Em um ZIP isolado de medição com DEFLATE nível 9, são 509923 bytes de payload (510441 bytes com cabeçalhos).
A biblioteca estática FriBidi mede 124884 bytes no PC e 121288 bytes no PowerPC; a soma de código/dados
dos objetos é cerca de 100 KiB, sem HarfBuzz. Não é uma comparação de tamanho total do app com uma base recompilada.

As quatro Noto ficam **ao lado de default.xex e tube360.xex**, com as quatro Inter. O empacotador inclui
as oito fontes e licenças em ZIP/7z e atualiza START-HERE. Uma atualização somente do executável não instala
arquivos de fonte: ao migrar de versão sem árabe, copie a pasta completa uma vez.

Antes do primeiro texto árabe, não há leitura de Noto nem atlas extra. Na primeira medição árabe, é criado
um atlas R8 (todos os estilos), reutilizado em seguida:

- SD/720p, normal ou texto grande: 2048 × 4096 = **8 MiB de pixels de GPU**.
- 1080p, normal ou texto grande: 4096 × 4096 = **16 MiB de pixels de GPU**.
- Durante a criação: vetor de pixels de mesmo tamanho, quatro TTF (1209440 bytes), dados de empacotamento e
  mapas de glifos. O upload ainda usa um staging temporário do tamanho do atlas: pico adicional aproximado
  de 25,2 MiB em 720p ou 49,2 MiB em 1080p, além dos mapas/caches e alinhamento do driver. Os pixels CPU/TTF temporários são liberados; os mapas ficam enquanto as fontes forem usadas.
- A resolução/texto grande recriam os atlas e invalidam as medidas. A primeira carga pode causar uma pausa;
  títulos árabes podem provocar essa carga também em pt/en/es. Não há upload/rasterização contínuos no player.

O log `Arabic fonts:` informa quantidade de glifos, dimensões e bytes. O alinhamento real de memória da GPU,
latência da primeira carga e folga de decode ainda precisam ser medidos no Xenos. Não extrapole o PC para o Xbox.
Se faltar uma Noto, o app mostra um aviso com o nome dos arquivos (inglês se não puder desenhar árabe) e registra
`Arabic font missing:`. Sem árabe no conteúdo, as quatro Inter continuam suficientes para desenhar a interface.

O lançador usa a fonte bitmap ASCII do xsdk antes de iniciar o renderizador. Com `lang=ar`, exibe somente
a marca `Tube360` durante esse breve bootstrap; o aplicativo completo abre em árabe.

## Tradução e integração

`i18n.hpp` mantém `TUBE360_TEXT(X)` com quatro argumentos (chave/en/pt/es). O árabe fica em
`i18n_ar.hpp`, em um switch por `Key`; `Lang::AR` foi acrescentado depois de ES. Novas strings de F32/F34/F35
continuam compilando e têm fallback inglês em árabe enquanto aguardam tradução.

Para integrar cada string nova, adicione `case Key::NovaChave: return "…";` ao switch árabe e rode
`arabic-text-test`; ele exige tradução explícita para toda chave, sem deixar o fallback mascarar faltas.
Mantenha nomes de botões A/B/X/Y/LB/RB, códigos, URLs e nomes próprios na forma original. Não inverta as strings.
Revise o aviso de fonte da F32 para citar **oito fontes**, incluindo os quatro nomes Noto e as duas XEX.
Inclua `libtube360-text.a` no kit de relink e a licença FriBidi no pacote.

A tradução usa árabe padrão moderno; falta revisão por falante nativo. Menor certeza terminológica:
Charts (الأكثر رواجًا), Mixes (مزيج), PlayerTimeline (شريط التقدم), VideoFitLetterbox (ملاءمة / أشرطة),
SpeakerSync (مزامنة السماعة) e a escolha regional de “cast” (البث). As traduções técnicas devem ser revisadas
no contexto da tela. Dados e mensagens arbitrárias devolvidos pelo YouTube não são traduzidos aqui;
o YouTube ainda pode escolher metadados em inglês por não reconhecer `ar`.

## Provas e console

[bench/arabic/README.md](../bench/arabic/README.md) descreve testes, replay fechado e capturas.
A rodada da F36 passou 319 verificações de texto, 3745 do renderizador, 337 de layout/locale e 23 capturas.
O Xbox compilou com GCC milestone 52 sob o lock; o pacote passou a auditoria de hashes/ZIP=7z, oito fontes
e licenças. Essas são provas de compilação/PC, sem execução no console.

As imagens de prova mostram o aplicativo de PC com títulos públicos gravados; arte e vídeo são sintéticos,
sem conta, cookies, rede do YouTube ou acesso ao console. O relatório local da frente registra os resultados.

No Xbox, use uma pasta de teste gravável com o pacote completo e confira:

1. Menu do Xbox em 720p: abrir `default.xex`, escolher العربية em Configurações → Idioma, sair e reabrir.
   O idioma deve persistir. Home, busca, Configurações, player e erro devem ter letras conectadas;
   B deve voltar em todas as telas, com as mesmas direções de controle.
2. Procurar `Xbox 360` com o teclado latino (RB), alternar para árabe e digitar ب três vezes.
   Confira ب inicial/medial/final, números 360/2026 e parênteses em títulos mistos. A letra ب três vezes
   é uma prova de composição, não uma consulta de conteúdo específica.
3. Buscar os IDs públicos da fixture (`zjSVzGsljwA`, `mMk4FZUZiTI`, `647fij3ECvo`) por texto ou telefone.
   Trocar para português e conferir que títulos árabes continuam corretos. Testar لا، لأ، لإ، لآ e harakat.
4. Texto grande em HD, depois 480i/480p 4:3: conferir margens, valores, reticências e dicas do controle.
   Em HDMI/1080p, conferir `Arabic fonts: ... atlas 4096x4096` sem `atlas full`.
5. Reproduzir o mesmo VOD 720p30 por dois minutos, comparando base e árabe. Registrar tempo da primeira carga,
   memória disponível, decode, quadros apresentados/perdidos, fila e áudio. Conferir se a primeira aparição
   de título árabe durante reprodução causa falha. A carga única deve ser avaliada com e sem atlas já quente.
6. Em uma cópia descartável, omitir uma Noto, abrir árabe e conferir aviso legível com os nomes; restaurar o
   arquivo e reabrir. A F32 pode adicionar um diagnóstico de início: integrar os nomes de todas as fontes.

Log esperado: `layout: ...`, uma linha `Arabic fonts: ...` por carga de resolução, sem repetição por quadro. Não há validação em console real nesta frente.


## Integração 0.3.2: revisão linguística pendente

Todas as 41 chaves novas de início/TLS, qualidade manual, SponsorBlock e Música têm valor árabe explícito em `i18n_ar.hpp`; a macro EN/PT/ES continua com quatro argumentos. Os testes exigem cobertura sem fallback inglês. Revisão nativa pendente, com menor certeza nestes grupos:

- `SponsorCategories`, `SponsorSkip`, `SponsorSkipped`: uso de الرعاية para patrocínio e descrição de promoção/inscrição.
- `ManualQualityHelp`, `ManualQualityPressure`, `ManualQualityFailed`: termos de perfil adaptativo e manutenção da fonte fixa.
- `YourMusic`, `PublicMusic`, `MusicRecent`, `MusicLiked`: distinguir catálogo da conta, público, recentes e curtidas em rótulos curtos.
- `SafeOutputHelp`, `StartupVideo`, `StartupStorage`: vocabulário técnico de saída/GPU/permissão.

Não há revisão por falante nativo nem garantia de posicionamento tipográfico completo sem GPOS. A substituição de atlas conserva a espera da GPU e o reset de comandos da F32 antes de retirar recursos árabes e latinos. As fontes Noto continuam sob demanda; o preflight de início exige Inter, e a carga de árabe informa a falta de Noto. O pacote completo traz as oito fontes.

A tela de emergência embutida (falha de fonte/SDL/GPU/pasta/launch) usa inglês quando o idioma salvo é árabe: o bitmap independente de TTF não cobre árabe. As traduções continuam explícitas para a interface completa. O bootstrap do lançador mostra a marca ASCII Tube360.
