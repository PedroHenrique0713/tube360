# F32 — erros de início/conexão, recuperação de qualidade e gatilhos

Execute da raiz do worktree. As provas usam pastas próprias, respostas gravadas,
certificados descartáveis em loopback e mídia sintética. Nenhuma conta, Nest,
cache de produção ou requisição ao YouTube é necessária.

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 tube360-quality-fixture reddit-contract-test reddit-tls-probe player-state-test h264-cadence-test
client/build/reddit-contract-test
python3 tests/reddit/tls.py client/build/reddit-tls-probe
python3 tests/reddit/ui.py .work/f32/ui
```

Prepare a mídia e execute o player:

```bash
python3 tests/reddit/prepare.py .work/f32/media
python3 tests/reddit/playback.py .work/f32
JOBS=2 tools/build-standalone.sh pc
client/build/player-state-test
client/build/h264-cadence-test
c++ -std=c++17 -Iclient/src client/src/player_ui_test.cpp -o /tmp/f32-player-ui
/tmp/f32-player-ui
c++ -std=c++17 -Iclient/src $(pkg-config --cflags sdl3) tests/reddit/startup_wrap.cpp -o /tmp/f32-startup-wrap
/tmp/f32-startup-wrap
```

`ui.py` grava 36 PNGs: seis falhas de rede e seis de início, em pt/en/es.
A ausência das fontes é real: a pasta daquele cenário contém só `settings.txt`.
A pasta sem escrita usa `chmod 0555`, restaurado em `finally`. Também mantém uma
instância sem entrada e exige que continue aberta, encerrando só seu próprio PID.
SDL falha por um driver de áudio inexistente. GPU/launch/prazo são injetados apenas
na build desktop por `XT_STARTUP_ERROR`.
A tela de emergência do Xbox é exercitada pelas 11 simulações do SDK existentes,
agora incluindo a espera por B e o cancelamento do watchdog ao mostrar um erro.
O desenho físico do xsdk e o controle do console exigem o roteiro abaixo.

`tls.py` requer `cryptography`; `ui.py` e `playback.py`, Pillow; `prepare.py`, FFmpeg
com libx264. Os testes não mudam o relógio da máquina. As flags de validade vêm da
verificação real do mbedTLS, conservando os erros de cadeia e nome.

`playback.py` aceita casos opcionais `recovery recovery-seek preview release paused`. A rodada
`recovery` dura 65 s: a fixture atrasa apenas as ranges de 1080 por 2 s nos primeiros
25 s e depois libera a rede. Exige 1080 → 720 → 1080 no player real, sem reiniciar
sua sessão de áudio. `recovery-seek` repete após um seek enquanto a fonte está
reduzida, provando que o cache conserva o teto original. A política isolada cobre CPU insuficiente, rede insuficiente,
fila vazia, pausa, perda de quadros, medidas antigas e recuo após uma subida ruim.
Não se trata de uma medida de desempenho do Xenon.

No PC, Q/E representam LT/RT. Em `XT_KEYS`, `l`/`r` ficam pressionados até a próxima
entrada: repita a letra para manter; `.` solta. Exemplo: `XT_KEYS_MS=700
XT_KEYS=....rrrr.`. A posição de destino e a barra avançam enquanto o vídeo está
pausado para o preview. Soltar faz um único seek, mantendo o estado anterior de
pausa. B cancela; View sai. A aceleração passa de 10 s/s para 30 s/s após 2 s e
120 s/s após 5 s, além do primeiro passo de 10 s. Lives e abertura desconhecida
continuam recusando seek. O ajuste de sincronia da caixa permanece em Configurações.

## Roteiro do console — somente o coordenador

Não foi executado pelo autor. Use pasta temporária nova e o controle físico para
B/LT/RT. Não mude a hora do console de uso diário só para testar TLS.
O teste de certificado acima prova a classificação sem essa alteração.

Da raiz deste worktree, com o app atual fechado:

```bash
F32_XBDM=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
F32_IP=192.168.101.39
F32_REMOTE='HDD:\Games\Tube360 F32'
mkdir -p .work/f32/console
python3 "$F32_XBDM" "$F32_IP" mkdir "$F32_REMOTE"
python3 "$F32_XBDM" "$F32_IP" put client/build-x360/tube360-launcher.xex "$F32_REMOTE\\default.xex"
python3 "$F32_XBDM" "$F32_IP" put client/build-x360/tube360.xex "$F32_REMOTE\\tube360.xex"
printf 'lang=pt\nserver=off\nsound=tv\nvideo_mode=1080p30\n' > .work/f32/console/settings.txt
python3 "$F32_XBDM" "$F32_IP" put .work/f32/console/settings.txt "$F32_REMOTE\\settings.txt"
python3 "$F32_XBDM" "$F32_IP" run "$F32_REMOTE\\default.xex"
python3 "$F32_XBDM" "$F32_IP" shot .work/f32/console/missing-fonts
```

1. Sem fontes: exigir tela em português com os quatro arquivos, instrução de cópia
   e B. Esperar 30 s: não deve retornar ao Aurora pelo watchdog. B deve sair.
   Baixar `startup.log` e `launcher.log` com `get`.
2. Abrir `tube360.xex` diretamente, ainda sem fontes: mesma tela, sem depender do
   lançador. Capturar, B para sair. Repetir pt/en/es trocando só `lang=` no arquivo.
3. Voltar ao Aurora (`run 'Usb0:\Aurora\Aurora.xex'`), enviar as quatro fontes e
   remover `tube360.xex` temporariamente. O lançador deve informar que não conseguiu
   abrir o app, mantendo a tela até B. Restaurar o XEX.
4. Completar a pasta e abrir normalmente. Não deve aparecer tela de erro. Escolher
   um VOD público de mais de 2 minutos, perfil 1080p30, som TV. Com rede limitada
   externamente, observar recuo no log; liberar a rede e exigir pelo menos 20 s de
   folga, intervalo mínimo de 30 s desde a troca e subida gradual. Se a CPU não
   sustenta a projeção, permanecer em 720 é o resultado correto. Repetir com 720p60.
5. Segurar RT por 1/3/6 s: conferir a aceleração e o destino. Soltar: um único
   reinício no destino, sem saltos repetidos. LT no começo deve parar em zero; RT
   no fim para em duração−3 s. Repetir pausado; B cancela sem sair do vídeo. Repetir
   com som Nest apenas se o coordenador tiver autorização para usar o aparelho.
6. Rede desconectada e DNS inválido em ambiente de teste: mensagens diferentes.
   Não alterar configurações de rede/relógio de uso diário sem autorização.
7. Antes da limpeza, baixar `default.log`, `startup.log`, `launcher.log`, `update.log`
   e os prints para `.work/f32/console`. Procurar `motivo=`, `folga estavel`,
   `busca: segurando`, `busca: soltou`, sem regressão de posição ou áudio duplicado.
8. Voltar ao Aurora e apagar **só** a pasta temporária e seus arquivos. A exclusão
   da pasta usa `delete name="HDD:\Games\Tube360 F32" dir` pelo cliente XBDM do
   coordenador, após conferir/arquivar a listagem. Não reiniciar o console.

Arquivos:

```bash
for f in client/assets/inter-*.ttf; do
  python3 "$F32_XBDM" "$F32_IP" put "$f" "$F32_REMOTE\\${f##*/}"
