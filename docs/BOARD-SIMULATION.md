# Bancada de placas: PC, board-sim no Xbox e Xenia

F38, fase 2. Complementa a [auditoria de hardware](HARDWARE-COMPATIBILITY.md).
O objetivo é reproduzir **decisões e falhas de software** com entradas controladas e depois executar
o mesmo caminho no 360 E. Um perfil Jasper passando nesta bancada não comprova funcionamento em Jasper.
Os clocks nominais e a capacidade de RAM/eDRAM são comuns às famílias retail; não há fundamento no
levantamento para reduzir automaticamente o clock ou a RAM de um Falcon/Jasper.

## Regra para preservar o console do dono

1. A integração 0.3.2 mantém Corona e Winchester em `native` por padrão, como na F32 e no baseline confirmado pelo coordenador. As demais famílias/unknown usam `safe`.
2. `TUBE360_NATIVE_BOARD=corona` ou `winchester` restringe explicitamente esse padrão à família escolhida. `unconfirmed` mantém o padrão Corona/Winchester; não significa qualificação física de todas as revisões.
3. `video-driver.txt=safe` ou inválido reduz o caminho. `native` pede explicitamente o caminho nativo; `auto` mantém a regra acima.
4. `board-sim.txt` tem precedência: por padrão força seguro; `driver=native` pede experimento declarado. Arquivo inválido força seguro e desliga as injeções. A detecção física permanece separada.

Exemplo de build **só após confirmar** a família Corona no log do dono:

```bash
TUBE360_NATIVE_BOARD=corona tools/build-xbox.sh
# PC de bancada com a mesma regra:
cmake -S client -B client/build -DTUBE360_NATIVE_BOARD=corona
cmake --build client/build -j2 --target tube360
```

Não remover o override nativo do uso normal antes de comparar o baseline. O check de modo real do
dashboard continua valendo: a simulação não pode elevar o framebuffer acima do que a seleção real
do driver autorizou. Não modifica o sinal HDMI, PLL, HANA/ANA, SMC ou o conteúdo de XboxHardwareInfo.

## Perfis no PC

Sem `XT_BOARD` e sem `board-sim.txt`, o renderer PC mantém o comportamento normal de antes.
Para simular uma família e aplicar a política real do Xbox:

```bash
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
  HOME="$PWD/.work/meu-teste/home" XDG_CACHE_HOME="$PWD/.work/meu-teste/cache" \
  XT_ASSETS="$PWD/.work/meu-teste/assets" XT_YT_REPLAY="$PWD/.work/meu-teste" \
  XT_BOARD=jasper XT_CAST_DISABLE=1 XT_SCREEN=settings \
  XT_SHOT="$PWD/.work/meu-teste/screen.ppm" XT_SHOT_MS=2600 \
  XT_KEYS_MS=90 XT_KEYS=DDDDDDDDDRRRDDRR client/build/tube360
```

Preparar assets isolados com as quatro fontes e `settings.txt`; `error.txt=network` na pasta de replay
impede tráfego externo. A [matriz automatizada](../tests/hardware/matrix.py) já faz essa preparação,
isola HOME/cache, guarda logs/capturas e verifica todas as transições. Não usar assets de conta real.

| Perfil | Família da política | Encoder descrito | Conector/modo sintéticos iniciais |
| --- | --- | --- | --- |
| xenon | 0 | ANA | componente / 720p |
| zephyr | 1 | HANA | HDMI / 1080p |
| falcon | 2 | HANA | HDMI / 1080p |
| opus | 2 | HANA | componente / 720p; HDMI recusado |
| jasper | 3 | HANA | HDMI / 1080p |
| trinity | 4 | HANA | HDMI / 1080p |
| corona | 5 | KSB | perfil S: HDMI / 1080p, aceita componente/VGA |
| corona-e | 5 | KSB | perfil E: HDMI / 1080p, recusa componente/VGA |
| winchester | 6 | KSB | HDMI / 1080p, recusa componente/VGA |
| unknown | nenhuma | desconhecido | hipótese de bancada HDMI / 720p, não detecção de conector |

