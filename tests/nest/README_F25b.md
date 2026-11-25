# F25b — IP salvo sem descoberta no caminho do áudio

Para a correção posterior de cadência/sincronismo sob jitter (RC2), veja
[README_JITTER.md](README_JITTER.md).

O worker Cast abre diretamente o host/porta de `nest.txt`, mesmo com apenas as
duas linhas da v0.3.1. Depois de PLAYING, outro worker tenta ler os metadados e
salvar UUID/modelo. Falha nessa leitura não inicia uma busca nem altera a saída.
Abrir o app sem reprodução não verifica nem varre a rede.

Uma busca automática exige falha real de `Connection::open` no worker Cast.
Ela verifica primeiro o endereço salvo e só então percorre a rede. Há intervalo
de 30 s depois de terminar uma busca, inclusive entre vídeos. A busca manual
em Configurações continua disponível imediatamente e B cancela.

| Operação | Prazo | Motivo |
|---|---:|---|
| TLS de reprodução e identificação | 4.000 ms | Handshakes de 1–2 s precisam de margem para TLS no Xenon com vídeo abrindo. |
| RECEIVER_STATUS na identificação | 2.000 ms | A resposta de controle também disputa CPU e rede; 450 ms era insuficiente. |
| Metadados HTTP | conexão 1.500 ms, total 2.500 ms | Leitura opcional depois da confirmação Cast. |
| Metadados HTTPS | conexão 4.000 ms, total 5.000 ms | A porta 8443 também exige TLS. |
| Sondagem TCP | 500 ms/host, até 16 sockets, fase de até 10 s | Um /24 silencioso precisa de cerca de 8 s só para as 16 levas. |
| Busca inteira | 30.000 ms | Inclui validação dos serviços com TCP aberto; o tempo restante limita TLS/status/metadados. |
| Nova busca automática / metadados após falha | 30.000 ms após a anterior | Evita repetir /24 a cada vídeo. |
| Início Cast | 8.000 ms | O TLS usa no máximo o tempo restante; erro anterior ao LOAD devolve a TV antes de 10 s. |

A busca continua limitada ao /24 do IP atual (ou à sub-rede menor). Rede grande,
muitos serviços TLS mudos ou sobrecarga podem produzir resultado incompleto;
não se adivinha por nome em uma busca incompleta. UUID permite localizar um
receptor mesmo com o IP antigo ocupado por outro. Sem UUID, só um nome único
em busca completa permite migrar o endereço.

## Bancadas locais

Tudo usa loopback, respostas gravadas, vídeo/som sintéticos e HOME próprio.
Os testes de descoberta compartilham portas e devem rodar em sequência.

```bash
PATH=/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f5-h264/.work/src/usr/bin:$PATH cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 nest-test nest-discovery-test cast-test-app cast-protocol-test cast-lan-test nest-scan-fault-test nest-live-clock-test
python3 tests/nest/discovery_test.py
python3 tests/nest/saved_ip_test.py
LP_NUM_THREADS=2 python3 tests/nest/saved_player_test.py
XT_NEST_NETWORK=127.77.29.1/255.255.255.252 python3 bench/standalone/cast_test.py client/build/nest-test
python3 tests/nest/review_probes.py
LP_NUM_THREADS=2 python3 tests/nest/wifi_player_test.py
python3 tests/nest/live_clock_test.py
LP_NUM_THREADS=2 python3 tests/nest/screenshots.py
LP_NUM_THREADS=2 python3 tests/cast/e2e.py --work .work/f25b/cast-e2e
```

`saved_player_test.py` abre vídeo 720p e AAC no main/player/render de produção,
com `nest.txt` de duas linhas e TLS atrasado 1 s e 2 s. Exige um único LOAD,
som no Nest, nenhuma busca e nenhuma troca para TV. O caso desligado exige
TV em menos de 10 s desde o pedido Cast e uma única busca de recuperação.
As capturas/logs ficam em `.work/f25/player/saved-*`.

