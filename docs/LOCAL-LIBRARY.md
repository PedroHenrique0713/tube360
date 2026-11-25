# Assistir depois e retomar neste console

Sem conta, Menu num cartão de vídeo → **Assistir mais tarde · console** adiciona ou remove o vídeo.
A Home oferece **Assistir mais tarde** e **Continuar assistindo**. A Biblioteca também oferece os dois
atalhos. Tudo funciona pelo controle; B fecha o menu ou volta à página anterior.

Ao entrar na conta, a lista `WL` e o histórico da conta continuam com o comportamento anterior.
Os atalhos **Assistir mais tarde · console** e **Continuar · console** continuam disponíveis na Home e
na Biblioteca. O Menu ganha uma ação local adicional, separada da ação da conta. Não há importação,
sincronização nem envio automático dos registros locais. Sair da conta não apaga os registros locais.
Quem compartilha a pasta do app também compartilha essas listas; não há perfis locais separados.

Abrir um vídeo salvo no histórico local mostra duas opções: **Continuar assistindo** e **Começar do
início**. B cancela a abertura. Pedidos do celular e reposicionamentos do player conservam a posição
explícita solicitada, sem abrir esse diálogo. Reprodução de VOD iniciada pelo controle sem conta salva
progresso; live, Music somente áudio e reprodução iniciada pelo celular não entram no histórico local.
Com conta, a reprodução continua registrando o histórico da conta; a posição local anterior é preservada.

Uma posição só permanece quando é ≥ 10 s, está mais de 10 s antes do fim e abaixo de 95% da duração.
Vídeos com duração desconhecida ou ≤ 20 s não geram retomada. O fim natural remove a posição.
As miniaturas mostram a mesma barra vermelha usada na Home da conta, inclusive quando o catálogo
público não informa duração: nesse caso, usamos a duração registrada durante a reprodução.

## Arquivo e limites

`local-library.json`, ao lado de `tube360.xex`, contém IDs, títulos, canais, durações e posições.
Não contém tokens, cookies nem links de mídia. É uma lista de referências; os vídeos não ficam offline.
Não inclua esse arquivo nem `local-library.json.tmp` em Git, releases ou pacotes compartilhados.
O empacotador existente usa uma lista explícita de arquivos e não inclui esses dados.

- Assistir depois: até **200 vídeos**, removendo o salvo mais antigo ao exceder o limite.
- Retomada: até **500 posições**, removendo a menos recentemente atualizada.
- Títulos/canais: até 384/192 bytes UTF-8; leitura limitada a 1 MiB e profundidade JSON limitada a 8.
- A ordem usa um contador persistido, sem depender da data correta do console.

A memória da interface é a fonte enquanto o app está aberto. Um worker escreve snapshots limitados:
edições de lista/limpeza, checkpoint de reprodução a cada **30 s**, troca de vídeo e fechamento normal.
O worker usa HW 5, prioridade de fundo (-1) no Xbox. Só há uma gravação ativa e uma pendente; uma alteração mais nova substitui a pendente. Não há gravação
por quadro. A última alteração e a limpeza prevalecem sobre snapshots anteriores ao fechar o worker.

A gravação usa arquivo temporário na mesma pasta, verificação de write/close e rename sobre o destino.
No PC, também verifica fsync; o SDK do Xbox não oferece essa garantia, como no armazenamento da conta.
O rename precisa ser validado no sistema de arquivos real do console. Não apagamos o destino antes do
rename. Em erro, o arquivo anterior permanece e a interface informa que não conseguiu salvar; alterações
podem continuar na memória e nova alteração/checkpoint tenta gravar novamente. Não é garantia contra perda
de energia durante a gravação ou saída forçada pelo dashboard. Uma saída abrupta pode perder os últimos 30 s.

Arquivo ilegível, versão desconhecida, truncamento, excesso de tamanho/itens ou dados inválidos mostram
aviso e iniciam listas vazias. A leitura não substitui o arquivo danificado; a próxima alteração explícita
ou posição válida o substitui por um arquivo novo. Um `.tmp` abandonado nunca é promovido na inicialização.

## Apagar os dados

As duas últimas linhas de Configurações são **Apagar histórico local** e **Apagar lista local**.
A mostra uma confirmação com **Voltar** selecionado. Para apagar, pressione baixo e A. B cancela.
A operação afeta somente a categoria escolhida, persiste em disco e não altera a conta do YouTube.

## Miniaturas de canal

As gravações públicas existentes de Vídeos, Ao vivo e Playlists continuam gerando URLs de capa válidas.
A captura pública de Shorts feita em 2026-11-25 usa `shortsLockupViewModel`, `onTap` e
`thumbnailViewModel`; antes, esses cartões eram ignorados. O canal agora oferece a aba Shorts e interpreta
esses campos, além de `reelItemRenderer`.

A seleção de imagem percorre todas as fontes dentro do campo de capa e escolhe a maior até 1280 pixels,
sem preferir cegamente o primeiro array `thumbnails` a `sources`. URLs `//` ganham HTTPS e passam pela
mesma lista restrita de hosts. URLs de outro host e relativas ao caminho continuam rejeitadas.
Se uma imagem ytimg não pode ser decodificada por stb_image (JPEG/PNG), o worker tenta uma vez a capa
`mqdefault.jpg` do mesmo ID. Isso atende WebP sem adicionar um decodificador ao console. Não reescrevemos
URLs arbitrárias de avatares/banners. A imagem alternativa pode ter menos resolução ou outro enquadramento.

A fixture de Shorts é gravação sanitizada. Os casos de WebP/URL `//` são variantes simuladas, declaradas
como tal; não provam que essa era a causa exata do relato do usuário em todas as abas. Não houve reprodução
do defeito original nas três gravações JPEG antigas. A bancada usa arte e vídeo sintéticos.

## Validação

Veja [bench/local-library/README.md](../bench/local-library/README.md) para comandos, proveniência e roteiro
no console. Há traduções explícitas em inglês, português, espanhol e árabe; o árabe ainda requer revisão
por falante nativo, especialmente `LocalHistory`, `LocalLimit` e os termos de armazenamento/retomada.
