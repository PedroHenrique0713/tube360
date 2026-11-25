# F32 — erros de início/conexão, recuperação de qualidade e gatilhos

Execute da raiz do worktree. As provas usam pastas próprias, respostas gravadas,
certificados descartáveis em loopback e mídia sintética. Nenhuma conta, Nest,
cache de produção ou requisição ao YouTube é necessária.

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360 tube360-quality-fixture reddit-contract-test reddit-tls-probe player-state-test h264-cadence-test
client/build/reddit-contract-test
python3 tests/reddit/tls.py client/build/reddit-tls-probe
python3 tests/reddit/ui.py .work/f32/ui
```

Prepare a mídia e execute o player:

```bash
python3 tests/reddit/prepare.py .work/f32/media
python3 tests/reddit/playback.py .work/f32
JOBS=3 tools/build-standalone.sh pc
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
`recovery` dura 55 s: a fixture atrasa apenas as ranges de 1080 por 2 s nos primeiros
10 s e depois libera a rede. Exige 1080 → 720 → 1080 no player real, sem reiniciar
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
