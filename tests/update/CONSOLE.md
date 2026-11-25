# F26: prova de dez minutos no Xbox (só o coordenador)

O kit fica em `.work/update-console/`. Nada é publicado. Não use a chave real. Os
arquivos `private/` nunca entram no ZIP nem no HTTP; não compartilhe essa pasta.
São três executáveis Xbox reais compilados do mesmo fonte: N `#2601 / 0.3.2-f26-n`,
N+1 `#2602 / 0.3.2-f26-good` e falha `#2603 / 0.3.2-f26-fail`.
A falha desativa deliberadamente a confirmação de boot, mantendo a interface utilizável.
Não causa travamento: simula um app que nunca concluiu o início, para provar a reserva.

## Sem controle: execução pelo coordenador via XBDM

O kit atual aceita **somente na build de teste** o arquivo `update-auto.txt` ao
lado do XEX. Sua presença habilita uma consulta ao abrir e um pedido de download
pela mesma função usada por A na interface. É consumido uma vez por abertura;
B continua cancelando. Sem o arquivo, a navegação manual continua igual. A build
pública não compila a leitura desse arquivo nem o disparo automático.

O coordenador roda, da raiz deste worktree, com o IP atual do XBDM:

```bash
python3 tests/update/console-run.py --host 192.168.101.39 --directory .work/update-console --io-timeout 30
```

Não é necessário subir outros processos nem usar o controle. O runner:

1. Confere pacote, candidatos, capability `auto_file_trigger` e Aurora no USB.
2. Sobe seu servidor TCP 8266 e receptor UDP 7466, guardando seus PIDs.
3. Usa exclusivamente `Games\Tube360 Teste`. Se a pasta já existir, arquiva seus
   logs antes de substituí-la. Tenta `Hdd1:`; somente erro 414 habilita `HDD:`.
4. Instala N, fontes/licenças, `settings.txt` (`server=off`, `sound=tv`), `net_log.txt`
   e `update-auto.txt`. Relê os dois XEX por XBDM e confere SHA-256.
5. Lança `default.xex`, espera staged #2602, relança e exige boot confirmado #2602.
   Libera o título no Aurora antes de selecionar fail, evitando uma consulta em
   curso do app anterior. Reabre para staged #2603; reabre para trial sem confirmação;
   reabre de novo para restaurar #2602 com `highest=2603` preservado.
6. Exige linhas novas de update.log/launcher.log ou UDP, journal assinado com os
   bytes exatos e, nas promoções/reserva, hash do XEX instalado. Na volta exige
   também `TEST boot healthy build=2602`, emitido após o sinal de saúde do renderer.
7. Captura os logs/journals de cada fase. No final libera o título pelo Aurora,
   baixa **todos os arquivos .log e o journal**, apaga arquivos/subpastas e só então
   a pasta de teste com `delete ... dir`. Confere sua ausência, envia o retorno
   final ao Aurora e encerra somente os PIDs de servidor/receptor que criou.

Cada etapa imprime **PASSOU/FALHOU** com a evidência; falha dá exit 1. As evidências
ficam em `.work/console-runs/<data-id>/`: `runner.log`, `report.json`, `xbdm-io.log`,
`udp.log`, `server.log`, snapshots por fase e `final/`. Logs anteriores ficam em
`preexisting/`. Se não conseguir arquivar os logs finais, preserva a pasta remota e
informa a falha; ainda tenta voltar ao Aurora e encerrar seus processos.

Os prazos padrão são 120 s por fase, 240 s para instalação, 90 s para a limpeza e
8 s de limite absoluto por operação XBDM, inclusive se os bytes chegarem pingados.
O comando acima usa 30 s por operação para dar margem à transferência dos XEX reais.
Pode ajustar `--step-timeout`, `--install-timeout`, `--cleanup-timeout` e `--io-timeout`.
Uma porta já ocupada não é tomada do processo existente. O `mkdir` verifica a
resposta bruta: 413 não é aceito como sucesso. Ctrl+C também passa pela limpeza.
O retorno ao Aurora prova envio de magicboot, reconexão XBDM e existência do XEX;
o protocolo não dá confirmação visual do dashboard.

O autor não executa esse comando contra o console. Prova local reproduzível:

```bash
python3 tests/update/console-run-test.py
```

São 13 casos com servidor XBDM TCP falso e HTTP/UDP reais em loopback: percurso
completo, aliases 414 (dirlist/mkdir), pasta anterior/subpastas, UDP sem arquivos,
logs com lock, confirmação antiga, XEX errado, ausência de rollback, mkdir 413,
bytes pingados até timeout, exclusão recusada, porta ocupada e interrupção.
Os casos negativos precisam produzir FALHOU/exit 1 e os processos próprios precisam
estar mortos. Uma instalação normal fictícia fica como sentinela e deve permanecer
intacta em todos os casos. Não simula GPU, XAM nem a execução dos XEX.

## Preparar antes de contar os dez minutos

Se precisar regenerar (exige build Xbox prévia e uma pasta nova):

```bash
python3 tests/update/console-kit.py build --directory .work/update-console \
  --host 192.168.101.27 --port 8266
```

Com o build de PC disponível, `python3 tests/update/kit-test.py` confere os
pacotes, a assinatura/hash dos XEX reais, a troca/reserva usando esses bytes no
PC e as respostas HTTP. Isso não executa XEX nem substitui a prova no console.

O script gera sua própria chave descartável, passa a configuração de confiança por
flag de compilação e serializa as três compilações pelo lock da frota. O cache
`client/build-x360` passa a ser DE TESTE. O empacotador público recusa esse binário
sem `--test-package`. Para voltar a um build público, remova a flag de teste do
cache e recompile; nunca copie um XEX do kit para uma release.

