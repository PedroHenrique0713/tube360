# F25b / RC2 — relógio Cast e cadência da imagem

O log de console `console-rc2-nest-2226.log` confirmou o IP salvo tocando, mas
registrou imagem chegando a 35 fps e RTT de 126–816 ms. Um ponto médio de RTT
alto pode ter erro de metade desse RTT quando ida e volta têm atrasos diferentes.
A correção de fase de 15% convertia essa incerteza em aceleração/frenagem da imagem.

O remetente agora escolhe o menor RTT de até 32 amostras dos últimos 8 s, projeta
essa amostra até o instante atual e mantém três informações independentes:

- `updated`: a estimativa do relógio, inclusive na primeira resposta lenta válida;
- `observed`: atividade válida da sessão, mesmo sem horário aproveitável;
- `clockReady`: início da imagem calibrado por RTT <=200 ms ou após 1,5 s de
  coleta. Essa espera não retarda o LOAD nem apaga o relógio inicial.

Respostas de mais de 1 s atualizam a vivacidade, sem deslocar o relógio. Notificação
sem requestId mantém estado e progresso; não substitui uma âncora já calibrada.
O filtro reinicia em geração/seek e mudança de estado ou velocidade. O detector
de travamento continua medindo progresso bruto do receptor, fora do filtro.

A primeira imagem nativa espera a âncora inicial, ou o retorno à TV em caso de
falha. Durante PLAYING, a fase avança continuamente e corrige no máximo 1%
(10 ms/s), em vez de 15%. Para fonte de 30 fps, isso corresponde idealmente a
29,7–30,3 quadros/s; não há salto para cada novo status. Uma resposta lenta não
remove o acompanhamento só porque a última amostra de relógio ficou antiga:
o player usa `observed` para esse prazo.

## Reprodução local

```bash
cmake --build client/build -j2 --target tube360 nest-test nest-discovery-test cast-test-app cast-protocol-test cast-lan-test nest-scan-fault-test nest-live-clock-test
# Aguardar o build terminar antes de medir cadência; o controle acusa falta de CPU.
LP_NUM_THREADS=2 python3 tests/nest/jitter_player_test.py
XT_NEST_NETWORK=127.77.29.1/255.255.255.252 python3 bench/standalone/cast_test.py client/build/nest-test
python3 tests/nest/discovery_test.py
python3 tests/nest/saved_ip_test.py
python3 tests/nest/saved_player_test.py
python3 tests/nest/wifi_player_test.py
python3 tests/nest/live_clock_test.py
python3 tests/nest/review_probes.py
LP_NUM_THREADS=2 python3 tests/nest/screenshots.py
LP_NUM_THREADS=2 python3 tests/cast/e2e.py --work .work/f25b-jitter/cast-e2e
```

O teste de cadência roda o main/player/decoder/render de produção, com H.264
720p30 e AAC sintéticos. O receptor TLS aplica RTT nominal de 50–900 ms com
**assimetria** de ida/volta, descarta três em cada dez MEDIA_STATUS e tem relógio
2.000 ppm mais rápido. Há controle sem jitter com o mesmo render/codec/relógio.

`video-metrics.txt=1` registra um evento após `vkQueuePresentKHR` para cada novo
quadro de vídeo; repetição de UI não conta. O campo `monotonic-ms` mede o instante
no aplicativo. O teste conta todas as janelas completas móveis de 1 s, nos dois
lados de cada entrada/saída de quadro da janela. Exige 29–31 apresentações em
**todas** elas e erro máximo <100 ms contra o relógio independente do receptor.
Não descarta janelas de PLAYING para aprovar uma rodada. CPU insuficiente também
reprova o controle.

Resultados, por quadro e resumo, ficam em
`.work/f25b-jitter/player/{control,jitter-50-900ms-loss30}/frames.json` e
`result.json`; há log completo e PNG do vídeo em cada pasta.

A rodada de aceitação mediu 29–31 fps no controle e no jitter; no jitter,
erro máximo **13,24 ms**, média **4,16 ms**, 29/99 estados descartados e 2.801
janelas verificadas. O controle teve erro máximo de 17,81 ms. O código anterior,
calibrado na mesma fixture e reanalisado nas mesmas janelas, fez 25–33 fps e chegou
a 141,25 ms de erro no jitter, com controle em 29–31 fps. O binário antigo não
tinha `monotonic-ms`: a calibração usa chegada das linhas de log; a aprovação
usa o timestamp depois de Vulkan. Uma tentativa enquanto o build ainda rodava reprovou
o próprio controle; ela não foi tratada como aprovação de cadência.

Esses erros são medidos no protocolo/render offscreen. O limite de incerteza da
rede é metade do menor RTT; não se pode prometer erro de 13 ms numa rede que só
oferece RTT de 900 ms assimétrico. HDMI, processamento da TV, DAC do Nest e
sincronismo acústico continuam sendo a prova do coordenador no hardware.

## Console — somente o coordenador executa

O kit desta revisão fica em `.work/f25b-jitter/console/`, com XEX, quatro fontes,
`settings.txt` (`sound=nest`, `server=off`), `nest.txt` legado e `autoplay.txt`
(`nuvwZLCNJCI 30`). Não copiar conta, journal ou servidor da pasta de uso real.
O kit deixa as métricas por quadro desligadas: esse volume de log pode interferir
na própria medição. O log agregado `qualidade` já está ativo.

Com o título fechado, o coordenador pode enviar/abrir a pasta nova:

```bash
xbdm=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
python3 "$xbdm" 192.168.1.50 mkdir 'HDD:\Games\Tube360-F25b-Jitter'
for file in .work/f25b-jitter/console/*; do
  python3 "$xbdm" 192.168.1.50 put "$file" "HDD:\\Games\\Tube360-F25b-Jitter\\${file##*/}"
done
python3 "$xbdm" 192.168.1.50 run 'HDD:\Games\Tube360-F25b-Jitter\tube360.xex'
```

Verificar som físico no Nest, sem busca unicast no receptor saudável. Depois da
primeira janela completa de vídeo, acompanhar `qualidade: ... fps na TV` por
90 s e procurar estabilidade perto de 30, sem a oscilação 25–35. Conferir
sincronismo boca/som com gravação da TV e do alto-falante; `sync_ms=0` mantém a
mesma configuração da bancada. Pausar/retomar e buscar devem estabelecer uma
nova âncora, sem carregar mídia duplicada. Voltar com B/View e esperar STOP.

```bash
python3 "$xbdm" 192.168.1.50 get 'HDD:\Games\Tube360-F25b-Jitter\tube360.log' .work/f25b-jitter/console-clock.log
```

Repetir a abertura com o Nest desligado: TV antes de 10 s desde `dono=console`.
Manter os testes de IP salvo e recuperação do `README_F25b.md`. Esta frente
nunca executa os comandos XBDM nem acessa o Nest físico.