done
python3 "$F32_XBDM" "$F32_IP" get "$F32_REMOTE\\startup.log" .work/f32/console/startup.log
```

O log de emergência é `startup.log`, aberto e fechado antes de desenhar. Uma pasta
sem escrita também impede esse log; nesse caso o diagnóstico continua na tela e
no logger do xsdk. Se a própria GPU não puder produzir nenhum quadro, não há como
provar texto na TV por software: o motivo fica no log, e B ainda é consultado
diretamente pelo XAM. Essa condição física não é simulada como prova de desenho.

## Adendo F32/5 — servidor efetivo na Home

```bash
cmake --build client/build -j2 --target tube360 reddit-contract-test
client/build/reddit-contract-test
python3 tests/reddit/home_errors.py .work/f32/item5
ctest --test-dir .work/standalone-pc --output-on-failure
flock ~/.cache/tube360-frota-xbox.lock systemd-run --user --scope -q \
  -p MemoryMax=6G -p MemorySwapMax=1G \
  cmake --build client/build-x360 -j2 --target tube360 tube360-launcher
```

A configuração Xbox acima reutiliza o toolchain já preparado por `tools/build-xbox.sh`.
Todos os builds desta retomada usam `-j2`.

`home_errors.py` executa o app real e gera 30 PNGs. Os 18 casos principais omitem
**tanto `server=` quanto `server.txt`**: a preferência continua `true` e o servidor
efetivo precisa estar desligado. TLS por data exibe relógio UTC; TLS geral, DNS,
rede, recusa do YouTube e resposta inválida mantêm seus diagnósticos em pt/en/es.

A reserva de teste responde HTTP 503 em uma porta efêmera de `127.0.0.1`; não toca
8091 nem qualquer servidor real. Em cada língua, o caso com `server.txt` e
`server=auto` exige o título de servidor, o endereço real e a instrução de iniciar
o servidor. Os demais casos exigem zero pedidos à reserva e ausência de texto
sobre servidor/`0.0.0.0` na mensagem desenhada. Há ainda `server=auto` explícito sem
endereço, endereço com `server=off` e 640×480 com texto grande.

O log `captura erro Home` registra as chaves e o detalhe usados pelo próprio
bloco de desenho naquela captura; não injeta mensagem na interface. Essa
instrumentação existe apenas no PC. A prova compara a preferência com o estado
efetivo e os textos desenhados, além de verificar as imagens. `results.json`
contém os 30 resultados e caminhos absolutos; `proofs/home-errors-montage.png`
mostra as seis causas sem servidor nas três línguas e a reserva na última linha.

Complemento do roteiro físico para o coordenador: na pasta temporária F32, omitir
`server.txt` e a linha `server=`; iniciar sem rede e conferir a mensagem de rede,
sem instrução para ligar servidor nem endereço `0.0.0.0`. Alternar Automático →
Desligado → Automático sem endereço deve continuar só console. Conferir a tela em
pt/en/es. A reserva configurada e indisponível deve manter a instrução de servidor.
Coletar logs/prints e limpar a pasta conforme o roteiro anterior. Não foi executado
no console pelo autor.

## Adendo F32/6–7 — log persistente e ambiente

O mapa completo das etapas e das dependências está em
[STARTUP-DIAGNOSTICS.md](../../docs/STARTUP-DIAGNOSTICS.md).
Execute os builds de PC com `-j2` nesta máquina compartilhada:

```bash
cmake --build client/build -j2 --target tube360 tube360-quality-fixture reddit-tls-probe reddit-contract-test
python3 tests/reddit/boot_log_test.py .work/f32/items67/app
python3 tests/reddit/tls.py client/build/reddit-tls-probe .work/f32/items67/tls
python3 tests/reddit/home_errors.py .work/f32/items67/home
python3 tests/reddit/playback.py .work/f32 preview release paused
JOBS=2 tools/build-standalone.sh pc
```

`boot_log_test.py` executa o app real: abertura normal, fontes realmente ausentes,
driver SDL inexistente, falha de vídeo injetada e caminho longo com espaços. Cada
caso guarda `stdout.log`, `assets/boot-app.log` e `screen.png`. Os casos normais
exigem todos os marcadores, ordem begin/end e apenas uma aquisição registrada,
sem I/O de abertura em cada frame. Dois subprocessos do logger real são encerrados
por `_Exit` e SIGKILL: o último begin precisa sobreviver sem fclose/destrutores.
Isso prova o flush no host, não a durabilidade de uma ponte USB de console.

Depois de configurar a build Xbox conforme `docs/BUILD.md`, prepare/recompile as
cópias privadas e os dois títulos dentro do lock. `XSDK_TOOLCHAIN` deve apontar
para o toolchain já instalado; os comandos abaixo reutilizam o CMake Xbox configurado:

```bash
flock ~/.cache/tube360-frota-xbox.lock systemd-run --user --scope -q \
  -p MemoryMax=6G -p MemorySwapMax=1G bash -c '
    set -e
    python3 tools/prepare-boot-diagnostics.py .work/native-display/xsdk .work/native-display/vulkan4xenon
    python3 tools/prepare-video-policy.py .work/native-display/xsdk .work/native-display/vulkan4xenon
    make -C .work/native-display/xsdk -j2 "TOOLCHAIN=$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-" build/libxsdk.a
    make -C .work/native-display/vulkan4xenon -j2 "TOOLCHAIN=$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-" "XSDK=$PWD/.work/native-display/xsdk" build/libvulkan4xenon.a
    cmake --build client/build-x360 -j2 --target tube360 tube360-launcher
  '
