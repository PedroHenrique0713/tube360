# F34 — diagnóstico no console, 2026-11-25

## Rodada após o resultado de 01:39 — prioridade e retry

A sonda já passou no console (200 / um trecho / 0.532 s), enquanto o app recebeu EOF depois de 17.039 s.
O app da pasta local foi atualizado: disparo na seleção, worker em HW1, prioridade relativa +1 durante o
pedido, connect/handshake de 2500 ms e total de 3500 ms. Há no máximo duas tentativas: a segunda espera
três segundos após a falha e três segundos contínuos de reprodução. Sem espera da rede na thread da UI.

Para repetir, use **o mesmo bloco do app** abaixo. A sonda isolada anterior permanece disponível para
comparação; não é necessário executá-la novamente. Com o app anterior fechado, a atualização mínima é:

```bash
cd /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f34-sponsorblock
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 put .work/console-f34-diagnostico-1125/tube360.xex 'HDD:\F34-TLS-1125\tube360.xex'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 run 'HDD:\F34-TLS-1125\tube360.xex'
# Depois de aparecer o trecho ou de terminar a segunda tentativa:
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 get 'HDD:\F34-TLS-1125\tube360.log' .work/proofs/f34-cpu/console-app.log
rg 'SponsorBlock|primeiro quadro|decod-worker' .work/proofs/f34-cpu/console-app.log
```

Conferir:

1. `tentativa 1/2 inicio` ocorre junto da seleção/resolução, com `hw_alvo=1 hw_efetivo=1`,
   `afinidade=0x00000000 prioridade=1 prioridade_status=0x00000000`. `afinidade` é o **status de retorno**,
   não a máscara de CPUs; zero é sucesso. `fila_ms` mostra o atraso até o worker começar.
2. O pedido normal deve terminar perto do tempo da sonda e fornecer `status=ok`, um trecho, botão Y e marca.
   Comparar o tempo até o primeiro quadro com a rodada anterior; não há gate esperando SponsorBlock.
3. Se ocorrer timeout/EOF, há uma linha `tentativa 2/2 aguardando`; a segunda consulta só começa após
   reprodução estável e deve retornar `cached=0`, usando conexão nova. Nenhuma `tentativa 3` pode aparecer.
4. Repetir botão/pulo e, se desejado, automático/Y para voltar. Fechar ou trocar de vídeo antes do retry
   deve cancelar a tentativa antiga, sem marcas do vídeo anterior. Um novo processo começa com cache vazio.

Os dois pedidos usam prazos de curl; a efetividade sob carga e a afinidade/prioridade reais precisam desta
medição no console. As configurações de certificados e de decodificação não foram alteradas nesta rodada.

O log antigo `SponsorBlock: 0 trechos` confundia vazio com erro. A build instrumentada distingue
`ok`, `empty`, `tls_error`, `network_error`, `http_error`, `parse_error` e cancelamento. Não há erro novo
na interface. O log inclui prefixo, HTTP/libcurl/mbedTLS, mensagem de erro, tamanho, bucket, match,
rejeitados, cache, duração do pedido e relógio civil (`epoch`). `verify=unavailable` significa que o curl
já liberou o contexto da conexão falhada; o código mbedTLS e sua mensagem continuam disponíveis.

No PC, **com o mesmo config.h do Xbox**, SponsorBlock retornou 200 e o trecho 7.774863–20.877296 s.
ECDSA, P-256, P-384 e ECDHE-ECDSA estão habilitados. O bundle existente GTS R1–R4 valida a cadeia
atual encerrando em R4; não é necessário confiar no GlobalSign cruzado para essa cadeia. Não foram
adicionadas raízes, nem desabilitadas validações, nem alteradas curvas sem evidência de falta.

## Primeiro: isolar HTTPS e parse da carga do player

A sonda faz **um GET** do prefixo `5f6b`, usa o mesmo transporte/bundle/configuração e uma thread de
512 KiB, e registra recursos criptográficos efetivamente compilados, relógio, resultado e intervalo.
Não precisa de fontes, conta, autoplay ou servidor. Não desenha uma interface; observe o arquivo de log.

Comandos **para o coordenador**, após sair do app em execução e confirmar o IP do console:

```bash
cd /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f34-sponsorblock
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 mkdir 'HDD:\F34-TLS-1125'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 put .work/console-f34-diagnostico-1125/sponsorblock-probe-final.xex 'HDD:\F34-TLS-1125\sponsorblock-probe-final.xex'
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 run 'HDD:\F34-TLS-1125\sponsorblock-probe-final.xex'
# Quando a sonda terminar (o pedido tem timeout de 15 s):
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 get 'HDD:\F34-TLS-1125\sponsorblock-probe-final.log' .work/proofs/f34-fix/console-probe.log
rg 'TLS probe|segment' .work/proofs/f34-fix/console-probe.log
```

Enviar as linhas `TLS probe ...` e `segment ...` à frente F34. Elas são necessárias para identificar a
causa **no hardware**; o êxito no PC com a mesma configuração não substitui esse resultado.

## Depois: o app completo sob decodificação

A pasta `.work/console-f34-diagnostico-1125/` também recebe o app instrumentado e suas fontes/settings
após o build terminar. `autoplay.txt` contém `wWC8wVI4E3E 5`; `settings.txt` usa botão e som na TV.

```bash
for asset in .work/console-f34-diagnostico-1125/*; do
  python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 put "$asset" "HDD:\\F34-TLS-1125\\${asset##*/}"
done
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 run 'HDD:\F34-TLS-1125\tube360.xex'
# Após o trecho / encerramento do app:
python3 /home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py 192.168.101.39 get 'HDD:\F34-TLS-1125\tube360.log' .work/proofs/f34-fix/console-app.log
rg 'SponsorBlock|primeiro quadro|layout' .work/proofs/f34-fix/console-app.log
```

Resultado esperado: `SponsorBlock: 1 trechos status=ok prefix=5f6b ... http=200 curl=0 ... bucket=81 matched=1`.
O número de vídeos do bucket é público e pode mudar. Perto de 7.775 s deve aparecer Y Pular patrocínio;
apertar busca 20.877 s. O automático e Y para voltar continuam sendo os mesmos do player.

Interpretação:

- `tls_error`: usar o mbedTLS hexadecimal e `detail`; ver relógio e cadeia. Nenhum fallback inseguro.
- `network_error`: `curl=6` DNS, `7` conexão, `28` timeout; comparar sonda isolada e app.
- `http_error`: HTTP 403/429/5xx, separado de ausência de segmentos. Não tratar 403 como vazio.
- `parse_error`: resposta chegou, mas limite/sintaxe/schema falhou; `reason`/`detail` indicam onde.
- `empty`: 404 ou resposta válida sem vídeo/trecho elegível; prefixo e bucket permitem conferir o hash.
- Sonda funciona e app falha: diferença de concorrência/recursos/tempo durante reprodução; comparar ambos.

Esta frente não executou XBDM, upload, execução remota ou reboot. A validação no console fica com o coordenador.
