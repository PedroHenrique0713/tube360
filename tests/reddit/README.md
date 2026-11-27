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

## Adendo 6 — qualidade manual e Corona 480i/componente

```bash
cmake --build client/build -j2 --target tube360 tube360-quality-fixture manual-quality-test display-layout-test
client/build/manual-quality-test .work/f32/item14/contracts
client/build/display-layout-test bench
python3 tests/reddit/manual_quality.py .work/f32/item14/manual .work/f32/media sources
python3 tests/reddit/manual_quality.py .work/f32/item14/manual .work/f32/media ui
python3 tests/reddit/manual_quality.py .work/f32/item14/manual .work/f32/media warnings
python3 tests/reddit/manual_quality.py .work/f32/item14/manual .work/f32/media automatic
python3 tests/reddit/board_sim.py .work/f32/item14/board
python3 tests/reddit/video_driver.py .work/f32/item14/driver \
  --source-sdk "$F32_ORIGINAL_XSDK" --source-v4x "$F32_ORIGINAL_V4X"
```

Os grupos `sources/ui/warnings/automatic` usam a mesma mídia sintética preparada
pelo primeiro grupo; sem argumentos de grupo o runner executa os quatro. São 24
capturas do app, controle, decoder FFmpeg e Vulkan reais: fonte exata nas quatro
resoluções; Configurações/menu pt/en/es em HD e SD; menu esparso contendo somente
Automático/480/144 (240 em VP9 fica fora); troca 480→144 preservando pausa/posição;
aviso de pressão e erro de qualidade ausente nas três línguas; falha da fonte
sem troca/reserva; volta a Automático que reativa 1080→720. Os três perfis antigos
continuam adaptativos; os quatro novos são fixos. O teste C++ cobre parser real,
persistência dos sete IDs, AVC sem AAC e HLS fixo, consumindo a variante escolhida.
HLS não tem prova completa de playback nesta rodada. Nenhum pedido ao YouTube.

`board_sim.py` cobre dez casos do parser e o app real 640×480, inclusive três
mudanças de saída ignoradas, com `board=corona`, `mode=480i`, `cable=component`
em **native e safe**. `video_driver.py` acrescenta quatro casos do pipeline real
preparado SDK/Vulkan: cada driver com sinal informado HD e com sinal 480i, mantendo
viewport real 640×480. Exige surface=swapchain=present, dois buffers, primeiro show
antes da única IRQ e zero escritas do nosso scaler. Dois mutantes removem o gancho
SD de board-sim e precisam falhar no setter, comprovando que a fixture não mascara
a regressão. A leitura do viewport é real dentro dessa fronteira; kernel/MMIO/
alocação/textura são simulados. Não representa prova física do sinal entrelaçado.

### Console: manual e Corona Slim

Só o coordenador executa, em pasta temporária com os dois XEX e quatro fontes.
Arquivar logs/prints antes de abrir novamente. Não usar conta ou Nest para aprovar
estes casos. Usar som TV e vídeos públicos que ofereçam os formatos necessários.

1. Sem vídeo, Configurações → Qualidade: verificar os sete valores; salvar 480p,
   fechar/reabrir e conferir `video_mode=480p`. Durante um vídeo, Configurações
   deve percorrer somente as escolhas AVC/AAC oferecidas naquele vídeo.
2. No player, Y ou ícone Qualidade abre o menu com setas/A/B no estilo Series.
   Conferir só as resoluções disponíveis. Testar 480/360/240/144 nos vídeos que
   as tiverem; repetir pt/en/es, 4:3/texto grande e HD. B fecha sem alterar.
3. Pausar, anotar posição, escolher outra qualidade: manter pausa e posição.
   O log deve registrar `selecao 480p fixa=1 fonte=480p itag=...` (etc.). Sob
   pressão sustentada, exigir aviso localizado e nenhuma troca de itag/fonte ou
   reserva do servidor. Não prometer desempenho por altura: medir no console.