python3 tests/reddit/boot_link.py .work/f32/items67/link \
  --toolchain "$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-"
python3 tests/reddit/boot_native.py .work/f32/items67/native
python3 tests/reddit/socket_policy.py .work/f32/items67/sockets --source-sdk "$F32_ORIGINAL_XSDK"
python3 tests/reddit/swapchain.py
```

`F32_ORIGINAL_XSDK` é um checkout **original, sem nossos hooks**, usado somente para
leitura. O teste copia três fontes para uma pasta temporária, aplica o preparador
duas vezes e exige idempotência. `--sdk` nos testes nativos e `--v4x` no teste de
swapchain permitem outros caminhos, sem depender de um checkout pessoal.

- `boot_native.py`: compila os arquivos reais com `__XSDK__` e headers reais do SDK;
  só exports e I/O NT são simulados. USB/HDD, HDMI/componente/enum desconhecido,
  exports ausentes, caminho perto do limite e fsync com EIO. Exige flushes e
  preservação de errno; termina sem fechar normalmente. Não usa rede remota.
- `socket_policy.py`: executa o **socket.c realmente preparado**, com NetDll fake;
  funções POSIX são renomeadas para não substituir o libc do runner. Cobre sucesso,
  limites recusados com retry, opção sem suporte 10042/10045, acesso negado 10013,
  falha de XNet e de WSA. Não abre conexões reais.
- `swapchain.py`: extrai e compila as funções reais de conversão de timeout e
  aquisição de imagem da cópia preparada; simula somente a apresentação nativa.
  Cobre saturação, sucesso, imagem já adquirida, falha NT, endereço desconhecido,
  timeout e retry. Não prova registradores/GPU/TV do console.
- `boot_link.py`: lê a seção de imports dos ELF finais, guarda os imports e o
  disassembly do CRT, exige hooks fortes e ordem anterior aos locks/newlib. Cobre
  o caso em que referência fraca não extrai um objeto de uma biblioteca estática.

No roteiro físico, coletar também `boot-crt.log`, `boot-early.log`,
`boot-launcher.log` e `boot-app.log`, ou `bc.log/be.log/bl.log/ba.log` em caminho
longo. **Baixar antes de relançar**: `boot-crt.log` é sobrescrito na entrada de
cada título. Ausência de arquivo não distingue loader de pasta sem escrita.
Comparar `begin` sem `end`, configuração HDMI/componente e caminho real; não
atribuir a trava ao USB só pela capacidade do disco. ABadAvatar e a revisão de
Jasper do item 8 permanecem sem validação física nesta entrega.

## Adendo F32/8 — Jasper e caminho conservador

```bash
python3 tests/reddit/video_policy.py .work/f32/item8/policy
python3 tests/reddit/video_driver.py .work/f32/item8/driver \
  --source-sdk "$F32_ORIGINAL_XSDK" --source-v4x "$F32_ORIGINAL_V4X"
