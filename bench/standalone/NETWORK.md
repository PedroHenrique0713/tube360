# Rede e degradação local — F19

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360 tube360-quality-fixture yt-network-probe yt-media-test yt-contract-test player-state-test
client/build/yt-media-test
client/build/yt-contract-test bench/standalone/fixtures
client/build/player-state-test
python3 bench/standalone/network_bench.py .work/network-tls
python3 bench/quality/network_e2e.py .work/quality-network
```

A bancada HTTPS usa o curl/mbedTLS do produto contra um servidor TLS em **127.0.0.1**, com 120 ms por pedido e
2,5 Mbit/s por conexão ou compartilhados. Não usa `tc`, cookies, YouTube ou o servidor da casa. Compila outro
objeto `https.cpp` com uma CA descartável, exclusiva do executável de teste; as raízes de `tube360` e do `.xex`
continuam as de produção. Compara Range 128/512/1024 KiB, leitura 16/64 KiB, uma/duas conexões e reuso/conexão
nova. `--faults-only` repete apenas conexão interrompida e parada de 14 s: o RangeFile precisa retomar pelo
primeiro byte ausente e entregar os 2 MiB em ordem. Os resultados ficam em `results.json` e stdout.

A prova do player usa os mesmos main/player/render, substituindo somente o transporte por arquivos sintéticos
com limite agregado de 2,5 Mbit/s. Os vídeos têm 4,2/3,1/1,4 Mbit/s e o AAC, 128 kbit/s. Exige duas trocas de
fonte, quadros efetivamente decodificados em 480p, uma única sessão AAC/primeira imagem e ausência de servidor.
A captura de 50 s fica em `proofs/ladder.png`; o log inclui a reserva PCM e as reaberturas do vídeo.
Esta bancada não mede CPU, TLS ou GPU do Xbox nem a complexidade dos vídeos reais.

## Decisões e limites

- Range de 512 KiB, janela atual + duas adiantadas para vídeo (até 1,5 MiB), conexões alternadas e independentes.
  AAC tem seu próprio transporte/janela. A sondagem do MP4/seek não dispara especulação inútil.
- Leitura curl de 64 KiB, TCP solicitado de 256 KiB por sentido; o log mostra o valor efetivo aceito pelo sistema.
  Pede conexão nova somente ao recuperar uma falha. Tentativas por Range: quatro, com 150/300/600 ms de recuo
  cancelável; timeout total de 12 s e proteção de baixa vazão de 16 KiB/s durante 4 s. Prefixo 206 é aproveitado
  somente com Content-Range coerente, mesmo tamanho total e erro transitório. TLS continua verificado.
- Os números `rede` do resumo do decoder contam bytes consumidos pela aplicação. Não são uma medida isolada
  da capacidade do enlace: CPU, pausa e fila cheia também reduzem esse contador. `TLS a/b Mbit/s` divide os
  bytes de cada conexão pelo seu tempo ativo de transferência, incluindo conexão/TLS e tentativas; não inclui
  o tempo ocioso do decoder. Não some esse tempo entre conexões sobrepostas.
- A escada MP4 usa fontes distintas: 720p60 → 720p30 → 480p30, quando oferecidas, mantendo AAC, relógio,
  posição e quadros prontos. O fechamento cancela somente os Ranges antigos. Pressão por três janelas decide a troca. A reserva PCM da TV no VOD separado é
  de 6 s (~2 MiB), limitada também no produtor. Sem fonte menor, a apresentação pode cair a 30 sem reduzir rede.
- HLS troca somente variantes com o mesmo grupo de áudio e fronteiras alinhadas por PROGRAM-DATE-TIME.
  Números de sequência diferentes são mapeados pelo UTC. Sem esse relógio, mantém a fonte e a apresentação
  reduzida: adivinhar pelo número do segmento pode perder ou repetir áudio. Segmentos já prontos são preservados.
- Sob pressão, o console continua quando o PC não responde. Reserva só após opções locais e GET 200 de
  `/api/remote/state`, no worker. Falha irrecuperável sem reserva encerra com erro; não cria um stream no PC.
- VISIONOS continua primeiro. Rede/HTTP transitório/JSON recebem uma repetição curta com conexão nova.
  LOGIN_REQUIRED renova a sessão já existente uma vez; recusas passam por ANDROID_VR e IOS, com agentes e
  cabeçalhos coerentes. Não há promessa de que YouTube aceite esses clientes: podem exigir PO token ou bloquear
  a mídia depois da abertura. Erro de certificado não repete nem desliga a verificação.

Identidades consultadas na [youtubei.js](https://github.com/LuanRT/YouTube.js/blob/main/src/utils/Constants.ts).
O [NewTube](https://github.com/aleixrodriala/newtube/blob/main/docs/mobile-port/HANDOFF.md#34-the-one-minute-wall-round-r11-router-v20-v22-2026-09-29-1110)
documenta recusas por visitante/cliente e bloqueios após um minuto; a validação real deve passar de 60 s.

## Roteiro para o coordenador no Xbox

Sem PC, `server=off`, TV, `video_mode=720p60`, saída 720p, `video-metrics.txt=0`; 2 minutos por vídeo:

1. `x14DyNL0BpU`: repetir a falha das 22:55, começando em 0 e depois buscando 60 s. Conferir itags, duas TLS,
   reuso, retomadas, TCP efetivo, fila, PCM e FPS na TV. Se houver pressão, esperar troca de fonte antes de erro.
2. `4p_9OPKgcDE`: futebol usado pela bancada anterior; testar 720p30, pausa/retomada e busca 30→60 s. Se a oferta
   atual não tiver 720p, registrar o catálogo recebido. Não chamar apresentação 30 de redução de tráfego.
3. `HvZt-nh9sGg`: live usada pelo probe anterior, se ainda estiver no ar; caso contrário escolher a live atual
   do mesmo canal na interface. Medir pelo menos 2 minutos, fila/PCM e trocas HLS alinhadas por UTC.

Depois repetir os três com `server=auto` e o PC **desligado**, verificando que pressão na menor fonte não encerra
o player. Com o PC ligado, a primeira abertura ainda deve ser `console: player OK`; reserva precisa ser precedida
por `opcoes diretas esgotadas, servidor respondendo`. Repetir com som no Nest depois da integração da F18.
Nenhum comando de deploy, FTP ou XBDM é necessário para as provas locais desta frente.