4. Abrir outro vídeo sem a qualidade salva: erro localizado, Y abre escolhas;
   selecionar disponível ou Automático. Se a fonte falhar, mostrar erro em vez
   de escolher outra. Automático volta a `fixa=0`, com a escada dos itens 9/13:
   assentamento, um degrau por vez, histórico de falhas e projeção antes de subir.
   Repetir `4p_9OPKgcDE` por três minutos, sem a volta repetitiva ao 1080 incapaz.
5. Em duas pastas separadas, usar o arquivo abaixo, uma com `native`, outra `safe`:

   ```ini
   board=corona
   driver=native
   mode=480i
   cable=component
   ```

   Conferir `board.sim.file`, `board.sim ... simulated=1` e o modelo real separado.
   Abrir pelo lançador e diretamente. Exigir `display.signal ... kernel=1`,
   `display.kernel-viewport`, dimensões iguais no surface/swapchain/present e
   `end display.first-show`, `end frame.present`, `end app.first-frame`. Pedido
   `output_height=1080` não deve recriar a saída SD; navegar e reproduzir VOD.
6. **Prova física do Tupapa2712:** Slim Corona/RGH com componente e painel/CRT já
   funcionando em 480i, repetir ambos os drivers (arquivo acima) e depois remover
   board-sim para validar detecção normal Corona/native. Simular no 360 E por HDMI
   não aprova componente/480i. No 360 E, usar o AV composto adequado e o roteiro
   CRT do item 12 para prova física distinta. Não alterar NAND/SMC/região ou forçar
   timings. Arquivar boot-early/app/launcher/CRT, log normal/UDP, fotos e conector.

O parser desta branch aceita só os quatro campos acima; extensões de faults/
mapped/delay da F38 falham de forma conservadora. Ao integrar, conservar o fix
de viewport/SD dos dois caminhos e a política Corona/Winchester native padrão.

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
F32_XBDM=<forcepush-360>/tools/xbdm.py
F32_IP=192.168.1.50
F32_REMOTE='HDD:\Games\Tube360 F32'
mkdir -p .work/f32/console
python3 "$F32_XBDM" "$F32_IP" mkdir "$F32_REMOTE"
python3 "$F32_XBDM" "$F32_IP" put client/build-x360/tube360-launcher.xex "$F32_REMOTE\\default.xex"
python3 "$F32_XBDM" "$F32_IP" put client/build-x360/tube360.xex "$F32_REMOTE\\tube360.xex"
printf 'lang=pt\nsound=tv\nvideo_mode=1080p30\n' > .work/f32/console/settings.txt
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

## Adendo F32/5 — Home sem servidor

`home_errors.py` executa o app real e gera 30 PNGs. Os 18 casos principais não têm
`server=` nem `server.txt`. Em cada língua há ainda `server.txt` com `server=auto`,
`server=off` com `server.txt` e 640×480 com texto grande: um servidor legado
configurado precisa ser ignorado em silêncio, com a mesma mensagem e zero pedidos ao
endereço. TLS por data exibe relógio UTC; TLS geral, DNS, rede, recusa do YouTube e
resposta inválida mantêm seus diagnósticos em pt/en/es, sem texto sobre servidor nem
`0.0.0.0`.

A porta de teste responde HTTP 503 em loopback efêmero; não toca nenhum servidor real.
O log `captura erro Home` registra o título, a ajuda e o detalhe usados pelo próprio
bloco de desenho naquela captura; não injeta mensagem na interface. Essa
instrumentação existe apenas no PC. `results.json` contém os 30 resultados e caminhos
absolutos; `proofs/home-errors-montage.png` mostra as seis causas nas três línguas e,
na última linha, o servidor legado configurado.

Complemento do roteiro físico para o coordenador: na pasta temporária F32, iniciar
sem rede e conferir a mensagem de rede, sem endereço `0.0.0.0` nem instrução de
servidor. Repetir com um `server.txt` e uma linha `server=auto` antigos na pasta: a
tela e o `settings.txt` regravado têm de ficar iguais, sem a linha `server=`. Conferir
pt/en/es. Coletar logs/prints e limpar a pasta conforme o roteiro anterior. Não foi
executado no console pelo autor.
