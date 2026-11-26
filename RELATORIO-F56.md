# F56 — saída pelo sistema e classificação de fence

Branch: `f56-saida-limpa`. Base: `c80087f`. Data: 2026-11-26.

## Mudança e motivo

O app registra `xsdk_on_terminate(boot_session_terminated)` logo após `boot_open` no início de `main`. Durante essa abertura, prepara em memória estática o caminho físico de `session.txt` a partir do diretório de `ExLoadedImageName`. Não fixa HDD nem troca USB por outro drive.

O callback usa somente `NtCreateFile`, `NtWriteFile`, `NtFlushBuffersFile` e `NtClose`, com caminho, atributos, buffer e estado de I/O estáticos. Não consulta `game:`, não reutiliza handle do título e não usa stdio, alocação, mutex, log ou retry. Grava exatamente:

```text
clean
terminated by system
```

O launcher já ignora marcadores iniciados por `clean\n`. Fault, abort e interrupção sem notificação não chamam este callback e continuam deixando uma sessão reportável. Caminho não físico, caminho grande demais ou falha de abertura não limpam o marcador; falha de escrita/truncamento também continua reportável. Relatórios válidos já pendentes na fila conservam a política existente.

Launcher e receptor separam linhas de diagnóstico antes de classificar. Os dois avisos normais de orçamento de fence deixam de contar como falha de GPU ou timeout de rede. Uma falha de GPU precisa estar na própria linha: `Vulkan falhou (...)`, `error gpu.vulkan ...`/erro extraído `gpu.vulkan ...`, ou fence com falha explícita. `vkWaitForFences` continua crash. Informações de GPU deixam de substituir a assinatura de um fault; checkpoints informativos não alimentam o último ponto no launcher, e o receptor neutraliza esses pontos em relatórios legados. As linhas originais continuam disponíveis no log sanitizado.

## Arquivos

- `client/src/boot_log.c` e `.h`: preparação do caminho e callback do kernel.
- `client/src/main.cpp`: apenas duas linhas no início do `main`, para registro e confirmação no log.
- `client/src/update/launcher_reports.hpp`: evidência por linha e exclusão dos checkpoints informativos.
- `tools/error-reports/receiver.py`: confirmação de falha real e extração de ponto/assinatura.
- `tests/update/error-reports.cpp`: regressões de classificação, sessão encerrada e launcher.
- `tests/update/launcher_platform.py`: disponibiliza `terminate.h` no SDK simulado.
- `tests/update/terminate-session.c` e `terminate-session-test.py`: callback nativo com headers reais do xsdk e I/O de kernel simulado, seguido do launcher real.
- `tools/error-reports/test_fence.py`: regressões de categoria e assinatura no receptor.
- `RELATORIO-F56.md`: este relatório.

Não houve strings novas de interface, alterações de i18n ou mudanças da 0.4.

## Testes e saídas reais

Descoberta feita com `rg -l 'boot_log|launcher_reports|receiver.py|error-reports' tests bench client/CMakeLists.txt` e busca pelos nomes exatos dos fontes. Foram executados os contratos de relatório pedidos e os testes de abertura e diagnóstico nativo que exercitam o logger alterado. Também foram exercitados o probe TLS e os contratos que incluem ou religam o main. As referências restantes de política de vídeo e benchmarks são stubs ou registros de builds anteriores, sem executar o logger alterado.

As saídas completas da execução local estão em `client/build/f56-evidence/`, fora do Git. Todos os comandos abaixo terminaram com código 0.

### Relatórios do launcher

Comando: `python3 tests/update/error-reports-test.py`.

```text
293 error-report checks passed (real log + privacy canaries, launcher policy, durable sessions)
```

Inclui ausência de envio após `clean\nterminated by system\n`, fence informativa sem crash, erro real de Vulkan, sessão aberta reportável, último checkpoint útil e persistência após `_Exit`/`SIGKILL`.

### Callback compilado como xsdk e consumo do arquivo pelo launcher

Comando: `python3 tests/update/terminate-session-test.py`.

Trechos reais da saída:

