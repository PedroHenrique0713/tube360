# Abertura MP4 acima de 2 GiB (F46)

## Causa e correção

A duração/HDR não era o discriminante. Os dois AVC selecionados pelo VISIONOS eram recursos de
3.723.217.268 bytes (BBC, itag 137/25 fps) e 2.357.786.511 bytes (Nature, itag 298/60 fps).
Os `sidx` terminavam exatamente no tamanho HTTP: 2.127 e 1.947 referências, respectivamente.
No desktop ambos abriam normalmente, inclusive na v0.3.1.

O GCC xsdk milestone 52 combina a cópia de um resultado `int64_t` e seu teste de sinal em `mr.`.
Em MSR.SF=0, essa instrução atualiza CR0 pelo sinal dos **32 bits baixos**. Em `avio_size`, um
tamanho entre 2 e 4 GiB era tratado como negativo; o fallback `SEEK_END(-1)` também parecia negativo,
então retornava o tamanho menos um **sem restaurar o cursor**. O `sidx` deixava de ser completo e o
MOV varria fragmentos. O mesmo padrão em `avio_seek` recusava offsets válidos acima de 2 GiB.
Isso explica a associação com vídeos longos, o EOF/cabeçalho inválido e a queda para uma fonte menor.

`tools/build-ffmpeg-x360.sh` desativa apenas o passe RTL `combine` em `libavformat/*.o` nos builds
Xbox/scalar. Os codecs, kernels VMX e opções do build PC permanecem com a receita existente.
Uma marca de migração invalida os objetos avformat de builds incrementais antigos; a identidade da
receita invalida o prefixo instalado. Não se ignora o índice nem se baixa o recurso inteiro.

O player registra `cabecalho: video/audio`, tamanho, bytes, pedidos, `header_ms`, `seek_ms` e resultado.
O read-ahead de áudio também só começa depois do seek inicial. Uma falha de cabeçalho continua podendo
usar a próxima fonte do vídeo atual. Ela não alimenta o histórico de pressão medida nem muda o perfil
salvo; o teste exercita uma falha real seguida de outro vídeo na fonte original.

## Provas reproduzíveis

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target mp4-opening-test yt-media-test player-state-test \
  h264-policy-test h264-cadence-test manual-quality-test yt-contract-test reddit-contract-test \
  player-extras-test player-extras-worker-test
python3 bench/opening/fixtures.py .work/opening
HOME="$PWD/.work/test-home" XDG_CACHE_HOME="$PWD/.work/test-cache" \
  client/build/mp4-opening-test "$PWD/.work/opening"
```

As duas fixtures contêm AVC sem áudio (som não é necessário para este defeito), três horas,
5.400 entradas `sidx`, payload de AVC real, 720p25 e 720p60 com metadados PQ/BT.2020.
Os arquivos esparsos têm ~2,83 GB lógicos e ocupam muito menos disco. São gerados pelo teste, sem mídia,
URL assinada, conta ou cookie de produção. A prova nativa usa RangeFile, MOV, seek e decodificação de
quadros reais; valida PTS, PQ e limite de transferências. A fixture PQ não qualifica tone mapping HDR.

A regressão de geração de código só aparece no alvo PowerPC. Instale/disponibilize `qemu-system-ppc64`
na máquina de desenvolvimento (não faz parte do produto) e use **as bibliotecas reais do build Xbox**:

```bash
python3 bench/opening/ppc.py .work/opening \
  --prefix "$PWD/.work/ffmpeg-xbox" \
  --cc /caminho/do/toolchain/bin/powerpc-xsdk-elf-gcc \
  --qemu /caminho/do/qemu-system-ppc64 --work .work/opening-ppc
```

Antes da correção, ambos falham: `size=expected_size-1`, orçamento de leituras excedido e seeks sem PTS.
Depois: tamanho exato, cinco leituras, PTS 120.000/10.000.000 ms, sem varredura de mídia.
`ppc.py` retorna status diferente de zero se qualquer caso falhar e sempre encerra seu QEMU.
`--extra-object` serve para calibrar a correção de uma unidade sobre um arquivo antigo, sem alterar
esse arquivo; a aceitação final deve rodar sem essa opção sobre o prefixo completo recém-compilado.

A bancada usa e6500 com registradores de 64 bits e MSR.SF=0, o modo relevante para esta falha de CR0.
Não emula kernel Xbox, TLS, GPU, áudio, VMX128 nem desempenho Xenon. Os stubs recusam criação de threads;
esta prova abre/demuxa pacotes e não executa codecs. Não constitui aceitação de console.

Sonda de rede opcional, poucos pedidos e espaçados:

```bash
cmake --build client/build -j2 --target mp4-opening-probe
HOME="$PWD/.work/probe-home" XDG_CACHE_HOME="$PWD/.work/probe-cache" \
  client/build/mp4-opening-probe 5PQYM_KSXag 120
# Em outra rodada:
HOME="$PWD/.work/probe-home" XDG_CACHE_HOME="$PWD/.work/probe-cache" \
  client/build/mp4-opening-probe NNS5Piu-EII 0
