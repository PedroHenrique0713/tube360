# Console — 0.3.2-rc4 / build 6

Somente as mudanças desde a rc3 aceita pelo dono. Preparado localmente, sem acessar
o console, a conta ou o Nest. O aceite físico da rc4 continua pendente.

Público, bancadas OFF: `.work/f48/package/tube360-v0.3.2-rc4-xbox360.zip`
e `.7z`; pasta extraída `.work/f48/console/Tube360/`. Fechar o app e copiar a pasta
completa para **`HDD:\Apps\Tube360-F48-rc4`**, temporária e nova. Abrir `default.xex`
(lançador); manter `tube360.xex`, oito fontes e licenças. Usar `server=off` e
`sound=tv` inicialmente. O dono mantém/opera a conta apenas no console; nenhum
token ou cookie sai para o PC. Não substituir a instalação habitual nesta rodada.

1. **Busca com conta:** com a conta TV conectada, Y → `astronomia` → Pesquisar.
   Conferir resultados, LB/filtros e próxima página; repetir sem filtros e voltar
   com B. A busca é pública mesmo logado. Música, recomendações e biblioteca da
   conta devem continuar funcionando. Em falha, guardar `catalogo falha: rota=WEB/search
   http=... codigo=... api_code=...` e `navegação search`; não coletar credenciais.
2. **MP4 grande:** com o app fechado, criar `autoplay.txt` com `5PQYM_KSXag 120`
   na pasta temporária. Usar `video_mode=1080p30`, abrir pelo lançador e conferir
   imagem/som em 02:00. Guardar `cabecalho`, tamanho >2 GiB, pedidos/bytes,
   `header_ms`, `seek_ms`, `abertura_ms` e `primeiro quadro`; sem erro de moov/TRUN
   ou queda de fonte por cabeçalho. Buscar depois de 02:30:00 e voltar a 02:00.
   Fechar, espaçar e repetir com `NNS5Piu-EII 0`; conferir abertura, pausa e seek.
   O título HDR/4K não qualifica o AVC selecionado como HDR. Abrir um vídeo curto
   depois e verificar que o perfil não foi penalizado. Remover `autoplay.txt`.
3. **Qualidade recortada:** abrir `R6MlUcmOul8`. Se a oferta continuar
   1920×800/24, detalhe, badge e menu Y devem dizer **1080p24**. O log mantém
   800 linhas, 24 fps, quatro workers e filtro completo. Conferir imagem/posição
   ao trocar qualidade; a classe do rótulo não aumenta o trabalho do decoder.
4. **Start e Música:** em miniatura na Home, busca, canal ou playlist, conferir
   **☰ Salvar / opções** e abrir Assistir mais tarde. B volta preservando seleção.
   Repetir pt/en/es/ar em HD e 4:3/texto grande; a dica deve caber inteira.
   Música da conta: recentes usa relógio sem seta solta, playlists usa lista e
   curtidas usa coração com ponta arredondada. Abrir cada atalho e voltar por B.
5. **Data do console / “sem Xbox Live”:** F32 deixou provas locais com
   certificados vencidos/futuros e telas traduzidas (`tests/reddit/tls.py` e
   `home_errors.py`); não são prova de relógio errado no Xbox do dono. Se aparecer
   a mensagem para acertar data/hora, conferir o relógio mostrado e ajustá-lo no
   painel do Xbox, depois repetir X. Xbox Live não é requisito. TLS continua
   validado; não aceitar certificado inválido como solução. Guardar a falha e
   `clock_epoch` antes/depois, sem alterar a data somente para provocar o erro.
6. **Nest:** quando a caixa estiver disponível, usar somente seu endereço
   autorizado na pasta temporária. Repetir um dos vídeos que agora abrem e um
   seek; conferir áudio contínuo/sincronizado, pausa, retorno e ausência de som
   duplicado na TV. Isso valida a integração; não há mudança nova de relógio Nest.
   Sair normalmente por B e confirmar STOP/silêncio antes de limpar a pasta.

Bancada 1080p F40 separada do público: `.work/f48/bench-console/`, casos
`tv137`, `tv137-1080`, `motion137`, `full137` e `nest137`. Seguir os critérios
e comandos da [rc3](CONSOLE-032-RC3.md), trocando somente worktree/prefixo F45 por
F48 e a pasta para `HDD:\Apps\Tube360-F48-<caso>`. Rodar o XEX de bancada
diretamente. Remover `mudo.txt` da cópia local **e** remota para aceite audível;
1080p exige HDMI progressivo selecionado no painel. Nenhum dado de PC aprova FPS,
memória, som ou sincronismo no Xenon. O kit não pode substituir o pacote público.

Guardar hashes, logs, tempos, itags e resultado visual/acústico. Não reiniciar nem
usar magicboot para encerrar. Não apagar journal Nest com STOP pendente. Publicação
e instalação na pasta habitual aguardam decisão do dono após o teste.
