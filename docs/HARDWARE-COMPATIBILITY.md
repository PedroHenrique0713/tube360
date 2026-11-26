# Placas e ambientes do Xbox 360: auditoria F38

Pesquisa de 2026-11-25, sobre `f38-placas`, herdada de `f32-reddit` (`683dd96`).
Continuação: [bancada de perfis, board-sim, regra do E e avaliação do Xenia](BOARD-SIMULATION.md).
**A carcaça 360 E é a única evidência física disponível; Corona e Winchester ainda não foram confirmadas.**
Os relatos de Jasper/Falcon/ABadAvatar/RGH são os do brief, sem reprodução independente.
Aqui, “suspeito” significa hipótese para um experimento, não causa demonstrada do travamento.

Resultado prático: manter `auto` no caminho `safe` da F32, conservar a lista de placas validadas vazia
e separar placa, saída de vídeo e payload no diagnóstico. Um shrink de silício não torna o Falcon
incompatível com o código PowerPC do app; programação direta do display e estado herdado do payload
merecem investigação antes de presumir falta de desempenho.

## 1. Placas, chips e saídas

Escopo: unidades retail de produção, sem overclock ou transplante de chips. Todas têm **512 MiB de GDDR3
compartilhada**, barramento de 128 bits, clock nominal de 700 MHz DDR (1.400 MT/s; 22,4 GB/s), além de
10 MiB de eDRAM da GPU. A eDRAM não é RAM geral disponível ao FFmpeg. Flash de 16/256/512 MB e eMMC de
4 GB são armazenamento, não memória extra do título. [Memória Free60](https://free60.org/Hardware/Console/Memory/),
[RAM e controlador](https://xenonlibrary.com/wiki/RAM).

“A/V” na tabela é o conector proprietário, com cabos para componente YPbPr, VGA/RGB e vídeo analógico
SD (composto e, conforme cabo/região, S-Video/RGB SCART). A presença de HDMI depende da placa;
um adaptador não cria uma saída nativa inexistente. Os modos/cabos são tratados em
[libxenon, xenos_autoset_mode](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xenos/xenos.c#L510).

| Placa/família | CPU / revisão | GPU e eDRAM | Vídeo | Saídas físicas | Fonte dos chips/conectores |
| --- | --- | --- | --- | --- | --- |
| Xenon | Waternoose DD2/DD3, 90 nm | Y1, 90 nm + eDRAM 90 nm em die separado no pacote GPU | ANA | A/V; sem HDMI | [Xenon](https://xenonlibrary.com/wiki/Xenon_(Motherboard)), [ANA](https://xenonlibrary.com/wiki/ANA) |
| Zephyr | Waternoose DD3, 90 nm | A: Y1; B: Y2; C: Rhea; GPU/eDRAM 90/90 nm, dois dies | HANA | HDMI + A/V | [Zephyr A/B/C](https://xenonlibrary.com/wiki/Zephyr) |
| Falcon | Loki DD1/A01, 65 nm | Rhea, 90/90 nm, dois dies; revisões de encapsulamento ao longo da produção | HANA | HDMI + A/V | [Falcon](https://xenonlibrary.com/wiki/Falcon), [part numbers](https://xenonlibrary.com/wiki/Chip_Part_Numbers) |
| Opus | Loki, 65 nm; família Falcon | Rhea revisada, 90/90 nm; dois dies | **HANA**, apesar de não ter HDMI | A/V customizado para gabinete Xenon; HDMI removido | [Falcon, seção Opus](https://xenonlibrary.com/wiki/Falcon#Opus) |
| Jasper | Loki, 65 nm | Zeus 65 nm + eDRAM 90 nm; algumas de julho/2009 e Tonasket/Jasper v2 usam Kronos 65/65 nm | HANA | HDMI + A/V | [Jasper](https://xenonlibrary.com/wiki/Jasper), [Tonasket](https://xenonlibrary.com/wiki/Tonasket) |
| Trinity (S) | CPU da XCGPU Vejle, 45 nm | CPU+GPU no mesmo die 45 nm; eDRAM **separada**, 65 nm, no mesmo pacote | HANA discreto | HDMI + A/V | [Trinity](https://xenonlibrary.com/wiki/Trinity) |
| Corona (S; família também inclui o E Stingray) | Vejle, 45 nm | CPU+GPU integradas; eDRAM separada 65 nm | Funções HANA incorporadas ao southbridge KSB | S: HDMI + A/V. E/Stingray: HDMI + composto; sem componente/VGA nativos | [Corona](https://xenonlibrary.com/wiki/Corona), [Stingray](https://xenonlibrary.com/wiki/Stingray) |
| Winchester, nome comunitário do E final/Barracuda | XCGPU Oban, 32 nm | CPU+GPU+eDRAM no **mesmo die**, 32 nm | Funções de vídeo no KSB | HDMI + composto; sem componente/VGA nativos | [Barracuda](https://xenonlibrary.com/wiki/Barracuda), [Oban](https://xenonlibrary.com/wiki/Oban) |

Há divergência histórica: o [Free60 Falcon/Opus](https://free60.org/Hardware/Console/Revisions/Falcon/)
descreve eDRAM de 80 nm; a documentação de chips da XenonLibrary identifica Rhea/Styx-90 como 90 nm.
A tabela adota a identificação por chip, explicitando o conflito; **80/90 nm não deve virar condição de
software**. Também há mudança de nomenclatura: [Winchester](https://xenonlibrary.com/wiki/Winchester)
é um projeto anterior cancelado; a placa vendida, conhecida pela comunidade por esse nome, é Barracuda.
Os nomes do log seguem os nomes comunitários/kernel usados por XeUnshackle.

As famílias retail mantêm CPU nominal de 3,2 GHz, três núcleos/seis threads e ISA PowerPC/VMX128;
GPU nominal de 500 MHz. Vejle emula a ligação CPU/GPU para preservar comportamento de latência.
Integração não significa um processador mais rápido para decodificar H.264.
[XCPU](https://xenonlibrary.com/wiki/CPU), [XCGPU](https://xenonlibrary.com/wiki/XCGPU).

### Ventilação e temperatura

Fat usa dissipadores separados e conjunto traseiro de ventoinhas; S/E usa um dissipador para XCGPU.
Há variações de dissipador dentro da mesma placa. Controle SMC é por tensão nas Fat e PWM nas S/E.
Os números abaixo são **alvos de firmware de referência**, não temperaturas medidas nestes relatos,
nem instruções para alterar a ventoinha. [Dissipadores](https://xenonlibrary.com/wiki/Heatsink),
[algoritmo e alvos térmicos](https://xenonlibrary.com/wiki/Thermal_Algorithm).

| Placa | Alvos CPU / GPU / eDRAM, °C |
| --- | --- |
| Xenon comum | 80 / 83 / 85; algumas primeiras tiveram 80 / 97 / 100, configuração abandonada |
| Zephyr | A: 80 / 83 / 85; B/C: 80 / 75 / 78 |
| Falcon / Opus | Falcon: 80 / 75 / 78; a fonte não dá alvo exclusivo para Opus |
| Jasper / Tonasket | 80 / 71 / 73; Tonasket: 80 / 75 / 77 |
| Trinity / Corona / Winchester | 82 / 78 / 76; os dois primeiros sensores representam hotspot/média da XCGPU |

Não há evidência de superaquecimento nos relatos. Travar logo na abertura não permite concluir defeito
térmico. Alterar SMC, clocks ou alvos para “corrigir” Tube360 não faz parte da recomendação.

## 2. Detecção dentro de um título xsdk

### Caminho atual: XboxHardwareInfo

`client/src/video_policy.c` resolve `xboxkrnl.exe` e seu **export de dados 0x156**, copia somente os
quatro bytes de flags e classifica `flags >> 28`. É a mesma máscara `0xf0000000` do
[XeUnshackle/Utils.cpp, GetMoboByHWFlags](https://github.com/Byrom90/XeUnshackle/blob/main/Utils.cpp#L424).
Não é uma função a ser chamada. Em PowerPC big-endian, a cópia para `uint32_t` usa a ordem nativa;
o teste host fornece o valor numérico simulado, não uma captura de bytes da RAM do Xbox.

| Nibble / máscara | Log F32 | Identifica | Não distingue |
| --- | --- | --- | --- |
| 0 / `0x00000000` | Xenon | Família inicial | Elpis/reparo/transplante |
| 1 / `0x10000000` | Zephyr | Família com HDMI inicial | A/B/C, GPU revisada |
| 2 / `0x20000000` | Falcon | Família Falcon | **Opus**; ausência do conector HDMI |
| 3 / `0x30000000` | Jasper | Família Jasper | Zeus/Kronos, Tonasket, NAND 16/256/512 |
| 4 / `0x40000000` | Trinity | Família Trinity | FAB/stepping |
| 5 / `0x50000000` | Corona | Família Corona | S/E-Stingray, revisões, NAND/eMMC |
| 6 / `0x60000000` | Winchester | Família Oban/E final | Revisão física exata |
| 7–15, módulo/export ausente | unknown | Nenhuma identificação confiável | Tudo |

**Veredito F32:** correto para famílias; não é detector exato das oito linhas de placa.
Opus não precisa de um oitavo nibble. Os bits inferiores são preservados no log e não autorizam
`native`. Falha da consulta permanece unknown/safe. Não há comprovação de flags reais do console do dono.
Em um kernel modificado o dado também pode ser alterado; é identificação reportada, não atestado físico.

Não acrescentar `GpuRevision` lendo cegamente o byte +4: há descrições divergentes dessa estrutura.
O [Xenia/xboxkrnl_module.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_module.cc#L136)
modela 16 bytes e escreve **contagem de CPUs** nesse offset, com os demais campos ainda incertos.
Emulação não comprova layout/revisão de silício de uma placa retail.

### SMC, PVR, PCI e GPU: corroborar, sem transplantar o boot do libxenon

SMC aceita mensagens de 16 bytes; `0x12` consulta a versão. Exemplo documentado de resposta:
`12 41 02 03 00 00…`. Em um probe separado, resolver dinamicamente `HalSendSMCMessage` (kernel
`0x29`), enviar comando `0x12` com os outros bytes zerados, guardar a resposta bruta e validar o
eco do comando. O header xsdk auditado não expõe esse wrapper; seria necessária declaração/ABI
verificada. Uma chamada síncrona que não retorna não ganha timeout só porque a chamadora mede tempo.
**Não foi acrescentada consulta SMC ao boot normal.**
[Protocolo SMC](https://free60.org/Hardware/Console/SMC/),
[export kernel no Xenia](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_table.inc).

| Família | SMC retail de referência: hardware / firmware | PVR: faixa usada pela heurística do libxenon | Outra condição dessa heurística |
| --- | --- | --- | --- |
| Xenon | 1.2 / 1.50 ou 1.51; Elpis 1.3 / 1.01–1.02 | `<= 0x710300` | DVE `< 0x11` |
| Zephyr | 2.1 / 1.09 (A), 1.13 (B/C) | `<= 0x710300` | DVE `>= 0x11` |
| Falcon/Opus | Falcon 3.1 / 1.06; Opus sem entrada exclusiva na matriz | `0x710300 < PVR <= 0x710500` | bridge revision `< 0x60` |
| Jasper/Tonasket | 4.1 / 2.03 | Mesma faixa Loki | bridge revision `>= 0x60` |
| Trinity | 5.1 / 3.01 | `0x710500 < PVR <= 0x710800` | DVE `< 0x20` |
| Corona/Stingray | 6.2 / 2.05 | Mesma faixa Vejle | DVE `>= 0x20`; Phison distingue armazenamento |
| Winchester/Barracuda | 7.1 / 1.03 | `0x710800 < PVR <= 0x710a00` | Phison distingue variante eMMC |

Valores SMC: [Software Matrix](https://xenonlibrary.com/wiki/Software_Matrix).
Faixas PVR e condições: [libxenon/xb360.c, xenon_get_console_type](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xb360/xb360.c#L667).
São faixas de classificação do código, **não valores exatos prometidos para cada console**.
SMC hack/firmware de reposição e reparos limitam essa inferência.

`mfpvr` lê a revisão de CPU; sozinho não separa Falcon/Jasper ou Trinity/Corona. O libxenon lê PCI bridge
em `0xd0000008`, GPU ID em `0xd0010000` (metade superior), e DVE via ANA `0xfe`.
Seu comentário associa IDs `0x5811` a Xenon e `0x5821` a Zephyr/Falcon **com interrogações**,
`0x5831` a Jasper e `0x5841` a slim. Não encontrei mapa primário completo de GPU-ID/stepping por placa,
principalmente Winchester; não promover esses comentários a regra de produção.
[Getters](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xb360/xb360.c#L633),
[comentário de IDs](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xenos/xenos.c#L21).

Os enums **não são intercambiáveis**: libxenon usa Corona=5, Corona_Phison=6, Winchester=7,
Winchester_MMC=8; o nibble de XboxHardwareInfo usa Winchester=6. Reutilizar um enum no outro
classificaria Corona 4 GB como Winchester.
[Enum REV_*](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xb360/xb360.h#L92).

## 3. O que o driver e o app fazem com esse hardware

Dependências efetivamente lidas, sem alterar os checkouts compartilhados:

- xsdk `8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28`, em `forcepush-360/vendor/xsdk`;
- vulkan4xenon `df25dc50e6296d89822626b433f4080db05bccc8`, em `forcepush-360/vendor/vulkan4xenon`;
- libxenon `1334a0ff48c467974b84cdf5927352196cc33eab`, cópia de pesquisa em `.work/f38/sources/libxenon`.

Só `XSDK_DIR` e `V4X_DIR` foram extraídos do arquivo de ambiente privado. A build usa outro arquivo
de ambiente, local à F38, com caminhos conhecidos de toolchain/SDL. Ordem auditada:
`prepare-native-display.py` → `prepare-video-memory.py` → `prepare-boot-diagnostics.py` →
`prepare-video-policy.py`. Ler só o primeiro patch dá uma imagem errada do comportamento final.

### Superfícies e modos

`prepare-native-display.py` acrescenta enums SD/1080 sem renumerar o ABI usado pelo SDL, guarda
largura/altura, ajusta o scaler e sincroniza o extent divulgado pelo V4X. `tube360_set_output` só admite
1080 nativo se o dashboard já informa **1920×1080 progressivo**; não programa um novo sinal HDMI.
SD escolhe 640×480 ou 848×480; o patch permite altura 480 com saída entrelaçada, caso antes delegado
ao viewport do kernel. Código local: [patch](../tools/prepare-native-display.py),
[política final](../tools/prepare-video-policy.py).

O README recomenda HDMI para 1080 nativo; o gate no código consulta dimensões/interlace, sem consultar
o conector. Isso não qualifica 1080 por componente: preservar a recomendação de HDMI até testar esse caso.

O xsdk original já usa LUT, scaler, surface MMIO e contador de linhas; `safe` preserva esses acessos.
Offsets são índices de dword: LUT `0x1930/0x1921/22/27/28`, viewport `0x1961`, lock scaler `0x1973`,
surface `0x1844/48/4d/4e`, latch `0x183b`, contador `0x194c`. Multiplicar por quatro ao comparar com
offsets em bytes do libxenon. O poll de latch/LUT pede 100 ms; o contador espera até 50.000 µs e pode
retornar sem atingir a altura. Não prova vblank correto em toda revisão.
[xsdk/display.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/display.c).

Na saída entrelaçada, o original pode retornar 640×480 enquanto V4X pede 1280×720: `safe` tem essa
limitação herdada. Não ativar `native` globalmente para contorná-la; qualificar SD separadamente.
A tela de emergência em `startup.hpp` também usa xsdk GPU/present, com fonte embutida, e pode travar
se GPU/display estiverem presos. Ela não é um renderer CPU independente no Xbox.

### Memória, cache e GPU

O [patch de memória](../tools/prepare-video-memory.py) permite imagem R8 linear amostrada e adiciona
tipo de memória 2: física, visível e cacheada pela CPU, **não coerente**. Aloca via kernel, usa endereço
GPU traduzido pelo SDK e publica planos FFmpeg com `dcbst` de 128 em 128 bytes seguido de `sync`.
Invalidate usa `dcbf`, não o `dcbi` da v1. `DecodePool` verifica pitch/alinhamento, retém superfícies
por referência/fence e limita a 48 superfícies/160 MiB; falha de capacidade/layout/alocação volta ao
staging. `safe` anuncia só dois tipos, sem R8 linear sampled e sem as novas operações de cache.
[Pool e publish](../client/src/gfx_video_memory.hpp), [gates](../tools/prepare-video-policy.py),
[memória original V4X](https://github.com/xenosworks/vulkan4xenon/blob/df25dc50e6296d89822626b433f4080db05bccc8/src/memory.c).

O alinhamento solicitado pelo tipo 2 é 4 KiB, com tamanho arredondado a 4 KiB, enquanto
`MEM_LARGE_PAGES` no SDK pede páginas de 64 KiB. O allocator original do SDK arredonda/alinha a
64 KiB. É diferença concreta para investigar com retornos/tamanhos do kernel, não prova de bug
exclusivo das Fat; a bancada host não reproduz o alocador real.
[Constante no kernel.h](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/include/xsdk/kernel.h),
[xsdk/gpu.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/gpu.c).

V4X é uma implementação Vulkan sobre o command processor Xenos, com render targets na eDRAM,
resolve para RAM e tiling quando necessário; não há um “driver Vulkan da Microsoft” instalado pelo
exploit. A GPU não decodifica H.264 no Tube360: shaders fazem composição/amostragem dos planos de
vídeo produzidos pelo FFmpeg. [render_pass.c](https://github.com/xenosworks/vulkan4xenon/blob/df25dc50e6296d89822626b433f4080db05bccc8/src/render_pass.c),
[gpu_tiles.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/gpu_tiles.c),
[player](../client/src/player_h264.hpp), [shader](../client/shaders/video.frag).

Contas sem alinhamento: um scanout RGBA 720p ocupa 3,52 MiB; 1080p, 7,91 MiB; três buffers,
10,55 contra 23,73 MiB. São RAM geral, não três buffers residentes simultaneamente na eDRAM.
Cor+depth 32 bits em 1080p excedem 10 MiB e requerem tiling; integração da eDRAM em Oban não aumenta
sua capacidade. Atlas R8 é 4 MiB (2048²) ou 8 MiB (2048×4096), além do staging temporário.
Dimensões: [gfx.hpp](../client/src/gfx.hpp), [present.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/present.c).

O V4X declara heap de 256 MiB como **orçamento**, não mede memória livre. `timestampPeriod` usa
498,75 MHz medidos, não clock detectado por placa. Isso pode afetar métricas/adaptação caso a
frequência real difira. O xsdk também documenta timebase medida de 49,875 MHz. Não encontrei garantia
por revisão que justifique trocar constantes por 500/50 MHz ou tocar PLLs.
[physical_device.c](https://github.com/xenosworks/vulkan4xenon/blob/df25dc50e6296d89822626b433f4080db05bccc8/src/physical_device.c),
[medições do SDK](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/docs/HARDWARE.md).

### Diferenças reais de inicialização no libxenon

O libxenon inicia hardware a partir de XeLL, sem depender do kernel retail manter o display pronto.
Em `xenos_init_phase1`, GPU ID `>=0x5841` muda sequência de PLL/registradores `0x218/244/204/208`;
Fat mais antiga também muda `0x204` conforme ID `<0x5821`. Corona/Winchester usam tabelas próprias
de modo e programação ANA/PLL. Isso demonstra diferenças reais, mas não que Tube360 tenha de
repetir essa inicialização depois de `VdInitializeEngines`.
[xenos.c](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xenos/xenos.c#L221).

`edram.c` seleciona calibração conforme GPU ID e `edram_rev`, muda `0x214` antes/depois de Jasper,
e valores de `edram_write(0x4001)` para IDs anteriores a `0x5821`; chips integrados seguem outro
caminho. Copiar essa calibração destrutiva para o app seria um experimento de alto risco.
[edram.c](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xenos/edram.c#L590).

### Configurações e endereços fixos

Trocar saída em `main.cpp` chama `gfx::output`: espera GPU idle, destrói pipelines/framebuffers/views/
render pass/swapchain, recria surface no Xbox e recarrega fontes. Mesmo em `safe`, pedir uma altura
que resulta no mesmo 720p ainda percorre essa reconstrução. Texto grande só refaz layout/atlas;
não muda sinal nem framebuffer. O 4:3 decorre do extent/saída SD, sem toggle independente de
hardware nessa região. [Ligação](../client/src/main.cpp), [output/loadFonts](../client/src/gfx.hpp),
[layout](../client/src/display_layout.hpp).

F38 acrescenta `begin/end settings.output` e `begin/end/error display.fonts`, com dimensões e estado
de texto grande. A gravação ocorre apenas na abertura/reconfiguração. Não modifica registradores,
política, idiomas ou comportamento visual. Uma espera após o primeiro frame ainda pode ser infinita:
os timeouts de diagnóstico do primeiro frame não cobrem `vkDeviceWaitIdle` da reconfiguração.

| Constante/endereço | Natureza e tratamento |
| --- | --- |
| `0x7fc80000` + índice×4 | MMIO GPU do título no xsdk; comum aos dois caminhos, requer ambiente válido. [gpu_private.h](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/gpu_private.h) |
| `0x82000000 + 0x1000` | Base virtual do título/linker, não endereço de framebuffer físico. [xsdk.ld](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/xsdk.ld) |
| `0x20000000` em coherency | Tamanho do espaço retail de 512 MiB, não ponteiro de pool. [gpu_setup.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/gpu_setup.c) |
| Pool/ring/scanout | Endereços retornados por alocação e `MmGetPhysicalAddress`; nenhum framebuffer físico escolhido por placa no app. [gpu.c](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/src/gpu.c) |
| PCR `0x70/0x74`, contexto de exceção e ordinal `0x36d` | ABI fixo do handler experimental 17559, desativado sem opt-in. Não confundir com GPU. [player-fault.c](../client/x360/player-fault.c) |
| `0x1e000000`, `0xec800000`, PCI `0xd0010000` do libxenon | Contexto bare-metal de referência; não copiar esses endereços para um título xsdk. [xenos.c](https://github.com/Free60Project/libxenon/blob/1334a0ff48c467974b84cdf5927352196cc33eab/libxenon/drivers/xenos/xenos.c#L19) |

### Segurança por família e caminho

Nenhuma linha abaixo equivale a qualificação física. “Preferir safe” só retira as extensões Tube360.

| Operação | Xenon/Zephyr | Falcon/Opus/Jasper | Trinity/Corona/Winchester | Motivo / ação |
| --- | --- | --- | --- | --- |
| xsdk/V4X original, 720p progressivo | Não qualificado; variações ANA/HANA | Não qualificado; Jasper tem relato de abertura travada | Baseline E de revisão desconhecida | Preferir `safe`; localizar `gpu.*` versus `display.*` |
| Scaler/framebuffer SD/1080 Tube360 | Suspeito | Suspeito; Falcon tem relato de reconfiguração | Só E foi exercitado antes, sem provar família | `native` explícito; 1080 exige dashboard progressivo; SD é ensaio separado |
| Contador/IRQ/LUT | Ainda ativos no safe | Ainda ativos no safe | Ainda ativos no safe | Herdados do SDK; não remover waits ou mexer em clocks sem captura |
| R8 linear + física cacheada | Não qualificado | Não qualificado | Medido no E, sem comprovação geral | `safe` usa staging; separar display de mapped em experimento posterior |
| Recriar swapchain/atlas | Suspeito em todas | Prioridade no Falcon | Pode reproduzir no E para baseline | Novos checkpoints; futura troca transacional e memória antes/depois |
| ISA CPU/linhas de cache | Sem diferença demonstrada que exija outro binário | Idem | Idem | Não inferir compatibilidade apenas do processo de fabricação |

## 4. Exploit não é o ambiente completo do título

Os cinco caminhos abaixo não mudam a quantidade física de RAM retail. **Não há um número universal de
“memória livre por exploit”**: kernel/XAM, payload, módulos residentes e plugins consomem/reservam páginas.
Medir `MmQueryStatistics` em cada abertura, antes e depois de GPU/atlas/player em uma próxima frente.
O próprio SDK expõe a estatística; a F32 já registra total e páginas disponíveis do título.
[kernel.h](https://github.com/xenosworks/xsdk/blob/8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28/include/xsdk/kernel.h),
[boot_platform.cpp](../client/src/boot_platform.cpp).

| Entrada | Estado documentado / o que pode estar carregado | Consequência para Tube360 |
| --- | --- | --- |
| BadUpdate | Exploit de hipervisor, temporário em RAM, dirigido a 17559; lança um XEX retail sem as restrições indicadas. O próprio autor distingue isso de uma solução completa de homebrew. Versões do exploit mudaram; não generalizar tempos/taxa de sucesso antigos. | Registrar versão e payload. Execução de um XEX não prova rede liberada, exports de debug ou conjunto completo de patches. [BadUpdate](https://github.com/grimdoomer/Xbox360BadUpdate), [releases](https://github.com/grimdoomer/Xbox360BadUpdate/releases) |
| BadUpdate + XeUnshackle | Acrescenta patches kernel/HV equivalentes ao conjunto xeBuild, carrega Dashlaunch `launch.xex`, pode carregar plugins ao sair para outro título e reverte patches temporários dispensáveis do exploit. | Esse é o baseline declarado do dono; módulos/plugins ainda variam. XBDM só existe se carregado; o app não depende dele. [XeUnshackle](https://github.com/Byrom90/XeUnshackle) |
| ABadAvatar | Entrada pelo dashboard/avatar; o README pede um payload separado, como XeUnshackle ou FreeMyXe. Documenta endereço/descarga específicos do estágio do exploit, sem fornecer um ambiente completo de título. | Não tratar “ABadAvatar” como variante de rede do app. Identificar payload/versão; separar falha Home/TLS de falha GPU. Não usar seus endereços de estágio no Tube360. [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) |
| PeerPressure | Persistência por alterações de NAND/HDD e ataques DMA no boot. Possui patches próprios: páginas executáveis, verificações XEX removidas, funções HV alteradas e remoção da checagem de privilégio XEX para sockets inseguros. Autor não garante paridade perfeita com freeboot. Não suporta Dashlaunch/plugins Dashlaunch; tem mecanismos próprios. | É outro conjunto de patches, mesmo em 17559. Jasper/Falcon são elegíveis; instalação documentada exclui Corona/Winchester. Não atribuir esse ambiente ao E do dono. [Projeto](https://github.com/grimdoomer/Xbox360PeerPressure), [features](https://github.com/grimdoomer/Xbox360PeerPressure/wiki/Softmod-Features), [compatibilidade de instalação](https://github.com/grimdoomer/Xbox360PeerPressure/wiki/Installation) |
| RGH / JTAG | Ataques distintos ao boot que permitem iniciar código/imagens modificados; uso típico: kernel/HV xeBuild/freeboot e Dashlaunch/plugins opcionais. RGH não é um driver de GPU e o método de boot não especifica a versão da imagem instalada. JTAG depende de bootloaders antigos vulneráveis; as fontes históricas não são matriz atual completa de RGH. | Anotar imagem/kernel, Dashlaunch e plugins. Retorno ao Aurora pode ocorrer por erro/terminação do título; não demonstra defeito Trinity nem OOM. [RGH Free60](https://free60.org/Hacks/Reset_Glitch_Hack/), [JTAG Free60](https://free60.org/Hacks/SMC_Hack/), [conjunto xeBuild descrito pelo XeUnshackle](https://github.com/Byrom90/XeUnshackle) |

Para APIs usadas pelo app: GPU/MMIO, memória física, threads, kernel/XAM/NetDll e carregamento de
XEX continuam necessários. A F32 tornou DbgPrint/fault handler opcionais; XBDM não é importado como
requisito. A consulta opcional de um export em telemetria não remove imports estáticos usados por
outras partes do SDK. TLS continua verificando certificados em todos os ambientes.
[Auditoria de abertura/imports](STARTUP-DIAGNOSTICS.md), [boot_link.py](../tests/reddit/boot_link.py).

## 5. Ações por risco e roteiro de qualificação

1. **Baixo risco, já disponível:** por padrão de build manter `auto → safe`, nenhuma família aprovada automaticamente,
   handler experimental desligado e logs duráveis. F38 cobre agora reconfiguração de saída e fontes.
   Conferir `model` como família, o `requested/selected` e a resolução real; não recomendar “native
   para todos os slim”.
   Na fase 2, o uso do dono permanece com override `native`; após confirmar e medir a família real,
   `TUBE360_NATIVE_BOARD` permite aprovar somente ela para auto. A simulação não fornece essa aprovação.
2. **Baixo risco, próxima frente:** tornar explícitos no diagnóstico os aliases Falcon/Opus,
   Jasper/Tonasket e Corona/Stingray; acrescentar memória disponível nas transições e retorno dos
   allocators, sem log por frame. Medir tempo de boot com/sem flush em USB antes de ampliar telemetria.
3. **Risco moderado:** evitar destruir swapchain/atlas quando o extent/layout efetivo não muda;
   tornar reconstrução transacional e com erro recuperável. Hoje falhar após destruir o anterior deixa
   poucos recursos para uma tela de erro. Qualificar alternância de saída, texto grande, Guide e retorno.
4. **Risco moderado:** separar opt-ins de display e mapped video. Atualmente `native` combina os dois;
   para uma comparação já possível, `native` com `decode-config.txt = 0 4 1 -1` desliga mapped no player.
   Essa configuração não elimina as capacidades anunciadas pelo driver nem o display nativo.
5. **Risco moderado:** validar alocação física cacheada a 64 KiB e coerência/fences em Fat e slim,
   sob pouca memória e diferentes tamanhos de vídeo. Corrigir a granularidade só com contrato/retorno
   do kernel e prova física; não trocar flags de proteção com base no shrink.
6. **Risco alto, só após logs:** backend de apresentação via contrato kernel `VdSwap`/viewport herdado,
   se falha demonstrada nos acessos diretos. Avaliar calibração do timebase/timestamp sem reprogramar PLL.
   Não importar sequências de inicialização/calibração eDRAM/ANA do libxenon para o título retail.

| Relato do brief | Primeira comparação e dado decisivo |
| --- | --- |
| Jasper, FernJayCee, flash branco/trava, HDD e USB | Pasta curta, `safe`, dashboard 720p progressivo; logs direto e launcher. Se para em GPU antes de display, não culpar o patch 1080p. Se para em LUT/surface/IRQ, comparar exatamente a etapa; HDD/USB já falharam e não estabelecem causa. |
| Falcon, Kumantej_, falhas frequentes | `safe`; alternar saída/texto grande e guardar novos `settings.output`/`display.fonts`. Comparar sessão sem tocar Configurações. Parado em begin de output indica teardown/recriação; fonts sem end pode ser idle/alocação/pack/upload. |
| ABadAvatar, Helix1545, Home não carrega | Payload exato e versões; se primeiro frame terminou, procurar DNS/TLS/clock/HTTP/Home. Não trocar driver para corrigir HTTP. Comparar o mesmo payload com outra entrada somente se disponível ao relator. |
| RGH slim, Flat_Appointment3973, volta ao Aurora | Identificar família real e kernel/payload/plugins; direto versus launcher, primeiro log criado e último begin. Sem entrada CRT, auditar XEX/imports/loader; não assumir Trinity por “slim”. |
| E do dono | Baseline `auto/safe` e `native` separados, logs de flags/mode/AV pack, foto da TV; só então aprovar a família exata e, separadamente, as operações testadas. |

Nenhum console foi acessado nesta frente. Para o coordenador, usar cópias numa pasta descartável,
por exemplo `Usb0:\Apps\Tube360-F38\`, com dois XEX correspondentes e as quatro fontes, sem arquivos
de conta, servidor, debug ou net_log. Conteúdo inicial de `video-driver.txt`: `safe`.

Sequência manual exata: dashboard em 720p progressivo → abrir `tube360.xex` direto → fechar normalmente
→ abrir `default.xex`/launcher correspondente → Configurações → linha Saída: 720→1080→480→720
→ Texto grande: ligar/desligar → B → sair. Em `safe`, **verificar que a resolução real permanece no
caminho original**, apesar dos pedidos. Guardar logs/TV a cada tentativa. Para ensaiar 1080 nativo,
selecionar antes 1080p no dashboard, reiniciar o título com `native`, repetir e coletar tudo separado.
Componente/VGA só onde o conector existir; SD entrelaçado é outro caso, não pré-requisito da primeira prova.

Pedir ao relator: versão/build/arquivo lançado, carcaça **e** família reportada/flags, kernel,
exploit **e** payload/versões, plugins residentes, HDD/USB e caminho, cabo/TV, resolução/Hz/interlace do
dashboard, `video-driver.txt`, configuração de saída/texto e ação imediatamente anterior à falha.
Coletar `boot-crt.log`, `boot-early.log`, `boot-app.log`, `boot-launcher.log`, `startup.log`,
`launcher.log` e log do título (ou `bc/be/ba/bl.log`). Ler todos os begins abertos, não só a última linha.
Coletar antes de relançar: CRT é sobrescrito. Não pedir cookies, OAuth, pairing, CPU/DVD keys ou NAND.
AV pack deve vir **bruto**, com descrição/foto do cabo: rótulos/flags não comprovam o conector físico
nem a placa. [Semântica e limites dos logs](STARTUP-DIAGNOSTICS.md).

## 6. Provas reproduzíveis no host

```bash
python3 tests/hardware/settings.py .work/f38/settings
python3 tests/reddit/boot_log_test.py .work/f38/boot
python3 tests/reddit/video_policy.py .work/f38/policy
python3 tests/reddit/video_driver.py .work/f38/driver \
  --source-sdk <forcepush-360>/vendor/xsdk \
  --source-v4x <forcepush-360>/vendor/vulkan4xenon
python3 tests/reddit/boot_native.py .work/f38/native
python3 tests/reddit/boot_link.py .work/f38/link \
  --toolchain ~/xsdk-gcc-52/bin/powerpc-xsdk-elf-
```

As provas settings/boot executam o app Vulkan do PC com replay local e HOME isolado. Driver/policy/native
executam código real com fronteiras de hardware simuladas; não emulam coerência física, scanout,
temperatura, exploit ou o alocador do kernel real. Build/link Xbox prova toolchain/imports, não execução.
Resultados, prints, hashes e comandos da build ficam no relatório `.work/f38/RELATORIO.md` desta frente.