```

A sonda não lê conta/cache persistido. Registra apenas itags, offsets, Content-Range, bytes e tempos.
Um quarto argumento opcional grava o primeiro Range de cada faixa em um prefixo local; não versionar
mídia capturada. Ela não substitui a reprodução pelo app de PC.

## Roteiro do coordenador no console (não executado pela F46)

1. Fechar o app e instalar o `client/build-x360/tube360.xex` desta branch com o procedimento normal do
   coordenador. Guardar o log e confirmar o hash do executável. Não alterar o launcher/fontes por este teste.
2. Usar `server=off`, `sound=tv`, `video_mode=1080p30`, sem conta, e `autoplay.txt` com
   `5PQYM_KSXag 120`. Reabrir pelo launcher. Recolher o log por até 30 s.
3. Esperar `cabecalho: video resultado=0`, tamanho acima de 2 GiB, poucos pedidos, `seek_ms` limitado;
   depois áudio, `console: MP4 1080p ... itags 137/140 ... início 120.000 abertura_ms=...` e
   `primeiro quadro`. Confirmar imagem/som perto de 02:00, posição avançando e ausência de `moov atom
   not found`, `corrupted TRUN`, `error reading header`. Meta de aceitação: primeiro quadro em poucos
   segundos; registrar tempos reais, sem usar os números PC como promessa Xenon.
4. Fechar, espaçar as rodadas e substituir `autoplay.txt` por `NNS5Piu-EII 0`. Reabrir. Na oferta medida,
   DETAIL selecionou 720p60, itag 298: o título "HDR/4K" não significa que o console recebeu um codec HDR.
   Esperar os mesmos marcos; não deve haver queda de fonte por falha de cabeçalho. Uma queda posterior
   por pressão **medida** do decoder/rede é uma decisão separada e deve ter seu motivo registrado.
5. Buscar no começo/meio/fim do BBC, inclusive depois de 02:30:00 (offset acima de 2 GiB), e voltar para
   02:00. Confirmar PTS/posição e continuidade. Repetir um seek/pausa no Nature.
6. Abrir um vídeo curto conhecido após os dois e conferir o perfil e itag escolhido. Uma falha de
   abertura não pode ter mudado `video_mode` nem aplicado penalidade de desempenho ao próximo vídeo.
7. Remover `autoplay.txt` e guardar logs/tempos/itags e hash para a integração. Console, Nest real e
   publicação ficam sob responsabilidade do coordenador; esta frente não os acessou.

## Integração rc4: fronteiras fora de libavformat

A revisão adicional usa o mesmo e6500/MSR.SF=0 da prova acima, com o GCC xsdk 52:

```bash
python3 bench/opening/limits-ppc.py \
  --cc /caminho/do/toolchain/bin/powerpc-xsdk-elf-gcc \
  --qemu /caminho/do/qemu-system-ppc64 --work .work/opening-limits
```

É necessário um build Xbox já configurado em `client/build-x360`; `--build-dir`
e `--prefix` permitem escolher outro build/prefixo. Includes/defines vêm do build
real. O oráculo compila em O0, separado das unidades de produto em O2 e O3.
Os objetos de FFmpeg e o parser libcurl vêm dos arquivos estáticos Xbox reais.
A função de progresso do libcurl recompila o fonte fixado em O3 com seções por
função para remover dependências de rede não exercitadas. Todos os comandos e
resultados ficam no diretório de saída. Status diferente de zero significa falha.

- **87 verificações em O2 + 87 em O3:** `RangeFile::seek/read/fetch` reais,
  tamanho conhecido e EOF, SEEK_SET/CUR/END/SIZE, leitura dos três últimos bytes
  e Content-Range em 2–6 GiB. São janelas pequenas; não há alocação de gigabytes.
  Inclui parse/ordenação de sequência local acima de 2³¹/2³² e o decoder real de
  manifesto: 64 MiB aceito, tamanhos de 2–6 GiB/negativos/extremos recusados.
- **100 verificações FFmpeg:** rescale positivo/negativo, comparação/delta/avanço
  de timestamps, metadados de pacote (incluindo posição) e inicialização/PTS/delay
  de swresample nas mesmas fronteiras. `libavcodec`, `libavutil` e `libswresample`
  conservam suas opções. Nenhum kernel de decodificação é exercitado ou alterado.
- **53 verificações libcurl:** parser de Content-Length/offset (`numblanks` e
  `number`), progresso de download, overflow de int64 e entrada negativa.
  `https.cpp` só copia Content-Range como texto; sua conversão/validação ocorre
  no RangeFile acima. Respostas da atualização são limitadas a 64 MiB e a
  biblioteca local a 1 MiB; elas não suportam arquivos de vários GiB.

Esses caminhos passaram com combine habilitado. Nenhuma restrição adicional foi
aplicada sem falha reproduzida. Isso não certifica toda função das dependências:
a prova cobre as fronteiras e os caminhos listados, sem transporte TLS real,
agendamento, codecs ou kernel Xbox. O controle com o arquivo avformat anterior
continua falhando nos dois MP4 esparsos; a receita integrada passa sem objetos
extras, comprovando que a sonda ainda detecta o defeito original.