```text
PASS raw termination \Device\Harddisk0\Partition1\Apps\Tube360\ mode=system create=1 write=1 flush=1 close=1; no stdio/malloc/lock
PASS launcher after system: reports=0
PASS raw termination \Device\Mass0\Apps with spaces\Tube360\ mode=system create=1 write=1 flush=1 close=1; no stdio/malloc/lock
PASS launcher after system: reports=0
PASS no callback: crash
PASS launcher after crash: reports=1
PASS no callback: hang
PASS launcher after hang: reports=1
PASS raw termination \Device\Mass0\Apps with spaces\Tube360\ mode=create-fail create=1 write=0 flush=0 close=0; no stdio/malloc/lock
PASS launcher after create-fail: reports=1
PASS raw termination \Device\Mass0\Apps with spaces\Tube360\ mode=write-fail create=1 write=1 flush=1 close=1; no stdio/malloc/lock
PASS launcher after write-fail: reports=1
9 native termination/launcher scenarios passed; no console accessed.
```

Os outros três cenários cobrem USB com caminho acima de 260 bytes, caminho `game:` indisponível e caminho acima do buffer de 1024 bytes. O teste apaga a imagem carregada em memória antes de invocar o callback, confere o caminho físico que foi preparado e intercepta tentativas de `fopen`, `malloc` e `pthread_mutex_lock` durante a notificação. O launcher recebe o arquivo que o callback escreveu, não uma cópia inventada do marcador.

### Launcher Xbox simulado

Comando: `python3 tests/update/launcher_platform.py client/build`.

Trecho real:

```text
PASS launcher report: receipt 0.03s to handoff
PASS launcher report: dns-hang 1.52s to handoff
PASS launcher report: slow 1.53s to handoff
PASS launcher report: offline 0.02s to handoff
PASS launcher report: garbage 0.02s to handoff
PASS launcher report: huge-code 0.02s to handoff
PASS launcher report: control-code 0.02s to handoff
PASS launcher report: opt-out 0.00s to handoff
PASS launcher report: settings-opt-out 0.00s to handoff
20 SDK simulations passed; physical drawing, timing, controller and UDP delivery remain unverified.
```

### Desmontagem do app real no PC

Comando: `python3 tests/update/exit-session-test.py client/build`.

```text
PASS app exit session: normal marker=clean exit=0
PASS app exit session: late-failure marker=open exit=73
2 actual app teardown scenarios passed; all HTTP replaced by an offline fixture.
```

### Receptor

Comando: `python3 -m unittest discover -s tools/error-reports -v`.

Trechos reais:

```text
test_actual_failures_remain_crashes (test_fence.FenceTests.test_actual_failures_remain_crashes) ... ok
test_fault_signature_ignores_incidental_gpu_information (test_fence.FenceTests.test_fault_signature_ignores_incidental_gpu_information) ... ok
test_information_is_not_crash_or_network_timeout (test_fence.FenceTests.test_information_is_not_crash_or_network_timeout) ... ok
test_open_session_and_legacy_checkpoint_stay_reportable (test_fence.FenceTests.test_open_session_and_legacy_checkpoint_stay_reportable) ... ok
Ran 28 tests in 22.298s
OK
```

A suíte também imprime a limitação preexistente `LIMITATION: 3 forged installation IDs from one IP created 1 fake GitHub issue`. Todos os receptores HTTP e GitHub usados nos testes são locais e falsos.

Durante o desenvolvimento, o teste de checkpoint legado detectou fallback indevido para `startup.video`, corrigido para `startup.handoff`. A primeira execução de `launcher_platform.py` detectou a ausência de `terminate.h` no SDK simulado; a fixture foi ajustada, e a execução final acima passou.

### Regressões do logger

Comando: `python3 tests/reddit/boot_native.py client/build/f56-evidence/boot-native`.

```text
PASS native boot usb syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot hdd syncs=40 path=\Device\Harddisk0\Partition1\Tube360\boot-app.log
PASS native boot component syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot unknown-av syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot absent syncs=33 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot sync-fail syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot 480i syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot 576i syncs=40 path=\Device\Mass0\Tube360\boot-app.log
PASS native boot 480p syncs=40 path=\Device\Mass0\Tube360\boot-app.log
```

Também passou o décimo caso `long`, usando `ba.log`; a linha completa com o caminho longo está na evidência local.

Comando: `XT_NEST_NETWORK=127.77.53.1/255.255.255.252 LP_NUM_THREADS=2 python3 tests/reddit/boot_log_test.py client/build/f56-evidence/boot-app`.

