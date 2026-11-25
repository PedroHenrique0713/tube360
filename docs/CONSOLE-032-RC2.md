# Console pela manhã — 0.3.2-rc2 / build 4

O dono religa o Xbox e restaura o ambiente de homebrew. O coordenador só começa
quando o console estiver disponível. Esta entrega não acessa o console nem o Nest,
não publica e não inclui bancada nova de GPU. Os testes abaixo usam o app integrado.
Não reiniciar, usar magicboot nem substituir a instalação habitual automaticamente.

## Artefatos locais

No worktree `tube360-frota/f42-integracao`:

- Público, bancadas OFF: `.work/f42/package/tube360-v0.3.2-rc2-xbox360.zip`
  e `.7z`, payload `tube360.xex` e `manifest.json`. Launcher em `default.xex`.
- Pasta pública extraída: `.work/f42/console/Tube360/`.
- Bancada F40 ON, separada: `.work/f42/bench-console/`, com manifesto SHA-256,
  casos `tv137`, `tv137-1080`, `motion137`, `full137` e `nest137`.
  Esse kit executa `tube360.xex` diretamente. Não substitui a candidata pública.
- Provas PC/build e relatório: `.work/f42/`. Os números de PC e os números herdados
  da F39 são identificados separadamente; nenhum deles aprova playback no Xbox.

## Política e mapa final

H.264 com 1080 linhas e taxa conhecida até 30,1 fps usa seis workers FRAME,
ordem `0,2,4,1,3,5`, prioridade solicitada 0 e loop filter ALL (48), definido antes
de abrir o decoder. O mesmo vale em Automático e após reabrir por seek/troca de fonte.
`h264_loop_filter=full` em `settings.txt` e reinício restauram o filtro completo (0),
mantendo seis workers no perfil. Fontes menores, altas ou de taxa desconhecida
mantêm o filtro completo; não há troca de filtro no meio do GOP.

Pares físicos: HW0/1, HW2/3 e HW4/5. Valores abaixo são prioridades solicitadas;
os logs devem confirmar o resultado efetivo do kernel.

| HW | Papéis e prioridade |
|---|---|
| 0 | UI/apresentação +2; AVC #1 em 0 |
| 1 | Demux/AAC/PCM +1; feeder SDL observado em `app-audio`; AVC #4 em 0 |
| 2 | AVC #2 em 0; Music AAC +1 durante áudio sem vídeo |
| 3 | Range/HLS/playlists TLS +1; AVC #5 em 0 |
| 4 | AVC #3 em 0 |
| 5 | AVC #6 em 0; Nest/Cast v2 e controle Nest +1; lounge/DIAL/conta/timer em 0; catálogo/miniaturas/legendas/comentários/descoberta/atualizações em −1; SponsorBlock em −1 com boost +3 só durante TLS, depois restaura −1 |

Não há HW exclusivo do SponsorBlock neste perfil. O boost temporário preservado
da F34 preempta vídeo/fundo no HW5 sem alterar a afinidade desses workers. Medir
no console a latência TLS, a restauração e a cadência durante a consulta.
`app-thread`, `decod-worker`, `decod-priority`, `app-audio` e `SponsorBlock:`
registram pedidos, status e consultas de prioridade. Helpers MPEG-1 dormem durante AVC.

A fila 1080p é limitada pelo orçamento de memória. A escada preserva os limites
manuais da F32, backoff e custo maior observado na sessão; o piso para a projeção
1080p30 é 22,567982 ms da F39, decoder isolado. Não é medição do app integrado.
Nest salvo/clock filtrado F25b, placas/safe/CRT e todas as traduções árabes da F41
continuam presentes. F40 não introduziu chaves de interface para traduzir; a opção
do filtro é por arquivo. Revisão nativa do árabe continua pendente.

## 1. Smoke público e recursos F41

Copiar a pasta pública completa para uma pasta temporária nova, como
`HDD:\Apps\Tube360-F42-rc2`, e abrir `default.xex`. Manter os dois XEX, oito fontes
e licenças. Não copiar conta/tokens para o notebook. Para a conta real, o dono
opera no console e preserva o grant existente; os testes locais usam conta fictícia.

Seguir também o [roteiro F41](CONSOLE-032-RC1.md) para launcher/rollback, placas,
CRT, diagnóstico de boot, Música, qualidade manual e árabe. Conferir em especial:

1. Boot, home e B; configurações repetidas e troca dos quatro idiomas, incluindo
   árabe, texto grande/4:3 e oito fontes presentes.
2. Qualidade: Y abre o menu quando não há ação SponsorBlock. 480–144p mantêm
   fonte/posição/pausa; Automático restaura a escada. LT/RT, seek e relógio do celular.
3. SponsorBlock Off/botão/automático; Y pula/volta; uma consulta paralela, sem
   bloquear o primeiro quadro; TLS ≤2 s sob decode e prioridade anterior restaurada.
4. Música da conta e fallback público indicado; nenhuma nova autenticação no PC.
5. Nest salvo, RTT/clock filtrado e retorno à TV; só iniciar quando a caixa estiver
   disponível. Sair por B e confirmar STOP antes de remover qualquer pasta/journal.

## 2. 1080p30 com a bancada do app

