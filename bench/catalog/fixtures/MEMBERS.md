Capturas públicas sem login de 2026-11-24, sem usar a conta do dono:

- `members-en.json` e `members-pt.json`: primeiro `lockupViewModel` de
  `https://www.youtube.com/playlist?list=UUMOXuqSBlHAE6Xw-yeJA0Tunw&hl=en`
  e `hl=pt`. Estrutura e valores de badge são os recebidos do YouTube.
- `members-player.json`: `playabilityStatus` de
  `https://www.youtube.com/watch?v=qiLUvCt3k6g&hl=en`, sem cookies.

As gravações foram reduzidas aos caminhos usados pelo parser. Título, ID, canal,
URL de imagem e nome do nível foram substituídos por marcadores; endpoints de
menus, rastreio, sessão e visitante foram retirados. Não há tokens ou cookies.

`members-cases.json` contém **variantes sintéticas**: TV, renderers antigos, Music,
acesso explicitamente concedido (`isPlayable: true`) e negativos. Não é uma
captura de conta membro. O caso de acesso pago real fica para o dono na integração.

Os três parsers (C++, Node e Python) usam os mesmos arquivos. A regra mantém
acesso desconhecido como `members` e deixa o player consultar o YouTube; não
interpreta um endpoint watch nem inscrição gratuita como autorização.