```text
PASS app boot normal rows 89
PASS app boot missing-fonts rows 28
PASS app boot video-fail rows 41
PASS app boot sdl-fail rows 16
PASS app boot long-path rows 89
PASS durable marker survives exit without normal close
PASS durable marker survives kill without normal close
```

### Contratos que incluem ou religam o main e probe TLS

O contrato `tests/f52/contracts.cpp` foi compilado e ligado com os mesmos flags de `tests/f52/run.py` (`-O0 -DNDEBUG` e bibliotecas de `client/build`), com objetos e arquivos de execução redirecionados para `client/build/f56-evidence/f52`, sem escrever na frente F52. Saída real final:

```text
284 F52 checks passed
```

Comando: `python3 tests/update/service-test.py --build client/build --output client/build/f56-evidence/update-service`.

```text
PASS HTTP service: good auto
PASS HTTP service: good download
PASS HTTP service: good cancel
PASS HTTP service: bad-hash error
PASS HTTP service: bad-range error
PASS HTTP service: redirect-out error
PASS HTTP service: bad-signature reject
PASS HTTP service: good legacy
PASS HTTP service: good corrupt
PASS production trust and file-trigger isolation; 9 worker HTTP scenarios
```

Comandos: `cmake --build client/build --target reddit-tls-probe -j6` e `python3 tests/reddit/tls.py "$PWD/client/build/reddit-tls-probe" client/build/f56-evidence/tls`.

```text
TLS probe: tls_date tls_certificate_date HTTP 0 body 0
PASS expired tls_date durable TLS stages without URL/query
TLS probe: tls_date tls_certificate_date HTTP 0 body 0
PASS future tls_date durable TLS stages without URL/query
TLS probe: tls tls_peer_verification HTTP 0 body 0
PASS untrusted tls durable TLS stages without URL/query
```

`git diff --check`: código 0, sem saída.

## Builds

PC, com FFmpeg já pronto e sem rebuild em `.work`:

```sh
cmake -S client -B client/build -DFFMPEG_PREFIX="$PWD/.work/ffmpeg-pc"
cmake --build client/build -j6
```

Saída real final:

```text
[100%] Built target tube360-main
[100%] Linking CXX executable tube360
[100%] Built target tube360
```

Xbox: um único build, código de saída 0:

```sh
PATH="$HOME/.local/bin:$PATH" FFMPEG_PREFIX="$PWD/.work/ffmpeg-xbox" TUBE360_VERSION=0.3.3 \
  flock /tmp/tube360-build-xbox.lock systemd-run --user --scope -q -p MemoryMax=6G tools/build-xbox.sh
```

Trechos reais:

```text
make: 'build/libxsdk.a' is up to date.
make: 'build/host/libxsc.so' is up to date.
make: 'build/libvulkan4xenon.a' is up to date.
[100%] Built target tube360
05c3fe8292dae0619b1e71ffd430de66cd96412a16f0d0cf07e850dff44be9dd  build-x360/tube360.xex
fcd9b3fab23717ea596da1a53aafc4b97444585b88b951092933203b193c7a02  build-x360/tube360-launcher.xex
```

Artefatos locais: `client/build-x360/tube360.xex` e `client/build-x360/tube360-launcher.xex`. São prova de compilação da versão 0.3.3, com build ID local 1; não foi gerado ou publicado pacote de release.

A desmontagem do objeto PowerPC de `boot_session_terminated` confirma exatamente quatro chamadas (`NtCreateFile`, `NtWriteFile`, `NtFlushBuffersFile`, `NtClose`), escrita de 27 bytes e nenhum salto para libc/newlib. Evidência: `client/build/f56-evidence/terminate-ppc.txt`. Também foi conferida a chamada a `xsdk_on_terminate` logo após `boot_open` na desmontagem de `main` do executável final. Símbolos reais do executável:

```text
829d11e0 T boot_session_terminated
82a8e688 T xsdk_on_terminate
```

O build emitiu avisos em fontes não alterados (FriBidi no PC; mbedTLS, curl, picojson e parâmetro de Cast no Xbox). A ferramenta do xsdk também avisou `227 instructions Xenia skips; Xenia results through these functions are unreliable`; não foi usado Xenia como prova de execução.

## Limites e conferência posterior no console

