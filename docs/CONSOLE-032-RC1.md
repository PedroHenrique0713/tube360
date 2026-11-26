# Roteiro do coordenador — 0.3.2-rc1

Candidata local na branch `f41-integracao`. Nenhum destes comandos foi executado no Xbox pela F41.
A F40/1080p30 no app **não está integrada**. Não usar este roteiro como prova de 1080p30 sustentado.
A pasta local preparada fica em `.work/f41/console/`; o pacote completo em `.work/f41/package/`.

## Preparação pelo XBDM

Fechar normalmente o Tube360 antes de enviar. Usar uma pasta nova, preservando a instalação diária.
`HDD:` é o nome testado para escrita; `Hdd1:` pode listar mas negar gravação.

```bash
cd <repo>
F41_XBDM=<forcepush-360>/tools/xbdm.py
F41_IP=192.168.1.50
F41_REMOTE='HDD:\Games\Tube360-032-RC1'
mkdir -p .work/f41/console-logs
python3 "$F41_XBDM" "$F41_IP" mkdir "$F41_REMOTE"
for F41_FILE in .work/f41/console/*; do
  [ -f "$F41_FILE" ] || continue
  python3 "$F41_XBDM" "$F41_IP" put "$F41_FILE" "$F41_REMOTE\\${F41_FILE##*/}"
done
python3 "$F41_XBDM" "$F41_IP" run "$F41_REMOTE\\default.xex"
```

Baseline: `server=off`, som TV, Automático, SponsorBlock desligado; sem conta/Nest/board-sim.
Oito fontes acompanham os dois XEX. O lançador é `default.xex`; o app é `tube360.xex`.
No boot, exigir `model=Corona path=native` no console do dono (ou registrar o modelo efetivamente lido),
`end app.first-frame`, `end frame.present`, fontes carregadas e ausência de falha/retorno ao Aurora.
Arquivar os logs **antes de cada nova abertura**, pois há arquivos truncados no boot:

```bash
for F41_LOG in tube360.log boot-app.log boot-launcher.log boot-early.log startup.log; do
  python3 "$F41_XBDM" "$F41_IP" get "$F41_REMOTE\\$F41_LOG" ".work/f41/console-logs/baseline-$F41_LOG"
done
```

Alguns nomes só existem depois de uma falha/etapa; a ausência deve ser registrada, sem confundir com sucesso.
Ver [diagnóstico de início](STARTUP-DIAGNOSTICS.md). Para UDP, configurar `net_log.txt` só nesta cópia
com o IP/porta do notebook e usar o receptor `tests/update/udp-log.py` em porta livre.

## Autoplay e abertura

`autoplay.txt` tem `VIDEO_ID segundos`, uma linha, e é lido a cada abertura. Para trocar o cenário,
fechar pelo app, enviar settings/autoplay e reabrir. Exemplo:

```bash
printf '4p_9OPKgcDE 0\n' > .work/f41/autoplay-quality.txt
python3 "$F41_XBDM" "$F41_IP" put .work/f41/autoplay-quality.txt "$F41_REMOTE\\autoplay.txt"
python3 "$F41_XBDM" "$F41_IP" run "$F41_REMOTE\\default.xex"
```

1. **Qualidade automática:** deixar três minutos, depois repetir um vídeo leve 720p30. Procurar
   `qualidade: selecao ... fixa=0`, assentamento inicial, descida de um degrau por decisão,
   previsão de decode antes da subida, backoff e memória do degrau recusado depois de seek.
   Não deve insistir a cada 20 s em 1080 que a previsão já recusou. Guardar `console: player OK`,
   `console: MP4 ... abertura_ms`, primeiro quadro e linhas agregadas de qualidade.
2. **Nest salvo:** com o app fechado, copiar o `nest.txt` da instalação normal **dentro do console**,
   sem copiar conta. Enviar `.work/f41/profiles/nest/settings.txt` e autoplay `nuvwZLCNJCI 30`.
   Exigir conexão ao IP salvo sem varredura no caso saudável, som físico na caixa, cadência perto
   de 30 fps depois da primeira janela e sincronismo boca/som por 90 s. Pausa/seek precisam
   restabelecer a âncora. O log não substitui a gravação de TV e caixa.
3. **Nest indisponível:** coordenador desliga/isola a caixa e repete desde a abertura. Exigir retorno
   à TV antes de 10 s. Em perda durante reprodução, preservar a tolerância existente de ~25 s e
   o prazo adicional de liberação. Ver [casos completos](../tests/nest/README_F25b.md) e
   [jitter](../tests/nest/README_JITTER.md). Encerrar com B/View e aguardar STOP; não matar o título
   pelo XBDM durante Cast. Se a caixa continuar tocando, confirmar STOP pelo Google Home antes
   de remover a cópia/journal.
