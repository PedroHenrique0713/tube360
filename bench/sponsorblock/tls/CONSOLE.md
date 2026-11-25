# F34 — rodada após 02:41: CPU para o handshake

A pasta pronta continua em `.work/console-f34-diagnostico-1125/`, para `HDD:\F34-TLS-1125`.
Esta rodada usa **autoplay do começo (0 s)**, modo botão, som na TV e qualidade automática.
Enviar também `settings.txt` e `autoplay.txt`: a rodada anterior pode ter salvo outra qualidade ao apertar Y.

A sonda isolada anterior já passou (HTTP 200 / 27093 bytes / um trecho / 0.532 s); não é preciso repeti-la.
Os comandos abaixo são para o coordenador. A frente F34 não acessa o console.

## Atualizar e executar

Com o app anterior fechado:

```bash
cd /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f34-sponsorblock
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 put .work/console-f34-diagnostico-1125/tube360.xex 'HDD:\F34-TLS-1125\tube360.xex'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 put .work/console-f34-diagnostico-1125/settings.txt 'HDD:\F34-TLS-1125\settings.txt'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 put .work/console-f34-diagnostico-1125/autoplay.txt 'HDD:\F34-TLS-1125\autoplay.txt'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 run 'HDD:\F34-TLS-1125\tube360.xex'
```

Deixar tocar sem apertar Y até aparecer **Y Pular patrocínio** em aproximadamente **7.775 s**.
Apertar Y enquanto a dica estiver visível: deve haver `SponsorBlock: skip ... -> 20.877`.
Depois de 20.877 s, fora de uma ação de pular/voltar, Y abre o menu Qualidade. Só A aplica a escolha e reabre o vídeo quando necessário; B fecha sem alterar. O log registra a abertura do menu explicitamente.

Após o teste:

```bash
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 get 'HDD:\F34-TLS-1125\tube360.log' .work/proofs/f34-priority/console-start-0.log
rg 'SponsorBlock|decod-worker|player: pedido|player: Y|player: encerrando|console: player|console: MP4|primeiro quadro|qualidade: fonte' .work/proofs/f34-priority/console-start-0.log
```

## O que medir

1. `tentativa 1/2 inicio`: `hw_alvo=5 hw_efetivo=5`, status `0x00000000`, prioridade solicitada **3**.
   A linha `prioridade` deve confirmar `efetiva=3 consulta_status=0 anterior=0 restaurada=0`, com restauração
   bem-sucedida. `tls_begin` e `tls_done` também devem registrar `prioridade_efetiva=3`. Os workers H.264
   agora registram sua prioridade real em `decod-worker`; conferir que seja inferior a 3. Não há HW livre: HW0
   apresenta, HW1 resolve/demuxa e alimenta o áudio (+1), HW2–5 têm H.264; HW5 também tem jobs de rede.
   O pedido começa na escolha, junto da resolução, com connect=1500 ms e total=1800 ms.
2. As fases são `dns_begin`, `dns_done`, `tcp_begin`, `tls_begin`, `tls_done`, `http_response`.
   Não são gravados headers, cookies, payload TLS ou a lista de IPs.
   **Logs adiados:** as fases são capturadas em memória e impressas após publicar o resultado, para não
   esperar pelo logger no handshake. `elapsed_ms` é o instante capturado; a ordem de impressão pode intercalar
   a linha da UI com as linhas da tentativa. `log_adiado=1` identifica esse comportamento.
3. No fim, `dns_ms`, `tcp_ms`, `tls_ms` são **durações individuais** do curl;
   `first_byte_ms` é acumulado desde o começo. `-1` significa fase não concluída ou conexão reutilizada,
   não duração negativa. `phase` indica onde houve a falha. `nonblocking=1` confirma o socket no início
   do handshake; `os_errno` preserva o erro do socket. `callback_gap_ms` mostra a maior janela sem callback.
   Se a soma e a parede excederem o prazo e houver um gap grande, o prazo só foi observado quando o worker
   voltou ao curl; não se trata de uma interrupção preemptiva de 1500 ms.
4. Não existe mais a janela de espera de 750 ms. O vídeo abre sem aguardar o pedido.
   A meta é **tentativa 1 bem-sucedida em até 2000 ms desde a escolha**, com
   `prazo ... dentro_prazo=1 dados_ok=1`, HTTP 200 e um trecho. O limite do curl continua cooperativo;
   o log de prioridade efetiva e as fases permitem conferir se houve CPU durante o handshake.
5. Se houver falha transitória, a segunda começa em `max(escolha+3000 ms, fim_da_primeira+250 ms)`;
   independe de primeiro quadro e usa conexão nova/cache bypass. No máximo dois pedidos por abertura.
6. `SponsorBlock: 1 trechos pos=... estado=...`: a **posição do vídeo** deve ser menor que 7.775 no teste do começo.
   `fila_ms` é tempo de parede desde a escolha, não posição do vídeo.
7. `console: player OK ... resolucao_ms`, `console: MP4 ... abertura_ms` e `primeiro quadro` separam
   resolução, abertura de mídia e pré-decodificação. Comparar TTFF com o mesmo vídeo, posição e modo.
   A seleção 720p60 pode cair para 360p30 ao não sustentar decodificação; esta rodada não muda esse algoritmo.

## Comparar com abertura em 5 s

Após encerrar a primeira rodada, criar um arquivo alternativo e enviar:

```bash
printf 'wWC8wVI4E3E 5\n' > .work/proofs/f34-priority/autoplay-5.txt
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 put .work/proofs/f34-priority/autoplay-5.txt 'HDD:\F34-TLS-1125\autoplay.txt'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 run 'HDD:\F34-TLS-1125\tube360.xex'
# Após o teste:
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.1.50 get 'HDD:\F34-TLS-1125\tube360.log' .work/proofs/f34-priority/console-start-5.log
```

O trecho gravado é 7.774863–20.877296 s, `sponsor`, no vídeo público `wWC8wVI4E3E` (prefixo 5f6b).
A API comunitária pode mudar esses dados. Off não consulta; vazio, rede, TLS, HTTP e parse permanecem distintos.
A validação HTTPS e o bundle GTS R1–R4 continuam ativos.

## Controle de tempo até o primeiro quadro

Para comparar na mesma build/vídeo/posição, encerrar o app, reenviar `autoplay.txt` do kit (0 s), enviar
`.work/proofs/f34-priority/settings-off.txt` como `HDD:\F34-TLS-1125\settings.txt` e executar novamente.
Salvar como `console-off-0.log`. Comparar `resolucao_ms`, `abertura_ms` e `primeiro quadro` com
`console-start-0.log`. O SponsorBlock deve chegar em até 2 s e o primeiro quadro não deve regredir. Depois restaurar `settings.txt` do kit.
Esta comparação ainda depende da rede/cache do console. Não há mais gate de abertura.
No log de 02:41 os 14 s ocorreram antes da criação dos workers H.264 dessa abertura, enquanto a resolução
levou 17.789 s. O HW5 é compartilhado, mas esse episódio não prova que o decodificador foi o único concorrente.