```bash
cmake -S client/x360 -B client/build-x360 '-DCMAKE_CXX_FLAGS=-mcpu=cell -fno-pic' \
  -DTUBE360_BUILD_ID=3 -DTUBE360_VERSION=0.3.2
# depois rode o build Xbox público com seu ID/versão finais e APP_NAME correto
```

Extraia `install/tube360-v0.3.2-f26-n-xbox360.zip`. Copie a pasta **Tube360 Teste**
para `Hdd1:\Games\Tube360 Teste\`, mantendo `default.xex`, `tube360.xex` e fontes.
Use uma pasta de teste limpa; não reutilize journals de uma prova anterior e não
sobreponha o app normal. Abra o `default.xex` dessa pasta pelo gerenciador do Aurora:
o title ID é igual ao normal, então não dependa de qual atalho o scan escolheu.
Se já existir uma pasta de teste, guarde-a com outro nome primeiro.

Crie somente na pasta de teste `settings.txt` com `lang=pt`, `server=off`, `sound=tv`
(uma chave por linha), sem `nest.txt`/conta reais. Opcionalmente crie `net_log.txt`
com `192.168.101.27 7466`. Para ver o UDP no notebook:

```bash
python3 tests/update/udp-log.py --port 7466
```

Em outro terminal, suba o servidor (TCP 8266 precisa estar acessível pelo Xbox):

```bash
python3 tests/update/console-kit.py serve --directory .work/update-console
```

Confira no notebook `curl -f http://192.168.101.27:8266/app.json`.
Este HTTP existe apenas na build de teste e aceita somente a origem literal
compilada. A build pública continua exigindo HTTPS/GitHub e a chave pública real.
Para cancelar downloads mais demorados, aumente `--chunk-delay 0.1` no servidor.

## 0–2 min: entrada e cancelamento

1. Abra `Tube360 Teste/default.xex`. `launcher.log`: `journal ausente`,
   `tela gpu fechada status=0`, `caminho resolvido=game:\tube360.xex`,
   `launch chamada`. É esperado não haver `launch retorno` quando o handoff funciona.
2. Espere quatro segundos, entre em **Configurações → Atualizações do app → A**.
   Veja instalada `0.3.2-f26-n · #2601`, candidata `0.3.2-f26-good`, tamanho e **A Atualizar**.
   O UDP/`update.log` deve mostrar `available build=2602`.
3. Aperte A, mova pela interface e aperte B durante a barra. A tela continua respondendo;
   o UDP/`update.log` registra `download cancelado`. Não deve haver journal staged nem `.part`.

## 2–5 min: atualizar e confirmar

1. Volte à tela e aperte A. Aguarde **Atualização pronta**. UDP/`update.log`:
   `staged build=2602 sha256=...`. `update-app-state.json`: `phase=staged`, `highest=2602`.
   O `tube360.xex` em execução ainda é N; o candidato está em `tube360.next.xex`.
2. Saia normalmente para o Aurora e abra o mesmo `default.xex`.
   `launcher.log`: assinatura/journal válidos, progresso `hash bytes=...`,
   `prepare rename app -> previous`, `prepare rename next -> app`,
   `journal gravacao fase=trial ok`, GPU fechada, launch.
3. Deixe renderizar por pelo menos quatro segundos. Configurações deve mostrar
   `0.3.2-f26-good · #2602`. UDP/`update.log`: `boot confirmed build=2602`;
   journal `phase=good`. Reabra uma vez: continua N+1, sem rollback.

## 5–8 min: reserva automática

No notebook, mantendo o servidor ativo:

```bash
python3 tests/update/console-kit.py select --scenario fail
```

1. No Xbox, entre em Atualizações e aperte **X** para verificar de novo. Deve oferecer
   `0.3.2-f26-fail / #2603`. A baixa; aguarde staged. Saia e reabra `default.xex`.
2. A versão instalada mostra `0.3.2-f26-fail · #2603`. Espere quatro segundos:
   UDP/`update.log` contém `TEST boot confirmation intentionally withheld build=2603`.
   O journal deve continuar `phase=trial`.
3. Saia normalmente e reabra. `launcher.log`: `journal valido fase=trial`,
   `rollback inicio`, `journal gravacao fase=rollback ok`, `rollback fim`, launch.
   Volta a `0.3.2-f26-good · #2602`; `tube360.failed.xex` preserva o rejeitado.
   `highest=2603` permanece, portanto #2603 não reaparece como atualização.

## 8–10 min: integridade e encerrar

Para exercitar hash/assinatura antes do teste da reserva (ou após restaurar uma
cópia limpa de N em outra pasta), selecione um cenário e aperte X na tela:

```bash
python3 tests/update/console-kit.py select --scenario bad-signature
# Nenhuma oferta nova é aceita. Uma oferta já validada permanece até reiniciar.
python3 tests/update/console-kit.py select --scenario bad-hash
# Oferece manifesto válido, mas A termina em erro; tube360.xex continua N.
python3 tests/update/console-kit.py select --scenario good
```

Após rollback, #2602/#2603 ficam abaixo/iguais ao high water e não podem ser usados
para uma nova atualização; use uma instalação de teste limpa para repetir.
Não apague o journal da instalação normal para contornar esse contador.
Encerre servidor e UDP com Ctrl+C. Guarde `launcher.log`, `update.log`, journal e
print de Configurações em cada estágio para o relatório do console.

Se houver tela preta, não insista em reabrir a build nova diretamente: saia/recupere
o console como de costume e abra `default.xex` para a reserva. O launcher não
consegue reiniciar um kernel/XAM travado. Esta bancada não valida perda real de
energia entre operações FATX; essa prova permanece separada.