Não houve acesso ao Xbox, Nest, Bluetooth ou serviços de produção. O teste nativo usa as declarações reais do xsdk e o callback real, com chamadas de kernel substituídas por arquivos locais; não prova a notificação de XAM, a latência de armazenamento nem o prazo físico de 2 s. O callback faz uma abertura, uma escrita de 27 bytes, um flush e um fechamento, sem laço ou retry. O receptor foi alterado em fonte e não foi implantado; a validação fim a fim depende de usar o receptor atualizado. Dados históricos de relatórios já armazenados não foram reclassificados.

Depois que o console estiver livre:

1. Usar uma pasta de teste sem relatórios pendentes anteriores e com relatórios habilitados. Instalar o app e o launcher desta branch em HDD, por exemplo `Hdd1:\Apps\Tube360-F56\`, preservando fontes e assets. Na instalação, `tube360-launcher.xex` deve ser `default.xex`; o app deve ser `tube360.xex`. Repetir depois em `Usb0:\Apps\Tube360-F56\`.
2. Abrir `default.xex`, esperar Home e navegar. Antes de sair, `session.txt` começa por `open` e contém `app_started=1`. Em `boot-app.log`, verificar `info session.terminate system termination callback registered; raw kernel marker`. O caminho de abertura do log deve apontar para o próprio HDD ou USB. Em pastas longas o arquivo pode se chamar `ba.log`.
3. Pelo botão Xbox, abrir o Guide e escolher Xbox Home/sair para o dashboard. Somente abrir e fechar o Guide não encerra o título. Confirmar que o dashboard continua respondendo, inclusive ao abrir outro título.
4. Antes de reabrir o launcher, ler `session.txt` pela interface de arquivos disponível: deve conter exatamente as duas linhas `clean` e `terminated by system`. O callback não acrescenta uma linha ao log; o encerramento externo pode deixar o log sem `end app.exit`, o que é esperado. Não atribuir crash à presença de `begin frame.fence first-frame timeout=5000ms` ou ao aviso de limite de `frame.complete`.
5. Reabrir `default.xex`. Não deve aparecer recibo `T360-...` por essa saída, nem surgir `error-report.json` novo. No trecho dessa partida de `launcher.log`, não deve aparecer `relatorio enviado:` nem `relatorio pendente; proxima partida`. O novo `session.txt` volta a `open`, porque a próxima sessão já começou; por isso o passo 4 precisa ocorrer antes. Conferir o receptor atualizado para ausência de um novo relatório dessa saída.
6. Repetir a saída pelo sistema no USB e, se possível, durante reprodução ou carregamento. Verificar que somente o `session.txt` da instalação executada mudou e que o retorno ao dashboard não congela.
7. Como controle de interrupção sem aviso, usar a instalação de teste e encerrar a energia enquanto o app roda, sem o menu de desligar. Ao voltar ao dashboard, ler o marcador antes de abrir o launcher: deve continuar `open`. Ao abrir o launcher, deve tentar um relatório e preservar a fila se estiver offline. Não reproduzir um travamento real de GPU só para validar o classificador: os casos de `Vulkan falhou ... vkWaitForFences` e `error gpu.vulkan` já foram exercitados localmente. Fault/abort reais exigem um build de diagnóstico separado para conferência física e não foram induzidos nesta frente.

## Divergências entre este prompt e o código real

- Não foi fornecido caminho de argumento `-o` na solicitação recebida; o relatório foi salvo nesta worktree como `RELATORIO-F56.md`.
- O launcher já aceita qualquer marcador com prefixo `clean\n`; não foi necessário mudar seu parser de sessão. Uma sessão limpa não descarta relatórios válidos que já estavam na fila de uma sessão anterior.
- A falha real de Vulkan é emitida em `client/src/gfx.hpp`, fora da frente. Esse arquivo foi apenas lido; a correção reconhece suas mensagens nos classificadores autorizados.
- `point` e a assinatura são calculados somente pelo receptor. No launcher foi corrigida a origem do último checkpoint, e no receptor o ponto extraído e o fallback de checkpoints legados.
- `boot_log.h` passou a incluir `xsdk/terminate.h` no alvo xsdk para permitir o registro exclusivamente na região autorizada do `main`. O SDK simulado de `launcher_platform.py` precisou disponibilizar esse header.
- O script de build Xbox chama os preparadores e `make` das dependências em `.work`. Foi verificado antes com `make -q` que estavam atuais; a saída real do build confirma `up to date`, sem recompilação ou remoção nessas dependências.
