# Orçamento da fila 1080p30

Não é consumo total medido do Xbox. O aceite de memória permanece pendente do log do app.

1920×1088, YUV 4:2:0 8-bit: **3.133.440 B** por quadro (2,988 MiB), antes de pitch/alinhamento.
O decoder e o renderer já compartilham os planos quando get_buffer2 mapeado funciona;
não somar outra cópia NV12: o formato real aqui é YUV420P, três planos R8.

| Retenção | Quantidade conservadora | Limite a 4 MiB/superfície |
|---|---:|---:|
| Fila pronta | 18 | 72 MiB |
| Workers FRAME | 6 | 24 MiB |
| Referências DPB do nível 4.0 em 1080 | 4 | 16 MiB |
| Reorder B | 2 | 8 MiB |
| Fences de desenho | 2 | 8 MiB |
| Quadro corrente | 1 | 4 MiB |
| Total deliberadamente sem descontar sobreposição | **33** | **132 MiB** |

O pool existente limita a soma real das alocações Vulkan a **160 MiB/48 superfícies**;
não foi ampliado. O log `video-memoria` informa pitch, tamanho efetivo e total a cada alocação.
As dimensões/refs reais precisam ser verificadas: streams fora dessa hipótese não recebem
uma promessa a partir da tabela. Staging de reserva pode adicionar cópias fora do pool.

A mudança de 10 para 18 posições acrescenta no máximo oito imagens: **25.067.520 B** densos,
com orçamento arredondado de **32 MiB**. O início passa de seis para doze: 400 ms de conteúdo
(366,7 ms entre primeiro e último PTS), mais 200 ms de proteção inicial que antes.
Pico p95 de entrega de 161 ms da F30 cabe nesse estoque; **p95 não limita o pior caso**, e
estoque finito não resolve média acima de 33,3 ms.

Não se afirma que 160 < 512 prove que o app inteiro cabe. A admissão consulta
`MmQueryStatistics.title.available_pages` **com o restante do app já carregado e o decoder
aberto**. Só aumenta a fila com pelo menos **96 MiB livres**, cobrindo os 32 extras e mantendo
64 MiB de margem naquele momento. A consulta falhar mantém 10/6. O que já está ocupado por
kernel, título, UI/miniaturas, rede, áudio, pilhas e decoder entra nessa medição real, em vez
de receber um desconto fictício. Rede reserva 6 MiB; PCM de 6 s estéreo float é ~2,02 MiB.

`decod-orcamento` amostra a disponibilidade durante o app; a sonda de 120 s registra mínimo,
fila inicial/final e médias dos dez primeiros/últimos pontos. O aceite exige ≥64 MiB livres,
sem erro de alocação/fallback de planos ou queda de fila sustentada. São amostras de 1 s,
não uma captura de todo pico transitório. Até receber esse log do console, **caber nos 512 MiB
com o restante do app não está demonstrado**. No PC, SIZE_MAX/4294967295 no campo de MiB é
sentinela de admissão sem limite, não memória medida nem prova de capacidade do Xbox.

A seleção da fila não altera PTS, relógio PCM, offsets de Nest nem política de perfil.
O AAC segue retido até o watermark inicial já existente, que agora consulta 12 no 1080p30.