O kit inclui `mudo.txt` em todos os quatro casos de TV, inclusive `tv137`,
`motion137` e `full137`. A rodada inicial pode ser muda: mantém PCM/clock e
retorna `console_gate=pending_audio`. Com o app fechado, remover o arquivo
**da cópia local e da pasta temporária do Xbox** para repetir com som e avaliar
continuidade/sincronia labial. Remover só a cópia local não apaga o arquivo remoto.
Não confundir `DONE PASS` técnico com aceite completo; conferir o gate recomputado.

Executar cada caso uma vez, com pausas entre consultas públicas. Exemplo a ser
rodado somente pelo coordenador após a liberação do console:

```bash
cd /home/pedrohenriquequadro/Documentos/VS/tube360-frota/f42-integracao
F42_XBDM=/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py
F42_IP=192.168.101.39 # conferir o IP atual com o dono
F42_CASE=tv137
F42_RUN=mudo # usar som na repetição sem mudo.txt
F42_LOCAL="$PWD/.work/f42/bench-console/$F42_CASE"
F42_REMOTE="HDD:\\Apps\\Tube360-F42-$F42_CASE"
python3 "$F42_XBDM" "$F42_IP" mkdir "$F42_REMOTE"
for file in "$F42_LOCAL"/*; do
  python3 "$F42_XBDM" "$F42_IP" put "$file" "$F42_REMOTE\\${file##*/}"
done
python3 "$F42_XBDM" "$F42_IP" run "$F42_REMOTE\\tube360.xex"
# Esperar saída normal: abertura + 10 s aquecimento + 120 s medidos.
# Timeout da sonda: 220 s. Não reiniciar se travar; preservar os logs.
python3 "$F42_XBDM" "$F42_IP" get "$F42_REMOTE\\tube360.log" ".work/f42/xbox-$F42_CASE-$F42_RUN.txt"
python3 bench/1080p-app/summarize.py ".work/f42/xbox-$F42_CASE-$F42_RUN.txt" ".work/f42/xbox-$F42_CASE-$F42_RUN.json"
```

Para repetir com som, depois de guardar o log mudo e fechar o app:

```bash
rm -- "$F42_LOCAL/mudo.txt"
python3 "$F42_XBDM" "$F42_IP" rm "$F42_REMOTE\\mudo.txt"
F42_RUN=som
python3 "$F42_XBDM" "$F42_IP" run "$F42_REMOTE\\tube360.xex"
# Repetir get + summarize acima; os logs mudo e som ficam separados.
```

Ordem e critérios:

1. `tv137`: futebol `4p_9OPKgcDE`, início 31 s, fonte 137/1920×1080/~30,
   saída 720p. Exigir seis FRAME/spread/ALL, fonte mantida, ≥29,5 fps, zero perdas,
   A/V p95 ≤50 ms, livres amostrados ≥64 MiB, nenhum silêncio do feeder após
   aquecimento, status de afinidade/prioridade válidos e som contínuo após tirar
   `mudo.txt`. Com som, 120 s medidos e 10 s de aquecimento, exigir gate `pass`.
2. `tv137-1080`: mesma fonte com HDMI **1080p progressivo no painel do Xbox** e
   `output_height=1080`. Exigir framebuffer 1920×1080 e `display: 1920x1080 shown 1:1`;
   comparar cadência, memória e imagem inteira. Mudar só o app não muda o sinal HDMI.
3. `motion137`: `83OlZMzqQJc`, 480 s. Conferir ação contínua; se houver menu/cutscene,
   escolher trecho movimentado, registrar o início e medir novamente. Mesmo limite
   do futebol; esse candidato não foi certificado como pior caso universal.
4. `full137`: controle com filtro completo/loop 0, seis workers. Registrar qualidade
   visual, blocos/nitidez e eventual descida da escada. Queda é resultado do controle,
   não aceite ALL. Na fonte 720p seguinte esperar quatro workers/filtro completo.
5. `nest137`: somente com caixa disponível e autorização do dono. Criar `nest.txt`
   **apenas na pasta temporária**, usando o endereço autorizado; o exemplo não é lido.
   Não há mute de TV no kit Nest. Exigir rota CAST, TV inaudível, status/clock válidos,
   mesma fonte/FPS/perdas/memória e conferir sincronismo ouvindo. A sonda F40 ainda
   marca `pending_f25b_clock`; o código F25b está integrado, mas o gate Nest não foi
   promovido a certificação com base no PC. Registrar validade/RTT/idade no log.

Repetir B, pausa, seek, Y, undo SponsorBlock e troca de qualidade sem
`f40-playback.txt` para evitar a saída automática da sonda. No controle FULL,
deixar a escada agir e registrar o resultado. Restaurar painel e app ao terminar.
Nenhum ensaio de shader/GPU novo faz parte desta rodada.

## 3. Encerramento

Sair normalmente por B; esperar STOP/silêncio no Nest. Não usar magicboot para
encerrar nem apagar `nest-owner.json` com STOP pendente. Guardar logs, resumos,
hashes e resultados visuais/acústicos antes de limpar só as pastas temporárias.
Reprovação de FPS, memória, fonte, filtro, som ou afinidade fica registrada; não
alterar os critérios para aprovar. Falta de aceite físico mantém rc2 local.