São **presets de experimento**, não afirmações de que a placa foi configurada assim pelo seu dono.
Por exemplo, Xenon/Opus não são incapazes de todo framebuffer 1080: aqui começam com dashboard
sintético 720p e não têm HDMI. A pesquisa não demonstrou uma proibição de R8 linear ou de memória
cacheada em todas as Fat; o modo seguro as desliga por falta de qualificação, não por ausência física.
Chips/saídas: [tabela e fontes da fase 1](HARDWARE-COMPATIBILITY.md#1-placas-chips-e-saídas).

O que o app de PC efetivamente consome do perfil:

- Família reportada para a **mesma função de política C** e overrides safe/native;
- modo/ratio sintéticos na seleção real de dimensões da janela/swapchain;
- gate do `DecodePool`: safe desliga mapped; native depende também da capacidade real do Vulkan host;
- cota/falhas no allocator real, uploads de staging e reconstrução real de fontes/pipelines;
- atraso explícito de conclusão/pacing, se pedido, com a GPU e o driver do PC ainda reais.

O host não executa os registradores Xenos. Memória física disponível, proteções de página, caches
PPC, permissões e módulos do kernel não são falsificados como se fossem fatos medidos.

## board-sim.txt: o mesmo contrato no PC e no 360 E

Arquivo ao lado **dos dois XEX** (ou nos assets isolados do PC). Lido uma vez em cada título,
antes da GPU; remover/trocar o arquivo só faz efeito no próximo lançamento. O console ignora XT_BOARD.
O log `video.board` conserva a detecção real, enquanto `board.sim` e `video.policy` identificam
perfil/família efetiva, origem, seleção e `simulated=1`. Nunca grava flags no kernel.
`tested_board` indica a correspondência com a família configurada na build; com `simulated=1` não
é resultado de teste físico e não deve alimentar uma allowlist.

Exemplo para reproduzir o caminho conservador no E, mesmo com o override nativo do dono presente:

```ini
board=jasper
driver=safe
```

Exemplo que isola as cópias de vídeo mantendo a saída nativa no E em dashboard 1080p progressivo:

```ini
board=unknown
driver=native
mode=1080p
cable=hdmi
mapped=off
```

| Campo | Valores / efeito |
| --- | --- |
| `board` | Um perfil da tabela; obrigatório no console. No PC, XT_BOARD pode fornecê-lo; se ambos existem, precisam coincidir. |
| `driver` | `safe` (padrão do arquivo) ou `native` explícito. Sem arquivo, XT_BOARD usa a política auto e video-driver.txt. |
| `mode` | `1080p`, `720p`, `480p` (4:3), `480w` (wide), `480i` (4:3 entrelaçado), `576i` (PAL). Sintético: não reprograma o sinal real. |
| `cable` | `hdmi`, `component`, `vga`, `composite`. Rejeita combinações impossíveis conhecidas: HDMI no Xenon/Opus, componente/VGA no perfil E, composto progressivo, VGA entrelaçado. |
| `mapped` | `auto` ou `off`. Nunca habilita um recurso não anunciado pelo driver real; safe sempre usa staging. |
| `mapped_limit_mib` | 1–160; cota do pool de vídeo, não da RAM inteira nem do heap do kernel. Falha antes de publicar superfície parcial. |
| `delay_ms` | 0–250; espera explícita em abertura do display, output, fontes e novas alocações mapped. Não é frequência medida da placa. |
| `frame_delay_ms` | 0–50; pacing sintético por quadro, armazenado no renderer no init. Sem log/alocação por quadro pela simulação; zero no uso normal. Exercita pressão de apresentação, não execução interna de eDRAM. |
| `fault` | `none`, `display-open`, `output`, `fonts`, `mapped-alloc`; falha uma vez na fronteira indicada. |
| `fault_after` | 0–50 chamadas correspondentes para pular antes da falha; 0 = primeira. Para falhar ao reconstruir fontes, usar 1, pois o boot já carrega um atlas. |

Arquivo limitado a 1024 bytes, com whitespace e comentários em linha própria iniciados por `#`.
NUL, chave desconhecida, repetida, número fora do intervalo, perfil ou combinação inválidos fazem
o arquivo inteiro falhar seguro: driver safe, nenhuma falha/espera injetada. Não há reload por frame.

Modos: saída pedida 480 usa 640×480; contratos SD usam 640×480, 848×480 (wide) ou 640×576 (PAL), em native e safe. 1080 requer contrato 1080p e, no console, autorização do modo real. Safe mantém o framebuffer existente: HD usa 1280×720; SD/entrelaçado conserva o viewport do painel. Alterar a preferência de saída em safe ou SD não recria o display. A integração F32 substitui a rejeição antiga de 480i+safe; a validação física de CRT ainda é necessária.

Pontos de injeção: `output` rejeita antes do teardown; `fonts` antes de destruir o atlas; `mapped-alloc`
antes de criar nova superfície. No Xbox, `display-open` retorna timeout simulado antes de
`xsdk_display_open`, sem instalar IRQ nem tocar scaler/LUT pela injeção. A tela de erro pode executar
depois os acessos normais do SDK. Nenhum failpoint deixa um MMIO intencionalmente preso.

## Matriz e calibração

```bash
cmake --build client/build -j2 --target tube360 board-mapped-test
python3 tests/hardware/policy.py .work/f38/phase2/policy
python3 tests/hardware/matrix.py .work/f38/phase2/matrix
python3 tests/hardware/driver.py .work/f38/phase2/driver-contract
python3 tests/hardware/calibrate.py .work/f38/phase2/calibration
```

`policy.py` compila o código real com o ABI/header xsdk e export de dados simulado: famílias Corona,
Winchester e nenhuma aprovada; overrides, dados ausentes, precedência, parsing inválido e imutabilidade.
XT_BOARD não altera a identificação de um binário `__XSDK__`.

`matrix.py` roda dez perfis × auto, native, SD 4:3 e 480i+safe. Pelo controle: saída
720→1080→480→720 e texto grande on/off. Verifica resolução, política, checkpoints, primeiro frame e
captura. Acrescenta veto ao override, arquivo inválido, atrasos e falhas em display/output/fontes.
Os erros esperados contam como sucesso só quando ocorrem no estágio/retorno esperado.

`board-mapped-test` usa `DecodePool::acquire` real e compara o shader de um quadro YUV sintético:
mapped, safe, mapped off, cota de 1 MiB e falha de alocação. A falha de uma tentativa permite retry;
quota não deixa bytes/slots publicados. As imagens de staging e mapped precisam diferir em no máximo
dois níveis por canal. Essa prova não mede H.264/1080p30 no console.

`driver.py` extrai a função real `tube360_set_output` do V4X preparado e a liga ao parser real;
prova redução de extent, staging separado e que o dashboard real 720p não é elevado para 1080.
A bancada F32 de display/memória/present também passou com os novos hooks, incluindo injeção que
retorna antes de criar buffers/IRQ. Nenhuma comparação de registradores usa `/dev/mem`.
Três mutantes em cópias privadas precisam falhar no cenário certo: perder o nativo da família
configurada, ignorar o veto safe e ultrapassar o limite real de modo do kernel. Falha de setup/compilação
não conta como mutante detectado. Teclas e captura da matriz usam o mesmo contador de frames, para
não encerrar o app enquanto a máquina compartilhada ainda está reconstruindo uma fonte.

## Roteiro do coordenador no E

Gerar um kit novo (recusa sobrescrever diretórios de cenários existentes):

```bash
python3 tests/hardware/console-kit.py .work/f38/phase2/console-kit
```

| Pasta | O que comparar |
| --- | --- |
| 00-native-baseline | Native explícito, sem board-sim; baseline 1080/mapped do dono |
| 01-safe-owner | Família efetiva unknown e safe no mesmo E; prova independente de uma identidade antiga |
| 02-safe-jasper / 03-safe-falcon | Perfil antigo + safe, mantendo detecção física separada |
| 04-native-staging | 1080 nativo, sem mapped: separa custo de cópia de display |
| 05-native-mapped-failure | Primeira alocação mapped rejeitada; verificar staging/retry com vídeo real |
| 06-safe-output-failure | Erro explícito ao trocar Saída, antes de destruir os recursos |
| 07-safe-fonts-failure | Erro explícito na primeira reconstrução após o boot |
| 08-safe-display-failure | Erro de abertura antes do SDK display; não é um travamento real de hardware |
| 09-safe-delay | Espera de 50 ms nas fronteiras + pacing de 25 ms/quadro, ambos sintéticos |

Usar uma pasta descartável e curta, **com o app de produção fechado**; nunca sobrescrever sua pasta,
conta ou settings. Abaixo são comandos para o coordenador executar, não executados nesta frente.
Substituir `$CONSOLE_IP` pelo IP conhecido, sem varredura. O cliente está em
`<forcepush-360>/tools/xbdm.py`.

```bash
XBDM=<forcepush-360>/tools/xbdm.py
KIT="$PWD/.work/f38/phase2/console-kit/00-native-baseline"
python3 "$XBDM" "$CONSOLE_IP" mkdir 'HDD:\Games\Tube360-F38-Native'
for asset in "$KIT"/*; do
  python3 "$XBDM" "$CONSOLE_IP" put "$asset" 'HDD:\Games\Tube360-F38-Native\'"${asset##*/}"
done
python3 "$XBDM" "$CONSOLE_IP" run 'HDD:\Games\Tube360-F38-Native\tube360.xex'
# Depois do ensaio, baixar ANTES de relançar:
python3 "$XBDM" "$CONSOLE_IP" get 'HDD:\Games\Tube360-F38-Native\boot-app.log' .work/f38/phase2/native-boot-app.log
```

Repetir com diretório próprio para cada cenário; testar direto e `default.xex` (launcher). O kit usa
os XEX correspondentes, quatro fontes, configurações descartáveis e override nativo; o board-sim
safe tem precedência. Ausência de board-sim no baseline mantém o caminho anterior. Capturar TV;
o screenshot XBDM não substitui uma foto que prove sinal/scanout real.

Para dizer que **o seguro não quebra o E**, exigir:

1. Baseline native: `simulated=0`, `selected=native`, swapchain 1920×1080 com dashboard 1080p,
   mapped selecionado e alocações efetivas durante vídeo. Confirmar raw flags/família do E.
2. Safe-owner/Jasper/Falcon: flags/model reais iguais ao baseline, `simulated=1`, perfil solicitado,
   `selected=safe`, extent real 1280×720, mapped desligado e nenhum tipo físico cacheado solicitado
   pelo player. TV com imagem estável, Home utilizável, B/Guide/saída normais.
3. Repetir pelo menos 20 alternâncias Saída/texto grande, guardar pares `settings.output` e
   `display.fonts`, medir memória antes/depois se a telemetria do coordenador oferecer isso, e
   verificar ausência de degradação progressiva/retorno ao Aurora.
4. Comparar alguns minutos do **mesmo vídeo e perfil 1080p30**, áudio e presented fps, com native,
   safe e native-staging. O safe acrescenta cópias: passar na UI não prova os 30 fps. Separar
   adaptação de qualidade, stutter e pressão de memória; não promover safe para o uso do dono.
5. Voltar ao baseline sem board-sim e conferir 1080/mapped novamente. Só após isso usar a família
   real na regra auto. Ensaios injetados não aprovam família nem contam como reprodução de um relato Fat.

Os logs de boot já indicam páginas livres no início; esta frente não mede toda a memória do kernel
por fase. O kit não inclui conta, tokens, endpoints privados ou uma rotina que conecte ao console.

## Xenia: resultado e alcance

Foi obtida a [build Linux Canary 02d2cb5](https://github.com/xenia-canary/xenia-canary-releases/releases/tag/02d2cb5)
e executada em Xvfb privado, com HOME/storage isolados, GPU Vulkan e áudio nop. O binário e os XEX
rodados têm hashes nos artefatos `.work/f38/phase2/xenia/`; timeout do processo é imposto pela bancada,
não demonstra deadlock do título. Script: [xenia_probe.py](../tests/hardware/xenia_probe.py).

A primeira execução chegou ao código do título, mas a montagem da pasta XEX era read-only:
`NtCreateFile` retornava `C0000022`, inclusive para o teste de escrita. A opção documentada no
[Emulator::CreateVfsDevice](https://github.com/xenia-canary/xenia-canary/blob/02d2cb5/src/xenia/emulator.cc)
é `--allow_game_relative_writes=true`; foi incluída na repetição, em pasta descartável.
Isso liberou o teste de escrita e o log SDK por `game:`, mas o caminho físico dos novos boot logs
ainda recebeu `C0000022` nessa montagem. A prova usa `stdout.log`/DbgPrint, não afirma durabilidade
dos checkpoints sob Xenia. Também houve chamada não implementada a GetTimeZoneInformation.

Mesmo com escrita liberada, o display do xsdk não é uma boa base para ensaiar placas antigas no
Canary sem implementar suporte adicional no emulador. A leitura/escrita da LUT `0x1928` é reportada
como registrador desconhecido; o autofill não conclui e o SDK retorna `C00000B5`. No fonte, a leitura
de `0x1951` devolve vblank constante e `0x1961` devolve 1280×720 fixo; o contador/scaler não seguem
o sinal de uma Fat. A explicação do timeout da LUT é uma inferência do trace e do fonte, não diagnóstico
de Jasper. [GraphicsSystem do mesmo commit](https://github.com/xenia-canary/xenia-canary/blob/02d2cb5/src/xenia/gpu/graphics_system.cc).

Xenia serve aqui para verificar loader/imports, chegada ao CRT/main e observar incompatibilidades
de API/emulação. Não aprovou Home ou transições de Configurações nesta execução. Corrigir stubs do
emulador pode abrir mais caminhos de software, mas “sempre devolver done” no LUT/latch não reproduz
um controlador físico. A [auditoria xsdk](HARDWARE-COMPATIBILITY.md) também registra instruções que o
scanner sabe que Xenia ignora; não traduzir falha de JIT para defeito da CPU de uma placa.

## O que só um Fat real prova

Nem perfil PC, nem board-sim no E, nem Xenia reproduzem conjuntamente:

- MMIO, PLL/ANA/HANA, contador de linhas, latch, IRQ/vblank e scanout no silício antigo;
- coerência/cache PPC, alias físico, permissões/alinhamento do allocator e fences na memória real;
- init/estado residual de uma combinação específica de bootloader, exploit, payload e plugins;
- estabilidade térmica/elétrica, peças reparadas/substituídas e falha intermitente de GPU/eDRAM;
- performance de decoder, cópia e apresentação sustentada com a banda/latência reais daquela unidade;
- compatibilidade física HDMI/componente/VGA/CRT e retorno do Guide/Aurora naquela placa.

Por isso o resultado permitido é “com esta resposta/limite injetado, o app faz esta escolha e trata
esta falha”. Para afirmar “Jasper trava por causa de X”, falta repetir em Jasper, guardar logs/TV e
isolar a variável com/sem a alteração. A [tabela de ambientes da fase 1](HARDWARE-COMPATIBILITY.md#4-exploit-não-é-o-ambiente-completo-do-título)
continua válida: não simular um exploit pela família da placa, nem inventar RAM livre, XBDM ou patches.
