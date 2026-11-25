# Bancadas F25 — rede local e Nest

As provas usam loopback, conteúdo sintético e diretórios em `.work/f25`. Não acessam Xbox, Nest físico nem conta real.

```bash
PATH=/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f5-h264/.work/src/usr/bin:$PATH cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360 nest-test nest-discovery-test cast-test-app cast-protocol-test nest-live-clock-test
python3 tests/nest/discovery_test.py
python3 bench/standalone/cast_test.py client/build/nest-test
python3 tests/nest/wifi_player_test.py
python3 tests/cast/ssdp_test.py
python3 tests/cast/e2e.py --work .work/f25/cast-e2e
python3 tests/nest/live_clock_test.py
```

`discovery_test.py` inclui máscara zero, receptor no último host do /24 e endereço salvo válido mesmo sem interface detectável. Mantém validação TLS/Cast, UUID e escolha manual.

`cast_test.py` introduz atraso de 65 ms em cada direção e descarta deterministicamente três de cada dez respostas MEDIA_STATUS. Isso simula atraso e falta de estados; não pretende reproduzir retransmissões TCP de uma rede com 30% de perda IP. Mede o relógio extrapolado contra o relógio do receptor independente. A bancada mantém pausa, seek, resposta velha, namespace, posse, partição e STOP.

`wifi_player_test.py` roda o player e render de produção com o mesmo perfil de rede, captura a tela e verifica uma única carga, ausência de retorno à TV e liberação após ~25 s sem notícias. O STOP precisa ser confirmado antes de o PCM local soar.

`ssdp_test.py` usa multicast IP real na interface 127.0.0.1/UDP 1934 e DIAL 8134, com YouTube lounge simulado em 8234. Verifica NOTIFY, quatro respostas a ssdp:all e XML DIAL. O modo que força falha de membership verifica NOTIFY e M-SEARCH unicast: a recepção multicast sem adesão no XAM só pode ser comprovada no console. Linux pode compartilhar adesões entre sockets.

`live_clock_test.py` gera um broadcast H.264/AAC HLS de 2 s por segmento e usa o `LiveFile` e demuxer FFmpeg do produto para abrir janelas separadas da mesma transmissão. A diferença de PTS é medida no conteúdo, antes da normalização para zero do player. Não é um teste de suporte HLS do Nest físico.

## Console: roteiro do coordenador

1. Instalar o XEX desta branch pelo procedimento habitual, abrir sem servidor de PC e iniciar captura UDP do log. Esta frente não instala nem executa no console.
2. Em Configurações → Procurar alto-falantes, verificar `mascara indisponivel; usando /24 do IP atual`, `hosts=253 limite=16` e resultado <5000 ms. Se houver `nest.txt`, deve aparecer `verificando IP salvo ... antes da busca`; conferir nome/UUID e selecionar pelo controle.
3. No início do receptor, conferir `SSDP grupo=239.255.255.250 IP=... membership=... errno=... saida_errno=... UDP=1900`. `ENOPROTOOPT` significa opção de protocolo não suportada, não Linux errno 109. `NOTIFY alive enviados=4/4 errno=0` prova apenas aceitação local do envio.
4. Abrir Transmitir no YouTube do celular, na mesma rede, e buscar `SSDP M-SEARCH ... respostas=... errno=0`. Conferir a presença de Tube360 e iniciar um vídeo. Sem M-SEARCH no log, capturar tráfego no roteador/AP ou com espelhamento; o PC conectado ao Wi-Fi não necessariamente enxerga tráfego dirigido ao console.
5. Sonda unicast opcional do PC (não prova multicast do celular): `python3 tests/cast/ssdp_probe.py --host 192.168.1.50` envia M-SEARCH para o console e lê LOCATION. Verificar `GET http://<IP-console>:8134/ssdp/device-desc.xml` e `/apps/YouTube`. O código de TV mantém seu fluxo anterior.
6. Tocar VOD com Nest escolhido. `status=PLAYING ... RTT=... relogio=1` deve manter som no Nest mesmo acima de 120 ms. Uma troca para TV com status PLAYING aos 3,5 s é regressão. Pausar pelo controle e no alto-falante; buscar no vídeo; conferir imagem acompanhando o áudio.
7. Durante perda de notícias, não pode solicitar liberação antes de 25 s; sem progresso confirmado pelo receptor permanece a regra de 8 s. Ao sair, aguardar STOP/quiet. Comparar A/V com gravação de microfone para medir sincronismo físico, pois a bancada mede apenas o protocolo/relógio.
8. Live segue na TV, com log explícito de falta de origem comum. A prova HLS abaixo explica a limitação; não anunciar suporte Nest live com base no receptor simulado.

## Investigação de SDK e live

O SDK usado por esta branch traduz WSAENOPROTOOPT=10042 para ENOPROTOOPT=109 (newlib). `setsockopt` repassa IP_ADD_MEMBERSHIP=12 ao XAM; o layout de ip_mreq tem 8 bytes. O SDK já marca sockets com 0x5801 (tráfego sem criptografia XNet). Não foi encontrada API adicional de máscara, nem flag comprovada para habilitar multicast; XNADDR não contém máscara e XAM 0x65 mostra configuração salva, que pode ser zero no DHCP. Não se reinicia XNet para tentar flags não comprovadas.

A implementação mantém UDP 1900 aberto sem adesão, tenta primeiro interface explícita e depois ANY, usa o IP do XNet sem connect UDP ao grupo, informa falhas de envio e repete NOTIFY no início/periodicamente. NOTIFY já existia na base; não é uma nova rota garantida de descoberta. Anunciar `_googlecast._tcp` sem implementar o receptor Cast v2 correspondente não resolveria DIAL.

O Cast documenta HLS e informações de início de live. Contudo o caminho nativo entrega ao player PTS normalizado da janela do console; não entrega à coordenação Nest a associação com PTS/UTC da transmissão. Dois leitores podem escolher segmentos diferentes. HLS ser aceito não estabelece essa associação. A bancada mediu 16/2000/6011 ms de diferença ao deslocar a janela em 0/1/3 segmentos; a janela pode avançar novamente numa reconexão, sem limite de sincronismo garantido por esse mecanismo.

Para implementar live com o relógio do Nest seria necessário expor a origem UTC/PTS do `LiveFile` e da decodificação e obter/validar a mesma origem no MEDIA_STATUS do Default Media Receiver, ou servir o mesmo segmento/relé pelo console. Isso envolve `yt/media.hpp`, `player_h264.hpp` e `player.hpp`, fora dos arquivos desta frente; o suporte real desse modelo de Nest também precisa ser medido no hardware. Não foi ativada uma saída de áudio que não pode ser sincronizada/qualificada com os contratos atuais.

Fontes primárias consultadas:

- [SDK: rede e limites](https://github.com/xenosworks/xsdk/blob/main/docs/PARITY.md)
- [SDK: sockets e tradução de erros](https://github.com/xenosworks/xsdk/blob/main/src/socket.c)
- [Microsoft: opções IPv4/multicast](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options)
- [Google Cast: protocolos de streaming](https://developers.google.com/cast/docs/media/streaming_protocols)
- [Google Cast: MediaInformation e início da live](https://developers.google.com/cast/docs/reference/web_receiver/cast.framework.messages.MediaInformation)