4. **SponsorBlock:** enviar perfil `sponsor-button` + autoplay `wWC8wVI4E3E 0`.
   Log: tentativa 1 com HW5, prioridade efetiva 3 em TLS e restauração ao fim; HTTP 200,
   `dados_ok=1`, um trecho e meta de dados em até 2000 ms desde a escolha. O trecho conhecido da
   fixture é 7,775–20,877 s; dados públicos podem mudar. Verificar fases DNS/TCP/TLS e tempos,
   sem interpretar `-1` como duração negativa. Repetir com `sponsor-off` e `sponsor-auto`, mesma
   posição e qualidade. O primeiro quadro não deve esperar a consulta. Mais detalhes no
   [roteiro TLS](../bench/sponsorblock/tls/CONSOLE.md).
5. **Placas:** começar em HD sem simulação. Depois, em pastas temporárias separadas, aplicar
   exemplos de `.work/f41/profiles/board-*`. Exigir modelo físico e perfil simulado separados,
   safe sem mapped, native com gate real, logs de saída/fontes completos e B funcionando em falha
   injetada. `board-sim.txt` não cria o sinal de um CRT nem prova Jasper/Falcon físicos.
6. **SD/CRT:** em Corona Slim/RGH com componente e painel já em 480i, testar native e safe;
   em E usar conector AV suportado. Exigir viewport do kernel e dimensões iguais em surface,
   swapchain e present. Pedido de 1080 não deve recriar a saída fixa. Fotografar corte, margens,
   texto grande e qualidade. O vídeo deve ocupar toda a largura física em SD/1080, com aspecto e
   letterbox corretos; a escala e as margens de menu são independentes. FAT, conector, exploit e
   480i físicos exigem seus próprios consoles.
7. **Árabe:** perfil `arabic`; conferir título/lista/busca, RB árabe↔latim, B, dígitos mistos,
   fontes Noto sob demanda, 4:3/texto grande e volta ao português. Registrar o custo da primeira
   carga e a cadência depois dela. Tradução ainda requer revisão nativa; termos novos de
   SponsorBlock, qualidade e Música estão marcados em `docs/ARABIC.md`.
8. **Erros e lançador:** em outra cópia, remover uma Inter, abrir `default.xex`: motivo legível
   e B saindo; restaurar a fonte. Testar data/hora incorreta apenas em ambiente em que o dono
   autorize mudar o relógio, confirmando `tls_certificate_date` e UTC. Não desligar verificação TLS.
   Atualizações/rollback são exercitados pelo kit descartável da F26; nunca servir payload falso
   à pasta de uso. O pacote público usa apenas a chave pública de produção.

Para cada rodada, registrar vídeo/tempo/qualidade/modelo/driver/conector, os dois hashes XEX do
manifesto e logs com prefixo do cenário. Retirar `autoplay.txt`/board-sim da cópia ao navegar manualmente.
Não publicar a candidata nem os logs que contenham dados de conta.

## Só o dono confere com o controle

- **Música da conta:** copiar `yt-console-account.json` somente dentro do console, com o app fechado.
  Conferir `music=tv`/`music=web`, rótulo de conta, recentes, playlists, curtidas, paginação e áudio
  original. Comparar recomendações com a conta no celular. `music=public` não aprova a conta.
  Não usar Sair da conta na cópia: revoga o grant compartilhado. Se houver refresh, preservar o
  arquivo novo na instalação normal por cópia interna antes da limpeza. [Procedimento](MUSIC-ACCOUNT.md).
- **Configurações 20 vezes:** alternar saída/texto grande/idioma/fit; conferir navegação, imagem,
  ausência de travamento e pares de logs GPU idle/reset/upload/retire. Safe mantém a saída fixa.
- **Y de qualidade:** fora de trecho SponsorBlock, abrir lista, escolher 480/360/240/144 oferecidos,
  manter pausa/posição, B cancelar, erro/aviso sem fallback silencioso e retorno a Automático.
- **SponsorBlock:** botão no trecho, automático, Y para voltar sem repetir o salto, Y fora da ação
  abrindo Qualidade; Off sem consulta. Ao transmitir pelo celular, conferir que o relógio dele
  acompanha o retorno com Y. Conferir aviso/crédito e B saindo um nível por vez.

Ao terminar: B até sair da reprodução, confirmar silêncio do Nest e fechar normalmente. Guardar logs,
conservar eventual refresh da conta internamente e apagar só as pastas temporárias deste teste.