python3 tests/reddit/boot_native.py .work/f32/item8/native
python3 tests/reddit/boot_log_test.py .work/f32/item8/app
python3 tests/reddit/boot_link.py .work/f32/item8/link \
  --toolchain "$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-"
```

Os dois diretórios `F32_ORIGINAL_*` são checkouts sem os patches do Tube360,
somente para leitura. O teste copia os preparadores para uma raiz temporária e
executa o pipeline real do zero, sem alterar esses fornecedores ou a cópia usada
pela build Xbox. Exige idempotência; usa headers reais do SDK/V4X.

`video_policy.py` executa a política C real em 22 processos: sete famílias,
desconhecida, módulo/export ausente, auto/safe/native, whitespace/CRLF, vazio,
caixa diferente, lixo, NUL, tamanho excedido e acesso negado. Guards demonstram
que o arquivo só é lido uma vez. `input.json` conserva a entrada: o harness muda
o arquivo depois da inicialização para provar que a escolha não muda durante IRQ/alocação.

`video_driver.py` executa seis casos do display real contra o fonte original,
com MMIO substituído por memória **do próprio processo Linux** em endereço
reservado por MAP_FIXED_NOREPLACE. Nenhum /dev/mem, registrador físico ou console
é acessado. Safe HD/SD/interlaced/contador parado têm snapshots iguais aos do
original; native HD/SD mantém dimensões estendidas. Um contador que não avança
retorna após os 50.000 µs simulados. Depois de desabilitar o trace, a rotina de
flip usada pela IRQ não registra eventos.

Os testes de memória executam APIs Vulkan reais de capacidades/requisitos e a
função integral de alocação: safe esconde o tipo 2, recusa linear sampled R8 e
não chama alocação física cacheada. Native mantém essas extensões. Instruções
PPC de cache são a fronteira simulada; os guards e loops reais de flush/invalidate
exigem zero operações adicionais em safe. Três testes de `xsdk_present_open`
real exercitam sucesso, falha do primeiro show e falha letterboxed, verificando
liberação de buffers e ausência de callback em erro. Sync/Vd/allocações são simulados.

O [manual](../../docs/STARTUP-DIAGNOSTICS.md#board-policy-and-choosing-a-video-path)
explica `video-driver.txt` e cada registrador/etapa. Conforme o Adendo 4, `auto`
seleciona native para Corona/Winchester e safe para as demais famílias/unknown,
preservando o baseline do 360 E do coordenador. Isso não afirma teste em todas as
subrevisões. `native` e `safe` continuam disponíveis para override explícito.
Os checkpoints são compartilhados por app, lançador e tela de emergência.

Para o coordenador, usar a pasta temporária existente e guardar separadamente
as tentativas safe/native, com fotos da TV e logs antes de relançar. Primeiro
conferir `video.board flags/model`, `video.policy requested/selected`, depois
o último begin sem end de GPU/LUT/scaler/surface/line-counter/IRQ. O caso Jasper
e o 360 E físico continuam sem teste pelo autor; a igualdade em memória simulada
não afirma que o driver original é compatível com todos os chips de vídeo.

## Adendo F32/9–11 — estabilidade, Configurações e visibilidade

```bash
cmake --build client/build -j2 --target tube360 tube360-quality-fixture reddit-contract-test reddit-tls-probe player-state-test h264-cadence-test
client/build/reddit-contract-test
python3 tests/reddit/settings_native.py
python3 tests/reddit/settings_stress.py .work/f32/items9-11/settings
python3 tests/reddit/adaptation.py .work/f32/items9-11/adaptation .work/f32/media
python3 bench/quality/network_e2e.py .work/f32/items9-11/network
python3 tests/reddit/playback.py .work/f32 recovery-seek preview release paused
python3 tests/reddit/video_policy.py .work/f32/items9-11/policy
```

O diretório de mídia da terceira chamada deve ter os MP4/AAC sintéticos de 80 s
preparados por `tests/reddit/prepare.py`; nenhuma conta ou URL real é usada.
`adaptation.py` acrescenta 480/360 **realmente disponíveis** para a prova não passar
só por falta de fonte inferior. Usa FFmpeg com duas threads. Exercita abertura
transitória (sem queda), pressão prolongada de 1080 com retorno após folga e o
perfil padrão com apenas 720p60/360p30: mantém 720p e tenta apresentação 30 antes
que pudesse escolher 360. Guarda logs completos da fixture e PNGs. Dados e tempos
são da bancada, não medidas do Xbox.

As decisões VOD desconsideram oito segundos de abertura/troca/resume/alteração de
recursos. Só contam janelas inteiras posteriores ao prazo, com vídeo medido,
atividade sem pausa e sinal de starvation **mais** carga demonstrada: decoder,
cópia/apresentação ou vazão medida insuficiente. Exigem cinco segundos em pelo
menos três janelas. Uma fila momentaneamente vazia não prova CPU insuficiente;
18–20 ms de decode em 720p30 com cópia curta e rede suficiente não pede 480p.
Uma fonte ausente/erro fatal de leitura ainda pode exigir outra tentativa direta;
essa falha é registrada separadamente e não é uma decisão de pressão.

A fonte real 720p30 tem prioridade como próximo degrau. Se ela não existe, o
itag de 720p60 pode ser mantido com apresentação 30 (e descarte permitido de
quadros não referenciais); essa etapa não reduz os bytes de rede, como o log
explica. Só pressão renovada depois de assentar pode reduzir a resolução. Se
480p não é oferecido, o próximo formato real pode ser 360p; não é prometido um
itag inexistente. A subida continua gradual, com 20 s de folga medida, intervalo
mínimo de 30 s e backoff após uma subida malsucedida.

`settings_native.py` executa as funções reais `output/quiesce` com `__XSDK__` e
mocks Vulkan estritos: safe não chama idle/reset/destroy/set_output; erro de idle
ou de reset não retira recursos; native só destrói depois dessas duas checagens.
`settings_stress.py` usa o app e o Vulkan reais, entradas de controle em pt/en/es,
três ciclos de saída, idioma, texto e fit. A flag **só desktop**
`XT_TEST_FIXED_OUTPUT=1` exercita o veto de saída sem fingir detecção física de
Falcon. A build XEX não contém essa flag. Atlas e referências de comandos têm a
ordem de eventos verificada; capturas com ajuda do modo seguro cabem em texto grande.

`video_policy.py` agora liga o **log.c real do SDK**: mocks são apenas kernel
(arquivo/lock/export). Um socket UDP real recebe os datagramas em loopback numa
porta efêmera; família/flags/path precisam constar tanto no arquivo normal quanto
no UDP. `input.json`, `boot.log`, `sdk-normal.log` e `udp.log` conservam cada caso.
Não acessa o console nem qualquer receptor externo. O resumo também é chamado
no CRT depois de `xsdk_log_open`, antes dos construtores; `boot_link.py` verifica
essa ordem nos dois ELF finais.

### Roteiro complementar do coordenador

Usar somente a pasta temporária já descrita e o par de XEX correspondente.
Não misturar arquivos de conta; não alterar o serviço ou o app de uso diário.

1. Sem `video-driver.txt`, no 360 E: iniciar e procurar no **log normal do título**
   e UDP `video: model=Corona|Winchester ... path=native requested=auto`. No Falcon/
   Jasper: `path=safe`; também conferir a mesma seleção nos logs boot. Se vier
   unknown, arquivar flags/status e comparar o override explícito sem inferir exploit.
   `startup.log` pode estar vazio numa abertura bem-sucedida.
2. Se precisar UDP, criar `net_log.txt` **na pasta de teste** com IP/porta do
   receptor do coordenador, no formato já suportado `<IPv4> <porta>`. Guardar o log
   normal mesmo se a rede não permitir UDP. Não enviar tokens/arquivos de conta.
3. Reproduzir `4p_9OPKgcDE` em 1080p30 e `wWC8wVI4E3E` com perfil padrão.
   Guardar cada segundo de `decod:`, `rede:`, `qualidade: decisao`, `fonte` e
   `apresentacao`. Conferir abertura/troca com `assentando`, cinco segundos de
   pressão elegível, um degrau por vez, ausência de 480 quando 720p30 mantém folga
   e retorno gradual quando a medição permite. Registrar itags realmente oferecidos;
   apresentação 30 sobre o mesmo 298 não equivale a uma nova fonte 136.
4. No caminho safe, alternar saída 480/720/1080 repetidamente: tamanho real,
   swapchain e atlas devem permanecer iguais; a ajuda explica o veto. Idioma e
   fit não refazem o display. Texto grande troca só atlas/layout, com GPU idle,
   reset de comandos, upload concluído e aposentadoria do atlas antigo em ordem.
5. No 360 E/native, repetir as trocas: observar os mesmos checks antes de retirar
   framebuffer/swapchain, e os checkpoints do primeiro quadro novo. Repetir idioma,
   texto e fit; B continua voltando. Fotografar pt/en/es, inclusive texto grande.
6. Coletar log normal/UDP, boot-early/app/launcher/raw e fotos **antes de relançar**.
   Uma espera kernel/MMIO que não retorna ainda pode bloquear o timeout de software:
   guardar o último begin aberto. Arquivar por modelo/path/saída e restaurar/remover
   só a pasta temporária. Este roteiro não foi executado fisicamente pelo autor.
