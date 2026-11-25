# Tube360 0.3.2 — resultado do console em 25/11/2026

Resultados informados pelo dono após os testes no Xbox. A preparação da build final
não acessa novamente o console, a conta nem o Nest.

| Verificação | Resultado informado |
|---|---|
| Fonte 1080p30, vídeo de ação | 30,00 fps, sem perda de quadros |
| Fonte 1080p30, futebol | 29,97 fps; uma correção de sincronia por minuto, igual ao 720p |
| Saída 1080p, painel do Xbox em 1080p progressivo | Imagem nítida; requer HDMI |
| Painel/TV em saída 720p | O app reduz a fonte; movimento rápido pode apresentar serrilhado |
| Música com a conta | Validada |
| Busca com filtros e sugestões | Validada no controle |
| Assistir mais tarde e continuar sem conta | Validados no controle |
| Menu Y Qualidade | Validado no controle |
| SponsorBlock no botão | Validado no controle |
| Configurações | Validadas no controle |
| MP4 acima de 2 GiB | Correção do defeito do GCC validada |

Essas medições são do Xbox do dono e dos vídeos exercitados. Não comprovam todas as
placas, redes, taxas de bits ou fontes. Não há promessa de 1080p60, HDR, VP9 ou AV1.
Placas Fat continuam sem teste físico, e árabe continua sem revisão nativa.
A paridade das recomendações de Música com o cliente da TV não foi estabelecida.

A distribuição final identifica-se como **0.3.2 / build 7**, com lançador em
`default.xex` e app em `tube360.xex`. Não substituir o lançador pelo payload.
A validação acima é a informação física recebida; o teste de instalação dos bytes
finais do pacote continua sendo uma etapa de publicação do mantenedor.

Depois de copiar o pacote completo para uma pasta temporária gravável, abrir o
lançador sem `server.txt`, conferir versão/ID, as oito fontes, navegação por A/B,
Home e abertura de um vídeo. Preservar a instalação anterior até concluir esse
smoke test. Nenhum comando de instalação, reboot ou acesso ao console é executado
na preparação desta entrega.