`saved_ip_test.py` inicia a reprodução apontando para um IP antigo fechado,
mantém um receptor no novo endereço e exige busca automática, atualização do
arquivo, PLAYING e confirmação de STOP. `discovery_test.py` cobre migração de
UUID em segundo plano, metadados indisponíveis sem busca, cooldown, IP reutilizado,
nome duplicado, TLS/status lentos, cancelamento e seleção pelo usuário.

`review_probes.py` ajusta o teto da prova de SYN silencioso para 10 s, de acordo
com os 500 ms por leva; a regra anterior de 5 s era incompatível com essa margem.
`README_R25.md` descreve uma limitação histórica da base: a prova de perda
permanente agora exige a volta à TV após a liberação e os 10 s sem contato.

## Console — somente o coordenador executa

Criar uma pasta temporária nova, sem conta, journal antigo ou `server.txt`.
Exemplo de preparação local (o ID é o mesmo do log de regressão):

```bash
python3 - <<'PY'
from pathlib import Path
import shutil
root = Path.cwd()
stage = root / '.work/f25b/console'
stage.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / 'client/build-x360/tube360.xex', stage / 'tube360.xex')
for font in (root / 'client/assets').glob('inter-*.ttf'):
    shutil.copy2(font, stage / font.name)
(stage / 'nest.txt').write_text('192.168.101.41 8009\nTeste Eliot\n')
(stage / 'autoplay.txt').write_text('nuvwZLCNJCI 30\n')
(stage / 'settings.txt').write_text('lang=pt\nquality=720\nsound=nest\nserver=off\nsync_ms=0\n')
PY
```

Com o app fechado no console, enviar apenas essa pasta nova pelo cliente XBDM
já usado pelo coordenador. `HDD:` é o nome que aceita escrita nesta máquina.
Os comandos abaixo **não são executados pela frente**:

```bash
xbdm=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
python3 "$xbdm" 192.168.101.39 mkdir 'HDD:\Games\Tube360-F25b'
for file in .work/f25b/console/*; do
  python3 "$xbdm" 192.168.101.39 put "$file" "HDD:\\Games\\Tube360-F25b\\${file##*/}"
done
python3 "$xbdm" 192.168.101.39 run 'HDD:\Games\Tube360-F25b\tube360.xex'
```

Verificar som físico no Nest e `dono=console` → CONNECT/LAUNCH/LOAD → PLAYING;
não deve haver `busca unicast` nem espera pela descoberta. Só depois de PLAYING
pode aparecer `metadados em segundo plano`. Ao sair com B/View, aguardar STOP e
silêncio confirmado antes de fechar o título. Depois baixar os arquivos:

```bash
python3 "$xbdm" 192.168.101.39 get 'HDD:\Games\Tube360-F25b\tube360.log' .work/f25b/console-healthy.log
python3 "$xbdm" 192.168.101.39 get 'HDD:\Games\Tube360-F25b\nest.txt' .work/f25b/console-nest-after.txt
```

Reabrir duas vezes, mantendo o Nest ligado: nenhum /24. UUID pode ser acrescentado
sem afetar o áudio; metadados indisponíveis podem deixar o formato antigo intacto.
Para repetir exatamente o legado, reenviar o arquivo de duas linhas após fechar.

Para testar IP mudado sem alterar DHCP da casa: com o título fechado, trocar
somente a primeira linha da cópia temporária por um IP livre confirmado pelo
coordenador, preservando nome e o UUID obtido. Reabrir e observar falha real de
Cast, uma busca, `reencontrado ... por UUID` e o endereço corrigido no arquivo.
Uma recuperação de rede lenta pode terminar depois do retorno inicial à TV;
nesse caso X ou o próximo vídeo usa o endereço novo. Repetir também sem UUID:
nome único e busca completa são necessários.

Por fim, com o Nest desligado antes de abrir, medir do `dono=console` até `som: TV`:
menos de 10 s. Reabrir um vídeo dentro dos 30 s após a busca: não deve varrer de
novo. A busca manual pode ultrapassar 5 s, informa resultado incompleto quando
necessário e precisa continuar cancelável pelo B. TLS/CPU e sincronismo físico
só ficam qualificados depois desta execução no hardware.
